#include "sdkconfig.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "board.h"
#include "config.h"
#include "display/display.h"
#include "ssid_manager.h"
#include "wifi_manager.h"

namespace {

constexpr char kTag[] = "ZectrixBoard";

constexpr uint8_t kFixedTemperatureCompensation = 244;

class ZectrixEpaperDisplay : public Display {
public:
    ZectrixEpaperDisplay() {
        width_ = QUELLOG_DISPLAY_WIDTH;
        height_ = QUELLOG_DISPLAY_HEIGHT;
        bytes_per_row_ = (width_ + 7) >> 3;
        framebuffer_.assign(static_cast<size_t>(bytes_per_row_ * height_), 0xFF);
        previous_framebuffer_.assign(framebuffer_.size(), 0xFF);
        tx_line_.assign(static_cast<size_t>(bytes_per_row_ * 2), 0xFF);
        InitializeHardware();
    }

    ~ZectrixEpaperDisplay() override {
        if (spi_ != nullptr) {
            spi_bus_remove_device(spi_);
            spi_ = nullptr;
        }
        if (spi_bus_initialized_) {
            const esp_err_t ret = spi_bus_free(QUELLOG_EPD_SPI_HOST);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(kTag, "spi bus free failed: %s", esp_err_to_name(ret));
            }
        }
    }

    void BeginPage() override {
        Clear(true);
    }

    void EndPage() override {
        if (!hardware_ready_) {
            ESP_LOGE(kTag, "epd hardware unavailable");
            return;
        }

        const bool do_partial = partial_refresh_requested_ && has_displayed_once_;
        ESP_LOGI(kTag, "end page, refresh=%s", do_partial ? "partial" : "full");
        InitializePanel();
        if (do_partial) {
            DisplayPartial();
        } else {
            DisplayFull();
        }

        previous_framebuffer_ = framebuffer_;
        has_displayed_once_ = true;
        partial_refresh_requested_ = false;
    }

    void Clear(bool white = true) override {
        std::fill(framebuffer_.begin(), framebuffer_.end(), white ? 0xFFU : 0x00U);
    }

    void SetPixel(int x, int y, bool black) override {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }

        const size_t index = static_cast<size_t>(y * bytes_per_row_ + (x >> 3));
        const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x & 0x07)));
        if (black) {
            framebuffer_[index] &= static_cast<uint8_t>(~mask);
        } else {
            framebuffer_[index] |= mask;
        }
    }

    void RequestFullRefresh() override {
        partial_refresh_requested_ = false;
        ESP_LOGI(kTag, "full refresh requested");
    }

    void RequestPartialRefresh() override {
        partial_refresh_requested_ = true;
        ESP_LOGI(kTag, "partial refresh requested");
    }

private:
    void InitializeHardware() {
        ConfigurePowerPin();
        ConfigureControlPins();
        ConfigureSpi();
    }

    void ConfigurePowerPin() {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << QUELLOG_EPD_POWER_GPIO;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_hold_dis(QUELLOG_EPD_POWER_GPIO);
        gpio_set_level(QUELLOG_EPD_POWER_GPIO, 0);
        gpio_hold_en(QUELLOG_EPD_POWER_GPIO);
    }

    void ConfigureControlPins() {
        gpio_config_t output_cfg = {};
        output_cfg.pin_bit_mask =
            (1ULL << QUELLOG_EPD_RST_GPIO) |
            (1ULL << QUELLOG_EPD_DC_GPIO) |
            (1ULL << QUELLOG_EPD_CS_GPIO);
        output_cfg.mode = GPIO_MODE_OUTPUT;
        output_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        output_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        output_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&output_cfg));

        gpio_config_t input_cfg = {};
        input_cfg.pin_bit_mask = 1ULL << QUELLOG_EPD_BUSY_GPIO;
        input_cfg.mode = GPIO_MODE_INPUT;
        input_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        input_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        input_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&input_cfg));

        gpio_set_level(QUELLOG_EPD_RST_GPIO, 1);
        gpio_set_level(QUELLOG_EPD_DC_GPIO, 1);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 1);
    }

    void ConfigureSpi() {
        if (spi_ != nullptr) {
            ESP_ERROR_CHECK(spi_bus_remove_device(spi_));
            spi_ = nullptr;
        }
        if (spi_bus_initialized_) {
            const esp_err_t ret = spi_bus_free(QUELLOG_EPD_SPI_HOST);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_ERROR_CHECK(ret);
            }
            spi_bus_initialized_ = false;
        }

        spi_bus_config_t buscfg = {};
        buscfg.miso_io_num = -1;
        buscfg.mosi_io_num = QUELLOG_EPD_MOSI_GPIO;
        buscfg.sclk_io_num = QUELLOG_EPD_SCK_GPIO;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = static_cast<int>(tx_line_.size());

        spi_device_interface_config_t devcfg = {};
        devcfg.spics_io_num = -1;
        devcfg.clock_speed_hz = 40 * 1000 * 1000;
        devcfg.mode = 0;
        devcfg.queue_size = 1;

        ESP_ERROR_CHECK(spi_bus_initialize(QUELLOG_EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
        spi_bus_initialized_ = true;
        ESP_ERROR_CHECK(spi_bus_add_device(QUELLOG_EPD_SPI_HOST, &devcfg, &spi_));
        hardware_ready_ = true;
    }

    void ReconfigureSpiForRx() {
        if (spi_ != nullptr) {
            ESP_ERROR_CHECK(spi_bus_remove_device(spi_));
            spi_ = nullptr;
        }
        if (spi_bus_initialized_) {
            const esp_err_t ret = spi_bus_free(QUELLOG_EPD_SPI_HOST);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_ERROR_CHECK(ret);
            }
            spi_bus_initialized_ = false;
        }

        spi_bus_config_t buscfg = {};
        buscfg.miso_io_num = QUELLOG_EPD_MOSI_GPIO;
        buscfg.mosi_io_num = -1;
        buscfg.sclk_io_num = QUELLOG_EPD_SCK_GPIO;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = static_cast<int>(tx_line_.size());

        spi_device_interface_config_t devcfg = {};
        devcfg.spics_io_num = -1;
        devcfg.clock_speed_hz = 8 * 1000 * 1000;
        devcfg.mode = 0;
        devcfg.queue_size = 1;

        ESP_ERROR_CHECK(spi_bus_initialize(QUELLOG_EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
        spi_bus_initialized_ = true;
        ESP_ERROR_CHECK(spi_bus_add_device(QUELLOG_EPD_SPI_HOST, &devcfg, &spi_));
    }

    void PowerOn() {
        gpio_hold_dis(QUELLOG_EPD_POWER_GPIO);
        gpio_set_level(QUELLOG_EPD_POWER_GPIO, 1);
        gpio_hold_en(QUELLOG_EPD_POWER_GPIO);
    }

    void PowerOff() {
        gpio_hold_dis(QUELLOG_EPD_POWER_GPIO);
        gpio_set_level(QUELLOG_EPD_POWER_GPIO, 0);
        gpio_hold_en(QUELLOG_EPD_POWER_GPIO);
    }

    void HardwareReset() {
        gpio_set_level(QUELLOG_EPD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(QUELLOG_EPD_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(QUELLOG_EPD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    void WaitBusy() {
        const gpio_num_t busy_gpio = QUELLOG_EPD_BUSY_GPIO;
        int attempts = 0;
        while (gpio_get_level(busy_gpio) == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            if (++attempts > 400) {
                ESP_LOGW(kTag, "epd busy wait timed out, gpio=%d level=%d", static_cast<int>(busy_gpio), gpio_get_level(busy_gpio));
                break;
            }
        }
    }

    void SendCommand(uint8_t command) {
        gpio_set_level(QUELLOG_EPD_DC_GPIO, 0);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 0);
        SpiTransmit(&command, 1);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 1);
    }

    void SendData(uint8_t data) {
        WriteBytes(&data, 1);
    }

    void WriteBytes(const uint8_t* data, size_t len) {
        gpio_set_level(QUELLOG_EPD_DC_GPIO, 1);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 0);
        SpiTransmit(data, len);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 1);
    }

    void SpiTransmit(const uint8_t* data, size_t len) {
        assert(spi_ != nullptr);
        spi_transaction_t t = {};
        t.length = static_cast<size_t>(len * 8);
        t.tx_buffer = data;
        ESP_ERROR_CHECK(spi_device_polling_transmit(spi_, &t));
    }

    uint8_t SpiReceiveByte() {
        assert(spi_ != nullptr);
        uint8_t data = 0;
        spi_transaction_t t = {};
        t.length = 8;
        t.rx_buffer = &data;
        ESP_ERROR_CHECK(spi_device_polling_transmit(spi_, &t));
        return data;
    }

    uint8_t ReceiveData() {
        ReconfigureSpiForRx();
        gpio_set_level(QUELLOG_EPD_DC_GPIO, 1);
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 0);
        const uint8_t value = SpiReceiveByte();
        gpio_set_level(QUELLOG_EPD_CS_GPIO, 1);
        ConfigureSpi();
        return value;
    }

    uint8_t ReadTemperatureCompensation() {
        SendCommand(0x40);
        WaitBusy();
        const uint8_t raw = ReceiveData();
        ESP_LOGI(kTag, "temperature register raw=%u", raw);
        if (raw <= 5) {
            return 232;
        }
        if (raw <= 10) {
            return 235;
        }
        if (raw <= 20) {
            return 238;
        }
        if (raw <= 30) {
            return 241;
        }
        if (raw <= 127) {
            return 244;
        }
        return kFixedTemperatureCompensation;
    }

    void InitializePanel() {
        ESP_LOGI(kTag, "initialize panel begin");
        PowerOn();
        vTaskDelay(pdMS_TO_TICKS(10));
        HardwareReset();
        WaitBusy();

        SendCommand(0x00);
        SendData(0x2F);
        SendData(0x2E);

        SendCommand(0xE9);
        SendData(0x01);
        WaitBusy();
        ESP_LOGI(kTag, "initialize panel end");
    }

    void TurnOnDisplay() {
        SendCommand(0x04);
        WaitBusy();

        SendCommand(0x12);
        SendData(0x00);
        WaitBusy();

        SendCommand(0x02);
        SendData(0x00);
        WaitBusy();
        PowerOff();
    }

    static void Pack1bppToPanel(uint8_t in, uint8_t& out0, uint8_t& out1) {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        for (uint8_t i = 0; i < 8; ++i) {
            const uint8_t bit = static_cast<uint8_t>((in >> (7 - i)) & 0x01U);
            if (i < 4) {
                b0 |= static_cast<uint8_t>(bit << (6 - (2 * i)));
            } else {
                b1 |= static_cast<uint8_t>(bit << (14 - (2 * i)));
            }
        }
        out0 = b0;
        out1 = b1;
    }

    void DisplayFull() {
        ESP_LOGI(kTag, "display full start");
        const uint8_t temperature_compensation = ReadTemperatureCompensation();
        SendCommand(0xE0);
        SendData(0x02);
        SendCommand(0xE6);
        SendData(temperature_compensation);
        SendCommand(0xA5);
        WaitBusy();
        vTaskDelay(pdMS_TO_TICKS(10));

        SendCommand(0x10);
        for (int y = 0; y < height_; ++y) {
            const uint8_t* src = framebuffer_.data() + (y * bytes_per_row_);
            uint8_t* dst = tx_line_.data();
            for (int xb = 0; xb < bytes_per_row_; ++xb) {
                uint8_t o0 = 0;
                uint8_t o1 = 0;
                Pack1bppToPanel(src[xb], o0, o1);
                *dst++ = o0;
                *dst++ = o1;
            }
            WriteBytes(tx_line_.data(), tx_line_.size());
        }
        TurnOnDisplay();
        ESP_LOGI(kTag, "display full end");
    }

    static uint16_t InterleaveBytes(uint8_t previous, uint8_t current) {
        uint16_t result = 0;
        for (int k = 0; k < 8; ++k) {
            const int src_bit = 7 - k;
            const int dst_even = 2 * src_bit;
            const int dst_odd = dst_even + 1;
            result |= static_cast<uint16_t>(((previous >> src_bit) & 1U) << dst_odd);
            result |= static_cast<uint16_t>(((current >> src_bit) & 1U) << dst_even);
        }
        return result;
    }

    void DisplayPartial() {
        ESP_LOGI(kTag, "display partial start");
        SendCommand(0x10);
        WaitBusy();

        for (int y = 0; y < height_; ++y) {
            const uint8_t* prev = previous_framebuffer_.data() + (y * bytes_per_row_);
            const uint8_t* curr = framebuffer_.data() + (y * bytes_per_row_);
            uint8_t* dst = tx_line_.data();
            for (int xb = 0; xb < bytes_per_row_; ++xb) {
                const uint16_t packed = InterleaveBytes(prev[xb], curr[xb]);
                *dst++ = static_cast<uint8_t>(packed >> 8);
                *dst++ = static_cast<uint8_t>(packed & 0xFF);
            }
            WriteBytes(tx_line_.data(), tx_line_.size());
        }
        TurnOnDisplay();
        ESP_LOGI(kTag, "display partial end");
    }

    int bytes_per_row_ = 0;
    spi_device_handle_t spi_ = nullptr;
    bool spi_bus_initialized_ = false;
    bool hardware_ready_ = false;
    bool partial_refresh_requested_ = false;
    bool has_displayed_once_ = false;
    std::vector<uint8_t> framebuffer_;
    std::vector<uint8_t> previous_framebuffer_;
    std::vector<uint8_t> tx_line_;
};

struct ButtonState {
    gpio_num_t gpio = GPIO_NUM_NC;
    InputKey key = InputKey::None;
    int level = 1;
    int64_t last_change_us = 0;
};

class ZectrixBoard : public Board {
public:
    ZectrixBoard() {
        ConfigureButton(buttons_[0], static_cast<gpio_num_t>(CONFIG_QUELLOG_BUTTON_UP_GPIO), InputKey::Up);
        ConfigureButton(buttons_[1], static_cast<gpio_num_t>(CONFIG_QUELLOG_BUTTON_DOWN_GPIO), InputKey::Down);
        ConfigureButton(buttons_[2], static_cast<gpio_num_t>(CONFIG_QUELLOG_BUTTON_CONFIRM_GPIO), InputKey::Confirm);
    }

    std::string GetBoardType() override {
        return "zectrix-s3-epaper-4.2";
    }

    Display* GetDisplay() override {
        return &display_;
    }

    void StartNetwork() override {
        if (network_started_) {
            return;
        }

        WifiManagerConfig config;
        config.ssid_prefix = "Quellog";
        config.ap_password = "";
        config.language = "zh-CN";
        config.station_scan_interval_seconds = 15;
        if (!WifiManager::GetInstance().Initialize(config)) {
            ESP_LOGE(kTag, "wifi manager init failed");
            network_state_.store(NetworkState::Disconnected, std::memory_order_release);
            return;
        }

        WifiManager::GetInstance().SetEventCallback([this](WifiEvent event) {
            if (!network_event_callback_) {
                switch (event) {
                    case WifiEvent::Scanning:
                        network_state_.store(NetworkState::Scanning, std::memory_order_release);
                        return;
                    case WifiEvent::Connecting:
                        network_state_.store(NetworkState::Connecting, std::memory_order_release);
                        return;
                    case WifiEvent::Connected:
                        network_state_.store(NetworkState::Connected, std::memory_order_release);
                        return;
                    case WifiEvent::Disconnected:
                        network_state_.store(NetworkState::Disconnected, std::memory_order_release);
                        return;
                    case WifiEvent::ConfigModeEnter:
                        network_state_.store(NetworkState::ConfigMode, std::memory_order_release);
                        return;
                    case WifiEvent::ConfigModeExit:
                        network_state_.store(NetworkState::Disconnected, std::memory_order_release);
                        return;
                }
            }

            switch (event) {
                case WifiEvent::Scanning:
                    network_state_.store(NetworkState::Scanning, std::memory_order_release);
                    network_event_callback_(NetworkEvent::Scanning, "");
                    break;
                case WifiEvent::Connecting:
                    network_state_.store(NetworkState::Connecting, std::memory_order_release);
                    network_event_callback_(NetworkEvent::Connecting, WifiManager::GetInstance().GetSsid());
                    break;
                case WifiEvent::Connected:
                    network_state_.store(NetworkState::Connected, std::memory_order_release);
                    network_event_callback_(NetworkEvent::Connected, WifiManager::GetInstance().GetIpAddress());
                    break;
                case WifiEvent::Disconnected:
                    network_state_.store(NetworkState::Disconnected, std::memory_order_release);
                    network_event_callback_(NetworkEvent::Disconnected, "");
                    break;
                case WifiEvent::ConfigModeEnter:
                    network_state_.store(NetworkState::ConfigMode, std::memory_order_release);
                    network_event_callback_(
                        NetworkEvent::WifiConfigModeEnter,
                        WifiManager::GetInstance().GetApSsid() + " " + WifiManager::GetInstance().GetApWebUrl());
                    break;
                case WifiEvent::ConfigModeExit:
                    network_state_.store(NetworkState::Disconnected, std::memory_order_release);
                    network_event_callback_(NetworkEvent::WifiConfigModeExit, "");
                    break;
            }
        });

        if (SsidManager::GetInstance().GetSsidList().empty()) {
            network_state_.store(NetworkState::ConfigMode, std::memory_order_release);
            WifiManager::GetInstance().StartConfigAp();
        } else {
            network_state_.store(NetworkState::Connecting, std::memory_order_release);
            WifiManager::GetInstance().StartStation();
        }
        network_started_ = true;
    }

    bool PollInput(InputEvent& event) override {
        const int64_t now_us = esp_timer_get_time();
        for (ButtonState& button : buttons_) {
            if (button.gpio == GPIO_NUM_NC) {
                continue;
            }

            const int level = gpio_get_level(button.gpio);
            if (level != button.level) {
                button.level = level;
                button.last_change_us = now_us;
                continue;
            }

            if (level == 0 &&
                button.last_change_us != 0 &&
                now_us - button.last_change_us >= static_cast<int64_t>(CONFIG_QUELLOG_INPUT_DEBOUNCE_MS) * 1000LL) {
                button.last_change_us = 0;
                event.key = button.key;
                return true;
            }
        }
        return false;
    }

    void EnterWifiConfigMode() override {
        if (!network_started_) {
            StartNetwork();
            return;
        }
        WifiManager::GetInstance().StartConfigAp();
    }

    bool IsWifiConnected() const override {
        return WifiManager::GetInstance().IsConnected();
    }

    bool IsWifiConfigMode() const override {
        return WifiManager::GetInstance().IsConfigMode();
    }

    NetworkState GetNetworkState() const override {
        return network_state_.load(std::memory_order_acquire);
    }

    std::string GetWifiSsid() const override {
        return WifiManager::GetInstance().GetSsid();
    }

    std::string GetWifiIpAddress() const override {
        return WifiManager::GetInstance().GetIpAddress();
    }

    std::string GetWifiConfigApSsid() const override {
        return WifiManager::GetInstance().GetApSsid();
    }

    std::string GetWifiConfigApUrl() const override {
        return WifiManager::GetInstance().GetApWebUrl();
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        network_event_callback_ = std::move(callback);
    }

private:
    void ConfigureButton(ButtonState& state, gpio_num_t gpio, InputKey key) {
        state.gpio = gpio;
        state.key = key;
        if (gpio == GPIO_NUM_NC) {
            return;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << gpio;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        state.level = gpio_get_level(gpio);
        ESP_LOGI(kTag, "button gpio=%d mapped", static_cast<int>(gpio));
    }

    ZectrixEpaperDisplay display_;
    std::array<ButtonState, 3> buttons_ = {};
    std::atomic<NetworkState> network_state_{NetworkState::Unknown};
    NetworkEventCallback network_event_callback_;
    bool network_started_ = false;
};

}  // namespace

DECLARE_BOARD(ZectrixBoard);

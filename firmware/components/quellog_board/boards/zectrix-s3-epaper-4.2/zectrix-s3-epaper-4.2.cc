#include "sdkconfig.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif
#include <esp_adc/adc_oneshot.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "board.h"
#include "charge_status.h"
#include "config.h"
#include "display/display.h"
#include "settings.h"
#include "ssid_manager.h"
#include "wifi_manager.h"

namespace {

constexpr char kTag[] = "ZectrixBoard";

constexpr uint8_t kFixedTemperatureCompensation = 244;
constexpr int64_t kOpenSettingsLongPressUs = 1200000LL;
constexpr int64_t kConfirmLongPressUs = kOpenSettingsLongPressUs;
constexpr int64_t kArrowLongPressUs = kOpenSettingsLongPressUs;
constexpr int kChargeLedPwmResolutionBits = 10;
constexpr int kChargeLedPwmMaxDuty = (1 << kChargeLedPwmResolutionBits) - 1;
constexpr int kChargeLedPwmFrequencyHz = 5000;
constexpr int kChargeLedBreathPeriodMs = 2400;
constexpr int kChargeLedBreathStepMs = 40;
constexpr int kChargeLedIdleDelayMs = 500;
constexpr int kDefaultVolumePercent = 50;
constexpr char kBluetoothEnabledSettingsKey[] = "bt_enabled";

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
    int64_t stable_pressed_us = 0;
    bool press_dispatched = false;
};

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
class BleAdvertiser {
public:
    bool SetEnabled(bool enabled, const std::string& device_name) {
        if (enabled) {
            return Start(device_name);
        }
        return Stop();
    }

    bool IsEnabled() const {
        return enabled_;
    }

private:
    bool Start(const std::string& device_name) {
        if (enabled_) {
            return true;
        }

        device_name_ = device_name.empty() ? "Quellog" : device_name;
        active_instance_ = this;

        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "nimble init failed: %s", esp_err_to_name(err));
            active_instance_ = nullptr;
            return false;
        }

        nimble_initialized_ = true;
        ble_hs_cfg.sync_cb = &BleAdvertiser::OnSync;
        ble_hs_cfg.reset_cb = &BleAdvertiser::OnReset;
        ble_svc_gap_init();

        const int name_rc = ble_svc_gap_device_name_set(device_name_.c_str());
        if (name_rc != 0) {
            ESP_LOGE(kTag, "ble device name set failed: %d", name_rc);
            Stop();
            return false;
        }

        enabled_ = true;
        nimble_port_freertos_init(&BleAdvertiser::HostTask);
        host_task_started_ = true;
        ESP_LOGI(kTag, "ble enabled as %s", device_name_.c_str());
        return true;
    }

    bool Stop() {
        enabled_ = false;

        if (advertising_) {
            const int adv_rc = ble_gap_adv_stop();
            if (adv_rc != 0 && adv_rc != BLE_HS_EALREADY) {
                ESP_LOGW(kTag, "ble adv stop failed: %d", adv_rc);
            }
            advertising_ = false;
        }

        if (host_task_started_) {
            const int stop_rc = nimble_port_stop();
            if (stop_rc != 0) {
                ESP_LOGW(kTag, "nimble stop failed: %d", stop_rc);
            }
            host_task_started_ = false;
        }

        if (nimble_initialized_) {
            const esp_err_t err = nimble_port_deinit();
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "nimble deinit failed: %s", esp_err_to_name(err));
                return false;
            }
            nimble_initialized_ = false;
        }

        if (active_instance_ == this) {
            active_instance_ = nullptr;
        }
        ESP_LOGI(kTag, "ble disabled");
        return true;
    }

    bool StartAdvertising() {
        uint8_t own_addr_type = 0;
        int rc = ble_hs_id_infer_auto(0, &own_addr_type);
        if (rc != 0) {
            ESP_LOGE(kTag, "ble address infer failed: %d", rc);
            return false;
        }

        ble_hs_adv_fields fields = {};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.name = reinterpret_cast<const uint8_t*>(device_name_.c_str());
        fields.name_len = static_cast<uint8_t>(std::min<size_t>(device_name_.size(), UINT8_MAX));
        fields.name_is_complete = 1;

        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            ESP_LOGE(kTag, "ble adv fields set failed: %d", rc);
            return false;
        }

        ble_gap_adv_params params = {};
        params.conn_mode = BLE_GAP_CONN_MODE_UND;
        params.disc_mode = BLE_GAP_DISC_MODE_GEN;

        rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &params, &BleAdvertiser::OnGapEvent, this);
        if (rc != 0) {
            ESP_LOGE(kTag, "ble adv start failed: %d", rc);
            return false;
        }

        advertising_ = true;
        return true;
    }

    static void HostTask(void* param) {
        (void)param;
        nimble_port_run();
        nimble_port_freertos_deinit();
    }

    static void OnSync() {
        if (active_instance_ != nullptr && active_instance_->enabled_) {
            active_instance_->StartAdvertising();
        }
    }

    static void OnReset(int reason) {
        ESP_LOGW(kTag, "ble reset: %d", reason);
    }

    static int OnGapEvent(ble_gap_event* event, void* arg) {
        auto* self = static_cast<BleAdvertiser*>(arg);
        if (self == nullptr) {
            return 0;
        }

        switch (event->type) {
            case BLE_GAP_EVENT_CONNECT:
                self->advertising_ = false;
                if (event->connect.status != 0 && self->enabled_) {
                    self->StartAdvertising();
                }
                break;
            case BLE_GAP_EVENT_DISCONNECT:
            case BLE_GAP_EVENT_ADV_COMPLETE:
                self->advertising_ = false;
                if (self->enabled_) {
                    self->StartAdvertising();
                }
                break;
            default:
                break;
        }
        return 0;
    }

    static BleAdvertiser* active_instance_;

    std::string device_name_;
    bool enabled_ = false;
    bool nimble_initialized_ = false;
    bool host_task_started_ = false;
    bool advertising_ = false;
};

BleAdvertiser* BleAdvertiser::active_instance_ = nullptr;
#endif

class ZectrixBoard : public Board {
public:
    ZectrixBoard() {
        LoadLocalSettings();
        InitializeBatteryPower();
        InitializeChargeStatus();
        InitializeChargeLed();
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

    bool GetBatteryLevel(int& level, bool& charging, bool& external_power) override {
        ChargeStatus::Snapshot snapshot = {};
        {
            std::lock_guard<std::mutex> lock(charge_status_mutex_);
            charge_status_.Tick(GetNowMs());
            snapshot = charge_status_.Get();
        }
        charging = snapshot.charging;
        external_power = snapshot.power_present;

        uint16_t voltage_mv = 0;
        uint8_t percent = 0;
        const bool ok = ReadBatteryStatus(voltage_mv, percent);
        level = static_cast<int>(percent);
        return ok;
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

        const bool has_saved_credentials = !SsidManager::GetInstance().GetSsidList().empty();
        network_state_.store(
            has_saved_credentials ? NetworkState::Connecting : NetworkState::Scanning,
            std::memory_order_release);
        WifiManager::GetInstance().StartStation();
        network_started_ = true;
    }

    void StopNetwork() override {
        if (!network_started_) {
            return;
        }
        WifiManager::GetInstance().StopConfigAp();
        WifiManager::GetInstance().StopStation();
        network_state_.store(NetworkState::Disconnected, std::memory_order_release);
        network_started_ = false;
    }

    bool IsWifiEnabled() const override {
        return network_started_;
    }

    bool PollInput(InputEvent& event) override {
        const int64_t now_us = esp_timer_get_time();
        for (ButtonState& button : buttons_) {
            if (button.gpio == GPIO_NUM_NC) {
                continue;
            }

            const int level = gpio_get_level(button.gpio);
            if (level != button.level) {
                const bool released = level == 1 && button.level == 0;
                const bool delayed_arrow_or_confirm =
                    button.key == InputKey::Confirm || button.key == InputKey::Up || button.key == InputKey::Down;
                const bool delayed_press =
                    released && delayed_arrow_or_confirm && button.stable_pressed_us != 0 && !button.press_dispatched;
                const bool delayed_long_press =
                    delayed_press &&
                    now_us - button.stable_pressed_us >=
                        (button.key == InputKey::Confirm ? kConfirmLongPressUs : kArrowLongPressUs);
                button.level = level;
                button.last_change_us = now_us;
                button.stable_pressed_us = 0;
                button.press_dispatched = false;
                if (delayed_press) {
                    event.key = button.key;
                    event.long_press = delayed_long_press;
                    return true;
                }
                continue;
            }

            if (level == 0 &&
                button.last_change_us != 0 &&
                button.stable_pressed_us == 0 &&
                now_us - button.last_change_us >= static_cast<int64_t>(CONFIG_QUELLOG_INPUT_DEBOUNCE_MS) * 1000LL) {
                button.stable_pressed_us = button.last_change_us;
            }
        }

        ButtonState& up_button = buttons_[0];
        ButtonState& down_button = buttons_[1];
        const bool up_pressed = up_button.stable_pressed_us != 0;
        const bool down_pressed = down_button.stable_pressed_us != 0;

        if (up_pressed && down_pressed) {
            const int64_t combo_start_us = std::max(up_button.stable_pressed_us, down_button.stable_pressed_us);
            if (!settings_combo_dispatched_ && now_us - combo_start_us >= kOpenSettingsLongPressUs) {
                up_button.press_dispatched = true;
                down_button.press_dispatched = true;
                settings_combo_dispatched_ = true;
                event.key = InputKey::OpenSettings;
                return true;
            }
            return false;
        }

        settings_combo_dispatched_ = false;

        for (ButtonState& button : buttons_) {
            if ((button.key != InputKey::Up && button.key != InputKey::Down) ||
                button.stable_pressed_us == 0 ||
                button.press_dispatched ||
                now_us - button.stable_pressed_us < kArrowLongPressUs) {
                continue;
            }

            button.press_dispatched = true;
            event.key = button.key;
            event.long_press = true;
            return true;
        }

        for (ButtonState& button : buttons_) {
            if (button.key != InputKey::Confirm ||
                button.stable_pressed_us == 0 ||
                button.press_dispatched ||
                now_us - button.stable_pressed_us < kConfirmLongPressUs) {
                continue;
            }

            button.press_dispatched = true;
            event.key = button.key;
            event.long_press = true;
            return true;
        }

        for (ButtonState& button : buttons_) {
            if (button.stable_pressed_us == 0 || button.press_dispatched) {
                continue;
            }
            if (button.key == InputKey::Confirm) {
                continue;
            }
            if (button.key == InputKey::Up) {
                continue;
            }
            if (button.key == InputKey::Down) {
                continue;
            }

            button.press_dispatched = true;
            event.key = button.key;
            event.long_press = false;
            return true;
        }
        return false;
    }

    void EnterWifiConfigMode() override {
        if (!network_started_) {
            StartNetwork();
        }
        WifiManager::GetInstance().StartConfigAp();
    }

    bool ConnectToOpenWifi(const std::string& ssid) override {
        if (!network_started_) {
            StartNetwork();
        }
        return WifiManager::GetInstance().ConnectToOpenWifi(ssid);
    }

    void PrepareWifiConfigForSsid(const std::string& ssid) override {
        if (!network_started_) {
            StartNetwork();
        }
        WifiManager::GetInstance().PrepareConfigApForSsid(ssid);
    }

    std::vector<BoardWifiNetwork> GetScannedWifiNetworks() const override {
        std::vector<BoardWifiNetwork> networks;
        std::vector<wifi_ap_record_t> records = WifiManager::GetInstance().GetAccessPoints();
        std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& lhs, const wifi_ap_record_t& rhs) {
            return lhs.rssi > rhs.rssi;
        });

        for (const wifi_ap_record_t& record : records) {
            const std::string ssid = reinterpret_cast<const char*>(record.ssid);
            if (ssid.empty()) {
                continue;
            }
            auto existing = std::find_if(networks.begin(), networks.end(), [&ssid](const BoardWifiNetwork& item) {
                return item.ssid == ssid;
            });
            if (existing != networks.end()) {
                continue;
            }
            networks.push_back({
                ssid,
                record.rssi,
                record.authmode != WIFI_AUTH_OPEN,
            });
        }
        return networks;
    }

    std::string GetPendingWifiConfigSsid() const override {
        return WifiManager::GetInstance().GetPendingConfigSsid();
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

    bool IsBluetoothAvailable() const override {
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
        return true;
#else
        return false;
#endif
    }

    bool IsBluetoothEnabled() const override {
        return bluetooth_enabled_;
    }

    bool SetBluetoothEnabled(bool enabled) override {
        if (!IsBluetoothAvailable()) {
            bluetooth_enabled_ = false;
            return false;
        }

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
        if (enabled == bluetooth_enabled_) {
            return true;
        }

        const bool ok = ble_advertiser_.SetEnabled(enabled, BuildBluetoothDeviceName(GetUuid()));
        if (!ok) {
            return false;
        }

        bluetooth_enabled_ = enabled;
        Settings settings("app", true);
        settings.SetInt(kBluetoothEnabledSettingsKey, bluetooth_enabled_ ? 1 : 0);
        return true;
#else
        (void)enabled;
        return false;
#endif
    }

    int GetVolumePercent() const override {
        return volume_percent_;
    }

    void SetVolumePercent(int percent) override {
        volume_percent_ = std::clamp(percent, 0, 100);
        Settings settings("app", true);
        settings.SetInt("volume_percent", volume_percent_);
    }

    BoardStorageInfo GetStorageInfo() const override {
        BoardStorageInfo info;
        uint32_t flash_size = 0;
        if (esp_flash_get_size(nullptr, &flash_size) == ESP_OK) {
            info.flash_total_kb = flash_size / 1024U;
            info.available = true;
        }

        const esp_partition_t* app_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        if (app_partition != nullptr) {
            info.app_total_kb = app_partition->size / 1024U;
            info.app_used_kb = info.app_total_kb;
            info.available = true;
        }

        const esp_partition_t* nvs_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
        if (nvs_partition != nullptr) {
            info.nvs_total_kb = nvs_partition->size / 1024U;
            nvs_stats_t stats = {};
            if (nvs_get_stats("nvs", &stats) == ESP_OK) {
                info.nvs_used_kb = static_cast<uint32_t>((stats.used_entries * 32U + 1023U) / 1024U);
            }
            info.available = true;
        }
        return info;
    }

private:
    static int64_t GetNowMs() {
        return esp_timer_get_time() / 1000;
    }

    void LoadLocalSettings() {
        Settings settings("app");
        volume_percent_ = std::clamp(
            static_cast<int>(settings.GetInt("volume_percent", kDefaultVolumePercent)), 0, 100);

        const bool saved_bluetooth_enabled = settings.GetInt(kBluetoothEnabledSettingsKey, 0) != 0;
        if (saved_bluetooth_enabled && IsBluetoothAvailable()) {
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
            bluetooth_enabled_ = ble_advertiser_.SetEnabled(true, BuildBluetoothDeviceName(GetUuid()));
#endif
        }
    }

    static std::string BuildBluetoothDeviceName(const std::string& uuid) {
        std::string compact_uuid;
        compact_uuid.reserve(uuid.size());
        for (char ch : uuid) {
            if (ch != '-') {
                compact_uuid.push_back(ch);
            }
        }

        const size_t suffix_length = std::min<size_t>(6, compact_uuid.size());
        return "Quellog-" + compact_uuid.substr(compact_uuid.size() - suffix_length);
    }

    void InitializeBatteryPower() {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << QUELLOG_BATTERY_POWER_GPIO;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_set_level(QUELLOG_BATTERY_POWER_GPIO, 1);

        if (QUELLOG_BATTERY_POWER_READY_GPIO != GPIO_NUM_NC) {
            gpio_config_t ready_cfg = {};
            ready_cfg.pin_bit_mask = 1ULL << QUELLOG_BATTERY_POWER_READY_GPIO;
            ready_cfg.mode = GPIO_MODE_INPUT;
            ready_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            ready_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            ready_cfg.intr_type = GPIO_INTR_DISABLE;
            ESP_ERROR_CHECK(gpio_config(&ready_cfg));
        }
    }

    void InitializeChargeStatus() {
        charge_status_.Init(QUELLOG_CHARGE_DETECT_GPIO, QUELLOG_CHARGE_FULL_GPIO, GetNowMs());
    }

    void InitializeChargeLed() {
        if (QUELLOG_CHARGE_LED_GPIO == GPIO_NUM_NC) {
            return;
        }

        // 使用 LEDC PWM 驱动充电指示灯，便于实现充电时的呼吸灯效果。
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
        timer_cfg.timer_num = LEDC_TIMER_0;
        timer_cfg.freq_hz = kChargeLedPwmFrequencyHz;
        timer_cfg.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&timer_cfg));

        ledc_channel_config_t channel_cfg = {};
        channel_cfg.gpio_num = QUELLOG_CHARGE_LED_GPIO;
        channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        channel_cfg.channel = LEDC_CHANNEL_0;
        channel_cfg.intr_type = LEDC_INTR_DISABLE;
        channel_cfg.timer_sel = LEDC_TIMER_0;
        channel_cfg.duty = PwmDutyFromBrightness(0);
        channel_cfg.hpoint = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&channel_cfg));

        SetChargeLedBrightness(0);
        xTaskCreatePinnedToCore(&ZectrixBoard::ChargeLedTask, "ChargeLedTask", 3 * 1024, this, 2, nullptr, 0);
    }

    static uint32_t ClampChargeLedBrightness(uint32_t brightness) {
        // 所有状态统一经过亮度限幅，避免满电常亮或呼吸峰值超过配置上限。
        constexpr uint32_t max_brightness =
            static_cast<uint32_t>((kChargeLedPwmMaxDuty * QUELLOG_CHARGE_LED_MAX_DUTY_PERCENT) / 100);
        return std::min(brightness, max_brightness);
    }

    static uint32_t PwmDutyFromBrightness(uint32_t brightness) {
        const uint32_t clamped = ClampChargeLedBrightness(brightness);
        if (QUELLOG_CHARGE_LED_ACTIVE_LEVEL == 0) {
            // 硬件为低电平点亮，业务亮度越高，对应 PWM duty 需要越低。
            return kChargeLedPwmMaxDuty - clamped;
        }
        return clamped;
    }

    void SetChargeLedBrightness(uint32_t brightness) {
        if (QUELLOG_CHARGE_LED_GPIO == GPIO_NUM_NC) {
            return;
        }

        if (brightness == 0) {
            // 熄灭时停止 PWM 并固定到关闭电平，避免低电平点亮 LED 因极窄脉冲产生微光。
            constexpr uint32_t off_level = QUELLOG_CHARGE_LED_ACTIVE_LEVEL == 0 ? 1 : 0;
            ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, off_level));
            return;
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, PwmDutyFromBrightness(brightness)));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    }

    void DelayChargeLedMs(int delay_ms) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    void BreatheChargeLed() {
        constexpr int half_period_ms = kChargeLedBreathPeriodMs / 2;
        constexpr int steps = std::max(1, half_period_ms / kChargeLedBreathStepMs);
        const uint32_t max_brightness = ClampChargeLedBrightness(kChargeLedPwmMaxDuty);

        // 采用三角波亮度曲线：先从灭渐亮到上限，再渐暗到灭。
        for (int step = 0; step <= steps; ++step) {
            SetChargeLedBrightness((max_brightness * step) / steps);
            DelayChargeLedMs(kChargeLedBreathStepMs);
        }
        for (int step = steps - 1; step >= 0; --step) {
            SetChargeLedBrightness((max_brightness * step) / steps);
            DelayChargeLedMs(kChargeLedBreathStepMs);
        }
    }

    static void ChargeLedTask(void* arg) {
        auto* self = static_cast<ZectrixBoard*>(arg);
        while (self != nullptr) {
            ChargeStatus::Snapshot snapshot = {};
            {
                std::lock_guard<std::mutex> lock(self->charge_status_mutex_);
                self->charge_status_.Tick(GetNowMs());
                snapshot = self->charge_status_.Get();
            }
            if (snapshot.full) {
                // 满电后常亮，但仍受 ClampChargeLedBrightness 的最高亮度限制。
                self->SetChargeLedBrightness(kChargeLedPwmMaxDuty);
                vTaskDelay(pdMS_TO_TICKS(kChargeLedIdleDelayMs));
            } else if (snapshot.charging) {
                self->BreatheChargeLed();
            } else {
                self->SetChargeLedBrightness(0);
                vTaskDelay(pdMS_TO_TICKS(kChargeLedIdleDelayMs));
            }
        }
    }

    uint16_t ReadBatteryVoltage() {
        static bool initialized = false;
        static adc_oneshot_unit_handle_t adc_handle = nullptr;
        static adc_cali_handle_t cali_handle = nullptr;

        if (!initialized) {
            adc_oneshot_unit_init_cfg_t init_config = {};
            init_config.unit_id = ADC_UNIT_1;
            init_config.ulp_mode = ADC_ULP_MODE_DISABLE;
            ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

            adc_oneshot_chan_cfg_t channel_config = {};
            channel_config.atten = ADC_ATTEN_DB_12;
            channel_config.bitwidth = ADC_BITWIDTH_12;
            ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, QUELLOG_BATTERY_ADC_CHANNEL, &channel_config));

            adc_cali_curve_fitting_config_t cali_config = {};
            cali_config.unit_id = ADC_UNIT_1;
            cali_config.chan = QUELLOG_BATTERY_ADC_CHANNEL;
            cali_config.atten = ADC_ATTEN_DB_12;
            cali_config.bitwidth = ADC_BITWIDTH_12;
            if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK) {
                initialized = true;
            }
        }

        if (!initialized) {
            return 0;
        }

        int raw_value = 0;
        int raw_voltage = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, QUELLOG_BATTERY_ADC_CHANNEL, &raw_value));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_value, &raw_voltage));
        return static_cast<uint16_t>(raw_voltage * 2);
    }

    bool ReadBatteryStatus(uint16_t& voltage_mv, uint8_t& percent) {
        int voltage_sum = 0;
        for (int i = 0; i < 10; ++i) {
            voltage_sum += ReadBatteryVoltage();
        }

        const int average_voltage = voltage_sum / 10;
        if (average_voltage <= 0) {
            voltage_mv = 0;
            percent = 0;
            return false;
        }

        int computed_percent =
            (-1 * average_voltage * average_voltage + 9016 * average_voltage - 19189000) / 10000;
        computed_percent = std::clamp(computed_percent, 0, 100);

        voltage_mv = static_cast<uint16_t>(average_voltage);
        percent = static_cast<uint8_t>(computed_percent);
        return true;
    }

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
    ChargeStatus charge_status_;
    std::mutex charge_status_mutex_;
    std::atomic<NetworkState> network_state_{NetworkState::Unknown};
    NetworkEventCallback network_event_callback_;
    bool network_started_ = false;
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
    BleAdvertiser ble_advertiser_;
#endif
    bool bluetooth_enabled_ = false;
    int volume_percent_ = kDefaultVolumePercent;
    bool settings_combo_dispatched_ = false;
};

}  // namespace

DECLARE_BOARD(ZectrixBoard);

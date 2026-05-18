#include "wifi_manager.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <string>
#include <utility>

#include "ssid_manager.h"
#include "wifi_configuration_ap.h"
#include "wifi_station.h"

namespace {

constexpr char kTag[] = "WifiManager";

}  // namespace

WifiManager& WifiManager::GetInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::~WifiManager() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (station_active_ && station_) {
        station_->Stop();
    }
    if (config_mode_active_ && config_ap_) {
        config_ap_->Stop();
    }
    if (initialized_) {
        esp_wifi_deinit();
    }
}

bool WifiManager::Initialize(const WifiManagerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    config_ = config;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "nvs init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "netif init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "event loop init failed: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config.nvs_enable = false;
    ret = esp_wifi_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "wifi init failed: %s", esp_err_to_name(ret));
        return false;
    }

    station_ = std::make_unique<WifiStation>();
    config_ap_ = std::make_unique<WifiConfigurationAp>();
    initialized_ = true;
    return true;
}

bool WifiManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

void WifiManager::StartStation(bool start_scan) {
    bool notify_config_exit = false;
    WifiStation* station = nullptr;
    WifiConfigurationAp* config_ap = nullptr;
    int scan_interval_seconds = 15;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!initialized_ || station_active_ || station_ == nullptr) {
            ESP_LOGW(kTag,
                     "skip starting station: initialized=%d station_active=%d station_present=%d",
                     initialized_,
                     station_active_,
                     station_ != nullptr);
            return;
        }
        station = station_.get();
        config_ap = config_ap_.get();
        scan_interval_seconds = config_.station_scan_interval_seconds;
    }

    if (config_ap != nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (config_mode_active_ && config_ap_.get() == config_ap) {
            lock.unlock();
            ESP_LOGI(kTag, "stopping config AP before starting station");
            config_ap->Stop();
            lock.lock();
            if (config_mode_active_ && config_ap_.get() == config_ap) {
                config_mode_active_ = false;
                notify_config_exit = true;
            }
        }
    }

    if (station == nullptr) {
        return;
    }

    station->SetScanIntervalSeconds(scan_interval_seconds);
    station->OnScanBegin([this]() { NotifyEvent(WifiEvent::Scanning); });
    station->OnConnect([this](const std::string&) { NotifyEvent(WifiEvent::Connecting); });
    station->OnConnected([this](const std::string&) { HandleStationConnected(); });
    station->OnDisconnected([this]() { HandleStationDisconnected(); });
    ESP_LOGI(kTag, "starting station, start_scan=%d", start_scan);
    station->Start(start_scan);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (station_.get() == station) {
            station_active_ = true;
            ESP_LOGI(kTag, "station marked active");
        }
    }

    if (notify_config_exit) {
        NotifyEvent(WifiEvent::ConfigModeExit);
    }
}

void WifiManager::StopStation() {
    bool notify_disconnected = false;
    WifiStation* station = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!station_active_ || station_ == nullptr) {
            return;
        }
        station = station_.get();
        station_active_ = false;
        notify_disconnected = true;
    }

    if (station != nullptr) {
        station->Stop();
    }

    if (notify_disconnected) {
        NotifyEvent(WifiEvent::Disconnected);
    }
}

bool WifiManager::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_active_ && station_ != nullptr && station_->IsConnected();
}

std::string WifiManager::GetSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_ != nullptr ? station_->GetSsid() : "";
}

std::string WifiManager::GetIpAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_ != nullptr ? station_->GetIpAddress() : "";
}

int WifiManager::GetRssi() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_ != nullptr ? station_->GetRssi() : 0;
}

int WifiManager::GetChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_ != nullptr ? station_->GetChannel() : 0;
}

std::vector<wifi_ap_record_t> WifiManager::GetAccessPoints() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (station_active_ && station_ != nullptr) {
        return station_->GetAccessPoints();
    }
    if (config_mode_active_ && config_ap_ != nullptr) {
        return config_ap_->GetAccessPoints();
    }
    return {};
}

bool WifiManager::ConnectToOpenWifi(const std::string& ssid) {
    WifiStation* station = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !station_active_ || station_ == nullptr) {
            return false;
        }
        station = station_.get();
        pending_config_ssid_.clear();
        pending_config_item_ = {};
        pending_config_credentials_submitted_ = false;
        retry_config_ssid_.clear();
        retry_config_ap_on_disconnect_ = false;
    }

    SsidManager::GetInstance().AddSsid(ssid, "");
    const bool started = station->ConnectToWifi(ssid, "");
    if (started) {
        NotifyEvent(WifiEvent::Connecting);
    }
    return started;
}

void WifiManager::StartConfigAp() {
    bool notify_disconnected = false;
    WifiStation* station = nullptr;
    WifiConfigurationAp* config_ap = nullptr;
    WifiManagerConfig config;
    std::string pending_ssid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || config_mode_active_ || config_ap_ == nullptr) {
            return;
        }
        station = station_.get();
        config_ap = config_ap_.get();
        config = config_;
        pending_ssid = pending_config_ssid_;
        if (station_active_ && station != nullptr) {
            station_active_ = false;
            notify_disconnected = true;
        }
    }

    if (station != nullptr && notify_disconnected) {
        station->Stop();
    }

    if (config_ap == nullptr) {
        return;
    }

    config_ap->SetSsidPrefix(config.ssid_prefix);
    config_ap->SetPassword(config.ap_password);
    config_ap->SetLanguage(config.language);
    config_ap->SetPendingSsid(pending_ssid);
    config_ap->OnCredentialsSubmitted([this](const SsidItem& item) { HandleCredentialsSubmitted(item); });
    config_ap->OnExitRequested([this]() { HandleConfigExitRequested(); });
    config_ap->Start();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config_ap_.get() == config_ap) {
            config_mode_active_ = true;
        }
    }

    if (notify_disconnected) {
        NotifyEvent(WifiEvent::Disconnected);
    }
    NotifyEvent(WifiEvent::ConfigModeEnter);
}

void WifiManager::PrepareConfigApForSsid(const std::string& ssid) {
    WifiConfigurationAp* config_ap = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_config_ssid_ = ssid;
        pending_config_item_ = {};
        pending_config_credentials_submitted_ = false;
        retry_config_ssid_.clear();
        retry_config_ap_on_disconnect_ = false;
        config_ap = config_ap_.get();
    }
    if (config_ap != nullptr) {
        config_ap->SetPendingSsid(ssid);
    }
    StartConfigAp();
}

void WifiManager::StopConfigAp() {
    bool notify_exit = false;
    WifiConfigurationAp* config_ap = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_mode_active_ || config_ap_ == nullptr) {
            ESP_LOGW(kTag,
                     "skip stopping config AP: config_mode_active=%d config_ap_present=%d",
                     config_mode_active_,
                     config_ap_ != nullptr);
            return;
        }
        config_ap = config_ap_.get();
        config_mode_active_ = false;
        notify_exit = true;
    }

    if (config_ap != nullptr) {
        ESP_LOGI(kTag, "stopping config AP by request");
        config_ap->Stop();
    }

    if (notify_exit) {
        ESP_LOGI(kTag, "config mode exited");
        NotifyEvent(WifiEvent::ConfigModeExit);
    }
}

bool WifiManager::IsConfigMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_mode_active_;
}

std::string WifiManager::GetApSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_ap_ != nullptr ? config_ap_->GetSsid() : "";
}

std::string WifiManager::GetApPassword() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_ap_ != nullptr ? config_ap_->GetPassword() : "";
}

std::string WifiManager::GetApWebUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_ap_ != nullptr ? config_ap_->GetWebServerUrl() : "";
}

std::string WifiManager::GetPendingConfigSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_config_ssid_;
}

void WifiManager::SetEventCallback(std::function<void(WifiEvent)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void WifiManager::HandleCredentialsSubmitted(const SsidItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_config_ssid_ = item.ssid;
    pending_config_item_ = item;
    pending_config_credentials_submitted_ = true;
    retry_config_ssid_ = item.ssid;
    retry_config_ap_on_disconnect_ = false;
    ESP_LOGI(kTag,
             "credentials submitted for SSID '%s' ip_mode=%s",
             item.ssid.c_str(),
             item.ip_mode == WifiIpMode::Static ? "static" : "dhcp");
}

void WifiManager::HandleStationConnected() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retry_config_ssid_.clear();
        retry_config_ap_on_disconnect_ = false;
    }
    NotifyEvent(WifiEvent::Connected);
}

void WifiManager::HandleStationDisconnected() {
    bool should_restart_config_ap = false;
    std::string retry_ssid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        should_restart_config_ap = retry_config_ap_on_disconnect_;
        retry_ssid = retry_config_ssid_;
        retry_config_ap_on_disconnect_ = false;
    }

    NotifyEvent(WifiEvent::Disconnected);

    if (should_restart_config_ap && !retry_ssid.empty()) {
        ESP_LOGW(kTag, "submitted SSID connection failed, restarting config AP for '%s'", retry_ssid.c_str());
        ScheduleConfigApRestart(retry_ssid);
    }
}

void WifiManager::NotifyEvent(WifiEvent event) {
    std::function<void(WifiEvent)> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = event_callback_;
    }
    if (callback) {
        callback(event);
    }
}

void WifiManager::HandleConfigExitRequested() {
    std::string target_ssid;
    SsidItem target_item;
    bool has_submitted_credentials = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_ssid = pending_config_ssid_;
        target_item = pending_config_item_;
        has_submitted_credentials = pending_config_credentials_submitted_;
        pending_config_ssid_.clear();
        pending_config_item_ = {};
        pending_config_credentials_submitted_ = false;
    }

    ESP_LOGI(kTag,
             "config AP exit requested, submitted=%d target_ssid='%s'",
             has_submitted_credentials,
             target_ssid.c_str());

    StopConfigAp();
    StartStation(!has_submitted_credentials);

    if (!has_submitted_credentials || target_ssid.empty()) {
        return;
    }

    WifiStation* station = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!station_active_ || station_ == nullptr) {
            ESP_LOGW(kTag,
                     "cannot direct connect after config exit: station_active=%d station_present=%d",
                     station_active_,
                     station_ != nullptr);
            return;
        }
        station = station_.get();
    }

    const bool started = station->ConnectToWifi(target_item);
    if (started) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            retry_config_ssid_ = target_ssid;
            retry_config_ap_on_disconnect_ = true;
        }
        ESP_LOGI(kTag, "direct connection started for submitted SSID '%s'", target_ssid.c_str());
        NotifyEvent(WifiEvent::Connecting);
    } else {
        ESP_LOGW(kTag, "direct connection to submitted SSID failed to start: %s", target_ssid.c_str());
        ScheduleConfigApRestart(target_ssid);
    }
}

void WifiManager::ScheduleConfigApRestart(const std::string& ssid) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_config_ssid_ = ssid;
        pending_config_item_ = {};
        pending_config_credentials_submitted_ = false;
        retry_config_ssid_.clear();
        retry_config_ap_on_disconnect_ = false;
    }

    xTaskCreate(
        [](void* arg) {
            vTaskDelay(pdMS_TO_TICKS(500));
            auto* self = static_cast<WifiManager*>(arg);
            self->StartConfigAp();
            vTaskDelete(nullptr);
        },
        "quellog_wifi_ap_retry",
        4096,
        this,
        5,
        nullptr);
}

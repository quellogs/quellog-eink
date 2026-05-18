#include "wifi_station.h"

#include <esp_log.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ssid_manager.h"

namespace {

constexpr char kTag[] = "WifiStation";

std::string FormatIp(esp_ip4_addr_t address) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&address));
    return std::string(buffer);
}

}  // namespace

WifiStation::WifiStation() = default;

WifiStation::~WifiStation() {
    Stop();
}

void WifiStation::Start(bool start_scan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }

    station_netif_ = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiStation::WifiEventHandler, this, &wifi_event_handler_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiStation::IpEventHandler, this, &got_ip_handler_));

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &WifiStation::ScanTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "quellog_sta_scan";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));

    wifi_config_t wifi_config = {};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    running_ = true;
    auto_scan_on_start_ = start_scan;
    connected_ = false;
    connecting_ = false;
    current_ssid_.clear();
    connecting_ssid_.clear();
    ip_address_.clear();
    ap_records_.clear();
    if (start_scan) {
        ESP_LOGI(kTag, "station started, begin scan");
        StartScan();
    } else {
        ESP_LOGI(kTag, "station started, waiting for direct connect");
    }
}

void WifiStation::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        ESP_LOGI(kTag, "station stop ignored; not running");
        return;
    }

    ESP_LOGI(kTag, "stopping station");
    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
        esp_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }
    if (got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_handler_);
        got_ip_handler_ = nullptr;
    }
    if (wifi_event_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler_);
        wifi_event_handler_ = nullptr;
    }
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(kTag, "esp_wifi_disconnect failed while stopping station: %s", esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop failed while stopping station: %s", esp_err_to_name(err));
    }
    if (station_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(station_netif_);
        station_netif_ = nullptr;
    }

    running_ = false;
    connected_ = false;
    connecting_ = false;
    scan_in_progress_ = false;
    current_ssid_.clear();
    connecting_ssid_.clear();
    ip_address_.clear();
    ap_records_.clear();
    ESP_LOGI(kTag, "station stopped");
}

bool WifiStation::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

std::string WifiStation::GetSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_ssid_.empty() ? connecting_ssid_ : current_ssid_;
}

std::string WifiStation::GetIpAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ip_address_;
}

int WifiStation::GetRssi() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return 0;
    }
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK ? ap_info.rssi : 0;
}

int WifiStation::GetChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return 0;
    }
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK ? static_cast<int>(ap_info.primary) : 0;
}

std::vector<wifi_ap_record_t> WifiStation::GetAccessPoints() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ap_records_;
}

bool WifiStation::ConnectToWifi(const std::string& ssid, const std::string& password) {
    SsidItem item = {};
    item.ssid = ssid;
    item.password = password;
    item.ip_mode = WifiIpMode::Dhcp;
    return ConnectToWifi(item);
}

bool WifiStation::ConnectToWifi(const SsidItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    return ConnectToWifiLocked(item);
}

void WifiStation::SetScanIntervalSeconds(int scan_interval_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_interval_seconds_ = std::max(5, scan_interval_seconds);
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_scan_begin_ = std::move(on_scan_begin);
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_connect_ = std::move(on_connect);
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_connected_ = std::move(on_connected);
}

void WifiStation::OnDisconnected(std::function<void()> on_disconnected) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_disconnected_ = std::move(on_disconnected);
}

void WifiStation::StartScan() {
    if (!running_ || scan_in_progress_ || connecting_) {
        ESP_LOGI(kTag,
                 "skip station scan: running=%d scan_in_progress=%d connecting=%d",
                 running_,
                 scan_in_progress_,
                 connecting_);
        return;
    }

    scan_in_progress_ = true;
    if (on_scan_begin_) {
        on_scan_begin_();
    }
    esp_err_t err = esp_wifi_scan_start(nullptr, false);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        scan_in_progress_ = false;
        ScheduleScan();
    }
}

void WifiStation::ScheduleScan() {
    if (scan_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(scan_timer_);
    esp_timer_start_once(scan_timer_, static_cast<uint64_t>(scan_interval_seconds_) * 1000ULL * 1000ULL);
}

void WifiStation::HandleScanDone() {
    std::function<void(const std::string&)> on_connect_callback;
    std::string target_ssid;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        scan_in_progress_ = false;

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        std::vector<wifi_ap_record_t> access_points(ap_count);
        if (ap_count > 0) {
            esp_wifi_scan_get_ap_records(&ap_count, access_points.data());
            access_points.resize(ap_count);
        }
        ap_records_ = access_points;

        const std::vector<SsidItem>& credentials = SsidManager::GetInstance().GetSsidList();
        if (credentials.empty()) {
            ESP_LOGI(kTag, "station scan done: %u APs, no saved credentials", static_cast<unsigned>(access_points.size()));
            ScheduleScan();
            return;
        }

        const SsidItem* selected = nullptr;
        int best_rssi = INT32_MIN;
        for (const wifi_ap_record_t& access_point : access_points) {
            const char* scanned_ssid = reinterpret_cast<const char*>(access_point.ssid);
            auto match = std::find_if(credentials.begin(), credentials.end(), [scanned_ssid](const SsidItem& credential) {
                return credential.ssid == scanned_ssid;
            });
            if (match != credentials.end() && (selected == nullptr || access_point.rssi > best_rssi)) {
                selected = &(*match);
                best_rssi = access_point.rssi;
            }
        }

        if (selected == nullptr) {
            ESP_LOGI(kTag,
                     "station scan done: %u APs, no saved SSID matched",
                     static_cast<unsigned>(access_points.size()));
            ScheduleScan();
            return;
        }

        ESP_LOGI(kTag, "station scan selected saved SSID '%s'", selected->ssid.c_str());
        if (!ConnectToWifiLocked(*selected)) {
            ScheduleScan();
            return;
        }
        target_ssid = selected->ssid;
        on_connect_callback = on_connect_;
    }

    if (on_connect_callback) {
        on_connect_callback(target_ssid);
    }
}

bool WifiStation::ConnectToWifiLocked(const std::string& ssid, const std::string& password) {
    SsidItem item = {};
    item.ssid = ssid;
    item.password = password;
    item.ip_mode = WifiIpMode::Dhcp;
    return ConnectToWifiLocked(item);
}

bool WifiStation::ConnectToWifiLocked(const SsidItem& item) {
    const std::string& ssid = item.ssid;
    const std::string& password = item.password;
    if (!running_ || ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        ESP_LOGW(kTag,
                 "reject connect request: running=%d ssid_empty=%d ssid_len=%u password_len=%u",
                 running_,
                 ssid.empty(),
                 static_cast<unsigned>(ssid.size()),
                 static_cast<unsigned>(password.size()));
        return false;
    }

    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
    }
    esp_err_t err = esp_wifi_scan_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(kTag, "failed to stop scan before connect: %s", esp_err_to_name(err));
    }

    wifi_config_t wifi_config = {};
    strlcpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid));
    strlcpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    connected_ = false;
    connecting_ = true;
    scan_in_progress_ = false;
    current_ssid_.clear();
    ip_address_.clear();
    connecting_ssid_ = ssid;
    ESP_LOGI(kTag, "connecting to SSID '%s'", ssid.c_str());
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_connect failed for SSID '%s': %s", ssid.c_str(), esp_err_to_name(err));
        connecting_ = false;
        connecting_ssid_.clear();
        ScheduleScan();
        return false;
    }
    if (!ConfigureIpLocked(item)) {
        ScheduleScan();
        return false;
    }
    return true;
}

bool WifiStation::ConfigureIpLocked(const SsidItem& item) {
    if (station_netif_ == nullptr) {
        return false;
    }

    if (item.ip_mode == WifiIpMode::Dhcp) {
        esp_netif_dns_info_t empty_dns_info = {};
        empty_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(station_netif_, ESP_NETIF_DNS_BACKUP, &empty_dns_info);
        esp_err_t err = esp_netif_dhcpc_start(station_netif_);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(kTag, "failed to start DHCP client: %s", esp_err_to_name(err));
            return false;
        }
        ESP_LOGI(kTag, "station IP mode: DHCP");
        return true;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_str_to_ip4(item.ip.c_str(), &ip_info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(item.netmask.c_str(), &ip_info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(item.gateway.c_str(), &ip_info.gw) != ESP_OK) {
        ESP_LOGW(kTag, "invalid static IP settings for SSID '%s'", item.ssid.c_str());
        return false;
    }

    esp_err_t err = esp_netif_dhcpc_stop(station_netif_);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(kTag, "failed to stop DHCP client: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_netif_set_ip_info(station_netif_, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "failed to set static IP info: %s", esp_err_to_name(err));
        return false;
    }

    esp_netif_dns_info_t dns_info = {};
    if (esp_netif_str_to_ip4(item.dns1.c_str(), &dns_info.ip.u_addr.ip4) != ESP_OK) {
        ESP_LOGW(kTag, "invalid primary DNS for SSID '%s'", item.ssid.c_str());
        return false;
    }
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    err = esp_netif_set_dns_info(station_netif_, ESP_NETIF_DNS_MAIN, &dns_info);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "failed to set primary DNS: %s", esp_err_to_name(err));
        return false;
    }

    if (!item.dns2.empty()) {
        esp_netif_dns_info_t backup_dns_info = {};
        if (esp_netif_str_to_ip4(item.dns2.c_str(), &backup_dns_info.ip.u_addr.ip4) != ESP_OK) {
            ESP_LOGW(kTag, "invalid secondary DNS for SSID '%s'", item.ssid.c_str());
            return false;
        }
        backup_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        err = esp_netif_set_dns_info(station_netif_, ESP_NETIF_DNS_BACKUP, &backup_dns_info);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "failed to set secondary DNS: %s", esp_err_to_name(err));
            return false;
        }
    } else {
        esp_netif_dns_info_t empty_dns_info = {};
        empty_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(station_netif_, ESP_NETIF_DNS_BACKUP, &empty_dns_info);
    }

    ESP_LOGI(kTag,
             "station IP mode: static ip=%s gateway=%s dns1=%s dns2=%s",
             item.ip.c_str(),
             item.gateway.c_str(),
             item.dns1.c_str(),
             item.dns2.c_str());
    return true;
}

void WifiStation::HandleDisconnected() {
    std::function<void()> on_disconnected_callback;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool was_active = connected_ || connecting_;
        connected_ = false;
        connecting_ = false;
        current_ssid_.clear();
        ip_address_.clear();
        scan_in_progress_ = false;
        if (!was_active) {
            ScheduleScan();
            return;
        }
        on_disconnected_callback = on_disconnected_;
        ScheduleScan();
    }

    if (on_disconnected_callback) {
        on_disconnected_callback();
    }
}

void WifiStation::WifiEventHandler(void* arg,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void* event_data) {
    (void)event_base;
    (void)event_data;
    auto* self = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(kTag, "WIFI_EVENT_STA_START");
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->auto_scan_on_start_) {
            self->StartScan();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(kTag, "WIFI_EVENT_SCAN_DONE");
        self->HandleScanDone();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        ESP_LOGI(kTag, "WIFI_EVENT_STA_DISCONNECTED reason=%d", event->reason);
        self->HandleDisconnected();
    }
}

void WifiStation::IpEventHandler(void* arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void* event_data) {
    (void)event_base;
    (void)event_id;
    auto* self = static_cast<WifiStation*>(arg);
    auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);

    std::function<void(const std::string&)> on_connected_callback;
    std::string ssid;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->connected_ = true;
        self->connecting_ = false;
        self->current_ssid_ = self->connecting_ssid_;
        self->ip_address_ = FormatIp(got_ip->ip_info.ip);
        self->scan_in_progress_ = false;
        self->connecting_ssid_.clear();
        ssid = self->current_ssid_;
        on_connected_callback = self->on_connected_;
        if (self->scan_timer_ != nullptr) {
            esp_timer_stop(self->scan_timer_);
        }
    }

    ESP_LOGI(kTag, "station got IP %s for SSID '%s'", self->ip_address_.c_str(), ssid.c_str());
    if (on_connected_callback) {
        on_connected_callback(ssid);
    }
}

void WifiStation::ScanTimerCallback(void* arg) {
    auto* self = static_cast<WifiStation*>(arg);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->StartScan();
}

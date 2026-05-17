#ifndef QUELLOG_WIFI_CONFIGURATION_AP_H_
#define QUELLOG_WIFI_CONFIGURATION_AP_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "dns_server.h"

class WifiConfigurationAp {
public:
    WifiConfigurationAp();
    ~WifiConfigurationAp();

    WifiConfigurationAp(const WifiConfigurationAp&) = delete;
    WifiConfigurationAp& operator=(const WifiConfigurationAp&) = delete;

    void SetSsidPrefix(const std::string& ssid_prefix);
    void SetPassword(const std::string& password);
    void SetLanguage(const std::string& language);
    void SetPendingSsid(const std::string& ssid);
    void Start();
    void Stop();

    bool ConnectToWifi(const std::string& ssid, const std::string& password);
    void Save(const std::string& ssid, const std::string& password);
    void RemoveCredential(const std::string& ssid);
    std::vector<wifi_ap_record_t> GetAccessPoints();
    std::string GetSsid() const;
    std::string GetPassword() const;
    std::string GetWebServerUrl() const;
    std::string GetPendingSsid() const;
    void OnCredentialsSubmitted(std::function<void(const std::string& ssid, const std::string& password)> callback);
    void OnExitRequested(std::function<void()> callback);

private:
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void ScanTimerCallback(void* arg);

    void StartAccessPoint();
    void StartWebServer();
    void ScheduleExit();
    void ScheduleScan();

    mutable std::mutex mutex_;
    std::unique_ptr<DnsServer> dns_server_;
    httpd_handle_t server_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    std::string ssid_prefix_ = "Quellog";
    std::string password_;
    std::string language_ = "zh-CN";
    std::string pending_ssid_;
    esp_event_handler_instance_t wifi_event_handler_ = nullptr;
    esp_event_handler_instance_t got_ip_handler_ = nullptr;
    esp_timer_handle_t scan_timer_ = nullptr;
    bool is_connecting_ = false;
    esp_netif_t* ap_netif_ = nullptr;
    esp_netif_t* sta_netif_ = nullptr;
    std::vector<wifi_ap_record_t> ap_records_;
    std::function<void(const std::string& ssid, const std::string& password)> on_credentials_submitted_;
    std::function<void()> on_exit_requested_;
};

#endif  // QUELLOG_WIFI_CONFIGURATION_AP_H_

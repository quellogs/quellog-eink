#include "wifi_configuration_ap.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <lwip/ip_addr.h>
#include <nvs.h>
#include <string.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <cJSON.h>

#include "ssid_manager.h"

namespace {

constexpr char kTag[] = "WifiConfigAp";
constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailedBit = BIT1;
constexpr char kWebUrl[] = "http://192.168.4.1";
constexpr int64_t kScanCacheTtlUs = 10LL * 1000 * 1000;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char done_html_start[] asm("_binary_done_html_start");

std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

void SendJson(httpd_req_t* req, const std::string& body) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

bool ReadRequestBody(httpd_req_t* req, std::string* body) {
    if (req->content_len <= 0 || req->content_len > 1024 || body == nullptr) {
        return false;
    }

    body->resize(static_cast<size_t>(req->content_len));
    int total_received = 0;
    while (total_received < req->content_len) {
        const int received = httpd_req_recv(req, body->data() + total_received, req->content_len - total_received);
        if (received <= 0) {
            return false;
        }
        total_received += received;
    }
    return true;
}

std::string GetJsonString(cJSON* root, const char* name) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    const char* value = cJSON_IsString(item) ? item->valuestring : "";
    return value != nullptr ? std::string(value) : std::string();
}

bool IsValidIpv4(const std::string& value) {
    esp_ip4_addr_t address = {};
    return !value.empty() && esp_netif_str_to_ip4(value.c_str(), &address) == ESP_OK;
}

bool BuildSubmittedWifiConfig(cJSON* root, SsidItem* item, std::string* error) {
    if (item == nullptr || error == nullptr) {
        return false;
    }

    item->ssid = GetJsonString(root, "ssid");
    item->password = GetJsonString(root, "password");
    if (item->ssid.empty() || item->ssid.size() > 32 || item->password.size() > 64) {
        *error = "Wi-Fi 名称或密码格式无效";
        return false;
    }

    const std::string ip_mode = GetJsonString(root, "ipMode");
    item->ip_mode = ip_mode == "static" ? WifiIpMode::Static : WifiIpMode::Dhcp;
    if (item->ip_mode == WifiIpMode::Dhcp) {
        return true;
    }

    item->ip = GetJsonString(root, "ip");
    item->netmask = GetJsonString(root, "netmask");
    item->gateway = GetJsonString(root, "gateway");
    item->dns1 = GetJsonString(root, "dns1");
    item->dns2 = GetJsonString(root, "dns2");
    if (!IsValidIpv4(item->ip) || !IsValidIpv4(item->netmask) || !IsValidIpv4(item->gateway) ||
        !IsValidIpv4(item->dns1) || (!item->dns2.empty() && !IsValidIpv4(item->dns2))) {
        *error = "手动 IP、网关或 DNS 格式无效";
        return false;
    }
    return true;
}

}  // namespace

WifiConfigurationAp::WifiConfigurationAp() {
    event_group_ = xEventGroupCreate();
}

WifiConfigurationAp::~WifiConfigurationAp() {
    Stop();
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

void WifiConfigurationAp::SetSsidPrefix(const std::string& ssid_prefix) {
    ssid_prefix_ = ssid_prefix;
}

void WifiConfigurationAp::SetPassword(const std::string& password) {
    password_ = password;
}

void WifiConfigurationAp::SetLanguage(const std::string& language) {
    language_ = language;
}

void WifiConfigurationAp::SetPendingSsid(const std::string& ssid) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ssid_ = ssid;
}

void WifiConfigurationAp::Start() {
    ESP_LOGI(kTag, "starting config AP");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiConfigurationAp::WifiEventHandler, this, &wifi_event_handler_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiConfigurationAp::IpEventHandler, this, &got_ip_handler_));

    StartAccessPoint();
    StartWebServer();

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &WifiConfigurationAp::ScanTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "quellog_ap_scan";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));
    ScheduleScan(1000);
}

void WifiConfigurationAp::Stop() {
    ESP_LOGI(kTag, "stopping config AP");
    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
        esp_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }
    scan_in_progress_ = false;

    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }

    if (dns_server_) {
        dns_server_->Stop();
        dns_server_.reset();
    }

    if (wifi_event_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler_);
        wifi_event_handler_ = nullptr;
    }
    if (got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_handler_);
        got_ip_handler_ = nullptr;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop failed while stopping config AP: %s", esp_err_to_name(err));
    }
    if (sta_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(sta_netif_);
        sta_netif_ = nullptr;
    }
    if (ap_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(ap_netif_);
        ap_netif_ = nullptr;
    }
    ESP_LOGI(kTag, "config AP stopped");
}

bool WifiConfigurationAp::ConnectToWifi(const std::string& ssid, const std::string& password) {
    SsidItem item = {};
    item.ssid = ssid;
    item.password = password;
    item.ip_mode = WifiIpMode::Dhcp;
    return ConnectToWifi(item);
}

bool WifiConfigurationAp::ConnectToWifi(const SsidItem& item) {
    if (item.ssid.empty() || item.ssid.size() > 32 || item.password.size() > 64) {
        return false;
    }

    is_connecting_ = true;
    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
    }
    esp_wifi_scan_stop();
    xEventGroupClearBits(event_group_, kWifiConnectedBit | kWifiFailedBit);

    wifi_config_t wifi_config = {};
    strlcpy(reinterpret_cast<char*>(wifi_config.sta.ssid), item.ssid.c_str(), sizeof(wifi_config.sta.ssid));
    strlcpy(reinterpret_cast<char*>(wifi_config.sta.password), item.password.c_str(), sizeof(wifi_config.sta.password));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (item.ip_mode == WifiIpMode::Static) {
        esp_netif_ip_info_t ip_info = {};
        esp_netif_dns_info_t dns_info = {};
        if (esp_netif_str_to_ip4(item.ip.c_str(), &ip_info.ip) != ESP_OK ||
            esp_netif_str_to_ip4(item.netmask.c_str(), &ip_info.netmask) != ESP_OK ||
            esp_netif_str_to_ip4(item.gateway.c_str(), &ip_info.gw) != ESP_OK ||
            esp_netif_str_to_ip4(item.dns1.c_str(), &dns_info.ip.u_addr.ip4) != ESP_OK) {
            is_connecting_ = false;
            ScheduleScan();
            return false;
        }
        esp_err_t err = esp_netif_dhcpc_stop(sta_netif_);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            is_connecting_ = false;
            ScheduleScan();
            return false;
        }
        ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif_, &ip_info));
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_MAIN, &dns_info));
        if (!item.dns2.empty()) {
            esp_netif_dns_info_t backup_dns_info = {};
            if (esp_netif_str_to_ip4(item.dns2.c_str(), &backup_dns_info.ip.u_addr.ip4) != ESP_OK) {
                is_connecting_ = false;
                ScheduleScan();
                return false;
            }
            backup_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_BACKUP, &backup_dns_info));
        } else {
            esp_netif_dns_info_t empty_dns_info = {};
            empty_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_BACKUP, &empty_dns_info);
        }
    } else {
        esp_netif_dns_info_t empty_dns_info = {};
        empty_dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(sta_netif_, ESP_NETIF_DNS_BACKUP, &empty_dns_info);
        const esp_err_t err = esp_netif_dhcpc_start(sta_netif_);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            is_connecting_ = false;
            ScheduleScan();
            return false;
        }
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    if (esp_wifi_connect() != ESP_OK) {
        is_connecting_ = false;
        ScheduleScan();
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        event_group_,
        kWifiConnectedBit | kWifiFailedBit,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(10000));
    is_connecting_ = false;

    if ((bits & kWifiConnectedBit) != 0) {
        esp_wifi_disconnect();
        ScheduleScan();
        return true;
    }

    ScheduleScan();
    return false;
}

void WifiConfigurationAp::Save(const SsidItem& item) {
    ESP_LOGI(kTag,
             "saving credentials for SSID '%s', password_len=%u ip_mode=%s",
             item.ssid.c_str(),
             static_cast<unsigned>(item.password.size()),
             item.ip_mode == WifiIpMode::Static ? "static" : "dhcp");
    SsidManager::GetInstance().AddSsid(item);
}

void WifiConfigurationAp::RemoveCredential(const std::string& ssid) {
    SsidManager::GetInstance().RemoveSsid(ssid);
}

std::vector<wifi_ap_record_t> WifiConfigurationAp::GetAccessPoints() {
    RequestScanIfStale();
    std::lock_guard<std::mutex> lock(mutex_);
    return ap_records_;
}

std::string WifiConfigurationAp::GetSsid() const {
    uint8_t mac[6] = {};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", ssid_prefix_.c_str(), mac[4], mac[5]);
    return std::string(ssid);
}

std::string WifiConfigurationAp::GetPassword() const {
    return password_;
}

std::string WifiConfigurationAp::GetWebServerUrl() const {
    return kWebUrl;
}

std::string WifiConfigurationAp::GetPendingSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ssid_;
}

void WifiConfigurationAp::OnCredentialsSubmitted(std::function<void(const SsidItem& item)> callback) {
    on_credentials_submitted_ = std::move(callback);
}

void WifiConfigurationAp::OnExitRequested(std::function<void()> callback) {
    on_exit_requested_ = std::move(callback);
}

void WifiConfigurationAp::StartAccessPoint() {
    ESP_LOGI(kTag, "creating APSTA netifs");
    ap_netif_ = esp_netif_create_default_wifi_ap();
    sta_netif_ = esp_netif_create_default_wifi_sta();

    esp_netif_ip_info_t ip_info = {};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif_));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif_, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif_));

    dns_server_ = std::make_unique<DnsServer>();
    dns_server_->Start(ip_info.gw);

    wifi_config_t wifi_config = {};
    const std::string ssid = GetSsid();
    ESP_LOGI(kTag, "starting AP SSID '%s'", ssid.c_str());
    strlcpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ssid.c_str(), sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = static_cast<uint8_t>(ssid.size());
    wifi_config.ap.max_connection = 4;
    if (password_.size() >= 8) {
        strlcpy(reinterpret_cast<char*>(wifi_config.ap.password), password_.c_str(), sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(kTag, "APSTA mode started");
}

void WifiConfigurationAp::StartWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    ESP_ERROR_CHECK(httpd_start(&server_, &config));
    ESP_LOGI(kTag, "config AP web server started");

    httpd_uri_t index_handler = {};
    index_handler.uri = "/";
    index_handler.method = HTTP_GET;
    index_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        ESP_LOGI(kTag, "GET /");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &index_handler));

    httpd_uri_t done_handler = {};
    done_handler.uri = "/done.html";
    done_handler.method = HTTP_GET;
    done_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        ESP_LOGI(kTag, "GET /done.html");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, done_html_start, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &done_handler));

    httpd_uri_t scan_handler = {};
    scan_handler.uri = "/scan";
    scan_handler.method = HTTP_GET;
    scan_handler.user_ctx = this;
    scan_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        const std::vector<wifi_ap_record_t> aps = self->GetAccessPoints();
        ESP_LOGI(kTag, "GET /scan -> %u APs", static_cast<unsigned>(aps.size()));
        std::string body = "{\"aps\":[";
        for (size_t i = 0; i < aps.size(); ++i) {
            if (i > 0) {
                body += ',';
            }
            body += "{\"ssid\":\"" + EscapeJsonString(reinterpret_cast<const char*>(aps[i].ssid)) + "\",";
            body += "\"rssi\":" + std::to_string(aps[i].rssi) + ",";
            body += "\"authmode\":" + std::to_string(static_cast<int>(aps[i].authmode)) + "}";
        }
        body += "]}";
        SendJson(req, body);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &scan_handler));

    httpd_uri_t setup_context_handler = {};
    setup_context_handler.uri = "/setup-context";
    setup_context_handler.method = HTTP_GET;
    setup_context_handler.user_ctx = this;
    setup_context_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        const std::string ssid = self->GetPendingSsid();
        ESP_LOGI(kTag, "GET /setup-context -> SSID '%s'", ssid.c_str());
        SendJson(req, "{\"ssid\":\"" + EscapeJsonString(ssid) + "\"}");
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &setup_context_handler));

    httpd_uri_t submit_handler = {};
    submit_handler.uri = "/submit";
    submit_handler.method = HTTP_POST;
    submit_handler.user_ctx = this;
    submit_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        ESP_LOGI(kTag, "POST /submit content_len=%d", req->content_len);
        std::string payload;
        if (!ReadRequestBody(req, &payload)) {
            ESP_LOGW(kTag, "submit rejected: invalid payload");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid payload");
            return ESP_FAIL;
        }
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root == nullptr) {
            ESP_LOGW(kTag, "submit rejected: invalid JSON");
            SendJson(req, "{\"success\":false,\"error\":\"JSON 无效\"}");
            return ESP_OK;
        }

        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        SsidItem submitted = {};
        std::string error;
        if (!BuildSubmittedWifiConfig(root, &submitted, &error)) {
            ESP_LOGW(kTag,
                     "submit rejected: ssid_len=%u password_len=%u ip_mode=%s",
                     static_cast<unsigned>(submitted.ssid.size()),
                     static_cast<unsigned>(submitted.password.size()),
                     submitted.ip_mode == WifiIpMode::Static ? "static" : "dhcp");
            cJSON_Delete(root);
            SendJson(req, "{\"success\":false,\"error\":\"" + EscapeJsonString(error) + "\"}");
            return ESP_OK;
        }
        ESP_LOGI(kTag,
                 "submit accepted for SSID '%s' ip_mode=%s",
                 submitted.ssid.c_str(),
                 submitted.ip_mode == WifiIpMode::Static ? "static" : "dhcp");
        self->Save(submitted);
        if (self->on_credentials_submitted_) {
            self->on_credentials_submitted_(submitted);
        }
        SendJson(req, "{\"success\":true}");
        self->ScheduleExit();
        cJSON_Delete(root);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &submit_handler));

    httpd_uri_t credentials_handler = {};
    credentials_handler.uri = "/credentials";
    credentials_handler.method = HTTP_GET;
    credentials_handler.user_ctx = this;
    credentials_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        (void)req->user_ctx;
        const std::vector<SsidItem>& credentials = SsidManager::GetInstance().GetSsidList();
        ESP_LOGI(kTag, "GET /credentials -> %u entries", static_cast<unsigned>(credentials.size()));
        std::string body = "{\"credentials\":[";
        for (size_t i = 0; i < credentials.size(); ++i) {
            if (i > 0) {
                body += ',';
            }
            body += "{\"ssid\":\"" + EscapeJsonString(credentials[i].ssid) + "\",";
            body += "\"isOpen\":" + std::string(credentials[i].password.empty() ? "true" : "false") + ",";
            body += "\"ipMode\":\"" + std::string(credentials[i].ip_mode == WifiIpMode::Static ? "static" : "dhcp") + "\"}";
        }
        body += "]}";
        SendJson(req, body);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &credentials_handler));

    httpd_uri_t delete_credential_handler = {};
    delete_credential_handler.uri = "/credentials/delete";
    delete_credential_handler.method = HTTP_POST;
    delete_credential_handler.user_ctx = this;
    delete_credential_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        std::string payload;
        if (!ReadRequestBody(req, &payload)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid payload");
            return ESP_FAIL;
        }

        cJSON* root = cJSON_Parse(payload.c_str());
        if (root == nullptr) {
            SendJson(req, "{\"success\":false,\"error\":\"JSON 无效\"}");
            return ESP_OK;
        }

        cJSON* ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
        const char* ssid = cJSON_IsString(ssid_item) ? ssid_item->valuestring : "";
        const std::string ssid_value = ssid != nullptr ? ssid : "";
        if (ssid_value.empty()) {
            cJSON_Delete(root);
            SendJson(req, "{\"success\":false,\"error\":\"缺少 Wi‑Fi 名称\"}");
            return ESP_OK;
        }

        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        self->RemoveCredential(ssid_value);
        cJSON_Delete(root);
        SendJson(req, "{\"success\":true}");
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &delete_credential_handler));

    httpd_uri_t exit_handler = {};
    exit_handler.uri = "/exit";
    exit_handler.method = HTTP_POST;
    exit_handler.user_ctx = this;
    exit_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        ESP_LOGI(kTag, "POST /exit");
        SendJson(req, "{\"success\":true}");
        self->ScheduleExit();
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &exit_handler));

    auto captive_portal_handler = [](httpd_req_t* req) -> esp_err_t {
        auto* self = static_cast<WifiConfigurationAp*>(req->user_ctx);
        std::string url = self->GetWebServerUrl();
        if (!self->language_.empty()) {
            url += "/?lang=" + self->language_;
        }
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Location", url.c_str());
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    };

    const char* captive_urls[] = {
        "/generate_204*",
        "/hotspot-detect.html",
        "/mobile/status.php",
        "/ncsi.txt",
        "/fwlink/",
        "/connectivity-check.html",
        "/success.txt",
    };
    for (const char* url : captive_urls) {
        httpd_uri_t redirect_handler = {};
        redirect_handler.uri = url;
        redirect_handler.method = HTTP_GET;
        redirect_handler.user_ctx = this;
        redirect_handler.handler = captive_portal_handler;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &redirect_handler));
    }
}

void WifiConfigurationAp::ScheduleExit() {
    ESP_LOGI(kTag, "schedule config AP exit");
    xTaskCreate(
        [](void* arg) {
            vTaskDelay(pdMS_TO_TICKS(1200));
            auto* self = static_cast<WifiConfigurationAp*>(arg);
            ESP_LOGI(kTag, "config AP exit task running");
            if (self->on_exit_requested_) {
                self->on_exit_requested_();
            }
            vTaskDelete(nullptr);
        },
        "quellog_ap_exit",
        4096,
        this,
        5,
        nullptr);
}

void WifiConfigurationAp::ScheduleScan(int delay_ms) {
    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
        esp_timer_start_once(scan_timer_, static_cast<uint64_t>(std::max(0, delay_ms)) * 1000ULL);
    }
}

void WifiConfigurationAp::ScanTimerCallback(void* arg) {
    auto* self = static_cast<WifiConfigurationAp*>(arg);
    if (self == nullptr || self->is_connecting_ || self->scan_in_progress_) {
        return;
    }

    self->scan_in_progress_ = true;
    const esp_err_t err = esp_wifi_scan_start(nullptr, false);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "config AP scan start failed: %s", esp_err_to_name(err));
        self->scan_in_progress_ = false;
    }
}

void WifiConfigurationAp::RequestScanIfStale() {
    if (is_connecting_ || scan_in_progress_) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (last_scan_us_ != 0 && now_us - last_scan_us_ < kScanCacheTtlUs) {
        return;
    }
    ScheduleScan(0);
}

void WifiConfigurationAp::WifiEventHandler(void* arg,
                                           esp_event_base_t event_base,
                                           int32_t event_id,
                                           void* event_data) {
    (void)event_base;
    auto* self = static_cast<WifiConfigurationAp*>(arg);
    if (event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        std::vector<wifi_ap_record_t> records(ap_count);
        if (ap_count > 0) {
            esp_wifi_scan_get_ap_records(&ap_count, records.data());
            records.resize(ap_count);
        }
        std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& left, const wifi_ap_record_t& right) {
            return left.rssi > right.rssi;
        });
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->ap_records_ = std::move(records);
            self->last_scan_us_ = esp_timer_get_time();
            self->scan_in_progress_ = false;
        }
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        auto* event = static_cast<wifi_event_ap_staconnected_t*>(event_data);
        ESP_LOGI(kTag,
                 "config AP client connected: " MACSTR ", aid=%d",
                 MAC2STR(event->mac),
                 event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
        ESP_LOGI(kTag,
                 "config AP client disconnected: " MACSTR ", aid=%d",
                 MAC2STR(event->mac),
                 event->aid);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED && self->is_connecting_) {
        xEventGroupSetBits(self->event_group_, kWifiFailedBit);
    }
}

void WifiConfigurationAp::IpEventHandler(void* arg,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         void* event_data) {
    (void)event_base;
    (void)event_id;
    auto* self = static_cast<WifiConfigurationAp*>(arg);
    auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
    ESP_LOGI(kTag, "temporary STA got IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));
    xEventGroupSetBits(self->event_group_, kWifiConnectedBit);
}

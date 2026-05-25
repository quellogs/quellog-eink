#include "settings_web_server.h"

#include <esp_err.h>
#include <esp_log.h>

#include <string>

#include <cJSON.h>

#include "device_api_settings.h"

namespace {

constexpr char kTag[] = "SettingsWeb";
constexpr size_t kMaxBodyLength = 2048;

extern const char settings_html_start[] asm("_binary_settings_html_start");

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
    if (body == nullptr || req->content_len <= 0 || req->content_len > kMaxBodyLength) {
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

}  // namespace

SettingsWebServer::~SettingsWebServer() {
    Stop();
}

bool SettingsWebServer::Start() {
    if (server_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "failed to start settings web server: %s", esp_err_to_name(err));
        server_ = nullptr;
        return false;
    }

    httpd_uri_t index_handler = {};
    index_handler.uri = "/";
    index_handler.method = HTTP_GET;
    index_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, settings_html_start, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &index_handler));

    httpd_uri_t get_settings_handler = {};
    get_settings_handler.uri = "/api/settings";
    get_settings_handler.method = HTTP_GET;
    get_settings_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        const DeviceApiConfig config = LoadDeviceApiConfig();
        std::string body = "{\"base_url\":\"" + EscapeJsonString(config.base_url) + "\",";
        body += "\"username\":\"" + EscapeJsonString(config.username) + "\",";
        body += "\"password_configured\":" + std::string(config.password.empty() ? "false" : "true") + "}";
        SendJson(req, body);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &get_settings_handler));

    httpd_uri_t post_settings_handler = {};
    post_settings_handler.uri = "/api/settings";
    post_settings_handler.method = HTTP_POST;
    post_settings_handler.handler = [](httpd_req_t* req) -> esp_err_t {
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

        cJSON* base_url_item = cJSON_GetObjectItemCaseSensitive(root, "base_url");
        cJSON* username_item = cJSON_GetObjectItemCaseSensitive(root, "username");
        cJSON* password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
        const char* base_url = cJSON_IsString(base_url_item) ? base_url_item->valuestring : "";
        const char* username = cJSON_IsString(username_item) ? username_item->valuestring : "";
        const char* password = cJSON_IsString(password_item) ? password_item->valuestring : nullptr;

        DeviceApiConfig config = LoadDeviceApiConfig();
        config.base_url = base_url != nullptr ? base_url : "";
        config.username = username != nullptr ? username : "";
        if (password != nullptr && password[0] != '\0') {
            config.password = password;
        }
        if (NormalizeDeviceApiBaseUrl(config.base_url).empty()) {
            cJSON_Delete(root);
            SendJson(req, "{\"success\":false,\"error\":\"请填写服务地址\"}");
            return ESP_OK;
        }
        if (config.username.empty()) {
            cJSON_Delete(root);
            SendJson(req, "{\"success\":false,\"error\":\"请填写用户名\"}");
            return ESP_OK;
        }
        if (config.password.empty()) {
            cJSON_Delete(root);
            SendJson(req, "{\"success\":false,\"error\":\"请填写密码\"}");
            return ESP_OK;
        }

        SaveDeviceApiConfig(config);
        cJSON_Delete(root);

        const DeviceApiConfig saved = LoadDeviceApiConfig();
        std::string body = "{\"success\":true,\"base_url\":\"" + EscapeJsonString(saved.base_url) + "\",";
        body += "\"username\":\"" + EscapeJsonString(saved.username) + "\",";
        body += "\"password_configured\":" + std::string(saved.password.empty() ? "false" : "true") + "}";
        SendJson(req, body);
        return ESP_OK;
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &post_settings_handler));

    ESP_LOGI(kTag, "settings web server started");
    return true;
}

void SettingsWebServer::Stop() {
    if (server_ == nullptr) {
        return;
    }
    httpd_stop(server_);
    server_ = nullptr;
    ESP_LOGI(kTag, "settings web server stopped");
}

#include "dashboard_data_provider.h"

#include <esp_http_client.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>

#include "device_api_settings.h"

namespace {

constexpr char kTag[] = "DashboardProvider";
constexpr int kHttpTimeoutMs = 8000;
constexpr int kMaxResponseBytes = 16 * 1024;
constexpr char kSessionCookieName[] = "quellog_session";

struct HttpResponse {
    bool ok = false;
    int status_code = 0;
    std::string body;
    std::string error;
    std::string session_cookie;
};

esp_err_t HttpEventHandler(esp_http_client_event_t* event) {
    auto* response = static_cast<HttpResponse*>(event->user_data);
    if (response == nullptr) {
        return ESP_OK;
    }

    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr && event->header_value != nullptr) {
        const std::string key = event->header_key;
        if (key == "Set-Cookie" || key == "set-cookie") {
            const std::string header = event->header_value;
            const std::string prefix = std::string(kSessionCookieName) + "=";
            const size_t begin = header.find(prefix);
            if (begin != std::string::npos) {
                const size_t value_begin = begin + prefix.size();
                const size_t value_end = header.find(';', value_begin);
                const std::string value = header.substr(value_begin, value_end - value_begin);
                if (!value.empty()) {
                    response->session_cookie = prefix + value;
                }
            }
        }
        return ESP_OK;
    }

    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }

    if (response->body.size() + static_cast<size_t>(event->data_len) > kMaxResponseBytes) {
        return ESP_FAIL;
    }

    response->body.append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
    return ESP_OK;
}

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

HttpResponse PostLogin(const DeviceApiConfig& api_config) {
    HttpResponse response;
    const std::string url = api_config.base_url + "/api/auth/login";
    const std::string payload = "{\"username\":\"" + EscapeJsonString(api_config.username) + "\",\"password\":\"" +
        EscapeJsonString(api_config.password) + "\"}";

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    config.event_handler = HttpEventHandler;
    config.user_data = &response;
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        response.error = "HTTP 初始化失败";
        return response;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload.c_str(), static_cast<int>(payload.size()));

    const esp_err_t err = esp_http_client_perform(client);
    response.status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        response.error = esp_err_to_name(err);
        ESP_LOGW(kTag, "POST %s failed: %s", url.c_str(), response.error.c_str());
        return response;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        response.error = response.status_code == 401 ? "用户名或密码错误" : "登录失败 " + std::to_string(response.status_code);
        ESP_LOGW(kTag, "POST %s returned HTTP %d", url.c_str(), response.status_code);
        return response;
    }
    if (response.session_cookie.empty()) {
        response.error = "登录响应缺少会话";
        ESP_LOGW(kTag, "POST %s returned no session cookie", url.c_str());
        return response;
    }

    response.ok = true;
    return response;
}

HttpResponse GetJsonWithCookie(const DeviceApiConfig& api_config, const std::string& path, const std::string& cookie) {
    HttpResponse response;
    const std::string url = api_config.base_url + path;

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kHttpTimeoutMs;
    config.event_handler = HttpEventHandler;
    config.user_data = &response;
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        response.error = "HTTP 初始化失败";
        return response;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Cookie", cookie.c_str());

    const esp_err_t err = esp_http_client_perform(client);
    response.status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        response.error = esp_err_to_name(err);
        ESP_LOGW(kTag, "GET %s failed: %s", url.c_str(), response.error.c_str());
        return response;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        response.error = "同步失败 " + std::to_string(response.status_code);
        ESP_LOGW(kTag, "GET %s returned HTTP %d", url.c_str(), response.status_code);
        return response;
    }

    response.ok = true;
    return response;
}

HttpResponse GetJson(const DeviceApiConfig& api_config, const std::string& path, std::string* cookie) {
    if (cookie == nullptr || cookie->empty()) {
        const HttpResponse login_response = PostLogin(api_config);
        if (!login_response.ok) {
            return login_response;
        }
        *cookie = login_response.session_cookie;
    }

    HttpResponse response = GetJsonWithCookie(api_config, path, *cookie);
    if (response.status_code != 401) {
        return response;
    }

    cookie->clear();
    const HttpResponse login_response = PostLogin(api_config);
    if (!login_response.ok) {
        return login_response;
    }
    *cookie = login_response.session_cookie;
    return GetJsonWithCookie(api_config, path, *cookie);
}

int64_t GetJsonInt64(cJSON* object, const char* name, int64_t default_value = 0) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return default_value;
    }
    return static_cast<int64_t>(item->valuedouble);
}

int GetJsonInt(cJSON* object, const char* name, int default_value = 0) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

std::string GetJsonString(cJSON* object, const char* name, const std::string& default_value = "") {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return default_value;
    }
    return item->valuestring;
}

const char* DashboardPeriodQueryValue(DashboardPeriod period) {
    switch (period) {
        case DashboardPeriod::Quarter:
            return "quarter";
        case DashboardPeriod::Year:
            return "year";
        case DashboardPeriod::Month:
        default:
            return "month";
    }
}

DashboardPeriod ParseDashboardPeriod(const std::string& period) {
    if (period == "quarter") {
        return DashboardPeriod::Quarter;
    }
    if (period == "year") {
        return DashboardPeriod::Year;
    }
    return DashboardPeriod::Month;
}

bool ParseDashboard(const std::string& body, DashboardData* data, std::string* error) {
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        if (error != nullptr) {
            *error = "看板 JSON 无效";
        }
        return false;
    }

    cJSON* dashboard = cJSON_GetObjectItemCaseSensitive(root, "dashboard");
    if (!cJSON_IsObject(dashboard)) {
        cJSON_Delete(root);
        if (error != nullptr) {
            *error = "看板响应无效";
        }
        return false;
    }

    DashboardData parsed;
    parsed.period = ParseDashboardPeriod(GetJsonString(dashboard, "period", "month"));
    cJSON* range = cJSON_GetObjectItemCaseSensitive(dashboard, "range");
    if (cJSON_IsObject(range)) {
        parsed.range.from = GetJsonString(range, "from", "");
        parsed.range.to = GetJsonString(range, "to", "");
    }
    parsed.today_expense_cents = GetJsonInt64(dashboard, "today_expense_cents");
    parsed.period_expense_cents = GetJsonInt64(dashboard, "period_expense_cents");
    parsed.budget_used_percent = GetJsonInt(dashboard, "budget_used_percent");
    parsed.sync_status = GetJsonString(dashboard, "sync_status", "已同步");

    cJSON* categories = cJSON_GetObjectItemCaseSensitive(dashboard, "categories");
    if (cJSON_IsArray(categories)) {
        cJSON* category = nullptr;
        cJSON_ArrayForEach(category, categories) {
            if (!cJSON_IsObject(category)) {
                continue;
            }
            parsed.categories.push_back({
                GetJsonString(category, "category", ""),
                GetJsonInt(category, "percent"),
                GetJsonInt64(category, "amount_cents"),
            });
        }
    }

    cJSON_Delete(root);
    if (data != nullptr) {
        *data = std::move(parsed);
    }
    return true;
}

bool ParseTransactionsIntoRecentRecords(const std::string& body, DashboardData* data, std::string* error) {
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        if (error != nullptr) {
            *error = "明细 JSON 无效";
        }
        return false;
    }

    cJSON* transactions = cJSON_GetObjectItemCaseSensitive(root, "transactions");
    if (!cJSON_IsArray(transactions)) {
        cJSON_Delete(root);
        if (error != nullptr) {
            *error = "明细响应无效";
        }
        return false;
    }

    std::vector<RecordSummary> records;
    records.reserve(static_cast<size_t>(cJSON_GetArraySize(transactions)));
    cJSON* transaction = nullptr;
    cJSON_ArrayForEach(transaction, transactions) {
        if (!cJSON_IsObject(transaction)) {
            continue;
        }
        records.push_back({
            GetJsonString(transaction, "title", GetJsonString(transaction, "category", "")),
            GetJsonString(transaction, "category", ""),
            GetJsonInt64(transaction, "amount_cents"),
        });
    }

    cJSON_Delete(root);
    if (data != nullptr) {
        data->recent_records = std::move(records);
    }
    return true;
}

}  // namespace

DashboardLoadResult LoadDashboardData(DashboardPeriod period) {
    DashboardLoadResult result;
    const DeviceApiConfig config = LoadDeviceApiConfig();
    if (config.base_url.empty() || config.username.empty() || config.password.empty()) {
        result.status_message = "未配置服务接口";
        result.data.sync_status = result.status_message;
        return result;
    }

    std::string session_cookie;
    const std::string dashboard_path =
        std::string("/api/client/dashboard?period=") + DashboardPeriodQueryValue(period);
    const HttpResponse dashboard_response = GetJson(config, dashboard_path, &session_cookie);
    if (!dashboard_response.ok) {
        result.status_message = dashboard_response.error.empty() ? "同步失败" : dashboard_response.error;
        result.data.sync_status = result.status_message;
        return result;
    }

    std::string parse_error;
    if (!ParseDashboard(dashboard_response.body, &result.data, &parse_error)) {
        result.status_message = parse_error;
        result.data.sync_status = result.status_message;
        return result;
    }

    const HttpResponse transactions_response = GetJson(config, "/api/client/transactions?limit=20&offset=0", &session_cookie);
    if (!transactions_response.ok) {
        result.status_message = transactions_response.error.empty() ? "明细同步失败" : transactions_response.error;
        result.data.sync_status = result.status_message;
        return result;
    }

    if (!ParseTransactionsIntoRecentRecords(transactions_response.body, &result.data, &parse_error)) {
        result.status_message = parse_error;
        result.data.sync_status = result.status_message;
        return result;
    }

    result.success = true;
    result.status_message = result.data.sync_status.empty() ? "已同步" : result.data.sync_status;
    result.data.sync_status = result.status_message;
    return result;
}

#include "device_api_settings.h"

#include <algorithm>
#include <cctype>

#include "settings.h"

namespace {

constexpr char kNamespace[] = "device_api";
constexpr char kBaseUrlKey[] = "base_url";
constexpr char kApiTokenKey[] = "api_token";

std::string Trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

}  // namespace

std::string NormalizeDeviceApiBaseUrl(const std::string& base_url) {
    std::string normalized = Trim(base_url);
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

DeviceApiConfig LoadDeviceApiConfig() {
    Settings settings(kNamespace, false);
    DeviceApiConfig config;
    config.base_url = NormalizeDeviceApiBaseUrl(settings.GetString(kBaseUrlKey, ""));
    config.api_token = Trim(settings.GetString(kApiTokenKey, ""));
    return config;
}

void SaveDeviceApiConfig(const DeviceApiConfig& config) {
    Settings settings(kNamespace, true);
    settings.SetString(kBaseUrlKey, NormalizeDeviceApiBaseUrl(config.base_url));
    settings.SetString(kApiTokenKey, Trim(config.api_token));
}

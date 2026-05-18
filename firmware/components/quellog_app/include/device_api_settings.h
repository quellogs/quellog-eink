#ifndef QUELLOG_DEVICE_API_SETTINGS_H_
#define QUELLOG_DEVICE_API_SETTINGS_H_

#include <string>

struct DeviceApiConfig {
    std::string base_url;
    std::string api_token;
};

DeviceApiConfig LoadDeviceApiConfig();
void SaveDeviceApiConfig(const DeviceApiConfig& config);
std::string NormalizeDeviceApiBaseUrl(const std::string& base_url);

#endif  // QUELLOG_DEVICE_API_SETTINGS_H_

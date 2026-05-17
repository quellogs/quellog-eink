#include "settings.h"

#include <esp_err.h>
#include <esp_log.h>

namespace {

constexpr char kTag[] = "Settings";

}  // namespace

Settings::Settings(const std::string& ns, bool read_write)
    : ns_(ns), read_write_(read_write) {
    nvs_open(ns_.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &nvs_handle_);
}

Settings::~Settings() {
    if (nvs_handle_ == 0) {
        return;
    }

    if (read_write_ && dirty_) {
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_));
    }
    nvs_close(nvs_handle_);
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK) {
        return default_value;
    }

    std::string value(length, '\0');
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle_, key.c_str(), value.data(), &length));
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(kTag, "namespace %s is not writable", ns_.c_str());
        return;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs_handle_, key.c_str(), value.c_str()));
    dirty_ = true;
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    int32_t value = default_value;
    if (nvs_get_i32(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

void Settings::SetInt(const std::string& key, int32_t value) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(kTag, "namespace %s is not writable", ns_.c_str());
        return;
    }

    ESP_ERROR_CHECK(nvs_set_i32(nvs_handle_, key.c_str(), value));
    dirty_ = true;
}

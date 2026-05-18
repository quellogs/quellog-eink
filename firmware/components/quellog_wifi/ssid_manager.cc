#include "ssid_manager.h"

#include <esp_log.h>
#include <nvs.h>

#include <algorithm>

namespace {

constexpr char kTag[] = "SsidManager";
constexpr char kNamespace[] = "wifi";
constexpr int kMaxWifiSsidCount = 5;

std::string MakeIndexedKey(const char* base, int index) {
    return index == 0 ? std::string(base) : std::string(base) + std::to_string(index);
}

}  // namespace

SsidManager& SsidManager::GetInstance() {
    static SsidManager instance;
    return instance;
}

SsidManager::SsidManager() {
    LoadFromNvs();
}

void SsidManager::AddSsid(const std::string& ssid, const std::string& password) {
    SsidItem item = {};
    item.ssid = ssid;
    item.password = password;
    item.ip_mode = WifiIpMode::Dhcp;
    AddSsid(item);
}

void SsidManager::AddSsid(const SsidItem& item) {
    const std::string& ssid = item.ssid;
    if (ssid.empty()) {
        return;
    }

    auto existing = std::find_if(ssid_list_.begin(), ssid_list_.end(), [&ssid](const SsidItem& item) {
        return item.ssid == ssid;
    });
    if (existing != ssid_list_.end()) {
        SsidItem updated = item;
        ssid_list_.erase(existing);
        ssid_list_.insert(ssid_list_.begin(), updated);
    } else {
        ssid_list_.insert(ssid_list_.begin(), item);
        if (static_cast<int>(ssid_list_.size()) > kMaxWifiSsidCount) {
            ssid_list_.resize(kMaxWifiSsidCount);
        }
    }
    SaveToNvs();
}

void SsidManager::RemoveSsid(const std::string& ssid) {
    if (ssid.empty()) {
        return;
    }

    auto existing = std::find_if(ssid_list_.begin(), ssid_list_.end(), [&ssid](const SsidItem& item) {
        return item.ssid == ssid;
    });
    if (existing == ssid_list_.end()) {
        return;
    }

    ssid_list_.erase(existing);
    SaveToNvs();
}

void SsidManager::Clear() {
    ssid_list_.clear();
    SaveToNvs();
}

void SsidManager::LoadFromNvs() {
    ssid_list_.clear();

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(kTag, "no saved wifi credentials");
        return;
    }

    for (int i = 0; i < kMaxWifiSsidCount; ++i) {
        const std::string ssid_key = MakeIndexedKey("ssid", i);
        const std::string password_key = MakeIndexedKey("password", i);
        const std::string ip_mode_key = MakeIndexedKey("ipMode", i);
        const std::string ip_key = MakeIndexedKey("ip", i);
        const std::string netmask_key = MakeIndexedKey("netmask", i);
        const std::string gateway_key = MakeIndexedKey("gateway", i);
        const std::string dns1_key = MakeIndexedKey("dns1", i);
        const std::string dns2_key = MakeIndexedKey("dns2", i);

        char ssid[33] = {};
        char password[65] = {};
        char ip_mode[8] = {};
        char ip[16] = {};
        char netmask[16] = {};
        char gateway[16] = {};
        char dns1[16] = {};
        char dns2[16] = {};
        size_t ssid_length = sizeof(ssid);
        size_t password_length = sizeof(password);
        size_t ip_mode_length = sizeof(ip_mode);
        size_t ip_length = sizeof(ip);
        size_t netmask_length = sizeof(netmask);
        size_t gateway_length = sizeof(gateway);
        size_t dns1_length = sizeof(dns1);
        size_t dns2_length = sizeof(dns2);
        if (nvs_get_str(handle, ssid_key.c_str(), ssid, &ssid_length) != ESP_OK) {
            continue;
        }
        if (nvs_get_str(handle, password_key.c_str(), password, &password_length) != ESP_OK) {
            password[0] = '\0';
        }
        SsidItem item = {};
        item.ssid = ssid;
        item.password = password;
        if (nvs_get_str(handle, ip_mode_key.c_str(), ip_mode, &ip_mode_length) == ESP_OK &&
            std::string(ip_mode) == "static") {
            item.ip_mode = WifiIpMode::Static;
            if (nvs_get_str(handle, ip_key.c_str(), ip, &ip_length) == ESP_OK) {
                item.ip = ip;
            }
            if (nvs_get_str(handle, netmask_key.c_str(), netmask, &netmask_length) == ESP_OK) {
                item.netmask = netmask;
            }
            if (nvs_get_str(handle, gateway_key.c_str(), gateway, &gateway_length) == ESP_OK) {
                item.gateway = gateway;
            }
            if (nvs_get_str(handle, dns1_key.c_str(), dns1, &dns1_length) == ESP_OK) {
                item.dns1 = dns1;
            }
            if (nvs_get_str(handle, dns2_key.c_str(), dns2, &dns2_length) == ESP_OK) {
                item.dns2 = dns2;
            }
        }
        ssid_list_.push_back(item);
    }

    nvs_close(handle);
}

void SsidManager::SaveToNvs() {
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(kNamespace, NVS_READWRITE, &handle));

    for (int i = 0; i < kMaxWifiSsidCount; ++i) {
        const std::string ssid_key = MakeIndexedKey("ssid", i);
        const std::string password_key = MakeIndexedKey("password", i);
        const std::string ip_mode_key = MakeIndexedKey("ipMode", i);
        const std::string ip_key = MakeIndexedKey("ip", i);
        const std::string netmask_key = MakeIndexedKey("netmask", i);
        const std::string gateway_key = MakeIndexedKey("gateway", i);
        const std::string dns1_key = MakeIndexedKey("dns1", i);
        const std::string dns2_key = MakeIndexedKey("dns2", i);
        if (i < static_cast<int>(ssid_list_.size())) {
            const SsidItem& item = ssid_list_[i];
            ESP_ERROR_CHECK(nvs_set_str(handle, ssid_key.c_str(), item.ssid.c_str()));
            ESP_ERROR_CHECK(nvs_set_str(handle, password_key.c_str(), item.password.c_str()));
            ESP_ERROR_CHECK(nvs_set_str(
                handle, ip_mode_key.c_str(), item.ip_mode == WifiIpMode::Static ? "static" : "dhcp"));
            if (item.ip_mode == WifiIpMode::Static) {
                ESP_ERROR_CHECK(nvs_set_str(handle, ip_key.c_str(), item.ip.c_str()));
                ESP_ERROR_CHECK(nvs_set_str(handle, netmask_key.c_str(), item.netmask.c_str()));
                ESP_ERROR_CHECK(nvs_set_str(handle, gateway_key.c_str(), item.gateway.c_str()));
                ESP_ERROR_CHECK(nvs_set_str(handle, dns1_key.c_str(), item.dns1.c_str()));
                if (!item.dns2.empty()) {
                    ESP_ERROR_CHECK(nvs_set_str(handle, dns2_key.c_str(), item.dns2.c_str()));
                } else {
                    nvs_erase_key(handle, dns2_key.c_str());
                }
            } else {
                nvs_erase_key(handle, ip_key.c_str());
                nvs_erase_key(handle, netmask_key.c_str());
                nvs_erase_key(handle, gateway_key.c_str());
                nvs_erase_key(handle, dns1_key.c_str());
                nvs_erase_key(handle, dns2_key.c_str());
            }
        } else {
            nvs_erase_key(handle, ssid_key.c_str());
            nvs_erase_key(handle, password_key.c_str());
            nvs_erase_key(handle, ip_mode_key.c_str());
            nvs_erase_key(handle, ip_key.c_str());
            nvs_erase_key(handle, netmask_key.c_str());
            nvs_erase_key(handle, gateway_key.c_str());
            nvs_erase_key(handle, dns1_key.c_str());
            nvs_erase_key(handle, dns2_key.c_str());
        }
    }

    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

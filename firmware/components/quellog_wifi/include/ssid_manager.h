#ifndef QUELLOG_WIFI_SSID_MANAGER_H_
#define QUELLOG_WIFI_SSID_MANAGER_H_

#include <string>
#include <vector>

enum class WifiIpMode {
    Dhcp,
    Static,
};

struct SsidItem {
    std::string ssid;
    std::string password;
    WifiIpMode ip_mode = WifiIpMode::Dhcp;
    std::string ip;
    std::string netmask;
    std::string gateway;
    std::string dns1;
    std::string dns2;
};

class SsidManager {
public:
    static SsidManager& GetInstance();

    void AddSsid(const std::string& ssid, const std::string& password);
    void AddSsid(const SsidItem& item);
    void RemoveSsid(const std::string& ssid);
    void Clear();
    const std::vector<SsidItem>& GetSsidList() const { return ssid_list_; }

private:
    SsidManager();

    void LoadFromNvs();
    void SaveToNvs();

    std::vector<SsidItem> ssid_list_;
};

#endif  // QUELLOG_WIFI_SSID_MANAGER_H_

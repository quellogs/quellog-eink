#ifndef QUELLOG_WIFI_CONNECTION_INFO_H_
#define QUELLOG_WIFI_CONNECTION_INFO_H_

#include <string>

struct WifiConnectionInfo {
    std::string ssid;
    std::string ip_address;
    std::string netmask;
    std::string gateway;
    std::string dns_main;
    std::string dns_backup;
};

#endif  // QUELLOG_WIFI_CONNECTION_INFO_H_

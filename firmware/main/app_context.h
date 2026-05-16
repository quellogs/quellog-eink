#ifndef QUELLOG_APP_CONTEXT_H_
#define QUELLOG_APP_CONTEXT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "device_state.h"

struct RecordSummary {
    std::string title;
    std::string category;
    int64_t amount_cents = 0;
};

struct CategorySummary {
    std::string category;
    int percent = 0;
    int64_t amount_cents = 0;
};

struct DashboardData {
    int64_t today_expense_cents = 0;
    int64_t month_expense_cents = 0;
    int budget_used_percent = 0;
    std::string sync_status;
    std::vector<RecordSummary> recent_records;
    std::vector<CategorySummary> categories;
};

struct StorageInfo {
    bool available = false;
    uint32_t flash_total_kb = 0;
    uint32_t app_total_kb = 0;
    uint32_t app_used_kb = 0;
    uint32_t nvs_total_kb = 0;
    uint32_t nvs_used_kb = 0;
};

struct AppContext {
    DeviceState device_state = kDeviceStateUnknown;
    int page_index = 0;
    int page_count = 0;
    bool battery_known = false;
    int battery_level = 0;
    bool battery_charging = false;
    bool battery_capacity_mah_known = false;
    int battery_capacity_mah = 0;
    bool wifi_connected = false;
    bool wifi_connecting = false;
    bool wifi_config_mode = false;
    bool wifi_enabled = false;
    bool bluetooth_available = false;
    bool bluetooth_enabled = false;
    int volume_percent = 0;
    std::string page_title;
    std::string device_alias;
    std::string board_type;
    std::string cpu_info;
    std::string device_uuid;
    std::string wifi_ssid;
    std::string wifi_ip;
    std::string wifi_ap_ssid;
    std::string wifi_ap_url;
    int settings_selected_item = 0;
    int settings_item_count = 0;
    StorageInfo storage;
    DashboardData dashboard;
};

#endif  // QUELLOG_APP_CONTEXT_H_

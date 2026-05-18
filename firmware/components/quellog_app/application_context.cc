#include "application.h"

#include <esp_timer.h>

#include <algorithm>

namespace {

constexpr int kStatsPageIndex = 0;
constexpr int kSettingsPageOffsetFromEnd = 1;
constexpr int kSettingsItemWifi = 0;
constexpr int kSettingsItemCount = 6;
constexpr int kBatteryStatusCheckIntervalUs = 3 * 1000 * 1000;
constexpr int kRecentRecordsPageSize = 6;

const char* DashboardPeriodLabel(DashboardPeriod period) {
    switch (period) {
        case DashboardPeriod::Quarter:
            return "季度";
        case DashboardPeriod::Year:
            return "年度";
        case DashboardPeriod::Month:
        default:
            return "月度";
    }
}

}  // namespace

AppContext Application::BuildContext() const {
    AppContext context;
    context.device_state = state_.load(std::memory_order_acquire);
    context.page_index = current_page_index_;
    context.page_count = pages_.Count();
    context.device_alias = device_alias_;
    context.board_type = board_.GetBoardType();
    context.device_uuid = board_.GetUuid();
    context.cpu_info = board_.GetCpuInfo();
    context.wifi_connected = board_.IsWifiConnected();
    context.wifi_connecting = context.device_state == kDeviceStateWifiConnecting;
    context.wifi_config_mode = board_.IsWifiConfigMode();
    context.wifi_enabled = board_.IsWifiEnabled();
    context.bluetooth_available = board_.IsBluetoothAvailable();
    context.bluetooth_enabled = board_.IsBluetoothEnabled();
    context.volume_percent = board_.GetVolumePercent();
    context.wifi_ssid = board_.GetWifiSsid();
    context.wifi_ip = board_.GetWifiIpAddress();
    context.wifi_ap_ssid = board_.GetWifiConfigApSsid();
    context.wifi_ap_url = board_.GetWifiConfigApUrl();
    context.settings_selected_item = settings_selected_item_;
    context.settings_item_count = kSettingsItemCount;
    context.settings_detail_focused = settings_detail_focused_;
    context.settings_wifi_focus_index = std::clamp(settings_wifi_focus_index_, 0, std::max(0, GetWifiFocusItemCount() - 1));
    context.settings_wifi_ap_modal_visible = settings_wifi_ap_modal_visible_;
    context.settings_wifi_connecting_modal_visible = settings_wifi_connecting_modal_visible_;
    context.settings_restart_modal_visible = settings_restart_modal_visible_;
    context.settings_restart_confirm_focused = settings_restart_confirm_focused_;
    context.wifi_mode = GetCurrentWifiSettingsMode();
    context.pending_wifi_config_ssid = board_.GetPendingWifiConfigSsid();
    const std::vector<BoardWifiNetwork> wifi_networks =
        context.settings_wifi_ap_modal_visible ? settings_wifi_cached_networks_ : board_.GetScannedWifiNetworks();
    for (const BoardWifiNetwork& network : wifi_networks) {
        context.wifi_networks.push_back({
            network.ssid,
            network.rssi,
            network.secure,
        });
    }
    const BoardStorageInfo storage = board_.GetStorageInfo();
    context.storage.available = storage.available;
    context.storage.flash_total_kb = storage.flash_total_kb;
    context.storage.app_total_kb = storage.app_total_kb;
    context.storage.app_used_kb = storage.app_used_kb;
    context.storage.nvs_total_kb = storage.nvs_total_kb;
    context.storage.nvs_used_kb = storage.nvs_used_kb;
    context.dashboard = dashboard_;
    context.recent_records_page_index = recent_records_page_index_;
    context.recent_records_page_size = kRecentRecordsPageSize;
    context.stats_period = stats_period_;
    context.stats_period_modal_visible = stats_period_modal_visible_;
    context.stats_period_focus_index = stats_period_focus_index_;

    int battery_level = 0;
    bool battery_charging = false;
    bool battery_external_power = false;
    context.battery_known = board_.GetBatteryLevel(battery_level, battery_charging, battery_external_power);
    context.battery_level = battery_level;
    context.battery_charging = battery_charging;
    int battery_capacity_mah = 0;
    context.battery_capacity_mah_known = board_.GetBatteryCapacityMah(battery_capacity_mah);
    context.battery_capacity_mah = battery_capacity_mah;

    const UiPage* page = pages_.Get(current_page_index_);
    context.page_title = page == nullptr ? "" : page->GetTitle();
    return context;
}

TopStatusBarState Application::BuildTopStatusBarState(const AppContext& context) const {
    TopStatusBarState state;
    state.title = context.page_title;
    if (context.page_index == kStatsPageIndex) {
        state.title += " - ";
        state.title += DashboardPeriodLabel(context.stats_period);
    }
    state.wifi_visible = context.wifi_enabled;
    state.wifi_connected = context.wifi_connected;
    state.hotspot_visible = context.wifi_config_mode;
    state.bluetooth_visible = context.bluetooth_enabled;
    state.battery_visible = context.battery_known;
    state.battery_level = context.battery_level;
    state.battery_charging = context.battery_charging;
    return state;
}

bool Application::ShouldAutoRefresh(int64_t now_us) const {
    (void)now_us;
    return false;
}

bool Application::HasBatteryChargingStateChanged(int64_t now_us) {
    if (now_us - last_battery_status_check_us_ < kBatteryStatusCheckIntervalUs) {
        return false;
    }

    last_battery_status_check_us_ = now_us;

    int battery_level = 0;
    bool battery_charging = false;
    bool battery_external_power = false;
    const bool battery_known = board_.GetBatteryLevel(battery_level, battery_charging, battery_external_power);

    if (!battery_status_initialized_) {
        battery_status_initialized_ = true;
        last_battery_known_ = battery_known;
        last_battery_charging_ = battery_charging;
        return false;
    }

    if (battery_known == last_battery_known_ && battery_charging == last_battery_charging_) {
        return false;
    }

    last_battery_known_ = battery_known;
    last_battery_charging_ = battery_charging;
    return true;
}

void Application::HandleNetworkEvent(NetworkEvent event, const std::string& data) {
    (void)data;
    switch (event) {
        case NetworkEvent::Scanning:
            break;
        case NetworkEvent::Connecting:
            if (settings_wifi_connecting_modal_visible_) {
                network_state_dirty_.store(true, std::memory_order_release);
            }
            break;
        case NetworkEvent::Connected:
            settings_wifi_ap_modal_visible_ = false;
            settings_wifi_connecting_modal_visible_ = false;
            refresh_requested_.store(true, std::memory_order_release);
            network_state_dirty_.store(true, std::memory_order_release);
            break;
        case NetworkEvent::Disconnected:
            network_state_dirty_.store(true, std::memory_order_release);
            break;
        case NetworkEvent::WifiConfigModeEnter:
            settings_wifi_connecting_modal_visible_ = false;
            if (settings_selected_item_ == kSettingsItemWifi) {
                settings_wifi_ap_modal_visible_ = true;
            }
            network_state_dirty_.store(true, std::memory_order_release);
            break;
        case NetworkEvent::WifiConfigModeExit:
            if (settings_wifi_ap_modal_visible_) {
                settings_wifi_ap_modal_visible_ = false;
                settings_wifi_connecting_modal_visible_ = true;
            }
            network_state_dirty_.store(true, std::memory_order_release);
            break;
    }
}

void Application::UpdateDeviceState() {
    const NetworkState network_state = board_.GetNetworkState();
    DeviceState next_state = kDeviceStateIdle;
    switch (network_state) {
        case NetworkState::ConfigMode:
            next_state = kDeviceStateWifiConfig;
            break;
        case NetworkState::Scanning:
        case NetworkState::Connecting:
            next_state = kDeviceStateWifiConnecting;
            break;
        case NetworkState::Disconnected:
            next_state = kDeviceStateWifiDisconnected;
            break;
        case NetworkState::Connected:
            next_state = IsSettingsPage() ? kDeviceStateSettings : kDeviceStateIdle;
            break;
        case NetworkState::Unknown:
        default:
            next_state = IsSettingsPage() ? kDeviceStateSettings : kDeviceStateIdle;
            break;
    }
    state_.store(next_state, std::memory_order_release);
}

void Application::UpdateSettingsWebServer() {
    if (board_.IsWifiConnected() && !board_.IsWifiConfigMode()) {
        settings_web_server_.Start();
        return;
    }
    settings_web_server_.Stop();
}

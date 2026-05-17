#include "application.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "settings.h"

namespace {

constexpr char kTag[] = "Application";
constexpr int kStatsPageIndex = 0;
constexpr int kSettingsPageOffsetFromEnd = 1;
constexpr int kSettingsItemWifi = 0;
constexpr int kSettingsItemBluetooth = 1;
constexpr int kSettingsItemSound = 2;
constexpr int kSettingsItemStorage = 3;
constexpr int kSettingsItemDeviceInfo = 4;
constexpr int kSettingsItemCount = 5;
constexpr int kVolumeStepPercent = 10;
constexpr int64_t kBatteryStatusCheckIntervalUs = 3 * 1000 * 1000;

}  // namespace

Application::Application()
    : board_(Board::GetInstance()) {
}

void Application::Initialize() {
    ESP_LOGI(kTag, "initialize start");
    display_ = board_.GetDisplay();
    ESP_LOGI(kTag, "display acquired");
    pages_ = UiPageRegistry::CreateDefault();
    board_.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        HandleNetworkEvent(event, data);
    });
    ESP_LOGI(kTag, "network start begin");
    board_.StartNetwork();
    ESP_LOGI(kTag, "network start end");
    LoadSettings();
    SeedMockData();
    state_.store(kDeviceStateStarting, std::memory_order_release);
    UpdateDeviceState();
    last_refresh_us_ = esp_timer_get_time();
    ESP_LOGI(kTag, "first render begin");
    RenderCurrentPage(true);
    ESP_LOGI(kTag, "first render end");
}

void Application::Run() {
    while (true) {
        InputEvent event;
        if (board_.PollInput(event)) {
            HandleInput(event);
        }

        const int64_t now_us = esp_timer_get_time();
        if (ShouldAutoRefresh(now_us)) {
            TriggerRefresh();
        }

        if (HasBatteryChargingStateChanged(now_us)) {
            RenderCurrentPage(false);
        }

        if (network_state_dirty_.exchange(false, std::memory_order_acq_rel)) {
            UpdateDeviceState();
            RenderCurrentPage(true);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Application::HandleInput(const InputEvent& event) {
    if (IsSettingsPage()) {
        if (settings_selected_item_ == kSettingsItemWifi) {
            if (settings_wifi_connecting_modal_visible_) {
                return;
            }

            if (settings_wifi_ap_modal_visible_) {
                switch (event.key) {
                    case InputKey::Confirm:
                    case InputKey::OpenSettings:
                        CloseWifiApModal();
                        return;
                    case InputKey::Up:
                    case InputKey::Down:
                    case InputKey::None:
                        return;
                }
            }

            switch (event.key) {
                case InputKey::Up:
                    PreviousWifiFocus();
                    return;
                case InputKey::Down:
                    NextWifiFocus();
                    return;
                case InputKey::Confirm:
                    ExecuteWifiFocus();
                    return;
                case InputKey::OpenSettings:
                    CloseSettingsPage();
                    return;
                case InputKey::None:
                    return;
            }
        }

        switch (event.key) {
            case InputKey::Up:
                PreviousSettingsItem();
                return;
            case InputKey::Down:
                NextSettingsItem();
                return;
            case InputKey::Confirm:
                ExecuteSettingsItem();
                return;
            case InputKey::OpenSettings:
                CloseSettingsPage();
                return;
            case InputKey::None:
                return;
        }
    }

    switch (event.key) {
        case InputKey::Up:
            PreviousPage();
            break;
        case InputKey::Down:
            NextPage();
            break;
        case InputKey::Confirm:
            TriggerRefresh();
            break;
        case InputKey::OpenSettings:
            OpenSettingsPage();
            break;
        case InputKey::None:
            break;
    }
}

void Application::OpenSettingsPage() {
    if (pages_.Count() <= 0) {
        return;
    }

    const int browseable_page_count = std::max(1, pages_.Count() - 1);
    if (!IsSettingsPage()) {
        settings_return_page_index_ = std::clamp(current_page_index_, 0, browseable_page_count - 1);
    }
    settings_selected_item_ = 0;
    settings_wifi_focus_index_ = 0;
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_wifi_cached_networks_.clear();
    current_page_index_ = pages_.Count() - kSettingsPageOffsetFromEnd;
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::CloseSettingsPage() {
    const int browseable_page_count = std::max(1, pages_.Count() - 1);
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_wifi_cached_networks_.clear();
    current_page_index_ = std::clamp(settings_return_page_index_, 0, browseable_page_count - 1);
    SaveSettings();
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::RenderCurrentPage(bool full_refresh) {
    const UiPage* page = pages_.Get(current_page_index_);
    if (page == nullptr || display_ == nullptr) {
        state_.store(kDeviceStateError, std::memory_order_release);
        return;
    }

    if (full_refresh) {
        display_->RequestFullRefresh();
    } else {
        display_->RequestPartialRefresh();
    }

    const AppContext context = BuildContext();
    last_battery_status_check_us_ = esp_timer_get_time();
    battery_status_initialized_ = true;
    last_battery_known_ = context.battery_known;
    last_battery_charging_ = context.battery_charging;

    const PageModel model = page->BuildModel(context);
    display_->RenderPage(model, BuildTopStatusBarState(context));
}

void Application::NextPage() {
    const int browseable_page_count = std::max(1, pages_.Count() - 1);
    if (current_page_index_ >= browseable_page_count) {
        current_page_index_ = kStatsPageIndex;
    } else {
        current_page_index_ = (current_page_index_ + 1) % browseable_page_count;
    }
    SaveSettings();
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::PreviousPage() {
    const int browseable_page_count = std::max(1, pages_.Count() - 1);
    if (current_page_index_ >= browseable_page_count) {
        current_page_index_ = browseable_page_count - 1;
    } else {
        current_page_index_ = (current_page_index_ - 1 + browseable_page_count) % browseable_page_count;
    }
    SaveSettings();
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::NextSettingsItem() {
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_wifi_cached_networks_.clear();
    settings_selected_item_ = (settings_selected_item_ + 1) % kSettingsItemCount;
    RenderCurrentPage(false);
}

void Application::PreviousSettingsItem() {
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_wifi_cached_networks_.clear();
    settings_selected_item_ = (settings_selected_item_ + kSettingsItemCount - 1) % kSettingsItemCount;
    RenderCurrentPage(false);
}

void Application::NextWifiFocus() {
    const int item_count = GetWifiFocusItemCount();
    settings_wifi_focus_index_ = item_count > 0 ? (settings_wifi_focus_index_ + 1) % item_count : 0;
    if (settings_wifi_focus_index_ == 0) {
        NextSettingsItem();
        return;
    }
    RenderCurrentPage(false);
}

void Application::PreviousWifiFocus() {
    if (settings_wifi_focus_index_ == 0) {
        PreviousSettingsItem();
        return;
    }
    const int item_count = GetWifiFocusItemCount();
    settings_wifi_focus_index_ =
        item_count > 0 ? (settings_wifi_focus_index_ + item_count - 1) % item_count : 0;
    RenderCurrentPage(false);
}

void Application::CloseWifiApModal() {
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    board_.StopNetwork();
    board_.StartNetwork();
    settings_wifi_cached_networks_.clear();
    network_state_dirty_.store(true, std::memory_order_release);
    RenderCurrentPage(true);
}

void Application::TriggerRefresh() {
    state_.store(kDeviceStateRefreshing, std::memory_order_release);
    ++refresh_count_;
    dashboard_.sync_status = "本地快照 #" + std::to_string(refresh_count_);
    last_refresh_us_ = esp_timer_get_time();
    RenderCurrentPage(true);
    UpdateDeviceState();
}

void Application::ExecuteSettingsItem() {
    switch (settings_selected_item_) {
        case kSettingsItemWifi:
            ExecuteWifiFocus();
            break;
        case kSettingsItemBluetooth: {
            const bool next_enabled = !board_.IsBluetoothEnabled();
            const bool ok = board_.SetBluetoothEnabled(next_enabled);
            if (display_ != nullptr) {
                display_->ShowNotification(ok ? (next_enabled ? "蓝牙已开启" : "蓝牙已关闭") : "蓝牙不可用");
            }
            RenderCurrentPage(true);
            break;
        }
        case kSettingsItemSound: {
            const int next_volume = (board_.GetVolumePercent() + kVolumeStepPercent) % (100 + kVolumeStepPercent);
            board_.SetVolumePercent(next_volume);
            if (display_ != nullptr) {
                display_->ShowNotification(next_volume == 0 ? "已静音" : "音量已调整");
            }
            RenderCurrentPage(true);
            break;
        }
        case kSettingsItemStorage:
        case kSettingsItemDeviceInfo:
        default:
            break;
    }
}

void Application::ExecuteWifiFocus() {
    if (settings_wifi_focus_index_ <= 0) {
        if (board_.IsWifiEnabled()) {
            board_.StopNetwork();
            settings_wifi_focus_index_ = 0;
            settings_wifi_ap_modal_visible_ = false;
            settings_wifi_connecting_modal_visible_ = false;
            settings_wifi_cached_networks_.clear();
            network_state_dirty_.store(true, std::memory_order_release);
            if (display_ != nullptr) {
                display_->ShowNotification("无线网络已关闭");
            }
            RenderCurrentPage(true);
            return;
        }

        board_.StartNetwork();
        network_state_dirty_.store(true, std::memory_order_release);
        if (display_ != nullptr) {
            display_->ShowNotification("WiFi 已开启");
        }
        RenderCurrentPage(true);
        return;
    }

    const std::vector<BoardWifiNetwork> networks = board_.GetScannedWifiNetworks();
    const int network_index = settings_wifi_focus_index_ - 1;
    if (network_index < 0 || network_index >= static_cast<int>(networks.size())) {
        settings_wifi_focus_index_ = 0;
        RenderCurrentPage(false);
        return;
    }

    const BoardWifiNetwork& selected = networks[network_index];
    if (selected.secure) {
        settings_wifi_cached_networks_ = networks;
        board_.PrepareWifiConfigForSsid(selected.ssid);
        settings_wifi_ap_modal_visible_ = true;
        settings_wifi_connecting_modal_visible_ = false;
        network_state_dirty_.store(true, std::memory_order_release);
        if (display_ != nullptr) {
            display_->ShowNotification("AP 配网已开启");
        }
        RenderCurrentPage(true);
        return;
    }

    if (board_.ConnectToOpenWifi(selected.ssid) && display_ != nullptr) {
        display_->ShowNotification("正在连接开放网络");
    }
    network_state_dirty_.store(true, std::memory_order_release);
    RenderCurrentPage(true);
}

int Application::GetWifiFocusItemCount() const {
    if (!board_.IsWifiEnabled()) {
        return 1;
    }
    if (settings_wifi_ap_modal_visible_) {
        return 1 + static_cast<int>(settings_wifi_cached_networks_.size());
    }
    return 1 + static_cast<int>(board_.GetScannedWifiNetworks().size());
}

WifiSettingsMode Application::GetCurrentWifiSettingsMode() const {
    if (!board_.IsWifiEnabled()) {
        return WifiSettingsMode::Off;
    }
    if (board_.IsWifiConfigMode()) {
        return WifiSettingsMode::Ap;
    }
    return WifiSettingsMode::Station;
}

bool Application::IsSettingsPage() const {
    return current_page_index_ == pages_.Count() - kSettingsPageOffsetFromEnd;
}

void Application::LoadSettings() {
    Settings settings("app", true);
    const int saved_page_index = settings.GetInt("page_index", kStatsPageIndex);
    current_page_index_ = NormalizeSavedPageIndex(saved_page_index);
    device_alias_ = settings.GetString("device_alias", "泉流迹墨水屏");
    if (current_page_index_ != saved_page_index) {
        SaveSettings();
    }
}

void Application::SaveSettings() {
    Settings settings("app", true);
    settings.SetInt("page_index", current_page_index_);
    settings.SetString("device_alias", device_alias_);
}

int Application::NormalizeSavedPageIndex(int saved_page_index) const {
    // Migrate persisted indexes from the legacy page order:
    // 0=总览, 1=最近记录, 2=统计, 3=设置.
    int normalized_index = saved_page_index;
    switch (saved_page_index) {
        case 0:
        case 2:
            normalized_index = 0;
            break;
        case 1:
            normalized_index = 1;
            break;
        case 3:
            normalized_index = 2;
            break;
        default:
            break;
    }

    const int page_count = std::max(1, pages_.Count());
    return std::clamp(normalized_index, 0, page_count - 1);
}

void Application::SeedMockData() {
    dashboard_.today_expense_cents = 4680;
    dashboard_.month_expense_cents = 125430;
    dashboard_.budget_used_percent = 63;
    dashboard_.sync_status = "本地占位数据";
    dashboard_.recent_records = {
        {"工作日午餐", "餐饮", 3200},
        {"地铁充值", "交通", 2000},
        {"咖啡豆补货", "日用", 6480},
    };
    dashboard_.categories = {
        {"餐饮", 34, 42500},
        {"日用", 27, 33800},
        {"交通", 12, 15400},
        {"居家", 11, 13900},
    };
}

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
    context.settings_wifi_focus_index = std::clamp(settings_wifi_focus_index_, 0, std::max(0, GetWifiFocusItemCount() - 1));
    context.settings_wifi_ap_modal_visible = settings_wifi_ap_modal_visible_;
    context.settings_wifi_connecting_modal_visible = settings_wifi_connecting_modal_visible_;
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
    state.wifi_visible = context.wifi_enabled;
    state.wifi_connected = context.wifi_connected;
    state.hotspot_visible = context.wifi_config_mode;
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

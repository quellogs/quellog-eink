#include "application.h"

#include "sdkconfig.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "settings.h"

namespace {

constexpr char kTag[] = "Application";
constexpr int kSettingsItemRefreshPolicy = 0;
constexpr int kSettingsItemWifiReconfigure = 1;
constexpr int kSettingsItemGoHome = 2;
constexpr int kSettingsItemCount = 3;

int64_t SecondsToMicros(int seconds) {
    return static_cast<int64_t>(seconds) * 1000000LL;
}

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

        if (network_state_dirty_.exchange(false, std::memory_order_acq_rel)) {
            UpdateDeviceState();
            RenderCurrentPage(true);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Application::HandleInput(const InputEvent& event) {
    if (IsSettingsPage()) {
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
            if (current_page_index_ == pages_.Count() - 1) {
                refresh_policy_ = refresh_policy_ == RefreshPolicy::Manual
                                      ? RefreshPolicy::Timed
                                      : RefreshPolicy::Manual;
                SaveSettings();
                display_->ShowNotification(refresh_policy_ == RefreshPolicy::Timed
                                               ? "已切换为定时刷新"
                                               : "已切换为手动刷新");
                RenderCurrentPage(true);
            } else {
                TriggerRefresh();
            }
            break;
        case InputKey::None:
            break;
    }
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
    const PageModel model = page->BuildModel(context);
    display_->RenderPage(model, BuildTopStatusBarState(context));
}

void Application::NextPage() {
    current_page_index_ = (current_page_index_ + 1) % std::max(1, pages_.Count());
    SaveSettings();
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::PreviousPage() {
    current_page_index_ = (current_page_index_ - 1 + std::max(1, pages_.Count())) % std::max(1, pages_.Count());
    SaveSettings();
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::NextSettingsItem() {
    settings_selected_item_ = (settings_selected_item_ + 1) % kSettingsItemCount;
    RenderCurrentPage(false);
}

void Application::PreviousSettingsItem() {
    settings_selected_item_ = (settings_selected_item_ + kSettingsItemCount - 1) % kSettingsItemCount;
    RenderCurrentPage(false);
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
        case kSettingsItemRefreshPolicy:
            refresh_policy_ = refresh_policy_ == RefreshPolicy::Manual
                                  ? RefreshPolicy::Timed
                                  : RefreshPolicy::Manual;
            SaveSettings();
            if (display_ != nullptr) {
                display_->ShowNotification(refresh_policy_ == RefreshPolicy::Timed
                                               ? "已切换为定时刷新"
                                               : "已切换为手动刷新");
            }
            RenderCurrentPage(true);
            break;
        case kSettingsItemWifiReconfigure:
            board_.EnterWifiConfigMode();
            network_state_dirty_.store(true, std::memory_order_release);
            if (display_ != nullptr) {
                display_->ShowNotification("已进入配网模式");
            }
            break;
        case kSettingsItemGoHome:
            current_page_index_ = 0;
            SaveSettings();
            UpdateDeviceState();
            RenderCurrentPage(false);
            break;
        default:
            break;
    }
}

bool Application::IsSettingsPage() const {
    return current_page_index_ == pages_.Count() - 1;
}

void Application::LoadSettings() {
    Settings settings("app", true);
    current_page_index_ = settings.GetInt("page_index", 0);
    refresh_policy_ = settings.GetInt("refresh_policy", 0) == 1 ? RefreshPolicy::Timed : RefreshPolicy::Manual;
    device_alias_ = settings.GetString("device_alias", "泉流迹墨水屏");
}

void Application::SaveSettings() {
    Settings settings("app", true);
    settings.SetInt("page_index", current_page_index_);
    settings.SetInt("refresh_policy", refresh_policy_ == RefreshPolicy::Timed ? 1 : 0);
    settings.SetString("device_alias", device_alias_);
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
    context.refresh_policy = refresh_policy_;
    context.device_alias = device_alias_;
    context.board_type = board_.GetBoardType();
    context.device_uuid = board_.GetUuid();
    context.last_refresh_label = BuildRefreshLabel();
    context.wifi_connected = board_.IsWifiConnected();
    context.wifi_connecting = context.device_state == kDeviceStateWifiConnecting;
    context.wifi_config_mode = board_.IsWifiConfigMode();
    context.wifi_ssid = board_.GetWifiSsid();
    context.wifi_ip = board_.GetWifiIpAddress();
    context.wifi_ap_ssid = board_.GetWifiConfigApSsid();
    context.wifi_ap_url = board_.GetWifiConfigApUrl();
    context.settings_selected_item = settings_selected_item_;
    context.settings_item_count = kSettingsItemCount;
    context.dashboard = dashboard_;

    int battery_level = 0;
    bool battery_charging = false;
    bool battery_external_power = false;
    context.battery_known = board_.GetBatteryLevel(battery_level, battery_charging, battery_external_power);
    context.battery_level = battery_level;
    context.battery_charging = battery_charging;

    const UiPage* page = pages_.Get(current_page_index_);
    context.page_title = page == nullptr ? "" : page->GetTitle();
    return context;
}

TopStatusBarState Application::BuildTopStatusBarState(const AppContext& context) const {
    TopStatusBarState state;
    state.title = context.page_title;
    state.wifi_visible = context.wifi_connected;
    state.hotspot_visible = context.wifi_config_mode;
    state.battery_visible = context.battery_known;
    state.battery_level = context.battery_level;
    state.battery_charging = context.battery_charging;
    return state;
}

std::string Application::BuildRefreshLabel() const {
    const int64_t age_sec = (esp_timer_get_time() - last_refresh_us_) / 1000000LL;
    return std::to_string(age_sec) + " 秒前";
}

bool Application::ShouldAutoRefresh(int64_t now_us) const {
    if (refresh_policy_ != RefreshPolicy::Timed) {
        return false;
    }
    return (now_us - last_refresh_us_) >= SecondsToMicros(CONFIG_QUELLOG_AUTO_REFRESH_SECONDS);
}

void Application::HandleNetworkEvent(NetworkEvent event, const std::string& data) {
    (void)data;
    switch (event) {
        case NetworkEvent::Scanning:
        case NetworkEvent::Connecting:
        case NetworkEvent::Connected:
        case NetworkEvent::Disconnected:
        case NetworkEvent::WifiConfigModeEnter:
        case NetworkEvent::WifiConfigModeExit:
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

#include "application.h"

#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "device_api_settings.h"

namespace {

constexpr int kStatsPageIndex = 0;
constexpr int kSettingsPageOffsetFromEnd = 1;
constexpr int kSettingsItemWifi = 0;
constexpr int kSettingsItemBluetooth = 1;
constexpr int kSettingsItemSound = 2;
constexpr int kSettingsItemStorage = 3;
constexpr int kSettingsItemDeviceInfo = 4;
constexpr int kSettingsItemRestart = 5;
constexpr int kSettingsItemCount = 6;
constexpr int kVolumeStepPercent = 10;
constexpr int kStatsPeriodCount = 3;
constexpr int kRecentRecordsPageSize = 6;
constexpr int64_t kRefreshNetworkTimeoutUs = 30 * 1000 * 1000;

int CalculateRecentRecordsPageCount(int total_count) {
    if (total_count <= 0) {
        return 0;
    }
    return (total_count + kRecentRecordsPageSize - 1) / kRecentRecordsPageSize;
}

}  // namespace

void Application::HandleInput(const InputEvent& event) {
    if (IsSettingsPage()) {
        if (settings_selected_item_ == kSettingsItemWifi && settings_wifi_connecting_modal_visible_) {
            return;
        }

        if (event.key == InputKey::Confirm && event.long_press && settings_detail_focused_) {
            if (settings_wifi_ap_modal_visible_) {
                settings_detail_focused_ = false;
                CloseWifiApModal();
                return;
            }
            ReturnSettingsMenuFocus();
            return;
        }

        if (settings_restart_modal_visible_) {
            switch (event.key) {
                case InputKey::Up:
                case InputKey::Down:
                    ToggleRestartModalFocus();
                    return;
                case InputKey::Confirm:
                    ExecuteRestartModalFocus();
                    return;
                case InputKey::OpenSettings:
                    CloseRestartModal();
                    return;
                case InputKey::None:
                    return;
            }
        }

        if (settings_selected_item_ == kSettingsItemWifi && settings_wifi_ap_modal_visible_) {
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

        if (settings_selected_item_ == kSettingsItemWifi && settings_detail_focused_) {
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

        if (!settings_detail_focused_) {
            switch (event.key) {
                case InputKey::Up:
                    PreviousSettingsItem();
                    return;
                case InputKey::Down:
                    NextSettingsItem();
                    return;
                case InputKey::Confirm:
                    EnterSettingsDetail();
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
                return;
            case InputKey::Down:
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

    if (current_page_index_ == kStatsPageIndex && stats_period_modal_visible_) {
        switch (event.key) {
            case InputKey::Up:
                PreviousStatsPeriodFocus();
                return;
            case InputKey::Down:
                NextStatsPeriodFocus();
                return;
            case InputKey::Confirm:
                ApplyStatsPeriodFocus();
                return;
            case InputKey::OpenSettings:
                CloseStatsPeriodModal();
                return;
            case InputKey::None:
                return;
        }
    }

    if (current_page_index_ == kStatsPageIndex && event.key == InputKey::Up && event.long_press) {
        OpenStatsPeriodModal();
        return;
    }

    if (IsRecentRecordsPage() && event.long_press) {
        switch (event.key) {
            case InputKey::Up:
                PreviousRecentRecordsPage();
                return;
            case InputKey::Down:
                NextRecentRecordsPage();
                return;
            case InputKey::Confirm:
            case InputKey::OpenSettings:
            case InputKey::None:
                break;
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
            if (!event.long_press) {
                TriggerRefresh();
            }
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
    settings_detail_focused_ = false;
    settings_wifi_focus_index_ = 0;
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    settings_wifi_cached_networks_.clear();
    current_page_index_ = pages_.Count() - kSettingsPageOffsetFromEnd;
    FinishRefreshNetworkSession(false);
    if (!board_.IsWifiEnabled()) {
        board_.StartNetwork();
        network_state_dirty_.store(true, std::memory_order_release);
    }
    UpdateDeviceState();
    RenderCurrentPage(false);
}

void Application::CloseSettingsPage() {
    const int browseable_page_count = std::max(1, pages_.Count() - 1);
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    settings_detail_focused_ = false;
    settings_wifi_cached_networks_.clear();
    current_page_index_ = std::clamp(settings_return_page_index_, 0, browseable_page_count - 1);
    SaveSettings();
    FinishRefreshNetworkSession(false);
    settings_web_server_.Stop();
    board_.StopNetwork();
    network_state_dirty_.store(true, std::memory_order_release);
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

    const int recent_records_page_count =
        CalculateRecentRecordsPageCount(static_cast<int>(dashboard_.recent_records.size()));
    recent_records_page_index_ =
        recent_records_page_count > 0 ? std::clamp(recent_records_page_index_, 0, recent_records_page_count - 1) : 0;

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

void Application::NextRecentRecordsPage() {
    const int page_count = CalculateRecentRecordsPageCount(static_cast<int>(dashboard_.recent_records.size()));
    if (page_count <= 0) {
        return;
    }

    if (page_count == 1) {
        if (display_ != nullptr) {
            display_->ShowNotification("只有一页记录");
        }
        return;
    }

    recent_records_page_index_ = (recent_records_page_index_ + 1) % page_count;
    RenderCurrentPage(false);
}

void Application::PreviousRecentRecordsPage() {
    const int page_count = CalculateRecentRecordsPageCount(static_cast<int>(dashboard_.recent_records.size()));
    if (page_count <= 0) {
        return;
    }

    if (page_count == 1) {
        if (display_ != nullptr) {
            display_->ShowNotification("只有一页记录");
        }
        return;
    }

    recent_records_page_index_ = (recent_records_page_index_ - 1 + page_count) % page_count;
    RenderCurrentPage(false);
}

void Application::NextSettingsItem() {
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    settings_detail_focused_ = false;
    settings_wifi_cached_networks_.clear();
    settings_selected_item_ = (settings_selected_item_ + 1) % kSettingsItemCount;
    settings_wifi_focus_index_ = 0;
    RenderCurrentPage(false);
}

void Application::PreviousSettingsItem() {
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    settings_detail_focused_ = false;
    settings_wifi_cached_networks_.clear();
    settings_selected_item_ = (settings_selected_item_ + kSettingsItemCount - 1) % kSettingsItemCount;
    settings_wifi_focus_index_ = 0;
    RenderCurrentPage(false);
}

void Application::EnterSettingsDetail() {
    settings_detail_focused_ = true;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    if (settings_selected_item_ == kSettingsItemWifi) {
        settings_wifi_focus_index_ =
            std::clamp(settings_wifi_focus_index_, 0, std::max(0, GetWifiFocusItemCount() - 1));
    } else {
        settings_wifi_focus_index_ = 0;
    }
    RenderCurrentPage(false);
}

void Application::ReturnSettingsMenuFocus() {
    settings_detail_focused_ = false;
    settings_wifi_focus_index_ = 0;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    RenderCurrentPage(false);
}

void Application::NextWifiFocus() {
    const int item_count = GetWifiFocusItemCount();
    settings_wifi_focus_index_ = item_count > 0 ? (settings_wifi_focus_index_ + 1) % item_count : 0;
    RenderCurrentPage(false);
}

void Application::PreviousWifiFocus() {
    const int item_count = GetWifiFocusItemCount();
    settings_wifi_focus_index_ =
        item_count > 0 ? (settings_wifi_focus_index_ + item_count - 1) % item_count : 0;
    RenderCurrentPage(false);
}

void Application::CloseWifiApModal() {
    FinishRefreshNetworkSession(false);
    settings_wifi_ap_modal_visible_ = false;
    settings_wifi_connecting_modal_visible_ = false;
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    settings_web_server_.Stop();
    board_.StopNetwork();
    board_.StartNetwork();
    settings_wifi_cached_networks_.clear();
    network_state_dirty_.store(true, std::memory_order_release);
    RenderCurrentPage(true);
}

void Application::TriggerRefresh() {
    if (!IsDeviceApiConfigured()) {
        const bool should_stop_network = refresh_started_network_;
        FinishRefreshNetworkSession(should_stop_network);
        UpdateSettingsWebServer();
        dashboard_.sync_status = "未配置服务接口";
        dashboard_data_state_ = DashboardDataState::NotConfigured;
        last_refresh_us_ = esp_timer_get_time();
        RenderCurrentPage(true);
        UpdateDeviceState();
        return;
    }

    if (!board_.IsWifiConnected()) {
        if (!refresh_waiting_for_network_) {
            refresh_waiting_for_network_ = true;
            refresh_started_network_ = !board_.IsWifiEnabled();
            refresh_network_deadline_us_ = esp_timer_get_time() + kRefreshNetworkTimeoutUs;
            if (refresh_started_network_) {
                board_.StartNetwork();
            }
        }
        dashboard_.sync_status = "正在连接 Wi-Fi";
        dashboard_data_state_ = DashboardDataState::Loading;
        last_refresh_us_ = esp_timer_get_time();
        network_state_dirty_.store(true, std::memory_order_release);
        RenderCurrentPage(true);
        UpdateDeviceState();
        return;
    }

    const bool should_stop_network = refresh_started_network_;
    refresh_waiting_for_network_ = false;
    refresh_started_network_ = false;
    refresh_network_deadline_us_ = 0;
    state_.store(kDeviceStateRefreshing, std::memory_order_release);
    dashboard_data_state_ = DashboardDataState::Loading;
    ++refresh_count_;
    ApplyDashboardLoadResult(LoadDashboardData(stats_period_));
    last_refresh_us_ = esp_timer_get_time();
    FinishRefreshNetworkSession(should_stop_network);
    UpdateSettingsWebServer();
    RenderCurrentPage(true);
    UpdateDeviceState();
}

void Application::CheckRefreshNetworkTimeout(int64_t now_us) {
    if (!refresh_waiting_for_network_ || refresh_network_deadline_us_ == 0 || now_us < refresh_network_deadline_us_) {
        return;
    }

    const bool should_stop_network = refresh_started_network_;
    FinishRefreshNetworkSession(should_stop_network);
    UpdateSettingsWebServer();
    dashboard_.sync_status = "Wi-Fi 连接超时";
    dashboard_data_state_ = DashboardDataState::Error;
    last_refresh_us_ = now_us;
    network_state_dirty_.store(true, std::memory_order_release);
    RenderCurrentPage(true);
    UpdateDeviceState();
}

void Application::FinishRefreshNetworkSession(bool stop_network) {
    refresh_waiting_for_network_ = false;
    refresh_started_network_ = false;
    refresh_network_deadline_us_ = 0;
    if (stop_network) {
        settings_web_server_.Stop();
        board_.StopNetwork();
    }
}

bool Application::IsDeviceApiConfigured() const {
    const DeviceApiConfig api_config = LoadDeviceApiConfig();
    return !api_config.base_url.empty() && !api_config.api_token.empty();
}

void Application::OpenStatsPeriodModal() {
    stats_period_modal_visible_ = true;
    stats_period_focus_index_ = std::clamp(static_cast<int>(stats_period_), 0, kStatsPeriodCount - 1);
    RenderCurrentPage(false);
}

void Application::CloseStatsPeriodModal() {
    stats_period_modal_visible_ = false;
    RenderCurrentPage(false);
}

void Application::NextStatsPeriodFocus() {
    stats_period_focus_index_ = (stats_period_focus_index_ + 1) % kStatsPeriodCount;
    RenderCurrentPage(false);
}

void Application::PreviousStatsPeriodFocus() {
    stats_period_focus_index_ = (stats_period_focus_index_ + kStatsPeriodCount - 1) % kStatsPeriodCount;
    RenderCurrentPage(false);
}

void Application::ApplyStatsPeriodFocus() {
    stats_period_ = static_cast<DashboardPeriod>(std::clamp(stats_period_focus_index_, 0, kStatsPeriodCount - 1));
    dashboard_.period = stats_period_;
    stats_period_modal_visible_ = false;
    SaveSettings();
    TriggerRefresh();
}

void Application::ApplyDashboardLoadResult(const DashboardLoadResult& result) {
    if (result.success) {
        dashboard_ = result.data;
        dashboard_data_state_ = DashboardDataState::Ready;
        return;
    }

    dashboard_.sync_status = result.status_message.empty() ? "同步失败" : result.status_message;
    dashboard_data_state_ = DashboardDataState::Error;
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
            break;
        case kSettingsItemRestart:
            settings_restart_modal_visible_ = true;
            settings_restart_confirm_focused_ = false;
            RenderCurrentPage(false);
            break;
        default:
            break;
    }
}

void Application::CloseRestartModal() {
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    RenderCurrentPage(false);
}

void Application::ToggleRestartModalFocus() {
    settings_restart_confirm_focused_ = !settings_restart_confirm_focused_;
    RenderCurrentPage(false);
}

void Application::ExecuteRestartModalFocus() {
    if (!settings_restart_confirm_focused_) {
        CloseRestartModal();
        return;
    }
    RequestDeviceRestart();
}

void Application::RequestDeviceRestart() {
    settings_restart_modal_visible_ = false;
    settings_restart_confirm_focused_ = false;
    if (display_ != nullptr) {
        display_->ShowNotification("正在重启...");
    }
    RenderCurrentPage(true);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void Application::ExecuteWifiFocus() {
    if (settings_wifi_focus_index_ <= 0) {
        if (board_.IsWifiEnabled()) {
            FinishRefreshNetworkSession(false);
            settings_web_server_.Stop();
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
        settings_web_server_.Stop();
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

bool Application::IsRecentRecordsPage() const {
    const UiPage* page = pages_.Get(current_page_index_);
    return page != nullptr && std::string(page->GetId()) == "recent_records";
}

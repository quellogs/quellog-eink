#ifndef QUELLOG_APPLICATION_H_
#define QUELLOG_APPLICATION_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "app_context.h"
#include "boards/common/board.h"
#include "dashboard_data_provider.h"
#include "device_state.h"
#include "display/ui_page_registry.h"
#include "settings_web_server.h"

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Initialize();
    void Run();

private:
    Application();

    void HandleInput(const InputEvent& event);
    void OpenSettingsPage();
    void CloseSettingsPage();
    void RenderCurrentPage(bool full_refresh);
    void NextPage();
    void PreviousPage();
    void NextSettingsItem();
    void PreviousSettingsItem();
    void EnterSettingsDetail();
    void ReturnSettingsMenuFocus();
    void NextWifiFocus();
    void PreviousWifiFocus();
    void ExecuteWifiFocus();
    void CloseWifiApModal();
    void OpenStatsPeriodModal();
    void CloseStatsPeriodModal();
    void NextStatsPeriodFocus();
    void PreviousStatsPeriodFocus();
    void ApplyStatsPeriodFocus();
    void CloseRestartModal();
    void ToggleRestartModalFocus();
    void ExecuteRestartModalFocus();
    void RequestDeviceRestart();
    void TriggerRefresh();
    void ExecuteSettingsItem();
    int GetWifiFocusItemCount() const;
    WifiSettingsMode GetCurrentWifiSettingsMode() const;
    bool IsSettingsPage() const;
    void LoadSettings();
    void SaveSettings();
    int NormalizeSavedPageIndex(int saved_page_index) const;
    AppContext BuildContext() const;
    TopStatusBarState BuildTopStatusBarState(const AppContext& context) const;
    bool ShouldAutoRefresh(int64_t now_us) const;
    bool HasBatteryChargingStateChanged(int64_t now_us);
    void HandleNetworkEvent(NetworkEvent event, const std::string& data);
    void UpdateDeviceState();
    void ApplyDashboardLoadResult(const DashboardLoadResult& result);
    void UpdateSettingsWebServer();

    Board& board_;
    Display* display_ = nullptr;
    UiPageRegistry pages_;
    std::atomic<DeviceState> state_{kDeviceStateUnknown};
    std::atomic<bool> network_state_dirty_{false};
    int current_page_index_ = 0;
    std::string device_alias_ = "Quellog E-Ink";
    DashboardData dashboard_;
    SettingsWebServer settings_web_server_;
    std::atomic<bool> refresh_requested_{false};
    DashboardPeriod stats_period_ = DashboardPeriod::Month;
    bool stats_period_modal_visible_ = false;
    int stats_period_focus_index_ = 0;
    int64_t last_refresh_us_ = 0;
    int refresh_count_ = 0;
    int settings_selected_item_ = 0;
    bool settings_detail_focused_ = false;
    int settings_wifi_focus_index_ = 0;
    bool settings_wifi_ap_modal_visible_ = false;
    bool settings_wifi_connecting_modal_visible_ = false;
    bool settings_restart_modal_visible_ = false;
    bool settings_restart_confirm_focused_ = false;
    std::vector<BoardWifiNetwork> settings_wifi_cached_networks_;
    int settings_return_page_index_ = 0;
    int64_t last_battery_status_check_us_ = 0;
    bool battery_status_initialized_ = false;
    bool last_battery_known_ = false;
    bool last_battery_charging_ = false;
};

#endif  // QUELLOG_APPLICATION_H_

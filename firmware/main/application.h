#ifndef QUELLOG_APPLICATION_H_
#define QUELLOG_APPLICATION_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "app_context.h"
#include "boards/common/board.h"
#include "device_state.h"
#include "display/ui_page_registry.h"

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
    void TriggerRefresh();
    void ExecuteSettingsItem();
    bool IsSettingsPage() const;
    void LoadSettings();
    void SaveSettings();
    int NormalizeSavedPageIndex(int saved_page_index) const;
    void SeedMockData();
    AppContext BuildContext() const;
    TopStatusBarState BuildTopStatusBarState(const AppContext& context) const;
    bool ShouldAutoRefresh(int64_t now_us) const;
    void HandleNetworkEvent(NetworkEvent event, const std::string& data);
    void UpdateDeviceState();

    Board& board_;
    Display* display_ = nullptr;
    UiPageRegistry pages_;
    std::atomic<DeviceState> state_{kDeviceStateUnknown};
    std::atomic<bool> network_state_dirty_{false};
    int current_page_index_ = 0;
    std::string device_alias_ = "Quellog E-Ink";
    DashboardData dashboard_;
    int64_t last_refresh_us_ = 0;
    int refresh_count_ = 0;
    int settings_selected_item_ = 0;
    int settings_return_page_index_ = 0;
};

#endif  // QUELLOG_APPLICATION_H_

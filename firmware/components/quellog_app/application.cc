#include "application.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "device_api_settings.h"

namespace {

constexpr char kTag[] = "Application";
constexpr int kStatsPageIndex = 0;

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
    LoadSettings();
    const DeviceApiConfig api_config = LoadDeviceApiConfig();
    const bool api_configured = !api_config.base_url.empty() && !api_config.username.empty() && !api_config.password.empty();
    dashboard_.period = stats_period_;
    dashboard_.sync_status = api_configured ? "等待同步" : "未配置服务接口";
    dashboard_data_state_ = api_configured ? DashboardDataState::Loading : DashboardDataState::NotConfigured;
    state_.store(kDeviceStateStarting, std::memory_order_release);
    UpdateDeviceState();
    last_refresh_us_ = esp_timer_get_time();
    ESP_LOGI(kTag, "first render begin");
    RenderCurrentPage(true);
    ESP_LOGI(kTag, "first render end");
    if (IsDeviceApiConfigured() && (current_page_index_ == kStatsPageIndex || IsRecentRecordsPage())) {
        refresh_requested_.store(true, std::memory_order_release);
    }
}

void Application::Run() {
    app_task_ = xTaskGetCurrentTaskHandle();
    board_.SetInputWakeTask(app_task_);

    while (true) {
        InputEvent event;
        if (board_.PollInput(event)) {
            HandleInput(event);
        }

        const int64_t now_us = esp_timer_get_time();
        if (ShouldAutoRefresh(now_us)) {
            TriggerRefresh();
        }

        CheckRefreshNetworkTimeout(now_us);

        if (HasBatteryChargingStateChanged(now_us)) {
            RenderCurrentPage(false);
        }

        if (network_state_dirty_.exchange(false, std::memory_order_acq_rel)) {
            UpdateDeviceState();
            UpdateSettingsWebServer();
            RenderCurrentPage(true);
        }

        if (refresh_requested_.exchange(false, std::memory_order_acq_rel)) {
            TriggerRefresh();
        }

        const int wait_ms = CalculateIdleWaitMs(esp_timer_get_time());
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

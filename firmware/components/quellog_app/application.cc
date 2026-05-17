#include "application.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dashboard_data_provider.h"

namespace {

constexpr char kTag[] = "Application";

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
    dashboard_ = LoadDashboardData();
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

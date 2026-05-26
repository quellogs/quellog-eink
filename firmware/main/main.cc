#include <esp_err.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

#include "application.h"

namespace {

constexpr char kTag[] = "Main";

void ConfigurePowerManagement() {
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = 80;
    pm_config.light_sleep_enable = true;
    const esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "power management configure failed: %s", esp_err_to_name(err));
    }
#endif
}

void KeepUsbSerialAvailable() {
#if CONFIG_PM_ENABLE && (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED || CONFIG_USJ_ENABLE_USB_SERIAL_JTAG)
    static esp_pm_lock_handle_t usb_serial_pm_lock = nullptr;
    const esp_err_t create_err =
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb-serial", &usb_serial_pm_lock);
    if (create_err != ESP_OK) {
        ESP_LOGW(kTag, "usb serial power lock create failed: %s", esp_err_to_name(create_err));
        return;
    }

    const esp_err_t acquire_err = esp_pm_lock_acquire(usb_serial_pm_lock);
    if (acquire_err != ESP_OK) {
        ESP_LOGW(kTag, "usb serial power lock acquire failed: %s", esp_err_to_name(acquire_err));
    }
#endif
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ConfigurePowerManagement();
    KeepUsbSerialAvailable();

    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();
}

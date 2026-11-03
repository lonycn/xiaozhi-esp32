#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#if CONFIG_XIAOZHI_DEVELOPMENT_IDENTITY || CONFIG_XIAOZHI_CONTROLLED_RELEASE
#include "development_identity.h"
#endif
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
#include "display/usb_text_chat.h"
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
#if CONFIG_XIAOZHI_CONTROLLED_RELEASE
        ESP_LOGE(TAG, "NVS requires recovery; controlled firmware refuses destructive erase");
        ESP_ERROR_CHECK(ret);
#else
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
#endif
    }
    ESP_ERROR_CHECK(ret);

#if CONFIG_XIAOZHI_DEVELOPMENT_IDENTITY
    ESP_ERROR_CHECK(ApplyDevelopmentIdentity());
#elif CONFIG_XIAOZHI_CONTROLLED_RELEASE
    ESP_ERROR_CHECK(RestoreStoredIdentity());
#endif

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
    ESP_ERROR_CHECK(StartUsbTextChat());
#endif
    app.Run();  // This function runs the main event loop and never returns
}

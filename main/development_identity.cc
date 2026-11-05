#include "development_identity.h"

#include <esp_check.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <array>
#include <nvs.h>

#include "development_identity_format.h"
#include "settings.h"

esp_err_t ApplyDevelopmentIdentity() {
#if CONFIG_XIAOZHI_DEVELOPMENT_IDENTITY
    constexpr const char* tag = "DevelopmentIdentity";
    std::array<uint8_t, 6> factory{}, expected{}, custom{};
    if (!development_identity::ParseMac(CONFIG_XIAOZHI_DEVELOPMENT_FACTORY_MAC, expected) ||
        !development_identity::ParseMac(CONFIG_XIAOZHI_DEVELOPMENT_MAC, custom) ||
        !development_identity::IsLocalUnicast(custom) ||
        !development_identity::ValidUuid(CONFIG_XIAOZHI_DEVELOPMENT_UUID)) {
        ESP_LOGE(tag, "Invalid development identity configuration");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_efuse_mac_get_default(factory.data()), tag, "Read factory MAC failed");
    if (factory != expected) {
        ESP_LOGE(tag, "This identity build belongs to a different physical board");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(esp_base_mac_addr_set(custom.data()), tag, "Set software base MAC failed");
    {
        Settings settings("board", true);
        if (settings.GetString("dev_mac") != CONFIG_XIAOZHI_DEVELOPMENT_MAC) {
            if (settings.GetString("factory_uuid").empty()) {
                settings.SetString("factory_uuid", settings.GetString("uuid"));
            }
            settings.SetString("uuid", CONFIG_XIAOZHI_DEVELOPMENT_UUID);
            settings.SetString("dev_mac", CONFIG_XIAOZHI_DEVELOPMENT_MAC);
        }
    }
    ESP_LOGI(tag, "Software MAC: %s; factory eFuse identity preserved",
             CONFIG_XIAOZHI_DEVELOPMENT_MAC);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t RestoreStoredIdentity() {
    constexpr const char* tag = "StoredIdentity";
    nvs_handle_t handle;
    auto error = nvs_open("board", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;  // New board: factory MAC, normal UUID creation.
    if (error != ESP_OK) return error;
    std::array<char, 18> mac{}; size_t mac_size = mac.size();
    error = nvs_get_str(handle, "dev_mac", mac.data(), &mac_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return ESP_OK; }
    if (error != ESP_OK) {
        nvs_close(handle); ESP_LOGE(tag, "Stored software MAC is invalid; refusing identity replacement"); return ESP_ERR_INVALID_STATE;
    }
    std::array<char, 37> uuid{}; size_t uuid_size = uuid.size();
    error = nvs_get_str(handle, "uuid", uuid.data(), &uuid_size);
    nvs_close(handle);
    if (error != ESP_OK || development_identity::DecideStoredIdentity(true, mac.data(), uuid.data()) !=
                               development_identity::StoredIdentityDecision::Restore) {
        ESP_LOGE(tag, "Stored software identity is incomplete or invalid; NVS preserved, network not started");
        return ESP_ERR_INVALID_STATE;
    }
    std::array<uint8_t, 6> custom{};
    development_identity::ParseMac(mac.data(), custom);
    ESP_RETURN_ON_ERROR(esp_base_mac_addr_set(custom.data()), tag, "Restore software base MAC failed");
    // No Settings setter, nvs_set, erase or eFuse operation occurs on this migration path.
    ESP_LOGI(tag, "Restored existing software identity; UUID/NVS/eFuse preserved");
    return ESP_OK;
}

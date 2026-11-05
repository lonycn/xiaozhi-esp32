#include "usb_text_chat.h"
#include "usb_screen_mirror.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/usb_serial_jtag.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include "application.h"

namespace {
constexpr size_t kMaxLine = 4096;
constexpr size_t kMaxTextBytes = 2000;
std::atomic<QueueHandle_t> outgoing{nullptr};
std::atomic<bool> pending{false};
portMUX_TYPE overflow_mux = portMUX_INITIALIZER_UNLOCKED;
char overflow_id[33] = {};
bool overflowed = false;
char input[kMaxLine];

bool ValidId(const char* id) {
    if (id == nullptr || std::strlen(id) != 32)
        return false;
    for (size_t i = 0; i < 32; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(id[i])))
            return false;
    }
    return true;
}

void ReceiveLine(const char* line) {
    if (std::strncmp(line, "@MIRROR ", 8) == 0) {
        cJSON* root = cJSON_Parse(line + 8);
        cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
        const bool valid =
            cJSON_IsObject(root) &&
            ((root->child == nullptr) ||
             (enabled == root->child && enabled->next == nullptr && cJSON_IsBool(enabled)));
        if (valid && enabled)
            SetUsbScreenMirrorEnabled(cJSON_IsTrue(enabled));
        flockfile(stdout);
        if (valid) {
            std::printf(
                "@MIRROR {\"v\":1,\"enabled\":%s,"
                "\"firmware\":\"xiaozhi-%s-usb-mirror\",\"text_chat\":true}\n",
                IsUsbScreenMirrorEnabled() ? "true" : "false", esp_app_get_description()->version);
        } else {
            std::printf(
                "@MIRROR {\"v\":1,\"error\":\"expected empty object or enabled boolean\"}\n");
        }
        funlockfile(stdout);
        cJSON_Delete(root);
        return;
    }
    const bool audio = std::strncmp(line, "@AUDIO ", 7) == 0;
    const bool end = std::strncmp(line, "@CHAT_END ", 10) == 0;
    if (audio || end) {
        cJSON* root = cJSON_Parse(line + (audio ? 7 : 10));
        cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsString(id) && ValidId(id->valuestring)) {
            if (end) {
                Application::GetInstance().CompleteRemoteAudio(id->valuestring);
            } else {
                cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
                uint8_t decoded[1024];
                size_t length = 0;
                if (cJSON_IsString(data) && std::strlen(data->valuestring) <= 1400 &&
                    mbedtls_base64_decode(decoded, sizeof(decoded), &length,
                                          reinterpret_cast<const unsigned char*>(data->valuestring),
                                          std::strlen(data->valuestring)) == 0 &&
                    length > 0) {
                    Application::GetInstance().SubmitRemoteAudio(
                        id->valuestring, std::vector<uint8_t>(decoded, decoded + length));
                } else {
                    PublishUsbTextChatEvent(id->valuestring, "error", "替代语音数据无效");
                }
            }
        }
        cJSON_Delete(root);
        return;
    }
    if (std::strncmp(line, "@CHAT ", 6) != 0)
        return;
    cJSON* root = cJSON_Parse(line + 6);
    cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (!cJSON_IsString(id) || !ValidId(id->valuestring)) {
        cJSON_Delete(root);
        return;
    }
    const std::string request_id(id->valuestring);
    if (!cJSON_IsString(text) || std::strlen(text->valuestring) == 0 ||
        std::strlen(text->valuestring) > kMaxTextBytes) {
        PublishUsbTextChatEvent(request_id, "error", "文字为空或过长");
        cJSON_Delete(root);
        return;
    }
    bool expected = false;
    if (!pending.compare_exchange_strong(expected, true)) {
        PublishUsbTextChatEvent(request_id, "error", "请等待上一条回复完成");
        cJSON_Delete(root);
        return;
    }
    PublishUsbTextChatEvent(request_id, "accepted");
    Application::GetInstance().SubmitRemoteText(request_id, text->valuestring);
    cJSON_Delete(root);
}

void Receiver(void*) {
    size_t used = 0;
    bool discard = false;
    uint8_t bytes[128];
    for (;;) {
        const int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(20));
        for (int i = 0; i < count; ++i) {
            if (bytes[i] == '\n') {
                input[used] = '\0';
                if (!discard)
                    ReceiveLine(input);
                used = 0;
                discard = false;
            } else if (used < sizeof(input) - 1 && !discard) {
                input[used++] = bytes[i];
            } else {
                used = 0;
                discard = true;
            }
        }
        if (count <= 0)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
}
}  // namespace

esp_err_t StartUsbTextChat() {
    // Receive through the interrupt-driven ring buffer rather than stdin.
    // Keep the existing nonblocking console TX path used by the mirror/logs.
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 256, .rx_buffer_size = 4096};
        const esp_err_t result = usb_serial_jtag_driver_install(&config);
        if (result != ESP_OK)
            return result;
    }
    outgoing = xQueueCreate(16, sizeof(char*));
    if (outgoing == nullptr)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(Receiver, "usb_text_rx", 6144, nullptr, 1, nullptr) != pdPASS) {
        vQueueDelete(outgoing);
        outgoing = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI("UsbTextChat", "Remote text chat enabled");
    return ESP_OK;
}

void PublishUsbTextChatEvent(const std::string& id, const char* event, const std::string& text) {
    if (outgoing == nullptr)
        return;
    size_t offset = 0;
    do {
        size_t length = std::min<size_t>(1800, text.size() - offset);
        if (offset + length < text.size()) {
            while (length > 0 && (static_cast<unsigned char>(text[offset + length]) & 0xc0) == 0x80)
                --length;
        }
        if (length == 0 && offset < text.size())
            return;
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "v", 1);
        cJSON_AddStringToObject(root, "id", id.c_str());
        cJSON_AddStringToObject(root, "event", event);
        cJSON_AddStringToObject(root, "text", text.substr(offset, length).c_str());
        char* json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json == nullptr || xQueueSend(outgoing, &json, 0) != pdTRUE) {
            cJSON_free(json);
            portENTER_CRITICAL(&overflow_mux);
            std::snprintf(overflow_id, sizeof(overflow_id), "%s", id.c_str());
            overflowed = true;
            portEXIT_CRITICAL(&overflow_mux);
            return;
        }
        offset += length;
    } while (offset < text.size());
}

void DrainUsbTextChatEvents() {
    if (outgoing == nullptr)
        return;
    char* json = nullptr;
    for (int i = 0; i < 8 && xQueueReceive(outgoing, &json, 0) == pdTRUE; ++i) {
        flockfile(stdout);
        std::printf("@CHAT %s\n", json);
        funlockfile(stdout);
        cJSON_free(json);
    }
    char id[33] = {};
    portENTER_CRITICAL(&overflow_mux);
    if (overflowed) {
        std::memcpy(id, overflow_id, sizeof(id));
        overflowed = false;
    }
    portEXIT_CRITICAL(&overflow_mux);
    if (id[0]) {
        flockfile(stdout);
        std::printf(
            "@CHAT {\"v\":1,\"id\":\"%s\",\"event\":\"error\",\"text\":\"USB reply queue overflow; "
            "reply may be incomplete\"}\n",
            id);
        funlockfile(stdout);
    }
}

void ReleaseUsbTextChatRequest() { pending.store(false); }

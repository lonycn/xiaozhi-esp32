#include "usb_screen_mirror.h"
#include "settings.h"
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
#include "usb_text_chat.h"
#endif

#include <array>
#include <cstdio>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_lcd_panel_interface.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

namespace {
constexpr size_t kMaxPixels = 1024;
constexpr char kTag[] = "UsbScreenMirror";

struct Frame {
    std::array<uint8_t, kMaxPixels> pixels{};
    int width = 0;
    int height = 0;
    uint32_t generation = 0;
    bool display_ok = false;
};

struct Mirror {
    esp_lcd_panel_handle_t panel = nullptr;
    decltype(esp_lcd_panel_t::draw_bitmap) draw = nullptr;
    decltype(esp_lcd_panel_t::disp_on_off) power = nullptr;
    decltype(esp_lcd_panel_t::invert_color) invert = nullptr;
    QueueHandle_t frames = nullptr;
    Frame last;
    uint32_t generation = 0;
    bool powered = true;
    bool inverted = false;
    bool captured = false;
    bool active = false;
    bool enabled = true;
};

// Fixed lifetime: display teardown detaches the panel, while the single worker
// and its bounded queue remain available for a subsequent display instance.
Mirror mirror;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t Crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320 & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xffffffff;
}

void QueueVisibleFrame() {
    Frame frame;
    bool ready;
    portENTER_CRITICAL(&mux);
    ready = mirror.active && mirror.enabled && mirror.captured;
    if (ready) {
        frame = mirror.last;
        const size_t size = frame.width * frame.height / 8;
        for (size_t i = 0; i < size; ++i) {
            frame.pixels[i] = !mirror.powered   ? 0
                              : mirror.inverted ? frame.pixels[i] ^ 0xff
                                                : frame.pixels[i];
        }
    }
    portEXIT_CRITICAL(&mux);
    if (ready) {
        xQueueOverwrite(mirror.frames, &frame);
    }
}

esp_err_t Draw(esp_lcd_panel_t* panel, int x_start, int y_start, int x_end, int y_end,
               const void* pixels) {
    const esp_err_t result = mirror.draw(panel, x_start, y_start, x_end, y_end, pixels);
    // esp_lvgl_port's monochrome path submits the complete page-oriented buffer.
    if (x_start == 0 && y_start == 0 && x_end == mirror.last.width && y_end == mirror.last.height &&
        pixels != nullptr) {
        portENTER_CRITICAL(&mux);
        if (!mirror.enabled) {
            portEXIT_CRITICAL(&mux);
            return result;
        }
        mirror.last.display_ok = result == ESP_OK;
        if (result == ESP_OK) {
            std::memcpy(mirror.last.pixels.data(), pixels, x_end * y_end / 8);
        }
        mirror.captured = result == ESP_OK;
        portEXIT_CRITICAL(&mux);
        QueueVisibleFrame();
    }
    return result;
}

esp_err_t Power(esp_lcd_panel_t* panel, bool enabled) {
    const esp_err_t result = mirror.power(panel, enabled);
    if (result == ESP_OK) {
        portENTER_CRITICAL(&mux);
        mirror.powered = enabled;
        portEXIT_CRITICAL(&mux);
        QueueVisibleFrame();
    }
    return result;
}

esp_err_t Invert(esp_lcd_panel_t* panel, bool enabled) {
    const esp_err_t result = mirror.invert(panel, enabled);
    if (result == ESP_OK) {
        portENTER_CRITICAL(&mux);
        mirror.inverted = enabled;
        portEXIT_CRITICAL(&mux);
        QueueVisibleFrame();
    }
    return result;
}

void Worker(void*) {
    Frame cached;
    bool have_frame = false;
    uint32_t sequence = 0;
    char encoded[1370];
    for (;;) {
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
        DrainUsbTextChatEvents();
#endif
        if (xQueueReceive(mirror.frames, &cached, 0) == pdTRUE) {
            have_frame = true;
        }
        portENTER_CRITICAL(&mux);
        const bool active =
            mirror.active && mirror.enabled && cached.generation == mirror.generation;
        portEXIT_CRITICAL(&mux);
        if (active && have_frame) {
            const size_t size = cached.width * cached.height / 8;
            size_t encoded_size = 0;
            if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded), sizeof(encoded),
                                      &encoded_size, cached.pixels.data(), size) == 0) {
                encoded[encoded_size] = '\0';
                // stdout's FILE lock also serializes ordinary ESP-IDF vprintf
                // logs, so JSON frames cannot be interleaved with log messages.
                flockfile(stdout);
                portENTER_CRITICAL(&mux);
                const bool current =
                    mirror.enabled && mirror.active && cached.generation == mirror.generation;
                portEXIT_CRITICAL(&mux);
                if (current)
                    std::printf(
                        "@SCREEN "
                        "{\"v\":1,\"type\":\"frame\",\"firmware\":\"xiaozhi-%s-usb-mirror\","
                        "\"format\":\"mono-vlsb\",\"width\":%d,\"height\":%d,\"seq\":%lu,"
                        "\"uptime_ms\":%llu,\"display_ok\":%s,\"display_control\":false,\"text_"
                        "chat\":%"
                        "s,"
                        "\"crc32\":\"%08lx\",\"data\":\"%s\"}\n",
                        esp_app_get_description()->version, cached.width, cached.height,
                        static_cast<unsigned long>(sequence++),
                        static_cast<unsigned long long>(esp_timer_get_time() / 1000),
                        cached.display_ok ? "true" : "false",
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
                        "true",
#else
                        "false",
#endif
                        static_cast<unsigned long>(Crc32(cached.pixels.data(), size)), encoded);
                funlockfile(stdout);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
}  // namespace

esp_err_t AttachUsbScreenMirror(esp_lcd_panel_handle_t panel, int width, int height) {
    if (panel == nullptr || width != 128 || (height != 32 && height != 64) || mirror.active) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mirror.frames == nullptr) {
        mirror.frames = xQueueCreate(1, sizeof(Frame));
        if (mirror.frames == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreate(Worker, "screen_mirror", 6144, nullptr, 1, nullptr) != pdPASS) {
            vQueueDelete(mirror.frames);
            mirror.frames = nullptr;
            return ESP_ERR_NO_MEM;
        }
    }
    Settings settings("usb_mirror");
    const bool enabled = settings.GetBool("enabled", true);
    portENTER_CRITICAL(&mux);
    mirror.enabled = enabled;
    mirror.panel = panel;
    mirror.draw = panel->draw_bitmap;
    mirror.power = panel->disp_on_off;
    mirror.invert = panel->invert_color;
    mirror.last = Frame{};
    mirror.last.width = width;
    mirror.last.height = height;
    mirror.last.generation = ++mirror.generation;
    mirror.powered = true;
    mirror.inverted = false;
    mirror.captured = false;
    mirror.active = true;
    portEXIT_CRITICAL(&mux);
    panel->draw_bitmap = Draw;
    if (mirror.power != nullptr) {
        panel->disp_on_off = Power;
    }
    if (mirror.invert != nullptr) {
        panel->invert_color = Invert;
    }
    ESP_LOGI(kTag, "USB screen mirror %s: %dx%d, max 4 fps", enabled ? "enabled" : "disabled",
             width, height);
#if CONFIG_XIAOZHI_USB_TEXT_CHAT
    // A host may query before panel attachment; publish the persisted state once
    // attachment has finished, even when no pixel frames will be emitted.
    flockfile(stdout);
    std::printf(
        "@MIRROR {\"v\":1,\"enabled\":%s,"
        "\"firmware\":\"xiaozhi-%s-usb-mirror\",\"text_chat\":true}\n",
        enabled ? "true" : "false", esp_app_get_description()->version);
    funlockfile(stdout);
#endif
    return ESP_OK;
}

void DetachUsbScreenMirror(esp_lcd_panel_handle_t panel) {
    if (panel == nullptr || panel != mirror.panel) {
        return;
    }
    panel->draw_bitmap = mirror.draw;
    panel->disp_on_off = mirror.power;
    panel->invert_color = mirror.invert;
    portENTER_CRITICAL(&mux);
    mirror.active = false;
    mirror.panel = nullptr;
    portEXIT_CRITICAL(&mux);
}

void SetUsbScreenMirrorEnabled(bool enabled) {
    // Serialize with a pending SCREEN write so none follows the off acknowledgement.
    flockfile(stdout);
    portENTER_CRITICAL(&mux);
    mirror.enabled = enabled;
    mirror.captured = false;
    mirror.last.generation = ++mirror.generation;
    portEXIT_CRITICAL(&mux);
    funlockfile(stdout);
    Settings settings("usb_mirror", true);
    settings.SetBool("enabled", enabled);
}

bool IsUsbScreenMirrorEnabled() {
    portENTER_CRITICAL(&mux);
    const bool enabled = mirror.enabled;
    portEXIT_CRITICAL(&mux);
    return enabled;
}

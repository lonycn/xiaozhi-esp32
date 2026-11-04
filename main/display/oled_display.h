#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"
#include "oled_idle_scene.h"

#include <array>
#include <ctime>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* idle_scene_ = nullptr;
    lv_obj_t* idle_face_ = nullptr;
    lv_obj_t* idle_clock_ = nullptr;
    lv_obj_t* idle_network_ = nullptr;
    lv_obj_t* idle_mute_ = nullptr;
    lv_obj_t* idle_battery_ = nullptr;
    lv_timer_t* idle_timer_ = nullptr;
    alignas(4) std::array<uint8_t, 8 + oled_idle::kWidth * oled_idle::kHeight / 8> idle_pixels_{};
    lv_image_dsc_t idle_clock_image_{};
    bool idle_visible_ = false;
    time_t idle_rendered_second_ = -1;
    const char* idle_face_text_ = nullptr;

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();
    void SetupIdleScene();
    void UpdateIdleScene();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
};

#endif  // OLED_DISPLAY_H

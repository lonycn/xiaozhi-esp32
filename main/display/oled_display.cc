#include "oled_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"
#if CONFIG_XIAOZHI_USB_SCREEN_MIRROR
#include "usb_screen_mirror.h"
#endif

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>
#include <src/misc/cache/instance/lv_image_cache.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;
#if CONFIG_XIAOZHI_USB_SCREEN_MIRROR
    // Capture the final monochrome buffer sent to the panel, not a second UI.
    esp_err_t mirror_result = AttachUsbScreenMirror(panel_, width_, height_);
    if (mirror_result != ESP_OK) {
        ESP_LOGW(TAG, "USB screen mirror unavailable: %s", esp_err_to_name(mirror_result));
    }
#endif

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

void OledDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
    if (idle_timer_ != nullptr)
        lv_timer_delete(idle_timer_);
    if (idle_scene_ != nullptr) {
        lv_image_cache_drop(&idle_clock_image_);
        lv_image_cache_drop(&face_image_);
        lv_obj_delete(idle_scene_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
#if CONFIG_XIAOZHI_USB_SCREEN_MIRROR
        DetachUsbScreenMirror(panel_);
#endif
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    // Replace all newlines with spaces
    std::string content_str = content ? content : "";
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');

    if (idle_scene_ != nullptr && role != nullptr && !content_str.empty()) {
        UpdateIdleScene();
        const auto state = Application::GetInstance().GetDeviceState();
        if (std::strcmp(role, "user") == 0 && state == kDeviceStateListening) {
            face_animator_.SetMode(oled_face::Mode::Thinking, lv_tick_get());
        } else if (std::strcmp(role, "assistant") == 0 && state == kDeviceStateSpeaking) {
            face_animator_.SetReply(content_str, lv_tick_get());
            ESP_LOGI(TAG, "Micro expression: %s (reply text)",
                     oled_face::Name(face_animator_.mood()));
        }
        UpdateFace();
    }

    lv_anim_delete(chat_message_label_, nullptr);
    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, content_str.c_str());
    } else {
        if (content_str.empty()) {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, content_str.c_str());
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OledDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr || content == nullptr) {
        return;
    }
    // OLED has one text slot and does not distinguish message roles.
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');
    if (content_str != lv_label_get_text(chat_message_label_)) {
        return;
    }
    lv_anim_delete(chat_message_label_, nullptr);
    lv_label_set_text(chat_message_label_, "");
    if (content_right_ != nullptr) {
        lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    content_left_ = lv_obj_create(content_);
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    lv_obj_set_style_border_width(content_left_, 0, 0);

    emotion_label_ = lv_label_create(content_left_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    content_right_ = lv_obj_create(content_);
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_right_, 0, 0);
    lv_obj_set_style_border_width(content_right_, 0, 0);
    lv_obj_set_flex_grow(content_right_, 1);
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_right_);
    lv_label_set_text(chat_message_label_, "");
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(chat_message_label_, width_ - 32);
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
    // These panels are fixed-size. Overflow must not create a scrollbar at the OLED edge.
    for (auto object : {screen, container_, content_, side_bar_, status_bar_}) {
        lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (width_ == oled_idle::kWidth && height_ == oled_idle::kHeight)
        SetupIdleScene();
}

void OledDisplay::SetupIdleScene() {
    auto theme = static_cast<LvglTheme*>(current_theme_);
    idle_scene_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(idle_scene_);
    lv_obj_set_size(idle_scene_, width_, height_);
    lv_obj_set_pos(idle_scene_, 0, 0);
    lv_obj_set_scrollbar_mode(idle_scene_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(idle_scene_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_scene_, LV_OBJ_FLAG_HIDDEN);

    // The monochrome port maps dark LVGL pixels to lit physical OLED pixels.
    const uint32_t palette[] = {0xffffffff, 0xff000000};
    std::memcpy(idle_pixels_.data(), palette, sizeof(palette));
    idle_clock_image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    idle_clock_image_.header.cf = LV_COLOR_FORMAT_I1;
    idle_clock_image_.header.w = width_;
    idle_clock_image_.header.h = height_;
    idle_clock_image_.header.stride = width_ / 8;
    idle_clock_image_.data_size = idle_pixels_.size();
    idle_clock_image_.data = idle_pixels_.data();
    idle_clock_ = lv_image_create(idle_scene_);
    lv_image_set_src(idle_clock_, &idle_clock_image_);

    std::memcpy(face_pixels_.data(), palette, sizeof(palette));
    face_image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    face_image_.header.cf = LV_COLOR_FORMAT_I1;
    face_image_.header.w = oled_face::kSize;
    face_image_.header.h = oled_face::kSize;
    face_image_.header.stride = oled_face::kSize / 8;
    face_image_.data_size = face_pixels_.size();
    face_image_.data = face_pixels_.data();
    idle_face_ = lv_image_create(idle_scene_);
    lv_image_set_src(idle_face_, &face_image_);
    conversation_face_ = lv_image_create(content_);
    lv_image_set_src(conversation_face_, &face_image_);
    lv_obj_set_pos(conversation_face_, 0, 0);
    lv_obj_add_flag(conversation_face_, LV_OBJ_FLAG_HIDDEN);

    auto icon = [&](int x, int y) {
        auto label = lv_label_create(idle_scene_);
        lv_obj_set_style_text_font(label, theme->icon_font()->font(), 0);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_pos(label, x, y);
        lv_label_set_text(label, "");
        return label;
    };
    idle_mute_ = icon(96, 0);
    idle_battery_ = icon(96, 18);
    idle_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<OledDisplay*>(lv_timer_get_user_data(timer))->UpdateIdleScene();
        },
        160, this);
    lv_timer_pause(idle_timer_);
}

void OledDisplay::UpdateIdleScene() {
    if (idle_scene_ == nullptr)
        return;
    const auto state = Application::GetInstance().GetDeviceState();
    if (face_device_state_ != state) {
        face_device_state_ = state;
        auto mode = oled_face::Mode::Disabled;
        if (state == kDeviceStateIdle)
            mode = oled_face::Mode::Idle;
        else if (state == kDeviceStateConnecting)
            mode = oled_face::Mode::Thinking;
        else if (state == kDeviceStateListening)
            mode = oled_face::Mode::Listening;
        else if (state == kDeviceStateSpeaking)
            mode = oled_face::Mode::Speaking;
        face_animator_.SetMode(mode, lv_tick_get());
    }
    const auto status = lv_label_get_text(status_label_);
    const bool clock_status = std::strlen(status) == 5 && status[2] == ':';
    const bool visible = state == kDeviceStateIdle &&
                         (clock_status || std::strcmp(status, Lang::Strings::STANDBY) == 0) &&
                         lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    if (visible != idle_visible_) {
        idle_visible_ = visible;
        if (visible) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(idle_scene_, LV_OBJ_FLAG_HIDDEN);
            idle_rendered_second_ = -1;
        } else {
            lv_obj_add_flag(idle_scene_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool conversation = state == kDeviceStateListening || state == kDeviceStateSpeaking ||
                              state == kDeviceStateConnecting;
    if (conversation) {
        lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(conversation_face_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(conversation_face_, LV_OBJ_FLAG_HIDDEN);
    }
    if (visible || conversation) {
        lv_timer_resume(idle_timer_);
        UpdateFace();
    } else {
        lv_timer_pause(idle_timer_);
    }
    if (!visible)
        return;

    const auto now = std::time(nullptr);
    if (now == idle_rendered_second_)
        return;
    idle_rendered_second_ = now;
    struct tm local{};
    localtime_r(&now, &local);
    const bool valid = local.tm_year >= 2025 - 1900;
    auto pixels = oled_idle::Clock(valid ? local.tm_hour : -1, valid ? local.tm_min : -1,
                                   !valid || local.tm_sec % 2 == 0);
    const bool offline =
        network_icon_ == nullptr || std::strcmp(network_icon_, MATERIAL_SYMBOLS_WIFI_OFF) == 0;
    oled_idle::Wifi(pixels, !offline);
    std::copy(pixels.begin(), pixels.end(), idle_pixels_.begin() + 8);
    lv_image_cache_drop(&idle_clock_image_);
    lv_obj_invalidate(idle_clock_);
    lv_label_set_text(idle_mute_, muted_ ? MATERIAL_SYMBOLS_VOLUME_OFF : "");
    lv_label_set_text(idle_battery_, lv_label_get_text(battery_label_));
}

void OledDisplay::UpdateFace() {
    const auto pixels = oled_face::Render(face_animator_.At(lv_tick_get()));
    if (std::equal(pixels.begin(), pixels.end(), face_pixels_.begin() + 8))
        return;
    std::copy(pixels.begin(), pixels.end(), face_pixels_.begin() + 8);
    lv_image_cache_drop(&face_image_);
    lv_obj_invalidate(idle_face_);
    lv_obj_invalidate(conversation_face_);
}

void OledDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    UpdateIdleScene();
}

void OledDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);
    DisplayLockGuard lock(this);
    UpdateIdleScene();
}

bool OledDisplay::SetEmotion(const char* emotion) {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const char* utf8 = noto_emoji_get_utf8(emotion);
    const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
    if (utf8 == nullptr) {
        utf8 = material_symbols_get_utf8(emotion);
        emotion_font = lvgl_theme->large_icon_font()->font();
    }
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return false;
    }
    const auto state = Application::GetInstance().GetDeviceState();
    if (idle_scene_ != nullptr && emotion != nullptr &&
        (state == kDeviceStateSpeaking || state == kDeviceStateListening)) {
        face_animator_.SetEmotion(emotion, lv_tick_get());
        UpdateFace();
    }
    if (utf8 != nullptr) {
        lv_obj_set_style_text_font(emotion_label_, emotion_font, 0);
        lv_label_set_text(emotion_label_, utf8);
        return true;
    }
    // Neither font has a glyph for this name, so what is shown is the neutral placeholder and
    // not the emotion that was asked for.
    lv_obj_set_style_text_font(emotion_label_, lvgl_theme->emoji_font()->font(), 0);
    lv_label_set_text(emotion_label_, NOTO_EMOJI_NEUTRAL);
    return false;
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}

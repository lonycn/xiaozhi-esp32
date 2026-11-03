#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_ops.h>

// Attach only to a monochrome panel. The worker mirrors completed I2C writes;
// the LVGL callback never performs serial I/O or waits for the worker.
esp_err_t AttachUsbScreenMirror(esp_lcd_panel_handle_t panel, int width, int height);
void DetachUsbScreenMirror(esp_lcd_panel_handle_t panel);

// Persisted runtime control; call setter from the command/main task only.
void SetUsbScreenMirrorEnabled(bool enabled);
bool IsUsbScreenMirrorEnabled();

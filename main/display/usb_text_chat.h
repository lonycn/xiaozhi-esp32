#pragma once

#include <esp_err.h>
#include <string>

esp_err_t StartUsbTextChat();
void DrainUsbTextChatEvents();
void PublishUsbTextChatEvent(const std::string& id, const char* event,
                             const std::string& text = "");
void ReleaseUsbTextChatRequest();

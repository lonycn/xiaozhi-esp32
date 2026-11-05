#pragma once

#include <esp_err.h>

// Apply the configured software identity before creating any network interface.
esp_err_t ApplyDevelopmentIdentity();

// Controlled profile: preserve an existing valid software identity, never provision one.
esp_err_t RestoreStoredIdentity();

#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Temporary Wi-Fi hotspot and captive portal for phone provisioning.
esp_err_t fortune_net_start(void);
bool fortune_net_connected(void);
bool fortune_net_connecting(void);
bool fortune_net_has_credentials(void);
bool fortune_net_portal_active(void);
esp_err_t fortune_net_error(void);
const char *fortune_net_ip(void);
const char *fortune_net_device_name(void);
const char *fortune_net_ap_password(void);
esp_err_t fortune_net_clear_credentials(void);
esp_err_t fortune_net_retry(void);

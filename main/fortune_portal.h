#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef esp_err_t (*fortune_portal_submit_cb_t)(const char *ssid, const char *password);
typedef const char *(*fortune_portal_status_cb_t)(void);

// Scan while only STA is active, before the phone joins the setup hotspot.
void fortune_portal_scan_now(void);

// Runs only while the temporary setup AP is active. The IP is the AP netif's
// IPv4 address in network byte order. The phone can always use 192.168.4.1.
esp_err_t fortune_portal_start(uint32_t ap_ip, fortune_portal_submit_cb_t submit,
                               fortune_portal_status_cb_t status);
void fortune_portal_stop(void);

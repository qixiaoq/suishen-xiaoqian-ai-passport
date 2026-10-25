// Temporary SoftAP captive portal provisioning for AI Passport.
#include "fortune_net.h"
#include "fortune_portal.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "fortune_net";
static esp_netif_t *s_ap_netif;
static wifi_config_t s_saved;
static wifi_config_t s_candidate;
static volatile bool s_connected;
static volatile bool s_connecting;
static volatile bool s_portal_active;
static volatile bool s_candidate_active;
static bool s_sntp_started;
static bool s_started;
static uint8_t s_retries;
static esp_err_t s_error;
static char s_ip[16];
static char s_ap_name[33];
static char s_ap_password[16];
static char s_portal_url[32]; // DHCP Option 114 retains this pointer.
static volatile const char *s_status = "等待配网";

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s: heap=%u largest=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static esp_err_t portal_submit(const char *ssid, const char *password)
{
    if (!s_portal_active || s_candidate_active || !ssid[0]) return ESP_ERR_INVALID_STATE;
    memset(&s_candidate, 0, sizeof(s_candidate));
    memcpy(s_candidate.sta.ssid, ssid, strlen(ssid));
    memcpy(s_candidate.sta.password, password, strlen(password));
    s_candidate.sta.threshold.authmode = WIFI_AUTH_OPEN;
    s_candidate_active = true;
    s_connecting = true;
    s_retries = 0;
    s_status = "设备正在连接所选 Wi-Fi…";
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &s_candidate);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        s_candidate_active = false;
        s_connecting = false;
        s_status = "连接未启动，请重新提交";
        s_error = err;
    }
    return err;
}

static const char *portal_status(void) { return (const char *)s_status; }

static esp_err_t start_portal(void)
{
    if (s_portal_active) return ESP_OK;
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(s_ap_name, sizeof(s_ap_name), "Xiaoqian-%02X%02X", mac[4], mac[5]);
    snprintf(s_ap_password, sizeof(s_ap_password), "Qq%08X", (unsigned)esp_random());
    // Scanning a single-radio AP+STA device can briefly disrupt a connected
    // phone. Populate the list before exposing the setup hotspot.
    fortune_portal_scan_now();
    wifi_config_t ap = { 0 };
    size_t name_len = strlen(s_ap_name);
    if (name_len >= sizeof(ap.ap.ssid)) return ESP_ERR_INVALID_ARG;
    memcpy(ap.ap.ssid, s_ap_name, name_len);
    memcpy(ap.ap.password, s_ap_password, strlen(s_ap_password));
    ap.ap.ssid_len = name_len;
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) return err;
    esp_netif_ip_info_t info;
    err = esp_netif_get_ip_info(s_ap_netif, &info);
    if (err != ESP_OK) return err;

    snprintf(s_portal_url, sizeof(s_portal_url), "http://" IPSTR, IP2STR(&info.ip));
    esp_netif_dhcps_stop(s_ap_netif);
    esp_err_t option = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                            ESP_NETIF_CAPTIVEPORTAL_URI,
                                            s_portal_url, strlen(s_portal_url));
    if (option != ESP_OK) ESP_LOGW(TAG, "DHCP portal hint unavailable: %s", esp_err_to_name(option));
    esp_netif_dhcps_start(s_ap_netif);
    log_heap("before portal HTTP");
    err = fortune_portal_start(info.ip.addr, portal_submit, portal_status);
    if (err != ESP_OK) {
        s_error = err;
        ESP_LOGE(TAG, "portal HTTP start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_portal_active = true;
    s_status = "等待填写 Wi-Fi";
    ESP_LOGI(TAG, "setup AP %s at %s", s_ap_name, s_portal_url);
    log_heap("portal ready");
    return ESP_OK;
}

static void start_portal_task(void *arg)
{
    (void)arg;
    esp_err_t err = start_portal();
    if (err != ESP_OK) s_error = err;
    vTaskDelete(NULL);
}

static void stop_portal_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));
    if (!s_connected) { vTaskDelete(NULL); return; }
    fortune_portal_stop();
    s_portal_active = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) ESP_LOGW(TAG, "stop setup AP failed: %s", esp_err_to_name(err));
    log_heap("portal stopped");
    vTaskDelete(NULL);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        if (s_saved.sta.ssid[0] && !s_portal_active && !s_candidate_active) {
            s_connecting = true;
            s_status = "正在连接已保存 Wi-Fi…";
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", event->reason);
        bool was_connected = s_connected;
        s_connected = false;
        s_ip[0] = '\0';
        if (was_connected && s_saved.sta.ssid[0] && !s_candidate_active) {
            s_connecting = true;
            s_retries = 0;
            s_status = "正在重连已保存 Wi-Fi…";
        }
        if (s_connecting && s_retries++ < 2) {
            esp_wifi_connect();
            return;
        }
        s_connecting = false;
        s_retries = 0;
        if (s_candidate_active) {
            s_candidate_active = false;
            s_status = "密码错误或网络不可用，请重新填写";
            esp_wifi_set_config(WIFI_IF_STA, &s_saved); // RAM-only until success
        } else if (!s_portal_active) {
            s_status = "旧网络不可用，请连接设备热点";
            xTaskCreate(start_portal_task, "setup_ap", 4096, NULL, 4, NULL);
        }
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "phone associated with setup AP");
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_AP_STAIPASSIGNED) {
        ESP_LOGI(TAG, "setup client received DHCP address");
        return;
    }
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_connected = true;
    s_connecting = false;
    s_retries = 0;
    s_status = "Wi-Fi 已连接，正在同步日期";
    ESP_LOGI(TAG, "Wi-Fi got IP: %s", s_ip);
    if (s_candidate_active) {
        s_candidate_active = false;
        esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &s_candidate);
        if (err == ESP_OK) memcpy(&s_saved, &s_candidate, sizeof(s_saved));
        else { s_error = err; ESP_LOGE(TAG, "Wi-Fi save failed: %s", esp_err_to_name(err)); }
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (!s_sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&config) == ESP_OK) s_sntp_started = true;
    }
    if (s_portal_active) xTaskCreate(stop_portal_task, "setup_done", 4096, NULL, 4, NULL);
}

esp_err_t fortune_net_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event, NULL);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_get_config(WIFI_IF_STA, &s_saved);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) { s_error = err; return err; }
    s_started = true;
    if (!s_saved.sta.ssid[0]) return start_portal();
    return ESP_OK;
}

bool fortune_net_connected(void) { return s_connected; }
bool fortune_net_connecting(void) { return s_connecting; }
bool fortune_net_has_credentials(void) { return s_saved.sta.ssid[0] != '\0'; }
bool fortune_net_portal_active(void) { return s_portal_active; }
esp_err_t fortune_net_error(void) { return s_error; }
const char *fortune_net_ip(void) { return s_ip; }
const char *fortune_net_device_name(void) { return s_ap_name; }
const char *fortune_net_ap_password(void) { return s_ap_password; }

esp_err_t fortune_net_retry(void)
{
    if (!s_saved.sta.ssid[0] || s_candidate_active) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_saved);
    if (err != ESP_OK) return err;
    s_connecting = true;
    s_retries = 0;
    s_status = "正在重连已保存 Wi-Fi…";
    return esp_wifi_connect();
}

esp_err_t fortune_net_clear_credentials(void)
{
    wifi_config_t empty = { 0 };
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    memset(&s_saved, 0, sizeof(s_saved));
    s_candidate_active = false;
    s_connecting = false;
    s_connected = false;
    s_ip[0] = '\0';
    err = start_portal();
    esp_wifi_disconnect();
    return err;
}

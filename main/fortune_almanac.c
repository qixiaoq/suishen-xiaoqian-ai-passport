#include "fortune_almanac.h"
#include "fortune_net.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "fortune_almanac";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static fortune_almanac_t s_latest;
static bool s_ready;

typedef struct { char *data; size_t len; size_t cap; } reply_t;

static esp_err_t on_http(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    reply_t *reply = event->user_data;
    if (!reply || event->data_len < 0 || reply->len + (size_t)event->data_len >= reply->cap)
        return ESP_FAIL;
    memcpy(reply->data + reply->len, event->data, (size_t)event->data_len);
    reply->len += (size_t)event->data_len;
    reply->data[reply->len] = '\0';
    return ESP_OK;
}

static bool copy_field(cJSON *root, const char *key, char *out, size_t cap)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    size_t len = strlen(item->valuestring);
    if (!len || len >= cap) return false;
    memcpy(out, item->valuestring, len + 1);
    return true;
}

static bool fetch_day(const char *date, fortune_almanac_t *out)
{
    if (!CONFIG_XIAOQIAN_COMPANION_URL[0]) return false;
    char url[256];
    int n = snprintf(url, sizeof(url), "%s/api/today?date=%s",
                     CONFIG_XIAOQIAN_COMPANION_URL, date);
    if (n < 0 || n >= (int)sizeof(url)) return false;
    reply_t reply = { .data = calloc(1, 1024), .cap = 1024 };
    if (!reply.data) return false;
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 5000, .event_handler = on_http,
        .user_data = &reply,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(reply.data); return false; }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    bool okay = false;
    if (err == ESP_OK && status == 200) {
        cJSON *root = cJSON_ParseWithLength(reply.data, reply.len);
        if (root) {
            memset(out, 0, sizeof(*out));
            okay = copy_field(root, "date", out->date, sizeof(out->date)) &&
                strcmp(out->date, date) == 0 &&
                copy_field(root, "lunarDate", out->lunar, sizeof(out->lunar)) &&
                copy_field(root, "dayType", out->day_type, sizeof(out->day_type)) &&
                copy_field(root, "yi", out->yi, sizeof(out->yi)) &&
                copy_field(root, "ji", out->ji, sizeof(out->ji));
            cJSON_Delete(root);
        }
    }
    free(reply.data);
    if (!okay) ESP_LOGW(TAG, "today summary unavailable: %s, HTTP %d", esp_err_to_name(err), status);
    return okay;
}

static void worker(void *arg)
{
    (void)arg;
    while (true) {
        time_t stamp = time(NULL);
        struct tm local;
        if (fortune_net_connected() && stamp >= 1704067200 && localtime_r(&stamp, &local)) {
            char date[11];
            if (strftime(date, sizeof(date), "%Y-%m-%d", &local)) {
                bool needed;
                portENTER_CRITICAL(&s_lock);
                needed = !s_ready || strcmp(s_latest.date, date) != 0;
                portEXIT_CRITICAL(&s_lock);
                if (needed) {
                    fortune_almanac_t next;
                    if (fetch_day(date, &next)) {
                        portENTER_CRITICAL(&s_lock);
                        s_latest = next;
                        s_ready = true;
                        portEXIT_CRITICAL(&s_lock);
                        ESP_LOGI(TAG, "today summary ready for %s", date);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

bool fortune_almanac_start(void)
{
    return xTaskCreate(worker, "fortune_day", 6144, NULL, 3, NULL) == pdPASS;
}

bool fortune_almanac_snapshot(fortune_almanac_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_lock);
    bool ready = s_ready;
    if (ready) *out = s_latest;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

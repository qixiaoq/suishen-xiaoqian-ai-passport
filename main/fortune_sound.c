#include "fortune_sound.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#define SAMPLE_RATE 16000
#define BLOCK_SAMPLES 160

static QueueHandle_t s_queue;
static const char *TAG = "fortune_sound";

static void play_coin_sound(void)
{
    if (bsp_audio_init() != ESP_OK ||
        bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "coin sound unavailable");
        return;
    }
    bsp_audio_set_volume(42);
    uint32_t noise = 0xA68231u;
    int16_t pcm[BLOCK_SAMPLES];
    // A brief rattle followed by three distinct metal-like coin landings.
    // The worker never accesses LVGL or blocks the button callback.
    for (int block = 0; block < 42; ++block) {
        int envelope = block < 30 ? 35 - block : 5;
        for (int i = 0; i < BLOCK_SAMPLES; ++i) {
            noise = noise * 1664525u + 1013904223u;
            int sample = (int)((noise >> 16) & 0x7fff) - 16384;
            pcm[i] = (int16_t)(sample * envelope / 120);
        }
        if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) return;
    }
    for (int coin = 0; coin < 3; ++coin) {
        for (int block = 0; block < 12; ++block) {
            for (int i = 0; i < BLOCK_SAMPLES; ++i) {
                noise = noise * 1664525u + 1013904223u;
                float t = (float)(block * BLOCK_SAMPLES + i) / SAMPLE_RATE;
                float decay = (12.0f - block) / 12.0f;
                float ring = sinf(2.0f * 3.14159265f * (1420.0f + coin * 85.0f) * t)
                           + 0.48f * sinf(2.0f * 3.14159265f * 2310.0f * t);
                float impact = block < 2 ? ((int)((noise >> 16) & 0xffff) - 32768) / 32768.0f : 0.0f;
                pcm[i] = (int16_t)((ring * 4700.0f + impact * 2800.0f) * decay);
            }
            if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) return;
        }
        for (int i = 0; i < BLOCK_SAMPLES; ++i) pcm[i] = 0;
        for (int block = 0; block < 12; ++block) {
            if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) return;
        }
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    uint8_t event;
    while (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
        if (event == 1) play_coin_sound();
    }
    vTaskDelete(NULL);
}

bool fortune_sound_start(void)
{
    s_queue = xQueueCreate(3, sizeof(uint8_t));
    if (!s_queue) return false;
    if (xTaskCreate(sound_task, "fortune_sound", 4096, NULL, 4, NULL) == pdPASS) return true;
    vQueueDelete(s_queue);
    s_queue = NULL;
    return false;
}

void fortune_sound_toss(void)
{
    if (!s_queue) return;
    uint8_t event = 1;
    xQueueSend(s_queue, &event, 0);
}

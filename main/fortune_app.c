#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "fortune_almanac.h"
#include "fortune_logic.h"
#include "fortune_qr.h"
#include "fortune_net.h"
#include "fortune_sound.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !LV_USE_FONT_COMPRESSED
#error "fortune_font_16 uses compressed glyphs; enable LV_USE_FONT_COMPRESSED"
#endif

LV_FONT_DECLARE(fortune_font_16);
LV_FONT_DECLARE(fortune_font_12);
LV_FONT_DECLARE(fortune_title_font_24);

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_input_t;
typedef enum { HOME, DAILY, MEIHUA, LIUYAO, RESULT, BAZI, QR_PAGE, SETTINGS, CONFIRM_EXIT } page_t;

static const char *NAMES[] = { "今日一签", "一念成卦", "六掷问事", "生辰小笺" };
static const char *SUBTITLES[] = { "今日只取一帖", "按此刻封时", "三钱六掷成爻", "从命帖开始" };
static QueueHandle_t s_queue;
static lv_obj_t *s_screen;
static lv_obj_t *s_footer;
static lv_obj_t *s_cards[4];
static lv_obj_t *s_network_badge;
static lv_obj_t *s_header_date;
static lv_obj_t *s_header_time;
static lv_obj_t *s_header_wifi;
static lv_obj_t *s_header_battery;
static lv_obj_t *s_home_clock;
static lv_obj_t *s_home_lunar;
static lv_obj_t *s_home_yi;
static lv_obj_t *s_home_ji;
static lv_obj_t *s_settings_body;
static lv_timer_t *s_status_timer;
static lv_timer_t *s_header_timer;
static volatile int s_battery_percent = -1;
static page_t s_page;
static unsigned s_selected;
static unsigned s_count;
static uint8_t s_lines[6];
static uint8_t s_coin_history[6][3];
static volatile bool s_ready;
static lv_timer_t *s_toss_timer;
static lv_obj_t *s_coin_objects[3];
static lv_obj_t *s_coin_labels[3];
static uint8_t s_pending_coins[3];
static uint8_t s_pending_line;
static unsigned s_frame;
static bool s_revealed;
static bool s_daily_drawn;
static bool s_meihua_cast;
static char s_cast_clock[16];
static char s_payload[24];
static void result_page(void);

static bool current_local_time(struct tm *out)
{
    time_t now = time(NULL);
    if (now < 1704067200 || localtime_r(&now, out) == NULL) return false;
    return out->tm_year + 1900 >= 2024;
}

static bool current_almanac(fortune_almanac_t *out)
{
    struct tm today;
    char date[11];
    return current_local_time(&today) &&
           strftime(date, sizeof(date), "%Y-%m-%d", &today) &&
           fortune_almanac_snapshot(out) && strcmp(out->date, date) == 0;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *value, int x, int y,
                            int width, int height, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, &fortune_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, value);
    return label;
}

static lv_obj_t *small_label(lv_obj_t *parent, const char *value, int x, int y,
                             int width, int height, uint32_t color)
{
    lv_obj_t *label = make_label(parent, value, x, y, width, height, color);
    lv_obj_set_style_text_font(label, &fortune_font_12, 0);
    return label;
}

static lv_obj_t *paper(lv_obj_t *parent, int x, int y, int width, int height,
                       uint32_t fill, uint32_t edge)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(edge), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void ink_rule(lv_obj_t *parent, int x, int y, int width, uint32_t color)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
}

static void page_start(const char *title)
{
    if (s_toss_timer) {
        lv_timer_delete(s_toss_timer);
        s_toss_timer = NULL;
    }
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    s_header_date = NULL;
    s_header_time = NULL;
    s_header_wifi = NULL;
    s_header_battery = NULL;
    s_home_clock = NULL;
    s_home_lunar = NULL;
    s_home_yi = NULL;
    s_home_ji = NULL;
    s_network_badge = NULL;
    s_settings_body = NULL;
    lv_obj_t *previous = s_screen;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x241F22), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    paper(s_screen, 7, 7, 226, 306, 0xF2E9D3, 0xA78B64);
    ink_rule(s_screen, 11, 11, 218, 0xCCB88F);
    s_header_date = small_label(s_screen, "----.--.--", 16, 15, 88, 16, 0x765B43);
    s_header_time = small_label(s_screen, "--:--", 111, 15, 39, 16, 0x765B43);
    s_header_wifi = small_label(s_screen, "网", 157, 15, 15, 16, 0xA8977D);
    lv_obj_t *battery_shell = paper(s_screen, 179, 19, 12, 7, 0xF2E9D3, 0x604838);
    paper(battery_shell, 1, 1, 7, 3, 0x885E35, 0x885E35);
    paper(s_screen, 191, 21, 2, 3, 0x604838, 0x604838);
    s_header_battery = small_label(s_screen, "--%", 195, 15, 29, 16, 0x765B43);
    ink_rule(s_screen, 17, 34, 205, 0xBDA983);
    lv_obj_t *heading = make_label(s_screen, title, 18, 38, 181, 30, 0x302921);
    lv_obj_set_style_text_font(heading, &fortune_title_font_24, 0);
    paper(s_screen, 201, 40, 22, 23, 0xAA4435, 0xAA4435);
    small_label(s_screen, "淇", 205, 44, 16, 16, 0xFFF1D7);
    ink_rule(s_screen, 15, 278, 210, 0xBDA983);
    s_footer = small_label(s_screen, "OK继续 · 长按OK返回", 16, 285, 210, 18, 0x785B46);
    lv_screen_load(s_screen);
    if (previous) lv_obj_delete(previous);
}

static void header_tick(lv_timer_t *timer)
{
    (void)timer;
    struct tm now;
    char date_text[36] = "日期待同步";
    char time_text[16] = "--:--";
    if (current_local_time(&now)) {
        strftime(date_text, sizeof(date_text), "%Y.%m.%d", &now);
        strftime(time_text, sizeof(time_text), "%H:%M", &now);
    }
    if (s_header_date) lv_label_set_text(s_header_date, date_text);
    if (s_header_time) lv_label_set_text(s_header_time, time_text);
    if (s_header_wifi) {
        lv_obj_set_style_text_color(s_header_wifi,
            lv_color_hex(fortune_net_connected() ? 0x8E3C2D : 0xA8977D), 0);
    }
    if (s_header_battery) {
        char battery[12];
        int value = s_battery_percent;
        if (value >= 0) snprintf(battery, sizeof(battery), "%d%%", value);
        else snprintf(battery, sizeof(battery), "--%%");
        lv_label_set_text(s_header_battery, battery);
    }
}

static void home_refresh(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_set_style_bg_color(s_cards[i],
            lv_color_hex(i == s_selected ? 0xE6D4AE : 0xFAF5E9), 0);
        lv_obj_set_style_border_color(s_cards[i],
            lv_color_hex(i == s_selected ? 0xA64032 : 0xBDAD8F), 0);
    }
    if (s_network_badge) {
        lv_label_set_text(s_network_badge, fortune_net_connected() ? "已联网" :
                          fortune_net_portal_active() ? "配网中" :
                          fortune_net_connecting() ? "连接中" : "未联网");
    }
    struct tm now;
    if (s_home_clock) {
        char clock_text[16] = "--:--";
        if (current_local_time(&now)) strftime(clock_text, sizeof(clock_text), "%H:%M", &now);
        lv_label_set_text(s_home_clock, clock_text);
    }
    if (s_home_lunar) {
        fortune_almanac_t day;
        if (current_almanac(&day)) {
            lv_label_set_text(s_home_lunar, day.lunar);
            lv_label_set_text_fmt(s_home_yi, "宜  %s", day.yi);
            lv_label_set_text_fmt(s_home_ji, "忌  %s", day.ji);
        } else {
            lv_label_set_text(s_home_lunar, "今日黄历待同步");
            lv_label_set_text(s_home_yi, "宜  联网后显示");
            lv_label_set_text(s_home_ji, "忌  联网后显示");
        }
    }
}

static void home_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_page == HOME) home_refresh();
}

static void home(void)
{
    s_page = HOME;
    page_start("掌中签馆");
    small_label(s_screen, "今日宜静观 · 一念成签", 19, 66, 202, 15, 0x8B5E3D);
    lv_obj_t *arch = paper(s_screen, 19, 83, 202, 80, 0xE6D1A7, 0xA88154);
    lv_obj_set_style_radius(arch, 44, 0);
    lv_obj_t *sun = paper(arch, 143, 11, 32, 32, 0xD6AD6A, 0xB37D4B);
    lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *far_hill = paper(arch, 72, 36, 150, 94, 0xA18B69, 0xA18B69);
    lv_obj_set_style_radius(far_hill, 75, 0);
    lv_obj_t *near_hill = paper(arch, -38, 31, 150, 98, 0x76694F, 0x76694F);
    lv_obj_set_style_radius(near_hill, 75, 0);
    paper(arch, 85, 53, 33, 18, 0x5A4432, 0x5A4432);
    paper(arch, 78, 48, 47, 5, 0x493327, 0x493327);
    s_home_lunar = small_label(s_screen, "", 27, 87, 137, 15, 0x674B38);
    lv_label_set_long_mode(s_home_lunar, LV_LABEL_LONG_MODE_DOTS);
    ink_rule(s_screen, 19, 169, 202, 0xB79972);
    s_home_yi = small_label(s_screen, "", 23, 174, 96, 17, 0x9E382C);
    s_home_ji = small_label(s_screen, "", 123, 174, 95, 17, 0x715B4E);
    lv_label_set_long_mode(s_home_yi, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(s_home_ji, LV_LABEL_LONG_MODE_DOTS);
    ink_rule(s_screen, 19, 194, 202, 0xB79972);
    const char *marks[] = { "日", "易", "爻", "命" };
    for (unsigned i = 0; i < 4; ++i) {
        int x = 17 + (int)(i % 2) * 106;
        int y = 201 + (int)(i / 2) * 36;
        lv_obj_t *card = paper(s_screen, x, y, 100, 32, 0xF4E8D0, 0xAB8963);
        s_cards[i] = card;
        lv_obj_t *mark = paper(card, 3, 4, 21, 21, 0xF5E4C4, 0xA88456);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
        small_label(mark, marks[i], 4, 3, 15, 15, 0x833D2C);
        small_label(card, NAMES[i], 28, 2, 70, 15, 0x302921);
        small_label(card, SUBTITLES[i], 28, 17, 70, 13, 0x826950);
    }
    home_refresh();
    s_status_timer = lv_timer_create(home_tick, 1000, NULL);
    lv_label_set_text(s_footer, "上下择门 · OK入 · 长上联网");
}

static void message_page(page_t page, const char *title, const char *message)
{
    s_page = page;
    page_start(title);
    paper(s_screen, 17, 70, 206, 194, 0xFBF5E8, 0xD0BA91);
    ink_rule(s_screen, 28, 81, 184, 0xA54232);
    make_label(s_screen, message, 28, 94, 184, 160, 0x302921);
    if (page == LIUYAO) lv_label_set_text(s_footer, "OK掷钱 · 长按OK返回");
    if (page == SETTINGS) lv_label_set_text(s_footer, "上键重试 · OK说明 · 长按OK返回");
    if (page == CONFIRM_EXIT) lv_label_set_text(s_footer, "上键继续 · 下键放弃");
}

static void settings_refresh(void)
{
    if (s_page != SETTINGS || !s_settings_body) return;
    char text[240];
    if (fortune_net_connected()) {
        struct tm now;
        char date_text[32] = "";
        bool synced = current_local_time(&now);
        if (synced) strftime(date_text, sizeof(date_text), "%Y-%m-%d %H:%M", &now);
        snprintf(text, sizeof(text), "已连接 Wi-Fi\nIP：%s\n\n%s%s\n\n长按下键清除旧网络",
                 fortune_net_ip(), synced ? "日期已同步\n" : "日期同步中", date_text);
    } else if (fortune_net_connecting() && fortune_net_portal_active()) {
        snprintf(text, sizeof(text), "正在连接 Wi-Fi…\n请稍候，本页自动更新\n\n热点：%s\n密码：%s\n\n手动打开 192.168.4.1",
                 fortune_net_device_name(), fortune_net_ap_password());
    } else if (fortune_net_connecting()) {
        snprintf(text, sizeof(text), "正在连接旧网络…\n\n若连接失败，设备会开启配网热点。\n\n本页会自动更新");
    } else if (fortune_net_portal_active()) {
        snprintf(text, sizeof(text), "手机先连接热点\n%s\n密码：%s\n\n弹窗填写 2.4G Wi-Fi\n不弹窗请打开\n192.168.4.1",
                 fortune_net_device_name(), fortune_net_ap_password());
    } else {
        snprintf(text, sizeof(text), "正在开启配网热点…\n\n请稍后再看\n\n上键重连旧网络");
    }
    lv_label_set_text(s_settings_body, text);
}

static void settings_tick(lv_timer_t *timer)
{
    (void)timer;
    settings_refresh();
}

static void settings_page(void)
{
    s_page = SETTINGS;
    page_start("联网与校时");
    paper(s_screen, 17, 70, 206, 194, 0xFBF5E8, 0xD0BA91);
    ink_rule(s_screen, 28, 81, 184, 0xA54232);
    s_settings_body = make_label(s_screen, "", 27, 94, 188, 166, 0x302921);
    lv_label_set_text(s_footer, "OK地址 · 长OK返 · 长下清除");
    settings_refresh();
    s_status_timer = lv_timer_create(settings_tick, 1000, NULL);
}

static void toss_page(void)
{
    s_page = LIUYAO;
    page_start("六掷问事");
    char heading[64];
    snprintf(heading, sizeof(heading), s_revealed ? "第 %u 掷 · 此爻已定" :
             "第 %u 掷 · 心中默念所问", s_revealed ? s_count : s_count + 1);
    small_label(s_screen, heading, 22, 74, 200, 18, 0xA54232);
    lv_obj_t *altar = paper(s_screen, 18, 95, 204, 98, 0xC09A67, 0xA77E4D);
    lv_obj_set_style_radius(altar, 47, 0);
    lv_obj_t *ring = paper(s_screen, 38, 105, 164, 77, 0xD5B379, 0xE4C48B);
    lv_obj_set_style_radius(ring, 45, 0);
    for (unsigned i = 0; i < 6; ++i) {
        unsigned index = 5 - i;
        int y = 210 + (int)i * 10;
        small_label(s_screen, index == 5 ? "上" : index == 0 ? "初" :
                   index == 1 ? "二" : index == 2 ? "三" :
                   index == 3 ? "四" : "五", 34, y - 2, 18, 13, 0x826B4E);
        if (index < s_count) {
            bool yang = s_lines[index] & 1u;
            if (yang) paper(s_screen, 62, y + 3, 119, 4, 0x55402F, 0x55402F);
            else {
                paper(s_screen, 62, y + 3, 52, 4, 0x55402F, 0x55402F);
                paper(s_screen, 129, y + 3, 52, 4, 0x55402F, 0x55402F);
            }
            if (s_lines[index] == 6 || s_lines[index] == 9)
                small_label(s_screen, "动", 190, y - 2, 16, 13, 0xA54232);
        } else {
            ink_rule(s_screen, 62, y + 5, 119, 0xC5B79D);
        }
    }
    char progress[24];
    snprintf(progress, sizeof(progress), "%u / 6", s_count);
    small_label(s_screen, "自下而上 · 逐爻成形", 26, 195, 156, 16, 0x826B4E);
    small_label(s_screen, progress, 185, 195, 38, 16, 0xA54232);
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *coin = paper(s_screen, 30 + (int)i * 63, 120, 43, 43,
                               0xD5AE66, 0x744D2A);
        lv_obj_set_style_radius(coin, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(coin, 3, 0);
        lv_obj_set_style_outline_color(coin, lv_color_hex(0xE8C784), 0);
        lv_obj_set_style_outline_width(coin, 1, 0);
        s_coin_objects[i] = coin;
        const char *face = s_revealed ?
            (s_coin_history[s_count - 1][i] ? "阳" : "阴") : "钱";
        s_coin_labels[i] = small_label(coin, face, 11, 13, 23, 17, 0x4A321D);
    }
    if (s_revealed)
        lv_label_set_text(s_footer, s_count == 6 ? "OK展卦 · 长按OK返回" :
                          "OK再掷 · 长按OK返回");
    else
        lv_label_set_text(s_footer, "OK摇三钱 · 长按OK返回");
}

static void toss_animation(lv_timer_t *timer)
{
    ++s_frame;
    for (unsigned i = 0; i < 3; ++i) {
        int width = s_frame < 22 ? ((s_frame + i) % 5 == 0 ? 8 : 43) : 43;
        int x = 30 + (int)i * 63 + (s_frame < 22 ? (int)((s_frame * (i + 3)) % 17) - 8 : 0);
        int y = 120 + (s_frame < 22 ? (int)((s_frame * (i + 5)) % 27) - 13 : 0);
        if (s_frame < 22) x += (43 - width) / 2;
        lv_obj_set_pos(s_coin_objects[i], x, y);
        lv_obj_set_width(s_coin_objects[i], width);
        if (s_frame == 22) {
            lv_label_set_text(s_coin_labels[i], s_pending_coins[i] ? "阳" : "阴");
            lv_obj_remove_flag(s_coin_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_frame < 32) return;
    lv_timer_delete(timer);
    s_toss_timer = NULL;
    memcpy(s_coin_history[s_count], s_pending_coins, sizeof(s_pending_coins));
    s_lines[s_count++] = s_pending_line;
    s_revealed = true;
    toss_page();
}

static void start_toss(void)
{
    for (unsigned i = 0; i < 3; ++i) s_pending_coins[i] = (uint8_t)(esp_random() & 1u);
    if (!fortune_three_coins(s_pending_coins, &s_pending_line)) return;
    for (unsigned i = 0; i < 3; ++i)
        lv_obj_add_flag(s_coin_labels[i], LV_OBJ_FLAG_HIDDEN);
    s_frame = 0;
    fortune_sound_toss();
    s_toss_timer = lv_timer_create(toss_animation, 50, NULL);
}

static void result_page(void)
{
    uint8_t base, changed;
    if (!fortune_six_lines(s_lines, &base, &changed)) return;
    static const char *trigrams[8] = { "坤", "震", "坎", "兑", "艮", "离", "巽", "乾" };
    s_page = RESULT;
    page_start("六爻成卦");
    paper(s_screen, 17, 80, 206, 176, 0xF7ECD7, 0xB08A58);
    small_label(s_screen, "六次铜钱 · 自下而上", 38, 89, 180, 17, 0x805C3E);
    char pair[64];
    snprintf(pair, sizeof(pair), "%s上%s下  之  %s上%s下",
             trigrams[(base >> 3) & 7], trigrams[base & 7],
             trigrams[(changed >> 3) & 7], trigrams[changed & 7]);
    small_label(s_screen, pair, 28, 111, 190, 18, 0x8E3C2D);
    ink_rule(s_screen, 29, 136, 182, 0xB99A6A);
    const char *position[] = { "初", "二", "三", "四", "五", "上" };
    for (unsigned i = 0; i < 6; ++i) {
        char line[40];
        snprintf(line, sizeof(line), "%s爻  %s%s", position[i],
                 s_lines[i] & 1u ? "阳" : "阴",
                 s_lines[i] == 6 || s_lines[i] == 9 ? " · 动" : " · 静");
        small_label(s_screen, line, 35 + (int)(i / 3) * 96,
                    148 + (int)(i % 3) * 23, 90, 18, 0x55402F);
    }
    small_label(s_screen, "卦象已记 · 手机写下所问", 29, 226, 185, 17, 0x7A5C43);
    lv_label_set_text(s_footer, "OK 手机深读 · 长按OK返回");
}

static void qr_page(const char *payload)
{
    s_page = QR_PAGE;
    page_start("扫码写下所问");
    lv_label_set_text(s_footer, "长按OK返回首页");
    const char *base = CONFIG_XIAOQIAN_COMPANION_URL;
    if (!base[0]) {
        make_label(s_screen, "请先配置手机解读服务地址", 18, 94, 208, 56, 0xA64032);
        return;
    }
    char url[256];
    int length = snprintf(url, sizeof(url), "%s/q/%s", base, payload);
    if (length <= 0 || length >= (int)sizeof(url) ||
        !fortune_qr_show(s_screen, url, 28, 65)) {
        make_label(s_screen, "二维码生成失败，请检查服务地址长度", 15, 94, 215, 75, 0xA64032);
        return;
    }
    make_label(s_screen, "手机填写问题 · 白话解读", 20, 253, 205, 25, 0x785B46);
}

static void provisioning_guide_page(void)
{
    s_page = QR_PAGE;
    page_start("打开配网页");
    lv_label_set_text(s_footer, "长按OK返回首页");
    const char *url = "http://192.168.4.1/";
    if (!fortune_qr_show(s_screen, url, 28, 65)) {
        make_label(s_screen, "说明二维码生成失败", 18, 100, 200, 45, 0xA64032);
    }
    make_label(s_screen, "先连热点 · 扫码打开配网", 17, 253, 210, 25, 0x785B46);
}

static void daily_page(void)
{
    s_payload[0] = '\0';
    struct tm today;
    if (!current_local_time(&today)) {
        message_page(DAILY, NAMES[0], "日期尚未同步\n请先完成 Wi-Fi 配网");
        return;
    }
    if (!strftime(s_payload, sizeof(s_payload), "d%Y%m%d", &today)) return;
    char date_text[32];
    if (!strftime(date_text, sizeof(date_text), "%Y年%m月%d日", &today)) return;
    s_page = DAILY;
    page_start(NAMES[0]);
    paper(s_screen, 27, 82, 186, 119, 0xF7EDD9, 0xB78E5D);
    ink_rule(s_screen, 35, 90, 170, 0xBFA176);
    small_label(s_screen, date_text, 45, 99, 121, 17, 0x9D5D39);
    fortune_almanac_t day;
    bool has_day = current_almanac(&day);
    if (has_day) {
        small_label(s_screen, day.day_type, 169, 99, 37, 17, 0x9E382C);
        lv_obj_t *lunar = small_label(s_screen, day.lunar, 45, 119, 164, 16, 0x765B43);
        lv_label_set_long_mode(lunar, LV_LABEL_LONG_MODE_DOTS);
    } else
        small_label(s_screen, "黄历正在同步", 45, 119, 164, 16, 0x765B43);
    const char *slips[] = { "见微", "缓行", "守正", "择机" };
    const char *hints[] = { "先看清眼前的小事", "留一步再作决定", "把重要的事做好", "准备好再迈下一步" };
    unsigned slip = has_day && strcmp(day.day_type, "黄道") == 0 ?
                    2u + ((unsigned)today.tm_yday & 1u) : ((unsigned)today.tm_yday & 1u);
    if (s_daily_drawn) {
        lv_obj_t *word = make_label(s_screen, slips[slip], 80, 139, 105, 33, 0x773A2B);
        lv_obj_set_style_text_font(word, &fortune_title_font_24, 0);
        small_label(s_screen, hints[slip], 50, 174, 150, 18, 0x5F4431);
    } else {
        make_label(s_screen, "今日未启", 64, 148, 135, 28, 0xA0856A);
        small_label(s_screen, "按 OK 揭开今日签", 53, 177, 150, 17, 0x765B43);
    }
    ink_rule(s_screen, 35, 194, 170, 0xBFA176);
    if (has_day) {
        lv_obj_t *yi = small_label(s_screen, "", 31, 211, 177, 19, 0x9E382C);
        lv_obj_t *ji = small_label(s_screen, "", 31, 235, 177, 19, 0x715B4E);
        lv_label_set_long_mode(yi, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_long_mode(ji, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_fmt(yi, "宜  %s", day.yi);
        lv_label_set_text_fmt(ji, "忌  %s", day.ji);
    } else {
        small_label(s_screen, "联网后显示今日宜忌", 37, 215, 170, 18, 0x715B4E);
    }
    lv_label_set_text(s_footer, !has_day ? "黄历待同步 · 长按OK返回" :
                      s_daily_drawn ? "OK 手机深读 · 长按OK返回" :
                      "OK 揭今日签 · 长按OK返回");
}

static void battery_task(void *arg)
{
    (void)arg;
    if (bsp_battery_init() == ESP_OK) {
        while (true) {
            s_battery_percent = bsp_battery_soc();
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }
    ESP_LOGW("fortune", "battery gauge unavailable");
    vTaskDelete(NULL);
}

static void meihua_page(void)
{
    struct tm now;
    if (!current_local_time(&now)) {
        message_page(MEIHUA, NAMES[1], "时间尚未同步\n请先完成 Wi-Fi 配网");
        return;
    }
    char clock_text[16];
    if (!strftime(clock_text, sizeof(clock_text), "%H:%M", &now)) return;
    s_page = MEIHUA;
    page_start(NAMES[1]);
    small_label(s_screen, s_meihua_cast ? "时刻已封 · 手机可见卦象" :
                "想好一事 · 按此刻起卦", 28, 75, 190, 17, 0x8E3C2D);
    lv_obj_t *dial = paper(s_screen, 58, 100, 124, 124, 0xF2E3C3, 0x9B714C);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *inner = paper(dial, 11, 11, 100, 100, 0xF4E7CB, 0xAF8A5B);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *center = paper(dial, 23, 23, 76, 76, 0xF4E7CB, 0xB99162);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    small_label(s_screen, "时", 115, 95, 18, 18, 0x9E382C);
    make_label(s_screen, s_meihua_cast ? s_cast_clock : clock_text,
               89, 146, 90, 25, 0x733C2B);
    small_label(s_screen, s_meihua_cast ? "此刻已记" : "此刻", 105, 174, 65, 18, 0x765B43);
    small_label(s_screen, s_meihua_cast ? "卦象在手机上按农历时刻计算" :
                "按 OK 封存此刻，再扫码解读", 32, 238, 187, 23, 0x76573D);
    lv_label_set_text(s_footer, s_meihua_cast ? "OK 手机深读 · 长按OK返回" :
                      "OK 此刻起卦 · 长按OK返回");
}

static void bazi_page(void)
{
    s_page = BAZI;
    page_start(NAMES[3]);
    small_label(s_screen, "出生资料不必全部记得", 31, 76, 190, 17, 0x8E3C2D);
    paper(s_screen, 29, 103, 182, 139, 0xF7ECD6, 0xB08A58);
    ink_rule(s_screen, 41, 115, 158, 0xD2B990);
    const char *heads[] = { "年", "月", "日", "时" };
    const char *values[] = { "待", "填", "写", "可略" };
    for (unsigned i = 0; i < 4; ++i) {
        int x = 40 + (int)i * 41;
        paper(s_screen, x, 131, 36, 79, 0xF7EAD0, 0xC6A776);
        small_label(s_screen, heads[i], x + 11, 140, 22, 18, 0x9E382C);
        small_label(s_screen, values[i], x + (i == 3 ? 4 : 10), 172,
                    i == 3 ? 32 : 22, 22, 0x785B46);
    }
    small_label(s_screen, "扫码填写年月日 · 时辰地点可留空", 25, 251,
                206, 18, 0x76573D);
    lv_label_set_text(s_footer, "OK 手机填命帖 · 长按OK返回");
}

static void process(key_input_t key)
{
    // MetaPass boots child applications from an OTA slot. Keep the standalone
    // factory build's controls unchanged, and offer a way back to its launcher.
    if (key.button == BSP_BTN_DOWN && key.event == BSP_BTN_LONG && s_page == HOME) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running && running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            const esp_partition_t *factory = esp_partition_find_first(
                ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
            if (factory && esp_ota_set_boot_partition(factory) == ESP_OK) {
                esp_restart();
            }
        }
        return;
    }
    if (key.button == BSP_BTN_DOWN && key.event == BSP_BTN_LONG && s_page == SETTINGS) {
        esp_err_t err = fortune_net_clear_credentials();
        if (err != ESP_OK) ESP_LOGW("fortune", "Wi-Fi clear failed: %s", esp_err_to_name(err));
        settings_page();
        return;
    }
    if (key.button == BSP_BTN_UP && key.event == BSP_BTN_LONG && s_page == HOME) {
        settings_page();
        return;
    }
    if (key.button == BSP_BTN_OK && key.event == BSP_BTN_LONG) {
        if (s_page == LIUYAO && s_count > 0 && !s_toss_timer) {
            message_page(CONFIRM_EXIT, "暂离此卦", "已有几次掷卦记录\n\n按上键：继续\n按下键：放弃本次");
            return;
        }
        if (s_page == LIUYAO && s_toss_timer) return;
        if (s_page != HOME) home();
        return;
    }
    if (key.event != BSP_BTN_CLICK) return;
    if (s_page == CONFIRM_EXIT) {
        if (key.button == BSP_BTN_UP) toss_page();
        else if (key.button == BSP_BTN_DOWN) home();
        return;
    }
    if (s_page == HOME) {
        if (key.button == BSP_BTN_UP) s_selected = (s_selected + 3) % 4;
        else if (key.button == BSP_BTN_DOWN) s_selected = (s_selected + 1) % 4;
        else if (key.button == BSP_BTN_OK) {
            switch (s_selected) {
            case 0: s_daily_drawn = false; daily_page(); break;
            case 1: s_meihua_cast = false; s_payload[0] = '\0'; meihua_page(); break;
            case 2: s_count = 0; s_revealed = false; memset(s_lines, 0, sizeof(s_lines)); memset(s_coin_history, 0, sizeof(s_coin_history)); toss_page(); break;
            default: bazi_page(); break;
            }
            return;
        }
        home_refresh();
        return;
    }
    if (s_page == LIUYAO && key.button == BSP_BTN_OK) {
        if (s_toss_timer) return;
        if (s_revealed && s_count == 6) result_page();
        else if (s_revealed) { s_revealed = false; toss_page(); }
        else if (s_count < 6) start_toss();
    } else if (key.button == BSP_BTN_OK && s_page == RESULT) {
        snprintf(s_payload, sizeof(s_payload), "l%u%u%u%u%u%u",
                 s_lines[0], s_lines[1], s_lines[2], s_lines[3], s_lines[4], s_lines[5]);
        qr_page(s_payload);
    } else if (key.button == BSP_BTN_OK && s_page == DAILY) {
        fortune_almanac_t day;
        if (!s_daily_drawn) {
            s_daily_drawn = current_almanac(&day);
            daily_page();
        }
        else if (s_payload[0]) qr_page(s_payload);
    } else if (key.button == BSP_BTN_OK && s_page == MEIHUA) {
        if (!s_meihua_cast) {
            struct tm now;
            if (current_local_time(&now) &&
                strftime(s_payload, sizeof(s_payload), "m%Y%m%d%H%M", &now) &&
                strftime(s_cast_clock, sizeof(s_cast_clock), "%H:%M", &now)) {
                s_meihua_cast = true;
                meihua_page();
            }
        } else if (s_payload[0]) qr_page(s_payload);
    } else if (key.button == BSP_BTN_OK && s_page == BAZI) {
        qr_page("b");
    } else if (key.button == BSP_BTN_OK && s_page == SETTINGS) {
        provisioning_guide_page();
    } else if (key.button == BSP_BTN_UP && s_page == SETTINGS) {
        esp_err_t err = fortune_net_retry();
        if (err != ESP_OK) ESP_LOGW("fortune", "Wi-Fi retry unavailable: %s", esp_err_to_name(err));
        settings_refresh();
    }
}

static void input_task(void *arg)
{
    (void)arg;
    key_input_t key;
    while (xQueueReceive(s_queue, &key, portMAX_DELAY) == pdTRUE) {
        if (bsp_lvgl_lock(1000)) {
            process(key);
            bsp_lvgl_unlock();
        }
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_ready) return;
    key_input_t key = { button, event };
    xQueueSend(s_queue, &key, 0);
}

void app_main(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE("fortune", "display initialization failed");
        return;
    }
    bsp_display_backlight(85);
    s_queue = xQueueCreate(8, sizeof(key_input_t));
    if (!s_queue || xTaskCreate(input_task, "fortune_input", 4096, NULL, 5, NULL) != pdPASS ||
        bsp_button_init(button_callback, NULL) != ESP_OK) {
        ESP_LOGE("fortune", "input initialization failed");
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        home();
        header_tick(NULL);
        s_header_timer = lv_timer_create(header_tick, 1000, NULL);
        bsp_lvgl_unlock();
        s_ready = true;
    }
    if (xTaskCreate(battery_task, "fortune_battery", 4096, NULL, 3, NULL) != pdPASS)
        ESP_LOGW("fortune", "battery worker unavailable");
    esp_err_t network_error = fortune_net_start();
    if (network_error != ESP_OK) {
        ESP_LOGE("fortune", "provisioning failed: %s", esp_err_to_name(network_error));
    }
    if (!fortune_almanac_start()) ESP_LOGW("fortune", "almanac worker unavailable");
    if (!fortune_sound_start()) ESP_LOGW("fortune", "coin sound worker unavailable");
}

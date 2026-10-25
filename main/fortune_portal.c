// Short-lived captive portal, adapted from ESP-IDF 5.5.3's CC0 example.
#include "fortune_portal.h"
#include "fortune_portal_form.h"
#include "fortune_net.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fortune_portal";
static httpd_handle_t s_http;
static volatile bool s_dns_running;
static volatile TaskHandle_t s_dns_task;
static uint32_t s_ap_ip;
static fortune_portal_submit_cb_t s_submit;
static fortune_portal_status_cb_t s_status;
static volatile bool s_scanning;
static volatile TaskHandle_t s_scan_task;
static char s_scan_json[1800] = "{\"state\":\"idle\",\"networks\":[]}";

static void scan_networks(void)
{
    wifi_scan_config_t config = { .show_hidden = false };
    config.scan_time.active.min = 30;
    config.scan_time.active.max = 45;
    esp_err_t err = esp_wifi_scan_start(&config, true);
    uint16_t count = 12;
    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    char *json = malloc(sizeof(s_scan_json));
    if (!records || !json) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    if (err == ESP_OK) {
        size_t used = 0;
        bool first = true;
        int n = snprintf(json, sizeof(s_scan_json), "{\"state\":\"ready\",\"networks\":[");
        used = (size_t)n;
        for (uint16_t i = 0; i < count; ++i) {
            size_t len = strnlen((const char *)records[i].ssid, sizeof(records[i].ssid));
            if (!len || used + len * 6 + 80 >= sizeof(s_scan_json)) continue;
            n = snprintf(json + used, sizeof(s_scan_json) - used, "%s{\"ssid\":\"", first ? "" : ",");
            used += (size_t)n;
            first = false;
            for (size_t j = 0; j < len; ++j) {
                unsigned char c = records[i].ssid[j];
                if (c == '"' || c == '\\') { json[used++] = '\\'; json[used++] = c; }
                else if (c < 0x20) used += (size_t)snprintf(json + used, sizeof(s_scan_json) - used, "\\u%04x", c);
                else json[used++] = c;
            }
            n = snprintf(json + used, sizeof(s_scan_json) - used,
                         "\",\"rssi\":%d,\"secure\":%s}", records[i].rssi,
                         records[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
            used += (size_t)n;
        }
        snprintf(json + used, sizeof(s_scan_json) - used, "]}");
        memcpy(s_scan_json, json, strlen(json) + 1);
        ESP_LOGI(TAG, "Wi-Fi scan found %u networks", (unsigned)count);
    } else {
        snprintf(s_scan_json, sizeof(s_scan_json), "{\"state\":\"error\",\"networks\":[]}");
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
    free(json);
    free(records);
    s_scanning = false;
}

void fortune_portal_scan_now(void)
{
    if (s_scanning) return;
    s_scanning = true;
    scan_networks();
}

static void scan_task(void *arg)
{
    (void)arg;
    scan_networks();
    s_scan_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    if (req->uri[5] == '?' && strstr(req->uri, "refresh=1") && !s_scanning &&
        !fortune_net_connecting()) {
        s_scanning = true;
        strcpy(s_scan_json, "{\"state\":\"scanning\",\"networks\":[]}");
        if (xTaskCreate(scan_task, "fortune_scan", 4096, NULL, 4,
                        (TaskHandle_t *)&s_scan_task) != pdPASS) {
            s_scanning = false;
            strcpy(s_scan_json, "{\"state\":\"error\",\"networks\":[]}");
        }
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, s_scan_json);
}

static const char PAGE[] =
    "<!doctype html><html lang='zh-CN'><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>随身小签 · 联网</title><style>"
    "body{margin:0;background:#261f21;color:#352c28;font:16px/1.6 system-ui,sans-serif}"
    "main{max-width:440px;margin:32px auto;padding:28px;background:#f3ead4;"
    "border:5px double #ae8b64;box-shadow:0 16px 40px #100c0c}"
    "h1{font-family:serif;font-size:27px;color:#a34434;margin:0 0 12px}"
    "p{margin:8px 0 20px}label{display:block;margin:16px 0 5px}"
    "input{box-sizing:border-box;width:100%;font:inherit;padding:11px;"
    "border:1px solid #aa9679;background:#fffaf0;border-radius:3px}"
    "button{width:100%;margin-top:22px;padding:12px;background:#a34434;"
    "color:#fff;border:0;border-radius:3px;font:inherit}"
    "small{color:#705f4e}#status{padding:9px 0;color:#a34434}"
    "[hidden]{display:none!important}"
    "#networks button{margin:5px 0;text-align:left;background:#e9dbc0;color:#352c28;"
    "border:1px solid #b9a17e;padding:9px}#rescan{background:#695746;margin-top:10px}</style>"
    "<main><h1>掌中签馆 · 连网</h1><p>为随身小签选择可用的 2.4 GHz Wi-Fi。"
    "连接成功后，设备会自动关闭此热点。</p>"
    "<form id='f' action='/connect' method='post'>"
    "<label>附近的 Wi-Fi</label><div id='networks'>正在搜索…</div>"
    "<button type='button' id='rescan'>重新搜索</button>"
    "<label for='s'>Wi-Fi 名称（也可手动填写）</label>"
    "<input id='s' name='ssid' maxlength='32' required>"
    "<div id='passwordRow' hidden><label for='p'>Wi-Fi 密码</label>"
    "<input id='p' name='password' type='password' maxlength='63'></div>"
    "<button>连接 Wi-Fi</button></form><div id='status'>等待填写</div>"
    "<small>若弹窗未出现，请在浏览器打开 http://192.168.4.1</small></main>"
    "<script>let f=document.getElementById('f'),s=document.getElementById('status');"
    "let list=document.getElementById('networks'),name=document.getElementById('s');"
    "let row=document.getElementById('passwordRow'),pass=document.getElementById('p');"
    "name.oninput=()=>{row.hidden=!name.value.trim()};"
    "async function scan(refresh){try{let r=await fetch('/scan'+(refresh?'?refresh=1':''));"
    "let d=await r.json();if(d.state==='scanning'){list.textContent='正在搜索…';"
    "setTimeout(()=>scan(false),1600);return}"
    "list.replaceChildren();if(d.state==='error'||!d.networks.length){"
    "list.textContent=d.state==='error'?'搜索失败，请重试或手动填写':'未找到网络，可手动填写';return}"
    "d.networks.forEach(n=>{let b=document.createElement('button');b.type='button';"
    "b.textContent=n.ssid+' · '+(n.rssi>=-60?'信号强':n.rssi>=-75?'信号中':'信号弱');"
    "b.onclick=()=>{name.value=n.ssid;row.hidden=!n.secure;"
    "if(!n.secure)pass.value='';else pass.focus()};"
    "list.append(b)})}catch(e){list.textContent='搜索中断，请重试或手动填写'}}"
    "document.getElementById('rescan').onclick=()=>scan(true);scan(false);"
    "f.onsubmit=async e=>{e.preventDefault();s.textContent='正在连接，请稍候…';"
    "try{let r=await fetch('/connect',{method:'POST',body:new URLSearchParams(new FormData(f))});"
    "s.textContent=await r.text()}catch(e){s.textContent='提交失败，请重试'}};"
    "setInterval(async()=>{try{let r=await fetch('/status');"
    "if(r.ok)s.textContent=await r.text()}catch(e){}},2500)</script></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, s_status ? s_status() : "等待配网");
}

static esp_err_t connect_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 256) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Form too large");
        return ESP_FAIL;
    }
    char body[257];
    size_t count = 0;
    while (count < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + count, (size_t)req->content_len - count);
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Incomplete form");
            return ESP_FAIL;
        }
        count += (size_t)n;
    }
    body[count] = '\0';
    char ssid[33];
    char password[64];
    if (!fortune_form_decode_field(body, "ssid", ssid, sizeof(ssid)) || !ssid[0] ||
        !fortune_form_decode_field(body, "password", password, sizeof(password))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi fields");
        return ESP_FAIL;
    }
    esp_err_t err = s_submit ? s_submit(ssid, password) : ESP_ERR_INVALID_STATE;
    memset(password, 0, sizeof(password));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "连接未启动，请稍后重试");
    }
    return httpd_resp_sendstr(req, "已收到，设备正在验证 Wi-Fi…");
}

static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_sendstr(req, "Open the setup page");
}

// Answer A queries with the AP address. Bounds are checked before copying the
// question; unsupported or malformed packets receive no response.
static size_t dns_reply(const uint8_t *in, size_t size, uint8_t *out, size_t cap)
{
    if (size < 18 || cap < size + 16 || in[2] & 0x80 || in[4] != 0 || in[5] != 1)
        return 0;
    size_t end = 12;
    while (end < size && in[end] != 0) {
        uint8_t label = in[end];
        if (label > 63 || end + 1 + label >= size) return 0;
        end += 1 + label;
    }
    if (end + 5 > size) return 0;
    end++;
    if (in[end] != 0 || in[end + 2] != 0 || in[end + 3] != 1) return 0;
    uint8_t query_type = in[end + 1];
    if (query_type != 1 && query_type != 28) return 0;
    size_t question = end + 4;
    memcpy(out, in, question);
    out[2] = 0x81; out[3] = 0x80;
    out[6] = 0; out[7] = query_type == 1 ? 1 : 0;
    out[8] = out[9] = out[10] = out[11] = 0;
    if (query_type == 28) return question; // NODATA avoids an AAAA timeout.
    uint8_t answer[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 0, 0, 0, 0 };
    memcpy(answer + 12, &s_ap_ip, 4);
    memcpy(out + question, answer, sizeof(answer));
    return question + sizeof(answer);
}

static void dns_task(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) goto done;
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 250000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons(53),
                                   .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        ESP_LOGW(TAG, "DNS bind failed: errno %d", errno);
        close(fd);
        goto done;
    }
    uint8_t request[256], answer[272];
    while (s_dns_running) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int received = recvfrom(fd, request, sizeof(request), 0,
                                (struct sockaddr *)&peer, &peer_len);
        if (received <= 0) continue;
        size_t answer_len = dns_reply(request, (size_t)received, answer, sizeof(answer));
        if (answer_len) sendto(fd, answer, answer_len, 0,
                               (struct sockaddr *)&peer, peer_len);
    }
    close(fd);
done:
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t fortune_portal_start(uint32_t ap_ip, fortune_portal_submit_cb_t submit,
                               fortune_portal_status_cb_t status)
{
    if (s_http) return ESP_OK;
    s_ap_ip = ap_ip;
    s_submit = submit;
    s_status = status;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.max_uri_handlers = 5;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&s_http, &config) != ESP_OK) return ESP_FAIL;
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post };
    httpd_uri_t state = { .uri = "/status", .method = HTTP_GET, .handler = status_get };
    httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get };
    if (httpd_register_uri_handler(s_http, &root) != ESP_OK ||
        httpd_register_uri_handler(s_http, &connect) != ESP_OK ||
        httpd_register_uri_handler(s_http, &state) != ESP_OK ||
        httpd_register_uri_handler(s_http, &scan) != ESP_OK ||
        httpd_register_err_handler(s_http, HTTPD_404_NOT_FOUND, redirect_404) != ESP_OK) {
        httpd_stop(s_http);
        s_http = NULL;
        return ESP_FAIL;
    }
    s_dns_running = true;
    if (xTaskCreate(dns_task, "fortune_dns", 3072, NULL, 4,
                    (TaskHandle_t *)&s_dns_task) != pdPASS) {
        s_dns_running = false;
        ESP_LOGW(TAG, "DNS unavailable; use manual portal address");
    }
    return ESP_OK;
}

void fortune_portal_stop(void)
{
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
    if (s_scanning) esp_wifi_scan_stop();
    for (int i = 0; s_scan_task && i < 40; ++i) vTaskDelay(pdMS_TO_TICKS(50));
    s_dns_running = false;
    for (int i = 0; s_dns_task && i < 10; ++i) vTaskDelay(pdMS_TO_TICKS(50));
    s_submit = NULL;
    s_status = NULL;
}

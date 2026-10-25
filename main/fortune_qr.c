#include "fortune_qr.h"
#include "vendor/qrcodegen/qrcodegen.h"
#include <stdint.h>
#include <string.h>

#define QR_PIXELS 184

static uint8_t s_temp[qrcodegen_BUFFER_LEN_MAX];
static uint8_t s_qr[qrcodegen_BUFFER_LEN_MAX];
// 1-bit indexed canvas avoids holding a 68 KB RGB565 bitmap in ESP32-C3 RAM.
static uint8_t s_pixels[8192];

bool fortune_qr_show(lv_obj_t *parent, const char *url, int x, int y)
{
    if (!url || !qrcodegen_encodeText(url, s_temp, s_qr, qrcodegen_Ecc_LOW,
                                       qrcodegen_VERSION_MIN, 12,
                                       qrcodegen_Mask_AUTO, false)) return false;
    int modules = qrcodegen_getSize(s_qr);
    int scale = QR_PIXELS / (modules + 8);
    if (scale < 2) return false;
    lv_obj_t *canvas = lv_canvas_create(parent);
    memset(s_pixels, 0, sizeof(s_pixels));
    lv_canvas_set_buffer(canvas, s_pixels, QR_PIXELS, QR_PIXELS, LV_COLOR_FORMAT_I1);
    lv_canvas_set_palette(canvas, 0, lv_color32_make(255, 255, 255, 255));
    lv_canvas_set_palette(canvas, 1, lv_color32_make(0, 0, 0, 255));
    lv_obj_set_pos(canvas, x, y);
    int origin = (QR_PIXELS - modules * scale) / 2;
    for (int row = 0; row < modules; ++row) {
        for (int col = 0; col < modules; ++col) {
            if (!qrcodegen_getModule(s_qr, col, row)) continue;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    lv_canvas_set_px(canvas, origin + col * scale + dx,
                                     origin + row * scale + dy,
                                     lv_color_make(0, 0, 1), LV_OPA_COVER);
                }
            }
        }
    }
    return true;
}

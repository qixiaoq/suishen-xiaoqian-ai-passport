#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Draws a scannable QR code with four-module quiet zone on a 184px canvas.
bool fortune_qr_show(lv_obj_t *parent, const char *url, int x, int y);

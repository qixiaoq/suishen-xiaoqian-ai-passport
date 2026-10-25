#pragma once

#include <stdbool.h>

// Audio runs on a worker task; callers never block LVGL or button input.
bool fortune_sound_start(void);
void fortune_sound_toss(void);

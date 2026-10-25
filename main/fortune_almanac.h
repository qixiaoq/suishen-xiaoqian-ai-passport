#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char date[11];
    char lunar[64];
    char day_type[16];
    char yi[64];
    char ji[64];
} fortune_almanac_t;

// Fetches a short traditional almanac summary from the companion service.
// The worker never touches LVGL objects; readers receive a locked snapshot.
bool fortune_almanac_start(void);
bool fortune_almanac_snapshot(fortune_almanac_t *out);

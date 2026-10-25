#pragma once

#include <stdbool.h>
#include <stdint.h>

// Bit 0 is the bottom line; a set bit is yang. The input numbers follow
// the traditional 1..12 branches and lunar month/day numbering.
typedef struct {
    uint8_t upper_trigram;
    uint8_t lower_trigram;
    uint8_t moving_line;
    uint8_t base_lines;
    uint8_t changed_lines;
} fortune_meihua_t;

bool fortune_meihua_from_time(uint8_t year_branch, uint8_t lunar_month,
                              uint8_t lunar_day, uint8_t hour_branch,
                              fortune_meihua_t *result);

// Each coin is 0 or 1. Values 6 and 9 are moving lines.
bool fortune_three_coins(const uint8_t coins[3], uint8_t *line_value);
bool fortune_six_lines(const uint8_t line_values[6], uint8_t *base_lines,
                       uint8_t *changed_lines);

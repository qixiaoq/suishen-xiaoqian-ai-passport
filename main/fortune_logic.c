#include "fortune_logic.h"

// 1=Qian, 2=Dui, 3=Li, 4=Zhen, 5=Xun, 6=Kan, 7=Gen, 8=Kun.
// Each value stores bottom-to-top yang lines in its lowest three bits.
static const uint8_t TRIGRAM_LINES[9] = {
    0, 0x7, 0x3, 0x5, 0x1, 0x6, 0x2, 0x4, 0x0,
};

static uint8_t one_based_remainder(unsigned value, unsigned divisor)
{
    unsigned remainder = value % divisor;
    return (uint8_t)(remainder == 0 ? divisor : remainder);
}

bool fortune_meihua_from_time(uint8_t year_branch, uint8_t lunar_month,
                              uint8_t lunar_day, uint8_t hour_branch,
                              fortune_meihua_t *result)
{
    if (result == 0 || year_branch < 1 || year_branch > 12 ||
        lunar_month < 1 || lunar_month > 12 || lunar_day < 1 ||
        lunar_day > 30 || hour_branch < 1 || hour_branch > 12) {
        return false;
    }
    unsigned first = year_branch + lunar_month + lunar_day;
    unsigned total = first + hour_branch;
    result->upper_trigram = one_based_remainder(first, 8);
    result->lower_trigram = one_based_remainder(total, 8);
    result->moving_line = one_based_remainder(total, 6);
    result->base_lines = (uint8_t)(TRIGRAM_LINES[result->lower_trigram] |
                                    (TRIGRAM_LINES[result->upper_trigram] << 3));
    result->changed_lines = (uint8_t)(result->base_lines ^
                                       (1u << (result->moving_line - 1)));
    return true;
}

bool fortune_three_coins(const uint8_t coins[3], uint8_t *line_value)
{
    if (coins == 0 || line_value == 0) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (coins[i] > 1) return false;
    }
    *line_value = (uint8_t)(6 + coins[0] + coins[1] + coins[2]);
    return true;
}

bool fortune_six_lines(const uint8_t line_values[6], uint8_t *base_lines,
                       uint8_t *changed_lines)
{
    if (line_values == 0 || base_lines == 0 || changed_lines == 0) return false;
    uint8_t base = 0;
    uint8_t changed = 0;
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t value = line_values[i];
        if (value < 6 || value > 9) return false;
        bool yang = (value & 1u) != 0;
        bool moving = value == 6 || value == 9;
        if (yang) base |= (uint8_t)(1u << i);
        if (yang != moving) changed |= (uint8_t)(1u << i);
    }
    *base_lines = base;
    *changed_lines = changed;
    return true;
}

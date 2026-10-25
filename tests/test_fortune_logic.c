#include "fortune_logic.h"

#include <assert.h>

int main(void)
{
    fortune_meihua_t m = {0};
    // Chen year, lunar month 12, day 17, Shen hour:
    // 34 -> Dui; 43 -> Li; moving line 1.
    assert(fortune_meihua_from_time(5, 12, 17, 9, &m));
    assert(m.upper_trigram == 2 && m.lower_trigram == 3);
    assert(m.moving_line == 1);
    assert(m.base_lines == 0x1d && m.changed_lines == 0x1c);
    assert(!fortune_meihua_from_time(5, 0, 17, 9, &m));

    uint8_t coins[3] = {0, 1, 1};
    uint8_t line = 0;
    assert(fortune_three_coins(coins, &line) && line == 8);
    coins[1] = 2;
    assert(!fortune_three_coins(coins, &line));

    uint8_t lines[6] = {6, 7, 8, 9, 7, 8};
    uint8_t base = 0, changed = 0;
    assert(fortune_six_lines(lines, &base, &changed));
    assert(base == 0x1a && changed == 0x13);
    lines[0] = 5;
    assert(!fortune_six_lines(lines, &base, &changed));
    return 0;
}

#include "fortune_portal_form.h"
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fortune_form_decode_field(const char *body, const char *key, char *out, size_t cap)
{
    if (!body || !key || !out || cap == 0) return false;
    size_t key_len = strlen(key);
    const char *item = body;
    while (*item) {
        const char *end = strchr(item, '&');
        if (!end) end = item + strlen(item);
        if ((size_t)(end - item) > key_len && !memcmp(item, key, key_len) &&
            item[key_len] == '=') {
            const char *value = item + key_len + 1;
            size_t length = 0;
            while (value < end) {
                unsigned char c = (unsigned char)*value++;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end - value < 2) return false;
                    int hi = hex_digit(value[0]);
                    int lo = hex_digit(value[1]);
                    if (hi < 0 || lo < 0) return false;
                    c = (unsigned char)((hi << 4) | lo);
                    value += 2;
                }
                if (!c || length + 1 >= cap) return false;
                out[length++] = (char)c;
            }
            out[length] = '\0';
            return true;
        }
        item = *end ? end + 1 : end;
    }
    return false;
}

#include "fortune_portal_form.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char ssid[33];
    char password[64];
    assert(fortune_form_decode_field("ssid=Home+WiFi&password=a%26b%2Bc",
                                     "ssid", ssid, sizeof(ssid)));
    assert(strcmp(ssid, "Home WiFi") == 0);
    assert(fortune_form_decode_field("ssid=Home+WiFi&password=a%26b%2Bc",
                                     "password", password, sizeof(password)));
    assert(strcmp(password, "a&b+c") == 0);
    assert(fortune_form_decode_field("ssid=%E5%AE%B6&password=", "ssid",
                                     ssid, sizeof(ssid)));
    assert(strcmp(ssid, "家") == 0);
    assert(!fortune_form_decode_field("ssid=%GG&password=x", "ssid", ssid, sizeof(ssid)));
    assert(!fortune_form_decode_field("ssid=a%00b", "ssid", ssid, sizeof(ssid)));
    assert(!fortune_form_decode_field("ssid=12345", "ssid", ssid, 5));
    assert(!fortune_form_decode_field("password=x", "ssid", ssid, sizeof(ssid)));
    return 0;
}

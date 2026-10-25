#pragma once

#include <stdbool.h>
#include <stddef.h>

// Decode one application/x-www-form-urlencoded value with a strict size bound.
bool fortune_form_decode_field(const char *body, const char *key, char *out, size_t capacity);

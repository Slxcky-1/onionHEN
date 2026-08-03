#pragma once

#include <stddef.h>

int onion_trial_hex_encode(const unsigned char *in, size_t in_len, char *out,
                           size_t out_size);
int onion_trial_hex_decode(const char *hex, unsigned char *out, size_t out_len);

#include "crypto_util.h"

#include <string.h>

int
onion_act_hex_encode(const unsigned char *in, size_t in_len, char *out,
                     size_t out_size) {
  static const char hex[] = "0123456789abcdef";

  if(in == NULL || out == NULL || out_size < in_len * 2 + 1) {
    return -1;
  }
  for(size_t i = 0; i < in_len; ++i) {
    out[i * 2] = hex[(in[i] >> 4) & 0xf];
    out[i * 2 + 1] = hex[in[i] & 0xf];
  }
  out[in_len * 2] = '\0';
  return 0;
}

static int
hex_nibble(char ch) {
  if(ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if(ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if(ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

int
onion_act_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
  if(hex == NULL || out == NULL || strlen(hex) != out_len * 2) {
    return -1;
  }
  for(size_t i = 0; i < out_len; ++i) {
    int high = hex_nibble(hex[i * 2]);
    int low = hex_nibble(hex[i * 2 + 1]);

    if(high < 0 || low < 0) {
      return -1;
    }
    out[i] = (unsigned char)((high << 4) | low);
  }
  return 0;
}

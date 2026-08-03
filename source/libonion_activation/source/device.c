#include "device.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <onion/log.h>

#define HW_SERIAL_BUFFER_SIZE 1000
#define OPEN_PSID_SIZE 16
#define OPEN_PSID_STRING_SIZE (OPEN_PSID_SIZE * 2 + 1)

int sceKernelGetHwSerialNumber(char *);

typedef struct SceKernelOpenPsId {
  uint8_t open_psid[OPEN_PSID_SIZE];
} SceKernelOpenPsId;

int sceKernelGetOpenPsId(SceKernelOpenPsId *id);
int sceKernelGetOpenPsIdForSystem(SceKernelOpenPsId *id);

static size_t
trim_device_serial(const char *raw, size_t raw_size, char *out,
                   size_t out_size) {
  const unsigned char *bytes = (const unsigned char *)raw;
  size_t start = 0;
  size_t end = raw_size;
  size_t len;

  while(start < end &&
        (bytes[start] == 0x00 || bytes[start] == 0xff || bytes[start] == ' ' ||
         bytes[start] == '\t' || bytes[start] == '\r' ||
         bytes[start] == '\n')) {
    start++;
  }
  while(end > start &&
        (bytes[end - 1] == 0x00 || bytes[end - 1] == 0xff ||
         bytes[end - 1] == ' ' || bytes[end - 1] == '\t' ||
         bytes[end - 1] == '\r' || bytes[end - 1] == '\n')) {
    end--;
  }

  len = end - start;
  if(out == NULL || out_size == 0 || len == 0 || len >= out_size) {
    return 0;
  }
  memcpy(out, raw + start, len);
  out[len] = '\0';
  return len;
}

static int
open_psid_bytes_usable(const uint8_t id[OPEN_PSID_SIZE]) {
  int all_zero = 1;
  int all_ff = 1;

  for(size_t i = 0; i < OPEN_PSID_SIZE; i++) {
    if(id[i] != 0x00) {
      all_zero = 0;
    }
    if(id[i] != 0xff) {
      all_ff = 0;
    }
  }
  return !(all_zero || all_ff);
}

static int
format_open_psid_string(const uint8_t id[OPEN_PSID_SIZE], char *out,
                        size_t out_size) {
  static const char hex[] = "0123456789abcdef";

  if(out == NULL || out_size < OPEN_PSID_STRING_SIZE) {
    return -1;
  }
  if(!open_psid_bytes_usable(id)) {
    return -1;
  }
  for(size_t i = 0; i < OPEN_PSID_SIZE; i++) {
    out[i * 2] = hex[(id[i] >> 4) & 0xf];
    out[i * 2 + 1] = hex[id[i] & 0xf];
  }
  out[OPEN_PSID_SIZE * 2] = '\0';
  return 0;
}

static int
read_hardware_serial(char *out, size_t out_size) {
  char raw[HW_SERIAL_BUFFER_SIZE];
  size_t raw_len;
  size_t serial_len;
  int ret;

  if(out == NULL || out_size == 0) {
    return -1;
  }
  memset(raw, 0, sizeof(raw));
  ret = sceKernelGetHwSerialNumber(raw);
  if(ret != 0) {
    LOG_WARN("[activation] sceKernelGetHwSerialNumber failed ret=0x%x",
             (unsigned)ret);
    return -1;
  }
  raw_len = strnlen(raw, sizeof(raw));
  serial_len = trim_device_serial(raw, raw_len, out, out_size);
  if(serial_len == 0) {
    LOG_WARN("[activation] hardware serial unusable");
    return -1;
  }
  return 0;
}

static int
read_open_psid_fallback(char *out, size_t out_size) {
  SceKernelOpenPsId id;
  int ret;

  if(out == NULL || out_size == 0) {
    return -1;
  }
  memset(&id, 0, sizeof(id));
  ret = sceKernelGetOpenPsId(&id);
  if(ret == 0 && format_open_psid_string(id.open_psid, out, out_size) == 0) {
    LOG_INFO("[activation] using OpenPsId fallback");
    return 0;
  }
  memset(&id, 0, sizeof(id));
  ret = sceKernelGetOpenPsIdForSystem(&id);
  if(ret == 0 && format_open_psid_string(id.open_psid, out, out_size) == 0) {
    LOG_INFO("[activation] using OpenPsIdForSystem fallback");
    return 0;
  }
  return -1;
}

int
onion_activation_device_serial(char *out, size_t out_size) {
  if(out == NULL || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if(read_hardware_serial(out, out_size) == 0) {
    return 0;
  }
  return read_open_psid_fallback(out, out_size);
}

#pragma once

#include <stddef.h>

#define ONION_LICENSE_MAX_FEATURES 32
#define ONION_LICENSE_TEXT_LENGTH 128
#define ONION_LICENSE_SIGNATURE_LENGTH 136

typedef struct {
  int version;
  char license_id[ONION_LICENSE_TEXT_LENGTH];
  char subject[ONION_LICENSE_TEXT_LENGTH];
  char device_id[ONION_LICENSE_TEXT_LENGTH];
  char features[ONION_LICENSE_MAX_FEATURES][ONION_LICENSE_TEXT_LENGTH];
  size_t feature_count;
  long long issued_at;
  long long expires_at;
  char nonce[ONION_LICENSE_TEXT_LENGTH];
  char signature[ONION_LICENSE_SIGNATURE_LENGTH];
} onion_license_t;

int onion_license_parse(const char *json, onion_license_t *license);
int onion_license_has_feature(const onion_license_t *license,
                              const char *feature);
int onion_license_build_signing_payload(const onion_license_t *license,
                                        char *out, size_t out_size);
int onion_license_verify_signature(const onion_license_t *license);

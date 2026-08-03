#include "license.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "crypto_util.h"
#include "ed25519.h"

/*
 * ONION_ACTIVATION_PUBLIC_KEY_HEX must be injected at compile time
 * (CMake reads env ONION_ACTIVATION_PUBLIC_KEY_HEX). No in-source default.
 */
#ifndef ONION_ACTIVATION_PUBLIC_KEY_HEX
#error "ONION_ACTIVATION_PUBLIC_KEY_HEX is required (set env ONION_ACTIVATION_PUBLIC_KEY_HEX at build)"
#endif

static int
copy_json_string(cJSON *root, const char *name, char *out, size_t out_size) {
  cJSON *item = cJSON_GetObjectItem(root, name);

  if(out == NULL || out_size == 0 || !cJSON_IsString(item) ||
     item->valuestring == NULL || item->valuestring[0] == '\0' ||
     strlen(item->valuestring) >= out_size) {
    return -1;
  }
  snprintf(out, out_size, "%s", item->valuestring);
  return 0;
}

static int
copy_json_int(cJSON *root, const char *name, long long *out) {
  cJSON *item = cJSON_GetObjectItem(root, name);

  if(out == NULL || !cJSON_IsNumber(item)) {
    return -1;
  }
  *out = (long long)item->valuedouble;
  return 0;
}

int
onion_license_parse(const char *json, onion_license_t *license) {
  cJSON *root = NULL;
  cJSON *features = NULL;
  int feature_count;
  long long version;

  if(json == NULL || license == NULL) {
    return -1;
  }
  memset(license, 0, sizeof(*license));
  root = cJSON_Parse(json);
  if(!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return -1;
  }
  if(copy_json_int(root, "version", &version) < 0 ||
     copy_json_string(root, "licenseId", license->license_id,
                      sizeof(license->license_id)) < 0 ||
     copy_json_string(root, "subject", license->subject,
                      sizeof(license->subject)) < 0 ||
     copy_json_string(root, "deviceId", license->device_id,
                      sizeof(license->device_id)) < 0 ||
     copy_json_int(root, "issuedAt", &license->issued_at) < 0 ||
     copy_json_int(root, "expiresAt", &license->expires_at) < 0 ||
     copy_json_string(root, "nonce", license->nonce, sizeof(license->nonce)) <
         0 ||
     copy_json_string(root, "signature", license->signature,
                      sizeof(license->signature)) < 0) {
    cJSON_Delete(root);
    return -1;
  }
  license->version = (int)version;

  features = cJSON_GetObjectItem(root, "features");
  feature_count = cJSON_IsArray(features) ? cJSON_GetArraySize(features) : -1;
  if(feature_count <= 0 || feature_count > ONION_LICENSE_MAX_FEATURES) {
    cJSON_Delete(root);
    return -1;
  }
  for(int i = 0; i < feature_count; ++i) {
    cJSON *feature = cJSON_GetArrayItem(features, i);

    if(!cJSON_IsString(feature) || feature->valuestring == NULL ||
       feature->valuestring[0] == '\0' ||
       strlen(feature->valuestring) >= sizeof(license->features[i])) {
      cJSON_Delete(root);
      return -1;
    }
    snprintf(license->features[i], sizeof(license->features[i]), "%s",
             feature->valuestring);
  }
  license->feature_count = (size_t)feature_count;
  cJSON_Delete(root);
  return 0;
}

int
onion_license_has_feature(const onion_license_t *license,
                          const char *feature) {
  if(license == NULL || feature == NULL || feature[0] == '\0') {
    return 0;
  }
  for(size_t i = 0; i < license->feature_count; ++i) {
    if(strcmp(license->features[i], feature) == 0 ||
       strcmp(license->features[i], "*") == 0) {
      return 1;
    }
  }
  return 0;
}

static int
append_text(char *out, size_t out_size, size_t *used, const char *text) {
  int written;

  if(out == NULL || used == NULL || text == NULL || *used >= out_size) {
    return -1;
  }
  written = snprintf(out + *used, out_size - *used, "%s", text);
  if(written < 0 || (size_t)written >= out_size - *used) {
    return -1;
  }
  *used += (size_t)written;
  return 0;
}

int
onion_license_build_signing_payload(const onion_license_t *license, char *out,
                                    size_t out_size) {
  size_t used = 0;
  char number[64];

  if(license == NULL || out == NULL || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  snprintf(number, sizeof(number), "%d", license->version);
  if(append_text(out, out_size, &used, "version=") < 0 ||
     append_text(out, out_size, &used, number) < 0 ||
     append_text(out, out_size, &used, "\nlicenseId=") < 0 ||
     append_text(out, out_size, &used, license->license_id) < 0 ||
     append_text(out, out_size, &used, "\nsubject=") < 0 ||
     append_text(out, out_size, &used, license->subject) < 0 ||
     append_text(out, out_size, &used, "\ndeviceId=") < 0 ||
     append_text(out, out_size, &used, license->device_id) < 0 ||
     append_text(out, out_size, &used, "\nfeatures=") < 0) {
    return -1;
  }
  for(size_t i = 0; i < license->feature_count; ++i) {
    if(i > 0 && append_text(out, out_size, &used, ",") < 0) {
      return -1;
    }
    if(append_text(out, out_size, &used, license->features[i]) < 0) {
      return -1;
    }
  }
  snprintf(number, sizeof(number), "%lld", license->issued_at);
  if(append_text(out, out_size, &used, "\nissuedAt=") < 0 ||
     append_text(out, out_size, &used, number) < 0) {
    return -1;
  }
  snprintf(number, sizeof(number), "%lld", license->expires_at);
  if(append_text(out, out_size, &used, "\nexpiresAt=") < 0 ||
     append_text(out, out_size, &used, number) < 0 ||
     append_text(out, out_size, &used, "\nnonce=") < 0 ||
     append_text(out, out_size, &used, license->nonce) < 0) {
    return -1;
  }
  return 0;
}

int
onion_license_verify_signature(const onion_license_t *license) {
  char payload[4096];
  unsigned char public_key[32];
  unsigned char signature[64];

  if(license == NULL ||
     onion_license_build_signing_payload(license, payload, sizeof(payload)) <
         0 ||
     onion_act_hex_decode(ONION_ACTIVATION_PUBLIC_KEY_HEX, public_key,
                          sizeof(public_key)) < 0 ||
     onion_act_hex_decode(license->signature, signature, sizeof(signature)) <
         0) {
    return -1;
  }
  return ed25519_verify(signature, (const unsigned char *)payload,
                        strlen(payload), public_key) == 1
             ? 0
             : -1;
}

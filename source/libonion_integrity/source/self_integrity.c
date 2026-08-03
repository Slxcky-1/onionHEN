#include <onion/integrity.h>

#ifdef ONION_ENABLE_ELF_PROTECTION

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ed25519.h"

#ifndef ONION_ELF_SIGNING_PUBLIC_KEY_HEX
#error "ONION_ELF_SIGNING_PUBLIC_KEY_HEX is required when ELF protection is on"
#endif

#define ONION_SIGNATURE_MAGIC "OHNSIG01"
#define ONION_SIGNATURE_VERSION 1u
#define ONION_CONFIG_MAGIC "OHNCFG01"

typedef struct {
  unsigned char magic[8];
  uint32_t version;
  uint32_t reserved;
  unsigned char signature[64];
} onion_embedded_signature_t;

typedef struct {
  unsigned char magic[8];
  uint64_t protected_vaddr;
  uint64_t protected_size;
} onion_integrity_config_t;

/* Signature lives outside the executable PT_LOAD (linker places it in rodata). */
__attribute__((section(".onion_signature"), used, aligned(16)))
static volatile const onion_embedded_signature_t k_embedded_signature = {
    .magic = ONION_SIGNATURE_MAGIC,
    .version = ONION_SIGNATURE_VERSION,
    .reserved = 0,
    .signature = {0},
};

/* Config is inside .text so range patches are covered by the signature. */
__attribute__((section(".text"), used, aligned(8)))
static volatile const onion_integrity_config_t k_integrity_config = {
    .magic = ONION_CONFIG_MAGIC,
    .protected_vaddr = 0,
    .protected_size = 0,
};

__attribute__((section(".text"), used, aligned(1)))
static const char k_signing_public_key_hex[] = ONION_ELF_SIGNING_PUBLIC_KEY_HEX;

/* LLD provides the ELF header base for PIE/payload images. */
extern const unsigned char __ehdr_start[];

static int
hex_value(char ch) {
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

static int
decode_public_key(unsigned char key[32]) {
  const char *text = k_signing_public_key_hex;

  if(strlen(text) != 64) {
    return -1;
  }
  for(size_t i = 0; i < 32; ++i) {
    int high = hex_value(text[i * 2]);
    int low = hex_value(text[i * 2 + 1]);
    if(high < 0 || low < 0) {
      return -1;
    }
    key[i] = (unsigned char)((high << 4) | low);
  }
  return 0;
}

static int
config_magic_valid(void) {
  static const char expected[8] = ONION_CONFIG_MAGIC;
  for(size_t i = 0; i < sizeof(expected); ++i) {
    if(k_integrity_config.magic[i] != (unsigned char)expected[i]) {
      return 0;
    }
  }
  return 1;
}

static int
signature_header_valid(void) {
  static const char expected[8] = ONION_SIGNATURE_MAGIC;
  for(size_t i = 0; i < sizeof(expected); ++i) {
    if(k_embedded_signature.magic[i] != (unsigned char)expected[i]) {
      return 0;
    }
  }
  return k_embedded_signature.version == ONION_SIGNATURE_VERSION &&
         k_embedded_signature.reserved == 0;
}

int
onion_self_integrity_verify(void) {
  unsigned char public_key[32];
  const unsigned char *signature =
      (const unsigned char *)(const void *)k_embedded_signature.signature;
  uintptr_t protected_start;

  if(!signature_header_valid() || !config_magic_valid() ||
     k_integrity_config.protected_size == 0 ||
     k_integrity_config.protected_size > SIZE_MAX ||
     k_integrity_config.protected_vaddr >
         UINTPTR_MAX - (uintptr_t)__ehdr_start ||
     decode_public_key(public_key) < 0) {
    return -1;
  }
  protected_start =
      (uintptr_t)__ehdr_start + (uintptr_t)k_integrity_config.protected_vaddr;
  if(k_integrity_config.protected_size > UINTPTR_MAX - protected_start) {
    return -1;
  }
  return ed25519_verify(signature, (const unsigned char *)protected_start,
                        (size_t)k_integrity_config.protected_size,
                        public_key) == 1
             ? 0
             : -1;
}

#define ONION_SELF_INTEGRITY_BAD (ONION_SELF_INTEGRITY_OK ^ 0x5a3c96e1u)

static atomic_uint g_integrity_status = ONION_SELF_INTEGRITY_BAD;
static pthread_once_t g_prime_once = PTHREAD_ONCE_INIT;

static unsigned
compute_integrity_status(void) {
  return onion_self_integrity_verify() == 0 ? ONION_SELF_INTEGRITY_OK
                                            : ONION_SELF_INTEGRITY_BAD;
}

static void
prime_integrity_status(void) {
  atomic_store(&g_integrity_status, compute_integrity_status());
}

unsigned
onion_self_integrity_status(void) {
  pthread_once(&g_prime_once, prime_integrity_status);
  return atomic_load(&g_integrity_status);
}

static void *
integrity_monitor_thread(void *arg) {
  (void)arg;
  for(;;) {
    unsigned interval = 15u + (unsigned)((unsigned long)clock() % 31u);
    sleep(interval);
    atomic_store(&g_integrity_status, compute_integrity_status());
  }
  return NULL;
}

void
onion_self_integrity_start_monitor(void) {
  pthread_t thread;

  pthread_once(&g_prime_once, prime_integrity_status);
  if(pthread_create(&thread, NULL, integrity_monitor_thread, NULL) == 0) {
    pthread_detach(thread);
  }
}

#else /* !ONION_ENABLE_ELF_PROTECTION */

int
onion_self_integrity_verify(void) {
  return 0;
}

unsigned
onion_self_integrity_status(void) {
  return ONION_SELF_INTEGRITY_OK;
}

void
onion_self_integrity_start_monitor(void) {
}

#endif

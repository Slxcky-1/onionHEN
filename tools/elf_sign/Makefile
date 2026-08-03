# Host tools for OnionHEN ELF self-signing (not cross-compiled for PS5).
#
#   make -C tools/elf_sign
#   ./tools/elf_sign/sign-elf SEED_HEX in.elf out.elf PUBLIC_KEY_HEX
#   ./tools/elf_sign/verify-signed-elf PUBLIC_KEY_HEX signed.elf

ROOT := $(abspath ../..)
ED25519 := $(ROOT)/source/libonion_integrity/vendor/ed25519
OUT_DIR ?= $(ROOT)/build/tools
HOST_CC ?= cc

COMMON_SRCS := \
	$(ED25519)/fe.c \
	$(ED25519)/ge.c \
	$(ED25519)/sc.c \
	$(ED25519)/sha512.c

SIGN_SRCS := sign_elf.c $(COMMON_SRCS) $(ED25519)/keypair.c $(ED25519)/sign.c $(ED25519)/seed.c
VERIFY_SRCS := verify_signed_elf.c $(COMMON_SRCS) $(ED25519)/verify.c

CFLAGS ?= -O2 -Wall -Wextra -I. -I$(ED25519)

.PHONY: all clean

all: $(OUT_DIR)/sign-elf $(OUT_DIR)/verify-signed-elf

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/sign-elf: $(SIGN_SRCS) elf64_format.h | $(OUT_DIR)
	$(HOST_CC) $(CFLAGS) -o $@ $(SIGN_SRCS)

$(OUT_DIR)/verify-signed-elf: $(VERIFY_SRCS) elf64_format.h | $(OUT_DIR)
	$(HOST_CC) $(CFLAGS) -o $@ $(VERIFY_SRCS)

clean:
	rm -f $(OUT_DIR)/sign-elf $(OUT_DIR)/verify-signed-elf

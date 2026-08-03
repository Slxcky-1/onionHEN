#include <onion/integrity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "elf64_min.h"

#ifdef ONION_ENABLE_ELF_PROTECTION

#include "ed25519.h"

#ifndef ONION_ELF_SIGNING_PUBLIC_KEY_HEX
#error "ONION_ELF_SIGNING_PUBLIC_KEY_HEX is required when ELF protection is on"
#endif

#define SIGNATURE_SECTION ".onion_signature"
#define SIGNATURE_MAGIC "OHNSIG01"
#define CONFIG_MAGIC "OHNCFG01"
#define SIGNATURE_RECORD_SIZE 80u
#define SIGNATURE_FIELD_OFFSET 16u
#define CONFIG_RECORD_SIZE 24u
#define CONFIG_VADDR_OFFSET 8u
#define CONFIG_SIZE_OFFSET 16u

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
  const char *text = ONION_ELF_SIGNING_PUBLIC_KEY_HEX;

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
range_valid(size_t offset, size_t length, size_t file_size) {
  return offset <= file_size && length <= file_size - offset;
}

int
onion_elf_verify_signed_image(const void *elf, size_t size) {
  const unsigned char *data = (const unsigned char *)elf;
  unsigned char public_key[32];
  const Elf64_Ehdr *ehdr;
  const Elf64_Phdr *phdr;
  const Elf64_Shdr *shdr;
  const Elf64_Shdr *shstr;
  const char *names;
  const unsigned char *record = NULL;
  const Elf64_Phdr *protected_segment = NULL;
  const unsigned char *config = NULL;
  uint64_t protected_vaddr;
  uint64_t protected_size;

  if(data == NULL || size < sizeof(Elf64_Ehdr) || decode_public_key(public_key) < 0) {
    return -1;
  }
  ehdr = (const Elf64_Ehdr *)data;
  if(memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
     ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
     ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
     ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
     ehdr->e_shentsize != sizeof(Elf64_Shdr) || ehdr->e_shstrndx >= ehdr->e_shnum ||
     !range_valid((size_t)ehdr->e_phoff,
                  (size_t)ehdr->e_phnum * sizeof(Elf64_Phdr), size) ||
     !range_valid((size_t)ehdr->e_shoff,
                  (size_t)ehdr->e_shnum * sizeof(Elf64_Shdr), size)) {
    return -1;
  }
  phdr = (const Elf64_Phdr *)(data + ehdr->e_phoff);
  shdr = (const Elf64_Shdr *)(data + ehdr->e_shoff);
  shstr = &shdr[ehdr->e_shstrndx];
  if(!range_valid((size_t)shstr->sh_offset, (size_t)shstr->sh_size, size)) {
    return -1;
  }
  names = (const char *)(data + shstr->sh_offset);
  for(size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name;
    if(shdr[i].sh_name >= shstr->sh_size) {
      return -1;
    }
    name = names + shdr[i].sh_name;
    if(memchr(name, '\0', (size_t)shstr->sh_size - shdr[i].sh_name) == NULL) {
      return -1;
    }
    if(strcmp(name, SIGNATURE_SECTION) == 0) {
      if(shdr[i].sh_size != SIGNATURE_RECORD_SIZE ||
         !range_valid((size_t)shdr[i].sh_offset, SIGNATURE_RECORD_SIZE, size)) {
        return -1;
      }
      record = data + shdr[i].sh_offset;
      break;
    }
  }
  if(record == NULL || memcmp(record, SIGNATURE_MAGIC, 8) != 0 ||
     record[8] != 1 || record[9] != 0 || record[10] != 0 || record[11] != 0 ||
     record[12] != 0 || record[13] != 0 || record[14] != 0 || record[15] != 0) {
    return -1;
  }
  for(size_t i = 0; i < ehdr->e_phnum; ++i) {
    if(phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_X) != 0) {
      if(protected_segment != NULL) {
        return -1;
      }
      protected_segment = &phdr[i];
    }
  }
  if(protected_segment == NULL ||
     !range_valid((size_t)protected_segment->p_offset,
                  (size_t)protected_segment->p_filesz, size)) {
    return -1;
  }
  for(size_t i = 0; i + CONFIG_RECORD_SIZE <= protected_segment->p_filesz; ++i) {
    const unsigned char *candidate =
        data + protected_segment->p_offset + i;
    if(memcmp(candidate, CONFIG_MAGIC, 8) == 0) {
      if(config != NULL) {
        return -1;
      }
      config = candidate;
    }
  }
  if(config == NULL) {
    return -1;
  }
  memcpy(&protected_vaddr, config + CONFIG_VADDR_OFFSET, sizeof(protected_vaddr));
  memcpy(&protected_size, config + CONFIG_SIZE_OFFSET, sizeof(protected_size));
  if(protected_segment->p_vaddr != protected_vaddr ||
     protected_segment->p_filesz != protected_size) {
    return -1;
  }
  if(ed25519_verify(record + SIGNATURE_FIELD_OFFSET,
                    data + protected_segment->p_offset,
                    (size_t)protected_segment->p_filesz, public_key) != 1) {
    return -1;
  }
  return 0;
}

#else /* !ONION_ENABLE_ELF_PROTECTION */

int
onion_elf_verify_signed_image(const void *elf, size_t size) {
  (void)elf;
  (void)size;
  return 0;
}

#endif

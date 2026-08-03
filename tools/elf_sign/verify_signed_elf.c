#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/libonion_integrity/vendor/ed25519/ed25519.h"
#include "elf64_format.h"

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
  if(ch >= '0' && ch <= '9') return ch - '0';
  if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static int
decode_public_key(const char *text, unsigned char key[32]) {
  if(text == NULL || strlen(text) != 64) return -1;
  for(size_t i = 0; i < 32; ++i) {
    int high = hex_value(text[i * 2]);
    int low = hex_value(text[i * 2 + 1]);
    if(high < 0 || low < 0) return -1;
    key[i] = (unsigned char)((high << 4) | low);
  }
  return 0;
}

static int
read_file(const char *path, unsigned char **data_out, size_t *size_out) {
  FILE *file = fopen(path, "rb");
  long length;
  unsigned char *data;

  if(file == NULL || fseek(file, 0, SEEK_END) != 0) {
    if(file != NULL) fclose(file);
    return -1;
  }
  length = ftell(file);
  if(length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  data = (unsigned char *)malloc((size_t)length);
  if(data == NULL || fread(data, (size_t)length, 1, file) != 1) {
    free(data);
    fclose(file);
    return -1;
  }
  fclose(file);
  *data_out = data;
  *size_out = (size_t)length;
  return 0;
}

static int
range_valid(size_t offset, size_t length, size_t file_size) {
  return offset <= file_size && length <= file_size - offset;
}

int
main(int argc, char **argv) {
  unsigned char public_key[32];
  unsigned char *data = NULL;
  unsigned char *record = NULL;
  size_t size = 0;
  Elf64_Ehdr *ehdr;
  Elf64_Phdr *phdr;
  Elf64_Shdr *shdr;
  Elf64_Shdr *shstr;
  const char *names;
  const Elf64_Phdr *protected_segment = NULL;
  unsigned char *config = NULL;
  uint64_t protected_vaddr;
  uint64_t protected_size;
  int valid;

  if(argc != 3 || decode_public_key(argv[1], public_key) < 0 ||
     read_file(argv[2], &data, &size) < 0 || size < sizeof(Elf64_Ehdr)) {
    fprintf(stderr, "Usage: %s PUBLIC_KEY_HEX SIGNED_ELF\n", argv[0]);
    free(data);
    return 2;
  }
  ehdr = (Elf64_Ehdr *)data;
  if(memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
     ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
     ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
     ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
     ehdr->e_shentsize != sizeof(Elf64_Shdr) || ehdr->e_shstrndx >= ehdr->e_shnum ||
     !range_valid((size_t)ehdr->e_phoff,
                  (size_t)ehdr->e_phnum * sizeof(Elf64_Phdr), size) ||
     !range_valid((size_t)ehdr->e_shoff,
                  (size_t)ehdr->e_shnum * sizeof(Elf64_Shdr), size)) {
    goto invalid;
  }
  phdr = (Elf64_Phdr *)(data + ehdr->e_phoff);
  shdr = (Elf64_Shdr *)(data + ehdr->e_shoff);
  shstr = &shdr[ehdr->e_shstrndx];
  if(!range_valid((size_t)shstr->sh_offset, (size_t)shstr->sh_size, size))
    goto invalid;
  names = (const char *)(data + shstr->sh_offset);
  for(size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name;
    if(shdr[i].sh_name >= shstr->sh_size) goto invalid;
    name = names + shdr[i].sh_name;
    if(memchr(name, '\0', (size_t)shstr->sh_size - shdr[i].sh_name) == NULL)
      goto invalid;
    if(strcmp(name, SIGNATURE_SECTION) == 0) {
      if(shdr[i].sh_size != SIGNATURE_RECORD_SIZE ||
         !range_valid((size_t)shdr[i].sh_offset, SIGNATURE_RECORD_SIZE, size))
        goto invalid;
      record = data + shdr[i].sh_offset;
      break;
    }
  }
  if(record == NULL || memcmp(record, SIGNATURE_MAGIC, 8) != 0 ||
     record[8] != 1 || record[9] != 0 || record[10] != 0 || record[11] != 0 ||
     record[12] != 0 || record[13] != 0 || record[14] != 0 || record[15] != 0)
    goto invalid;
  for(size_t i = 0; i < ehdr->e_phnum; ++i) {
    if(phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_X) != 0) {
      if(protected_segment != NULL) goto invalid;
      protected_segment = &phdr[i];
    }
  }
  if(protected_segment == NULL ||
     !range_valid((size_t)protected_segment->p_offset,
                  (size_t)protected_segment->p_filesz, size))
    goto invalid;
  for(size_t i = 0; i + CONFIG_RECORD_SIZE <= protected_segment->p_filesz; ++i) {
    unsigned char *candidate = data + protected_segment->p_offset + i;
    if(memcmp(candidate, CONFIG_MAGIC, 8) == 0) {
      if(config != NULL) goto invalid;
      config = candidate;
    }
  }
  if(config == NULL) goto invalid;
  memcpy(&protected_vaddr, config + CONFIG_VADDR_OFFSET, sizeof(protected_vaddr));
  memcpy(&protected_size, config + CONFIG_SIZE_OFFSET, sizeof(protected_size));
  if(protected_segment->p_vaddr != protected_vaddr ||
     protected_segment->p_filesz != protected_size)
    goto invalid;
  valid = ed25519_verify(record + SIGNATURE_FIELD_OFFSET,
                         data + protected_segment->p_offset,
                         (size_t)protected_segment->p_filesz, public_key);
  free(data);
  if(valid != 1) {
    fprintf(stderr, "verify_signed_elf: signature mismatch\n");
    return 1;
  }
  printf("VALID\n");
  return 0;

invalid:
  free(data);
  fprintf(stderr, "verify_signed_elf: invalid ELF signature record\n");
  return 1;
}

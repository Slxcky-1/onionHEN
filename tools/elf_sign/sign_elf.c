#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/libonion_integrity/vendor/ed25519/ed25519.h"
#include "elf64_format.h"

#define SIGNATURE_SECTION ".onion_signature"
#define SIGNATURE_MAGIC "OHNSIG01"
#define CONFIG_MAGIC "OHNCFG01"
#define SIGNATURE_SIZE 64u
#define SIGNATURE_FIELD_OFFSET 16u
#define SIGNATURE_RECORD_SIZE 80u
#define CONFIG_VADDR_OFFSET 8u
#define CONFIG_SIZE_OFFSET 16u
#define CONFIG_RECORD_SIZE 24u

typedef struct {
  unsigned char *data;
  size_t size;
  Elf64_Ehdr *ehdr;
  Elf64_Phdr *phdr;
  Elf64_Shdr *shdr;
  const char *shstrtab;
  Elf64_Shdr *signature_section;
} elf_view_t;

static int
hex_value(char ch) {
  if(ch >= '0' && ch <= '9') return ch - '0';
  if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static int
decode_hex(const char *text, unsigned char *out, size_t out_size) {
  if(text == NULL || strlen(text) != out_size * 2) return -1;
  for(size_t i = 0; i < out_size; ++i) {
    int high = hex_value(text[i * 2]);
    int low = hex_value(text[i * 2 + 1]);
    if(high < 0 || low < 0) return -1;
    out[i] = (unsigned char)((high << 4) | low);
  }
  return 0;
}

static void
print_hex(const unsigned char *data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  for(size_t i = 0; i < size; ++i) {
    putchar(digits[data[i] >> 4]);
    putchar(digits[data[i] & 0x0f]);
  }
  putchar('\n');
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

static int
parse_elf(unsigned char *data, size_t size, elf_view_t *view) {
  Elf64_Ehdr *ehdr;
  Elf64_Shdr *shstr;

  if(size < sizeof(Elf64_Ehdr)) return -1;
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
    return -1;
  }
  memset(view, 0, sizeof(*view));
  view->data = data;
  view->size = size;
  view->ehdr = ehdr;
  view->phdr = (Elf64_Phdr *)(data + ehdr->e_phoff);
  view->shdr = (Elf64_Shdr *)(data + ehdr->e_shoff);
  shstr = &view->shdr[ehdr->e_shstrndx];
  if(!range_valid((size_t)shstr->sh_offset, (size_t)shstr->sh_size, size)) return -1;
  view->shstrtab = (const char *)(data + shstr->sh_offset);

  for(size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name;
    if(view->shdr[i].sh_name >= shstr->sh_size) return -1;
    name = view->shstrtab + view->shdr[i].sh_name;
    if(memchr(name, '\0', (size_t)shstr->sh_size - view->shdr[i].sh_name) == NULL)
      return -1;
    if(strcmp(name, SIGNATURE_SECTION) == 0) {
      view->signature_section = &view->shdr[i];
      break;
    }
  }
  if(view->signature_section == NULL ||
     view->signature_section->sh_size != SIGNATURE_RECORD_SIZE ||
     !range_valid((size_t)view->signature_section->sh_offset,
                  SIGNATURE_RECORD_SIZE, size) ||
     memcmp(data + view->signature_section->sh_offset, SIGNATURE_MAGIC, 8) != 0) {
    return -1;
  }
  return 0;
}

static int
build_signing_message(const elf_view_t *view, unsigned char **message_out,
                      size_t *message_size_out) {
  size_t message_size = 0;
  size_t message_offset = 0;
  unsigned char *message;
  size_t segment_count = 0;

  for(size_t i = 0; i < view->ehdr->e_phnum; ++i) {
    const Elf64_Phdr *segment = &view->phdr[i];
    if(segment->p_type != PT_LOAD || (segment->p_flags & PF_X) == 0) continue;
    if(!range_valid((size_t)segment->p_offset, (size_t)segment->p_filesz,
                    view->size) ||
       segment->p_filesz > SIZE_MAX - message_size) {
      return -1;
    }
    message_size += (size_t)segment->p_filesz;
    segment_count++;
  }
  if(message_size == 0 || segment_count != 1 ||
     (message = malloc(message_size)) == NULL)
    return -1;
  for(size_t i = 0; i < view->ehdr->e_phnum; ++i) {
    const Elf64_Phdr *segment = &view->phdr[i];
    size_t segment_offset;
    size_t segment_size;
    if(segment->p_type != PT_LOAD || (segment->p_flags & PF_X) == 0) continue;
    segment_offset = (size_t)segment->p_offset;
    segment_size = (size_t)segment->p_filesz;
    memcpy(message + message_offset, view->data + segment_offset, segment_size);
    message_offset += segment_size;
  }
  *message_out = message;
  *message_size_out = message_size;
  return 0;
}

static int
patch_integrity_config(elf_view_t *view, uint64_t *vaddr_out,
                       uint64_t *size_out) {
  const Elf64_Phdr *protected_segment = NULL;
  unsigned char *config = NULL;

  for(size_t i = 0; i < view->ehdr->e_phnum; ++i) {
    Elf64_Phdr *segment = &view->phdr[i];
    if(segment->p_type != PT_LOAD || (segment->p_flags & PF_X) == 0) continue;
    if(protected_segment != NULL ||
       !range_valid((size_t)segment->p_offset, (size_t)segment->p_filesz,
                    view->size))
      return -1;
    protected_segment = segment;
  }
  if(protected_segment == NULL) return -1;
  for(size_t i = 0; i < view->ehdr->e_shnum; ++i) {
    const Elf64_Shdr *section = &view->shdr[i];
    const Elf64_Rela *relocations;
    size_t relocation_count;

    if(section->sh_type != SHT_RELA) continue;
    if(section->sh_entsize != sizeof(Elf64_Rela) ||
       !range_valid((size_t)section->sh_offset, (size_t)section->sh_size,
                    view->size))
      return -1;
    relocations = (const Elf64_Rela *)(view->data + section->sh_offset);
    relocation_count = (size_t)section->sh_size / sizeof(Elf64_Rela);
    for(size_t j = 0; j < relocation_count; ++j) {
      if(relocations[j].r_offset >= protected_segment->p_vaddr &&
         relocations[j].r_offset - protected_segment->p_vaddr <
             protected_segment->p_filesz) {
        fprintf(stderr,
                "sign_elf: executable PT_LOAD contains runtime relocations\n");
        return -1;
      }
    }
  }
  for(size_t i = 0; i + CONFIG_RECORD_SIZE <= protected_segment->p_filesz; ++i) {
    unsigned char *candidate =
        view->data + protected_segment->p_offset + i;
    if(memcmp(candidate, CONFIG_MAGIC, 8) == 0) {
      if(config != NULL) return -1;
      config = candidate;
    }
  }
  if(config == NULL) return -1;
  *vaddr_out = protected_segment->p_vaddr;
  *size_out = protected_segment->p_filesz;
  memcpy(config + CONFIG_VADDR_OFFSET, vaddr_out, sizeof(*vaddr_out));
  memcpy(config + CONFIG_SIZE_OFFSET, size_out, sizeof(*size_out));
  return 0;
}

static int
write_file(const char *path, const unsigned char *data, size_t size) {
  FILE *file = fopen(path, "wb");
  int result = 0;

  if(file == NULL) return -1;
  if(fwrite(data, size, 1, file) != 1) result = -1;
  if(fclose(file) != 0) result = -1;
  return result;
}

int
main(int argc, char **argv) {
  unsigned char seed[32];
  unsigned char public_key[32];
  unsigned char expected_public_key[32];
  unsigned char private_key[64];
  unsigned char signature[64];
  unsigned char *data = NULL;
  unsigned char *message = NULL;
  size_t data_size = 0;
  size_t message_size = 0;
  uint64_t protected_vaddr = 0;
  uint64_t protected_size = 0;
  elf_view_t view;

  if(argc == 3 && strcmp(argv[1], "--public-key") == 0 &&
     decode_hex(argv[2], seed, sizeof(seed)) == 0) {
    ed25519_create_keypair(public_key, private_key, seed);
    print_hex(public_key, sizeof(public_key));
    return 0;
  }
  if(argc != 5 || decode_hex(argv[1], seed, sizeof(seed)) < 0 ||
     decode_hex(argv[4], expected_public_key, sizeof(expected_public_key)) < 0 ||
     read_file(argv[2], &data, &data_size) < 0 ||
     parse_elf(data, data_size, &view) < 0 ||
     patch_integrity_config(&view, &protected_vaddr, &protected_size) < 0 ||
     build_signing_message(&view, &message, &message_size) < 0 ||
     message_size != protected_size) {
    fprintf(stderr,
            "Usage: %s SEED_HEX INPUT_ELF OUTPUT_ELF PUBLIC_KEY_HEX\n"
            "       %s --public-key SEED_HEX\n",
            argv[0], argv[0]);
    free(message);
    free(data);
    return 2;
  }
  ed25519_create_keypair(public_key, private_key, seed);
  if(memcmp(public_key, expected_public_key, sizeof(public_key)) != 0) {
    fprintf(stderr, "sign_elf: seed and public key do not match\n");
    free(message);
    free(data);
    return 1;
  }
  ed25519_sign(signature, message, message_size, public_key, private_key);
  memcpy(data + view.signature_section->sh_offset + SIGNATURE_FIELD_OFFSET,
         signature, sizeof(signature));
  if(write_file(argv[3], data, data_size) < 0) {
    fprintf(stderr, "sign_elf: output failed: %s\n", strerror(errno));
    free(message);
    free(data);
    return 1;
  }
  free(message);
  free(data);
  return 0;
}

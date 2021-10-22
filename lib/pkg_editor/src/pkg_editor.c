// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/* Editor for Altera OpenCL package files
 *
 * Uses ELF format to store data. For more info on ELF, see "LibELF by example"
 * from http://mdsp.googlecode.com/files/libelf-by-example-20100112.pdf. You
 * can also just search for a specific function name to get the manpage.
 *
 * Underlying Libelf library is downloaded from
 *   http://www.mr511.de/software/libelf-0.8.13.tar.gz
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <linux/limits.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

// We support two implementations of the ELF library:
// 1. libelf by Michael Riepe <http://www.mr511.de/software/>
// 2. elfutils by Red Hat <https://sourceware.org/elfutils/>
#if HAVE_LIBELF_LIBELF
#include <libelf/libelf.h>
#else
#include <libelf.h>
#endif

#include "pkg_editor/pkg_editor.h"
#if USE_ZLIB
#include "zlib_interface.h"
#endif

typedef struct acl_pkg_file {
  const char *fname;
  int fd;
  Elf *elf;
  unsigned writable : 1;   /* Can we update this ELF? */
  unsigned dirty : 1;      /* Have changes been made? If yes, then require an
                              elf_update() call */
  unsigned show_error : 1; /* If true: Show errors on stderr */
  unsigned show_info : 1;  /* If true: Show info messages on stdout */
} acl_pkg_file;

typedef enum acl_pack_kind { PACK_FILE, PACK_DIR, PACK_END } acl_pack_kind;
#define PACK_MAGIC 0xBEEFFEEB
typedef struct acl_pkg_pack_info {
  int magic;            // Magic number to allow checking.
  acl_pack_kind kind;   // Directory/file/end
  int file_mode;        // file mode for files
  unsigned name_length; // Length of name in bytes including terminating NUL.
  unsigned file_length; // Length of file contents in bytes.
  // name (NUL terminated) (name_length bytes)
  // File contents (file_length bytes)
} acl_pkg_pack_info;

/*
 * Directories must precede any files in that directory.
 *   Directories have file_mode 0 and file_length 0.
 * PACK_END terminates the file
 */

#define strtabname ".shstrtab"
#define INITIAL_STRING_TABLE_SIZE 4096
#define MAX_SECTION_NAME_SIZE INITIAL_STRING_TABLE_SIZE

#define MODE_READ_ONLY(mode) (mode & ACL_PKG_READ)
#define MODE_READ_WRITE(mode) (mode & ACL_PKG_READ_WRITE)
#define MODE_CREATE(mode) (mode & ACL_PKG_CREATE)
#define SHOW_ERROR(mode) (!!(mode & ACL_PKG_SHOW_ERROR))
#define SHOW_INFO(mode) (!!(mode & ACL_PKG_SHOW_INFO))

// Check the package open mode flags for general use:
//  Read, read/write, read/write/create
// Return 0 if the mode flags were bad.
// Return 1 if the mode flags were ok.
static int l_check_mode(int mode) {
  const int show_error = SHOW_ERROR(mode);
  const int read_only = MODE_READ_ONLY(mode);
  const int read_write = MODE_READ_WRITE(mode);
  const int create_mode = MODE_CREATE(mode);

  // Check mode rules.
  if (!(read_only || read_write)) {
    if (show_error)
      fprintf(stderr,
              "Binary package open mode 0x%x must include either read-only or "
              "read-write\n",
              mode);
    return 0;
  }
  if (read_only && read_write) {
    if (show_error)
      fprintf(stderr,
              "Binary package open mode 0x%x cannot include both read-only and "
              "read-write\n",
              mode);
    return 0;
  }
  if (create_mode && !read_write) {
    if (show_error)
      fprintf(stderr, "Binary package creation flag can only be used with "
                      "read-write mode.\n");
    return 0;
  }

  // All is ok.
  return 1;
}

// Get an Elf_data handle for the data section of the shared string table.
// This is where we store section names.
static Elf_Data *get_name_data_ptr(const acl_pkg_file *pkg) {
  size_t result = (size_t)-1;
  Elf_Scn *str_scn;

  // Get index of shared string index section.
  if (elf_getshdrstrndx(pkg->elf, &result) < 0) {
    if (pkg->show_info)
      fprintf(stderr, "No string table section\n");
    return NULL;
  }
  // Get a section handle for it.
  str_scn = elf_getscn(pkg->elf, result);
  // Get the first data section in it.
  return elf_getdata(str_scn, NULL);
}

#ifdef DEBUG
static void dump_string_table(Elf_Data *data) {
  size_t idx = 1; // First char is always NUL.
  printf("Dumping string table at %p:\n", data->d_buf);
  while (idx < data->d_size) {
    char *str = (char *)(data->d_buf) + idx;
    printf(" %4d '%s'\n", (unsigned)idx, str);
    idx += strlen(str) + 1; // include the terminating NUL.
  }
}
#endif

// Ensure that the string table can be extended via realloc().
// The memory addressed by the data pointer returned by elg_getdata is
// not a heap object in its own right, and therefore cannot be reallocated.
// The string table data is therefore copied into a heap object, the address
// of which replaces the original pointer.
// Returns 0 on error, otherwise returns the size of the string table.
static Elf32_Word make_string_table_extensible(acl_pkg_file *pkg) {

  Elf_Data *data = get_name_data_ptr(pkg);
  void *new_string_table = NULL;

  if (!pkg->writable)
    return data->d_size;

  new_string_table = malloc(data->d_size);

  if (new_string_table == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " Failed to allocate %u bytes for string table",
              (unsigned)(data->d_size));
    return 0;
  }
#ifdef DEBUG
  fprintf(stderr,
          "make_string_table_extensible: replacing string table at %p with "
          "heap object %p\n",
          data->d_buf, new_string_table);
#endif
  memcpy(new_string_table, data->d_buf, data->d_size);
  data->d_buf = new_string_table;

  return data->d_size;
}

// Adds the given section name to the shared string table.
// Returns the index to the start of the newly added name, relative
// to the start of the data section.
// Just assign it to section_header->sh_name
static Elf32_Word add_section_name(acl_pkg_file *pkg, const char *sect_name,
                                   int show_error) {
  Elf_Data *data = get_name_data_ptr(pkg);
  Elf32_Word to_return = (Elf32_Word)data->d_size;
  const size_t num_extra_bytes =
      strlen(sect_name) + 1; // must account for terminating NUL
  const size_t new_string_table_size = data->d_size + num_extra_bytes;

  if (!pkg->writable)
    return 0;

  if (num_extra_bytes - 1 > MAX_SECTION_NAME_SIZE) {
    if (show_error) {
      fprintf(stderr,
              "Section name length %u exceeds limit of %d bytes. Section name "
              "is %s\n",
              (unsigned)num_extra_bytes - 1, MAX_SECTION_NAME_SIZE, sect_name);
    }
    return 0;
  }

#ifdef DEBUG
  printf("add_section_name: String table at %p is size %u\n", data->d_buf,
         (unsigned)data->d_size);
  dump_string_table(data);
#endif

#ifdef DEBUG
  printf("add_section_name: Writing %u bytes '%s' to %p\n",
         (unsigned)num_extra_bytes, sect_name,
         (char *)data->d_buf + data->d_size);
#endif

  data->d_buf = realloc(data->d_buf, new_string_table_size);
#ifdef DEBUG
  printf("add_section_name: Reallocated string table, updated address is %p\n",
         data->d_buf);
#endif
  if (data->d_buf == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " Failed to allocate %u bytes for string table",
              (unsigned)new_string_table_size);
    return 0;
  }
  strncpy((char *)data->d_buf + data->d_size, sect_name, num_extra_bytes);
  data->d_size = new_string_table_size;

#ifdef DEBUG
  printf("add_section_name: String table at %p is now size %u\n", data->d_buf,
         (unsigned)data->d_size);
  dump_string_table(data);
#endif

  pkg->dirty = 1;

  return to_return;
}

static Elf_Data *get_section_data(Elf_Scn *scn, int show_error) {

  Elf_Data *data = elf_getdata(scn, NULL);
  if (data == NULL) {
    if (show_error)
      fprintf(stderr, "No data for section.\n");
    return NULL;
  }
  if (elf_getdata(scn, data) != NULL) {
    if (show_error)
      fprintf(stderr, "Multiple data descriptors for section.\n");
    return NULL;
  }
  return data;
}

// Find section with given name. NULL if not found.
static Elf_Scn *get_section_by_name(const acl_pkg_file *pkg,
                                    const char *sect_name) {

  Elf_Data *data = get_name_data_ptr(pkg);
  Elf_Scn *next = NULL;
  if (sect_name == NULL)
    return NULL;

  while ((next = elf_nextscn(pkg->elf, next)) != NULL) {
    Elf32_Shdr *shdr = elf32_getshdr(next);
    if (shdr != NULL) {
      // Look up the section's name in the shared string table.
      char *cur_name = (char *)data->d_buf + (int)shdr->sh_name;
      if (strcmp(cur_name, sect_name) == 0) {
        return next;
      }
    }
  }
  // Didn't find matching name.
  return NULL;
}

// Flush ELF edits to file
// Return 0 on success to align with the glibc calls in acl_pkg_close_file()
static int flush_elf_edits(acl_pkg_file *pkg) {
  if (pkg->fd != -1) {
    if (pkg->show_info)
      printf("flush_elf_edits: Pkg fd is live\n");
    if (pkg->dirty) {
      if (pkg->show_info)
        printf("flush_elf_edits: Pkg is dirty\n");
      if (elf_update(pkg->elf, ELF_C_WRITE) < 0) {
        fprintf(stderr, " elf_update ()  failed : %s.", elf_errmsg(-1));
        return -1;
      } else {
        if (pkg->show_info)
          printf("flush_elf_edits: Pkg update is ok\n");
      }
    }
  }
  return 0;
}

int acl_pkg_section_names(const acl_pkg_file *pkg, char *buf, size_t max_len) {

  Elf_Data *data = get_name_data_ptr(pkg);
  Elf_Scn *next = NULL;
  size_t len = 0;
  char *temp = buf;

  while ((next = elf_nextscn(pkg->elf, next)) != NULL) {
    Elf32_Shdr *shdr = elf32_getshdr(next);
    if (shdr != NULL) {
      size_t cur_name_len;
      size_t new_len;

      // Look up the section's name in the shared string table.
      char *cur_name = (char *)data->d_buf + (int)shdr->sh_name;

      // skip two sections that are always there.
      // First one is name string table. The other -- I don't know.
      if (strcmp(cur_name, ".shstrtab") == 0)
        continue;
      if (strlen(cur_name) == 0)
        continue;

      // Will it fit?
      cur_name_len = strlen(cur_name);
      new_len = len + cur_name_len + 1; // +1 for \n
      if (new_len >= max_len) {
        return 0; // won't fit!
      }
      if (len != 0) {
        temp[0] = '\n';
        temp++;
        len++;
      }
      strncpy(temp, cur_name, cur_name_len + 1);
      temp[cur_name_len] = '\0';
      temp += cur_name_len;
      len += cur_name_len;
    }
  }
  return 1;
}

// List sections in the package file to stdout
int acl_pkg_list_file_sections(acl_pkg_file *pkg) {
  if (!pkg)
    return 0;

  {
    Elf_Data *data = get_name_data_ptr(pkg);
    Elf_Scn *next = NULL;
    printf("Sections in package file:\n");
    while ((next = elf_nextscn(pkg->elf, next)) != NULL) {
      Elf32_Shdr *shdr = elf32_getshdr(next);
      if (shdr != NULL) {
        char *cur_name = (char *)data->d_buf + (int)shdr->sh_name;
        // Don't need to show internal sections
        if (strcmp(cur_name, strtabname) != 0 && strcmp(cur_name, "") != 0) {
          Elf_Data *sdata = get_section_data(next, pkg->show_error);
          assert(sdata);
          printf("  %s, %zu bytes\n", cur_name, sdata->d_size);
        }
      }
    }
  }
  return 1;
}

// Add a new section with given name and content.
int acl_pkg_add_data_section(acl_pkg_file *pkg, const char *sect_name,
                             const void *content, size_t size) {

  Elf_Scn *scn;
  Elf_Data *data;
  Elf32_Shdr *shdr;
  Elf32_Word ret;

  if (!pkg->writable) {
    if (pkg->show_error)
      fprintf(stderr, "Cannot add section to read-only binary\n");
    return 0;
  }

  if (get_section_by_name(pkg, sect_name) != NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Section '%s' already exists.\n", sect_name);
    return 0;
  }

  if ((scn = elf_newscn(pkg->elf)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf_newscn ()  failed : %s.", elf_errmsg(-1));
    return 0;
  }
  if ((data = elf_newdata(scn)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf_newdata ()  failed : %s.", elf_errmsg(-1));
    return 0;
  }

  // Need 4 byte alignment for core rbf programming in kernel driver.
  // Go for at least 16 because it's easy to spot in "od".
  // Go for 128 because that's the minimum alignment for the largest OpenCL
  // data types, e.g. ulong16.
  data->d_align = ACL_PKG_MIN_SECTION_ALIGNMENT;

  data->d_off = 0LL;
  data->d_buf = (void *)content; // casts away constness
  data->d_type = ELF_T_BYTE;
  data->d_size = size;
  data->d_version = EV_CURRENT;

  pkg->dirty = 1;

  if ((shdr = elf32_getshdr(scn)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf32_getshdr ()  failed : %s.", elf_errmsg(-1));
  }

  assert(shdr);

  shdr->sh_type = SHT_PROGBITS; // semantics defined by the program.
  shdr->sh_flags = 0; // no writable during exection, not allocating space etc.
  shdr->sh_entsize = 0;

  ret = add_section_name(pkg, sect_name, pkg->show_error);
  if (ret == 0) {
    return 0;
  }

  shdr->sh_name = ret;

  if (pkg->show_info)
    printf("acl_pkg_add_data_section: Added section %s with %zu bytes\n",
           sect_name, size);
  return 1;
}

// Read full content of file into a buffer.
// The buffer is allocated by this function but must be freed by the caller.
// File length is returned in the second argument
void *acl_pkg_read_file_into_buffer(const char *in_file,
                                    size_t *file_size_out) {

  FILE *f = NULL;
  void *buf;
  size_t file_size;

  // When reading as binary file, no new-line translation is done.
  f = fopen(in_file, "rb");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open file %s for reading\n", in_file);
    return NULL;
  }

  // get file size
  fseek(f, 0, SEEK_END);
  file_size = ftell(f);
  rewind(f);

  // slurp the whole file into allocated buf
  buf = malloc(sizeof(char) * file_size);
  if (buf == NULL) {
    fprintf(stderr, "Could not allocated %zu bytes of memory\n", file_size);
    fclose(f);
    return NULL;
  }
  *file_size_out = fread(buf, sizeof(char), file_size, f);
  fclose(f);

  if (*file_size_out != file_size) {
    fprintf(stderr, "Error reading %s. Read only %zu out of %zu bytes\n",
            in_file, *file_size_out, file_size);
    free(buf);
    return NULL;
  }
  return buf;
}

int acl_pkg_add_data_section_from_file(acl_pkg_file *pkg, const char *sect_name,
                                       const char *in_file) {

  char *buf;
  size_t file_size;
  int result;

  if (!pkg->writable) {
    if (pkg->show_error)
      fprintf(stderr, "Cannot add section to read-only binary\n");
    return 0;
  }

  if (get_section_by_name(pkg, sect_name) != NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Section '%s' already exists.\n", sect_name);
    return 0;
  }

  buf = acl_pkg_read_file_into_buffer(in_file, &file_size);
  if (buf == NULL) {
    return 0;
  }

  // There is a bug in libelf.so.1 that causes a corrupted/misaligned
  // elf section in the case where a section is added with an input file of size
  // 0, if the previous section was of size 0 or [125-128] + 128*k where k is an
  // integer. This is a workaround to first create the section with a temporary
  // buffer of size one, and then update the section data to be empty
  if (file_size == 0) {
    if (pkg->show_info)
      printf("Empty input file. Creating section using dummy file and then "
             "updating it.\n");
    char *temp_buf = malloc(sizeof(char));
    result = acl_pkg_add_data_section(pkg, sect_name, temp_buf, 1);
    if (result) {
      if (flush_elf_edits(pkg) != 0)
        return 0;
      result = acl_pkg_update_section(pkg, sect_name, buf, file_size);
    }
    free(temp_buf);
  } else {
    result = acl_pkg_add_data_section(pkg, sect_name, buf, file_size);
  }

  if (result) {
    if (pkg->show_info)
      printf("Read %zu bytes from file %s and created new section '%s'.\n",
             file_size, in_file, sect_name);
  }

  return result;
}

// If the section does not exist, then return 0.
// Otherwise, return 1, and if size_ret is non-NULL, then store the section
// size (in bytes) into *size_ret. The size does NOT include NULL terminator.
int acl_pkg_section_exists(const struct acl_pkg_file *pkg,
                           const char *sect_name, size_t *size_ret) {

  Elf_Data *data;
  Elf_Scn *scn = get_section_by_name(pkg, sect_name);
  if (scn == NULL) {
    return 0;
  }
  data = get_section_data(scn, pkg->show_error);
  if (data == NULL) {
    return 0;
  }
  if (size_ret)
    *size_ret = data->d_size;
  return 1;
}

// Copy a pointer to the content of the given section into buf_ptr.
int acl_pkg_read_section_transient(const struct acl_pkg_file *pkg,
                                   const char *sect_name, char **buf_ptr) {

  Elf_Data *data;
  Elf_Scn *scn = get_section_by_name(pkg, sect_name);
  if (buf_ptr == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Buffer pointer argument is NULL\n");
    return 0;
  }
  if (scn == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Couldn't find section with name '%s'.\n", sect_name);
    return 0;
  }
  data = get_section_data(scn, pkg->show_error);
  if (data == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "No data for section '%s'.\n", sect_name);
    return 0;
  }

  *buf_ptr = data->d_buf;
  return 1;
}

// Put content of given section into pre-allocated buffer, upto len bytes,
// INCLUDING \0, which will be added at the end.
int acl_pkg_read_section(const struct acl_pkg_file *pkg, const char *sect_name,
                         char *buf, size_t len) {

  Elf_Data *data;
  Elf_Scn *scn = get_section_by_name(pkg, sect_name);
  if (buf == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Buffer argument is NULL\n");
    return 0;
  }
  if (scn == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Couldn't find section with name '%s'.\n", sect_name);
    return 0;
  }
  data = get_section_data(scn, pkg->show_error);
  if (data == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "No data for section '%s'.\n", sect_name);
    return 0;
  }
  if (data->d_size >= len) {
    if (pkg->show_error)
      fprintf(stderr, "Target buffer for section '%s' is too small.\n",
              sect_name);
    return 0;
  }

  memcpy(buf, data->d_buf, data->d_size);
  buf[data->d_size] = '\0';
  return 1;
}

// Extract content of section sect_name into a new out_file.
// If file already exists, over-write it.
// If no such section exists, do nothing.
int acl_pkg_read_section_into_file(acl_pkg_file *pkg, const char *sect_name,
                                   const char *out_file) {

  FILE *outf = NULL;
  size_t num_written;
  Elf_Data *data = NULL;
  Elf_Scn *scn = get_section_by_name(pkg, sect_name);
  if (scn == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Couldn't find section with name '%s'.\n", sect_name);
    return 0;
  }
  data = elf_getdata(scn, data);
  if (data == NULL) { // Must allow us to write data section with 0 bytes in it.
    if (pkg->show_error)
      fprintf(stderr, "No data for section '%s'.\n", sect_name);
    return 0;
  }
  if (elf_getdata(scn, data) != NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Multiple data descriptors for '%s'.\n", sect_name);
    return 0;
  }

  outf = fopen(out_file, "wb");
  if (outf == NULL) {
    if (pkg->show_error)
      perror("Couldn't open file for writing");
    return 0;
  }
  num_written = fwrite(data->d_buf, 1, data->d_size, outf);
  fclose(outf);
  if (num_written < data->d_size) {
    if (pkg->show_error)
      fprintf(stderr, "Wrote only %zu out of %zu bytes into %s for writing",
              num_written, data->d_size, out_file);
    return 0;
  }

  if (pkg->show_info)
    printf("Wrote %zu bytes from section '%s' to file %s\n", num_written,
           sect_name, out_file);
  return 1;
}

// Change content of an existing section to new_content of length new_len.
// Old content is discarded.
int acl_pkg_update_section(acl_pkg_file *pkg, const char *sect_name,
                           const void *new_content, size_t new_len) {

  Elf_Data *data;
  Elf_Scn *scn = get_section_by_name(pkg, sect_name);

  if (!pkg->writable) {
    if (pkg->show_error)
      fprintf(stderr, "Cannot add section to read-only binary\n");
    return 0;
  }

  if (scn == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Section '%s' does not exist.\n", sect_name);
    return 0;
  }
  data = get_section_data(scn, pkg->show_error);
  if (data == NULL) {
    return 0;
  }
  if (pkg->show_info)
    printf("acl_pkg_update_section: Updating section %s with %zu bytes of new "
           "data\n",
           sect_name, new_len);

  // Old data is just discarded
  data->d_buf = (void *)new_content; // cast away constness
  data->d_size = new_len;

  if (0 == elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY)) {
    // An error occurred
    if (pkg->show_error)
      fprintf(stderr, "Failed to mark section '%s' data as dirty.\n",
              sect_name);
    return 0;
  }

  pkg->dirty = 1;

  return 1;
}

int acl_pkg_update_section_from_file(acl_pkg_file *pkg, const char *sect_name,
                                     const char *in_file) {

  char *buf;
  size_t file_size;
  int result;

  if (!pkg->writable) {
    if (pkg->show_error)
      fprintf(stderr, "Cannot add section to read-only binary\n");
    return 0;
  }

  if (get_section_by_name(pkg, sect_name) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "Section '%s' does not exist.\n", sect_name);
    return 0;
  }

  buf = acl_pkg_read_file_into_buffer(in_file, &file_size);
  if (buf == NULL) {
    return 0;
  }

  result = acl_pkg_update_section(pkg, sect_name, buf, file_size);

  if (result) {
    if (pkg->show_info)
      printf("Read %zu bytes from file %s and updated section '%s'.\n",
             file_size, in_file, sect_name);
  }

  return result;
}

// Opposite of elf_getshdrstrndx().
// Set the e_shstrndx member of the given Elf by updating the Ehdr, and if
// applicable, by also updating section zero's extension field.
// Returns 1 on success.
static int set_shdrstrndx(Elf *elf, size_t shdrstrndx, Elf32_Ehdr *ehdr,
                          int show_error) {
  // Caller should have called elf_newscn at least once to create section 0
  Elf_Scn *scn_zero = elf_getscn(elf, 0);
  if (scn_zero == NULL) {
    if (show_error) {
      fprintf(stderr, " elf_getscn ()  failed : %s.", elf_errmsg(-1));
    }
    return 0;
  }
  Elf32_Shdr *scn_zero_hdr = elf32_getshdr(scn_zero);
  if (scn_zero_hdr == NULL) {
    if (show_error) {
      fprintf(stderr, " elf32_getshdr ()  failed : %s.", elf_errmsg(-1));
    }
    return 0;
  }

  if (shdrstrndx >= SHN_LORESERVE) {
    // Index is not found in section header table, instead it is in the extended
    // section table Store index in the sh_link of the section header of section
    // 0 NOTE: This if case is for handling theoretical scenario. In practice,
    // this situation is not very likely to happen. Currently, the parent
    // function add_required_parts will add one single section containing string
    // table for section names, so shdrstrndx will be 1, this number will grow
    // if more sections are added but still it is not likely that shdrstrndx
    // will be larger than SHN_LORESERVE (0xff00). See
    // https://man7.org/linux/man-pages/man5/elf.5.html for more information.
    scn_zero_hdr->sh_link = shdrstrndx;
    // Update elf header's e_shstrndx to indicate that index is stored in
    // extended section table
    ehdr->e_shstrndx = SHN_XINDEX;
  } else {
    scn_zero_hdr->sh_link = 0;
    ehdr->e_shstrndx = shdrstrndx;
  }
  elf_flagehdr(elf, ELF_C_SET, ELF_F_DIRTY);
  elf_flagshdr(scn_zero, ELF_C_SET, ELF_F_DIRTY);

  return 1;
}

// Add required headers and sections to new pkg file
static int add_required_parts(acl_pkg_file *pkg) {

  Elf32_Ehdr *ehdr;
  Elf_Scn *scn;
  Elf_Data *data;
  Elf32_Shdr *shdr;

  // Add required header and string section
  if ((ehdr = elf32_newehdr(pkg->elf)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, "elf_newehdr failed: %s\n", elf_errmsg(errno));
    return 0;
  }
  ehdr->e_ident[EI_DATA] = ELFDATA2LSB; // little-endian
  ehdr->e_machine = EM_X86_64;          // 64-bit cpu arch
  ehdr->e_type = ET_NONE;               // not an exe or a library

  // Add string table for section names.
  if ((scn = elf_newscn(pkg->elf)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf_newscn ()  failed : %s.", elf_errmsg(-1));
    return 0;
  }

  if ((data = elf_newdata(scn)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf_newdata ()  failed : %s.", elf_errmsg(-1));
    return 0;
  }

  {
    void *blankbuf = calloc(INITIAL_STRING_TABLE_SIZE, sizeof(char));
    if (blankbuf == NULL) {
      if (pkg->show_error)
        fprintf(stderr, " Failed to allocate %d bytes for string table",
                INITIAL_STRING_TABLE_SIZE);
      return 0;
    }
    // Elf_Data
    data->d_align = ACL_PKG_MIN_SECTION_ALIGNMENT;
    data->d_off = 0LL;
    data->d_buf = blankbuf;
    data->d_type = ELF_T_BYTE;
    data->d_version = EV_CURRENT;
    // See Elf spec, section on String Table.
    // "One references a string as an index into the string table section.
    // The first byte, which is index zero, is defined to hold a null
    // character. Likewise, a string table's last byte is defined to hold a
    // null character, ensuring null termination for all strings.
    // A string whose index is zero specifies either no name or a null name,
    // depending on the context.  An empty string table is permitted; its
    // section header's sh_size member would contain zero.  Non-zero indices
    // are invalid for an empty string table.
    //
    // A section header's sh_name member holds an index into the section
    // header string table section, as designated by the e_shstrndx member of
    // the ELF header."
    strncpy((char *)data->d_buf + 1, strtabname,
            INITIAL_STRING_TABLE_SIZE -
                1); // +1 to reserve space for initial NUL.
    data->d_size =
        strlen(strtabname) + 2; // +2 to count initial and terminating NUL.
  }

  if ((shdr = elf32_getshdr(scn)) == NULL) {
    if (pkg->show_error)
      fprintf(stderr, " elf32_getshdr ()  failed : %s.", elf_errmsg(-1));
    return 0;
  }
  shdr->sh_name = 1; // index 1 in the string table.
  shdr->sh_type = SHT_STRTAB;
  shdr->sh_flags =
      // SHF_ALLOC | SHF_STRINGS;  // Why SHF_ALLOC?  Does not occupy space
      // during runtime.
      SHF_STRINGS;
  shdr->sh_entsize = 0;

  // Set index of section name string table (e_shstrndx)
  if (!set_shdrstrndx(pkg->elf, elf_ndxscn(scn), ehdr, pkg->show_error)) {
    if (pkg->show_error) {
      fprintf(stderr, " Failed to set e_shstrndx\n");
    }
    return 0;
  }

  acl_pkg_add_data_section(pkg, "", calloc(MAX_SECTION_NAME_SIZE, 1),
                           MAX_SECTION_NAME_SIZE);

  pkg->dirty = 1;

  return 1;
}

// Open an existing package file or create a new one.
// if mode is ACL_PKG_CREATE, creates a new file. Otherwise, opens an existing
// one.
acl_pkg_file *acl_pkg_open_file(const char *fname, int mode) {

  int fd = -1;
  Elf *e = NULL;
  int perm, open_mode, elf_begin_mode;
  acl_pkg_file *result = NULL;

  const int read_only = MODE_READ_ONLY(mode);
  const int create_mode = MODE_CREATE(mode);
  const int show_error = SHOW_ERROR(mode);
  const int show_info = SHOW_INFO(mode);

  if (!l_check_mode(mode)) {
    // Already issued message, if requested.
    return result;
  }

#ifdef DEBUG
  fprintf(stderr, "acl_pkg_open_file: Opening file %s in mode 0x%x\n", fname,
          mode);
#endif

// File permissions:  These are only significant when creating the file.
// (If the file already exits, these will be ignored.)
// By default, make the file readable and writable by everybody
// But that's overridden by the process's umask.  So the user always
// has enough control to turn off other-write, for example.
// he O_CREAT description in "man 2 open".
#ifdef _WIN32
  if (read_only) {
    perm = perm = _S_IREAD;
    open_mode = O_RDONLY | O_BINARY;
  } else {
    perm = _S_IREAD | _S_IWRITE;
    open_mode = O_RDWR | O_BINARY;
  }
#else
  if (read_only) {
    perm = 0; // Not significant anyway.
    open_mode = O_RDONLY;
  } else {
    perm = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    open_mode = O_RDWR;
  }
#endif
  elf_begin_mode = read_only ? ELF_C_READ : ELF_C_RDWR;
  if (create_mode) {
    open_mode |= O_CREAT;
    elf_begin_mode = ELF_C_WRITE;
  }

  if (fname == NULL) {
    if (show_error)
      fprintf(stderr, "No filename specified\n");
    return NULL;
  }
  if (elf_version(EV_CURRENT) == EV_NONE) {
    if (show_error)
      fprintf(stderr, "elf_version failed: %s\n", elf_errmsg(errno));
    return result;
  }

  if ((fd = open(fname, open_mode, perm)) < 0) {
    if (show_error)
      fprintf(stderr, "Couldn't open %s: %s\n", fname, strerror(errno));
    return result;
  }

  result = malloc(sizeof(acl_pkg_file));
  if (result == NULL) {
    if (show_error)
      fprintf(stderr, "Failed to allocate %d bytes of memory\n",
              (int)sizeof(acl_pkg_file));
    close(fd);
    return NULL;
  }
  result->fname = fname;
  result->fd = fd;
  result->elf = NULL;
  result->writable = !(read_only);
  result->dirty = 0;
  result->show_error = show_error;
  result->show_info = show_info;

  if ((e = elf_begin(fd, elf_begin_mode, NULL)) == NULL) {
    if (show_error)
      fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(errno));
    acl_pkg_close_file(result);
    return NULL;
  }
  result->elf = e;

  if (create_mode) {
    if (!add_required_parts(result)) {
      acl_pkg_close_file(result);
      return NULL;
    }
  } else if (!read_only) {
    if (!make_string_table_extensible(result)) {
      acl_pkg_close_file(result);
      return NULL;
    }
  }
  return result;
}

/* Open ELF in memory */
acl_pkg_file *acl_pkg_open_file_from_memory(char *pkg_image,
                                            size_t pkg_image_size, int mode) {

  acl_pkg_file *result = NULL;
  Elf *e = NULL;
  const int show_error = SHOW_ERROR(mode);
  const int show_info = SHOW_INFO(mode);

  if (mode & ~(ACL_PKG_SHOW_INFO | ACL_PKG_SHOW_ERROR)) {
    if (show_error)
      fprintf(stderr, "Invalid mode %d for opening a binary in memory\n", mode);
    return result;
  }

  if (pkg_image == NULL || pkg_image_size == 0) {
    return result;
  }

  if (elf_version(EV_CURRENT) == EV_NONE) {
    if (show_error)
      fprintf(stderr, "elf_version failed: %s\n", elf_errmsg(errno));
    return result;
  }

  if ((e = elf_memory(pkg_image, pkg_image_size)) == NULL) {
    if (show_error)
      fprintf(stderr, "elf_memory failed: %s\n", elf_errmsg(errno));
    return result;
  }

  result = malloc(sizeof(acl_pkg_file));
  if (result == NULL) {
    if (show_error)
      fprintf(stderr, "Failed to allocated %d bytes of memory\n",
              (int)sizeof(acl_pkg_file));
    return NULL;
  }
  result->fname = NULL;
  result->fd = -1;
  result->elf = e;
  result->writable = 0; // When opening from memory, it's always read-only.
  result->dirty = 0;
  result->show_info = show_info;
  result->show_error = show_error;

  return result;
}

// Close pkg file.
// This call flushes all the ELF edits, closes the actual file, and frees the
// pkg ptr.
int acl_pkg_close_file(acl_pkg_file *pkg) {
  if (!pkg)
    return 0;

  int status = 0;

  status = flush_elf_edits(pkg);

  // Should be albe to call elf_end even if elf_update failed
  while (elf_end(pkg->elf)) {
    if (pkg->show_info)
      printf("acl_pkg_close_file: elf_end returned non-zero.  Do it again.\n");
  }

  if (pkg->fd != -1) {
    if (pkg->dirty) {
      // Need to ensure data gets out to disk.
      // Failure mode: One machine generating data on NFS, other machine
      // looking at data.
#ifdef _WIN32
      status = _commit(pkg->fd); // 0 on success, otherwise  -1, with errno set.
#else
      status = fsync(pkg->fd); // 0 on success, otherwise  -1, with errno set.
#endif
      if (status != 0) {
        if (pkg->show_error)
          fprintf(stderr,
                  "acl_pkg_close_file: Failed to flush data on %s: %s\n",
                  pkg->fname, strerror(errno));
      }
    }
    status = close(pkg->fd);
    if (status != 0) {
      if (pkg->show_error)
        fprintf(stderr, "acl_pkg_close_file: Failed to close %s: %s\n",
                pkg->fname, strerror(errno));
    }
  }
  free(pkg);
  return !status;
}

void acl_pkg_set_show_mode(acl_pkg_file *pkg, int show_mode) {
  if (pkg) {
    const int show_info = !!(show_mode & ACL_PKG_SHOW_INFO);
    const int show_error = !!(show_mode & ACL_PKG_SHOW_ERROR);
    pkg->show_info = show_info;
    pkg->show_error = show_error;
  }
}

#if USE_ZLIB

typedef struct ZInfo {
  z_stream strm;
  unsigned char buffer[32 * 1024];
} ZInfo;

static acl_pack_kind add_file_or_dir(const char *out_file, FILE *of,
                                     const char *file_or_dir, ZInfo *z_fino);

// Return 'true' if the data was written successfully.
static int append_data(const void *data, size_t size, ZInfo *z_info, FILE *of,
                       int finish) {
  // Write this data, collecting the output into z_info->buffer.  Flush to disk
  // if needed.
  z_info->strm.next_in = (unsigned char *)data;
  z_info->strm.avail_in = size;

  // Continue deflating until we are finished with the input.  There may be left
  // over output that we will handle later.
  if (finish) {
    // We have to ensure that we get ALL the output.
    // Flush anything left over in the current output buffer.
    size_t output_size = sizeof(z_info->buffer) - z_info->strm.avail_out;
    if (output_size > 0) {
      if (fwrite(z_info->buffer, output_size, 1, of) != 1) {
        return 0;
      }
    }
    // Continue until there is nothing left to output.
    do {
      int ret;
      z_info->strm.avail_out = sizeof(z_info->buffer);
      z_info->strm.next_out = z_info->buffer;
      ret = zlib_deflate(&z_info->strm, Z_FINISH);
      assert(ret != Z_STREAM_ERROR);
      output_size = sizeof(z_info->buffer) - z_info->strm.avail_out;
      if (output_size > 0) {
        if (fwrite(z_info->buffer, output_size, 1, of) != 1) {
          return 0;
        }
      }
    } while (z_info->strm.avail_out == 0);
  } else {
    // Only dump the output buffer when it is full.
    do {
      int ret = zlib_deflate(&z_info->strm, Z_NO_FLUSH);
      assert(ret != Z_STREAM_ERROR);
      if (z_info->strm.avail_out == 0) {
        if (fwrite(z_info->buffer, sizeof(z_info->buffer), 1, of) != 1) {
          return 0;
        }
        // Start a fresh output buffer.
        z_info->strm.avail_out = sizeof(z_info->buffer);
        z_info->strm.next_out = z_info->buffer;
      }
    } while (z_info->strm.avail_in > 0);
  }
  assert(z_info->strm.avail_in == 0);

  return 1;
}

static acl_pack_kind add_file(const char *out_file, FILE *of, const char *file,
                              unsigned mode, unsigned size, ZInfo *z_info) {
  acl_pkg_pack_info info;
  size_t name_length = strlen(file) + 1;
  FILE *in_file = fopen(file, "rb");
  if (in_file == NULL) {
    fprintf(stderr, "acl_pkg_pack: Unable to open %s for reading: %s\n", file,
            strerror(errno));
    return PACK_END;
  }

  info.magic = PACK_MAGIC;
  info.kind = PACK_FILE;
  info.file_mode = mode;
  info.name_length = name_length;
  info.file_length = size;

  // File header
  if (!append_data(&info, sizeof(info), z_info, of, 0)) {
    fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
            strerror(errno));
    fclose(in_file);
    return PACK_END;
  }

  // File name
  if (!append_data(file, name_length, z_info, of, 0)) {
    fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
            strerror(errno));
    fclose(in_file);
    return PACK_END;
  }

  // File data
  if (size > 0) {
    char buf[64 * 1024];
    if (size <= sizeof(buf)) {
      // Just use buf and skip the memory allocation.
      if (fread(buf, size, 1, in_file) != 1) {
        fprintf(stderr, "acl_pkg_pack: Failed to read file %s: %s\n", file,
                strerror(errno));
        fclose(in_file);
        return PACK_END;
      }
      if (!append_data(buf, size, z_info, of, 0)) {
        fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
                strerror(errno));
        fclose(in_file);
        return PACK_END;
      }
    } else {
      char *buffer = malloc(size);
      if (buffer == NULL) {
        fprintf(stderr,
                "acl_pkg_pack: Failed to allocate buffer to read %s: %s\n",
                file, strerror(errno));
        fclose(in_file);
        return PACK_END;
      }
      if (fread(buffer, size, 1, in_file) != 1) {
        fprintf(stderr, "acl_pkg_pack: Failed to read file %s: %s\n", file,
                strerror(errno));
        fclose(in_file);
        free(buffer);
        return PACK_END;
      }
      if (!append_data(buffer, size, z_info, of, 0)) {
        fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
                strerror(errno));
        fclose(in_file);
        free(buffer);
        return PACK_END;
      }
      free(buffer);
    }
  }

  fclose(in_file);
  // Success
  return PACK_FILE;
}

static acl_pack_kind add_directory(const char *out_file, FILE *of,
                                   const char *dir_name, ZInfo *z_info) {
#ifdef FULL_NAME_LENGTH
#undef FULL_NAME_LENGTH
#endif
  acl_pkg_pack_info info;
  size_t name_length = strlen(dir_name) + 1;

  info.magic = PACK_MAGIC;
  info.kind = PACK_DIR;
  info.file_mode = 0;
  info.name_length = name_length;
  info.file_length = 0;

  // File header
  if (!append_data(&info, sizeof(info), z_info, of, 0)) {
    fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
            strerror(errno));
    return PACK_END;
  }

  // Directory name
  if (!append_data(dir_name, name_length, z_info, of, 0)) {
    fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
            strerror(errno));
    return PACK_END;
  }

  // Now walk the directory processing each name.
  {
#ifdef _WIN32
#define FULL_NAME_LENGTH (2 * MAX_PATH)
    char full_name[FULL_NAME_LENGTH];
    if (FULL_NAME_LENGTH < name_length) {
      fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
              "Directory name too long");
      return PACK_END;
    }
    HANDLE file_handle;
    WIN32_FIND_DATA file_info;

    // Partially initialize the full path name.
    strncpy(full_name, dir_name, FULL_NAME_LENGTH);
    strncpy(full_name + name_length - 1, "\\*.*",
            FULL_NAME_LENGTH - name_length + 1);
    if (full_name[FULL_NAME_LENGTH - 1] != '\0') {
      full_name[FULL_NAME_LENGTH - 1] = '\0';
    }

    // Walk through all the files in the directory.
    file_handle = FindFirstFile(full_name, &file_info);
    if (file_handle != INVALID_HANDLE_VALUE) {
      do {
        // Ignore the special cases.
        if (strcmp(file_info.cFileName, ".") == 0 ||
            strcmp(file_info.cFileName, "..") == 0) {
          continue;
        }

        // Finish the full file name
        strncpy(full_name + name_length, file_info.cFileName,
                FULL_NAME_LENGTH - name_length);
        if (full_name[FULL_NAME_LENGTH - 1] != '\0') {
          full_name[FULL_NAME_LENGTH - 1] = '\0';
        }
        if (add_file_or_dir(out_file, of, full_name, z_info) == PACK_END) {
          FindClose(file_handle);
          return PACK_END;
        }
      } while (FindNextFile(file_handle, &file_info));
    }
#else
    // Linux
    DIR *dir;
    struct dirent *entry;
#define FULL_NAME_LENGTH (2 * PATH_MAX)
    char full_name[FULL_NAME_LENGTH];
    if (FULL_NAME_LENGTH < name_length) {
      fprintf(stderr, "acl_pkg_pack: Failed to write to %s: %s\n", out_file,
              "Directory name too long");
      return PACK_END;
    }

    // Partially initialize the full path name.
    strncpy(full_name, dir_name, FULL_NAME_LENGTH);

    if (full_name[FULL_NAME_LENGTH - 1] != '\0') {
      full_name[FULL_NAME_LENGTH - 1] = '\0';
    }

    full_name[name_length - 1] = '/';

    dir = opendir(dir_name);
    if (dir == NULL) {
      fprintf(stderr, "acl_pkg_pack: Unable to read directory %s: %s\n",
              dir_name, strerror(errno));
      return PACK_END;
    }
    entry = readdir(dir);
    while (entry) {
      // Only process non ./.. entries
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {

        // Finish the full file name, truncate name if the file is too long to
        // avoid buffer overflow
        size_t buffer_space_left = FULL_NAME_LENGTH - name_length;
        strncpy(full_name + name_length, entry->d_name, buffer_space_left);
        if (full_name[FULL_NAME_LENGTH - 1] != '\0') {
          full_name[FULL_NAME_LENGTH - 1] = '\0';
        }
        if (add_file_or_dir(out_file, of, full_name, z_info) == PACK_END) {
          closedir(dir);
          return PACK_END;
        }
      }
      entry = readdir(dir);
    }
    closedir(dir);
#endif
  }
  return PACK_DIR;
}

static acl_pack_kind add_file_or_dir(const char *out_file, FILE *of,
                                     const char *file_or_dir, ZInfo *z_info) {
#ifdef _WIN32
  struct _stat64i32 buf;
  int status = _stat(file_or_dir, &buf);
#undef S_ISDIR
#define S_ISDIR(mode) (!!(_S_IFDIR & mode) && !(_S_IFREG & mode))
#undef S_ISREG
#define S_ISREG(mode) (!!(_S_IFREG & mode) && !(_S_IFDIR & mode))
#else
  // Linux
  struct stat buf;
  int status = stat(file_or_dir, &buf);
#endif
  if (status != 0) {
    fprintf(stderr, "acl_pkg_pack: Unable to stat %s: %s\n", file_or_dir,
            strerror(errno));
    return PACK_END;
  }
  if (S_ISDIR(buf.st_mode)) {
    return add_directory(out_file, of, file_or_dir, z_info);
  } else if (S_ISREG(buf.st_mode)) {
    return add_file(out_file, of, file_or_dir, buf.st_mode, buf.st_size,
                    z_info);
  }
  // If we got here, this wasn't a file or directory.
  fprintf(stderr, "acl_pkg_pack: File %s is not a file or directory\n",
          file_or_dir);
  return PACK_END;
}

int acl_pkg_pack(const char *out_file, const char **input_files_dirs) {
  acl_pkg_pack_info info;
  ZInfo z_info;
  int ret;

  // Recursively pack all the input files to the output file.
  // Compression will be added later.
  FILE *of = fopen(out_file, "wb");
  if (of == NULL) {
    fprintf(stderr, "acl_pkg_pack: Unable to open %s for writing: %s\n",
            out_file, strerror(errno));
    return 0;
  }

  // Initialize zlib.
  z_info.strm.zalloc = Z_NULL;
  z_info.strm.zfree = Z_NULL;
  z_info.strm.opaque = Z_NULL;
  z_info.strm.avail_out = sizeof(z_info.buffer);
  z_info.strm.next_out = z_info.buffer;
  ret = zlib_deflateInit(&z_info.strm, Z_BEST_COMPRESSION);
  if (ret != Z_OK) {
    fprintf(stderr, "acl_pkg_pack: Unable to initialize zlib for writing %s\n",
            out_file);
    fclose(of);
    return 0;
  }

  // Iterate through the files and add them to the output.
  while (*input_files_dirs != NULL) {
    acl_pack_kind result =
        add_file_or_dir(out_file, of, *input_files_dirs, &z_info);
    if (result == PACK_END) {
      // We had a failure; stop here.
      fclose(of);
      zlib_deflateEnd(&z_info.strm);
      return 0;
    }
    input_files_dirs++;
  }

  // All done.  Write end record
  memset(&info, '\0', sizeof(info));
  info.magic = PACK_MAGIC;
  info.kind = PACK_END;
  append_data(&info, sizeof(info), &z_info, of, 1);
  if (fclose(of) != 0) {
    fprintf(stderr, "acl_pkg_pack: Write of %s failed: %s\n", out_file,
            strerror(errno));
    zlib_deflateEnd(&z_info.strm);
    return 0;
  }
  zlib_deflateEnd(&z_info.strm);
  return 1 /* success */;
}

static int read_data(void *data, size_t size, ZInfo *z_info, FILE *in_fd) {
  // We want to fill 'data' with 'size' bytes.
  z_info->strm.next_out = data;
  z_info->strm.avail_out = size;
  do {
    int ret, count;
    if (z_info->strm.avail_in == 0) {
      // We ran out of the last chunk of input data.  Grab some more (if reading
      // from a file).
      if (in_fd == NULL || feof(in_fd)) {
        // Nothing left to read!
        return 0;
      }
      count = fread(z_info->buffer, 1, sizeof(z_info->buffer), in_fd);
      if (count < 1) {
        // Failed to read the file.
        return 0;
      }
      z_info->strm.avail_in = count;
      z_info->strm.next_in = z_info->buffer;
    }
    // Grab the next chunk of data from the input buffer.
    ret = zlib_inflate(&z_info->strm, Z_NO_FLUSH);
    assert(ret != Z_STREAM_ERROR);
    if (ret == Z_STREAM_END) {
      // Last bit of data.
      return z_info->strm.avail_out == 0 ? 1 : 0;
    }
    if (ret != Z_OK) {
      return 0;
    }
    if (z_info->strm.avail_out == 0) {
      // We read the whole thing.
      return 1;
    }
  } while (z_info->strm.avail_in == 0);
  return 1;
}

static int acl_pkg_unpack_buffer_or_file(const char *buffer, size_t buffer_size,
                                         FILE *input, const char *out_dir,
                                         const char *routine_name) {
#ifdef FULL_NAME_LEN
#undef FULL_NAME_LEN
#endif
#ifdef NAME_LEN
#undef NAME_LEN
#endif
#ifdef _WIN32
#define FULL_NAME_LEN (3 * MAX_PATH)
#define NAME_LEN (2 * MAX_PATH)
#else
#define FULL_NAME_LEN (3 * PATH_MAX)
#define NAME_LEN (2 * PATH_MAX)
#endif

  size_t out_dir_length = strlen(out_dir);
  char full_name[FULL_NAME_LEN];
  char name[NAME_LEN];
  ZInfo z_info;
  int ret;

  strncpy(full_name, out_dir, FULL_NAME_LEN);
  if (full_name[FULL_NAME_LEN - 1] != '\0') {
    full_name[FULL_NAME_LEN - 1] = '\0';
  }

  // Initialize zlib.
  z_info.strm.zalloc = Z_NULL;
  z_info.strm.zfree = Z_NULL;
  z_info.strm.opaque = Z_NULL;
  if (buffer != NULL) {
    assert(input == NULL);
    z_info.strm.avail_in = buffer_size;
    z_info.strm.next_in = (unsigned char *)buffer;
  } else {
    assert(input != NULL);
    z_info.strm.avail_in = 0;
    z_info.strm.next_in = NULL;
  }
  ret = zlib_inflateInit(&z_info.strm);
  if (ret != Z_OK) {
    fprintf(stderr, "%s: Unable to initialize zlib for reading from buffer\n",
            routine_name);
    return 0;
  }

  // Create output directory (ignore any errors).
#ifdef _WIN32
  CreateDirectory(full_name, NULL);
#else
  mkdir(full_name, 0755);
#endif
  full_name[out_dir_length] = '/';

  // Process the file until we hit the PACK_END record (or finish the
  // buffer/file).
  while (z_info.strm.avail_in > 0 || (input != NULL && !feof(input))) {
    acl_pkg_pack_info info;
    if (!read_data(&info, sizeof(info), &z_info, input)) {
      fprintf(stderr, "%s: Error reading from buffer\n", routine_name);
      zlib_inflateEnd(&z_info.strm);
      return 0;
    }
    if (info.magic != PACK_MAGIC) {
      fprintf(stderr, "%s: Incorrect magic number read from buffer\n",
              routine_name);
      zlib_inflateEnd(&z_info.strm);
      return 0;
    }

    // Are we all done?
    if (info.kind == PACK_END) {
      break;
    }

    // Read the filename.
    if (!read_data(name, info.name_length, &z_info, input)) {
      fprintf(stderr, "%s: Error reading file name from buffer\n",
              routine_name);
      zlib_inflateEnd(&z_info.strm);
      return 0;
    }

    // Generate the full name, truncate or zero pad to avoid buffer overflow
    if (FULL_NAME_LEN < out_dir_length) {
      fprintf(stderr, "%s: Directory name too long\n", routine_name);
    }
    strncpy(full_name + out_dir_length + 1, name,
            FULL_NAME_LEN - out_dir_length - 1);
    if (full_name[FULL_NAME_LEN - 1] != '\0') {
      full_name[FULL_NAME_LEN - 1] = '\0';
    }

    if (info.kind == PACK_DIR) {
#ifdef _WIN32
      CreateDirectory(full_name, NULL);
#else
      mkdir(full_name, 0755);
#endif
    } else {
      // Read file contents
      FILE *out_file = fopen(full_name, "wb");
      if (out_file == NULL) {
        fprintf(stderr, "%s: Unable to open %s for writing: %s\n", routine_name,
                full_name, strerror(errno));
        zlib_inflateEnd(&z_info.strm);
        return 0;
      }
      if (info.file_length > 0) {
        char buf[64 * 1024];
        if (info.file_length < sizeof(buf)) {
          if (!read_data(buf, info.file_length, &z_info, input)) {
            fprintf(stderr, "%s: Error reading file data for %s from buffer\n",
                    routine_name, full_name);
            fclose(out_file);
            zlib_inflateEnd(&z_info.strm);
            return 0;
          }
          if (fwrite(buf, info.file_length, 1, out_file) != 1) {
            fprintf(stderr, "%s: Failed to write to %s: %s\n", routine_name,
                    full_name, strerror(errno));
            fclose(out_file);
            zlib_inflateEnd(&z_info.strm);
            return 0;
          }
        } else {
          char *buf2 = malloc(info.file_length);
          if (buf2 == NULL) {
            fprintf(stderr, "%s: Failed to allocate buffer to write %s: %s\n",
                    routine_name, full_name, strerror(errno));
            fclose(out_file);
            free(buf2);
            zlib_inflateEnd(&z_info.strm);
            return PACK_END;
          }
          if (!read_data(buf2, info.file_length, &z_info, input)) {
            fprintf(stderr, "%s: Error reading file data for %s from buffer\n",
                    routine_name, full_name);
            fclose(out_file);
            free(buf2);
            zlib_inflateEnd(&z_info.strm);
            return 0;
          }
          if (fwrite(buf2, info.file_length, 1, out_file) != 1) {
            fprintf(stderr, "%s: Failed to write to %s: %s\n", routine_name,
                    full_name, strerror(errno));
            fclose(out_file);
            free(buf2);
            zlib_inflateEnd(&z_info.strm);
            return 0;
          }
          free(buf2);
        }
      }
      fclose(out_file);
    }
  }

  zlib_inflateEnd(&z_info.strm);
  return 1;
}

int acl_pkg_unpack_buffer(const char *buffer, size_t buffer_size,
                          const char *out_dir) {
  return acl_pkg_unpack_buffer_or_file(buffer, buffer_size, NULL, out_dir,
                                       "acl_pkg_unpack_buffer");
}

int acl_pkg_unpack(const char *in_file, const char *out_dir) {
  FILE *input;
  int is_stdin = 0;
  int ret;

  if (strcmp(in_file, ACL_PKG_UNPACKAGE_STDIN) == 0) {
    input = stdin;
    is_stdin = 1;
  } else {
    input = fopen(in_file, "rb");
    if (input == NULL) {
      fprintf(stderr, "acl_pkg_unpack: Unable to open %s for reading: %s\n",
              in_file, strerror(errno));
      return 0;
    }
  }

  ret =
      acl_pkg_unpack_buffer_or_file(NULL, 0, input, out_dir, "acl_pkg_unpack");
  if (!is_stdin) {
    fclose(input);
  }
  return ret;
}

#else // USE_ZLIB

int acl_pkg_pack(const char *out_file, const char **input_files_dirs) {
  // Not implemented if no ZLIB
  return 0;
}

int acl_pkg_unpack(const char *in_file, const char *out_dir) {
  // Not implemented if no ZLIB
  return 0;
}

int acl_pkg_unpack_buffer(const char *buffer, size_t buffer_size,
                          const char *out_dir) {
  // Not implemented if no ZLIB
  return 0;
}

#endif // USE_ZLIB

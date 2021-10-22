// Copyright (C) 2017-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/* Interface to Zlib DLL
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <linux/limits.h>
#include <unistd.h>
#endif

#include "zlib_interface.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef int (*zlib_stream)(z_streamp);
typedef int (*zlib_stream_int)(z_streamp, int);
typedef struct ZlibInterface {
  int (*deflateInit_)(z_streamp, int, const char *version, int stream_size);
  zlib_stream_int deflate;
  zlib_stream deflateEnd;
  int (*inflateInit_)(z_streamp, const char *version, int stream_size);
  zlib_stream_int inflate;
  zlib_stream inflateEnd;
} ZlibInterface;

static ZlibInterface zlib_interface;

static void *load_one_symbol(void *library, const char *function_name) {
  void *symbol;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4152)
  symbol = GetProcAddress((HMODULE)library, function_name);
#pragma warning(pop)
#else
  symbol = dlsym(library, function_name);
  if (symbol == NULL) {
    fprintf(stderr, "Unable to find dynamic symbol %s: %s\n", function_name,
            dlerror());
    exit(1);
  }
#endif
  return symbol;
}

#ifdef _WIN32
// Retrieve zlib path from INTELFPGAOCLSDKROOT
// The caller must free the returned buffer after use!
static char *zlib_get_path_fpga_root(const char *acl_root_dir) {
  const char *const zlib_rel_path = "\\windows64\\bin\\zlib1.dll";
  char *const zlib_path =
      malloc(strlen(acl_root_dir) + strlen(zlib_rel_path) + 1);
  if (!zlib_path) {
    fprintf(stderr, "Failed to allocate memory for the zlib path!\n");
    exit(1);
  }
  strcpy(zlib_path, acl_root_dir);
  strcat(zlib_path, zlib_rel_path);
  return zlib_path;
}
#endif

// Dynamically loads zlib library and returns handle on success, or `NULL` on
// failure.
static void *zlib_load_library() {
#ifdef _WIN32
  const char *const acl_root_dir = getenv("INTELFPGAOCLSDKROOT");
  if (acl_root_dir) {
    char *const zlib_path = zlib_get_path_fpga_root(acl_root_dir);
    void *const library = (void *)LoadLibraryA(zlib_path);
    free(zlib_path);
    return library;
  }
  // Standalone runtime, INTELFPGAOCLSDKROOT is not set.
  void *const library = (void *)LoadLibraryA(WINDOWS_ZLIB_PATH);
  return library;
#else
  return dlopen("libz.so", RTLD_NOW);
#endif
}

static void zlib_load() {
  if (zlib_interface.deflateInit_) {
    return; // library is already loaded
  }
  void *const library = zlib_load_library();
  if (library == NULL) {
    fprintf(stderr, "Unable to open zlib library!\n");
    exit(1);
  }
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4152)
#endif
  zlib_interface.deflateInit_ = load_one_symbol(library, "deflateInit_");
  zlib_interface.deflate = load_one_symbol(library, "deflate");
  zlib_interface.deflateEnd = load_one_symbol(library, "deflateEnd");
  zlib_interface.inflateInit_ = load_one_symbol(library, "inflateInit_");
  zlib_interface.inflate = load_one_symbol(library, "inflate");
  zlib_interface.inflateEnd = load_one_symbol(library, "inflateEnd");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

int zlib_deflateInit(z_streamp strm, int level) {
  zlib_load();
  return (*zlib_interface.deflateInit_)(strm, level, ZLIB_VERSION,
                                        (int)sizeof(z_stream));
}

int zlib_deflate(z_streamp strm, int flush) {
  return (*zlib_interface.deflate)(strm, flush);
}

int zlib_deflateEnd(z_streamp strm) {
  return (*zlib_interface.deflateEnd)(strm);
}

int zlib_inflateInit(z_streamp strm) {
  zlib_load();
  return (*zlib_interface.inflateInit_)(strm, ZLIB_VERSION,
                                        (int)sizeof(z_stream));
}

int zlib_inflate(z_streamp strm, int flush) {
  return (*zlib_interface.inflate)(strm, flush);
}
int zlib_inflateEnd(z_streamp strm) {
  return (*zlib_interface.inflateEnd)(strm);
}

#ifdef __cplusplus
}
#endif

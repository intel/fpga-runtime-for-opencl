// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_SUPPORT_H
#define ACL_SUPPORT_H

// Support API to the underlying operating environment.

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include <CL/opencl.h>

#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

extern int l_malloc_enable;

const char *acl_getenv(const char *name); // Get environment variable value.

// Memory allocation
template <typename T> static inline T *acl_new() {
  return l_malloc_enable ? new (std::nothrow) T() : nullptr;
}
template <typename T> static inline void acl_delete(T *t) { delete t; }
template <typename T> static inline T *acl_new_arr(size_t i) {
  return l_malloc_enable ? new (std::nothrow) T[i]() : nullptr;
}
template <typename T> static inline void acl_delete_arr(T *t) { delete[] t; }
void *acl_malloc(size_t size);
void *acl_realloc(void *buffer, size_t size);
void acl_free(void *buffer);

// This is used in unit tests to test CL_OUT_OF_HOST_MEMORY error codes
void acl_set_malloc_enable(int malloc_enable);

// Simple file IO
struct acl_file_handle_t;
struct acl_file_handle_t *acl_get_handle_for_stdout(void);
struct acl_file_handle_t *acl_get_handle_for_stderr(void);
struct acl_file_handle_t *acl_fopen(const char *name, const char *mode);
int acl_fclose(struct acl_file_handle_t *);
int acl_unlink(const char *filename);
int acl_printf(const char *spec, ...);
int acl_fprintf(struct acl_file_handle_t *handle, const char *spec, ...);
size_t acl_fwrite(const void *ptr, size_t size, size_t nelem,
                  struct acl_file_handle_t *handle);
ACL_EXPORT
void acl_notify_print(const char *errinfo, const void *private_info, size_t cb,
                      void *user_data);

// Return true if we could change directory. 0 otherwise
int acl_chdir(const char *dir);

// Return all filenames that match pattern. The pattern is resolved
// relative to the current working directory.
std::vector<std::string> acl_glob(const std::string &pattern);

// Create a unique file name and make sure the directory is writable
int acl_create_unique_filename(std::string &name);

// Return non-zero if and only if there has been an error via errno
// mechanism.
int acl_has_error(void);

// Return a string describing the last errno-based error.
// For example, if acl_chdir fails, then call this to get an English
// description of the error.
const char *acl_support_last_error(void);

// Run a command and return its process status.
int acl_system(const char *command);

#ifdef _WIN32
#define ACL_PATH_MAX _MAX_PATH
#else
// Assume POSIX
#define ACL_PATH_MAX PATH_MAX
#endif

// Get absolute path for an existing file or directory.
// If there is an error, an empty string is returned.
//
// For consistent results across operating systems, you must ensure that
// the path refers to an existing filesystem object.
// (It will fail on Linux otherwise.)
//
// The absolute path returned will have unix style path separators.
std::string acl_realpath_existing(const std::string &path);

// Ensure a path to the directory exists.
// Return non-zero on success, and zero on failure.
// It can fail if we lack permissions, or if the path refers to an existing
// file.  or many other reasons.
int acl_make_path_to_dir(const std::string &path);

// Dynamic library loading support
void *acl_dlopen(const char *file);
void *acl_dlsym(void *dllhandle, const char *symbolname);
char *acl_dlerror(void);
int acl_dlpresent(void);
int acl_dlclose(void *);

// Supportfunction needed to create "unique" filenames
int acl_getpid();

// Get wall clock time in nanoseconds.
cl_ulong acl_get_time_ns(void);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

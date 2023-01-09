// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HAS_STDLIB_STDIO
#error "Sorry, we only support platforms with certain basic capabilities!"
#endif

// System headers.
#include <cassert>
#include <errno.h>
#include <sstream>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

// External library headers.
#include <acl_check_sys_cmd/acl_check_sys_cmd.h>

// Internal headers.
#include <acl_hal.h>
#include <acl_support.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

int l_malloc_enable = 1;

struct acl_file_handle_t;

// Normalize backslashes in path to forward-slashes.
std::string l_normalize_path(const std::string &path) {
  auto result = path;
  for (auto &ch : result) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return result;
}

const char *acl_getenv(const char *name) { return getenv(name); }

struct acl_file_handle_t *acl_get_handle_for_stdout(void) {
  return (struct acl_file_handle_t *)stdout;
}
struct acl_file_handle_t *acl_get_handle_for_stderr(void) {
  return (struct acl_file_handle_t *)stderr;
}

struct acl_file_handle_t *acl_fopen(const char *name, const char *mode) {
  FILE *fp = fopen(name, mode);
  if (fp)
    errno = 0; // Signal ok. Confusing otherwise.
  return (struct acl_file_handle_t *)fp;
}

int acl_fclose(struct acl_file_handle_t *handle) {
  FILE *fp = (FILE *)handle;
  return fclose(fp);
}
#ifdef _WIN32
int acl_unlink(const char *filename) { return DeleteFile(filename); }
#else
int acl_unlink(const char *filename) { return unlink(filename); }
#endif

int acl_printf(const char *format, ...) {
  int result;
  va_list args;
  va_start(args, format);
  result = vprintf(format, args);
  va_end(args);
  return result;
}

int acl_fprintf(struct acl_file_handle_t *handle, const char *format, ...) {
  int result;
  FILE *fp = (FILE *)handle;
  va_list args;
  va_start(args, format);
  result = vfprintf(fp, format, args);
  va_end(args);
  return result;
}

size_t acl_fwrite(const void *ptr, size_t size, size_t nelem,
                  struct acl_file_handle_t *handle) {
  FILE *fp = (FILE *)handle;
  return fwrite(ptr, size, nelem, fp);
}

void acl_notify_print(const char *errinfo, const void *private_info, size_t cb,
                      void *user_data) {
  private_info = private_info;
  cb = cb;
  user_data = user_data;
  printf("Error: %s\n", errinfo);
}

// This is used in unit tests to test CL_OUT_OF_HOST_MEMORY error codes
void acl_set_malloc_enable(int malloc_enable) {
  l_malloc_enable = malloc_enable;
}

void *acl_malloc(size_t size) { return l_malloc_enable ? malloc(size) : NULL; }

void *acl_realloc(void *buffer, size_t size) {
  return l_malloc_enable ? realloc(buffer, size) : NULL;
}

void acl_free(void *buffer) { free(buffer); }

std::string acl_realpath_existing(const std::string &path) {
  acl_assert_locked();
  std::vector<char> p(ACL_PATH_MAX);

#ifdef _WIN32
  if (!_fullpath(p.data(), path.c_str(), ACL_PATH_MAX)) {
    return "";
  }

  // Normalize to forward slashes.
  for (auto &ch : p) {
    if (ch == '\\') {
      ch = '/';
    }
  }
#else
  // Use POSIX relpath.
  // Relies on ACL_PATH_MAX being the same as PATH_MAX.
  if (!realpath(path.c_str(), p.data())) {
    return "";
  }

  if (p[0] != '/') {
    // Not an absolute path. Prepend current working directory.
    auto *cwd = getcwd(0, 0);
    if (!cwd)
      return "";

    std::stringstream ss;
    ss << cwd << "/" << p.data();
    free(cwd);

    if (!realpath(ss.str().c_str(), p.data())) {
      return "";
    }
  }
#endif

  return std::string(p.data());
}

// Change directory.
// Return 0 on failure, non-zero on success.
// Note: this inverts the usual sense.
int acl_chdir(const char *dir) {
  acl_assert_locked();
  // These will set errno
#ifdef _WIN32
  return !_chdir(dir);
#else
  return !chdir(dir);
#endif
}

std::vector<std::string> acl_glob(const std::string &pattern) {
  acl_assert_locked();
  std::vector<std::string> result;

#ifdef _WIN32
  WIN32_FIND_DATA ffd;
  HANDLE hFind = FindFirstFile(pattern.c_str(), &ffd);
  if (INVALID_HANDLE_VALUE != hFind) {
    result.push_back(ffd.cFileName);
    while (FindNextFile(hFind, &ffd)) {
      result.push_back(ffd.cFileName);
    }
  }
  FindClose(hFind);
#else // Linux
  glob_t globbuf;
  auto err = glob(pattern.c_str(), 0, nullptr, &globbuf);

  if (!err) {
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
      result.push_back(globbuf.gl_pathv[i]);
    }
  }
  globfree(&globbuf);
#endif

  return result;
}

int acl_has_error(void) { return errno != 0; }

const char *acl_support_last_error(void) { return strerror(errno); }

#ifdef _WIN32
#define l_mkdir(d) _mkdir(d)
#define l_chdir(d) _chdir(d)
#else
#define l_mkdir(d) mkdir(d, 0777)
#define l_chdir(d) chdir(d)
#endif

static int l_make_path_to_dir(const std::string &path);
int acl_make_path_to_dir(const std::string &path) {
  acl_assert_locked();
  if (path == "")
    return 0;
  else {
    // Remember where we started!
    auto original_dir = acl_realpath_existing(".");
    if (original_dir == "")
      return 0;

    acl_print_debug_msg("acl_make_path_to_dir: cwd is %s\n",
                        original_dir.c_str());

    auto result = l_make_path_to_dir(path);

    // Go back
    auto final_chdir = l_chdir(original_dir.c_str());
    if (final_chdir != 0) {
      acl_print_debug_msg("acl_make_path_to_dir: can't chdir back to %s: %d\n",
                          original_dir.c_str(), final_chdir);
      result = 0;
    }
    acl_print_debug_msg("acl_make_path_to_dir: result is %d\n", result);
    return result;
  }
}

static int l_make_path_to_dir(const std::string &path) {
  acl_assert_locked();

  acl_print_debug_msg("\nacl_make_path_to_dir: path arg is %s\n", path.c_str());

  auto normalized_path = l_normalize_path(path);
  // Root directory always exists.
  if (normalized_path == "/")
    return 1;
  // Trim trailing forward slash if it exists.
  if (normalized_path[normalized_path.length() - 1] == '/') {
    normalized_path = normalized_path.substr(0, normalized_path.length() - 1);
  }

  // Walk the directory path and create any missing directories along the way.
  std::string::size_type curr_pos = 0;
  while (curr_pos < normalized_path.length() && curr_pos != std::string::npos) {
    auto end_pos = normalized_path.find_first_of('/', curr_pos);
    std::string component = normalized_path.substr(
        curr_pos, end_pos == std::string::npos ? std::string::npos
                                               : end_pos - curr_pos + 1);

    // Current component doesn't exist so create it.
    auto status = l_chdir(component.c_str());
    if (status != 0) {
      auto mkdir_status = l_mkdir(component.c_str());
      if (mkdir_status != 0) {
        acl_print_debug_msg("Can't make path component '%s': %d\n",
                            component.c_str(), mkdir_status);
        return 0;
      }
      auto chdir_status = l_chdir(component.c_str());
      if (chdir_status != 0) {
        acl_print_debug_msg("Can't chdir path component '%s': %d\n",
                            component.c_str(), chdir_status);
        return 0;
      }
    }

    if (end_pos == std::string::npos) {
      curr_pos = std::string::npos;
    } else {
      curr_pos = end_pos + 1;
      // Consecutive forward slashes should be treated like a single forward
      // slash.
      while (curr_pos < normalized_path.length() &&
             normalized_path[curr_pos] == '/') {
        curr_pos++;
      }
    }
  }

  return 1;
}

// Extract content of section sect_name into a new unique out_file.
// If no such section exists, do nothing.
int acl_create_unique_filename(std::string &name) {
  static int enumerator = 0;

  std::stringstream fnss;
#ifdef _WIN32
  const char *tmpdir = acl_getenv("TEMP");
  if (!tmpdir) {
    tmpdir = acl_getenv("TMP");
    if (!tmpdir) {
      tmpdir = ".";
    }
  }
  fnss << tmpdir << "\\aocl-" << acl_getpid() << "-kernel" << enumerator++
       << ".dll";
#else
  const char *tmpdir = acl_getenv("TMPDIR");
  if (!tmpdir) {
    tmpdir = "/tmp";
  }
  fnss << tmpdir << "/aocl-" << acl_getpid() << "-kernel" << enumerator++
       << ".so";
#endif

  name = std::move(fnss.str());
  acl_print_debug_msg("Using temporary kernel file %s\n", name.c_str());

  return 1;
}

int acl_system(const char *command) {
  acl_assert_locked();
  assert(system_cmd_is_valid(command));
  return system(command);
}

// Dynamic library loading support
#ifdef _WIN32
void *acl_dlopen(const char *file) { return LoadLibrary(file); }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4054)
#endif
void *acl_dlsym(void *handle, const char *name) {
  return (void *)GetProcAddress((HMODULE)handle, name);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

char *acl_dlerror(void) {
  DWORD error_code = 0;
  char *buf = 0;
  error_code = GetLastError();
  if (error_code) {
    if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error_code, 0, buf, 0, NULL)) {
      printf("Failed to format Windows error message for %ld\n", error_code);
      exit(4);
    }
    return buf;
  } else {
    return 0;
  }
}

int acl_dlpresent(void) { return &LoadLibrary != nullptr; }

int acl_dlclose(void *handle) { return (int)FreeLibrary((HMODULE)handle); }

// Supportfunction needed to create "unique" filenames
int acl_getpid() { return _getpid(); }
#else
#define RTLD_NOW 0x00002 /* Immediate function call binding.  */
ACL_EXPORT
extern void *dlopen(__const char *__file, int __mode) __attribute__((weak));
ACL_EXPORT
extern void *dlsym(void *__restrict __handle, __const char *__restrict __name)
    __attribute__((weak));
ACL_EXPORT
extern char *dlerror(void) __attribute__((weak));
ACL_EXPORT
extern int dlclose(void *) __attribute__((weak));

void *acl_dlopen(const char *file) { return dlopen(file, RTLD_NOW); }

void *acl_dlsym(void *dllhandle, const char *symbolname) {
  return dlsym(dllhandle, symbolname);
}

char *acl_dlerror(void) { return dlerror(); }

int acl_dlpresent(void) { return &dlopen != nullptr; }

int acl_dlclose(void *dllhandle) { return !dlclose(dllhandle); }

// Supportfunction needed to create "unique" filenames
int acl_getpid() { return getpid(); }
#endif

// Get wall clock time in ns.
// Taken from the PCIe HAL.
#ifdef _WIN32
// Query the system timer, return a timer value in ns
static INT64 ticks_per_second;
static int ticks_per_second_valid = 0;
cl_ulong acl_get_time_ns() {
  LARGE_INTEGER li;
  double seconds;
  INT64 ticks;
  const INT64 NS_PER_S = 1000000000;

  if (!ticks_per_second_valid) {
    LARGE_INTEGER tps;
    tps.QuadPart = 0;
    QueryPerformanceFrequency(&tps);
    ticks_per_second = tps.QuadPart;
    // Performance counter might not be present. Then it returns 0.
    if (!ticks_per_second) {
      ticks_per_second = 1;
    }
    ticks_per_second_valid = 1;
  }

  QueryPerformanceCounter(&li);
  ticks = li.QuadPart;
  seconds = ticks / (double)ticks_per_second;
  return (cl_ulong)(seconds * NS_PER_S + 0.5);
}

#else

// Query the system timer, return a timer value in ns
cl_ulong acl_get_time_ns() {
  struct timespec a;
  const cl_ulong NS_PER_S = 1000000000;
  // Must use the MONOTONIC clock because the REALTIME clock
  // can go backwards due to adjustments by NTP for clock drift.
  // The MONOTONIC clock provides a timestamp since some fixed point in
  // the past, which might be system boot time or the start of the Unix
  // epoch.  This matches the Windows QueryPerformanceCounter semantics.
  // The MONOTONIC clock is to be used for measuring time intervals, and
  // fits the semantics of timestamps from the *device* perspective as defined
  // in OpenCL for clGetEventProfilingInfo.
  clock_gettime(CLOCK_MONOTONIC, &a);
  return (cl_ulong)(a.tv_nsec) + (cl_ulong)(a.tv_sec * NS_PER_S);
}
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

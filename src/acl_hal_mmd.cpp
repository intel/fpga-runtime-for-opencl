// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// HAL for mmd - Generic memory mapped device                           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

// System headers.
#include <cassert>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef __linux__
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4668)
#include <windows.h>
#pragma warning(pop)
#else
#define __USE_POSIX199309 1
#include <time.h>
#endif

// External library headers.
#include <acl_threadsupport/acl_threadsupport.h>
#include <pkg_editor/pkg_editor.h>

// Internal headers.
#include <acl_auto.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_hal_mmd.h>
#include <acl_kernel_if.h>
#include <acl_platform.h>
#include <acl_pll.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>
#include <acl_version.h>

// MMD headers.
#include <MMD/aocl_mmd.h>
#include <MMD/aocl_mmd_deprecated.h>

#ifndef MAX_NAME_SIZE
#define MAX_NAME_SIZE 1204
#endif

// User-callable parts of the HAL.
void *acl_hal_mmd_shared_alloc(cl_device_id device, size_t size,
                               size_t alignment, mem_properties_t *properties,
                               int *error);
void *acl_hal_mmd_host_alloc(const std::vector<cl_device_id> devices,
                             size_t size, size_t alignment,
                             mem_properties_t *properties, int *error);
int acl_hal_mmd_free(cl_context context, void *mem);
int acl_hal_mmd_try_devices(cl_uint num_devices, const cl_device_id *devices,
                            cl_platform_id platform);
int acl_hal_mmd_close_devices(cl_uint num_devices, const cl_device_id *devices);
void acl_hal_mmd_init_device(const acl_system_def_t *sysdef);
void acl_hal_mmd_yield(cl_uint num_devices, const cl_device_id *devices);
cl_ulong acl_hal_mmd_get_timestamp(void);
void acl_hal_mmd_copy_hostmem_to_hostmem(cl_event event, const void *src,
                                         void *dest, size_t size);
void acl_hal_mmd_copy_hostmem_to_globalmem(cl_event event, const void *src,
                                           void *dest, size_t size);
void acl_hal_mmd_copy_globalmem_to_hostmem(cl_event event, const void *src,
                                           void *dest, size_t size);
void acl_hal_mmd_copy_globalmem_to_globalmem(cl_event event, const void *src,
                                             void *dest, size_t size);
void acl_hal_mmd_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf);
void acl_hal_mmd_launch_kernel(unsigned int physical_device_id,
                               acl_kernel_invocation_wrapper_t *wrapper);
void acl_hal_mmd_unstall_kernel(unsigned int physical_device_id,
                                int activation_id);
int acl_hal_mmd_program_device(unsigned int physical_device_id,
                               const acl_device_def_t *devdef,
                               const struct acl_pkg_file *binary,
                               int acl_program_mode);
cl_bool acl_hal_mmd_query_temperature(unsigned int physical_device_id,
                                      cl_int *temp);
int acl_hal_mmd_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size);
int acl_hal_mmd_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size);
void acl_hal_mmd_kernel_interrupt(int handle, void *user_data);
void acl_hal_mmd_device_interrupt(int handle, aocl_mmd_interrupt_info *data_in,
                                  void *user_data);
void acl_hal_mmd_status_handler(int handle, void *user_data, aocl_mmd_op_t op,
                                int status);
void *acl_hal_mmd_legacy_shared_alloc(cl_context context, size_t size,
                                      unsigned long long *device_ptr_out);
void acl_hal_mmd_legacy_shared_free(cl_context context, void *host_ptr,
                                    size_t size);
int acl_hal_mmd_pll_reconfigure(unsigned int physical_device_id,
                                const char *pll_settings_str);
void acl_hal_mmd_reset_kernels(cl_device_id device);
void acl_hal_mmd_get_device_status(cl_uint num_devices,
                                   const cl_device_id *devices);
int acl_hal_mmd_get_debug_verbosity();
int acl_hal_mmd_hostchannel_create(unsigned int physical_device_id,
                                   char *channel_name, size_t num_packets,
                                   size_t packet_size, int direction);
int acl_hal_mmd_hostchannel_destroy(unsigned int physical_device_id,
                                    int channel_handle);
size_t acl_hal_mmd_hostchannel_pull(unsigned int physical_device_id,
                                    int channel_handle, void *host_buffer,
                                    size_t read_size, int *status);
size_t acl_hal_mmd_hostchannel_push(unsigned int physical_device_id,
                                    int channel_handle, const void *host_buffer,
                                    size_t write_size, int *status);
void *acl_hal_mmd_hostchannel_get_buffer(unsigned int physical_device_id,
                                         int channel_handle,
                                         size_t *buffer_size, int *status);
size_t acl_hal_mmd_hostchannel_ack_buffer(unsigned int physical_device_id,
                                          int channel_handle, size_t ack_size,
                                          int *status);

// Following six calls provide access to the kernel profiling hardware
// accel_id is the same as in the invocation image passed to
// acl_hal_mmd_launch_kernel()
int acl_hal_mmd_get_profile_data(unsigned int physical_device_id,
                                 unsigned int accel_id, uint64_t *data,
                                 unsigned int length);
int acl_hal_mmd_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id);
int acl_hal_mmd_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id);
int acl_hal_mmd_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id);
int acl_hal_mmd_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id);
int acl_hal_mmd_set_profile_start_count(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value);
int acl_hal_mmd_set_profile_stop_count(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value);

void acl_hal_mmd_simulation_streaming_kernel_start(
    unsigned int physical_device_id, const std::string &kernel_name,
    const int accel_id);
void acl_hal_mmd_simulation_streaming_kernel_done(
    unsigned int physical_device_id, const std::string &kernel_name,
    unsigned int &finish_counter);

void acl_hal_mmd_simulation_set_kernel_cra_address_map(
    unsigned int physical_device_id,
    const std::vector<uintptr_t> &kernel_csr_address_map);

size_t acl_hal_mmd_read_csr(unsigned int physical_device_id, uintptr_t offset,
                            void *ptr, size_t size);

size_t acl_hal_mmd_write_csr(unsigned int physical_device_id, uintptr_t offset,
                             const void *ptr, size_t size);

int acl_hal_mmd_simulation_device_global_interface_read(
    unsigned int physical_device_id, const char *interface_name,
    void *host_addr, size_t dev_addr, size_t size);
int acl_hal_mmd_simulation_device_global_interface_write(
    unsigned int physical_device_id, const char *interface_name,
    const void *host_addr, size_t dev_addr, size_t size);

static size_t acl_kernel_if_read(acl_bsp_io *io, dev_addr_t src, char *dest,
                                 size_t size);
static size_t acl_kernel_if_write(acl_bsp_io *io, dev_addr_t dest,
                                  const char *src, size_t size);
static size_t acl_pll_read(acl_bsp_io *io, dev_addr_t src, char *dest,
                           size_t size);
static size_t acl_pll_write(acl_bsp_io *io, dev_addr_t dest, const char *src,
                            size_t size);
static time_ns acl_bsp_get_timestamp(void);

int acl_hal_mmd_has_svm_support(unsigned int physical_device_id, int *value);
int acl_hal_mmd_has_physical_mem(unsigned int physical_device_id);

static void *
acl_hal_get_board_extension_function_address(const char *func_name,
                                             unsigned int physical_device_id);

static int l_try_device(unsigned int physical_device_id, const char *name,
                        acl_system_def_t *sys,
                        acl_mmd_dispatch_t *mmd_dispatch_for_board);
ACL_HAL_EXPORT const acl_hal_t *
acl_mmd_get_system_definition(acl_system_def_t *sys,
                              acl_mmd_library_names_t *_libraries_to_load);
unsigned acl_convert_mmd_capabilities(unsigned mmd_capabilities);

const static size_t MIN_SOF_SIZE = 1;
const static size_t MIN_PLL_CONFIG_SIZE = 1;

std::vector<acl_mmd_dispatch_t> internal_mmd_dispatch;

// Dynamically load board mmd & symbols
static size_t num_board_pkgs;
static double min_MMD_version = DBL_MAX;
extern "C" {
void *null_fn = NULL;
}
#define IS_VALID_FUNCTION(X) (&X == NULL) ? 0 : 1
#ifdef _MSC_VER
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_get_offline_info=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_get_info=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_open=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_close=null_fn")
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_set_interrupt_handler=null_fn")
#pragma comment(                                                               \
    linker,                                                                    \
    "/alternatename:__imp_aocl_mmd_set_device_interrupt_handler=null_fn")
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_set_status_handler=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_yield=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_read=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_write=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_copy=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_reprogram=null_fn")
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_shared_mem_alloc=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_shared_mem_free=null_fn")
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_hostchannel_create=null_fn")
#pragma comment(linker,                                                        \
                "/alternatename:__imp_aocl_mmd_hostchannel_destroy=null_fn")
#pragma comment(                                                               \
    linker, "/alternatename:__imp_aocl_mmd_hostchannel_get_buffer=null_fn")
#pragma comment(                                                               \
    linker, "/alternatename:__imp_aocl_mmd_hostchannel_ack_buffer=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_program=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_host_alloc=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_free=null_fn")
#pragma comment(linker, "/alternatename:__imp_aocl_mmd_shared_alloc=null_fn")
#endif

#ifdef _WIN32
char *acl_strtok(char *str, const char *delim, char **saveptr) {
  return strtok_s(str, delim, saveptr);
}
#else // Linux
char *acl_strtok(char *str, const char *delim, char **saveptr) {
  return strtok_r(str, delim, saveptr);
}
#endif

#define ADD_STATIC_FN_TO_HAL(STRUCT, X, REQUIRED)                              \
  if (!IS_VALID_FUNCTION(X)) {                                                 \
    if (REQUIRED) {                                                            \
      ACL_HAL_DEBUG_MSG_VERBOSE(                                               \
          1, "Function X is not defined statically by board library\n");       \
      return NULL;                                                             \
    } else {                                                                   \
      STRUCT.X = NULL;                                                         \
    }                                                                          \
  }                                                                            \
  STRUCT.X = X;

#define ADD_DYNAMIC_FN_TO_HAL(STRUCT, LIBRARY, X, REQUIRED, TYPE)              \
  STRUCT->X = (TYPE)my_dlsym(LIBRARY, #X, &error_msg);                         \
  if (!STRUCT->X && REQUIRED) {                                                \
    printf("Error: Symbol %s not found in board library", #X);                 \
    if (error_msg && error_msg[0] != '\0') {                                   \
      printf("(message: %s)", error_msg);                                      \
    }                                                                          \
    printf("\n");                                                              \
    return CL_FALSE;                                                           \
  }

#ifdef _WIN32
static cl_ulong m_ticks_per_second = 0;
#endif

#define info_assert(COND, ...)                                                 \
  do {                                                                         \
    if (!(COND)) {                                                             \
      printf("%s:%d:assert failure: ", __FILE__, __LINE__);                    \
      printf(__VA_ARGS__);                                                     \
      fflush(stdout);                                                          \
      assert(0);                                                               \
    }                                                                          \
  } while (0)

// Handle to the device

// Interfaces
static int kernel_interface = -1;
static int pll_interface = -1;
static int memory_interface = -1;

static int debug_verbosity = 0;
#define ACL_HAL_DEBUG_MSG_VERBOSE(verbosity, m, ...)                           \
  if (debug_verbosity >= verbosity)                                            \
    do {                                                                       \
      printf((m), ##__VA_ARGS__);                                              \
  } while (0)

static acl_bsp_io bsp_io_kern[ACL_MAX_DEVICE];
static acl_bsp_io bsp_io_pll[ACL_MAX_DEVICE];
static acl_kernel_if kern[ACL_MAX_DEVICE];
static acl_pll pll[ACL_MAX_DEVICE];
static acl_mmd_device_t device_info[ACL_MAX_DEVICE];

static unsigned num_physical_devices = 0;

static int uses_yield_ref = -1;

// The PCIe HAL structure
static acl_event_update_callback acl_event_update_fn = NULL;
acl_kernel_update_callback acl_kernel_update_fn = NULL;
acl_profile_callback acl_profile_fn = NULL;
acl_device_update_callback acl_device_update_fn = NULL;

static acl_hal_t acl_hal_mmd = {
    acl_hal_mmd_init_device,   // init_device
    NULL,                      // yield: Populated based on MMD property
    acl_hal_mmd_get_timestamp, // get_timestamp
    acl_hal_mmd_copy_hostmem_to_hostmem,     // copy_hostmem_to_hostmem
    acl_hal_mmd_copy_hostmem_to_globalmem,   // copy_hostmem_to_globalmem
    acl_hal_mmd_copy_globalmem_to_hostmem,   // copy_globalmem_to_hostmem
    acl_hal_mmd_copy_globalmem_to_globalmem, // copy_globalmem_to_globalmem
    acl_hal_mmd_register_callbacks,          // register_callbacks
    acl_hal_mmd_launch_kernel,               // launch_kernel
    acl_hal_mmd_unstall_kernel,              // unstall_kernel
    acl_hal_mmd_program_device,              // program_device
    acl_hal_mmd_query_temperature,           // query_temperature
    acl_hal_mmd_get_device_official_name,    // get_device_official_name
    acl_hal_mmd_get_device_vendor_name,      // get_device_vendor_name
    acl_hal_mmd_legacy_shared_alloc,         // legacy_shared_alloc
    acl_hal_mmd_legacy_shared_free,          // legacy_shared_free
    acl_hal_mmd_get_profile_data,            // get_profile_data
    acl_hal_mmd_reset_profile_counters,      // reset_profile_counters
    acl_hal_mmd_disable_profile_counters,    // disable_profile_counters
    acl_hal_mmd_enable_profile_counters,     // enable_profile_counters
    acl_hal_mmd_set_profile_shared_control,  // set_profile_shared_control
    acl_hal_mmd_set_profile_start_count,     // set_profile_start_cycle
    acl_hal_mmd_set_profile_stop_count,      // set_profile_stop_cycle
    acl_hal_mmd_has_svm_support,             // has_svm_memory_support
    acl_hal_mmd_has_physical_mem,            // has_physical_mem
    acl_hal_get_board_extension_function_address, // get_board_extension_function_address
    acl_hal_mmd_pll_reconfigure,                  // pll_reconfigure
    acl_hal_mmd_reset_kernels,                    // reset_kernels
    acl_hal_mmd_hostchannel_create,               // hostchannel_create
    acl_hal_mmd_hostchannel_destroy,              // hostchannel_destroy
    acl_hal_mmd_hostchannel_pull,                 // hostchannel_pull
    acl_hal_mmd_hostchannel_push,                 // hostchannel_push
    acl_hal_mmd_hostchannel_get_buffer,           // hostchannel_get_buffer
    acl_hal_mmd_hostchannel_ack_buffer,           // hostchannel_ack_buffer
    acl_hal_mmd_get_device_status,                // get_device_status
    acl_hal_mmd_get_debug_verbosity,              // get_debug_verbosity
    acl_hal_mmd_try_devices,                      // try_devices
    acl_hal_mmd_close_devices,                    // close_devices
    acl_hal_mmd_host_alloc,                       // host_alloc
    acl_hal_mmd_free,                             // free
    acl_hal_mmd_shared_alloc,                     // shared_alloc
    acl_hal_mmd_simulation_streaming_kernel_start, // simulation_streaming_kernel_start
    acl_hal_mmd_simulation_streaming_kernel_done, // simulation_streaming_kernel_done
    acl_hal_mmd_simulation_set_kernel_cra_address_map, // simulation_set_kernel_cra_address_map
    acl_hal_mmd_read_csr,                              // read_csr
    acl_hal_mmd_write_csr,                             // write_csr
    acl_hal_mmd_simulation_device_global_interface_read, // simulation_device_global_interface_read
    acl_hal_mmd_simulation_device_global_interface_write, // simulation_device_global_interface_write
};

// This will contain the device physical id to tell us which device across all
// loaded BSPs (even with the same handle numbers) is calling the interrupt
// handler.
unsigned interrupt_user_data[ACL_MAX_DEVICE];

// ********************* Helper functions ********************
#define MAX_BOARD_NAMES_LEN (ACL_MAX_DEVICE * 30 + 1)
#define MMD_VERSION_LEN 30
static void *my_dlopen_flags(const char *library_name, int flag,
                             char **error_msg) {
  void *library;
  acl_assert_locked();

#ifdef _WIN32
  // Removing Windows warning
  flag = flag;
  library = (void *)LoadLibraryA(library_name);

  // Retrieve error string
  DWORD err_id = GetLastError();
  if (err_id == 0) {
    *error_msg = "";
  } else {
    char *msg_buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPSTR)&msg_buf, 0, NULL);
    *error_msg = msg_buf;
  }
#else
  library = dlopen(library_name, flag);
  *error_msg = dlerror();
#endif
  return library;
}

static void *my_dlopen(const char *library_name, char **error_msg) {
#ifdef _WIN32
  return my_dlopen_flags(library_name, 0, error_msg);
#else
  return my_dlopen_flags(library_name, RTLD_NOW, error_msg);
#endif
}
void *my_dlopen_global(const char *library_name, char **error_msg) {
#ifdef _WIN32
  return my_dlopen_flags(library_name, 0, error_msg);
#else
  return my_dlopen_flags(library_name, RTLD_NOW | RTLD_GLOBAL, error_msg);
#endif
}

static void *my_dlsym(void *library, const char *function_name,
                      char **error_msg) {
  void *symbol;
  acl_assert_locked();
#ifdef _WIN32
  if (!library || !function_name) {
    *error_msg = "library or function name is empty";
    return NULL;
  }
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4152)
#endif
  symbol = GetProcAddress((HMODULE)library, function_name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  *error_msg = "";
#else
  symbol = dlsym(library, function_name);
  *error_msg = dlerror();
#endif
  return symbol;
}

static void my_dlclose(void *library) {
  acl_assert_locked();
#ifdef _WIN32
  FreeLibrary((HMODULE)library);
#else
  dlclose(library);
#endif
}

cl_bool l_load_board_functions(acl_mmd_dispatch_t *mmd_dispatch,
                               const char *library_name, void *mmd_library,
                               char *error_msg) {
  acl_assert_locked();
// my_dlsym returns a void pointer to be generic but the functions have a
// specific prototype. Ignore the resulting warning.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4152)
#endif

  mmd_dispatch->library_name = library_name;
  mmd_dispatch->mmd_library = mmd_library;
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_get_offline_info, 1,
      int (*)(aocl_mmd_offline_info_t, size_t, void *, size_t *));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_get_info, 1,
      int (*)(int, aocl_mmd_info_t, size_t, void *, size_t *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_open, 1,
                        int (*)(const char *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_close, 1,
                        int (*)(int));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library,
                        aocl_mmd_set_interrupt_handler, 1,
                        int (*)(int, aocl_mmd_interrupt_handler_fn, void *));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_set_device_interrupt_handler, 0,
      int (*)(int, aocl_mmd_device_interrupt_handler_fn, void *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_set_status_handler,
                        1, int (*)(int, aocl_mmd_status_handler_fn, void *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_yield, 1,
                        int (*)(int));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_read, 1,
      int (*)(int, aocl_mmd_op_t, size_t, void *, int, size_t));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_write, 1,
      int (*)(int, aocl_mmd_op_t, size_t, const void *, int, size_t));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_copy, 1,
      int (*)(int, aocl_mmd_op_t, size_t, int, size_t, size_t));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_reprogram, 0,
                        int (*)(int, void *, size_t));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_shared_mem_alloc, 0,
                        void *(*)(int, size_t, unsigned long long *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_shared_mem_free, 0,
                        void (*)(int, void *, size_t));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_hostchannel_create,
                        0, int (*)(int, char *, size_t, int));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_hostchannel_destroy,
                        0, int (*)(int, int));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library,
                        aocl_mmd_hostchannel_get_buffer, 0,
                        void *(*)(int, int, size_t *, int *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library,
                        aocl_mmd_hostchannel_ack_buffer, 0,
                        size_t(*)(int, int, size_t, int *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_program, 0,
                        int (*)(int, void *, size_t, aocl_mmd_program_mode_t));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_host_alloc, 0,
                        void *(*)(int *, size_t, size_t, size_t,
                                  aocl_mmd_mem_properties_t *, int *));
  ADD_DYNAMIC_FN_TO_HAL(mmd_dispatch, mmd_library, aocl_mmd_free, 0,
                        int (*)(void *));
  ADD_DYNAMIC_FN_TO_HAL(
      mmd_dispatch, mmd_library, aocl_mmd_shared_alloc, 0,
      void *(*)(int, size_t, size_t, aocl_mmd_mem_properties_t *, int *));

#ifdef _MSC_VER
#pragma warning(pop)
#endif

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "Successfully loaded board MMD %s\n",
                            library_name);
  return CL_TRUE;
}

#ifdef __linux__
static bool lib_already_loaded = false;
// Callback for dl_iterate_phdr; called for each library that is already loaded.
// Check to see if the requested library matches any of the libraries that have
// already been loaded.
// Emits messages and sets the lib_already_loaded flag.
static int lib_checker(struct dl_phdr_info *info, size_t size, void *data) {
  const char *library_name = (char *)data;

  // Library name starts after last occurrance of '/', if any
  const char *loaded_lib_name = strrchr(info->dlpi_name, '/');
  if (loaded_lib_name == NULL) {
    loaded_lib_name = info->dlpi_name;
  } else {
    loaded_lib_name++;
  }

  if (strncmp(library_name, loaded_lib_name, MAX_NAME_SIZE) == 0) {
    // This exact library was already loaded: emit error message.
    std::cout << "Warning: The BSP library '" << library_name
              << "' was already loaded as '" << info->dlpi_name
              << "'; not attempting to reload it.\n";
    lib_already_loaded = true;
  } else if (strncmp(library_name, loaded_lib_name, 6) == 0) {
    // Somewhat arbitrary limit that allows for detection of versioned libraries
    // with a name containing at least one unique character.
    // e.g. libX.so and libX.so.1
    std::cout << "Warning: Attempting to load BSP library '" << library_name
              << "' but library with similar name '" << info->dlpi_name
              << "' has already been loaded.\n";
  }

  return 0;
}
#endif

cl_bool l_load_single_board_library(const char *library_name,
                                    size_t &num_boards_found,
                                    cl_bool load_libraries) {
  acl_assert_locked();

  char *error_msg = nullptr;
#ifdef __linux__
  if (debug_verbosity > 0) {
    // TODO: Add similar support for Windows?
    // Check to see if this lib or a similarly named lib is already opened.
    lib_already_loaded = false;
    dl_iterate_phdr(lib_checker, (void *)library_name);
    if (lib_already_loaded)
      return CL_FALSE;
  }
#endif
  auto *mmd_library = my_dlopen(library_name, &error_msg);
  if (!mmd_library) {
    std::cout << "Error: Could not load board library " << library_name;
    if (error_msg && error_msg[0] != '\0') {
      std::cout << " (error_msg: " << error_msg << ")";
    }
    std::cout << "\n";
    return CL_FALSE;
  }

  auto *test_symbol =
      my_dlsym(mmd_library, "aocl_mmd_get_offline_info", &error_msg);
  if (!test_symbol) {
    // On Linux, for custom libraries close the library (which was opened
    // locally) and then reopen globally. For Windows, there is no option (i.e.
    // it is always global)
#ifdef __linux__
    my_dlclose(mmd_library);
    ACL_HAL_DEBUG_MSG_VERBOSE(
        1, "This library is a custom library. Opening globally.\n");
    mmd_library = my_dlopen_global(library_name, &error_msg);
    if (!mmd_library) {
      std::cout << "Error: Could not load custom library " << library_name;
      if (error_msg && error_msg[0] != '\0') {
        std::cout << " (error_msg: " << error_msg << ")";
      }
      std::cout << "\n";
      return CL_FALSE;
    }
#endif
  } else {
    if (load_libraries) {
      auto result =
          l_load_board_functions(&(internal_mmd_dispatch[num_boards_found]),
                                 library_name, mmd_library, error_msg);
      if (result == CL_FALSE) {
        std::cout << "Error: Could not load board library " << library_name
                  << " due to failure to load symbols\n";
        return result;
      }
    }
    ++num_boards_found;
  }

  return CL_TRUE;
}

#ifdef _WIN32
cl_bool l_load_board_libraries(cl_bool load_libraries) {
  char library_name[1024] = {0};
  size_t num_boards_found = 0;

  // On Windows it will check the regkey. If ACL_BOARD_VENDOR_PATH is defined it
  // will check HKCU, otherwise HKLM
  auto *secondaryVendorPath = acl_getenv("ACL_BOARD_VENDOR_PATH");
  acl_assert_locked();

  HKEY HKEY_TYPE =
      !secondaryVendorPath ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  std::string hkey_name = !secondaryVendorPath ? "HKLM" : "HKCU";

  std::string boards_reg = "SOFTWARE\\Intel\\OpenCL\\Boards";
  HKEY boards_key = nullptr;
  ACL_HAL_DEBUG_MSG_VERBOSE(1, "Opening key %s\\%s...\n", hkey_name.c_str(),
                            boards_reg.c_str());
  auto result =
      RegOpenKeyExA(HKEY_TYPE, boards_reg.c_str(), 0, KEY_READ, &boards_key);
  if (ERROR_SUCCESS == result) {
    // for each value
    for (DWORD dwIndex = 0;; ++dwIndex) {
      DWORD dwLibraryNameSize = sizeof(library_name);
      DWORD dwLibraryNameType = 0;
      DWORD dwValue = 0;
      DWORD dwValueSize = sizeof(dwValue);

      // read the value name
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "Reading value %ld...\n", dwIndex);
      result = RegEnumValueA(boards_key, dwIndex, library_name,
                             &dwLibraryNameSize, NULL, &dwLibraryNameType,
                             (LPBYTE)&dwValue, &dwValueSize);
      // if RegEnumKeyEx fails, we are done with the enumeration
      if (ERROR_SUCCESS != result) {
        ACL_HAL_DEBUG_MSG_VERBOSE(
            1, "Failed to read value %ld, done reading key.\n", dwIndex);
        break;
      }
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value %s found...\n", library_name);

      // Require that the value be a DWORD and equal zero
      if (REG_DWORD != dwLibraryNameType) {
        ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value not a DWORD, skipping\n");
        continue;
      }
      if (dwValue) {
        ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value not zero, skipping\n");
        continue;
      }

      // add the library
      cl_bool cl_result = l_load_single_board_library(
          library_name, num_boards_found, load_libraries);
      if (!cl_result) {
        result = RegCloseKey(boards_key);
        if (ERROR_SUCCESS != result) {
          printf("Failed to close platforms key %s, ignoring\n",
                 (char *)boards_key);
        }
        return cl_result;
      }
    }
  } else {
    if (hkey_name == "HKCU") {
      ACL_HAL_DEBUG_MSG_VERBOSE(1,
                                "Warning: Failed to open the platforms key %s "
                                "from HKCU. Will try to open it from HKLM\n",
                                boards_reg.c_str());
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "Opening key %s\\%s...\n", "HKLM",
                                boards_reg.c_str());
      result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, boards_reg.c_str(), 0,
                             KEY_READ, &boards_key);
      if (ERROR_SUCCESS == result) {
        // for each value
        for (DWORD dwIndex = 0;; ++dwIndex) {
          DWORD dwLibraryNameSize = sizeof(library_name);
          DWORD dwLibraryNameType = 0;
          DWORD dwValue = 0;
          DWORD dwValueSize = sizeof(dwValue);

          // read the value name
          ACL_HAL_DEBUG_MSG_VERBOSE(1, "Reading value %ld...\n", dwIndex);
          result = RegEnumValueA(boards_key, dwIndex, library_name,
                                 &dwLibraryNameSize, NULL, &dwLibraryNameType,
                                 (LPBYTE)&dwValue, &dwValueSize);
          // if RegEnumKeyEx fails, we are done with the enumeration
          if (ERROR_SUCCESS != result) {
            ACL_HAL_DEBUG_MSG_VERBOSE(
                1, "Failed to read value %ld, done reading key.\n", dwIndex);
            break;
          }
          ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value %s found...\n", library_name);

          // Require that the value be a DWORD and equal zero
          if (REG_DWORD != dwLibraryNameType) {
            ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value not a DWORD, skipping\n");
            continue;
          }
          if (dwValue) {
            ACL_HAL_DEBUG_MSG_VERBOSE(1, "Value not zero, skipping\n");
            continue;
          }

          // add the library
          auto cl_result = l_load_single_board_library(
              library_name, num_boards_found, load_libraries);
          if (!cl_result) {
            result = RegCloseKey(boards_key);
            if (ERROR_SUCCESS != result) {
              printf("Failed to close platforms key %s, ignoring\n",
                     (char *)boards_key);
            }
            return cl_result;
          }
        }
      } else {
        ACL_HAL_DEBUG_MSG_VERBOSE(
            1,
            "Error: Failed to open platforms key %s to load board library at "
            "runtime. Either link to the board library ",
            boards_reg.c_str());
        ACL_HAL_DEBUG_MSG_VERBOSE(
            1, "while compiling your host code or refer to your board vendor's "
               "documentation on how to install the board library ");
        ACL_HAL_DEBUG_MSG_VERBOSE(1, "so that it can be loaded at runtime.\n");
      }
    } else {
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1,
          "Error: Failed to open platforms key %s to load board library at "
          "runtime. Either link to the board library ",
          boards_reg.c_str());
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1, "while compiling your host code or refer to your board vendor's "
             "documentation on how to install the board library ");
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "so that it can be loaded at runtime.\n");
    }
  }

  if (ERROR_SUCCESS == result) {
    result = RegCloseKey(boards_key);
    if (ERROR_SUCCESS != result) {
      ACL_HAL_DEBUG_MSG_VERBOSE(1,
                                "Failed to close platforms key %s, ignoring\n",
                                (char *)boards_key);
    }
  }

  if (!load_libraries) {
    num_board_pkgs = num_boards_found;
    internal_mmd_dispatch.resize(num_board_pkgs);
  }
  return CL_TRUE;
}
#else // Linux
cl_bool l_load_board_libraries(cl_bool load_libraries) {
  // Keeping the old path for backward compatibility
  std::string board_vendor_path_old = "/opt/Intel/OpenCL_boards/";
  std::string board_vendor_path = "/opt/Intel/OpenCL/Boards/";
  auto *customer_board_vendor_path = acl_getenv("ACL_BOARD_VENDOR_PATH");
  acl_assert_locked();

  // If the customer_board_vendor_path is defined, runtime should only load the
  // fcd file there Otherwise, runtime will load the fcd file from the default
  // directory
  if (customer_board_vendor_path) {
    // append the '/' to the end of the customer_board_vendor_path
    // and load it to board_vendor_path
    board_vendor_path = customer_board_vendor_path + std::string("/");
  }

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "Intel(R) FPGA Board Vendor Path: %s\n",
                            board_vendor_path.c_str());

  size_t num_boards_found = 0;
  auto num_vendor_files_found = 0;
  auto num_passes = 2;
  DIR *dir = nullptr;
  for (auto ipass = 0; ipass < num_passes; ++ipass) {
    std::string vendor_path_to_use;
    if (ipass == 0) {
      dir = opendir(board_vendor_path_old.c_str());
      if (!dir)
        continue;

      vendor_path_to_use = board_vendor_path_old;
    } else {
      dir = opendir(board_vendor_path.c_str());
      if (!dir) {
        ACL_HAL_DEBUG_MSG_VERBOSE(1, "Failed to open path %s\n",
                                  board_vendor_path.c_str());
        continue;
      }
      vendor_path_to_use = board_vendor_path;
    }

    // attempt to load all files in the directory
    for (auto *dir_entry = readdir(dir); dir_entry; dir_entry = readdir(dir)) {
      std::string extension = ".fcd";

      // make sure the file name ends in .fcd
      std::string filename = dir_entry->d_name;
      if (extension.length() > filename.length() ||
          filename.substr(filename.length() - extension.length()) !=
              extension) {
        continue;
      }

      filename = vendor_path_to_use + filename;
      num_vendor_files_found++;
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "Reading file %s\n", filename.c_str());

      // open the file and read its contents
      std::ifstream fin(filename);
      if (!fin.is_open())
        break;

      std::string library_name;
      while (std::getline(fin, library_name)) {
        if (library_name == "") {
          continue;
        }

        // add the library
        ACL_HAL_DEBUG_MSG_VERBOSE(1,
                                  "Trying to dynamically load board MMD %s "
                                  "(length is %zu, last char is '%c')\n",
                                  library_name.c_str(), library_name.length(),
                                  library_name[library_name.length() - 1]);
        auto result_status = l_load_single_board_library(
            library_name.c_str(), num_boards_found, load_libraries);
        if (!result_status) {
          printf("Failed to dynamically load board MMD %s\n",
                 library_name.c_str());
          // Ignoring this failure since other libraries may successfully load.
        }
      }
      fin.close();
    }
    closedir(dir);
  }

  if (num_vendor_files_found == 0) {
    ACL_HAL_DEBUG_MSG_VERBOSE(
        1, "Error: Did not find any board vendor files in %s",
        board_vendor_path.c_str());
    ACL_HAL_DEBUG_MSG_VERBOSE(1, " to load board library at runtime. Either "
                                 "link to the board library while compiling ");
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "your host code or refer to your board "
                                 "vendor's documentation on how to ");
    ACL_HAL_DEBUG_MSG_VERBOSE(
        1, "install the board library so that it can be loaded at runtime.\n");
  }

  if (!load_libraries) {
    num_board_pkgs = num_boards_found;
    if (num_board_pkgs) {
      internal_mmd_dispatch.resize(num_board_pkgs);
    }
  }

  return num_boards_found == 0 ? CL_FALSE : CL_TRUE;
}
#endif

void acl_hal_mmd_pll_override(unsigned int physical_device_id) {
  char *env_pllsettings_str = getenv("ACL_PLL_SETTINGS");
  acl_assert_locked();

  if (env_pllsettings_str) {
    int return_val =
        acl_hal_mmd_pll_reconfigure(physical_device_id, env_pllsettings_str);
    assert(return_val == 0);
  }
}

int acl_hal_mmd_pll_reconfigure(unsigned int physical_device_id,
                                const char *pll_settings_str) {
  pll_setting_t pll_setting;
  acl_pll *current_pll = &pll[physical_device_id];

  // parse manually. sscanf doesn't link.
  int filled = 0;
  char *space_loc = (char *)pll_settings_str;
  unsigned int *dest = (unsigned int *)&pll_setting;

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL: Parsing ACL_PLL_SETTINGS string: %s\n",
                            space_loc);
  *dest = (unsigned)atoi(space_loc);
  filled++;
  dest++;
  while ((space_loc = strchr(space_loc + 1, ' ')) != NULL) {
    *dest = (unsigned)atoi(space_loc + 1);
    filled++;
    dest++;
  }

  if (filled == 9) {
    return acl_pll_reconfigure(current_pll, pll_setting);
  } else {
    printf("HAL Warning: Failed to parse pll settings from ACL_PLL_SETTINGS "
           "environment variable, ignoring pll override\n");
    return -1;
  }
}

void acl_hal_mmd_get_device_status(cl_uint num_devices,
                                   const cl_device_id *devices) {
  unsigned physical_device_id;
  for (unsigned idevice = 0; idevice < num_devices; idevice++) {
    assert(devices[idevice]->opened_count > 0);

    physical_device_id = devices[idevice]->def.physical_device_id;
    acl_kernel_if_check_kernel_status(&kern[physical_device_id]);
  }
}

int acl_hal_mmd_get_debug_verbosity() { return debug_verbosity; }

// ********************* HAL functions ********************

// Attempt to add a single device
static int l_try_device(unsigned int physical_device_id, const char *name,
                        acl_system_def_t *sys,
                        acl_mmd_dispatch_t *mmd_dispatch_for_board) {
  acl_assert_locked();
  assert(physical_device_id < ACL_MAX_DEVICE);

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Calling mmd_open on %s\n", name);
  acl_mmd_device_t *device = &(device_info[physical_device_id]);
  // If device is available, we shouldn't overwrite this memory, so we keep it
  // in the acl_mmd_device_t struct.
  device->name = name;
  auto tmp_handle = mmd_dispatch_for_board->aocl_mmd_open(device->name.c_str());
  if (tmp_handle < 0) {
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : device not found!\n");
    return 0;
  }

  device->mmd_dispatch = mmd_dispatch_for_board;
  device->handle = tmp_handle;

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : mmd_open returned handle %d\n",
                            device->handle);

  if (debug_verbosity > 0) {
    char buf[1024];
    device->mmd_dispatch->aocl_mmd_get_info(device->handle, AOCL_MMD_BOARD_NAME,
                                            sizeof(buf), buf, NULL);
    buf[sizeof(buf) - 1] = 0;
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Getting info board name: %s\n", buf);
  }

  // Get interfaces - for now assume one kernel, one pll and one memory
  ACL_HAL_DEBUG_MSG_VERBOSE(1,
                            "HAL : Getting interfaces via aocl_mmd_get_info\n");
  device->mmd_dispatch->aocl_mmd_get_info(device->handle,
                                          AOCL_MMD_KERNEL_INTERFACES,
                                          sizeof(int), &kernel_interface, NULL);
  device->mmd_dispatch->aocl_mmd_get_info(device->handle,
                                          AOCL_MMD_PLL_INTERFACES, sizeof(int),
                                          &pll_interface, NULL);
  device->mmd_dispatch->aocl_mmd_get_info(device->handle,
                                          AOCL_MMD_MEMORY_INTERFACE,
                                          sizeof(int), &memory_interface, NULL);
  ACL_HAL_DEBUG_MSG_VERBOSE(
      1, "HAL : Found kernel, pll, and memory interfaces: %d %d %d\n",
      kernel_interface, pll_interface, memory_interface);

  // For now require pll dynamic reconfiguration
  if (!(kernel_interface >= 0 && memory_interface >= 0) ||
      (kernel_interface == memory_interface) ||
      (kernel_interface == pll_interface) ||
      (pll_interface == memory_interface)) {
    printf("Error mmd_get_info: handles for kernel, pll, and memory must be "
           "unique and greater than 0\n");
  }
  device->mmd_dispatch->aocl_mmd_set_status_handler(
      device->handle, acl_hal_mmd_status_handler, NULL);

  kern[physical_device_id].physical_device_id = physical_device_id;

  bsp_io_kern[physical_device_id].debug_verbosity = debug_verbosity;

  // Initialize PLL
  if (pll_interface >= 0) {
    bsp_io_pll[physical_device_id].device_info = device;
    bsp_io_pll[physical_device_id].read = acl_pll_read;
    bsp_io_pll[physical_device_id].write = acl_pll_write;
    bsp_io_pll[physical_device_id].get_time_ns = acl_bsp_get_timestamp;
    bsp_io_pll[physical_device_id].printf = printf;
    bsp_io_pll[physical_device_id].debug_verbosity =
        bsp_io_kern[physical_device_id].debug_verbosity;
    info_assert(acl_pll_init(&pll[physical_device_id],
                             bsp_io_pll[physical_device_id], "") == 0,
                "Failed to read PLL config");

    // If environment override set use it, and disable m_freq_per_kernel
    acl_hal_mmd_pll_override(physical_device_id);

    // Sanity check that PLL is locked
    assert(acl_pll_is_locked(&pll[physical_device_id]));
  }

  // Initialize Kernel Interface
  bsp_io_kern[physical_device_id].device_info = device;
  bsp_io_kern[physical_device_id].read = acl_kernel_if_read;
  bsp_io_kern[physical_device_id].write = acl_kernel_if_write;
  bsp_io_kern[physical_device_id].get_time_ns = acl_bsp_get_timestamp;
  bsp_io_kern[physical_device_id].printf = printf;

  info_assert(acl_kernel_if_init(&kern[physical_device_id],
                                 bsp_io_kern[physical_device_id], sys) == 0,
              "Failed to initialize kernel interface");

  acl_kernel_if_reset(&kern[physical_device_id]);

  // Register interrupt handlers
  // Set kernel interrupt handler
  interrupt_user_data[physical_device_id] = physical_device_id;
  device->mmd_dispatch->aocl_mmd_set_interrupt_handler(
      device->handle, acl_hal_mmd_kernel_interrupt,
      &interrupt_user_data[physical_device_id]);

  // ECC is handled by the device_interrupt
  if (device->mmd_dispatch->aocl_mmd_set_device_interrupt_handler != NULL) {
    device->mmd_dispatch->aocl_mmd_set_device_interrupt_handler(
        device->handle, acl_hal_mmd_device_interrupt,
        &interrupt_user_data[physical_device_id]);
  }

  {
    // If we are using an old MMD that doesn't support these attributes, assume
    // half duplex
    unsigned int concurrent_reads = 1, concurrent_writes = 1,
                 max_inflight_mem_ops = 1;

    if (!MMDVERSION_LESSTHAN(mmd_dispatch_for_board->mmd_version, 18.1)) {
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_CONCURRENT_READS, sizeof(int),
          &concurrent_reads, NULL);
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_CONCURRENT_WRITES, sizeof(int),
          &concurrent_writes, NULL);
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_CONCURRENT_READS_OR_WRITES, sizeof(int),
          &max_inflight_mem_ops, NULL);
      // Sanity checking the output of the BSP
      // Other combinations may have ambiguous ways of interpreting the data
      assert(concurrent_reads == concurrent_writes);
      assert(max_inflight_mem_ops == concurrent_reads + concurrent_writes ||
             max_inflight_mem_ops == concurrent_reads);
    }
    sys->device[physical_device_id].physical_device_id = physical_device_id;
    sys->device[physical_device_id].concurrent_reads = concurrent_reads;
    sys->device[physical_device_id].concurrent_writes = concurrent_writes;
    sys->device[physical_device_id].max_inflight_mem_ops = max_inflight_mem_ops;
  }

  {
    // not supported on legacy devices
    unsigned int host_capabilities = 0, shared_capabilities = 0,
                 device_capabilities = 0;
    if (!MMDVERSION_LESSTHAN(mmd_dispatch_for_board->mmd_version, 20.3)) {
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_HOST_MEM_CAPABILITIES, sizeof(unsigned),
          &host_capabilities, NULL);
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_SHARED_MEM_CAPABILITIES, sizeof(unsigned),
          &shared_capabilities, NULL);
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_DEVICE_MEM_CAPABILITIES, sizeof(unsigned),
          &device_capabilities, NULL);
    }

    sys->device[physical_device_id].host_capabilities =
        acl_convert_mmd_capabilities(host_capabilities);
    sys->device[physical_device_id].shared_capabilities =
        acl_convert_mmd_capabilities(shared_capabilities);
    sys->device[physical_device_id].device_capabilities =
        acl_convert_mmd_capabilities(device_capabilities);
  }

  {
    size_t min_host_mem_alignment = 2097152;
    if (!MMDVERSION_LESSTHAN(mmd_dispatch_for_board->mmd_version, 20.3)) {
      device->mmd_dispatch->aocl_mmd_get_info(
          device->handle, AOCL_MMD_MIN_HOST_MEMORY_ALIGNMENT, sizeof(size_t),
          &min_host_mem_alignment, NULL);
    }
    sys->device[physical_device_id].min_host_mem_alignment =
        min_host_mem_alignment;
  }

  // Post-PLL config init function - at this point, it's safe to talk to the
  // kernel CSR registers.
  if (acl_kernel_if_post_pll_config_init(&kern[physical_device_id]))
    return 0;

  return 1;
}

void l_close_device(unsigned int physical_device_id,
                    acl_mmd_dispatch_t *mmd_dispatch_for_board) {
  acl_assert_locked();

  mmd_dispatch_for_board->aocl_mmd_close(
      device_info[physical_device_id].handle);

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Closing device %d\n", physical_device_id);

  return;
}

static acl_mmd_dispatch_t *get_msim_mmd_layer() {
#ifdef _WIN32

  const char *acl_root_dir = acl_getenv("INTELFPGAOCLSDKROOT");
  info_assert(acl_root_dir,
              "INTELFPGAOCLSDKROOT environment variable is missing!");
  const std::string mmd_lib_name_aoc_str =
      std::string(acl_root_dir) + "\\host\\windows64\\bin\\aoc_cosim_mmd.dll";
  const std::string mmd_lib_name_ipa_str =
      std::string(acl_root_dir) + "\\host\\windows64\\bin\\ipa_cosim_mmd.dll";

  const char *mmd_lib_name_aoc = mmd_lib_name_aoc_str.c_str();
  const char *mmd_lib_name_ipa = mmd_lib_name_ipa_str.c_str();
  const char *sym_name = "msim_mmd_layer";
#else
  const char *mmd_lib_name_aoc = "libaoc_cosim_mmd.so";
  const char *mmd_lib_name_ipa = "libipa_cosim_mmd.so";
  const char *sym_name = "msim_mmd_layer";
#endif

  const char *ipa_simulator = acl_getenv("ACL_IPA_SIM");
  const char *mmd_lib_name =
      ipa_simulator ? mmd_lib_name_ipa : mmd_lib_name_aoc;

  char *error_msg = nullptr;
  auto *mmd_lib = my_dlopen(mmd_lib_name, &error_msg);
  typedef acl_mmd_dispatch_t *(*fcn_type)();
  if (!mmd_lib) {
    std::cout << "Error: Could not load simulation MMD library "
              << mmd_lib_name;
    if (error_msg && error_msg[0] != '\0') {
      std::cout << " (error_msg: " << error_msg << ")";
    }
    std::cout << "\n";
    return nullptr;
  }
  auto *sym = my_dlsym(mmd_lib, sym_name, &error_msg);
  if (!sym) {
    std::cout << "Error: Symbol " << sym_name
              << " not found in simulation MMD library ";
    if (error_msg && error_msg[0] != '\0') {
      std::cout << "(message: " << error_msg << ")";
    }
    std::cout << "\n";
    return nullptr;
  }

  // Now call the function. Ignore the Windows cast to fcn pointer
  // warning/error.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4055)
#endif
  return ((fcn_type)sym)();
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

ACL_HAL_EXPORT const acl_hal_t *
acl_mmd_get_system_definition(acl_system_def_t *sys,
                              acl_mmd_library_names_t *_libraries_to_load) {
  char *hal_debug_var;
  int num_boards;
  static char
      buf[MAX_BOARD_NAMES_LEN]; // This is a bit subtle, pointers to device
                                // names might get cached by various routines
  char *ptr, *saveptr;
  int use_offline_only;

#ifdef _WIN32
  // We're really relying on this being called before anything else
  LARGE_INTEGER li;
  QueryPerformanceFrequency(&li);
  m_ticks_per_second = (cl_ulong)li.QuadPart;
  assert(m_ticks_per_second != 0);
#endif
  acl_assert_locked();

  hal_debug_var = getenv("ACL_HAL_DEBUG");
  if (hal_debug_var) {
    debug_verbosity = atoi(hal_debug_var);
    ACL_HAL_DEBUG_MSG_VERBOSE(0, "Setting debug level to %u\n",
                              debug_verbosity);
  }

  ACL_HAL_DEBUG_MSG_VERBOSE(1, "%s\n", ACL_BANNER);

#ifdef _WIN32
#define MAX_PATH_LEN 512
  char lib_path[MAX_PATH_LEN];
  HMODULE hm = NULL;
  if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)&acl_mmd_get_system_definition, &hm)) {
    ACL_HAL_DEBUG_MSG_VERBOSE(
        1, "Error: Could not retrieve library path information \n");
  } else {
    if (!GetModuleFileName(hm, lib_path, sizeof(lib_path))) {
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1, "Error: Could not retrieve library path information \n");
    } else {
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "Path to runtime library: %s \n ", lib_path);
    }
  }
#else
  Dl_info lib_info;
  if (!dladdr((const void *)&acl_mmd_get_system_definition, &lib_info)) {
    ACL_HAL_DEBUG_MSG_VERBOSE(
        1, "Error: Could not retrieve library path information \n");
  } else {
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "Path to runtime library: %s \n ",
                              lib_info.dli_fname);
  }
#endif

  // Dynamically load board mmd & symbols
  (void)acl_get_offline_device_user_setting(&use_offline_only);
  if (use_offline_only == ACL_CONTEXT_MPSIM) {

    // Substitute the simulator MMD layer.
    auto *result = get_msim_mmd_layer();
    if (!result)
      return nullptr;
    else
      internal_mmd_dispatch.push_back(*result);

    ACL_HAL_DEBUG_MSG_VERBOSE(1, "Use simulation MMD\n");
    num_board_pkgs = 1;
  } else if (IS_VALID_FUNCTION(aocl_mmd_get_offline_info)) {
    num_board_pkgs = 1; // It is illegal to define more than one board package
                        // while statically linking a board package to the host.
    internal_mmd_dispatch.resize(num_board_pkgs);
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "Board MMD is statically linked\n");

    internal_mmd_dispatch[0].library_name = "runtime_static";
    internal_mmd_dispatch[0].mmd_library = nullptr;

    internal_mmd_dispatch[0].aocl_mmd_get_offline_info =
        aocl_mmd_get_offline_info;

    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_get_offline_info,
                         1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_get_info, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_open, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_close, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0],
                         aocl_mmd_set_interrupt_handler, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0],
                         aocl_mmd_set_device_interrupt_handler, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_set_status_handler,
                         1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_yield, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_read, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_write, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_copy, 1);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_reprogram, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_shared_mem_alloc,
                         0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_shared_mem_free, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_hostchannel_create,
                         0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_hostchannel_destroy,
                         0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0],
                         aocl_mmd_hostchannel_get_buffer, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0],
                         aocl_mmd_hostchannel_ack_buffer, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_program, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_host_alloc, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_free, 0);
    ADD_STATIC_FN_TO_HAL(internal_mmd_dispatch[0], aocl_mmd_shared_alloc, 0);
  } else if (_libraries_to_load) {
    for (auto ipass = 0; ipass < 2; ++ipass) {
      cl_bool load_libraries;
      size_t num_boards_found = 0;
      acl_mmd_library_names_t *next_library;
      if (ipass == 0) {
        load_libraries = CL_FALSE;
      } else {
        load_libraries = CL_TRUE;
      }
      next_library = _libraries_to_load;
      while (next_library) {
        const auto &library_name = next_library->library_name;
        ACL_HAL_DEBUG_MSG_VERBOSE(1,
                                  "Trying to dynamically load board MMD %s\n",
                                  library_name.c_str());
        auto result_status = l_load_single_board_library(
            library_name.c_str(), num_boards_found, load_libraries);
        if (!result_status) {
          return NULL;
        }
        next_library = next_library->next;
      }
      if (!load_libraries) {
        num_board_pkgs = num_boards_found;
        internal_mmd_dispatch.resize(num_board_pkgs);
      }
    }
  } else {
    cl_bool result_status;
    // Call twice. Once to count number of libraries and once to load them
    result_status = l_load_board_libraries(CL_FALSE);
    if (!result_status) {
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1, "Error: Could not load FPGA board libraries successfully.\n");
      return NULL;
    }
    result_status = l_load_board_libraries(CL_TRUE);
    if (!result_status) {
      printf("Error: Could not load FPGA board libraries successfully.\n");
      return NULL;
    }
  }

  sys->num_devices = 0;
  num_physical_devices = 0;
  for (unsigned iboard = 0; iboard < num_board_pkgs; ++iboard) {
    internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
        AOCL_MMD_VERSION, sizeof(buf), buf, NULL);
    buf[sizeof(buf) - 1] = 0;
    internal_mmd_dispatch[iboard].mmd_version = atof(buf);
    min_MMD_version =
        (!MMDVERSION_LESSTHAN(min_MMD_version,
                              internal_mmd_dispatch[iboard].mmd_version))
            ? internal_mmd_dispatch[iboard].mmd_version
            : min_MMD_version;
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Getting info version: %s\n", buf);

    if (MMDVERSION_LESSTHAN(
            atof(AOCL_MMD_VERSION_STRING),
            internal_mmd_dispatch[iboard].mmd_version) || // MMD newer than HAL
        MMDVERSION_LESSTHAN(internal_mmd_dispatch[iboard].mmd_version,
                            14.0)) // Before this wasn't forward compatible
    {
      printf("  Runtime version: %s\n", AOCL_MMD_VERSION_STRING);
      printf("  MMD version:     %s\n", buf);
      fflush(stdout);
      assert(0 && "MMD version mismatch");
    }

    // Disable yield as initialization
    acl_hal_mmd.yield = NULL;

    // Dump offline info
    if (debug_verbosity > 0) {
      internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
          AOCL_MMD_VENDOR_NAME, sizeof(buf), buf, NULL);
      buf[sizeof(buf) - 1] = 0;
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Getting info vendor: %s\n", buf);
      internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
          AOCL_MMD_NUM_BOARDS, sizeof(int), &num_boards, NULL);
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Getting info num_boards: %d\n",
                                num_boards);
      internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
          AOCL_MMD_BOARD_NAMES, sizeof(buf), buf, NULL);
      buf[sizeof(buf) - 1] = 0;
      ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL : Getting info boards: %s\n", buf);
    }

    internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
        AOCL_MMD_BOARD_NAMES, MAX_BOARD_NAMES_LEN, buf, NULL);
    buf[MAX_BOARD_NAMES_LEN - 1] = 0;
    // Probe the platform devices by going through all the possibilities in the
    // semicolon delimited list
    sys->num_devices = 0;
    ptr = acl_strtok(buf, ";", &saveptr);
    while (ptr != NULL) {
      num_physical_devices++;
      ptr = acl_strtok(NULL, ";", &saveptr);
    }
    sys->num_devices = num_physical_devices;

    ACL_HAL_DEBUG_MSG_VERBOSE(1, "Found %d devices\n", sys->num_devices);
  }
  return &acl_hal_mmd;
}

int acl_hal_mmd_try_devices(cl_uint num_devices, const cl_device_id *devices,
                            cl_platform_id platform) {

  unsigned int physical_device_id = 0;
  unsigned int idevice;
  unsigned int failed_device_id;
  char *ptr, *saveptr1, *saveptr2;
  static char
      buf[MAX_BOARD_NAMES_LEN]; // This is a bit subtle, pointers to device
                                // names might get cached by various routines

  for (unsigned iboard = 0; iboard < num_board_pkgs; ++iboard) {
    internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
        AOCL_MMD_BOARD_NAMES, MAX_BOARD_NAMES_LEN, buf, NULL);
    buf[MAX_BOARD_NAMES_LEN - 1] = 0; // ensure it's null terminated

    // query for the polling/interrupt mode for each bsp
    int uses_yield = 0;
    if (!MMDVERSION_LESSTHAN(internal_mmd_dispatch[iboard].mmd_version, 14.1)) {
      internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
          AOCL_MMD_USES_YIELD, sizeof(int), &uses_yield, NULL);
    }
    // Probe the platform devices by going through all the possibilities in the
    // semicolon delimited list
    ptr = acl_strtok(buf, ";", &saveptr1);
    while (ptr != NULL) {
      // Now we must go through the device list to see if one of them matches
      // this physical id:
      for (idevice = 0; idevice < num_devices; idevice++) {
        if (devices[idevice] == &(platform->device[physical_device_id])) {
          if (devices[idevice]->opened_count == 0) {
            if (l_try_device(physical_device_id, ptr,
                             platform->initial_board_def,
                             &(internal_mmd_dispatch[iboard]))) {
              ACL_HAL_DEBUG_MSG_VERBOSE(1, "Device: %s device is available\n",
                                        ptr);
              if (uses_yield_ref != -1 && uses_yield != uses_yield_ref) {
                fprintf(stderr,
                        " HAL : Multiple BSPs are installed with a mixing of "
                        "using polling and using interrupts.\n");
                fprintf(stderr,
                        "       This combination of BSPs is not supported by "
                        "the Intel(R) FPGA SDK for OpenCL(TM) runtime.\n");
                fprintf(stderr,
                        "       Uninstall the incompatible BSP(s) using "
                        "\"aocl uninstall <board_package_path>\".\n");
                failed_device_id = physical_device_id;
                goto failed;
              }
              if (uses_yield) {
                // Enable yield function.  In 14.0 this function was never
                // called, in 14.1 it is called only if the offline attribute is
                // set.
                if (!MMDVERSION_LESSTHAN(
                        internal_mmd_dispatch[iboard].mmd_version, 14.1)) {
                  ((acl_hal_t *)acl_get_hal())->yield = acl_hal_mmd_yield;
                }
              }
              uses_yield_ref = uses_yield;
            } else {
              ACL_HAL_DEBUG_MSG_VERBOSE(
                  1, "Device: %s device is NOT available\n", ptr);
              failed_device_id = physical_device_id;
              goto failed;
            }
          }
          devices[idevice]->opened_count++;
        }
      }
      ptr = acl_strtok(NULL, ";", &saveptr1);
      physical_device_id++;
    }
  }
  return 0;

failed:

  // Loop through exactly as above, closing devices until we hit the index that
  // failed. Then return an error.
  physical_device_id = 0;

  for (unsigned iboard = 0; iboard < num_board_pkgs; ++iboard) {

    internal_mmd_dispatch[iboard].aocl_mmd_get_offline_info(
        AOCL_MMD_BOARD_NAMES, MAX_BOARD_NAMES_LEN, buf, NULL);
    buf[MAX_BOARD_NAMES_LEN - 1] = 0; // ensure it's null terminated

    // Probe the platform devices by going through all the possibilities in the
    // semicolon delimited list
    ptr = acl_strtok(buf, ";", &saveptr2);
    while (ptr != NULL) {
      if (physical_device_id == failed_device_id) {
        // We've gotten to the device that failed, which means we've closed all
        // the devices that we opened. We can now return:
        return CL_DEVICE_NOT_AVAILABLE;
      }

      // Now we must go through the device list to see if one of them matches
      // this physical id:
      for (idevice = 0; idevice < num_devices; idevice++) {
        if (devices[idevice] == &(platform->device[physical_device_id])) {
          if (devices[idevice]->opened_count == 1) { // we just opened it
            l_close_device(physical_device_id,
                           &(internal_mmd_dispatch[iboard]));
          }
          devices[idevice]->opened_count--;
        }
      }
      ptr = acl_strtok(NULL, ";", &saveptr2);
      physical_device_id++;
    }
  }

  assert(0 && "Should never get here");
  return 1; // Prevent compiler warnings
}

int acl_hal_mmd_close_devices(cl_uint num_devices,
                              const cl_device_id *devices) {
  cl_uint idevice;

  for (idevice = 0; idevice < num_devices; idevice++) {
    assert(devices[idevice]->opened_count > 0);
    devices[idevice]->opened_count--;

    // We actually close it if there are no more contexts with it opened:
    if (devices[idevice]->opened_count == 0) {
      unsigned int physical_device_id =
          devices[idevice]->def.physical_device_id;
      l_close_device(physical_device_id,
                     device_info[physical_device_id].mmd_dispatch);
    }
  }
  return 0;
}

void acl_hal_mmd_init_device(const acl_system_def_t *sysdef) {
  // Removing Windows warning
  sysdef = sysdef;

  // Perhaps tell kernel interface what cl_device looks like
}

void acl_hal_mmd_kernel_interrupt(int handle_in, void *user_data) {
  unsigned physical_device_id;
#ifdef __linux__
  // Callbacks received from non-dma transfers.
  // (those calls are not initiated by a signal handler, so we need to block all
  // signals here to avoid simultaneous calls to signal handler.)
  acl_sig_block_signals(); // Call before acl_sig_started. Must call
                           // acl_sig_unblock_signals after acl_sig_finished.
#endif
  acl_sig_started();
  // NOTE: all exit points of this function must first call acl_sig_finished()

  // Removing Windows warning
  user_data = user_data;
  // Make sure the combination of handle_in and device id match
  assert(user_data != NULL);
  physical_device_id = *((unsigned *)user_data);

  if (device_info[physical_device_id].handle == handle_in) {
    assert(acl_kernel_if_is_valid(&kern[physical_device_id]));
    acl_kernel_if_update_status(&kern[physical_device_id]);
    acl_sig_finished();
#ifdef __linux__
    // Unblocking the signals we blocked
    acl_sig_unblock_signals();
#endif
    return;
  }

  fprintf(stderr, "physical_device_id= %d, handle_in = %d\n",
          physical_device_id, handle_in);
  info_assert(0, "Failed to find handle ");
}

void acl_hal_mmd_device_interrupt(int handle_in,
                                  aocl_mmd_interrupt_info *data_in,
                                  void *user_data) {
  unsigned physical_device_id;
#ifdef __linux__
  // Callbacks received from non-dma transfers.
  //(those calls are not initiated by a signal handler, so we need to block all
  // signals
  // here to avoid simultaneous calls to signal handler.)
  acl_sig_block_signals(); // Call before acl_sig_started. Must call
                           // acl_sig_unblock_signals after acl_sig_finished.
#endif
  acl_sig_started();
  // NOTE: all exit points of this function must first call acl_sig_finished()

  // Make sure the combination of handle_in and device id match
  assert(user_data != NULL);
  assert(data_in != NULL);

  physical_device_id = *((unsigned *)user_data);

  if (device_info[physical_device_id].handle == handle_in) {
    acl_device_update_fn(physical_device_id, data_in->exception_type,
                         data_in->user_private_info, data_in->user_cb);
    acl_sig_finished();
#ifdef __linux__
    // Unblocking the signals we blocked
    acl_sig_unblock_signals();
#endif
    return;
  }

  fprintf(stderr, "physical_device_id= %d, handle_in = %d\n",
          physical_device_id, handle_in);
  info_assert(0, "Failed to find handle ");
}

void acl_hal_mmd_status_handler(int handle, void *user_data, aocl_mmd_op_t op,
                                int status) {
#ifdef __linux__
  // Callbacks received from non-dma transfers.
  //(those calls are not initiated by a signal handler, so we need to block all
  // signals)
  // here to avoid simultaneous  calls to signal handler.)
  acl_sig_block_signals(); // Call before acl_sig_started. Must call
                           // acl_sig_unblock_signals after acl_sig_finished.
#endif
  acl_sig_started();
  // NOTE: all exit points of this function must first call acl_sig_finished()
  // Removing Windows warning
  handle = handle;
  user_data = user_data;
  assert(status == 0);
  acl_event_update_fn((cl_event)op, CL_COMPLETE);

  acl_sig_finished();
#ifdef __linux__
  // Unblocking the signals we blocked
  acl_sig_unblock_signals();
#endif
}

void acl_hal_mmd_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf) {
  acl_assert_locked();
  acl_event_update_fn = event_update;
  acl_kernel_if_register_callbacks(kernel_update, profile_update,
                                   process_printf);
  acl_kernel_update_fn = kernel_update; // Keeping a copy to be able to use it
                                        // for reseting the kernels.
  acl_profile_fn = profile_update;
  acl_device_update_fn = device_update;
}

void acl_hal_mmd_yield(cl_uint num_devices, const cl_device_id *devices) {
  cl_uint idevice;
  unsigned int physical_device_id;
  int keep_going;
  acl_assert_locked();

  // Check each device and try to perform some slice of useful work
  do {
    keep_going = 0;
    for (idevice = 0; idevice < num_devices; idevice++) {
      assert(devices[idevice]->opened_count > 0);

      physical_device_id = devices[idevice]->def.physical_device_id;
      acl_kernel_if_check_kernel_status(&kern[physical_device_id]);
      if (device_info[physical_device_id].mmd_dispatch->aocl_mmd_yield(
              device_info[physical_device_id].handle))
        keep_going = 1;
    }
  } while (keep_going);
}

// Host to host memory transfer (presently blocking)
void acl_hal_mmd_copy_hostmem_to_hostmem(cl_event event, const void *src,
                                         void *dest, size_t size) {
  acl_assert_locked();

  // Verify the callbacks are valid
  assert(acl_event_update_fn != NULL);

  // Host to host has nothing to do with the device
  acl_event_update_fn(event, CL_RUNNING);
  if (dest != src) {
    safe_memcpy(dest, src, size, size, size);
  }
  acl_event_update_fn(event, CL_COMPLETE);
}

// Host to device-global-memory write
void acl_hal_mmd_copy_hostmem_to_globalmem(cl_event event, const void *src,
                                           void *dest, size_t size) {
  int s;
  unsigned int physical_device_id;
  acl_assert_locked();
  physical_device_id = ACL_GET_PHYSICAL_ID(dest);
  assert(physical_device_id < num_physical_devices);

  ACL_HAL_DEBUG_MSG_VERBOSE(4, "HAL Writing to memory: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);

  // Verify the callbacks are valid
  assert(acl_event_update_fn != NULL);
  acl_event_update_fn(event, CL_RUNNING); // MMD device will send complete

  s = device_info[physical_device_id].mmd_dispatch->aocl_mmd_write(
      device_info[physical_device_id].handle, (aocl_mmd_op_t)event, size, src,
      memory_interface, (size_t)ACL_STRIP_PHYSICAL_ID(dest));
  assert(s == 0 && "mmd read/write failed");
}

// Device-global to host memory read
void acl_hal_mmd_copy_globalmem_to_hostmem(cl_event event, const void *src,
                                           void *dest, size_t size) {
  int s;
  unsigned int physical_device_id;
  acl_assert_locked();
  physical_device_id = ACL_GET_PHYSICAL_ID(src);
  assert(physical_device_id < num_physical_devices);

  ACL_HAL_DEBUG_MSG_VERBOSE(4,
                            "HAL Reading from memory: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);

  // Verify the callbacks are valid
  assert(acl_event_update_fn != NULL);
  acl_event_update_fn(event, CL_RUNNING); // MMD device will send complete

  s = device_info[physical_device_id].mmd_dispatch->aocl_mmd_read(
      device_info[physical_device_id].handle, (aocl_mmd_op_t)event, size, dest,
      memory_interface, (size_t)ACL_STRIP_PHYSICAL_ID(src));
  assert(s == 0 && "mmd read/write failed");
}

static int src_dev_done;
static int dst_dev_done;

static void l_dev_to_dev_copy_handler(int handle, void *user_data,
                                      aocl_mmd_op_t op, int status) {
  acl_sig_started();
  // NOTE: all exit points of this function must first call acl_sig_finished()

  // Removing Windows warning
  user_data = user_data;
  handle = handle;
  status = status;
  if (op == (aocl_mmd_op_t)&src_dev_done) {
    src_dev_done = 1;
  } else if (op == (aocl_mmd_op_t)&dst_dev_done) {
    dst_dev_done = 1;
  } else
    assert(0 && "dev_to_dev_copy got unexpected event");

  acl_sig_finished();
}

// Device-global to device-global
void acl_hal_mmd_copy_globalmem_to_globalmem(cl_event event, const void *src,
                                             void *dest, size_t size) {
  int s = -1;
  unsigned int physical_device_id_src;
  unsigned int physical_device_id_dst;
  acl_assert_locked();

  physical_device_id_src = ACL_GET_PHYSICAL_ID(src);
  assert(physical_device_id_src < num_physical_devices);
  physical_device_id_dst = ACL_GET_PHYSICAL_ID(dest);
  assert(physical_device_id_dst < num_physical_devices);

  ACL_HAL_DEBUG_MSG_VERBOSE(4, "HAL Copying memory: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);

  // Verify the callbacks are valid
  assert(acl_event_update_fn != NULL);
  acl_event_update_fn(event, CL_RUNNING); // MMD device will send complete

  if (physical_device_id_src == physical_device_id_dst) {
    // Let the MMD provider do the intra-device copy.
    s = device_info[physical_device_id_src].mmd_dispatch->aocl_mmd_copy(
        device_info[physical_device_id_src].handle, (aocl_mmd_op_t)event, size,
        memory_interface, (size_t)ACL_STRIP_PHYSICAL_ID(src),
        (size_t)ACL_STRIP_PHYSICAL_ID(dest));
  } else {
    // Copy from device to device via host memory.

    // Let's use a small static buffer as we move data from device to device
#define BLOCK_SIZE (8 * 1024 * 1024)
#ifdef _WIN32
    __declspec(align(128)) static unsigned char data[2][BLOCK_SIZE];
#else
    static unsigned char data[2][BLOCK_SIZE] __attribute__((aligned(128)));
#endif

    size_t transfer_size;
    size_t transfer_size_next;
    int buffer;

    // We're going to block here
    transfer_size = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
    transfer_size_next = 0;

    // Read the initial block into data[0]
    s = device_info[physical_device_id_src].mmd_dispatch->aocl_mmd_read(
        device_info[physical_device_id_src].handle, NULL, transfer_size,
        &data[0][0], memory_interface, (size_t)ACL_STRIP_PHYSICAL_ID(src));
    src = (const char *)src + transfer_size;
    size -= transfer_size;

    buffer = 0;

    device_info[physical_device_id_src]
        .mmd_dispatch->aocl_mmd_set_status_handler(
            device_info[physical_device_id_src].handle,
            l_dev_to_dev_copy_handler, NULL);
    device_info[physical_device_id_dst]
        .mmd_dispatch->aocl_mmd_set_status_handler(
            device_info[physical_device_id_dst].handle,
            l_dev_to_dev_copy_handler, NULL);

    do {

      transfer_size_next = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;

      src_dev_done = 0;
      dst_dev_done = 0;

      device_info[physical_device_id_dst].mmd_dispatch->aocl_mmd_write(
          device_info[physical_device_id_dst].handle, &dst_dev_done,
          transfer_size, &data[buffer][0], memory_interface,
          (size_t)ACL_STRIP_PHYSICAL_ID(dest));

      if (transfer_size_next)
        device_info[physical_device_id_src].mmd_dispatch->aocl_mmd_read(
            device_info[physical_device_id_src].handle, &src_dev_done,
            transfer_size_next, &data[1 - buffer][0], memory_interface,
            (size_t)ACL_STRIP_PHYSICAL_ID(src));
      else
        src_dev_done = 1;

      while (!(src_dev_done && dst_dev_done)) {
        device_info[physical_device_id_src].mmd_dispatch->aocl_mmd_yield(
            device_info[physical_device_id_src].handle);
        device_info[physical_device_id_dst].mmd_dispatch->aocl_mmd_yield(
            device_info[physical_device_id_dst].handle);
      }

      src = (const char *)src + transfer_size_next;
      dest = (char *)dest + transfer_size;

      size -= transfer_size_next;
      transfer_size = transfer_size_next;

      // Flip the buffers
      buffer = 1 - buffer;
    } while (transfer_size_next > 0);
    device_info[physical_device_id_src]
        .mmd_dispatch->aocl_mmd_set_status_handler(
            device_info[physical_device_id_src].handle,
            acl_hal_mmd_status_handler, NULL);
    device_info[physical_device_id_dst]
        .mmd_dispatch->aocl_mmd_set_status_handler(
            device_info[physical_device_id_dst].handle,
            acl_hal_mmd_status_handler, NULL);
    acl_event_update_fn(event, CL_COMPLETE);
  }
  assert(s == 0 && "mmd read/write failed");
}

// Launch a kernel
void acl_hal_mmd_launch_kernel(unsigned int physical_device_id,
                               acl_kernel_invocation_wrapper_t *wrapper) {
  acl_assert_locked();

  const auto &streaming_args = wrapper->streaming_args;
  if (!streaming_args.empty()) {
    device_info[physical_device_id]
        .mmd_dispatch->aocl_mmd_simulation_streaming_kernel_args(
            device_info[physical_device_id].handle, streaming_args);
  }

  acl_kernel_if_launch_kernel(&kern[physical_device_id], wrapper);
}

void acl_hal_mmd_unstall_kernel(unsigned int physical_device_id,
                                int activation_id) {
  acl_assert_locked();
  acl_kernel_if_unstall_kernel(&kern[physical_device_id], activation_id);
}

static void update_simulator(int handle, unsigned int physical_device_id,
                             const acl_device_def_autodiscovery_t &dev) {
  std::vector<aocl_mmd_memory_info_t> mem_info(dev.num_global_mem_systems);
  for (unsigned i = 0; i < mem_info.size(); ++i) {
    mem_info[i].start =
        reinterpret_cast<uintptr_t>(dev.global_mem_defs[i].range.begin);
    mem_info[i].size =
        reinterpret_cast<uintptr_t>(dev.global_mem_defs[i].range.next);
  }

  device_info[physical_device_id].mmd_dispatch->aocl_mmd_simulation_device_info(
      handle, static_cast<int>(mem_info.size()), mem_info.data());
}

// Program the FPGA device with the given binary.
// the status returned:
// 0 : program succeeds
// ACL_PROGRAM_FAILED (-1) : program failed
// ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM (-2) : memory-preserved program failed
int acl_hal_mmd_program_device(unsigned int physical_device_id,
                               const acl_device_def_t *devdef,
                               const struct acl_pkg_file *binary,
                               int acl_program_mode) {
  char *sof;
  size_t sof_len;
  bool is_simulator;
  static cl_bool msg_printed = CL_FALSE;
  int temp_handle = 0;
  acl_assert_locked();

  if (acl_pkg_section_exists(binary, ACL_PKG_SECTION_FPGA_BIN, &sof_len)) {
    if (sof_len < MIN_SOF_SIZE) {
      printf(" mmd: program_device:  fpga.bin is too small, only %lu bytes.\n",
             (long)sof_len);
      fflush(stdout);
      return ACL_PROGRAM_FAILED;
    }
  }
  // Get reference to the fpga.bin, just in case.
  ACL_HAL_DEBUG_MSG_VERBOSE(
      1, " mmd: program_device:  Get fpga.bin from binary...\n");
  if (!acl_pkg_read_section_transient(binary, ACL_PKG_SECTION_FPGA_BIN, &sof)) {
    printf("HAL program_device:  Could not get fpga.bin out of binary\n");
    fflush(stdout);
    return ACL_PROGRAM_FAILED;
  }

  size_t pll_config_len = 0;
  std::string pll_config;
  if (acl_pkg_section_exists(binary, ACL_PKG_SECTION_PLL_CONFIG,
                             &pll_config_len)) {
    if (pll_config_len < MIN_PLL_CONFIG_SIZE) {
      printf(
          " mmd: program_device:  pll_config is too small, only %lu bytes.\n",
          (long)pll_config_len);
      fflush(stdout);
      return ACL_PROGRAM_FAILED;
    }
    char *pll_config_read = nullptr;
    if (!acl_pkg_read_section_transient(binary, ACL_PKG_SECTION_PLL_CONFIG,
                                        &pll_config_read)) {
      printf("HAL program_device:  Could not get pll_config out of binary\n");
      fflush(stdout);
      return ACL_PROGRAM_FAILED;
    }
    // The returned pll_config_read is not null terminated so have to
    // assign the first pll_config_len characters to pll_config.
    pll_config.assign(pll_config_read, pll_config_read + pll_config_len);
  }

  if (debug_verbosity) {
    // Show the hash and version info, if they exist.
    size_t version_len = 0;
    size_t hash_len = 0;
#define EXPECTED_HASH_LEN 40
#define MAX_VERSION_LEN 40
    char hash[EXPECTED_HASH_LEN + 1];
    char version[MAX_VERSION_LEN + 1];
    if (acl_pkg_section_exists(binary, ACL_PKG_SECTION_ACL_VERSION,
                               &version_len) &&
        version_len <= MAX_VERSION_LEN &&
        acl_pkg_read_section(binary, ACL_PKG_SECTION_ACL_VERSION, version,
                             version_len)) {
      version[version_len] = 0; // terminate with NUL
      printf("mmd: binary built with aocl version %s\n", version);
    } else {
      printf("mmd: no aocl version stored in binary\n");
    }
    if (acl_pkg_section_exists(binary, ACL_PKG_SECTION_HASH, &hash_len)) {
      if (hash_len == EXPECTED_HASH_LEN &&
          acl_pkg_read_section(binary, ACL_PKG_SECTION_HASH, hash, hash_len)) {
        hash[EXPECTED_HASH_LEN] = 0; // terminate with NUL
        printf("mmd: binary hash %s\n", hash);
      } else {
        if (hash_len != EXPECTED_HASH_LEN) {
          printf("mmd: binary hash len should be %d but is %d\n",
                 EXPECTED_HASH_LEN, (int)hash_len);
        } else {
          printf("mmd: Could not get hash from binary\n");
        }
      }
    } else {
      printf("mmd: no hash stored in binary\n");
    }
#undef EXPECTED_HASH_LEN
    fflush(stdout);
  }

  // The message below may interfere with the simulator standard output in some
  // cases (e.g. features/printf/test), where there are multiple kernels and
  // resources are released between kernel runs.  For the simulator, only print
  // this message once. This is a horrible kludge.
  is_simulator = device_info[physical_device_id]
                     .mmd_dispatch->aocl_mmd_simulation_device_info != NULL;
  msg_printed = (cl_bool)CL_FALSE;
  if (!(is_simulator && msg_printed)) {
    ACL_HAL_DEBUG_MSG_VERBOSE(1, "Reprogramming device [%d] with handle %d\n",
                              physical_device_id,
                              device_info[physical_device_id].handle);
    msg_printed = CL_TRUE;
  }

  // mmd check and use the correct reprogram flow accordingly
  // only check the first board package
  if (MMDVERSION_LESSTHAN(
          device_info[physical_device_id].mmd_dispatch->mmd_version, 18.1)) {
    // The mmd version is old. The runtime and hal would have to try reprogram
    // the device with the old flow but first we need to check the
    // aocl_mmd_reprogram is implemented.
    if (!device_info[physical_device_id].mmd_dispatch->aocl_mmd_reprogram) {
      fprintf(stderr, "mmd program_device: The current board support package "
                      "does not support device reprogramming. Exit.\n");
      return ACL_PROGRAM_FAILED;
    }
    if (acl_program_mode == ACL_PROGRAM_PRESERVE_MEM) {
      return ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM;
    } else {
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1,
          "HAL program_device: MMD version [%f] does not match runtime, trying "
          "memory-unpreserved reprogramming.\n",
          device_info[physical_device_id].mmd_dispatch->mmd_version);
      if ((device_info[physical_device_id].handle =
               device_info[physical_device_id].mmd_dispatch->aocl_mmd_reprogram(
                   device_info[physical_device_id].handle, sof, sof_len)) < 0) {
        fprintf(stderr, "mmd program_device: Board reprogram failed\n");
        return ACL_PROGRAM_FAILED;
      }
    }
  } else {
    // The mmd version is up-to-date. Can safely try the new program flow
    // first of all, check if the aocl_mmd_program is implemented
    if (!device_info[physical_device_id].mmd_dispatch->aocl_mmd_program) {
      fprintf(stderr, "mmd program_device: The current board support package "
                      "does not support device reprogramming. Exit.\n");
      return ACL_PROGRAM_FAILED;
    }
    // check if the old reprogram API is implemented in the BSP. If so, exit as
    // error
    if (device_info[physical_device_id].mmd_dispatch->aocl_mmd_reprogram) {
      fprintf(stderr, "mmd program_device: aocl_mmd_reprogram is deprecated! "
                      "Program with aocl_mmd_program instead. Exit.\n");
      return ACL_PROGRAM_FAILED;
    }
    if (acl_program_mode == ACL_PROGRAM_PRESERVE_MEM) {
      // first time try reprogram with memory preserving
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1, "HAL program_device: Trying memory-preserved programming\n");
      temp_handle =
          device_info[physical_device_id].mmd_dispatch->aocl_mmd_program(
              device_info[physical_device_id].handle, sof, sof_len,
              AOCL_MMD_PROGRAM_PRESERVE_GLOBAL_MEM);
      if (temp_handle < 0) {
        // memory-preserved program failed, needs to go back do the save/restore
        // first and come back
        ACL_HAL_DEBUG_MSG_VERBOSE(1, "HAL program_device: memory-preserved "
                                     "reprogramming unsuccessful\n");
        return ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM;
      } else {
        // memory-preserved program success, no need to reprogram again
        device_info[physical_device_id].handle = temp_handle;
      }
    } else {
      ACL_HAL_DEBUG_MSG_VERBOSE(
          1, "HAL program_device: Trying memory-unpreserved programming\n");
      // already know memory-preserved program failed. have done save/restore.
      // Safe to go through memory-unpreserved program
      if ((device_info[physical_device_id].handle =
               device_info[physical_device_id].mmd_dispatch->aocl_mmd_program(
                   device_info[physical_device_id].handle, sof, sof_len, 0)) <
          0) {
        ACL_HAL_DEBUG_MSG_VERBOSE(
            1, "HAL program_device: memory-unpreserved programming failed\n");
        fprintf(stderr, "mmd program_device: Board reprogram failed\n");
        return ACL_PROGRAM_FAILED;
      }
    }
  }
  // Need to remap the IDs after reprogramming
  kern[physical_device_id].io.device_info = &(device_info[physical_device_id]);
  bsp_io_kern[physical_device_id].device_info =
      &(device_info[physical_device_id]);
  bsp_io_pll[physical_device_id].device_info =
      &(device_info[physical_device_id]);

  // Tell the simulator (if present) about global memory sizes.
  if (is_simulator) {
    update_simulator(device_info[physical_device_id].handle, physical_device_id,
                     devdef->autodiscovery_def);
  }

  device_info[physical_device_id].mmd_dispatch->aocl_mmd_set_status_handler(
      device_info[physical_device_id].handle, acl_hal_mmd_status_handler, NULL);
  acl_kernel_if_update(devdef->autodiscovery_def, &kern[physical_device_id]);
  if (pll_interface >= 0) {
    if (acl_pkg_section_exists(binary, ACL_PKG_SECTION_PLL_CONFIG,
                               &pll_config_len)) {
      info_assert(acl_pll_init(&pll[physical_device_id],
                               bsp_io_pll[physical_device_id], pll_config) == 0,
                  "Failed to read PLL config");

    } else {
      info_assert(acl_pll_init(&pll[physical_device_id],
                               bsp_io_pll[physical_device_id], "") == 0,
                  "Failed to read PLL config");
      acl_hal_mmd_pll_override(physical_device_id);
    }
  }
  acl_kernel_if_reset(&kern[physical_device_id]);

  // Register interrupt handlers
  // Set kernel interrupt handler
  interrupt_user_data[physical_device_id] = physical_device_id;
  device_info[physical_device_id].mmd_dispatch->aocl_mmd_set_interrupt_handler(
      device_info[physical_device_id].handle, acl_hal_mmd_kernel_interrupt,
      &interrupt_user_data[physical_device_id]);

  // ECC is handled by the device_interrupt
  if (device_info[physical_device_id]
          .mmd_dispatch->aocl_mmd_set_device_interrupt_handler != NULL) {
    device_info[physical_device_id]
        .mmd_dispatch->aocl_mmd_set_device_interrupt_handler(
            device_info[physical_device_id].handle,
            acl_hal_mmd_device_interrupt,
            &interrupt_user_data[physical_device_id]);
  }
  // Post-PLL config init function - at this point, it's safe to talk to the
  // kernel CSR registers.
  if (acl_kernel_if_post_pll_config_init(&kern[physical_device_id]))
    return -1;

  return 0;
}

int acl_hal_mmd_hostchannel_create(unsigned int physical_device_id,
                                   char *channel_name, size_t num_packets,
                                   size_t packet_size, int direction) {
  int pcie_dev_handle;

  pcie_dev_handle = device_info[physical_device_id].handle;
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_create);
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_hostchannel_create(
          pcie_dev_handle, channel_name, num_packets * packet_size, direction);
}

int acl_hal_mmd_hostchannel_destroy(unsigned int physical_device_id,
                                    int channel_handle) {
  int pcie_dev_handle;

  pcie_dev_handle = device_info[physical_device_id].handle;
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_destroy);
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_hostchannel_destroy(pcie_dev_handle,
                                                  channel_handle);
}

size_t acl_hal_mmd_hostchannel_pull(unsigned int physical_device_id,
                                    int channel_handle, void *host_buffer,
                                    size_t read_size, int *status) {
  size_t buffer_size = 0;
  size_t pulled;
  int pcie_dev_handle;
  void *pull_buffer;

  *status = 0;
  pcie_dev_handle = device_info[physical_device_id].handle;

  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_get_buffer);
  pull_buffer = device_info[physical_device_id]
                    .mmd_dispatch->aocl_mmd_hostchannel_get_buffer(
                        pcie_dev_handle, channel_handle, &buffer_size, status);

  if ((NULL == pull_buffer) || (0 == buffer_size)) {
    return 0;
  }

  // How much can be pulled to user buffer
  buffer_size = (read_size > buffer_size) ? buffer_size : read_size;

  // Copy the data into the user buffer
  safe_memcpy(host_buffer, pull_buffer, buffer_size, buffer_size, buffer_size);

  // acknowledge host channel MMD that copy of data from its buffer is done
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer);
  pulled = device_info[physical_device_id]
               .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer(
                   pcie_dev_handle, channel_handle, buffer_size, status);

  // This shouldn't happen, but if the amount of data that was pulled, and the
  // amount of data get buffer said was available are not equal, something went
  // wrong
  assert(pulled == buffer_size);

  return pulled;
}

size_t acl_hal_mmd_hostchannel_push(unsigned int physical_device_id,
                                    int channel_handle, const void *host_buffer,
                                    size_t write_size, int *status) {
  size_t buffer_size = 0;
  size_t pushed;
  int pcie_dev_handle;
  void *push_buffer;

  *status = 0;
  pcie_dev_handle = device_info[physical_device_id].handle;

  // get the pointer to host channel push buffer
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_get_buffer);
  push_buffer = device_info[physical_device_id]
                    .mmd_dispatch->aocl_mmd_hostchannel_get_buffer(
                        pcie_dev_handle, channel_handle, &buffer_size, status);

  if ((NULL == push_buffer) || (0 == buffer_size)) {
    return 0;
  }

  // How much can be pushed to push buffer
  buffer_size = (write_size > buffer_size) ? buffer_size : write_size;

  // Copy the data into the push buffer

  safe_memcpy(push_buffer, host_buffer, buffer_size, buffer_size, buffer_size);

  // Acknowledge host channel MMD that copy of data to its buffer is done
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer);
  pushed = device_info[physical_device_id]
               .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer(
                   pcie_dev_handle, channel_handle, buffer_size, status);

  // This shouldn't happen, but if the amount of data that was pushed, and the
  // amount of space get buffer said was available are not equal, something went
  // wrong
  assert(pushed == buffer_size);
  return pushed;
}

void *acl_hal_mmd_hostchannel_get_buffer(unsigned int physical_device_id,
                                         int channel_handle,
                                         size_t *buffer_size, int *status) {
  int pcie_dev_handle;

  pcie_dev_handle = device_info[physical_device_id].handle;
  *status = 0;

  // get the pointer to host channel mmd buffer
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_get_buffer);
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_hostchannel_get_buffer(
          pcie_dev_handle, channel_handle, buffer_size, status);
}

size_t acl_hal_mmd_hostchannel_ack_buffer(unsigned int physical_device_id,
                                          int channel_handle, size_t ack_size,
                                          int *status) {
  int pcie_dev_handle;

  pcie_dev_handle = device_info[physical_device_id].handle;
  *status = 0;

  // ack the host channel mmd buffer
  assert(device_info[physical_device_id]
             .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer);
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_hostchannel_ack_buffer(
          pcie_dev_handle, channel_handle, ack_size, status);
}

cl_bool acl_hal_mmd_query_temperature(unsigned int physical_device_id,
                                      cl_int *temp) {
  float f;
  acl_assert_locked();

  device_info[physical_device_id].mmd_dispatch->aocl_mmd_get_info(
      device_info[physical_device_id].handle, AOCL_MMD_TEMPERATURE,
      sizeof(float), &f, NULL);
  *temp = (cl_int)f;
  return (cl_bool)1;
}

int acl_hal_mmd_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size) {
  int status;
  acl_assert_locked();

  status = device_info[physical_device_id].mmd_dispatch->aocl_mmd_get_info(
      device_info[physical_device_id].handle, AOCL_MMD_BOARD_NAME, size, name,
      NULL);
  name[size - 1] = 0;
  return status;
}

int acl_hal_mmd_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size) {
  int status;
  acl_assert_locked();

  status =
      device_info[physical_device_id].mmd_dispatch->aocl_mmd_get_offline_info(
          AOCL_MMD_VENDOR_NAME, size, name, NULL);
  name[size - 1] = 0;
  return status;
}

/**
 * Returns SVM capabilities as defined by OpenCL v2.0
 * May appear in 'value' bitmask: CL_DEVICE_SVM_COARSE_GRAIN_BUFFER,
 * CL_DEVICE_SVM_FINE_GRAIN_BUFFER, CL_DEVICE_SVM_FINE_GRAIN_SYSTEM
 * @param  physical_device_id which device to query
 * @param  value              bitmask of the type of SVM that is supported
 * @return                    1 if svm is supported, else 0
 */
int acl_hal_mmd_has_svm_support(unsigned int physical_device_id, int *value) {
  int status;
  acl_assert_locked();
  *value = 0;

  // Before 14.1 all boards supported physical memory and did not support SVM.
  // If this data is not defined and it is prior to 14.1, assume this board
  // only support physical memory.
  if (MMDVERSION_LESSTHAN(
          device_info[physical_device_id].mmd_dispatch->mmd_version, 14.1)) {
    return 0;
  } else {
    int mem_types = 0;
    status =
        device_info[physical_device_id].mmd_dispatch->aocl_mmd_get_offline_info(
            AOCL_MMD_MEM_TYPES_SUPPORTED, sizeof(int), &mem_types, NULL);
    if (status >= 0) {
      if (mem_types & AOCL_MMD_SVM_COARSE_GRAIN_BUFFER) {
        *value |= CL_DEVICE_SVM_COARSE_GRAIN_BUFFER;
      }
      if (mem_types & AOCL_MMD_SVM_FINE_GRAIN_BUFFER) {
        *value |= CL_DEVICE_SVM_FINE_GRAIN_BUFFER;
      }
      if (mem_types & AOCL_MMD_SVM_FINE_GRAIN_SYSTEM) {
        *value |= CL_DEVICE_SVM_FINE_GRAIN_SYSTEM;
      }
    }
  }

  return (*value > 0);
}

/**
 * Returns if device supports global physical memory
 * @param  physical_device_id which device to query
 * @return                    1 if supported, else 0
 */
int acl_hal_mmd_has_physical_mem(unsigned int physical_device_id) {
  acl_assert_locked();

  // Before 14.1 all boards supported physical memory and did not support SVM.
  // If this data is not defined and it is prior to 14.1, assume this board
  // only support physical memory.
  if (MMDVERSION_LESSTHAN(
          device_info[physical_device_id].mmd_dispatch->mmd_version, 14.1)) {
    return 1;
  } else {
    int mem_types = 0;
    int ret = 0;
    int status =
        device_info[physical_device_id].mmd_dispatch->aocl_mmd_get_offline_info(
            AOCL_MMD_MEM_TYPES_SUPPORTED, sizeof(int), &mem_types, NULL);
    if (status >= 0) {
      ret = (mem_types & AOCL_MMD_PHYSICAL_MEMORY);
    }

    return ret;
  }
}

#ifdef _WIN32
// Query the system timer, return a timer value in ns
cl_ulong acl_hal_mmd_get_timestamp() {
  LARGE_INTEGER li;
  double seconds;
  INT64 ticks;

  const INT64 NS_PER_S = 1000000000;
  acl_assert_locked_or_sig();

  QueryPerformanceCounter(&li);
  ticks = li.QuadPart;
  seconds = ticks / (double)m_ticks_per_second;
  return (cl_ulong)((double)seconds * (double)NS_PER_S + 0.5);
}

#else

// Query the system timer, return a timer value in ns
cl_ulong acl_hal_mmd_get_timestamp() {
  struct timespec a;
  const cl_ulong NS_PER_S = 1000000000;
  acl_assert_locked_or_sig();
  // Must use the MONOTONIC clock because the REALTIME clock
  // can go backwards due to adjustments by NTP for clock drift.
  // The MONOTONIC clock provides a timestamp since some fixed point in
  // the past, which might be system boot time or the start of the Unix
  // epoch.  This matches the Windows QueryPerformanceCounter semantics.
  // The MONOTONIC clock is to be used for measuring time intervals, and
  // fits the semantics of timestamps from the *device* perspective as defined
  // in OpenCL for clGetEventProfilingInfo.
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &a);
#else
  clock_gettime(CLOCK_MONOTONIC, &a);
#endif

  return (cl_ulong)(a.tv_nsec) + (cl_ulong)(a.tv_sec * NS_PER_S);
}
#endif

// ********************* Profile hardware accessors *********************
int acl_hal_mmd_get_profile_data(unsigned int physical_device_id,
                                 unsigned int accel_id, uint64_t *data,
                                 unsigned int length) {
  acl_assert_locked_or_sig();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_get_profile_data(&kern[physical_device_id], accel_id,
                                        data, length);
}

int acl_hal_mmd_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id) {
  acl_assert_locked_or_sig();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_reset_profile_counters(&kern[physical_device_id],
                                              accel_id);
}

int acl_hal_mmd_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id) {
  acl_assert_locked();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_disable_profile_counters(&kern[physical_device_id],
                                                accel_id);
}

int acl_hal_mmd_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id) {
  acl_assert_locked();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_enable_profile_counters(&kern[physical_device_id],
                                               accel_id);
}

int acl_hal_mmd_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id) {
  acl_assert_locked();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_set_profile_shared_control(&kern[physical_device_id],
                                                  accel_id);
}

int acl_hal_mmd_set_profile_start_count(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value) {
  acl_assert_locked();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_set_profile_start_cycle(&kern[physical_device_id],
                                               accel_id, value);
}

int acl_hal_mmd_set_profile_stop_count(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value) {
  acl_assert_locked();
  assert(physical_device_id < num_physical_devices);
  return acl_kernel_if_set_profile_stop_cycle(&kern[physical_device_id],
                                              accel_id, value);
}

// ********************* Wrapped functions ********************
// Shared memory allocator
void *acl_hal_mmd_legacy_shared_alloc(cl_context context, size_t size,
                                      unsigned long long *device_ptr_out) {
  unsigned int idevice;
  acl_mmd_dispatch_t *mmd_dispatch_found = NULL;
  acl_assert_locked();

  if (num_board_pkgs == 0) {
    printf("mmd legacy_shared_alloc: No board libaries found so cannot "
           "allocate shared memory\n");
    return NULL;
  }
  // Since this function is called for the context and not for a specific
  // device, it is only supported when only one board package is used within
  // that context.
  if (num_board_pkgs > 1) {
    mmd_dispatch_found = device_info[0].mmd_dispatch;
    for (idevice = 1; idevice < context->num_devices; ++idevice) {
      unsigned device_id = context->device[idevice]->def.physical_device_id;
      if (mmd_dispatch_found != device_info[device_id].mmd_dispatch) {
        printf("mmd legacy_shared_alloc: Can only allocate shared memory from "
               "a single board library but context contains multiple board "
               "libraries\n");
        return NULL;
      }
    }
  }

  if (MMDVERSION_LESSTHAN(min_MMD_version, 20.3)) {
    // Deprecated
    return device_info[0].mmd_dispatch->aocl_mmd_shared_mem_alloc(
        device_info[0].handle, size, device_ptr_out);
  } else {
    int error = 0;
    std::vector<cl_device_id> devices = std::vector<cl_device_id>(
        context->device, context->device + context->num_devices);
    void *mem = acl_hal_mmd_host_alloc(devices, size, 0, nullptr, &error);
    device_ptr_out = static_cast<unsigned long long *>(mem);
    if (error) {
      switch (error) {
      case CL_OUT_OF_HOST_MEMORY:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_alloc: Unable to allocate %zu bytes\n",
                 size);
        break;
      case CL_INVALID_VALUE:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_alloc: Unsupported alignment of 0\n");
        break;
      case CL_INVALID_PROPERTY:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_alloc: Unsuported properties\n");
        break;
      default:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_alloc: Unable to allocate memory\n");
        break;
      }
    }
    return mem;
  }
}

void acl_hal_mmd_legacy_shared_free(cl_context context, void *host_ptr,
                                    size_t size) {
  unsigned int idevice;
  acl_mmd_dispatch_t *mmd_dispatch_found = NULL;
  acl_assert_locked();

  if (num_board_pkgs == 0) {
    printf("mmd legacy_shared_free: No board libaries found so cannot free "
           "shared memory\n");
    return;
  }
  // Since this function is called for the context and not for a specific
  // device, it is only supported when only one board package is used within
  // that context.
  if (num_board_pkgs > 1) {
    mmd_dispatch_found = device_info[0].mmd_dispatch;
    for (idevice = 1; idevice < context->num_devices; ++idevice) {
      unsigned device_id = context->device[idevice]->def.physical_device_id;
      if (mmd_dispatch_found != device_info[device_id].mmd_dispatch) {
        printf(
            "mmd legacy_shared_free: Can only free shared memory from a single "
            "board library but context contains multiple board libraries\n");
        return;
      }
    }
  }

  if (MMDVERSION_LESSTHAN(min_MMD_version, 20.3)) {
    // Deprecated
    device_info[0].mmd_dispatch->aocl_mmd_shared_mem_free(device_info[0].handle,
                                                          host_ptr, size);
  } else {
    int error = acl_hal_mmd_free(context, host_ptr);
    if (error) {
      switch (error) {
      case CL_INVALID_VALUE:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_free: Invalid pointer provided\n");
        break;
      default:
        if (debug_verbosity >= 1)
          printf("mmd legacy_shared_free: Unknown error during deallocation\n");
        break;
      }
    }
  }
}

// Convert kernel and pll accessors to aocl_mmd

static size_t acl_kernel_if_read(acl_bsp_io *io, dev_addr_t src, char *dest,
                                 size_t size) {
  acl_assert_locked_or_sig();

  ACL_HAL_DEBUG_MSG_VERBOSE(5,
                            "HAL Reading from Kernel: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);
  return io->device_info->mmd_dispatch->aocl_mmd_read(
             io->device_info->handle, NULL, size, (void *)dest,
             kernel_interface, (size_t)src) == 0
             ? size
             : 0;
}

static size_t acl_kernel_if_write(acl_bsp_io *io, dev_addr_t dest,
                                  const char *src, size_t size) {
  acl_assert_locked_or_sig();

  ACL_HAL_DEBUG_MSG_VERBOSE(5, "HAL Writing to Kernel: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);
  return io->device_info->mmd_dispatch->aocl_mmd_write(
             io->device_info->handle, NULL, size, (const void *)src,
             kernel_interface, (size_t)dest) == 0
             ? size
             : 0;
}

void acl_hal_mmd_reset_kernels(cl_device_id device) {
  unsigned int physical_device_id = device->def.physical_device_id;
  acl_kernel_if_reset(&kern[physical_device_id]);
  for (unsigned int k = 0; k < kern[physical_device_id].num_accel; ++k) {
    for (unsigned int i = 0;
         i < kern[physical_device_id].accel_invoc_queue_depth[k] + 1; ++i) {
      int activation_id = kern[physical_device_id].accel_job_ids[k][i];
      if (kern[physical_device_id].accel_job_ids[k][i] >= 0) {
        kern[physical_device_id].accel_job_ids[k][i] = -1;
        acl_kernel_update_fn(activation_id,
                             -1); // Signal that it finished with error, since
                                  // we forced it to finish
      }
    }
  }
}

static size_t acl_pll_read(acl_bsp_io *io, dev_addr_t src, char *dest,
                           size_t size) {
  acl_assert_locked_or_sig();

  ACL_HAL_DEBUG_MSG_VERBOSE(5, "HAL Reading from PLL: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);
  return io->device_info->mmd_dispatch->aocl_mmd_read(
             io->device_info->handle, NULL, size, (void *)dest, pll_interface,
             (size_t)src) == 0
             ? size
             : 0;
}

static size_t acl_pll_write(acl_bsp_io *io, dev_addr_t dest, const char *src,
                            size_t size) {
  acl_assert_locked_or_sig();

  ACL_HAL_DEBUG_MSG_VERBOSE(5, "HAL Writing to PLL: %zu bytes %zx -> %zx\n",
                            size, (size_t)src, (size_t)dest);
  return io->device_info->mmd_dispatch->aocl_mmd_write(
             io->device_info->handle, NULL, size, (const void *)src,
             pll_interface, (size_t)dest) == 0
             ? size
             : 0;
}
static time_ns acl_bsp_get_timestamp() {
  return (time_ns)acl_hal_mmd_get_timestamp();
}

static void *
acl_hal_get_board_extension_function_address(const char *func_name,
                                             unsigned int physical_device_id) {
  char *error_msg;
  acl_assert_locked();

  auto *fn_ptr =
      my_dlsym(device_info[physical_device_id].mmd_dispatch->mmd_library,
               func_name, &error_msg);

  if (!fn_ptr) {
    printf("Error: Unable to find function name %s in board library %s (%p)\n",
           func_name,
           device_info[physical_device_id].mmd_dispatch->library_name.c_str(),
           device_info[physical_device_id].mmd_dispatch->mmd_library);
    return nullptr;
  }

  return fn_ptr;
}

void *acl_hal_mmd_shared_alloc(cl_device_id device, size_t size,
                               size_t alignment, mem_properties_t *properties,
                               int *error) {
  // Note we do not support devices in the same context with different MMDs
  // Safe to get the mmd handle from first device
  void *result = NULL;
  unsigned int physical_device_id = device->def.physical_device_id;
  acl_mmd_dispatch_t *dispatch = device_info[physical_device_id].mmd_dispatch;
  if (!dispatch->aocl_mmd_shared_alloc) {
    // Not implemented by the board.
    return result;
  }

  int handle = device_info[physical_device_id].handle;
  result = dispatch->aocl_mmd_shared_alloc(
      handle, size, alignment, (aocl_mmd_mem_properties_t *)properties, error);

  if (error) {
    switch (*error) {
    case AOCL_MMD_ERROR_INVALID_HANDLE:
      assert(*error != AOCL_MMD_ERROR_INVALID_HANDLE &&
             "Error: Invalid device provided");
      break;
    case AOCL_MMD_ERROR_OUT_OF_MEMORY:
      *error = CL_OUT_OF_HOST_MEMORY;
      break;
    case AOCL_MMD_ERROR_UNSUPPORTED_ALIGNMENT:
      *error = CL_INVALID_VALUE;
      break;
    case AOCL_MMD_ERROR_UNSUPPORTED_PROPERTY:
      *error = CL_INVALID_PROPERTY;
      break;
    }
  }
  return result;
}

void *acl_hal_mmd_host_alloc(const std::vector<cl_device_id> devices,
                             size_t size, size_t alignment,
                             mem_properties_t *properties, int *error) {
  // Note we do not support devices in the same context with different MMDs
  // Safe to get the mmd handle from first device
  void *result = NULL;
  assert(!devices.empty());
  unsigned int physical_device_id = devices[0]->def.physical_device_id;
  acl_mmd_dispatch_t *dispatch = device_info[physical_device_id].mmd_dispatch;
  if (!dispatch->aocl_mmd_host_alloc) {
    // Not implemented by the board. It is safe to assume buffer is forgotten
    return result;
  }
  int *handles = acl_new_arr<int>(devices.size());
  if (handles == NULL) {
    return result;
  }

  for (size_t i = 0; i < devices.size(); i++) {
    physical_device_id = devices[i]->def.physical_device_id;
    handles[i] = device_info[physical_device_id].handle;
  }

  result = dispatch->aocl_mmd_host_alloc(
      handles, devices.size(), size, alignment,
      (aocl_mmd_mem_properties_t *)properties, error);
  acl_delete_arr(handles);

  if (error) {
    switch (*error) {
    case AOCL_MMD_ERROR_INVALID_HANDLE:
      assert(*error != AOCL_MMD_ERROR_INVALID_HANDLE &&
             "Error: Invalid device provided");
      break;
    case AOCL_MMD_ERROR_OUT_OF_MEMORY:
      *error = CL_OUT_OF_HOST_MEMORY;
      break;
    case AOCL_MMD_ERROR_UNSUPPORTED_ALIGNMENT:
      *error = CL_INVALID_VALUE;
      break;
    case AOCL_MMD_ERROR_UNSUPPORTED_PROPERTY:
      *error = CL_INVALID_PROPERTY;
      break;
    }
  }
  return result;
}

int acl_hal_mmd_free(cl_context context, void *mem) {
  // This call is device agnostic get the mmd handle from first device
  // Note we do not support devices in the same context with different MMDs
  cl_device_id device = context->device[0];
  unsigned int physical_device_id = device->def.physical_device_id;
  acl_mmd_dispatch_t *dispatch = device_info[physical_device_id].mmd_dispatch;
  int result = 0;

  if (!dispatch->aocl_mmd_free) {
    // Not implemented by the board. It is safe to assume buffer is forgotten
    return result;
  }
  result = dispatch->aocl_mmd_free(mem);
  switch (result) {
  case AOCL_MMD_ERROR_INVALID_POINTER:
    return CL_INVALID_VALUE;
  default:
    return result;
  }
}

/**
 *  Converts MMD allocation capabilities to runtime allocation capabilities
 *  @param mmd_capabilities the capabilities as defined by the MMD
 *  @return the capabilities as defined by the runtime
 */
unsigned acl_convert_mmd_capabilities(unsigned mmd_capabilities) {
  unsigned capability = 0;

  if (mmd_capabilities & AOCL_MMD_MEM_CAPABILITY_SUPPORTED) {
    capability |= ACL_MEM_CAPABILITY_SUPPORTED;
  }
  if (mmd_capabilities & AOCL_MMD_MEM_CAPABILITY_ATOMIC) {
    capability |= ACL_MEM_CAPABILITY_ATOMIC;
  }
  if (mmd_capabilities & AOCL_MMD_MEM_CAPABILITY_CONCURRENT) {
    capability |= ACL_MEM_CAPABILITY_CONCURRENT;
  }
  if (mmd_capabilities & AOCL_MMD_MEM_CAPABILITY_P2P) {
    capability |= ACL_MEM_CAPABILITY_P2P;
  }
  return capability;
}

void acl_hal_mmd_simulation_streaming_kernel_start(
    unsigned int physical_device_id, const std::string &kernel_name,
    const int accel_id) {
  device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_simulation_streaming_kernel_start(
          device_info[physical_device_id].handle, kernel_name, accel_id);
}

void acl_hal_mmd_simulation_streaming_kernel_done(
    unsigned int physical_device_id, const std::string &kernel_name,
    unsigned int &finish_counter) {
  device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_simulation_streaming_kernel_done(
          device_info[physical_device_id].handle, kernel_name, finish_counter);
}

void acl_hal_mmd_simulation_set_kernel_cra_address_map(
    unsigned int physical_device_id,
    const std::vector<uintptr_t> &kernel_csr_address_map) {
  device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_simulation_set_kernel_cra_address_map(
          device_info[physical_device_id].handle, kernel_csr_address_map);
}

size_t acl_hal_mmd_read_csr(unsigned int physical_device_id, uintptr_t offset,
                            void *ptr, size_t size) {
  return device_info[physical_device_id].mmd_dispatch->aocl_mmd_read(
      device_info[physical_device_id].handle, NULL, size, (void *)ptr,
      kernel_interface, (size_t)offset);
}

size_t acl_hal_mmd_write_csr(unsigned int physical_device_id, uintptr_t offset,
                             const void *ptr, size_t size) {
  return device_info[physical_device_id].mmd_dispatch->aocl_mmd_write(
      device_info[physical_device_id].handle, NULL, size, (const void *)ptr,
      kernel_interface, (size_t)offset);
}

int acl_hal_mmd_simulation_device_global_interface_read(
    unsigned int physical_device_id, const char *interface_name,
    void *host_addr, size_t dev_addr, size_t size) {
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_simulation_device_global_interface_read(
          device_info[physical_device_id].handle, interface_name, host_addr,
          dev_addr, size);
}

int acl_hal_mmd_simulation_device_global_interface_write(
    unsigned int physical_device_id, const char *interface_name,
    const void *host_addr, size_t dev_addr, size_t size) {
  return device_info[physical_device_id]
      .mmd_dispatch->aocl_mmd_simulation_device_global_interface_write(
          device_info[physical_device_id].handle, interface_name, host_addr,
          dev_addr, size);
}
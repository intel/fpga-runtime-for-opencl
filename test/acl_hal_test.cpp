// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <cinttypes>
#include <stdarg.h>
#include <stdio.h>

#include <acl.h>
#include <acl_event.h>
#include <acl_hal.h>
#include <acl_support.h>
#include <acl_util.h>
#include <unref.h>

#include "acl_hal_test.h"
#include "acl_test.h"

static int acl_test_svm_memory_support =
    (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER | CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
     CL_DEVICE_SVM_FINE_GRAIN_SYSTEM);
static bool acl_test_physical_memory_support = true;
static bool acl_test_buffer_location_support = true;

// Parts of a valid HAL.
void acltest_hal_init_device(const acl_system_def_t *def);
void acltest_hal_yield(cl_uint num_devices, const cl_device_id *devices) {
  UNREFERENCED_PARAMETER(num_devices);
  UNREFERENCED_PARAMETER(devices);
}
cl_ulong acltest_hal_get_timestamp(void);
void acltest_hal_copy_hostmem_to_hostmem(cl_event event, const void *src,
                                         void *dest, size_t size);
void acltest_hal_copy_hostmem_to_globalmem(cl_event event, const void *src,
                                           void *dest, size_t size);
void acltest_hal_copy_globalmem_to_hostmem(cl_event event, const void *src,
                                           void *dest, size_t size);
void acltest_hal_copy_globalmem_to_globalmem(cl_event event, const void *src,
                                             void *dest, size_t size);
void acltest_hal_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf);
void acltest_hal_launch_kernel(unsigned int physical_id,
                               acl_kernel_invocation_wrapper_t *wrapper);
void acltest_hal_unstall_kernel(unsigned int physical_id, int activation_id);
int acltest_hal_program_device(unsigned int physical_id,
                               const acl_device_def_t *devdef,
                               const struct acl_pkg_file *binary,
                               int acl_program_mode);
cl_bool acltest_hal_query_temperature(unsigned int physical_id, cl_int *temp);
int acltest_hal_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size);
int acltest_hal_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size);
int acltest_hal_get_profile_data(unsigned int physical_device_id,
                                 unsigned int accel_id, uint64_t *data,
                                 unsigned int length);
int acltest_hal_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id);
int acltest_hal_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id);
int acltest_hal_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id);
int acltest_hal_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id);
int acltest_hal_set_profile_start_cycle(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value);
int acltest_hal_set_profile_stop_cycle(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value);
int acl_test_hal_has_svm_support(unsigned int physical_device_id, int *value);
int acl_test_hal_has_physical_mem(unsigned int physical_device_id);
int acl_test_hal_support_buffer_location(
    const std::vector<cl_device_id> &devices);
int acl_test_hal_pll_reconfigure(unsigned int physical_device_id,
                                 const char *pll_settings_str);
void acl_test_hal_reset_kernels(cl_device_id device);
int acl_test_hal_try_devices(cl_uint num_devices, const cl_device_id *devices,
                             cl_platform_id platform);
int acl_test_hal_close_devices(cl_uint num_devices,
                               const cl_device_id *devices);
void *acl_test_hal_shared_alloc(cl_device_id device, size_t size,
                                size_t alignment, mem_properties_t *properties,
                                int *error);
void *acl_test_hal_host_alloc(const std::vector<cl_device_id> &devices,
                              size_t size, size_t alignment,
                              mem_properties_t *properties, int *error);
int acl_test_hal_free(cl_context context, void *ptr);

static acl_event_update_callback acltest_hal_event_callback = NULL;
static acl_kernel_update_callback acltest_hal_kernel_callback = NULL;
static acl_profile_callback acltest_hal_profile_callback = NULL;
static acl_device_update_callback acltest_hal_device_callback = NULL;
static acl_process_printf_buffer_callback
    acltest_process_printf_buffer_callback = NULL;

static const acl_hal_t simple_hal = {acltest_hal_init_device,
                                     acltest_hal_yield,
                                     acltest_hal_get_timestamp,
                                     acltest_hal_copy_hostmem_to_hostmem,
                                     acltest_hal_copy_hostmem_to_globalmem,
                                     acltest_hal_copy_globalmem_to_hostmem,
                                     acltest_hal_copy_globalmem_to_globalmem,
                                     acltest_hal_register_callbacks,
                                     acltest_hal_launch_kernel,
                                     acltest_hal_unstall_kernel,
                                     acltest_hal_program_device,
                                     acltest_hal_query_temperature,
                                     acltest_hal_get_device_official_name,
                                     acltest_hal_get_device_vendor_name,
                                     0,
                                     0,
                                     acltest_hal_get_profile_data,
                                     acltest_hal_reset_profile_counters,
                                     acltest_hal_disable_profile_counters,
                                     acltest_hal_enable_profile_counters,
                                     acltest_hal_set_profile_shared_control,
                                     acltest_hal_set_profile_start_cycle,
                                     acltest_hal_set_profile_stop_cycle,
                                     acl_test_hal_has_svm_support,
                                     acl_test_hal_has_physical_mem,
                                     acl_test_hal_support_buffer_location,
                                     0,
                                     acl_test_hal_pll_reconfigure,
                                     acl_test_hal_reset_kernels,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     acl_test_hal_try_devices,
                                     acl_test_hal_close_devices,
                                     acl_test_hal_host_alloc,
                                     acl_test_hal_free,
                                     acl_test_hal_shared_alloc};

// Emulate device memory.
//
// Device memory is modeled as a contiguous set of addresses, and they
// usually start at 0.
// We can't just copy to address 0 on a regular host because that will
// cause a segfault.
//
// So keep a buffer around to store what *should* be in device memory.
// Address 0 translates into the first location in acltest_hal_device_mem.
// We resize it as needed to accommodate the maximum transfer.
//
// This is only required when the runtime is in regular mode; if it's
// emulating an offline device then the host runtime already does mallocs
// for device buffers.
bool acltest_hal_emulate_device_mem = false;
static void *acltest_hal_device_mem = 0;
static size_t acltest_hal_device_mem_size = 0;

TEST_GROUP(acl_hal){void setup(){acl_mutex_wrapper.lock();
}
void teardown() {
  acl_mutex_wrapper.unlock();
  acl_assert_unlocked();
}
}
;

TEST(acl_hal, zero_when_unint) { CHECK(0 == acl_get_hal()); }

TEST(acl_hal, new_hal_must_exist) { CHECK(0 == acl_set_hal(0)); }

TEST(acl_hal, valid_init) {
  const acl_hal_t *stored_hal;
  CHECK(1 == acl_set_hal(&simple_hal));

  stored_hal = acl_get_hal();
  CHECK(stored_hal != 0);
  // Check all fields
  CHECK(stored_hal->init_device == simple_hal.init_device);
  CHECK(stored_hal->yield == simple_hal.yield);
  CHECK(stored_hal->get_timestamp == simple_hal.get_timestamp);
  CHECK(stored_hal->copy_hostmem_to_hostmem ==
        simple_hal.copy_hostmem_to_hostmem);
  CHECK(stored_hal->copy_hostmem_to_globalmem ==
        simple_hal.copy_hostmem_to_globalmem);
  CHECK(stored_hal->copy_globalmem_to_hostmem ==
        simple_hal.copy_globalmem_to_hostmem);
  CHECK(stored_hal->copy_globalmem_to_globalmem ==
        simple_hal.copy_globalmem_to_globalmem);
  CHECK(stored_hal->launch_kernel == simple_hal.launch_kernel);
  CHECK(stored_hal->unstall_kernel == simple_hal.unstall_kernel);
  CHECK(stored_hal->program_device == simple_hal.program_device);
  CHECK(stored_hal->query_temperature == simple_hal.query_temperature);
  CHECK(stored_hal->get_device_official_name ==
        simple_hal.get_device_official_name);
  CHECK(stored_hal->get_device_vendor_name ==
        simple_hal.get_device_vendor_name);
  CHECK(stored_hal->get_profile_data == simple_hal.get_profile_data);
  CHECK(stored_hal->reset_profile_counters ==
        simple_hal.reset_profile_counters);
  CHECK(stored_hal->disable_profile_counters ==
        simple_hal.disable_profile_counters);
  CHECK(stored_hal->enable_profile_counters ==
        simple_hal.enable_profile_counters);
  CHECK(stored_hal->set_profile_start_cycle ==
        simple_hal.set_profile_start_cycle);
  CHECK(stored_hal->set_profile_stop_cycle ==
        simple_hal.set_profile_stop_cycle);
  CHECK(stored_hal->has_svm_memory_support ==
        simple_hal.has_svm_memory_support);
  CHECK(stored_hal->has_physical_mem == simple_hal.has_physical_mem);
  CHECK(stored_hal->pll_reconfigure == simple_hal.pll_reconfigure);
  CHECK(stored_hal->reset_kernels == simple_hal.reset_kernels);

  // Reset should invalidate
  acl_reset_hal();
  CHECK(0 == acl_get_hal());
}

TEST(acl_hal, field_check) {
  acl_hal_t bad_hal;

  bad_hal = simple_hal;
  bad_hal.init_device = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.yield = 0;
  CHECK(0 != acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.get_timestamp = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.copy_hostmem_to_hostmem = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.copy_hostmem_to_globalmem = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.copy_globalmem_to_hostmem = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.copy_globalmem_to_globalmem = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.launch_kernel = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.unstall_kernel = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.program_device = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.query_temperature = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.get_device_official_name = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.get_device_vendor_name = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.get_profile_data = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.reset_profile_counters = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.disable_profile_counters = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.enable_profile_counters = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.set_profile_start_cycle = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.set_profile_stop_cycle = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.has_svm_memory_support = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.has_physical_mem = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.pll_reconfigure = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
  bad_hal = simple_hal;
  bad_hal.reset_kernels = 0;
  CHECK(0 == acl_set_hal(&bad_hal));
}

TEST(acl_hal, debugging) {
  const char test_str[] = "hal debug printf test\n";
  CHECK(acl_set_hal(&simple_hal));
  const acl_hal_t *hal = acl_get_hal();

  acl_test_setenv("ACL_DEBUG", "0");
  char *acl_debug_env = getenv("ACL_DEBUG");
  CHECK(acl_debug_env);
  debug_mode = atoi(acl_debug_env);
  // The simple hal starts out in non-debug mode.
  CHECK_EQUAL(0, debug_mode);
  // Nothing gets printed when debug is 0.
  CHECK_EQUAL(0, acl_print_debug_msg("some text"));
  CHECK_EQUAL(0, acl_print_debug_msg(""));

  // Now start debugging.
  acl_test_setenv("ACL_DEBUG", "4");
  acl_debug_env = getenv("ACL_DEBUG");
  CHECK(acl_debug_env);
  debug_mode = atoi(acl_debug_env);
  CHECK_EQUAL(4, debug_mode);
  acl_test_setenv("ACL_DEBUG", "3");
  acl_debug_env = getenv("ACL_DEBUG");
  CHECK(acl_debug_env);
  debug_mode = atoi(acl_debug_env);
  CHECK_EQUAL(3, debug_mode);
  acl_test_setenv("ACL_DEBUG", "2");
  acl_debug_env = getenv("ACL_DEBUG");
  CHECK(acl_debug_env);
  debug_mode = atoi(acl_debug_env);
  CHECK_EQUAL(2, debug_mode);
  CHECK_EQUAL(
      sizeof(test_str) / sizeof(test_str[0]) - 1,
      acl_print_debug_msg(test_str)); // -1 because source array includes a NUL
                                      // char, but we don't print it.
  CHECK_EQUAL(0, acl_print_debug_msg(""));
  acl_test_setenv("ACL_DEBUG", "0");
  acl_debug_env = getenv("ACL_DEBUG");
  CHECK(acl_debug_env);
  debug_mode = atoi(acl_debug_env);
  CHECK_EQUAL(0, debug_mode);
  CHECK_EQUAL(0, acl_print_debug_msg(test_str));
  acl_test_unsetenv("ACL_DEBUG");

  cl_int temp;
  CHECK_EQUAL(1, hal->query_temperature(0, &temp));
  CHECK_EQUAL(0, temp);

#define BUF_SIZE 1024
  char name[BUF_SIZE];
  int size_returned;
  size_returned = hal->get_device_official_name(0, name, BUF_SIZE);
  CHECK(size_returned < BUF_SIZE);
  CHECK(strcmp(name, "Test Device") == 0);
  size_returned = hal->get_device_vendor_name(0, name, BUF_SIZE);
  CHECK(size_returned < BUF_SIZE);
  CHECK(strcmp(name, "Intel(R) Corporation") == 0);

  uint64_t temp_u64;
  // Return values in following six calls are defined by the test HAL as
  // 1-6, simply to verify ordering of the function pointers.
  CHECK_EQUAL(1, hal->get_profile_data(0, 1, &temp_u64, 1));
  CHECK_EQUAL(2, hal->reset_profile_counters(0, 1));
  CHECK_EQUAL(3, hal->disable_profile_counters(0, 1));
  CHECK_EQUAL(4, hal->enable_profile_counters(0, 1));
  CHECK_EQUAL(5, hal->set_profile_start_cycle(0, 1, 2));
  CHECK_EQUAL(6, hal->set_profile_stop_cycle(0, 1, 2));
}

/////////////////////////////
// A very simple HAL

void acltest_hal_init_device(const acl_system_def_t *def) {
  def = def;

  // Only emulate memory if using an offline device.
  // In that case the regular runtime will already do malloc's to cover
  // device memory.
  const char *env = acl_getenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
  acltest_hal_emulate_device_mem = (env == NULL || env[0] == 0);
}

const acl_hal_t *acl_test_get_simple_hal(void) { return &simple_hal; }

cl_ulong acltest_hal_get_timestamp(void) {
  static cl_ulong now = 0;
  return now++; // always advancing is reasonable.
}

void acltest_hal_copy_hostmem_to_hostmem(cl_event event, const void *src,
                                         void *dest, size_t size) {
  acltest_hal_event_callback(
      event, CL_RUNNING); // in "real life" this in response to a hw message
  size_t i;
  acl_print_debug_msg(" Copying %zu bytes from %p to %p event %p\n", size, src,
                      dest, event);
  for (i = 0; i < size; i++) {
    ((char *)dest)[i] = ((char *)src)[i];
  }
  acltest_hal_event_callback(
      event, CL_COMPLETE); // in "real life" this in response to a hw message
}
void acltest_hal_copy_hostmem_to_globalmem(cl_event event, const void *src,
                                           void *dest, size_t size) {
  // For testing purposes, the same.
  (void)acltest_translate_device_address(dest, size);
  void *dev_ptr = acltest_translate_device_address(dest, 0);
  acltest_hal_copy_hostmem_to_hostmem(event, src, dev_ptr, size);
}
void acltest_hal_copy_globalmem_to_hostmem(cl_event event, const void *src,
                                           void *dest, size_t size) {
  // For testing purposes, the same.
  (void)acltest_translate_device_address(src, size);
  void *dev_ptr = acltest_translate_device_address(src, 0);
  acltest_hal_copy_hostmem_to_hostmem(event, dev_ptr, dest, size);
}
void acltest_hal_copy_globalmem_to_globalmem(cl_event event, const void *src,
                                             void *dest, size_t size) {
  // For testing purposes, the same.
  (void)acltest_translate_device_address(src, size);
  (void)acltest_translate_device_address(dest, size);
  void *src_ptr = acltest_translate_device_address(src, 0);
  void *dest_ptr = acltest_translate_device_address(dest, 0);
  acltest_hal_copy_hostmem_to_hostmem(event, src_ptr, dest_ptr, size);
}

void acltest_hal_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf) {
  acltest_hal_event_callback = event_update;
  acltest_hal_kernel_callback = kernel_update;
  acltest_hal_profile_callback = profile_update;
  acltest_hal_device_callback = device_update;
  acltest_process_printf_buffer_callback = process_printf;
}

void acltest_call_event_update_callback(cl_event event, int new_status) {
  acltest_hal_event_callback(event, new_status);
}

void acltest_call_kernel_update_callback(int activation_id, cl_int status) {
  acltest_hal_kernel_callback(activation_id, status);
}

void acltest_call_device_update_callback(unsigned physical_device_id,
                                         int device_status) {
  acltest_hal_device_callback(physical_device_id,
                              (CL_EXCEPTION_TYPE_INTEL)device_status, NULL, 0);
}

void acltest_call_printf_buffer_callback(int activation_id, int size,
                                         int stalled) {
  acltest_process_printf_buffer_callback(activation_id, size, stalled);
}

void acltest_hal_launch_kernel(
    unsigned int physical_id,
    acl_kernel_invocation_wrapper_t *invocation_wrapper) {
  // Send a message to the device controller, pointing at
  // the global buffer pointed at by invocation_wrapper->mem's global
  // memory buffer.

  // For unit testing, just trust the tester is doing the right thing.
  invocation_wrapper = invocation_wrapper; // avoid warning on MSVC
  physical_id = physical_id;
}

void acltest_hal_unstall_kernel(unsigned int physical_id, int invocation_id) {
  invocation_id = invocation_id; // avoid warning on windows
  // For unit testing, just trust the tester is doing the right thing.
  physical_id = physical_id;
}

cl_bool acltest_hal_query_temperature(unsigned int physical_id, cl_int *temp) {
  *temp = 0; // Avoid Windows warning
  physical_id = physical_id;
  return 1; // Fake success
}

int acltest_hal_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size) {
  static const char *the_name = "Test Device";
  physical_device_id = physical_device_id; // Avoid Windows warning
  const size_t the_size = strnlen(the_name, size - 1) + 1;
  strncpy(name, the_name, the_size - 1);
  name[the_size - 1] = '\0';
  return static_cast<int>(the_size);
}

int acltest_hal_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size) {
  static const char *the_name = "Intel(R) Corporation";
  physical_device_id = physical_device_id; // Avoid Windows warning
  const size_t the_size = strnlen(the_name, size - 1) + 1;
  strncpy(name, the_name, the_size - 1);
  name[the_size - 1] = '\0';
  return static_cast<int>(the_size);
}

int acltest_hal_program_device(unsigned int physical_id,
                               const acl_device_def_t *devdef,
                               const struct acl_pkg_file *binary,
                               int acl_program_mode) {
  devdef = devdef;
  binary = binary;
  physical_id = physical_id;
  acl_program_mode = acl_program_mode;

  char *str_use_jtag_programming = getenv("ACL_PCIE_USE_JTAG_PROGRAMMING");
  // program the device based on the acl_program_mode and
  // str_use_jtag_programming
  if (acl_program_mode == ACL_PROGRAM_PRESERVE_MEM &&
      str_use_jtag_programming) {
    return ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM;
  }

  return 0; // signals success
}

int acltest_hal_get_profile_data(unsigned int physical_device_id,
                                 unsigned int accel_id, uint64_t *data,
                                 unsigned int length) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  data = data;
  length = length;
  return 1;
}

int acltest_hal_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 2;
}

int acltest_hal_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 3;
}

int acltest_hal_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 4;
}

int acltest_hal_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 7;
}

int acltest_hal_set_profile_start_cycle(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  value = value;
  return 5;
}

int acltest_hal_set_profile_stop_cycle(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  value = value;
  return 6;
}

void acl_test_hal_set_svm_memory_support(int value) {
  acl_test_svm_memory_support = value;
}

void acl_test_hal_set_physical_memory_support(bool value) {
  acl_test_physical_memory_support = value;
}

void acl_test_hal_set_buffer_location_support(bool value) {
  acl_test_buffer_location_support = value;
}

int acl_test_hal_has_svm_support(unsigned int physical_device_id, int *value) {
  physical_device_id = physical_device_id; // Avoid Windows warning
  *value = acl_test_svm_memory_support;
  return *value != 0;
}

int acl_test_hal_has_physical_mem(unsigned int physical_device_id) {
  physical_device_id = physical_device_id; // Avoid Windows warning
  return acl_test_physical_memory_support;
}

int acl_test_hal_support_buffer_location(
    const std::vector<cl_device_id> &devices) {
  return acl_test_buffer_location_support;
}

int acl_test_hal_pll_reconfigure(unsigned int physical_device_id,
                                 const char *pll_settings_str) {
  physical_device_id = physical_device_id;
  pll_settings_str = pll_settings_str;
  return 0;
}
void acl_test_hal_reset_kernels(cl_device_id device) { device = device; }

int acl_test_hal_try_devices(cl_uint num_devices, const cl_device_id *devices,
                             cl_platform_id platform) {
  // Windows warnings:
  platform = platform;

  for (unsigned i = 0; i < num_devices; i++) {
    devices[i]->opened_count++;
  }
  return 0;
}

int acl_test_hal_close_devices(cl_uint num_devices,
                               const cl_device_id *devices) {
  for (unsigned i = 0; i < num_devices; i++) {
    assert(devices[i]->opened_count > 0);
    devices[i]->opened_count--;
  }
  return 0;
}

void *acl_test_hal_shared_alloc(cl_device_id device, size_t size,
                                size_t alignment, mem_properties_t *properties,
                                int *error) {
  device = device;
  size = size;
  alignment = alignment;
  properties = properties;
  error = error;
  return (void *)0xdeadbeefdeadbeef;
}

void *acl_test_hal_host_alloc(const std::vector<cl_device_id> &, size_t, size_t,
                              mem_properties_t *, int *) {
  return (void *)0xdeadbeefdeadbeef;
}

int acl_test_hal_free(cl_context context, void *ptr) {
  context = context;
  ptr = ptr;
  return 0;
}

///////////////
// Emulate device memory.
// This is only necessary

// Translate the device pointer to something we can actually write to in  host
// memory. And translate them into a host pointer.  It's transient though!
void *acltest_translate_device_address(const void *device_ptr, size_t offset) {
  if (!acltest_hal_emulate_device_mem) {
    // cast away const and get bottom 48 bits
    void *result = (void *)(0xffffffffffff & ((uintptr_t)device_ptr));
    return result;
  }

  uintptr_t max_dev_addr = 0xfffffff & ((uintptr_t)device_ptr + offset);
  acl_print_debug_msg("maxdevaddr %" PRIuPTR "\n", max_dev_addr);
  if (!acltest_hal_device_mem || max_dev_addr >= acltest_hal_device_mem_size) {
    if (!acltest_hal_device_mem) {
      acltest_hal_device_mem = acl_malloc(max_dev_addr + 1);
      acl_print_debug_msg("malloc %p\n", acltest_hal_device_mem);
    } else {
      acltest_hal_device_mem =
          acl_realloc(acltest_hal_device_mem, max_dev_addr + 1);
      acl_print_debug_msg("realloc %p\n", acltest_hal_device_mem);
    }
    assert(acltest_hal_device_mem);
    acltest_hal_device_mem_size = max_dev_addr + 1;
  }
  void *result = ((char *)acltest_hal_device_mem) + max_dev_addr;
  acl_print_debug_msg(" dev %p --> fake %p     (base %p size %llx)\n",
                      device_ptr, result, acltest_hal_device_mem,
                      (unsigned long long)acltest_hal_device_mem_size);
  return result;
}

void acltest_hal_teardown(void) {
  if (acltest_hal_device_mem) {
    acl_free(acltest_hal_device_mem);
  }
  acltest_hal_device_mem = 0;
  acltest_hal_device_mem_size = 0;
  acltest_hal_emulate_device_mem = false;
}

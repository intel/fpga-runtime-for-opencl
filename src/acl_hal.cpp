// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <string.h>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_hal.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

//////////////////////////////
// Global variables
static acl_hal_t acl_hal_value;
static int acl_hal_is_valid = 0;
int debug_mode = 0;
static int l_is_valid_hal(const acl_hal_t *hal);
acl_mmd_library_names_t *libraries_to_load = NULL;

const acl_hal_t *acl_get_hal(void) {
  // NOTE: This function is called from multiple threads (i.e. in the
  // signal handler) without a lock. It's probably fine
  // though since the HAL is normally just set once on startup and then not
  // changed.
  if (acl_hal_is_valid) {
    return &acl_hal_value;
  } else {
    return 0;
  }
}

int acl_set_hal(const acl_hal_t *new_hal) {
  acl_assert_locked();
  const char *debug_env = getenv("ACL_DEBUG");
  if (debug_env) {
    debug_mode = atoi(debug_env);
  }
  if (l_is_valid_hal(new_hal)) {
    acl_hal_value = *new_hal;
    acl_hal_is_valid = 1;
    return 1;
  } else {
    debug_mode = 0;
    return 0;
  }
}

void acl_reset_hal(void) {
  acl_assert_locked();

  for (unsigned int i = 0; i < sizeof(acl_hal_t); i++) {
    ((char *)&acl_hal_value)[i] = 0;
  }
  acl_hal_is_valid = 0;
  debug_mode = 0;
}

static int l_is_valid_hal(const acl_hal_t *hal) {
  acl_assert_locked();

  if (hal == 0)
    return 0;
  if (hal->init_device == 0)
    return 0;
  if (hal->get_timestamp == 0)
    return 0;
  if (hal->copy_hostmem_to_hostmem == 0)
    return 0;
  if (hal->copy_hostmem_to_globalmem == 0)
    return 0;
  if (hal->copy_globalmem_to_hostmem == 0)
    return 0;
  if (hal->copy_globalmem_to_globalmem == 0)
    return 0;
  if (hal->launch_kernel == 0)
    return 0;
  if (hal->unstall_kernel == 0)
    return 0;
  if (hal->program_device == 0)
    return 0;
  if (hal->query_temperature == 0)
    return 0;
  if (hal->get_device_official_name == 0)
    return 0;
  if (hal->get_device_vendor_name == 0)
    return 0;
  if (hal->get_profile_data == 0)
    return 0;
  if (hal->reset_profile_counters == 0)
    return 0;
  if (hal->disable_profile_counters == 0)
    return 0;
  if (hal->enable_profile_counters == 0)
    return 0;
  if (hal->set_profile_shared_control == 0)
    return 0;
  if (hal->set_profile_start_cycle == 0)
    return 0;
  if (hal->set_profile_stop_cycle == 0)
    return 0;
  if (hal->has_svm_memory_support == 0)
    return 0;
  if (hal->has_physical_mem == 0)
    return 0;
  if (hal->pll_reconfigure == 0)
    return 0;
  if (hal->reset_kernels == 0)
    return 0;
  return 1;
}

int acl_print_debug_msg(const char *msg, ...) {
  int num_chars_printed = 0;
  if (debug_mode > 0) {
    va_list ap;
    va_start(ap, msg);
    num_chars_printed = vprintf(msg, ap);
    va_end(ap);
  }
  return num_chars_printed;
}

extern CL_API_ENTRY void CL_API_CALL
clSetBoardLibraryIntelFPGA(char *library_name) {
  acl_mmd_library_names_t *next_library = NULL;
  std::scoped_lock lock{acl_mutex_wrapper};

  acl_print_debug_msg("Adding library '%s' to list of libraries to open\n",
                      library_name);

  next_library = acl_new<acl_mmd_library_names_t>();
  assert(next_library);
  next_library->library_name = library_name;

  if (libraries_to_load == NULL) {
    libraries_to_load = next_library;
  } else {
    acl_mmd_library_names_t *insertion_point;
    insertion_point = libraries_to_load;
    while (insertion_point->next != NULL) {
      insertion_point = insertion_point->next;
    }
    insertion_point->next = next_library;
  }

  return;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <stddef.h>

// External library headers.
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_auto.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_offline_hal.h>
#include <acl_platform.h>
#include <acl_profiler.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

////////////////////////////////////////////////////
// Global data

std::unordered_map<std::string, bool> l_allow_invalid_type{};

// This variable exists only to ensure that the user has linked the host
// library with a matching HAL.
int ACL_VERSION_COMPATIBILITY_SYMBOL = 0;

////////////////////////////////////////////////////
// Static data

// This is the board that is plugged into the host system.
// It might be invalid in the offline compilation scenario.
static int acl_present_board_is_valid_value = 0;
static acl_system_def_t builtin_prog_def_value;

// Static functions
static int l_is_valid_system_def(const acl_system_def_t *sys);
static void l_reset_present_board();

////////////////////////////////////////////////////
// Global functions

acl_system_def_t *acl_present_board_def(void) {
  acl_assert_locked();

  if (acl_present_board_is_valid_value) {
    return &builtin_prog_def_value;
  } else {
    return 0;
  }
}

int acl_present_board_is_valid(void) {
  acl_assert_locked();

  return acl_present_board_is_valid_value;
}

static void l_reset_present_board() {
  acl_assert_locked();

  acl_present_board_is_valid_value = 0;
}

// Determine user's offline device setting from the environment variable.
// If it's prefixed by "+", then it's in addition to any auto-discovered
// devices.
// If not, then we don't even probe for auto-discovered devices.
const char *acl_get_offline_device_user_setting(int *use_offline_only_ret) {
  int use_offline_only = 0;
  const char *setting = 0;
  const char *setting_deprecated = 0;
  const char *result = 0;
  static char warn_depr1 = 0;

  setting = acl_getenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
  setting_deprecated = acl_getenv("CL_CONTEXT_OFFLINE_DEVICE_ALTERA");
  if (!setting && setting_deprecated) {
    setting = setting_deprecated;
    if (0 == warn_depr1) {
      fprintf(stderr, "[Runtime Warning]: CL_CONTEXT_OFFLINE_DEVICE_ALTERA has "
                      "been deprecated. Use "
                      "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA instead.\n");
      warn_depr1 = 1;
    }
  }

  if (setting) {
    if (*setting == '+') {
      use_offline_only = ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY;
      result = setting + 1; // Skip over the leading "+"
    } else {
      use_offline_only = ACL_CONTEXT_OFFLINE_ONLY;
      result = setting;
    }
    // Treat empty string like no setting at all.
    if (*result == 0) {
      result = 0;
      use_offline_only = ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY;
    }
  } else {
    // Look for multi-process simulator before old simulator.
    setting = acl_getenv("CL_CONTEXT_MPSIM_DEVICE_INTELFPGA");
    // Check if simulation board spec directory is set, which implies
    // CL_CONTEXT_MPSIM_DEVICE_INTELFPGA if that is not set
    if (!setting && acl_getenv(INTELFPGA_SIM_DEVICE_SPEC_DIR)) {
      setting = "1"; // Use 1 simulator device by default
    }
    if (setting) {
      use_offline_only = ACL_CONTEXT_MPSIM;
      result = 0;
    }
  }

  *use_offline_only_ret = use_offline_only;
  return result;
}

int acl_init(const acl_system_def_t *newsys) {
  acl_assert_locked();

  // The user/tester really knows what they're doing.
  acl_reset();

  // Must have set the HAL first, so we can print errors if necessary.
  if (acl_get_hal() && l_is_valid_system_def(newsys)) {
    builtin_prog_def_value = *newsys;
    acl_present_board_is_valid_value =
        1; // Must come before acl_init_platform because that *uses* the defined
           // system
  }
  acl_init_platform();
  acl_init_profiler();
  return acl_present_board_is_valid_value;
}

// Initialize the HAL and load the builtin system definition.
//
// It will handle setting up an offline device, either exclusively (by
// default), or in addition to devices found via HAL probing.
//
// This function returns CL_TRUE if a hal is initialized and CL_FALSE
// if it is not.
cl_bool acl_init_from_hal_discovery(void) {
  int use_offline_only = 0;
  const acl_hal_t *board_hal;
  acl_assert_locked();

  (void)acl_get_offline_device_user_setting(&use_offline_only);

  // Two jobs:
  // 1. Set the HAL from the linked-in HAL library.
  // 2. Discover the device parameters by probing.
  acl_reset();

  switch (use_offline_only) {
  case ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY:
  case ACL_CONTEXT_MPSIM:
    board_hal = acl_mmd_get_system_definition(&builtin_prog_def_value,
                                              libraries_to_load);
    break;
  case ACL_CONTEXT_OFFLINE_ONLY:
    board_hal = acl_get_offline_hal();
    break;
  default:
    // Not a valid setting so don't know which HAL to load
    return CL_FALSE;
  }

  if (board_hal == NULL) {
    return CL_FALSE;
  }
  // Probe the HAL for a device.
  if (!acl_set_hal(board_hal)) {
    return CL_FALSE;
  }

  if (use_offline_only != ACL_CONTEXT_OFFLINE_ONLY) {
    acl_present_board_is_valid_value = 1;
  }
  acl_init_platform();
  acl_init_profiler();

  if (acl_get_hal() == nullptr) {
    return CL_FALSE;
  }

  return CL_TRUE;
}

void acl_reset(void) {
  acl_assert_locked();

  l_reset_present_board();

  acl_platform.offline_device = "";
  acl_platform.offline_mode = ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY;
  acl_platform.num_devices = 0;
  for (unsigned i = 0; i < ACL_MAX_DEVICE; ++i) {
    acl_platform.device[i] = _cl_device_id();
  }
  acl_platform.initialized = 0;
}

////////////////////////////////////////////////////
// Static functions

static int l_is_valid_range(int (*msg)(const char *, ...),
                            const std::string &name,
                            const acl_addr_range_t *range) {
  ptrdiff_t begin_addr = ((char *)range->begin) - ((char *)0);
  ptrdiff_t next_addr = ((char *)range->next) - ((char *)0);
  acl_assert_locked();

  if ((char *)range->next < (char *)range->begin) {
    msg("Invalid address range for %s: beginning of storage %x comes after "
        "next storage %x\n",
        name.c_str(), range->begin, range->next);
    return 0;
  }

  // Check alignment.
  if ((begin_addr & (ACL_MEM_ALIGN - 1)) & ~(ACL_MEM_ALIGN)) {
    msg("Invalid address range for %s: begin pointer %x should be aligned to "
        "%d bytes\n",
        name.c_str(), range->begin, ACL_MEM_ALIGN);
    return 0;
  }
  if ((next_addr & (ACL_MEM_ALIGN - 1)) & ~(ACL_MEM_ALIGN)) {
    msg("Invalid address range for %s: next pointer %x should be aligned to %d "
        "bytes\n",
        name.c_str(), range->next, ACL_MEM_ALIGN);
    return 0;
  }
  return 1;
}

int l_is_valid_system_def(const acl_system_def_t *sys) {
  int is_ok = 1;
  int (*msg)(const char *, ...) = acl_print_debug_msg;
  acl_assert_locked();

#define AND_ALSO(COND, ISSUE_MSG)                                              \
  do {                                                                         \
    if (is_ok) {                                                               \
      is_ok = COND;                                                            \
      if (!is_ok) {                                                            \
        ISSUE_MSG;                                                             \
      }                                                                        \
    }                                                                          \
  } while (0)

  AND_ALSO(sys != 0, msg("System definition is NULL pointer\n"));
  if (!is_ok) {
    return 0;
  }

  AND_ALSO(sys->num_devices <= ACL_MAX_DEVICE,
           msg("Too many devices %d > %d\n", sys->num_devices, ACL_MAX_DEVICE));

  if (!is_ok) {
    return 0;
  }

  // Check each device in turn
  for (cl_uint idev = 0; idev < sys->num_devices; idev++) {
    AND_ALSO(sys->device[idev].autodiscovery_def.num_global_mem_systems > 0,
             msg("Currently loaded device binary version is not supported\n"));
    for (unsigned imem = 0;
         imem < sys->device[idev].autodiscovery_def.num_global_mem_systems;
         imem++) {
      AND_ALSO(l_is_valid_range(msg, "global memory",
                                &sys->device[idev]
                                     .autodiscovery_def.global_mem_defs[imem]
                                     .range), );
      AND_ALSO(sys->device[idev]
                       .autodiscovery_def.global_mem_defs[imem]
                       .num_global_banks > 0,
               msg("num global banks (%d) should be positive.\n",
                   sys->device[idev]
                       .autodiscovery_def.global_mem_defs[imem]
                       .num_global_banks));
    }

    AND_ALSO(!sys->device[idev].autodiscovery_def.name.empty(),
             msg("Accelerator %d does not have a name\n", idev));

    // Check each accelerator block in turn.
    for (const auto &accel : sys->device[idev].autodiscovery_def.accel) {

      AND_ALSO(l_is_valid_range(msg, accel.iface.name.c_str(), &(accel.mem)), );

      AND_ALSO(!accel.iface.name.empty(),
               msg("Accelerator [%d][%d] has no name\n", idev, accel.id));

      // Now check arg specs.
      unsigned iarg = 0;
      for (const auto &arg_info : accel.iface.args) {
        switch (arg_info.addr_space) {
        case ACL_ARG_ADDR_LOCAL:
        case ACL_ARG_ADDR_GLOBAL:
        case ACL_ARG_ADDR_CONSTANT:
        case ACL_ARG_ADDR_NONE:
          break;
        default:
          AND_ALSO(0, msg("Accelerator [%d] \"%s\" arg %d is in wrong address "
                          "space %d\n",
                          accel.id, accel.iface.name.c_str(), iarg,
                          static_cast<int>(arg_info.addr_space)));
          break;
        }
        switch (arg_info.category) {
        case ACL_ARG_BY_VALUE:
        case ACL_ARG_MEM_OBJ:
        case ACL_ARG_SAMPLER:
          break;
        default:
          AND_ALSO(0,
                   msg("Accelerator [%d] \"%s\" arg %d has wrong category %d\n",
                       accel.id, accel.iface.name.c_str(), iarg,
                       static_cast<int>(arg_info.category)));
          break;
        }
        AND_ALSO(
            (arg_info.addr_space == ACL_ARG_ADDR_LOCAL || arg_info.size > 0),
            msg("Accelerator [%d] \"%s\" arg %d is not a __local argument but "
                "has 0 or negative size:  %d\n",
                accel.id, accel.iface.name.c_str(), iarg,
                static_cast<int>(arg_info.size)));
        // Support devices that use 64-bit pointers or 32-bit pointers
        AND_ALSO((arg_info.addr_space != ACL_ARG_ADDR_LOCAL ||
                  arg_info.size == 4 || arg_info.size == 8),
                 msg("Accelerator [%d] \"%s\" __local arg %d should have size "
                     "= 4 or 8  but is size:  %d\n",
                     accel.id, accel.iface.name.c_str(), iarg,
                     static_cast<int>(arg_info.size)));
        ++iarg;
      }
    }
  }
  return is_ok;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

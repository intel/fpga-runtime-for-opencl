// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_icd_dispatch.h>
#include <acl_mem.h>
#include <acl_platform.h>
#include <acl_printf.h>
#include <acl_profiler.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_svm.h>
#include <acl_thread.h>
#include <acl_util.h>

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 1024
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

static cl_context l_create_context(const cl_context_properties *properties,
                                   acl_notify_fn_t pfn_notify, void *user_data,
                                   cl_int *errcode_ret);
static cl_int l_finalize_context(cl_context context, cl_uint num_devices,
                                 const cl_device_id *devices);
static cl_int l_load_properties(cl_context context,
                                const cl_context_properties *properties);
static cl_int l_init_context_with_devices(cl_context context,
                                          cl_uint num_devices,
                                          const cl_device_id *devices);
static void
l_init_kernel_invocation_wrapper(acl_kernel_invocation_wrapper_t *wrapper,
                                 unsigned i);
static void l_forcibly_release_allocations(cl_context context);
static cl_device_id l_find_device_by_name(const std::string &name);
static cl_int l_update_program_library_root(cl_context context,
                                            const char *new_root);
static cl_int l_update_compile_command(cl_context context, const char *new_cmd);

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_context);

// Conditionally override the normal definition of BAIL() from acl_util.h
#ifdef ACL_DEBUG_BAIL
#define BAIL(STATUS)                                                           \
  do {                                                                         \
    debug_mode++;                                                              \
    acl_print_debug_msg("%s:%d bailing with status %d\n", __FILE__, __LINE__,  \
                        STATUS);                                               \
    if (errcode_ret) {                                                         \
      *errcode_ret = (STATUS);                                                 \
    }                                                                          \
    return 0;                                                                  \
  } while (0)
#endif

extern int platform_owner_pid; // Used to detect if user is creating contexts in
                               // multiple processes.

//////////////////////////////
// OpenCL API

// Create a context
ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL clCreateContextIntelFPGA(
    const cl_context_properties *properties, cl_uint num_devices,
    const cl_device_id *devices, acl_notify_fn_t pfn_notify, void *user_data,
    cl_int *errcode_ret) {
  cl_context context;
  cl_int status;
  acl_lock();

  context = l_create_context(properties, pfn_notify, user_data, &status);
  if (context == NULL || status != CL_SUCCESS) {
    acl_free_cl_context(context);
    UNLOCK_BAIL(status);
  }

  // Now check the devices.
  if (num_devices == 0) {
    acl_context_callback(context, "No devices specified");
    acl_free_cl_context(context);
    UNLOCK_BAIL(CL_INVALID_VALUE);
  }
  if (devices == 0) {
    acl_context_callback(context, "No device array specified");
    acl_free_cl_context(context);
    UNLOCK_BAIL(CL_INVALID_VALUE);
  }

  // Make sure all mentioned devices are valid.
  for (cl_uint i = 0; i < num_devices; i++) {
    if (!acl_device_is_valid_ptr(devices[i])) {
      acl_context_callback(context, "Invalid device specified");
      acl_free_cl_context(context);
      UNLOCK_BAIL(CL_INVALID_DEVICE);
    }

    if (devices[i]->opened_count) {
      if (context->uses_dynamic_sysdef && devices[i]->mode_lock == BUILT_IN) {
        acl_context_callback(
            context, "Could not create context with reprogramming enabled. A "
                     "device in the device list is currently in use in another "
                     "context created with reprogramming disabled.");
        acl_free_cl_context(context);
        UNLOCK_BAIL(CL_INVALID_VALUE);
      } else if (!context->uses_dynamic_sysdef &&
                 devices[i]->mode_lock == BINARY) {
        acl_context_callback(
            context, "Could not create context with reprogramming disabled. A "
                     "device in the device list is currently in use in another "
                     "context created with reprogramming enabled.");
        acl_free_cl_context(context);
        UNLOCK_BAIL(CL_INVALID_VALUE);
      }
    } else {
      // Since this is the first time creating a context for this device, we
      // lock all future context creation to the mode of the current setting
      if (context->uses_dynamic_sysdef) {
        devices[i]->mode_lock = BINARY;
      } else {
        devices[i]->mode_lock = BUILT_IN;
      }
    }
  }

  status = l_finalize_context(context, num_devices, devices);

  if (status != CL_SUCCESS) {
    UNLOCK_BAIL(status);
  }

  // Open the profiler output file after the first context creation
  acl_open_profiler_file();

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }
  // the context is created successfully, add it to the set
  acl_platform.contexts_set.insert(context);
  UNLOCK_RETURN(context);
}

ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL
clCreateContext(const cl_context_properties *properties, cl_uint num_devices,
                const cl_device_id *devices, acl_notify_fn_t pfn_notify,
                void *user_data, cl_int *errcode_ret) {
  return clCreateContextIntelFPGA(properties, num_devices, devices, pfn_notify,
                                  user_data, errcode_ret);
}
ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL clCreateContextFromTypeIntelFPGA(
    const cl_context_properties *properties, cl_device_type device_type,
    acl_notify_fn_t pfn_notify, void *user_data, cl_int *errcode_ret) {
  cl_context context = 0;
  cl_uint num_devices = 0;
  cl_int status;
  cl_device_id devices[ACL_MAX_DEVICE];
  acl_lock();

  context = l_create_context(properties, pfn_notify, user_data, &status);
  if (context == NULL || status != CL_SUCCESS) {
    acl_free_cl_context(context);
    UNLOCK_BAIL(status);
  }

  // Determine device IDs.
  status = clGetDeviceIDs(acl_get_platform(), device_type, ACL_MAX_DEVICE,
                          &devices[0], &num_devices);
  if (status != CL_SUCCESS || num_devices == 0) {
    acl_context_callback(context, "Device not found");
    acl_free_cl_context(context);
    UNLOCK_BAIL(CL_DEVICE_NOT_FOUND);
  }

  // Filter out devices.
  cl_uint i = 0;
  for (cl_uint j = 0; j < num_devices; j++) {
    if (devices[j]->opened_count) {
      if ((context->uses_dynamic_sysdef && devices[j]->mode_lock == BINARY) ||
          (!context->uses_dynamic_sysdef &&
           devices[j]->mode_lock == BUILT_IN)) {
        devices[i] = devices[j];
        i++;
      }
    } else {
      // Since this is the first time creating a context for this device, we
      // lock all future context creation to the mode of the current setting
      if (context->uses_dynamic_sysdef) {
        devices[j]->mode_lock = BINARY;
      } else {
        devices[j]->mode_lock = BUILT_IN;
      }
      devices[i] = devices[j];
      i++;
    }
  }
  num_devices = i;
  // Error out accordingly if all the devices got filtered out.
  if (!num_devices) {
    if (context->uses_dynamic_sysdef) {
      acl_context_callback(
          context, "Could not create context with reprogramming enabled. All "
                   "devices of the given device type are currently in use in "
                   "other contexts created with reprogramming disabled.");
      acl_free_cl_context(context);
      UNLOCK_BAIL(CL_DEVICE_NOT_AVAILABLE);
    } else {
      acl_context_callback(
          context, "Could not create context with reprogramming disabled. All "
                   "devices of the given device type are currently in use in "
                   "other contexts created with reprogramming enabled.");
      acl_free_cl_context(context);
      UNLOCK_BAIL(CL_DEVICE_NOT_AVAILABLE);
    }
  }

  status = l_finalize_context(context, num_devices, devices);

  if (status != CL_SUCCESS) {
    UNLOCK_BAIL(status);
  }

  // Open the profiler output file after the first context creation
  acl_open_profiler_file();

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }
  UNLOCK_RETURN(context);
}

ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL clCreateContextFromType(
    const cl_context_properties *properties, cl_device_type device_type,
    acl_notify_fn_t pfn_notify, void *user_data, cl_int *errcode_ret) {
  return clCreateContextFromTypeIntelFPGA(properties, device_type, pfn_notify,
                                          user_data, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainContextIntelFPGA(cl_context context) {
  acl_lock();

  // Note: Context creation uses acl_retain<> directly, but users must use
  // clRetainContext.
  // So it's ok for us to error out if the current reference count is 0.
  // That's why we use acl_context_is_valid() here instead of just
  // acl_is_valid_ptr().
  if (!acl_context_is_valid(context)) {
    UNLOCK_RETURN(CL_INVALID_CONTEXT);
  }
  acl_retain(context);
  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainContext(cl_context context) {
  return clRetainContextIntelFPGA(context);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseContextIntelFPGA(cl_context context) {
  acl_lock();

  // Error out if the reference count is already 0
  if (!acl_context_is_valid(context)) {
    UNLOCK_RETURN(CL_INVALID_CONTEXT);
  }

  // Must mirror what is retained in clRetainContext.
  // Since that method doesn't retain the constituent objects, we don't
  // release them until our own reference count is 0.

  if (acl_ref_count(context) > context->num_automatic_references) {
    acl_release(context);
  } else {
    // num_automatic_references is the ref count the context had when it was
    // given to the user. So, if we have a ref count equal to that, the user
    // must have run release and retain an equal number of times (before the
    // current release). Therefore, the current release will destroy the
    // context. Any remaining ref count are from circular references from the
    // command queues and memory objects attached to this context.

    // When we call clReleaseCommandQueue and clReleaseMemObject below,
    // they're gonna call clReleaseContext themselves. This stops us from
    // recursively trying to delete them again.
    if (context->is_being_freed) {
      acl_release(context);
      UNLOCK_RETURN(CL_SUCCESS);
    }
    context->is_being_freed = 1;

    // Make sure we clean up the elf files.  This should already be done by
    // clReleaseProgram.
    for (unsigned i = 0; i < context->num_devices; i++) {
      if (context->device[i]->loaded_bin != nullptr)
        context->device[i]->loaded_bin->unload_content();
      if (context->device[i]->last_bin != nullptr)
        context->device[i]->last_bin->unload_content();
    }

    // We have to close all devices associated with this context so they can be
    // opened by other processes
    acl_get_hal()->close_devices(context->num_devices, context->device);

    // remove the context from the context set in the platform
    acl_platform.contexts_set.erase(context);

    context->notify_fn = 0;
    context->notify_user_data = 0;

    if (context->auto_queue) {
      // Really subtle.
      // The command queue release needs to see the context as being valid.
      // That's why we had to defer the acl_release call.
      clReleaseCommandQueue(context->auto_queue);
      context->auto_queue = 0; // for clarity
    }

    if (context->user_event_queue) {
      clReleaseCommandQueue(context->user_event_queue);
      context->user_event_queue = 0; // for clarity
    }

    if (context->command_queue) {
      acl_free(context->command_queue);
    }

    clReleaseMemObject(context->unwrapped_host_mem);

    l_forcibly_release_allocations(context);

    acl_untrack_object(context);

    // all that should be left now is the single implicit retain from when
    // the cl_context was created
    assert(acl_ref_count(context) == 1);
    acl_free_cl_context(context);

    // Close the profiler output file after the last context release
    acl_close_profiler_file();
  }

  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseContext(cl_context context) {
  return clReleaseContextIntelFPGA(context);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetContextInfoIntelFPGA(
    cl_context context, cl_context_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  acl_lock();

  if (!acl_context_is_valid(context)) {
    UNLOCK_RETURN(CL_INVALID_CONTEXT);
  }
  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value,
                                 param_value_size_ret, context);

  RESULT_INIT;

  switch (param_name) {
  case CL_CONTEXT_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(context));
    break;
  case CL_CONTEXT_DEVICES:
    RESULT_BUF(&(context->device[0]),
               context->num_devices * sizeof(context->device[0]));
    break;
  case CL_CONTEXT_NUM_DEVICES:
    RESULT_UINT(context->num_devices);
    break;
  case CL_CONTEXT_PLATFORM:
    RESULT_PTR(acl_get_platform());
    break;

  // When returning the context properties, the size includes the
  // terminating NULL pointer.
  case CL_CONTEXT_PROPERTIES:
    RESULT_BUF(context->properties,
               context->num_property_entries * sizeof(cl_context_properties));
    break;
  default:
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                   "Invalid or unsupported context info query");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetContextInfo(cl_context context,
                                                 cl_context_info param_name,
                                                 size_t param_value_size,
                                                 void *param_value,
                                                 size_t *param_value_size_ret) {
  return clGetContextInfoIntelFPGA(context, param_name, param_value_size,
                                   param_value, param_value_size_ret);
}
//////////////////////////////
// Internals

int acl_context_is_valid(cl_context context) {
  acl_assert_locked();

  if (!acl_is_valid_ptr(context)) {
    return 0;
  }
  if (!acl_ref_count(context)) {
    return 0;
  }
  return 1;
}

int acl_context_uses_device(cl_context context, cl_device_id device) {
  unsigned int i;
  acl_assert_locked();

  // Assumes both context and device are valid.
  for (i = 0; i < context->num_devices; i++) {
    if (context->device[i] == device) {
      return 1;
    }
  }
  return 0;
}

static cl_context l_create_context(const cl_context_properties *properties,
                                   acl_notify_fn_t pfn_notify, void *user_data,
                                   cl_int *errcode_ret) {
  cl_context context = 0;
  cl_int status;

  acl_lock();

  if (user_data && !pfn_notify) {
    UNLOCK_BAIL(CL_INVALID_VALUE);
  }

  {
    // Blocking multiple_processing
    const char *allow_mp = NULL;
    // Check if user wants to disable the check.
    allow_mp = acl_getenv("CL_CONTEXT_ALLOW_MULTIPROCESSING_INTELFPGA");
    if (!allow_mp && platform_owner_pid != 0 &&
        platform_owner_pid != acl_get_pid()) {
      if (pfn_notify) {
        int lock_count = acl_suspend_lock();
        (pfn_notify)("Cannot create contexts in more than one process", 0, 0,
                     user_data);
        acl_resume_lock(lock_count);
      }
      UNLOCK_BAIL(CL_OUT_OF_RESOURCES);
    }
  }

  // Get the context, but don't retain it yet.
  context = acl_alloc_cl_context();
  if (context == 0) {
    if (pfn_notify) {
      int lock_count = acl_suspend_lock();
      (pfn_notify)("Could not allocate a context object", 0, 0, user_data);
      acl_resume_lock(lock_count);
    }
    UNLOCK_BAIL(CL_OUT_OF_HOST_MEMORY);
  }

  context->notify_fn = pfn_notify;
  context->notify_user_data = user_data;

  // Load the platform and compiler mode.
  status = l_load_properties(context, properties);
  if (status != CL_SUCCESS) {
    acl_free_cl_context(context);
    UNLOCK_BAIL(status);
  } // already called context error callback

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  UNLOCK_RETURN(context);
}

static cl_int l_finalize_context(cl_context context, cl_uint num_devices,
                                 const cl_device_id *devices) {
  cl_int status;
  acl_lock();

  status = acl_get_hal()->try_devices(num_devices, devices, &acl_platform);
  if (status) {
    acl_context_callback(context, "Could not open devices");
    acl_free_cl_context(context);
    UNLOCK_RETURN(status);
  }

  acl_retain(context);

  status = l_init_context_with_devices(context, num_devices, devices);
  if (status != CL_SUCCESS) {
    l_forcibly_release_allocations(context);
    acl_free_cl_context(context);
    UNLOCK_RETURN(status); // already signaled callback
  }

  acl_track_object(ACL_OBJ_CONTEXT, context);

  UNLOCK_RETURN(CL_SUCCESS);
}

// Analyze and load the context properties.
// Populates the given context's
//    properties
//    platform
//    compiler mode
// If ok, return CL_SUCCESS.
// Otherwise, it calls the callback function and returns an error.
static cl_int l_load_properties(cl_context context,
                                const cl_context_properties *properties) {
  const char *default_compile_cmd = 0;
  int env_override = 0;
  acl_assert_locked();

  // Set defaults.
  context->dispatch = &acl_icd_dispatch;
  context->compiler_mode = static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA);
  context->split_kernel = 0;
  context->is_being_freed = 0;
  context->eagerly_program_device_with_first_binary = 1;

  // This one is only overridden with an environment variable.
  {
    const char *override = acl_getenv("AOCL_EAGERLY_LOAD_FIRST_BINARY");
    if (override) {
      // There was a string.
      char *endptr = 0;
      long value = strtol(override, &endptr, 10);
      if (endptr != override) { // we parsed something
        context->eagerly_program_device_with_first_binary = (int)value;
      }
    }
  }

  // Environment variable can provide a default for compiler mode.
  {
    const char *override = 0;
    const char *override_deprecated = 0;
    override = acl_getenv("CL_CONTEXT_COMPILER_MODE_INTELFPGA");
    override_deprecated = acl_getenv("CL_CONTEXT_COMPILER_MODE_ALTERA");
    if (!override && override_deprecated) {
      override = override_deprecated;
      fprintf(stderr,
              "Warning: CL_CONTEXT_COMPILER_MODE_ALTERA has been deprecated. "
              "Use CL_CONTEXT_COMPILER_MODE_INTELFPGA instead.\n");
    }

    if (override) {
      // There was a string.
      char *endptr = 0;
      long value = strtol(override, &endptr, 10);
      if (endptr == override // no valid characters
          || *endptr         // an invalid character
          || (value < 0 || value >= (long)ACL_COMPILER_MODE_NUM_MODES)) {
        // the value of "ACL_COMPILER_MODE_NUM_MODES" is set in "acl_types.h"
        ERR_RET(CL_INVALID_VALUE, context,
                "Invalid compiler mode in environment variable "
                "CL_CONTEXT_COMPILER_MODE_INTELFPGA");
      }
      // Was ok.
      context->compiler_mode = static_cast<acl_compiler_mode_t>(value);
    }
  }

  // Environment variable can specify we always an offline device.
  if (!acl_platform.offline_device.empty()) {
    if (!l_find_device_by_name(acl_platform.offline_device))
      ERR_RET(CL_INVALID_VALUE, context,
              "Invalid offline device specified by environment variable "
              "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
  }

  // Get default for program_library_root.
  // The directory does not have to exist.
  // If the env var is not set, then use "aocl_program_library" in the
  // current directory.
  const auto *default_root = acl_getenv(
      "CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA"); // this one is public,
                                                        // in cl_ext_intelfpga.h
  if (!default_root) {
    default_root = "aocl_program_library";
  }
  auto status = l_update_program_library_root(context, default_root);
  if (status != CL_SUCCESS) {
    return status;
  } // already signaled.

  // Get user-specified compile command.
  // The default command depends on effective compiler mode, so defer
  // until later.
  const char *default_cmd = acl_getenv("CL_CONTEXT_COMPILE_COMMAND_INTELFPGA");
  if (default_cmd) {
    status = l_update_compile_command(context, default_cmd);
    if (status != CL_SUCCESS)
      return status; // already signaled.
  }

  context->num_property_entries = 0;

  if (properties) {
    const cl_context_properties *curr_prop;

    for (curr_prop = properties; curr_prop && *curr_prop; ++curr_prop) {

      // Check overflow.
      // Properties always come in pairs.
      // And we should account for the terminating 0.
      if (context->num_property_entries + 3 >
          ACL_MAX_NUM_CONTEXT_PROPERTY_ENTRIES)
        ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
                "Could not save context properties");

      switch (*curr_prop) {
      case CL_CONTEXT_PLATFORM: {
        // There's only one valid option for platform, so just check
        // that they've passed the right one
        cl_platform_id selected_platform = *((cl_platform_id *)(++curr_prop));
        if (selected_platform != acl_get_platform()) {
          // In later OpenCL 1.1 this is CL_INVALID_PROPERTY
          ERR_RET(
              CL_INVALID_PLATFORM, context,
              "Invalid platform specified with CL_CONTEXT_PLATFORM property");
        }
        context->properties[context->num_property_entries++] =
            CL_CONTEXT_PLATFORM;
        context->properties[context->num_property_entries++] =
            (cl_context_properties)selected_platform;
      } break;

      case CL_CONTEXT_COMPILER_MODE_INTELFPGA: {
        cl_context_properties proposed = *(++curr_prop);
        if (proposed < 0 || proposed >= ACL_COMPILER_MODE_NUM_MODES)
          ERR_RET(CL_INVALID_VALUE, context,
                  "Invalid CL_CONTEXT_COMPILER_MODE_INTELFPGA property");
        context->compiler_mode = static_cast<acl_compiler_mode_t>(proposed);
        context->properties[context->num_property_entries++] =
            CL_CONTEXT_COMPILER_MODE_INTELFPGA;
        context->properties[context->num_property_entries++] = proposed;
      } break;

      case CL_CONTEXT_COMPILE_COMMAND_INTELFPGA: {
        cl_context_properties proposed = *(++curr_prop);
        const char *name = (const char *)proposed;
        status = l_update_compile_command(context, name);
        if (status != CL_SUCCESS)
          return status;

        context->properties[context->num_property_entries++] =
            CL_CONTEXT_COMPILE_COMMAND_INTELFPGA;
        context->properties[context->num_property_entries++] = proposed;
      } break;

      case CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA: {
        cl_context_properties proposed = *(++curr_prop);
        const char *name = (const char *)proposed;
        status = l_update_program_library_root(context, name);
        if (status != CL_SUCCESS)
          return status;

        context->properties[context->num_property_entries++] =
            CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA;
        context->properties[context->num_property_entries++] = proposed;
      } break;

      default:
        ERR_RET(
            CL_INVALID_VALUE, // In later OpenCL 1.1 this is CL_INVALID_PROPERTY
            context, "Invalid context property");
      }
    }
  }
  // Always terminate list. After all, 'properties' might be empty!
  context->properties[context->num_property_entries++] = 0;

  (void)acl_get_offline_device_user_setting(&env_override);

  context->compiles_programs_incompletely = 0;
  switch (context->compiler_mode) {
  case static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA):
    context->compiles_programs = 0; // need to generate sys_description.txt
    context->programs_devices = 1;
    context->saves_and_restores_buffers_for_reprogramming = 1;
    context->uses_dynamic_sysdef = 1;
    context->uses_program_library = 0;
    break;

  case static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA):
    context->uses_program_library = 1;
    context->compiles_programs = 1;
    context->compiles_programs_incompletely = 1; // Only mode that does this.
    default_compile_cmd = "aoc -rtl -tidy";
    context->saves_and_restores_buffers_for_reprogramming =
        1; // for test purposes
    context->uses_dynamic_sysdef = 1;
    context->programs_devices = 0; // does not switch devices: might be offline
    break;

  case static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_ONLINE_INTELFPGA):
    // Emulate having an online compiler.
    // Not practical.
    context->uses_program_library = 1;
    context->compiles_programs = 1;
    context->saves_and_restores_buffers_for_reprogramming = 1;
    context->uses_dynamic_sysdef = 1;
    context->programs_devices = 1;
    default_compile_cmd = "aoc -tidy"; // Yes, the full compile.
    break;

  case static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY_INTELFPGA):
    context->uses_program_library = 1;
    context->compiles_programs = 0;
    context->saves_and_restores_buffers_for_reprogramming = 1;
    context->uses_dynamic_sysdef = 1;
    context->programs_devices = 1; // This is production mode. Must swap SOFs
    break;

  case static_cast<acl_compiler_mode_t>(
      CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA):
    context->split_kernel = 1;
    context->uses_program_library = 1;
    context->compiles_programs = 0;
    context->saves_and_restores_buffers_for_reprogramming = 1;
    context->uses_dynamic_sysdef = 1;
    context->programs_devices = 1;
    // In split_kernel mode it is difficult to determine which
    // binary should be loaded first.
    context->eagerly_program_device_with_first_binary = 0;
    break;

  default: // Embedded mode does none of these special things.
    context->compiles_programs = 0;
    context->programs_devices = 0;
    context->saves_and_restores_buffers_for_reprogramming = 0;
    context->uses_dynamic_sysdef = 0;
    context->uses_program_library = 0;
    break;
  }

  // We need backing store for the buffers.
  context->device_buffers_have_backing_store = 1;

  if (env_override == ACL_CONTEXT_MPSIM) {
    //  Simulator should support save/restore buffers around programming if
    //  reprogramming on-the-fly is supported
    context->saves_and_restores_buffers_for_reprogramming = 1;

    // Required to support compiler mode 3 in simulation mode.
    context->uses_dynamic_sysdef = 1;
    context->programs_devices = 1;
  }

  if (default_compile_cmd && context->compile_command == "") {
    // Need to supply a compile command.
    status = l_update_compile_command(context, default_compile_cmd);
    if (status != CL_SUCCESS)
      return status; // already signaled error.
  }

  if (context->uses_program_library) {
    // Canonicalize the program library root path.
    // This means we also make path too it.
    if (!acl_make_path_to_dir(context->program_library_root))
      ERR_RET(CL_INVALID_VALUE, context,
              "Could not make path to program libary root");
    auto root = acl_realpath_existing(context->program_library_root);
    status = l_update_program_library_root(context, root.c_str());
    if (status != CL_SUCCESS)
      return status; // already signaled error
  }

  return CL_SUCCESS;
}

static cl_device_id l_find_device_by_name(const std::string &name) {
  acl_assert_locked();

  for (unsigned i = 0; i < acl_platform.num_devices; ++i) {
    if (name == acl_platform.device[i].def.autodiscovery_def.name) {
      return &(acl_platform.device[i]);
    }
  }
  return 0;
}

// Initialize the given context.
// Yes, this is like a "placement new".
//
// We perform two kinds of allocations here:
//    1. A command queue to serialize memory transfers
//    2. Several memory buffers.
// We've already checked for command queue allocation. That should not fail.
//
// The memory object allocations can fail.  Since those objects are drawn
// from the platform, we have to perform cleanup on failure.
//
// We can easily roll back the mem buffer allocations via
// acl_forcibly_release_all_memory_for_context.
static cl_int l_init_context_with_devices(cl_context context,
                                          cl_uint num_devices,
                                          const cl_device_id *devices) {
  acl_assert_locked();

  acl_finalize_init_platform(num_devices, devices);

  context->command_queue = (cl_command_queue *)acl_malloc(
      ACL_INIT_COMMAND_QUEUE_ALLOC * sizeof(cl_command_queue));
  if (!context->command_queue) {
    return CL_OUT_OF_HOST_MEMORY;
  };
  context->num_command_queues_allocated = ACL_INIT_COMMAND_QUEUE_ALLOC;
  context->num_command_queues = 0;
  context->last_command_queue_idx_with_update = -1;
  context->auto_queue = 0;       // will get overwritten later
  context->user_event_queue = 0; // will get overwritten later
  context->num_automatic_references = 0;
  context->reprogram_buf_read_callback = 0;
  context->reprogram_buf_write_callback = 0;

  // Initialize the emulated memory region.
  // Do it early so we have an easier time on early exit conditions.
  // The release of memory is trivial.  (.first_block = NULL is key here).
  context->emulated_global_mem.is_user_provided = 0;
  context->emulated_global_mem.is_host_accessible = 1;
  context->emulated_global_mem.is_device_accessible = 1;
  context->emulated_global_mem.uses_host_system_malloc = 1;
  context->emulated_global_mem.range.begin = 0; // not used
  context->emulated_global_mem.range.next = 0;  // not used
  context->emulated_global_mem.first_block = NULL;
  // For now, just point global mem at ourselves.
  // We'll override this later if this context uses real (present) devcies.
  context->global_mem = &(context->emulated_global_mem);

  context->svm_list = NULL;

  // Add the devices.
  context->num_devices = 0;
  int num_present = 0;
  int num_absent = 0;
  for (cl_uint i = 0; i < num_devices; i++) {
    int usable = devices[i]->present;

    // Can't mix both (actually) present and absent devices because there
    // is no consistent way to place device global memory.
    if (devices[i]->present) {
      num_present++;
      // We should use real device memory, not emulated memory.
      context->global_mem = &(acl_platform.global_mem);
    } else {
      num_absent++;
    }
    if (num_present && num_absent)
      ERR_RET(CL_INVALID_DEVICE, context,
              "Can't create a context with both offline and online devices");

    usable = usable || acl_platform.offline_device ==
                           devices[i]->def.autodiscovery_def.name;

    if (!usable)
      ERR_RET(CL_DEVICE_NOT_AVAILABLE, context, "Device not available");

    // Mark the device(s) as opened
    devices[i]->has_been_opened = 1;

    context->device[i] = devices[i];
    acl_retain(devices[i]);

    // Determine *minimum* max alloc across all devices.
    cl_ulong max_alloc = 0;
    clGetDeviceInfo(devices[i], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc),
                    &max_alloc, 0);
    if (i == 0 || context->max_mem_alloc_size > max_alloc) {
      context->max_mem_alloc_size = max_alloc;
    }

    context->num_devices++;
  }
  assert(!(num_present && num_absent));

  cl_int status = CL_SUCCESS;

  // Create the command queue for internal purposes.
  // For example, profiling buffers are transferred over it.
  // Just attach it to the first device.
  // We know we can't run out of resources because we checked earlier if
  // we could allocate the command queue.
  // Enable profiling, but not out-of-order execution.
  // Out-of-order on mem objects is a bad bad bad thing.
  context->auto_queue = clCreateCommandQueue(
      context, devices[0], CL_QUEUE_PROFILING_ENABLE, &status);
  if (status != CL_SUCCESS) {
    acl_free(context->command_queue);
    return CL_OUT_OF_HOST_MEMORY;
  } // already signalled callback

  // Create the user event queue.
  // The OpenCL spec says clGetEventInfo with query property
  // CL_EVENT_COMMAND_QUEUE should return NULL so logically we are not supposed
  // to associate user events with a command queue. However, internally we
  // associate user events with a special out of order command queue which
  // should never be exposed to the end user. Profiling is disabled on this
  // special command queue because it is an error to call
  // clGetEventProfilingInfo on a user event.
  context->user_event_queue =
      clCreateCommandQueue(context, devices[0], 0, &status);
  if (status != CL_SUCCESS || context->user_event_queue == NULL) {
    acl_free(context->command_queue);
    return CL_OUT_OF_HOST_MEMORY;
  } // already signalled callback
  // This queue is special.
  // It does not submit commands.
  context->user_event_queue->submits_commands = 0;
  // It is out of order.
  context->user_event_queue->properties |=
      static_cast<cl_command_queue_properties>(
          CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);

  // Create the cl_mem handle for all of host memory.
  // This is used internally by buffer read/write commands
  // to talk about host memory that the caller has given
  // us as a plain void pointer.
  //
  // The clCreateBuffer API doesn't allow us to use a host pointer
  // of 0.  So use ACL_MEM_ALIGN as the base pointer.
  // When we wrap a raw pointer, we have to subtract ACL_MEM_ALIGN
  // from its value.
  context->unwrapped_host_mem = 0; // not actually an allocated buffer.
  context->unwrapped_host_mem =
      clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR,
                     (size_t)-1 - ACL_MEM_ALIGN, // All of memory!
                     (void *)ACL_MEM_ALIGN, &status);
  if (status != CL_SUCCESS || context->unwrapped_host_mem == NULL) {
    acl_free(context->command_queue);
    return CL_OUT_OF_HOST_MEMORY;
  } // already signalled callback
  // This memory is never considered SVM memory, regardless of what devices are
  // available
  context->unwrapped_host_mem->is_svm = CL_FALSE;

  // Initialize invocation image wrappers and images.
  context->invocation_wrapper = 0;
  context->num_invocation_wrappers_allocated = 0;

  // Remember how many automatic references were attached to this context.
  // This tells clReleaseContext when to really shut down the context
  // object.
  context->num_automatic_references = acl_ref_count(context);

  return CL_SUCCESS;
}

// Release resources owned by this context.
// This is called when the context has been retained, and
// context->global_mem is valid.
// Does not release the context
static void l_forcibly_release_allocations(cl_context context) {
  acl_assert_locked();

  if (context) {
    unsigned int idevice;
    // Double check: should not have failed this allocation because we
    // pre-check.
    context->auto_queue = 0;

    if (context->user_event_queue) {
      clReleaseCommandQueue(context->user_event_queue);
      context->user_event_queue = 0;
    }

    if (context->invocation_wrapper) {
      for (unsigned int i = 0; i < context->num_invocation_wrappers_allocated;
           i++) {
        auto *const wrapper = context->invocation_wrapper[i];
        if (wrapper->image->arg_value) {
          acl_delete_arr(wrapper->image->arg_value);
        }
        // invoke non-default destructors of non-POD types, e.g., std::vector
        wrapper->~acl_kernel_invocation_wrapper_t();
        acl_free(wrapper);
      }
      acl_free(context->invocation_wrapper);
      context->invocation_wrapper = 0;
    }
    context->num_invocation_wrappers_allocated = 0;

    for (cl_event event : context->used_events) {
      acl_free_cl_event(event);
    }
    context->used_events.clear();

    while (!context->free_events.empty()) {
      cl_event event = context->free_events.front();
      context->free_events.pop_front();
      acl_free_cl_event(event);
    }

    // During initialization we retained all the devices before first
    // possible failure.
    // During release, num_devices is still valid
    for (idevice = 0; idevice < context->num_devices; idevice++) {
      acl_release(context->device[idevice]);
    }

    // Buffers might have been allocated.
    acl_forcibly_release_all_memory_for_context(context);
    acl_forcibly_release_all_svm_memory_for_context(context);
  }
}

// Update the storage for program libary root, but don't try to
// canonicalize into an absolute pathname.
// We make a copy of new_root
static cl_int l_update_program_library_root(cl_context context,
                                            const char *new_root) {
  acl_assert_locked();

  // In OpenCL 1.2, use CL_INVALID_PROPERTY for errors.
  // But not available in 1.1

  if (!new_root)
    ERR_RET(CL_INVALID_VALUE, context,
            "NULL pointer provided for program library root property");

  context->program_library_root = new_root;

  return CL_SUCCESS;
}

// Update the storage for compile command.
// We make a copy of new_cmd
static cl_int l_update_compile_command(cl_context context,
                                       const char *new_cmd) {
  acl_assert_locked();

  // In OpenCL 1.2, use CL_INVALID_PROPERTY for errors.
  // But not available in 1.1

  if (!new_cmd)
    ERR_RET(CL_INVALID_VALUE, context,
            "NULL pointer provided for program compile command property");

  context->compile_command = new_cmd;
  return CL_SUCCESS;
}

// Update internal data structures in response to external messages.
// Do this as part of a waiting loop, along with acl_hal_yield();
void acl_update_context(cl_context context) {
  int num_updates = 0;
  unsigned iters = 0;
  const unsigned max_iters = 10000; // This is unreasonably high.
  acl_assert_locked();

  do {
    num_updates = 0;
    // Check if an ecc interrupt occured (platform flag is raised first for a
    // quick check)
    if (acl_platform.device_exception_platform_counter) {
      // Perform a run through all devices to find the one causing the ecc
      // interrupt
      for (cl_uint device_iter = 0; device_iter < context->num_devices;
           device_iter++) {
        if (context->device[device_iter]->device_exception_status) {
          cl_device_id device = context->device[device_iter];

          if (device->exception_notify_fn) {
            acl_exception_notify_fn_t notify_fn = device->exception_notify_fn;
            void *notify_user_data = device->exception_notify_user_data;

            for (unsigned int i = 0; i < sizeof(CL_EXCEPTION_TYPE_INTEL) * 8;
                 ++i) {
              CL_EXCEPTION_TYPE_INTEL exception_type = 1ULL << i;
              if (device->device_exception_status & exception_type) {
                int lock_count = acl_suspend_lock();
                notify_fn(exception_type, device->exception_private_info[i],
                          device->exception_cb[i], notify_user_data);
                acl_resume_lock(lock_count);
              }
            }

          } else {
            acl_context_callback(
                context, "Device exception has occured, however the device "
                         "callback function was not registered\n");
          }
          // All exceptions for the device were processed
          device->device_exception_status = 0;
          acl_platform.device_exception_platform_counter -= 1;
          for (unsigned int i = 0; i < sizeof(CL_EXCEPTION_TYPE_INTEL) * 8;
               ++i) {
            if (device->exception_private_info[i]) {
              acl_free(device->exception_private_info[i]);
              device->exception_private_info[i] = NULL;
              device->exception_cb[i] = 0;
            }
          }
        }
      }
    }

    // Ensure forward progress is made on devices that uses yield
    if (acl_get_hal()->yield) {
      acl_get_hal()->yield(context->num_devices, context->device);
    }

    // Starting the update round from right after the last command_queue with
    // update. Dont forget to wrap around.
    const int starting_idx = context->last_command_queue_idx_with_update + 1;
    for (int count = 0; count < context->num_command_queues; count++) {

      int index = (count + starting_idx) % context->num_command_queues;
      const int updates = acl_update_queue(context->command_queue[index]);
      if (updates) {
        context->last_command_queue_idx_with_update = index;
      }

      if (!acl_is_retained(context->command_queue[index])) {
        // the command queue is not retained, is marked for deleted
        acl_delete_command_queue(context->command_queue[index]);
        // deleting a command queue places a live command queue at this index
        // itterate on this index again
        if (--count < 0) {
          count = 0;
        }
      }

      acl_print_debug_msg(" cq[%d] had %d updates\n", index, updates);

      num_updates += updates;
    }
  } while ((num_updates > 0) && (++iters < max_iters));

  if (num_updates) {
    acl_print_debug_msg(" context[%p] idle update: still had %d 'updates' "
                        "after %u iters. Breaking out\n",
                        context, num_updates, iters);
  }
}

// Functionality: acl_idle_update firstly updates the current context, and then
// updates the sibling contexts within the same platform Reason for the
// extension:
//   The previous implementation only takes a single context into account. In
//   the multiple contexts scenario, when there are dependencies among contexts,
//   updating the current context is insufficient if the dependency relies on a
//   callback when an event in another context runs into a specific exec status
//   after the context switch. To resolve this dependency issue, a feasible way
//   is by updating every context when acl_idle_update is called.
//
void acl_idle_update(cl_context context) {
  // firstly update the current context
  acl_update_context(context);
  // update the other contexts from the platform
  for (auto _context : acl_platform.contexts_set) {
    if (context != _context)
      acl_update_context(_context);
  }
  // if there are any new updates on the current contect done by updating the
  // other it will be handled in clflush/clfinish or acl_wait_for_event where
  // acl_idle_update are called
}

static void
l_init_kernel_invocation_wrapper(acl_kernel_invocation_wrapper_t *wrapper,
                                 unsigned i) {
  acl_assert_locked();

  if (wrapper) {
    // invoke non-default constructors of non-POD types, e.g., std::vector
    new (wrapper) acl_kernel_invocation_wrapper_t{};
    wrapper->id = i;
    wrapper->image = &(wrapper->image_storage);
    wrapper->image->arg_value = nullptr;
    wrapper->event = 0;
    acl_reset_ref_count(wrapper);
  }
}

acl_kernel_invocation_wrapper_t *
acl_get_unused_kernel_invocation_wrapper(cl_context context) {
  acl_assert_locked();

  for (unsigned ii = 0; ii < context->num_invocation_wrappers_allocated; ii++) {
    acl_kernel_invocation_wrapper_t *candidate =
        context->invocation_wrapper[ii];
    if (candidate == 0) {
      // Could have happened on a partial allocation from an earlier step.
      context->invocation_wrapper[ii] =
          (acl_kernel_invocation_wrapper_t *)acl_malloc(
              sizeof(acl_kernel_invocation_wrapper_t));
      candidate = context->invocation_wrapper[ii];

      l_init_kernel_invocation_wrapper(candidate, ii);
    }
    if (candidate && !acl_is_retained(candidate))
      return candidate;
  }
  // If we got here, then need to allocate.
  {
    unsigned first_new, limit, ij;
    if (0 == context->invocation_wrapper) {
      // First time allocation
      const unsigned initial_alloc = 50;
      context->invocation_wrapper =
          (acl_kernel_invocation_wrapper_t **)acl_malloc(
              initial_alloc * sizeof(acl_kernel_invocation_wrapper_t *));
      if (!context->invocation_wrapper) {
        acl_context_callback(context,
                             "Cannot allocate space for kernel invocations");
        return 0;
      }
      first_new = 0;
      limit = initial_alloc;
    } else {
      const unsigned next_size =
          context->num_invocation_wrappers_allocated * 2 + 50;
      if (next_size < context->num_invocation_wrappers_allocated) {
        return 0;
      }
      {

        acl_kernel_invocation_wrapper_t **newloc =
            (acl_kernel_invocation_wrapper_t **)acl_realloc(
                context->invocation_wrapper,
                next_size * sizeof(acl_kernel_invocation_wrapper_t *));
        if (!newloc) {
          acl_context_callback(
              context, "Cannot allocate space for more kernel invocations");
          return 0;
        }
        context->invocation_wrapper = newloc;
        first_new = context->num_invocation_wrappers_allocated;
        limit = next_size;
      }
    }
    // Initialize the new entries.
    context->num_invocation_wrappers_allocated = limit;
    for (ij = first_new; ij < limit; ij++) {
      context->invocation_wrapper[ij] = 0;
    }
    // Now fill them in.
    for (ij = first_new; ij < limit; ij++) {
      context->invocation_wrapper[ij] =
          (acl_kernel_invocation_wrapper_t *)acl_malloc(
              sizeof(acl_kernel_invocation_wrapper_t));
      if (!context->invocation_wrapper[ij]) {
        acl_context_callback(
            context, "Cannot allocate space for another kernel invocation");
        return 0;
      }
      l_init_kernel_invocation_wrapper(context->invocation_wrapper[ij], ij);
    }
    // If we get here, then the first one must be available.
    return context->invocation_wrapper[first_new];
  }
}

void acl_context_callback(cl_context context, const std::string errinfo) {
  // Call the notifcation function registered during context creation.
  // We don't support the special info (middle two arguments).
  if (context && context->notify_fn) {
    acl_notify_fn_t notify_fn = context->notify_fn;
    void *notify_user_data = context->notify_user_data;

    int lock_count = acl_suspend_lock();
    notify_fn(errinfo.c_str(), 0, 0, notify_user_data);
    acl_resume_lock(lock_count);
  }
}

// Called when runtime has been waiting for device update for a while
void acl_context_print_hung_device_status(cl_context context) {
  acl_get_hal()->get_device_status(context->num_devices, context->device);
}
#ifdef __GNUC__
#pragma GCC visibility pop
#endif

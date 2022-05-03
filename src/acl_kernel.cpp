// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <unordered_map>
#include <vector>

// External library headers.
#include <CL/opencl.h>
#include <acl_hash/acl_hash.h>
#include <acl_threadsupport/acl_threadsupport.h>
#include <pkg_editor/pkg_editor.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hostch.h>
#include <acl_icd_dispatch.h>
#include <acl_kernel.h>
#include <acl_mem.h>
#include <acl_profiler.h>
#include <acl_program.h>
#include <acl_sampler.h>
#include <acl_support.h>
#include <acl_svm.h>
#include <acl_types.h>
#include <acl_usm.h>
#include <acl_util.h>
#include <acl_visibility.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Kernels
// ========
//
// Lifecycle of cl_kernel:
//    States are:
//       "new"             - initial state. It's attached to a built program.
//       "ready_to_run"    - All its arguments ar specified
//
// Data model:
//
//    cl_kernel has:
//       - name
//       - reference to program
//       - reference to interface, discovered via the
//       program->context->device[]->name
//       - arg values
//
//    acl_kernel_interface_t has:
//       - num_args
//       - arg_info[]
//          .addr_space: one of LOCAL|GLOBAL|CONSTANT
//          .category: PLAIN, MEM_OBJ, SAMPLER
//          .size

//////////////////////////////
// Variables and macros

// Prints more logs for debugging purposes.
static int debug_verbosity = 0;
#define ACL_KERNEL_DEBUG_MSG_VERBOSE(verbosity, m, ...)                        \
  do {                                                                         \
    if (debug_verbosity >= verbosity) {                                        \
      printf((m), ##__VA_ARGS__);                                              \
      fflush(stdout);                                                          \
    }                                                                          \
  } while (0)

// Local functions

static size_t l_round_up_for_alignment(size_t x);

static int l_init_kernel(cl_kernel kernel, cl_program program,
                         const acl_accel_def_t *accel_def,
                         const acl_device_binary_t *dev_bin,
                         cl_int *errcode_ret);

static cl_int l_load_consistently_built_kernels_in_program(
    cl_program program,
    std::vector<std::pair<const acl_device_binary_t *, const acl_accel_def_t *>>
        &accel_ret);

static int l_kernel_interfaces_match(const acl_accel_def_t &a,
                                     const acl_accel_def_t &b);

static size_t l_local_mem_size(cl_kernel kernel);

static cl_int l_enqueue_kernel_with_type(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event, cl_command_type type);
static void l_get_arg_offset_and_size(cl_kernel kernel, cl_uint arg_index,
                                      size_t *start_idx_ret, size_t *size_ret);
static cl_int
l_copy_and_adjust_arguments_for_device(cl_kernel kernel, cl_device_id device,
                                       char *buf, cl_uint *num_bytes,
                                       acl_mem_migrate_t *memory_migration);

static void l_abort_use_of_wrapper(acl_kernel_invocation_wrapper_t *wrapper);

static void l_complete_kernel_execution(cl_event event);

static cl_bool l_check_mem_type_support_on_kernel_arg(
    cl_kernel kernel, cl_uint arg_index,
    acl_system_global_mem_type_t expected_type);

unsigned int l_get_kernel_arg_mem_id(const cl_kernel kernel, cl_uint arg_index);

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_kernel);

//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainKernelIntelFPGA(cl_kernel kernel) {
  acl_lock();
  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }
  acl_retain(kernel);
  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainKernel(cl_kernel kernel) {
  return clRetainKernelIntelFPGA(kernel);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseKernelIntelFPGA(cl_kernel kernel) {
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  acl_print_debug_msg("Release kernel %p\n", kernel);
  if (1 == acl_ref_count(kernel)) {

    // kernel had profile data, free the buffer
    if (kernel->profile_data) {
      acl_free(kernel->profile_data);
      kernel->profile_data = 0;
    }

    if (kernel->printf_device_buffer) {
      clReleaseMemObject(kernel->printf_device_buffer);
      kernel->printf_device_buffer = 0;
    }

    if (kernel->printf_device_ptr) {
      clSVMFree(kernel->program->context, kernel->printf_device_ptr);
      kernel->printf_device_ptr = 0;
    }

    if (kernel->arg_value) {
      acl_delete_arr(kernel->arg_value);
      kernel->arg_value = nullptr;
    }

    acl_untrack_object(kernel);

    acl_release(kernel);
    acl_program_forget_kernel(kernel->program, kernel);
    clReleaseProgram(kernel->program);

  } else {
    acl_release(kernel);
  }
  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseKernel(cl_kernel kernel) {
  return clReleaseKernelIntelFPGA(kernel);
}

ACL_EXPORT
CL_API_ENTRY cl_kernel CL_API_CALL clCreateKernelIntelFPGA(
    cl_program program, const char *kernel_name, cl_int *errcode_ret) {
  cl_int status;
  cl_kernel kernel = 0;

  acl_lock();

  // Can't call the callback, because we have no valid context.
  if (!acl_program_is_valid(program))
    UNLOCK_BAIL(CL_INVALID_PROGRAM);

  if (!kernel_name)
    UNLOCK_BAIL_INFO(CL_INVALID_VALUE, program->context, "kernel_name is NULL");

  // What device program is associated with this kernel?
  // Right now we only support one device per kernel.
  const acl_device_binary_t *dev_bin = nullptr;
  const auto *accel_def = acl_find_accel_def(program, kernel_name, dev_bin,
                                             &status, program->context, 0);

  if (status != CL_SUCCESS)
    UNLOCK_BAIL(status); // already signaled callback

  kernel = acl_program_alloc_kernel(program);
  if (kernel == 0) {
    UNLOCK_BAIL_INFO(CL_OUT_OF_HOST_MEMORY, program->context,
                     "Could not allocate a program object");
  }

  l_init_kernel(kernel, program, accel_def, dev_bin, errcode_ret);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  UNLOCK_RETURN(kernel);
}

ACL_EXPORT
CL_API_ENTRY cl_kernel CL_API_CALL clCreateKernel(cl_program program,
                                                  const char *kernel_name,
                                                  cl_int *errcode_ret) {
  return clCreateKernelIntelFPGA(program, kernel_name, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCreateKernelsInProgramIntelFPGA(
    cl_program program, cl_uint num_kernels, cl_kernel *kernels,
    cl_uint *num_kernels_ret) {
  acl_lock();

  if (!acl_program_is_valid(program)) {
    UNLOCK_RETURN(CL_INVALID_PROGRAM);
  }

  auto context = program->context;

  std::vector<std::pair<const acl_device_binary_t *, const acl_accel_def_t *>>
      accel_ret;
  auto status =
      l_load_consistently_built_kernels_in_program(program, accel_ret);

  if (status != CL_SUCCESS) {
    UNLOCK_RETURN(status); // already signaled
  }
  if (accel_ret.size() == 0) {
    UNLOCK_ERR_RET(
        CL_INVALID_PROGRAM_EXECUTABLE, context,
        "No kernels were built across all devices with the same interface");
  }

  // Check return buffer spec
  if (num_kernels == 0 && kernels) {
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                   "num_kernels is zero but kernels array is specified");
  }
  if (num_kernels > 0 && kernels == 0) {
    UNLOCK_ERR_RET(
        CL_INVALID_VALUE, context,
        "num_kernels is non-zero but kernels array is not specified");
  }

  if (kernels) {
    // User wants to send the kernels back.

    // Result buffer isn't big enough.
    if (num_kernels < accel_ret.size()) {
      UNLOCK_RETURN(CL_INVALID_VALUE);
    }

    // The definitions are in accel_ret. Create the kernels.
    status = CL_SUCCESS;
    for (cl_uint i = 0; i < accel_ret.size() && status == CL_SUCCESS; ++i) {
      cl_kernel kernel = acl_program_alloc_kernel(program);
      if (kernel) {
        l_init_kernel(kernel, program, accel_ret[i].second, accel_ret[i].first,
                      &status);
        kernels[i] = kernel;
      } else {
        status = CL_OUT_OF_HOST_MEMORY;
        acl_context_callback(context, "Could not allocate a kernel object");
        // Unwind the ones we've created
        for (cl_uint j = 0; j < i; j++) {
          clReleaseKernel(kernels[j]);
        }
      }
    }
  }

  if (num_kernels_ret)
    *num_kernels_ret = static_cast<cl_uint>(accel_ret.size());

  UNLOCK_RETURN(status);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clCreateKernelsInProgram(cl_program program, cl_uint num_kernels,
                         cl_kernel *kernels, cl_uint *num_kernels_ret) {
  return clCreateKernelsInProgramIntelFPGA(program, num_kernels, kernels,
                                           num_kernels_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgIntelFPGA(cl_kernel kernel,
                                                        cl_uint arg_index,
                                                        size_t arg_size,
                                                        const void *arg_value) {
  const acl_kernel_arg_info_t *arg_info = 0;
  cl_context context;
  cl_bool is_pipe = CL_FALSE;
  cl_bool is_sampler = CL_FALSE;
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  context = kernel->program->context;

  if (arg_index >= kernel->accel_def->iface.args.size()) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_INDEX, context,
                   "Argument index is too large");
  }

  arg_info = &(kernel->accel_def->iface.args[arg_index]);

  // Check for valid mem object or sampler
  if (arg_info->category == ACL_ARG_MEM_OBJ) {
    // In OpenCL 1.2 a pointer to a NULL value is also allowed for arg_values
    // representing buffers.
    if (arg_value && (*(cl_mem *)arg_value) &&
        !acl_mem_is_valid(*(cl_mem *)arg_value))
      UNLOCK_ERR_RET(CL_INVALID_MEM_OBJECT, context,
                     "Non-memory object passed in as memory object argument");

  } else if (arg_info->category == ACL_ARG_SAMPLER) {
    if (arg_value && !acl_sampler_is_valid(*(cl_sampler *)arg_value))
      UNLOCK_ERR_RET(CL_INVALID_SAMPLER, context,
                     "Non-sampler object passed in as sampler object argument");
    is_sampler = CL_TRUE;
  } else if (arg_size != arg_info->size && arg_value &&
             acl_sampler_is_valid(*(cl_sampler *)arg_value)) {
    is_sampler = CL_TRUE;
  }

  // Check argument size, and value pointer.
  switch (arg_info->addr_space) {
  case ACL_ARG_ADDR_LOCAL: /* Size is number of local bytes to allocate */
    if (arg_size == 0) {
      UNLOCK_ERR_RET(CL_INVALID_ARG_SIZE, context,
                     "Pointer-to-local argument specified zero size");
    }
    if (arg_value != 0) {
      UNLOCK_ERR_RET(
          CL_INVALID_ARG_VALUE, context,
          "Pointer-to-local argument specified with a non-null value");
    }

    /* We instantiated a specific mem capacity to handle this pointer.
     * Make sure that user didn't ask for more at runtime than they
     * specified (and we instantiated) at kernel compile time */
    {
      unsigned lmem_size_instantiated = arg_info->lmem_size_bytes;
      if (arg_size > lmem_size_instantiated) {
        UNLOCK_ERR_RET(
            CL_INVALID_ARG_SIZE, context,
            "Pointer-to-local argument requested size is larger than "
            "maximum specified at compile time");
      }
    }
    break;

  case ACL_ARG_ADDR_GLOBAL:
  case ACL_ARG_ADDR_CONSTANT:
    if (arg_size != sizeof(cl_mem)) {
      UNLOCK_ERR_RET(
          CL_INVALID_ARG_SIZE, context,
          "Pointer-to-global or Pointer-to-constant argument size is "
          "not the size of cl_mem");
    }
    // Can pass NULL or pointer to NULL in arg_value, or it must be a valid
    // memory object.
    if (arg_value && (*(cl_mem *)arg_value) &&
        !acl_mem_is_valid(*(cl_mem *)arg_value)) {
      UNLOCK_ERR_RET(
          CL_INVALID_ARG_VALUE, context,
          "Pointer-to-global or Pointer-to-constant argument value is "
          "not a valid memory object");
    }

    if (arg_value && (*(cl_mem *)arg_value) &&
        arg_info->type_qualifier == ACL_ARG_TYPE_PIPE &&
        (*(cl_mem *)arg_value)->mem_object_type == CL_MEM_OBJECT_PIPE) {
      is_pipe = CL_TRUE;
    }
    // If this buffer is an SVM buffer, assume that the user wants the memory to
    // be in sync. Treat this the same as an SVM kernel arg and return.
    if (arg_value && (*(cl_mem *)arg_value) && (*(cl_mem *)arg_value)->is_svm) {
      UNLOCK_RETURN(clSetKernelArgSVMPointerIntelFPGA(
          kernel, arg_index, (*(cl_mem *)arg_value)->host_mem.aligned_ptr));
    }
    break;

  case ACL_ARG_ADDR_NONE:
    if (is_sampler && arg_value != 0 &&
        acl_sampler_is_valid_ptr(*((cl_sampler *)arg_value))) {
      if (arg_size != sizeof(cl_sampler)) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_SIZE, context,
                       "Sampler argument size is not the size of cl_sampler");
      }
      if (arg_info->size != sizeof(int)) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_SIZE, context,
                       "Argument size is the wrong size");
      }
    } else if (arg_size == sizeof(cl_mem) &&
               acl_pipe_is_valid_pointer(*((cl_mem *)arg_value), kernel)) {
      is_pipe = CL_TRUE;
    } else if (arg_size != arg_info->size) {
      UNLOCK_ERR_RET(CL_INVALID_ARG_SIZE, context,
                     "Argument size is the wrong size");
    }
    if (arg_value == 0) {
      UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context, "Argument value is NULL");
    }
    break;
  }

  // May be a pipe - but currently the pipe object is empty (no allocated
  // memory) and we don't actually read pipe arguments in the kernel so we
  // shouldn't spend time setting up the argument properly.
  if (is_pipe) {
    cl_mem pipe_ptr = *((cl_mem *)arg_value);

    kernel->arg_is_svm[arg_index] = CL_FALSE;
    kernel->arg_is_ptr[arg_index] = CL_FALSE;
    kernel->arg_defined[arg_index] = 1;

    /* If this is a host pipe, create a host channel and bind them together */
    if (arg_info->host_accessible && pipe_ptr->host_pipe_info != NULL) {
      if (pipe_ptr->host_pipe_info->m_binded_kernel != NULL) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                       "This pipe has already been bound to a kernel. Cannot "
                       "rebind to a new kernel");
      }

      // Check to see if the kernel argument's width matches up with our cl_pipe
      if (!context->uses_dynamic_sysdef) {
        // In mode 3
        // All the device need to have the same host pipe def
        for (unsigned int i = 0; i < kernel->program->num_devices; ++i) {
          bool found = false;
          for (const auto &hostpipe_info :
               kernel->program->device[i]
                   ->def.autodiscovery_def.acl_hostpipe_info) {
            if (arg_info->pipe_channel_id == hostpipe_info.name) {
              // Check direction
              if (pipe_ptr->flags & CL_MEM_HOST_READ_ONLY &&
                  hostpipe_info.is_dev_to_host) {
                // Direction match
              } else if (pipe_ptr->flags & CL_MEM_HOST_WRITE_ONLY &&
                         hostpipe_info.is_host_to_dev) {
                // Direction match
              } else {
                UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                               "Host accessible pipe direction is not the same "
                               "of cl_pipe");
              }
              // Check width
              if (pipe_ptr->fields.pipe_objs.pipe_packet_size !=
                  hostpipe_info.data_width) {
                UNLOCK_ERR_RET(CL_INVALID_ARG_SIZE, context,
                               "Host accessible pipe size is not the same size "
                               "of cl_pipe");
              }
              // Check max buffer size
              if (pipe_ptr->fields.pipe_objs.pipe_max_packets >
                  hostpipe_info.max_buffer_depth) {
                UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                               "Host accessible pipe max packets size is "
                               "smaller than cl_pipe requested size");
              }
              found = true;
            }
          }
          assert(found);
        }
      } else {
        // Not in mode 3
        bool found = false;
        for (const auto &hostpipe_info :
             kernel->dev_bin->get_devdef()
                 .autodiscovery_def.acl_hostpipe_info) {
          if (arg_info->pipe_channel_id == hostpipe_info.name) {
            // Check direction
            if (pipe_ptr->flags & CL_MEM_HOST_READ_ONLY &&
                hostpipe_info.is_dev_to_host) {
              // Direction match
            } else if (pipe_ptr->flags & CL_MEM_HOST_WRITE_ONLY &&
                       hostpipe_info.is_host_to_dev) {
              // Direction match
            } else {
              UNLOCK_ERR_RET(
                  CL_INVALID_ARG_VALUE, context,
                  "Host accessible pipe direction is not the same of cl_pipe");
            }
            // Check width
            if (pipe_ptr->fields.pipe_objs.pipe_packet_size !=
                hostpipe_info.data_width) {
              UNLOCK_ERR_RET(
                  CL_INVALID_ARG_SIZE, context,
                  "Host accessible pipe size is not the same size of cl_pipe");
            }
            // Check max buffer size
            if (pipe_ptr->fields.pipe_objs.pipe_max_packets >
                hostpipe_info.max_buffer_depth) {
              UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                             "Host accessible pipe max packets size is smaller "
                             "than cl_pipe requested size");
            }
            found = true;
          }
        }
        assert(found);
      }

      // Here we bind the kernel, but we delay hostpipe binding until kernel
      // enqueue
      pipe_ptr->host_pipe_info->m_binded_kernel = kernel;
      pipe_ptr->host_pipe_info->host_pipe_channel_id =
          arg_info->pipe_channel_id;

      // Always do the binding until kernel enqueue time, because we can only
      // figure out which device at enqueue time
      pipe_ptr->host_pipe_info->binded = false;
    }
    UNLOCK_RETURN(CL_SUCCESS);
  }

  // Now try saving the value.
  // Intel x86 is little-endian, i.e. least significant byte is stored in
  // the lowest numbered address.
  {
    // Determine where to write the value.
    size_t start_idx = 0;
    size_t iface_arg_size = 0;
    l_get_arg_offset_and_size(kernel, arg_index, &start_idx, &iface_arg_size);

    // We would write beyond the end of the array!
    // Kinda late to inform the user... Maybe it should happen at kernel
    // creation time, or at system initialization...
#ifndef REMOVE_VALID_CHECKS
    if ((start_idx + iface_arg_size) > kernel->arg_value_size) {
      UNLOCK_ERR_RET(
          CL_INVALID_KERNEL, context,
          "Argument overflows the space allocated for kernel arguments");
    }
#endif

    // If the board has both SVM and DGM, make sure kernel argument is DGM
    if (arg_info->addr_space == ACL_ARG_ADDR_GLOBAL ||
        arg_info->addr_space == ACL_ARG_ADDR_CONSTANT) {
      cl_bool context_has_device_with_physical_mem = CL_FALSE;
      cl_bool context_has_device_with_svm = CL_FALSE;
      for (unsigned idevice = 0; idevice < context->num_devices; ++idevice) {
        if (acl_svm_device_supports_physical_memory(
                context->device[idevice]->def.physical_device_id)) {
          context_has_device_with_physical_mem = CL_TRUE;
          break;
        }
      }
      for (unsigned idevice = 0; idevice < context->num_devices; ++idevice) {
        if (acl_svm_device_supports_any_svm(
                context->device[idevice]->def.physical_device_id)) {
          context_has_device_with_svm = CL_TRUE;
          break;
        }
      }
      if (context_has_device_with_svm && context_has_device_with_physical_mem &&
          kernel->dev_bin->get_devdef()
                  .autodiscovery_def.num_global_mem_systems > 1 &&
          !l_check_mem_type_support_on_kernel_arg(
              kernel, arg_index, ACL_GLOBAL_MEM_DEVICE_PRIVATE)) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                       "cl_mem object was set on kernel argument that doesn't "
                       "have attribute to access device private memory");
      }
    }

    // If using heterogeneous memory, try to set a default mem_id for the cl_mem
    // we are using This is optimization
    if (arg_info->addr_space == ACL_ARG_ADDR_GLOBAL) {
      // We need to check if this arg is assigned to the correct memory
      if (arg_value &&
          (*(cl_mem *)arg_value)) { // Can pass in a NULL pointer or pointer to
                                    // NULL, to provide NULL arg
        cl_mem mem = *(cl_mem *)arg_value;

        if (((mem->flags & CL_MEM_HETEROGENEOUS_INTELFPGA) ||
             !arg_info->buffer_location.empty()) &&
            mem->allocation_deferred) {

          if (!arg_info->buffer_location.empty()) {
            for (unsigned gmem_idx = 0;
                 gmem_idx < kernel->dev_bin->get_devdef()
                                .autodiscovery_def.num_global_mem_systems;
                 gmem_idx++) {
              // Look for a buffer, if we found this one, use it
              assert(!kernel->dev_bin->get_devdef()
                          .autodiscovery_def.global_mem_defs[gmem_idx]
                          .name.empty());
              if (arg_info->buffer_location ==
                  kernel->dev_bin->get_devdef()
                      .autodiscovery_def.global_mem_defs[gmem_idx]
                      .name) {
                // This is actually just a *HINT* since the allocation hasn't
                // happened yet !
                mem->mem_id = gmem_idx;
                break;
              }
            }
          }
        }
      }
    }

    if (arg_info->addr_space != ACL_ARG_ADDR_LOCAL) {
      if (arg_value == 0) {
        // Example: NULL arg for __global or __constant.
        // Store a zero value for the pointer.
        cl_ulong null_ptr = 0;
        safe_memcpy(&(kernel->arg_value[start_idx]), &null_ptr, iface_arg_size,
                    kernel->arg_value_size - start_idx, iface_arg_size);
        kernel->arg_is_svm[arg_index] = CL_FALSE;
        kernel->arg_is_ptr[arg_index] = CL_FALSE;
      } else if (is_sampler) {
        cl_sampler sampler = *(cl_sampler *)arg_value;
        int sampler_bitfield = 0;

        sampler_bitfield = 0;

        switch (sampler->normalized_coords) {
        case CL_TRUE:
          sampler_bitfield |= CLK_NORMALIZED_COORDS_TRUE;
          break;
        case CL_FALSE:
          sampler_bitfield |= CLK_NORMALIZED_COORDS_FALSE;
          break;
        // Default is CL_TRUE
        default:
          sampler_bitfield |= CLK_NORMALIZED_COORDS_TRUE;
          break;
        }

        switch (sampler->addressing_mode) {
        case CL_ADDRESS_NONE:
          sampler_bitfield |= CLK_ADDRESS_NONE;
          break;
        case CL_ADDRESS_MIRRORED_REPEAT:
          sampler_bitfield |= CLK_ADDRESS_MIRRORED_REPEAT;
          break;
        case CL_ADDRESS_REPEAT:
          sampler_bitfield |= CLK_ADDRESS_REPEAT;
          break;
        case CL_ADDRESS_CLAMP_TO_EDGE:
          sampler_bitfield |= CLK_ADDRESS_CLAMP_TO_EDGE;
          break;
        case CL_ADDRESS_CLAMP:
          sampler_bitfield |= CLK_ADDRESS_CLAMP;
          break;
        // Default is CL_ADDRESS_CLAMP
        default:
          sampler_bitfield |= CLK_ADDRESS_CLAMP;
          break;
        }

        switch (sampler->filter_mode) {
        case CL_FILTER_NEAREST:
          sampler_bitfield |= CLK_FILTER_NEAREST;
          break;
        case CL_FILTER_LINEAR:
          sampler_bitfield |= CLK_FILTER_LINEAR;
          break;
        // Default is CL_FILTER_NEAREST
        default:
          sampler_bitfield |= CLK_FILTER_NEAREST;
          break;
        }

        safe_memcpy(&(kernel->arg_value[start_idx]), &sampler_bitfield,
                    iface_arg_size, kernel->arg_value_size - start_idx,
                    iface_arg_size);
        kernel->arg_is_svm[arg_index] = CL_FALSE;
        kernel->arg_is_ptr[arg_index] = CL_FALSE;
      } else {
        safe_memcpy(&(kernel->arg_value[start_idx]), arg_value, iface_arg_size,
                    kernel->arg_value_size - start_idx, iface_arg_size);
        kernel->arg_is_svm[arg_index] = CL_FALSE;
        kernel->arg_is_ptr[arg_index] = CL_FALSE;
      }
    } else {
      // A LOCAL param is an integer saying how many bytes the local
      // storage should be.
      // The number of bytes is specified by the ***arg_size*** argument.
      safe_memcpy(&(kernel->arg_value[start_idx]), &arg_size, iface_arg_size,
                  kernel->arg_value_size - start_idx, iface_arg_size);
      kernel->arg_is_svm[arg_index] = CL_FALSE;
      kernel->arg_is_ptr[arg_index] = CL_FALSE;
    }

    kernel->arg_defined[arg_index] = 1;
  }

  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArg(cl_kernel kernel,
                                               cl_uint arg_index,
                                               size_t arg_size,
                                               const void *arg_value) {
  return clSetKernelArgIntelFPGA(kernel, arg_index, arg_size, arg_value);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgSVMPointerIntelFPGA(
    cl_kernel kernel, cl_uint arg_index, const void *arg_value) {
  cl_context context;
  acl_lock();

#ifndef REMOVE_VALID_CHECKS
  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  context = kernel->program->context;

  if (arg_index >= kernel->accel_def->iface.args.size()) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_INDEX, context,
                   "Argument index is too large");
  }

  if (arg_value == NULL) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context, "SVM argument is NULL");
  }

  unsigned expected_alignment =
      kernel->accel_def->iface.args[arg_index].alignment;
  expected_alignment =
      expected_alignment ? expected_alignment : ACL_MEM_ALIGN; // For tests
  if ((uintptr_t)arg_value % expected_alignment != 0) {
    if (expected_alignment == ACL_MEM_ALIGN) {
      UNLOCK_ERR_RET(
          CL_INVALID_ARG_VALUE, context,
          "SVM argument is not aligned correctly for type.  Ensure the "
          "kernel argument is targeting the correct buffer location.");
    } else {
      UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                     "SVM argument is not aligned correctly for type.");
    }
  }
#endif

  // Now try saving the value.
  {
    // Determine where to write the value.
    size_t start_idx = 0;
    size_t iface_arg_size = 0;
    l_get_arg_offset_and_size(kernel, arg_index, &start_idx, &iface_arg_size);

    // We would write beyond the end of the array!
    // Kinda late to inform the user... Maybe it should happen at kernel
    // creation time, or at system initialization...
#ifndef REMOVE_VALID_CHECKS
    if ((start_idx + iface_arg_size) > kernel->arg_value_size) {
      UNLOCK_ERR_RET(
          CL_INVALID_KERNEL, context,
          "Argument overflows the space allocated for kernel arguments");
    }
    // If the board has both SVM and DGM, make sure kernel argument is SVM
    cl_bool context_has_device_with_physical_mem = CL_FALSE;
    cl_bool context_has_device_with_svm = CL_FALSE;
    for (unsigned idevice = 0; idevice < context->num_devices; ++idevice) {
      if (acl_svm_device_supports_physical_memory(
              context->device[idevice]->def.physical_device_id)) {
        context_has_device_with_physical_mem = CL_TRUE;
        break;
      }
    }
    for (unsigned idevice = 0; idevice < context->num_devices; ++idevice) {
      if (acl_svm_device_supports_any_svm(
              context->device[idevice]->def.physical_device_id)) {
        context_has_device_with_svm = CL_TRUE;
        break;
      }
    }
    if (context_has_device_with_svm && context_has_device_with_physical_mem &&
        kernel->dev_bin->get_devdef().autodiscovery_def.num_global_mem_systems >
            1 &&
        !l_check_mem_type_support_on_kernel_arg(
            kernel, arg_index, ACL_GLOBAL_MEM_SHARED_VIRTUAL)) {
      UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                     "SVM pointer was set on kernel argument that doesn't have "
                     "attribute to access SVM");
    }
#endif

    // Track usage of buffers.
    // Note: The OpenCL 1.2 spec forbids the kernel object from updating
    // reference counts for the arguments.  See clSetKernelArg spec.
    // We just have to trust the user to not release the memory object too
    // early.
    safe_memcpy(&(kernel->arg_value[start_idx]), &arg_value, iface_arg_size,
                kernel->arg_value_size - start_idx, iface_arg_size);
    kernel->arg_is_svm[arg_index] = CL_TRUE;
    kernel->arg_is_ptr[arg_index] = CL_TRUE;

    kernel->arg_defined[arg_index] = 1;
  }

  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgSVMPointer(
    cl_kernel kernel, cl_uint arg_index, const void *arg_value) {
  return clSetKernelArgSVMPointerIntelFPGA(kernel, arg_index, arg_value);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgMemPointerINTEL(
    cl_kernel kernel, cl_uint arg_index, const void *arg_value) {
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  cl_context context = kernel->program->context;

  if (arg_index >= kernel->accel_def->iface.args.size()) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_INDEX, context,
                   "Argument index is too large");
  }

  // Determine where to write the value.
  size_t start_idx = 0;
  size_t iface_arg_size = 0;
  l_get_arg_offset_and_size(kernel, arg_index, &start_idx, &iface_arg_size);

  // We would write beyond the end of the array!
  // Kinda late to inform the user... Maybe it should happen at kernel
  // creation time, or at system initialization...
#ifndef REMOVE_VALID_CHECKS
  if ((start_idx + iface_arg_size) > kernel->arg_value_size) {
    UNLOCK_ERR_RET(
        CL_INVALID_KERNEL, context,
        "Argument overflows the space allocated for kernel arguments");
  }

  unsigned expected_alignment =
      kernel->accel_def->iface.args[arg_index].alignment;
  expected_alignment =
      expected_alignment ? expected_alignment : ACL_MEM_ALIGN; // For tests
  if ((uintptr_t)arg_value % expected_alignment != 0) {
    if (expected_alignment == ACL_MEM_ALIGN) {
      UNLOCK_ERR_RET(
          CL_INVALID_ARG_VALUE, context,
          "Pointer argument is not aligned correctly for type.  If you are "
          "using unified shared memory compile the kernel with the -usm flag.");
    } else {
      UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                     "Pointer argument is not aligned correctly for type.");
    }
  }

  if (!acl_usm_ptr_belongs_to_context(context, arg_value)) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                   "Pointer argument is not allocated using USM or not "
                   "allocated in correct context.");
  }

  // Ensure the USM allocation (arg_value) is compatible with what the kernel
  // argument is expecting.
  //
  // All information we have about the global memory system comes from the
  // autodiscovery string now.
  //
  // We know which interface the kernel argument is connected to.
  //
  // However, all we know about the USM allocation is which type of allocation
  // it is (host, shared, or device).  Assuming the board_spec.xml has the
  // allocation_type attribute set on the appropriate interfaces we can
  // determine which interface this will correspond to.

  unsigned kernel_arg_mem_id = l_get_kernel_arg_mem_id(kernel, arg_index);
  const auto *kernel_arg_mem =
      &kernel->dev_bin->get_devdef()
           .autodiscovery_def.global_mem_defs[kernel_arg_mem_id];

  acl_usm_allocation_t *usm_alloc =
      acl_get_usm_alloc_from_ptr(context, arg_value);
  if (usm_alloc == nullptr) {
    UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                   "Pointer argument is not allocated using USM or not "
                   "allocated in correct context.");
  }

  // Try to find the memory interface that corresponds to this allocation.
  const acl_system_global_mem_def_t *allocation_mem = nullptr;
  for (unsigned gmem_idx = 0;
       gmem_idx <
       kernel->dev_bin->get_devdef().autodiscovery_def.num_global_mem_systems;
       gmem_idx++) {
    auto allocation_type = kernel->dev_bin->get_devdef()
                               .autodiscovery_def.global_mem_defs[gmem_idx]
                               .allocation_type;
    if ((allocation_type & ACL_GLOBAL_MEM_HOST_ALLOCATION &&
         usm_alloc->type == CL_MEM_TYPE_HOST_INTEL) ||
        (allocation_type & ACL_GLOBAL_MEM_SHARED_ALLOCATION &&
         usm_alloc->type == CL_MEM_TYPE_SHARED_INTEL) ||
        (allocation_type & ACL_GLOBAL_MEM_DEVICE_ALLOCATION &&
         usm_alloc->type == CL_MEM_TYPE_DEVICE_INTEL)) {
      allocation_mem = &kernel->dev_bin->get_devdef()
                            .autodiscovery_def.global_mem_defs[gmem_idx];
      break;
    }
  }

  // We will only be able to perform these checks if the "allocation_type" field
  // was set for the appropriate interfaces in the board_spec.xml and it is an
  // OpenCL compile. Compiler assumes unlabeled pointers are device global mem.
  // This assumption is only true for OpenCL compiles.
  if (allocation_mem && kernel->accel_def->is_sycl_compile == 0) {
    if (kernel_arg_mem->allocation_type ==
        ACL_GLOBAL_MEM_UNDEFINED_ALLOCATION) {
      // The allocation_type field was not indicated in the board_spec.xml for
      // the interface associated with this argument, or it does not contain an
      // expected value.
      acl_context_callback(
          context,
          "Warning: Unable to determine expected USM "
          "allocation type for this kernel argument.  Functional or performance"
          " issues may be encountered.");
    }

    if (usm_alloc->type == CL_MEM_TYPE_DEVICE_INTEL) {
      if (!(kernel_arg_mem->allocation_type &
            ACL_GLOBAL_MEM_DEVICE_ALLOCATION)) {
        if (kernel_arg_mem->allocation_type & ACL_GLOBAL_MEM_HOST_ALLOCATION) {
          // Host not compatible with device memory.
          UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                         "Argument expects host allocation but pointer is to "
                         "USM device memory");
        } else if (kernel_arg_mem->allocation_type &
                   ACL_GLOBAL_MEM_SHARED_ALLOCATION) {
          // Shared not compatible with device memory.
          UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                         "Argument expects shared allocation but pointer is to "
                         "USM device memory");
        }
      } else if (allocation_mem != kernel_arg_mem) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                       "Possibly incompatible interface used for device memory "
                       "allocation.");
      }
    } else if (usm_alloc->type == CL_MEM_TYPE_SHARED_INTEL) {
      if (!(kernel_arg_mem->allocation_type &
            ACL_GLOBAL_MEM_SHARED_ALLOCATION)) {
        if (kernel_arg_mem->allocation_type &
            ACL_GLOBAL_MEM_DEVICE_ALLOCATION) {
          UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                         "Argument expects device allocation but pointer is to "
                         "USM shared memory");
        } else if (kernel_arg_mem->allocation_type &
                   ACL_GLOBAL_MEM_HOST_ALLOCATION) {
          bool compatible = false;
          for (const auto &can_access : kernel_arg_mem->can_access_list) {
            if (can_access == allocation_mem->name) {
              compatible = true;
              acl_context_callback(context,
                                   "Warning: "
                                   "Argument expects host allocation but "
                                   "pointer is to USM shared memory. "
                                   "Performance issues may be encountered.");
              break;
            }
          }
          if (!compatible) {
            UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                           "Argument expects host allocation but pointer is to "
                           "USM shared memory");
          }
        }
      } else if (allocation_mem != kernel_arg_mem) {
        UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                       "Possibly incompatible interface used for shared memory "
                       "allocation.");
      }
    } else if (usm_alloc->type == CL_MEM_TYPE_HOST_INTEL) {
      if (!(kernel_arg_mem->allocation_type & ACL_GLOBAL_MEM_HOST_ALLOCATION)) {
        if (kernel_arg_mem->allocation_type &
            ACL_GLOBAL_MEM_DEVICE_ALLOCATION) {
          UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                         "Argument expects device allocation but pointer is to "
                         "USM host memory");
        } else if (kernel_arg_mem->allocation_type &
                   ACL_GLOBAL_MEM_SHARED_ALLOCATION) {
          bool compatible = false;
          for (const auto &can_access : kernel_arg_mem->can_access_list) {
            if (can_access == allocation_mem->name) {
              compatible = true;
              acl_context_callback(context,
                                   "Warning: "
                                   "Argument expects shared allocation but "
                                   "pointer is to USM host memory. "
                                   "Performance issues may be encountered.");
              break;
            }
          }
          if (!compatible) {
            UNLOCK_ERR_RET(CL_INVALID_ARG_VALUE, context,
                           "Argument expects shared allocation but pointer is "
                           "to USM host memory");
          }
        }
      } else if (allocation_mem != kernel_arg_mem) {
        UNLOCK_ERR_RET(
            CL_INVALID_ARG_VALUE, context,
            "Possibly incompatible interface used for host memory allocation.");
      }
    }
  } else {
    // The allocation_type field was not indicated in the board_spec.xml for
    // this allocation type.
    // Only issue this warning if the BSP supports host and/or shared
    // allocations.
    if ((acl_get_hal()->shared_alloc || acl_get_hal()->host_alloc) &&
        kernel->accel_def->is_sycl_compile == 0) {
      acl_context_callback(
          context,
          "Warning: Unable to determine memory interface"
          " associated with this allocation.  Functional or performance issues "
          "may be encountered.");
    }
  }

#endif

  safe_memcpy(&(kernel->arg_value[start_idx]), &arg_value, iface_arg_size,
              kernel->arg_value_size - start_idx, iface_arg_size);
  kernel->arg_is_svm[arg_index] = CL_FALSE;
  kernel->arg_is_ptr[arg_index] = CL_TRUE;

  kernel->arg_defined[arg_index] = 1;

  // double vector size if size < arg_index
  while (kernel->ptr_arg_vector.size() <= arg_index) {
    kernel->ptr_arg_vector.resize(kernel->ptr_arg_vector.size() * 2);
  }
  kernel->ptr_arg_vector[arg_index] = usm_alloc->range.begin;

  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clSetKernelExecInfoIntelFPGA(cl_kernel kernel, cl_kernel_exec_info param_name,
                             size_t param_value_size, const void *param_value) {
  cl_context context;
  cl_int status = CL_SUCCESS;
  size_t iparam;
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }
  context = kernel->program->context;

  if (param_value == NULL)
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context, "param_value cannot be NULL");

  switch (param_name) {
  case CL_KERNEL_EXEC_INFO_SVM_PTRS: {
    iparam = 0;
    // param_value_size must be a coefficient of sizeof(void*)
    if (param_value_size % sizeof(void *) != 0)
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");

    // The pointers must be valid svm pointers or svm pointers + offset into the
    // SVM region.
    for (iparam = 0; iparam < param_value_size / (sizeof(void *)); iparam++) {
      if (!acl_ptr_is_contained_in_context_svm(
              context, ((void **)param_value)[iparam])) {
        UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                       "param_value contains a pointer that is not contained "
                       "in the SVM region");
      }
    }
    break;
  }
  case CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM:
    if (param_value_size != sizeof(cl_bool))
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");
    // We currently don't support any fine-grain system SVM:
    if (*(cl_bool *)param_value == CL_TRUE)
      UNLOCK_ERR_RET(CL_INVALID_OPERATION, context,
                     "No devices in context associated with "
                     "kernel support fine-grain system SVM allocations");
    break;
  case CL_KERNEL_EXEC_INFO_INDIRECT_HOST_ACCESS_INTEL:
    if (param_value_size != sizeof(cl_bool))
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");

    if (*((cl_bool *)param_value) != CL_TRUE &&
        *((cl_bool *)param_value) != CL_FALSE) {
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value is not valid cl_bool value");
    }
    break;
  case CL_KERNEL_EXEC_INFO_INDIRECT_DEVICE_ACCESS_INTEL:
    if (param_value_size != sizeof(cl_bool))
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");

    if (*((cl_bool *)param_value) != CL_TRUE &&
        *((cl_bool *)param_value) != CL_FALSE) {
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value is not valid cl_bool value");
    }
    break;
  case CL_KERNEL_EXEC_INFO_INDIRECT_SHARED_ACCESS_INTEL:
    if (param_value_size != sizeof(cl_bool))
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");

    if (*((cl_bool *)param_value) != CL_TRUE &&
        *((cl_bool *)param_value) != CL_FALSE) {
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value is not valid cl_bool value");
    }
    break;
  case CL_KERNEL_EXEC_INFO_USM_PTRS_INTEL:
    iparam = 0;
    kernel->ptr_hashtable.clear();
    // param_value_size must be a coefficient of sizeof(void*)
    if (param_value_size % sizeof(void *) != 0)
      UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                     "param_value_size is not valid");

    // The pointers must be valid device pointer
    for (iparam = 0; iparam < param_value_size / (sizeof(void *)); iparam++) {
      acl_usm_allocation_t *usm_alloc =
          acl_get_usm_alloc_from_ptr(context, ((void **)param_value)[iparam]);
      if (!usm_alloc) {
        UNLOCK_ERR_RET(
            CL_INVALID_VALUE, context,
            "param_value contains a pointer that is not part of context");
      }
      kernel->ptr_hashtable.insert(usm_alloc->range.begin);
    }
    break;
  default:
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context, "Invalid param_name");
  }

  UNLOCK_RETURN(status);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clSetKernelExecInfo(cl_kernel kernel, cl_kernel_exec_info param_name,
                    size_t param_value_size, const void *param_value) {
  return clSetKernelExecInfoIntelFPGA(kernel, param_name, param_value_size,
                                      param_value);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelArgInfoIntelFPGA(
    cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  // According to specs, if the kernel is not created using source, we can
  // return CL_KERNEL_ARG_INFO_NOT_AVAILABLE. But we have the info anyways, so
  // we return it.
  acl_result_t result;
  cl_context context;
  cl_program program;

  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  program = kernel->program;
  context = program->context;
  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value,
                                 param_value_size_ret, context);

  if (arg_indx >= kernel->accel_def->iface.args.size())
    UNLOCK_ERR_RET(CL_INVALID_ARG_INDEX, context, "Invalid kernel arg index.");
  // addr_space and type_qualifier is always available via autodiscovery, the
  // other three parameters are optionally loaded in the autodiscovery string,
  // therefore any one of the three parameter being empty infers information not
  // available
  if ((kernel->accel_def->iface.args[arg_indx].name.empty()) &&
      !(param_name == CL_KERNEL_ARG_ADDRESS_QUALIFIER ||
        param_name == CL_KERNEL_ARG_TYPE_QUALIFIER))
    UNLOCK_ERR_RET(CL_KERNEL_ARG_INFO_NOT_AVAILABLE, context,
                   "Kernel arg info not available.");

  // filtering the arguments that are added by the compiler to handle printfs.
  // In such cases, the arguments won't have any type, hence the type_name is
  // empty.
  if (!kernel->accel_def->iface.args[arg_indx].name.empty() &&
      kernel->accel_def->iface.args[arg_indx].type_name.empty())
    UNLOCK_ERR_RET(CL_INVALID_ARG_INDEX, context, "Invalid kernel arg index.");

  RESULT_INIT;

  switch (param_name) {
  case CL_KERNEL_ARG_ADDRESS_QUALIFIER: {
    cl_uint add_qualifier_map[] = {
        CL_KERNEL_ARG_ADDRESS_PRIVATE, CL_KERNEL_ARG_ADDRESS_LOCAL,
        CL_KERNEL_ARG_ADDRESS_GLOBAL, CL_KERNEL_ARG_ADDRESS_CONSTANT};
    // we use different constants for these, so have to map them to OpenCL
    // standard constants. our constants: ACL_ARG_ADDR_NONE, ACL_ARG_ADDR_LOCAL,
    // ACL_ARG_ADDR_GLOBAL, ACL_ARG_ADDR_CONSTANT. NONE (which is the default)
    // is the same as PRIVATE.
    RESULT_ENUM(
        add_qualifier_map[kernel->accel_def->iface.args[arg_indx].addr_space]);
    break;
  }
  case CL_KERNEL_ARG_ACCESS_QUALIFIER: {
    // mapping from our access qualifier enum (0,1,2,3) to OpenCL standard
    // constants.
    cl_uint access_qualifier_map[] = {
        CL_KERNEL_ARG_ACCESS_NONE, CL_KERNEL_ARG_ACCESS_READ_ONLY,
        CL_KERNEL_ARG_ACCESS_WRITE_ONLY, CL_KERNEL_ARG_ACCESS_READ_WRITE};
    RESULT_ENUM(access_qualifier_map[kernel->accel_def->iface.args[arg_indx]
                                         .access_qualifier]);
    break;
  }
  case CL_KERNEL_ARG_TYPE_NAME:
    RESULT_STR(kernel->accel_def->iface.args[arg_indx].type_name.c_str());
    break;

  case CL_KERNEL_ARG_TYPE_QUALIFIER:
    RESULT_ENUM(kernel->accel_def->iface.args[arg_indx].type_qualifier);
    break;

  case CL_KERNEL_ARG_NAME:
    RESULT_STR(kernel->accel_def->iface.args[arg_indx].name.c_str());
    break;

  default:
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context, "Invalid kernel arg info query");
  }

  if (result.size == 0) {
    UNLOCK_RETURN(CL_INVALID_VALUE);
  } // should have already signaled

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
CL_API_ENTRY cl_int CL_API_CALL clGetKernelArgInfo(
    cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  return clGetKernelArgInfoIntelFPGA(kernel, arg_indx, param_name,
                                     param_value_size, param_value,
                                     param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelInfoIntelFPGA(
    cl_kernel kernel, cl_kernel_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  cl_context context;
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  context = kernel->program->context;
  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value,
                                 param_value_size_ret, context);

  RESULT_INIT;

  switch (param_name) {
  case CL_KERNEL_FUNCTION_NAME:
    RESULT_STR(kernel->accel_def->iface.name.c_str());
    break;
  case CL_KERNEL_NUM_ARGS:
    RESULT_UINT(static_cast<unsigned>(kernel->accel_def->iface.args.size()));
    break;
  case CL_KERNEL_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(kernel));
    break;
  case CL_KERNEL_CONTEXT:
    RESULT_PTR(kernel->program->context);
    break;
  case CL_KERNEL_PROGRAM:
    RESULT_PTR(kernel->program);
    break;
  case CL_KERNEL_ATTRIBUTES:
    RESULT_STR("");
    break;

  default:
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context, "Invalid kernel info query");
  }

  if (result.size == 0) {
    UNLOCK_RETURN(CL_INVALID_VALUE);
  } // should have already signaled

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
CL_API_ENTRY cl_int CL_API_CALL clGetKernelInfo(cl_kernel kernel,
                                                cl_kernel_info param_name,
                                                size_t param_value_size,
                                                void *param_value,
                                                size_t *param_value_size_ret) {
  return clGetKernelInfoIntelFPGA(kernel, param_name, param_value_size,
                                  param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelWorkGroupInfoIntelFPGA(
    cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  cl_context context;
  acl_lock();

  if (!acl_kernel_is_valid(kernel)) {
    UNLOCK_RETURN(CL_INVALID_KERNEL);
  }

  context = kernel->program->context;

  // Check or set device arg.
  if (device != 0) {
    // Must be on the list of devices for the program.
    cl_program program = kernel->program;
    cl_uint idev;
    int matched = 0;
    for (idev = 0; idev < program->num_devices && !matched; idev++) {
      if (device == program->device[idev]) {
        matched = 1;
      }
    }
    if (!matched) {
      UNLOCK_ERR_RET(CL_INVALID_DEVICE, context,
                     "Kernel program is not built for the specified device");
    }
  } else {
    // Must only be one device for this kernel.
    if (kernel->program->num_devices != 1) {
      UNLOCK_ERR_RET(
          CL_INVALID_DEVICE, context,
          "Device is not specified, but kernel is not built for a unique "
          "device");
    }
    device = kernel->program->device[0];
  }

  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value,
                                 param_value_size_ret, context);

  RESULT_INIT;

  switch (param_name) {
  case CL_KERNEL_GLOBAL_WORK_SIZE:
    // check if builtin kernel or a custom device (latter not applicable to
    // intel fpgas)
    if (kernel->program->uses_builtin_kernels) {
      RESULT_SIZE_T3(acl_platform.max_work_item_sizes,
                     acl_platform.max_work_item_sizes,
                     acl_platform.max_work_item_sizes);
      break;
    } else {
      UNLOCK_RETURN(CL_INVALID_VALUE);
    }
  case CL_KERNEL_WORK_GROUP_SIZE:
    RESULT_SIZE_T(kernel->accel_def->max_work_group_size);
    break;
  case CL_KERNEL_COMPILE_WORK_GROUP_SIZE: {
    const size_t *p = &(kernel->accel_def->compile_work_group_size[0]);
    RESULT_SIZE_T3(p[0], p[1], p[2]);
    break;
  }
  case CL_KERNEL_LOCAL_MEM_SIZE:
    RESULT_SIZE_T(l_local_mem_size(kernel));
    break;
  case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
    RESULT_SIZE_T(1);
    break; // No preference
  case CL_KERNEL_PRIVATE_MEM_SIZE:
    RESULT_ULONG(0);
    break;
  default:
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context, "Invalid kernel info query");
  }

  if (result.size == 0) {
    UNLOCK_RETURN(CL_INVALID_VALUE);
  } // already signalled.

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
CL_API_ENTRY cl_int CL_API_CALL clGetKernelWorkGroupInfo(
    cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  return clGetKernelWorkGroupInfoIntelFPGA(kernel, device, param_name,
                                           param_value_size, param_value,
                                           param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNativeKernelIntelFPGA(
    cl_command_queue command_queue, void (*user_func)(void *), void *args,
    size_t cb_args, cl_uint num_mem_objects, const cl_mem *mem_list,
    const void **args_mem_loc, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  acl_lock();

  if (!acl_command_queue_is_valid(command_queue)) {
    UNLOCK_RETURN(CL_INVALID_COMMAND_QUEUE);
  }

  // Avoid warnings
  command_queue = command_queue;
  user_func = user_func;
  args = args;
  cb_args = cb_args;
  num_mem_objects = num_mem_objects;
  mem_list = mem_list;
  args_mem_loc = args_mem_loc;
  num_events_in_wait_list = num_events_in_wait_list;
  event_wait_list = event_wait_list;
  event = event;

  // We don't support native kernels.
  UNLOCK_ERR_RET(CL_INVALID_OPERATION, command_queue->context,
                 "Native kernels are not supported.");
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNativeKernel(
    cl_command_queue command_queue, void (*user_func)(void *), void *args,
    size_t cb_args, cl_uint num_mem_objects, const cl_mem *mem_list,
    const void **args_mem_loc, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueNativeKernelIntelFPGA(
      command_queue, user_func, args, cb_args, num_mem_objects, mem_list,
      args_mem_loc, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueTaskIntelFPGA(cl_command_queue command_queue, cl_kernel kernel,
                       cl_uint num_events_in_wait_list,
                       const cl_event *event_wait_list, cl_event *event) {
  size_t task_global_work_size = 1;
  size_t task_local_work_size = 1;
  cl_int ret;
  acl_lock();

  ret = l_enqueue_kernel_with_type(
      command_queue, kernel,
      1, // task work dim
      0, // global work offset
      &task_global_work_size, &task_local_work_size, num_events_in_wait_list,
      event_wait_list, event, CL_COMMAND_TASK);
  UNLOCK_RETURN(ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueTask(cl_command_queue command_queue,
                                              cl_kernel kernel,
                                              cl_uint num_events_in_wait_list,
                                              const cl_event *event_wait_list,
                                              cl_event *event) {
  return clEnqueueTaskIntelFPGA(command_queue, kernel, num_events_in_wait_list,
                                event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernelIntelFPGA(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_int ret;
  acl_lock();

  ret = l_enqueue_kernel_with_type(
      command_queue, kernel, work_dim, global_work_offset, global_work_size,
      local_work_size, num_events_in_wait_list, event_wait_list, event,
      CL_COMMAND_NDRANGE_KERNEL);

  UNLOCK_RETURN(ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernel(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueNDRangeKernelIntelFPGA(
      command_queue, kernel, work_dim, global_work_offset, global_work_size,
      local_work_size, num_events_in_wait_list, event_wait_list, event);
}

// This will force reset all of the running kernels, including the autorun
// kernels for the devices associated with the program
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clResetKernelsIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list) {
  acl_lock();

  if (!acl_context_is_valid(context))
    UNLOCK_RETURN(CL_INVALID_CONTEXT);
  if (num_devices == 0 && device_list != NULL)
    UNLOCK_ERR_RET(CL_INVALID_VALUE, context,
                   "num_devices is 0 while device list is not NULL");
  if (device_list) {
    // The supplied devices must be associated with the context.
    cl_uint idev, ictxdev;
    for (idev = 0; idev < num_devices; idev++) {
      // Optimize the common case: the caller is just using the same
      // device list as passed in to the context creation
      int saw_it = idev < context->num_devices &&
                   context->device[idev] == device_list[idev];
      for (ictxdev = 0; ictxdev < context->num_devices && !saw_it; ictxdev++) {
        saw_it = (context->device[ictxdev] == device_list[idev]);
      }
      if (!saw_it) {
        UNLOCK_ERR_RET(CL_INVALID_DEVICE, context,
                       "A specified device is not associated with the context");
      }
    }
    // Ok, each device is associated with the context.
  } else {
    // Build for all devices in the context.
    // Heinous to overload the variables...
    num_devices = context->num_devices;
    device_list = context->device;
  }

  {
    cl_uint idev;
    for (idev = 0; idev < num_devices; idev++) {
      acl_print_debug_msg("reseting all the kernels of device %u\n",
                          device_list[idev]->def.physical_device_id);
      acl_get_hal()->reset_kernels(device_list[idev]);
    }
  }
  acl_idle_update(context); // nudge the scheduler to take care of the rest.

  UNLOCK_RETURN(CL_SUCCESS);
}

//////////////////////////////
// Internal

static void l_get_arg_offset_and_size(cl_kernel kernel, cl_uint arg_index,
                                      size_t *start_idx_ret, size_t *size_ret) {
  size_t start_idx = 0;
  cl_uint i;
  acl_assert_locked();

  for (i = 0, start_idx = 0; i < arg_index; i++) {
    start_idx += kernel->accel_def->iface.args[i].size;
  }
  *start_idx_ret = start_idx;
  *size_ret = kernel->accel_def->iface.args[arg_index].size;
}

static void l_abort_use_of_wrapper(acl_kernel_invocation_wrapper_t *wrapper) {
  acl_assert_locked();

  // Make this wrapper available to later kernel launches.
  acl_reset_ref_count(wrapper);
}

static cl_int l_enqueue_kernel_with_type(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event, cl_command_type type) {
  // Errors:
  // Coded, Unit tested:
  //    CL_INVALID_PROGRAM_EXECUTABLE
  //    CL_INVALID_COMMAND_QUEUE
  //    CL_INVALID_KERNEL
  //    CL_INVALID_CONTEXT
  //    CL_INVALID_KERNEL_ARGS
  //    CL_INVALID_WORK_DIMENSION
  //    CL_INVALID_GLOBAL_OFFSET
  //    CL_OUT_OF_RESOURCES : global work sizes out of range for device size_t
  // Coded, for conformance testing:
  //    CL_INVALID_CONTEXT
  //    CL_INVALID_WORK_GROUP_SIZE
  //    CL_INVALID_WORK_GROUP_SIZE
  //    CL_INVALID_WORK_GROUP_SIZE
  //    CL_INVALID_WORK_ITEM_SIZE
  //    CL_MEM_OBJECT_ALLOCATION_FAILURE
  //    CL_INVALID_EVENT_WAIT_LIST
  //    CL_OUT_OF_HOST_MEMORY
  // Not coded at all:
  //    CL_OUT_OF_RESOURCES : others

  cl_device_id device = 0;
  size_t effective_work_group_size[3] = {
      0, 0, 0}; // 0,0,0 means we should determine it automatically.
  size_t effective_global_work_size[3] = {0, 0, 0};
  cl_event launch_event = 0;
  acl_kernel_invocation_wrapper_t *wrapper = 0, *serialization_wrapper = 0;
  acl_dev_kernel_invocation_image_t *invocation = 0;
  cl_context context;
  acl_mem_migrate_t memory_migration;
  cl_int status = CL_SUCCESS;
  int serialization_needed = 0;
  acl_assert_locked();

#ifndef REMOVE_VALID_CHECKS
  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  context = command_queue->context;
  if (!acl_kernel_is_valid(kernel)) {
    ERR_RET(CL_INVALID_KERNEL, context, "Invalid kernel");
  }

  if (kernel->program->context != command_queue->context) {
    acl_context_callback(context,
                         "Kernel context does not match command queue context");
    acl_context_callback(kernel->program->context,
                         "Kernel context does not match command queue context");
    return CL_INVALID_CONTEXT;
  }
#endif

  device = command_queue->device;

  /* Refine the accel def to be specific to the device for this queue */
  const acl_accel_def_t *new_accel_def = acl_find_accel_def(
      kernel->program, kernel->accel_def->iface.name, kernel->dev_bin, &status,
      kernel->program->context, device);
  if (!new_accel_def || status != CL_SUCCESS) {
    ERR_RET(CL_INVALID_PROGRAM_EXECUTABLE, context,
            "Kernel program is not built for the device associated with the "
            "command queue");
  }

  // Update to the found def:
  kernel->accel_def = new_accel_def;

#ifndef REMOVE_VALID_CHECKS
  // All kernel args must have been set.
  // If any args are SVM pointers the device must support SVM. Similarily, if
  // any args are non-SVM pointers the device must support physical memory.
  cl_bool device_supports_svm, device_support_physical_memory;
  cl_uint num_args_not_set = 0;
  device_supports_svm = acl_svm_device_supports_any_svm(
      command_queue->device->def.physical_device_id);
  device_support_physical_memory = acl_svm_device_supports_physical_memory(
      command_queue->device->def.physical_device_id);
  for (cl_uint iarg = 0; iarg < kernel->accel_def->iface.args.size(); ++iarg) {
    if (!kernel->arg_defined[iarg])
      num_args_not_set++;

    if (kernel->arg_is_svm[iarg] && !device_supports_svm) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "Kernel argument is SVM pointer but device does not support SVM");
    }
    if (kernel->accel_def->iface.args[iarg].category == ACL_ARG_MEM_OBJ &&
        !kernel->arg_is_svm[iarg] && !device_support_physical_memory &&
        kernel->accel_def->iface.args[iarg].addr_space != ACL_ARG_ADDR_LOCAL) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "Kernel argument is non-SVM pointer but device does not "
              "support physical memories");
    }
  }

  if (num_args_not_set > 0) {
    ERR_RET(CL_INVALID_KERNEL_ARGS, context,
            "Some kernel arguments are not set");
  }

  // The program must have been built for this device.
  cl_program program = kernel->program;
  cl_uint dev_idx =
      program->num_devices; // index into kernel->program->device[] that matches
                            // command_queue->device
  for (cl_uint idev = 0; idev < program->num_devices; idev++) {
    if (program->device[idev] == device) {
      dev_idx = idev;
      break;
    }
  }

  // Kernel's program is not associated with this command queue's device
  if (dev_idx == program->num_devices) {
    ERR_RET(CL_INVALID_PROGRAM_EXECUTABLE, context,
            "Kernel program is not built for the device associated with the "
            "command queue");
  }
  // Kernel's program has not been compiled for this device
  if (!kernel->dev_bin ||
      kernel->dev_bin->get_dev_prog()->build_status != CL_BUILD_SUCCESS) {
    ERR_RET(CL_INVALID_PROGRAM_EXECUTABLE, context,
            "Kernel program is not built for the device associated with the "
            "command queue");
  }

  if (work_dim < 1 || 3 < work_dim) {
    ERR_RET(CL_INVALID_WORK_DIMENSION, context, "Invalid work dimension");
  }

  if (!global_work_size) {
    ERR_RET(CL_INVALID_GLOBAL_WORK_SIZE, context, "global_work_size is NULL");
  }

  if (global_work_offset) {
    for (unsigned i = 0; i < work_dim; ++i) {
      // global_work_offset is only used in OpenCL 1.1 and later.
      // Also the user must not have explicitly disabled support for it
      // using the uses_global_work_offset kernel attribute.
      if (kernel->accel_def->uses_global_work_offset == 0 &&
          global_work_offset[i] != 0) {
        ERR_RET(CL_INVALID_GLOBAL_OFFSET, context,
                "Non-zero global_work_offset is not allowed. Kernel program "
                "was built with a version "
                "of the Intel(R) FPGA SDK for OpenCL(TM) that only supports "
                "OpenCL 1.0 or the "
                "uses_global_work_offset kernel attribute was set to 0.");
      }

      // The kernel id iterator hardware is optimized out when
      // max_global_work_dim is set to 0 so we cannot support a non-zero
      // global_work_offset.
      if (kernel->accel_def->max_global_work_dim == 0 &&
          global_work_offset[i] != 0) {
        ERR_RET(CL_INVALID_GLOBAL_OFFSET, context,
                "Non-zero global_work_offset is not allowed for kernel with "
                "max_global_work_dim attribute set to 0.");
      }

      // According to the OpenCL specifications the sum of global_work_offset
      // and global_work_size cannot overflow a variable of type size_t on the
      // device. On SoC systems the host size_t is 32-bits but the FPGA is still
      // a 64-bit device so we need to explicitly cast these to cl_ulong before
      // checking overflow.
      if ((cl_ulong)global_work_size[i] + (cl_ulong)global_work_offset[i] <
          (cl_ulong)global_work_size[i]) {
        std::stringstream ss;
        ss << "Invalid global work offset at dimension " << i
           << ". The sum of global work size and global work offset: "
           << global_work_size[i] << " + " << global_work_offset[i]
           << " exceeds the maximum value storeable in a variable of type "
              "size_t "
              "on the device without overflowing.";
        ERR_RET(CL_INVALID_GLOBAL_OFFSET, context, ss.str().c_str());
      }
    }
  }
#endif

  // Determine effective global work size.
  for (cl_uint idim = 0; idim < work_dim; idim++) {
    acl_print_debug_msg(" global work size[%d] = %zu\n", idim,
                        global_work_size[idim]);

#ifndef REMOVE_VALID_CHECKS
    // Check that the passed-in global work sizes are compatible with the
    // max_global_work_dim declared for the kernel (if any)
    unsigned int kernel_max_global_work_dim =
        kernel->accel_def->max_global_work_dim;
    if (kernel_max_global_work_dim != 0 && idim >= kernel_max_global_work_dim &&
        global_work_size[idim] != 1) {
      std::stringstream ss;
      ss << "Invalid global work size for kernel with max_global_work_dim "
            "attribute set to "
         << kernel_max_global_work_dim << ". global_work_size[" << idim
         << "] must be 1.";
      ERR_RET(CL_INVALID_GLOBAL_WORK_SIZE, context, ss.str().c_str());
    } else if (kernel_max_global_work_dim == 0 && global_work_size[idim] != 1) {
      ERR_RET(
          CL_INVALID_GLOBAL_WORK_SIZE, context,
          "Invalid global work size for kernel with max_global_work_dim "
          "attribute set to 0. All elements of global_work_size must be 1.");
    }
#endif
    effective_global_work_size[idim] = global_work_size[idim];
  }

  // Determine effective work group size.
  if (local_work_size) {
    cl_uint idim;
    for (idim = 0; idim < work_dim; idim++) {
      unsigned int kernel_max_global_work_dim;
      size_t kernel_compile_work_group_size_idim;
      acl_print_debug_msg(" local work size[%d] = %d\n", idim,
                          local_work_size[idim]);
      // If the work group size was specified in the kernel source, then
      // it must match the passed-in size. If the kernel is workgroup invariant,
      // then this check can be safely skipped.
#ifndef REMOVE_VALID_CHECKS
      kernel_compile_work_group_size_idim =
          kernel->accel_def->compile_work_group_size[idim];
      if (!kernel->accel_def->is_workgroup_invariant &&
          kernel_compile_work_group_size_idim &&
          kernel_compile_work_group_size_idim != local_work_size[idim]) {
        std::stringstream ss;
        ss << "Specified work group size " << local_work_size[idim]
           << " does not match reqd_work_group_size kernel attribute "
           << kernel_compile_work_group_size_idim << ".";
        ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
      }

      // Check that the passed-in local work sizes are compatible with the
      // max_global_work_dim declared for the kernel (if any)
      kernel_max_global_work_dim = kernel->accel_def->max_global_work_dim;
      if (kernel_max_global_work_dim != 0 &&
          idim >= kernel_max_global_work_dim && local_work_size[idim] != 1) {
        std::stringstream ss;
        ss << "Invalid local work size for kernel with max_global_work_dim "
              "attribute set to "
           << kernel_max_global_work_dim << ". local_work_size[" << idim
           << "] must be 1.";
        ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
      } else if (kernel_max_global_work_dim == 0 &&
                 local_work_size[idim] != 1) {
        ERR_RET(
            CL_INVALID_WORK_GROUP_SIZE, context,
            "Invalid local work size for kernel with max_global_work_dim "
            "attribute set to 0. All elements of local_work_size must be 1.");
      }
#endif

      // Save this value.
      effective_work_group_size[idim] = local_work_size[idim];
    }
  } else if (kernel->accel_def->compile_work_group_size[0]) {
    // Take the size as defined in the kernel source.
    cl_uint idim;
    for (idim = 0; idim < work_dim; idim++) {
      effective_work_group_size[idim] =
          kernel->accel_def->compile_work_group_size[idim];
      acl_print_debug_msg(" reqd work size[%d] = %zu\n", idim,
                          effective_work_group_size[idim]);
    }
  } else {
    // If the neither the user nor the kernel cares, then for now just
    // use work group size of 1 in each interesting dimension.
    // This is an easy default which will always pass the remaining
    // tests, and use minimal resources on the hardware.
    // But it might be slow. In which case the developer should be more
    // explicit.
    cl_uint idim;
    for (idim = 0; idim < work_dim; idim++) {
      effective_work_group_size[idim] = 1;
      acl_print_debug_msg(" defaulting work grp size[%d] = %zu\n", idim,
                          effective_work_group_size[idim]);
    }
  }

#ifndef REMOVE_VALID_CHECKS
  // Check that all dimensions of the work group have non-zero size
  // Check that each dim does not exceed device limit
  // Check that total local work item count does not exceed device limit
  size_t max_items;
  size_t max_dim_items[3];
  size_t num_items = 1;
  clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(max_dim_items),
                  &max_dim_items, 0);
  clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_items),
                  &max_items, 0);

  for (cl_uint idim = 0; idim < work_dim; idim++) {
    acl_print_debug_msg(" eff work grp size[%d] = %zu\n", idim,
                        effective_work_group_size[idim]);
    if (effective_work_group_size[idim] == 0) {
      ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context,
              "Work group size is zero in one or more dimensions");
    }
    if (effective_work_group_size[idim] > max_dim_items[idim]) {
      ERR_RET(CL_INVALID_WORK_ITEM_SIZE, context,
              "Work group size in one or more dimensions exceeds device limit "
              "specified in CL_DEVICE_MAX_WORK_ITEM_SIZES");
    }

    num_items *= effective_work_group_size[idim];

    // If the user hasn't specified implicitly that they are using more
    // than one workgroup (ie. local or global thread IDs query, or work-group
    // ID query) then it is possible that the max_work_group_size_arr has the
    // format {max_work_group_size, 0, 0}, in which case we just skip the
    // individual dimension check and just check that the (product of dimensions
    // < max_work_group_size)
    unsigned int kernel_max_work_group_size_arr =
        kernel->accel_def->max_work_group_size_arr[idim];
    if (kernel_max_work_group_size_arr > 0 &&
        effective_work_group_size[idim] > kernel_max_work_group_size_arr) {
      std::stringstream ss;
      ss << "Work group size " << effective_work_group_size[idim]
         << " exceeds kernel-specific limit " << kernel_max_work_group_size_arr
         << ".";
      ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
    }
  }

  if (num_items > max_items) {
    std::stringstream ss;
    ss << "Total work group size " << num_items
       << " exceeds device limit specified in CL_DEVICE_MAX_WORK_GROUP_SIZE "
       << max_items << ".";
    ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
  }

  // Check that number threads no larger than the attribute
  // max_work_group_size. If user didn't specify the kernel attribute,
  // then a default from AlteraCLParam.h was set.  Letting the kernel
  // size go over this limit will overrun barriers and cause a hang.
  unsigned int kernel_max_work_group_size =
      kernel->accel_def->max_work_group_size;
  if (num_items > kernel_max_work_group_size) {
    std::stringstream ss;
    ss << "Total work group size " << num_items
       << " exceeds kernel-specific limit " << kernel_max_work_group_size
       << ".";
    ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
  }

  // Check global_work_size
  for (cl_uint idim = 0; idim < work_dim; idim++) {
    if (effective_global_work_size[idim] > 4294967295U) {
      // This check is technically not OpenCL spec compliant since we should
      // support any value that can be represented in a variable of type size_t.
      // However, our CRA limits global_work_size values to 32-bit unsigned ints
      // so the max value is 2^32-1. After the CRA is changed to take a 64-bit
      // global_work_size input this check isn't needed because any size_t value
      // except 0 is valid.
      ERR_RET(CL_OUT_OF_RESOURCES, context,
              "Global work size exceeds limit of 4294967295 in one or more "
              "dimensions");
    }

    // Can evenly divide the global space into work groups.
    if (effective_global_work_size[idim] == 0) {
      ERR_RET(CL_INVALID_GLOBAL_WORK_SIZE, context,
              "Global work size is zero in one or more dimensions");
    }
    if (effective_work_group_size[idim] &&
        effective_global_work_size[idim] % effective_work_group_size[idim]) {
      std::stringstream ss;
      ss << "Work group size " << effective_work_group_size[idim]
         << " does not divide evenly into global work size "
         << effective_global_work_size[idim] << ".";
      ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context, ss.str().c_str());
    }
  }

  // If the kernel code can't tell that the workgroup is smaller than
  // the entire global space, then just use one workgroup.
  // Do this after all checks, so we have already given the right error
  // messages.
  if (kernel->accel_def->is_workgroup_invariant) {
    for (cl_uint idim = 0; idim < work_dim; idim++) {
      effective_work_group_size[idim] = effective_global_work_size[idim];
    }
  }
#endif // REMOVE_VALID_CHECKS

  // handle vectorization information, if auto-discovery is used.
  if (kernel->accel_def->num_vector_lanes != 0) {
    // ACL compiler makes sure that vectorization is only applied when vector
    // lanes evenly divides work_group_size[0]
    if ((effective_work_group_size[0] % kernel->accel_def->num_vector_lanes) !=
        0) {
      ERR_RET(CL_INVALID_WORK_GROUP_SIZE, context,
              "Kernel vector lane parameter does not evenly divide into work "
              "group size in dimension 0");
    }
    // Scale down local_size0 and global_size0
    effective_work_group_size[0] =
        effective_work_group_size[0] / kernel->accel_def->num_vector_lanes;
    effective_global_work_size[0] =
        effective_global_work_size[0] / kernel->accel_def->num_vector_lanes;
  }

  if (debug_mode > 0) {
    printf(" vectorized x%d\n", kernel->accel_def->num_vector_lanes);
    printf(" eff work grp size[0] = %zu\n", effective_work_group_size[0]);
    printf(" eff global work size[0] = %zu\n", effective_global_work_size[0]);
  }

  ACL_KERNEL_DEBUG_MSG_VERBOSE(1, "KERNEL: In enqueue kernel; device id: %u \n",
                               device->def.physical_device_id);

  // Kernel host pipe binding
  for (const auto &pipe : context->pipe_vec) {
    // If this is a host pipe
    if (pipe->host_pipe_info != nullptr) {
      if (pipe->host_pipe_info->binded) {
        // The host pipe was binded to host already, maybe because a previous
        // enqueue We just need to check if it was binded to the correct device
        // Relaunching
        if (kernel == pipe->host_pipe_info->m_binded_kernel) {
          ;
        }
        // Else case: we don't do anything, because this host pipe was binded to
        // another kernel
      } else {
        // Here the host pipe is not bineded to a host cl_pipe
        if (pipe->host_pipe_info->m_binded_kernel) {
          // clSetKernelArg was called to store the info needed for the binding
          if (kernel == pipe->host_pipe_info->m_binded_kernel) {
            // Only bind and process pipe transactions if the program is loaded
            // onto the device Otherwire, we do them after device programming
            if (device->loaded_bin &&
                device->loaded_bin->get_dev_prog()->program ==
                    kernel->program) {
              if (!context->uses_dynamic_sysdef) {
                // In mode 3
                status = acl_bind_pipe_to_channel(
                    pipe, device, device->def.autodiscovery_def);
              } else {
                // Not in mode 3
                status = acl_bind_pipe_to_channel(
                    pipe, device,
                    kernel->dev_bin->get_devdef().autodiscovery_def);
              }
              if (status != CL_SUCCESS) {
                ERR_RET(status, context, "Host pipe binding error");
              }
              acl_process_pipe_transactions(pipe);
            }
          }
          // Else case: we don't do anything, because this host pipe will need
          // to be binded to another kernel
        } else {
          // clSetKernelArg not called
          // Two cases here:
          // 1. User forgot to call clSetKernelArg, this should be reported as
          // error
          //    Error reporting is done above(so we should't hit this case here)
          // 2. User enqueue a different kernel than the one this host pipe is
          // bounded to
          //    Not an error, do nothing
        }
      }
    }
  }

  cl_uint kernel_arg_bytes = 0;
  // If the global work size is more than one but the compiler didn't infere it,
  // serializing the execution of workitems also warn the user to set up proper
  // attribute for efficient throughput
  if (kernel->accel_def->is_workitem_invariant) {
    cl_uint idim2;
    for (idim2 = 0; idim2 < work_dim; idim2++) {
      if (effective_global_work_size[idim2] > 1) {
        serialization_needed = 1;
        acl_context_callback(
            context, "Warning: Launching a kernel with global_work_size > 1, "
                     "while the kernel was compiled to be work-item invariant. "
                     "This will lead to low performance, due to serialization "
                     "of work-items. "
                     "To avoid this issue, explicitly specify a "
                     "req_work_group_size larger than (1,1,1).");
        break;
      }
    }
  }
  if (serialization_needed) {
    // Set up a wrapper that launches only one workitem, but keep track of the
    // whole ndrange through a backup wrapper.
    serialization_wrapper = acl_get_unused_kernel_invocation_wrapper(context);
    if (serialization_wrapper == 0) {
      // If some kernels have terminated recently, then maybe we can
      // reclaim their invocation images, and retry the allocation.
      // Do this conditionally to avoid queue processing overhead.
      acl_idle_update(context);
      serialization_wrapper = acl_get_unused_kernel_invocation_wrapper(context);
      if (serialization_wrapper == 0) {
        ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate a kernel invocation object");
      }
    }
    acl_retain(serialization_wrapper);
    serialization_wrapper->event = 0;

    invocation = serialization_wrapper->image;
    invocation->padding = 0;
    invocation->work_group_size = 1;

    invocation->activation_id =
        -1; // Will be updated when submitted to the device op queue.
    invocation->accel_id = kernel->accel_def->id;
    invocation->work_dim = 1;

    invocation->global_work_size[0] = invocation->global_work_size[1] =
        invocation->global_work_size[2] = 1;
    invocation->num_groups[0] = invocation->num_groups[1] =
        invocation->num_groups[2] = 1;
    invocation->local_work_size[0] = invocation->local_work_size[1] =
        invocation->local_work_size[2] = 1;
    invocation->global_work_offset[0] = invocation->global_work_offset[1] =
        invocation->global_work_offset[2] = 0;

    // Allocate same amount of space as the kernel args. May be different from
    // the adjusted size below.
    invocation->arg_value = acl_new_arr<char>(kernel->arg_value_size);
    if (kernel->arg_value == nullptr) {
      ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate memory for kernel arguments");
    }

    kernel_arg_bytes = (cl_uint)l_copy_and_adjust_arguments_for_device(
        kernel, device, &(invocation->arg_value[0]), &kernel_arg_bytes,
        &memory_migration);

    assert(kernel_arg_bytes <= kernel->arg_value_size);

    invocation->arg_value_size = kernel_arg_bytes;
  }

  // Set up the launch parameters
  wrapper = acl_get_unused_kernel_invocation_wrapper(context);
  if (wrapper == 0) {
    // If some kernels have terminated recently, then maybe we can
    // reclaim their invocation images, and retry the allocation.
    // Do this conditionally to avoid queue processing overhead.
    acl_idle_update(context);
    wrapper = acl_get_unused_kernel_invocation_wrapper(context);
    if (wrapper == 0) {
      ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a kernel invocation object");
    }
  }

  acl_retain(wrapper);
  wrapper->event = 0;

  invocation = wrapper->image;
  invocation->padding = 0;
  invocation->work_group_size = 1; // Will be updated later.

  invocation->activation_id =
      -1; // Will be updated when submitted to the device op queue.
  invocation->accel_id = kernel->accel_def->id;
  invocation->work_dim = work_dim;

  for (cl_uint idim = 0; idim < work_dim; idim++) {
    // Earlier size checks ensure these casts are safe.
    invocation->global_work_size[idim] =
        (cl_uint)effective_global_work_size[idim];
    invocation->num_groups[idim] = (cl_uint)(effective_global_work_size[idim] /
                                             effective_work_group_size[idim]);
    invocation->local_work_size[idim] =
        (cl_uint)effective_work_group_size[idim];
    invocation->work_group_size *= (cl_uint)effective_work_group_size[idim];
    invocation->global_work_offset[idim] =
        global_work_offset ? (cl_ulong)global_work_offset[idim] : 0L;
  }
  for (cl_uint idim = work_dim; idim < 3; idim++) {
    invocation->global_work_size[idim] = 1;
    invocation->num_groups[idim] = 1;
    invocation->local_work_size[idim] = 1;
    invocation->global_work_offset[idim] = 0L;
  }

  memory_migration.num_mem_objects = 0;

  // Allocate same amount of space as the kernel args. May be different from the
  // adjusted size below.
  invocation->arg_value = acl_new_arr<char>(kernel->arg_value_size);
  if (kernel->arg_value == nullptr) {
    ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
            "Could not allocate memory for kernel arguments");
  }

  status = l_copy_and_adjust_arguments_for_device(
      kernel, device, &(invocation->arg_value[0]), &kernel_arg_bytes,
      &memory_migration);

  if (status != CL_SUCCESS) {
    ERR_RET(status, context, "Argument error");
  }

  assert(kernel_arg_bytes <= kernel->arg_value_size);

  invocation->arg_value_size = kernel_arg_bytes;

  // The HAL kernel launch method will write the invocation
  // parameters into the accelerator's control/status registers.

  // Schedule the kernel invocation.
  status = acl_create_event(command_queue, num_events_in_wait_list,
                            event_wait_list, type, &launch_event);

  if (status != CL_SUCCESS) {
    // Bail out. We'll never do the operation.
    // Already signaled error via callback
    l_abort_use_of_wrapper(wrapper);
    return status;
  }
  acl_retain(kernel);
  launch_event->cmd.info.ndrange_kernel.kernel = kernel;
  launch_event->cmd.info.ndrange_kernel.device = device;
  launch_event->cmd.info.ndrange_kernel.accel_id = kernel->accel_def->id;
  launch_event->cmd.info.ndrange_kernel.dev_bin = kernel->dev_bin;
  if (serialization_needed) {
    launch_event->cmd.info.ndrange_kernel.serialization_wrapper = wrapper;
    launch_event->cmd.info.ndrange_kernel.invocation_wrapper =
        serialization_wrapper;
  } else {
    launch_event->cmd.info.ndrange_kernel.serialization_wrapper = NULL;
    launch_event->cmd.info.ndrange_kernel.invocation_wrapper = wrapper;
  }
  launch_event->cmd.info.ndrange_kernel.memory_migration = memory_migration;
  launch_event->completion_callback = l_complete_kernel_execution;
  if (acl_kernel_is_valid(kernel)) {
    launch_event->ptr_hashtable = kernel->ptr_hashtable;
  }
  for (auto arg_ptr : kernel->ptr_arg_vector) {
    if (arg_ptr != NULL) {
      launch_event->ptr_hashtable.insert(arg_ptr);
    }
  }

  wrapper->event =
      launch_event; // ... because Option3 CSR doesn't have a RUNNING status bit
  if (serialization_wrapper)
    serialization_wrapper->event = launch_event;

  // Queue management overhead becomes significant once there are many command
  // queues.  When we enqueue a task/transfer/etc, should we check all command
  // queues or just the queue this event belongs to?  I've added
  // acl_idle_update_queue to do that but do not use it (yet).
  acl_idle_update(context); // Launch right away if we can.
  // acl_idle_update_queue( command_queue );

  if (event) {
    *event = launch_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(launch_event);
    acl_idle_update(context); // Clean up early.
  }

  return CL_SUCCESS;
}

// Reset contents of the kernel object.
//
// It doesn't belong to any program, etc.
void acl_reset_kernel(cl_kernel kernel) {
  acl_assert_locked();

  if (acl_kernel_is_valid_ptr(kernel)) {
    acl_reset_ref_count(kernel);
    kernel->program = nullptr;
    kernel->accel_def = nullptr;
    kernel->dev_bin = nullptr;
    std::fill(kernel->arg_defined.begin(), kernel->arg_defined.end(), 0);
  }
}

// Just a regular old reference counted object.
int acl_kernel_is_valid(cl_kernel kernel) {
  acl_assert_locked();

#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  if (!acl_kernel_is_valid_ptr(kernel))
    return 0;
  if (!acl_ref_count(kernel))
    return 0;
  if (!acl_program_is_valid(kernel->program))
    return 0;
  return 1;
#endif
}

static int l_init_kernel(cl_kernel kernel, cl_program program,
                         const acl_accel_def_t *accel_def,
                         const acl_device_binary_t *dev_bin,
                         cl_int *errcode_ret) {
  cl_mem tmp_printf_buffer_start = (cl_mem)0;
  void *tmp_printf_ptr_start = 0;
  cl_int err = CL_SUCCESS;
  cl_uint printf_buffer_size;
  unsigned int num_profile_counters;
  acl_assert_locked();

#ifndef REMOVE_VALID_CHECKS
  if (!acl_program_is_valid(program))
    BAIL(CL_INVALID_PROGRAM);
  if (!acl_context_is_valid(program->context))
    BAIL(CL_INVALID_CONTEXT);
#endif
  {
    const char *kernel_debug_var = getenv("ACL_KERNEL_DEBUG");
    if (kernel_debug_var) {
      debug_verbosity = atoi(kernel_debug_var);
      ACL_KERNEL_DEBUG_MSG_VERBOSE(0, "Setting kernel debug level to %u\n",
                                   debug_verbosity);
    }
  }

  acl_reset_kernel(kernel);
  acl_retain(kernel);
  kernel->dispatch = &acl_icd_dispatch;
  kernel->program = program;
  kernel->accel_def = accel_def;
  // Dynamically allocate memory equal to the total size of kernel args and
  // store how many bytes this is.
  size_t total_arguments_size = 0;
  for (acl_kernel_arg_info_t info : accel_def->iface.args) {
    total_arguments_size += info.size;
  }
  kernel->arg_defined.resize(accel_def->iface.args.size(), 0);
  kernel->arg_value_size = total_arguments_size;
  kernel->arg_value = acl_new_arr<char>(total_arguments_size);
  if (kernel->arg_value == nullptr) {
    acl_release(kernel);
    BAIL(CL_OUT_OF_HOST_MEMORY);
  }
  kernel->arg_is_svm.resize(accel_def->iface.args.size(), CL_FALSE);
  kernel->arg_is_ptr.resize(accel_def->iface.args.size(), CL_FALSE);
  kernel->dev_bin = dev_bin;
  auto kernel_arg_num = static_cast<cl_uint>(
      accel_def->iface.args.size() -
      (!kernel->accel_def->printf_format_info.empty() ? 2 : 0));

  // Check if there is hardware profile data in the kernel,
  // if so, allocate the buffer to store the data when kernel completes
  num_profile_counters = accel_def->profiling_words_to_readback;
  if (num_profile_counters != 0) {
    unsigned i;

    kernel->profile_data =
        (uint64_t *)acl_malloc(num_profile_counters * sizeof(uint64_t));
    if (kernel->profile_data == 0) {
      acl_release(kernel);
      BAIL(CL_OUT_OF_HOST_MEMORY);
    }
    for (i = 0; i < num_profile_counters; ++i) {
      kernel->profile_data[i] = 0;
    }
  } else {
    kernel->profile_data = 0;
  }

  // Check if there are printfs in the kernel
  kernel->printf_device_buffer = 0; // Default is none.
  kernel->printf_device_ptr = 0;    // Default is none.
  // Keep track of already processed buffer size
  // It will be reset when the buffer is full and dumped.
  kernel->processed_printf_buffer_size = 0;
  if (!accel_def->printf_format_info.empty()) {
    auto gmem_idx = static_cast<size_t>(
        acl_get_default_memory(kernel->dev_bin->get_devdef()));
    int default_memory_is_svm = kernel->dev_bin->get_devdef()
                                    .autodiscovery_def.global_mem_defs[gmem_idx]
                                    .type == ACL_GLOBAL_MEM_SHARED_VIRTUAL;
    if (default_memory_is_svm) {
      tmp_printf_ptr_start =
          clSVMAlloc(program->context, CL_MEM_READ_WRITE,
                     acl_get_platform()->printf_buffer_size, ACL_MEM_ALIGN);

      tmp_printf_buffer_start = clCreateBuffer(
          program->context, CL_MEM_USE_HOST_PTR,
          acl_get_platform()->printf_buffer_size, tmp_printf_ptr_start, &err);
      if (err != CL_SUCCESS) {
        clSVMFree(program->context, tmp_printf_ptr_start);
        acl_release(kernel);
        BAIL(err);
      }
    } else {
      tmp_printf_ptr_start = NULL;
      tmp_printf_buffer_start =
          clCreateBuffer(program->context, CL_MEM_READ_WRITE,
                         acl_get_platform()->printf_buffer_size, 0, &err);
      if (err != CL_SUCCESS) {
        acl_release(kernel);
        BAIL(err);
      }
    }

    acl_print_debug_msg(
        "setting kernel arg #%d to printf buffer start address %p\n",
        kernel_arg_num, tmp_printf_buffer_start->block_allocation->range.begin);

    // printf buffer start address as kernel argument
    err |= clSetKernelArg(kernel, kernel_arg_num++, sizeof(cl_mem),
                          &tmp_printf_buffer_start);

    acl_print_debug_msg("setting kernel arg #%d to printf buffer size %d\n",
                        kernel_arg_num, acl_get_platform()->printf_buffer_size);

    // set printf buffer size argument
    printf_buffer_size = acl_get_platform()->printf_buffer_size;
    err |= clSetKernelArg(kernel, kernel_arg_num++, sizeof(cl_uint),
                          &printf_buffer_size);

    if (err != CL_SUCCESS) {
      acl_release(kernel);
      clReleaseMemObject(tmp_printf_buffer_start);
      if (tmp_printf_ptr_start)
        clSVMFree(program->context, tmp_printf_ptr_start);
      BAIL(err);
    }

    acl_print_debug_msg("setting device buffer[%d]=%p\n", accel_def->id,
                        tmp_printf_buffer_start->block_allocation->range.begin);

    kernel->printf_device_ptr = tmp_printf_ptr_start;
    kernel->printf_device_buffer = tmp_printf_buffer_start;
  }

  // initialize ptr_arg_vector with size current kernel argument number
  kernel->ptr_arg_vector.resize(accel_def->iface.args.size());

#ifdef REMOVE_VALID_CHECKS
  if (errcode_ret)
    *errcode_ret = CL_SUCCESS;
#else
  if (errcode_ret)
    *errcode_ret = err;
#endif

  clRetainProgram(program);

  acl_print_debug_msg("Created kernel %p\n", kernel);
  acl_track_object(ACL_OBJ_KERNEL, kernel);

  return CL_SUCCESS;
}

// Find the accelerator implementing the given kernel.
// Also return the acl_device_binary_t via dev_bin_ret. Just support one
// device associated with a kernel, for now.
//
// This API will likely change once we support proper program compilation.
//
// Error codes must be consistent with those from clCreateKernel
const acl_accel_def_t *
acl_find_accel_def(cl_program program, const std::string &kernel_name,
                   const acl_device_binary_t *&dev_bin_ret, cl_int *errcode_ret,
                   cl_context context, cl_device_id which_device) {
  const acl_accel_def_t *result = nullptr;
  int num_devices_having_built_program = 0;
  acl_assert_locked();

  if (!acl_program_is_valid(program))
    BAIL_INFO(CL_INVALID_PROGRAM, context, "Invalid program");

  // Search through devices in this program for which the build status is
  // successful.
  for (cl_uint idev = 0; idev < program->num_devices && result == 0; idev++) {
    auto *dev_prog = program->dev_prog[idev];
    if (dev_prog && dev_prog->build_status == CL_BUILD_SUCCESS) {
      if (which_device != 0 && program->device[idev] != which_device) {
        /* This isn't the device I'm looking for */
        continue;
      }

      num_devices_having_built_program++;
      dev_bin_ret = dev_prog->get_or_create_device_binary(kernel_name);
      result = dev_prog->get_kernel_accel_def(kernel_name);
    }
  }

  if (num_devices_having_built_program == 0)
    BAIL_INFO(CL_INVALID_PROGRAM_EXECUTABLE, context, "No programs are built");
  if (result == nullptr)
    BAIL_INFO(CL_INVALID_KERNEL_NAME, context,
              "Specified kernel was not built for any devices");

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  return result;
}

// Determine which accelerator definitions are consistent on all
// devices for the given program.
// Return value is consistent with clCreateKernelsInProgram
//
// Return the acl_device_binary_t and acl_accel_def_t for the consistent
// accelerater definitions on the first device which has a successfully
// built program.
static cl_int l_load_consistently_built_kernels_in_program(
    cl_program program,
    std::vector<std::pair<const acl_device_binary_t *, const acl_accel_def_t *>>
        &accel_ret) {
  cl_int first_built_dev =
      -1; // index of the first device having the program built for it
  cl_context context = program->context;
  acl_assert_locked();

  // The strategy is to copy all accelerator definitions from the first
  // device for which the program has been built.
  // For later devices, we check consistency with the kernels of the first
  // device.  We may have to eliminate some kernels during this stage.

  for (cl_uint idev = 0; idev < program->num_devices; idev++) {
    // If the program isn't built for this device, then skip this device.
    if (!program->dev_prog[idev] ||
        !program->dev_prog[idev]->build_status == CL_BUILD_SUCCESS)
      continue;

    if (first_built_dev == -1) {
      if (context->uses_dynamic_sysdef) {
        const auto names = program->dev_prog[idev]->get_all_kernel_names();
        for (const auto &n : names) {
          accel_ret.push_back(
              {program->dev_prog[idev]->get_or_create_device_binary(n),
               program->dev_prog[idev]->get_kernel_accel_def(n)});
        }
      } else {
        for (const auto &a :
             program->device[idev]->def.autodiscovery_def.accel) {
          accel_ret.push_back(
              {program->dev_prog[idev]->get_or_create_device_binary(
                   a.iface.name),
               &a});
        }
      }

      first_built_dev = static_cast<cl_int>(idev);
      continue;
    }

    // Every kernel in this device must match the same-named kernel
    // that we've seen before. Also each kernel must be built on all
    // devices that have a built dev_prog.

    if (context->uses_dynamic_sysdef) {
      auto end = std::remove_if(
          accel_ret.begin(), accel_ret.end(),
          [&program, &idev](std::pair<const acl_device_binary_t *,
                                      const acl_accel_def_t *> &a) {
            const auto *orig = a.second;
            const auto *other = program->dev_prog[idev]->get_kernel_accel_def(
                a.second->iface.name);

            return !(orig && other && l_kernel_interfaces_match(*orig, *other));
          });

      accel_ret.erase(end, accel_ret.end());
    } else {
      auto end = std::remove_if(
          accel_ret.begin(), accel_ret.end(),
          [&program, &idev](std::pair<const acl_device_binary_t *,
                                      const acl_accel_def_t *> &a) {
            const auto &orig = *(a.second);
            for (const auto &oa :
                 program->device[idev]->def.autodiscovery_def.accel) {
              if (orig.iface.name == oa.iface.name) {
                return !l_kernel_interfaces_match(orig, oa);
              }
            }

            // Did not find matching kernel in the dev_prog
            // for this device so this kernel is not consistently
            // built on all devices.
            return true;
          });

      accel_ret.erase(end, accel_ret.end());
    }
  }

  return CL_SUCCESS;
}

static int l_kernel_interfaces_match(const acl_accel_def_t &a,
                                     const acl_accel_def_t &b) {
  acl_assert_locked();
  if (a.iface.name != b.iface.name)
    return 0; // Name is part of the interface, formally.
  if (a.iface.args.size() != b.iface.args.size())
    return 0;

  for (cl_uint iarg = 0; iarg < a.iface.args.size(); ++iarg) {
    const auto &aarg = a.iface.args[iarg];
    const auto &barg = b.iface.args[iarg];
    if (aarg.addr_space != barg.addr_space)
      return 0;
    if (aarg.category != barg.category)
      return 0;
    if (aarg.size != barg.size)
      return 0;
  }
  return 1;
}

static size_t l_round_up_for_alignment(size_t x) {
  // This assumes ACL_MEM_ALIGN is a power of 2.
  // This code requires no branches.  So maybe it's faster because there
  // should not be any pipeline stalls.
  unsigned fuzz = (ACL_MEM_ALIGN - 1) & x; // Lower address bits
  unsigned offset = (ACL_MEM_ALIGN - fuzz) & (ACL_MEM_ALIGN - 1);
  return x + offset;
}

// Return number of bytes of local memory used by this kernel
// See clGetKernelWorkGroupInfo
static size_t l_local_mem_size(cl_kernel kernel) {
  acl_assert_locked();

  // There are two parts:  The statically determined amount, and the
  // amounts from clSetKernelArg.  These are defined for each local
  // address space used by the kernel, so need to sum them all up.
  size_t total = 0;
  for (const auto &aspace : kernel->accel_def->local_aspaces) {
    // Add in the static demand
    total += l_round_up_for_alignment(aspace.static_demand);
  }

  // The sizes of __local arguments that have been set.
  for (cl_uint iarg = 0, arg_value_idx = 0;
       iarg < kernel->accel_def->iface.args.size(); ++iarg) {
    const acl_kernel_arg_info_t &arg_info = kernel->accel_def->iface.args[iarg];
    if (kernel->arg_defined[iarg]) {
      if (arg_info.addr_space == ACL_ARG_ADDR_LOCAL) {
        // Must use memcpy here just in case the argument pointer is not
        // aligned for a (size_t*).
        // In other words, we can't just cast to (size_t*) and
        // dereference.  We don't want a bus error on an exotic
        // architecture.
        size_t arg_local_mem_size = 0;
        safe_memcpy(&arg_local_mem_size, &(kernel->arg_value[arg_value_idx]),
                    arg_info.size, sizeof(size_t), arg_info.size);
        total += l_round_up_for_alignment(arg_local_mem_size);
      }
    }
    arg_value_idx += arg_info.size;
  }

  return total;
}

int acl_num_non_null_mem_args(cl_kernel kernel) {
  int result = 0;
  acl_assert_locked();

  for (cl_uint iarg = 0, arg_value_idx = 0;
       iarg < kernel->accel_def->iface.args.size(); ++iarg) {
    const acl_kernel_arg_info_t &arg_info = kernel->accel_def->iface.args[iarg];
    if (kernel->arg_defined[iarg] && arg_info.category == ACL_ARG_MEM_OBJ) {
      // Must use memcpy here just in case the argument pointer is not aligned.
      cl_mem mem_obj;
      safe_memcpy(&mem_obj, &(kernel->arg_value[arg_value_idx]), arg_info.size,
                  sizeof(cl_mem), arg_info.size);
      if (mem_obj != 0)
        result++;
    }
    arg_value_idx += arg_info.size;
  }

  return result;
}

// Copy kernel arguments to another buffer.
//
// Adjust for:
//    - Device sizes as necessary.
//       We assume device sizes are never larger than host sizes; that's why we
//       don't bother sizing the buffer.
//
// We assume all arguments have been defined.
//
// Returns number of bytes written to the device-side buffer in num_bytes.
// Returns failure if memory could not be reserved on the device.
static cl_int
l_copy_and_adjust_arguments_for_device(cl_kernel kernel, cl_device_id device,
                                       char *buf, cl_uint *num_bytes,
                                       acl_mem_migrate_t *memory_migration) {
  // indices into the host and device arg value buffer arrays.
  size_t host_idx = 0;
  size_t device_idx = 0;

  // Where **in kernel __local memory space** does the next __local
  // object go?  We assume __local addresses start at 0.
  // Yes, we're using a host pointer to emulate the values going into
  // a device pointer (so make it pointer-to-const...).
  //
  // We have multiple local aspaces - each needs its own bump allocator
  // with a base address of 0

  // Need to determine sizeof device's pointers.
  const cl_uint dev_local_ptr_size = 4; // Always.
  const cl_uint dev_global_ptr_size = device->address_bits >> 3;

  // Bump allocator pointer for each local aspace
  // Maps the aspace ID to the next available local memory address.
  std::unordered_map<unsigned, size_t> next_local;

  acl_assert_locked();

  // Pre-allocate the static portions described by the kernel, once for
  // each aspace
  for (const auto &aspace : kernel->accel_def->local_aspaces) {
    next_local[aspace.aspace_id] +=
        l_round_up_for_alignment(aspace.static_demand);
  }
#ifdef MEM_DEBUG_MSG
  printf("kernel args\n");
#endif

  for (cl_uint iarg = 0; iarg < kernel->accel_def->iface.args.size(); ++iarg) {
#ifdef MEM_DEBUG_MSG
    printf("arg %d ", iarg);
#endif

    const acl_kernel_arg_info_t *arg_info =
        &(kernel->accel_def->iface.args[iarg]);

    // Exclude kernel argument value from device-side buffer by default.
    cl_uint buf_incr = 0;

    if (arg_info->addr_space == ACL_ARG_ADDR_LOCAL) {
#ifdef MEM_DEBUG_MSG
      printf("local");
#endif

      const unsigned int this_aspace =
          kernel->accel_def->iface.args[iarg].aspace_number;

      // This arg is a pointer to __local.
      cl_ulong local_size = 0;
      safe_memcpy(buf + device_idx, &(next_local[this_aspace]),
                  dev_local_ptr_size, dev_local_ptr_size, dev_local_ptr_size);
      // Now reserve space for this object.
      // Yes, this is a bump allocator. :-)
      safe_memcpy(&local_size, &(kernel->arg_value[host_idx]), arg_info->size,
                  sizeof(cl_ulong), arg_info->size);
      // (Need cast to size_t on 32-bit platforms)
      next_local[this_aspace] += l_round_up_for_alignment((size_t)local_size);
      buf_incr = dev_local_ptr_size;
    } else if (arg_info->category == ACL_ARG_MEM_OBJ &&
               !kernel->arg_is_svm[iarg] && !kernel->arg_is_ptr[iarg]) {
      // Must use memcpy here just in case the argument pointer is not aligned.
      cl_mem mem_obj =
          NULL; // It is important to initialize this, since in emulation,
                // arg_info->size = 0 for pipe arguments.

      // On the emulator, the argument size of a pipe (which is a mem object) is
      // 0. Since we copy in 0 bytes, we should read out 0 bytes.
      const size_t copy_sz =
          (arg_info->size == 0) ? arg_info->size : sizeof(cl_mem);
      safe_memcpy(&mem_obj, &(kernel->arg_value[host_idx]), copy_sz,
                  sizeof(cl_mem), copy_sz);

#ifdef MEM_DEBUG_MSG
      printf("mem_obj %zx ", (size_t)mem_obj);
#endif

      if (mem_obj == 0) {
#ifdef MEM_DEBUG_MSG
        printf("null ");
#endif

        /*
        2.0 Spec:
        "If the argument is a buffer object, the arg_value pointer can be NULL
        or point to a NULL value in which case a NULL value will be used as the
        value for the argument declared as a pointer to global or constant
        memory in the kernel."
        */
        const cl_ulong null_ptr = 0;
        safe_memcpy(buf + device_idx, &null_ptr, dev_global_ptr_size,
                    dev_global_ptr_size, dev_global_ptr_size);
        // Shared physical memory:
      } else if (mem_obj->host_mem.device_addr != 0L) {
#ifdef MEM_DEBUG_MSG
        printf("shared physical mem");
#endif
        // Write the address into the invocation image:
        safe_memcpy(buf + device_idx, &(mem_obj->host_mem.device_addr),
                    dev_global_ptr_size, dev_global_ptr_size,
                    dev_global_ptr_size);
        // Regular buffer:
      } else {
#ifdef MEM_DEBUG_MSG
        printf("regular buffer ");
#endif
        const unsigned int needed_mem_id =
            l_get_kernel_arg_mem_id(kernel, iarg);
        const unsigned int needed_physical_id = device->def.physical_device_id;

// Always enqueue a migration, even if the memory is where it should be there
// could be something in the queue ahead of us which will move the memory.
#ifdef MEM_DEBUG_MSG
        printf("needed_physical_id %d needed_mem_id %d ", needed_physical_id,
               needed_mem_id);
#endif

        // first, is there a reserved region?
        if (mem_obj->reserved_allocations_count[needed_physical_id].size() ==
            0) {
          acl_resize_reserved_allocations_for_device(mem_obj, device->def);
        }
        if (mem_obj->reserved_allocations_count[needed_physical_id]
                                               [needed_mem_id] == 0) {
          if (mem_obj->reserved_allocations[needed_physical_id]
                                           [needed_mem_id] == NULL) {
#ifdef MEM_DEBUG_MSG
            printf("needs reservation ");
#endif

            // Need to reserve
            if (!acl_reserve_buffer_block(mem_obj,
                                          &(acl_get_platform()->global_mem),
                                          needed_physical_id, needed_mem_id)) {
              // What happens if this fails and there are other mem objects for
              // this kernel that have reserved allocations? Could result in a
              // memory leak....
              return CL_MEM_OBJECT_ALLOCATION_FAILURE;
            }
          } else {
#ifdef MEM_DEBUG_MSG
            printf("already reserved (reserved allocation %zx block allocation "
                   "%zx) ",
                   (size_t)(mem_obj->reserved_allocations[needed_physical_id]
                                                         [needed_mem_id]),
                   (size_t)(mem_obj->block_allocation));
#endif
          }
        }
        mem_obj
            ->reserved_allocations_count[needed_physical_id][needed_mem_id]++;
#ifdef MEM_DEBUG_MSG
        printf("count %d ",
               mem_obj->reserved_allocations_count[needed_physical_id]
                                                  [needed_mem_id]);
#endif

        // copy the address of the reserved allocation into the invocation
        // image:
        const void *mem_addr =
            mem_obj->reserved_allocations[needed_physical_id][needed_mem_id]
                ->range.begin;
        safe_memcpy(buf + device_idx, &mem_addr, dev_global_ptr_size,
                    dev_global_ptr_size, dev_global_ptr_size);

        if (memory_migration->num_mem_objects == 0) {
          // First time allocation, 128 was chosen because previously, number of
          // kernel arguments were set to an hardcoded limit of 128
          const unsigned int initial_alloc = 128;

          memory_migration->src_mem_list =
              (acl_mem_migrate_wrapper_t *)acl_malloc(
                  initial_alloc * sizeof(acl_mem_migrate_wrapper_t));
          if (!memory_migration->src_mem_list) {
            return CL_OUT_OF_RESOURCES;
          }

          memory_migration->num_alloc = initial_alloc;
        } else if (memory_migration->num_mem_objects >=
                   memory_migration->num_alloc) {
          const unsigned int next_alloc = memory_migration->num_alloc * 2;
          // check for overflow, num_alloc is a 32-bit unsigned integer and
          // unsigned integer overflow is defined behaviour
          if (next_alloc < memory_migration->num_alloc)
            return CL_OUT_OF_RESOURCES;

          acl_mem_migrate_wrapper_t *new_src_mem_list =
              (acl_mem_migrate_wrapper_t *)acl_realloc(
                  memory_migration->src_mem_list,
                  next_alloc * sizeof(acl_mem_migrate_wrapper_t));

          if (!new_src_mem_list) {
            return CL_OUT_OF_RESOURCES;
          }

          memory_migration->src_mem_list = new_src_mem_list;
          memory_migration->num_alloc = next_alloc;
        }

        const unsigned int index = memory_migration->num_mem_objects;
        memory_migration->src_mem_list[index].src_mem = mem_obj;
        memory_migration->src_mem_list[index].destination_physical_device_id =
            needed_physical_id;
        memory_migration->src_mem_list[index].destination_mem_id =
            needed_mem_id;
        ++memory_migration->num_mem_objects;
      }
      buf_incr = dev_global_ptr_size;
    } else {
#ifdef MEM_DEBUG_MSG
      printf("const");
#endif

      // Host and device sizes are the same.
      // E.g. for cl_uint, SVM ptr etc.
      safe_memcpy(buf + device_idx, kernel->arg_value + host_idx,
                  arg_info->size, arg_info->size, arg_info->size);
      buf_incr = arg_info->size;
    }
    device_idx += buf_incr;
    host_idx += arg_info->size;
#ifdef MEM_DEBUG_MSG
    printf("\n");
#endif
  }

  *num_bytes = (cl_uint)device_idx;
  return CL_SUCCESS;
}

int acl_kernel_has_unmapped_subbuffers(acl_mem_migrate_t *mem_migration) {
  acl_assert_locked();

  for (unsigned int ibuf = 0; ibuf < mem_migration->num_mem_objects; ibuf++) {
    if (mem_migration->src_mem_list[ibuf].src_mem->auto_mapped) {
      return 1;
    }
  }
  return 0;
}

int acl_submit_kernel_device_op(cl_event event) {
  // No user-level scheduling blocks this kernel enqueue from running.
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  // This may also imply reprogramming the device and transferring buffers
  // to and from the device.
  // Return a positive number if we committed device ops, zero otherwise.
  int result = 0;
  acl_assert_locked();

  if (!acl_event_is_valid(event)) {
    return result;
  }
  if (!acl_command_queue_is_valid(event->command_queue)) {
    return result;
  }
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;
  int ok = 1;

  cl_device_id device = event->cmd.info.ndrange_kernel.device;
  auto *dev_bin = event->cmd.info.ndrange_kernel.dev_bin;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  bool need_reprogram = true;
  if (device->last_bin) {
    // compare hash of last program that went through device op queue and the
    // program required by kernel
    need_reprogram =
        device->last_bin->get_devdef().autodiscovery_def.binary_rand_hash !=
        dev_bin->get_devdef().autodiscovery_def.binary_rand_hash;
  } else {
    // compare hash of program that is on the device and the program required by
    // kernel
    need_reprogram = device->def.autodiscovery_def.binary_rand_hash !=
                     dev_bin->get_devdef().autodiscovery_def.binary_rand_hash;
  }

  if (event->context->split_kernel) {
    // Always reprogram in split kernel mode. This is a temporary workaround.
    need_reprogram = true;
  }

  if (need_reprogram) {
    // Need to reprogram the device before this kernel runs.
    last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_REPROGRAM, event);
    ok = (last_op != NULL);
  }

  // Arrange for mem migration
  for (unsigned ibuf = 0;
       ok &&
       ibuf < event->cmd.info.ndrange_kernel.memory_migration.num_mem_objects;
       ibuf++) {
    last_op = acl_propose_indexed_device_op(doq, ACL_DEVICE_OP_MEM_MIGRATION,
                                            event, ibuf);
    ok = (last_op != NULL);
    cl_mem mem_to_migrate =
        event->cmd.info.ndrange_kernel.memory_migration.src_mem_list[ibuf]
            .src_mem;
    if (ok && mem_to_migrate->flags & CL_MEM_COPY_HOST_PTR &&
        !acl_is_sub_or_parent_buffer(mem_to_migrate)) {
      // Mem migration to copy the contents of the host pointer to the device
      // buffer has been proposed but not executed at this point. However,
      // writable_copy_on_host needs to be set to false so we don't try to
      // migrate again while the current migration is in the queue. Don't enter
      // if this a sub buffer or a buffer with sub buffers. In that case, the
      // data is kept on the host and copied when it is actually used to
      // prevents us from copying over data that may be modified by an
      // overlapping sub-buffer before this location is used.
      mem_to_migrate->writable_copy_on_host = 0;
    }
  }

  // Schedule the actual kernel launch.
  last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_KERNEL, event);
  ok = ok && (last_op != NULL);

  if (last_op) {
    // The activation_id is the index into the device op queue.
    event->cmd.info.ndrange_kernel.invocation_wrapper->image->activation_id =
        last_op->id;
  }

  if (ok) {
    // We managed to enqueue everything.
    cl_kernel kernel = event->cmd.info.ndrange_kernel.kernel;

    device->last_bin = event->cmd.info.ndrange_kernel.dev_bin;
    event->last_device_op = last_op;

    // Mark this accelerator as being occupied.
    dev_bin->get_dev_prog()->current_event[kernel->accel_def] = event;

    // Commit the operations.
    acl_commit_proposed_device_ops(doq);
    result = 1;
  } else {
    // Did not succeed in enqueueing everything.
    // Back off, and wait until later when we have more space in the
    // device op queue.
    acl_forget_proposed_device_ops(doq);
  }
  return result;
}

// This implements the device op to launch a kernel.
// Everything else has already been set up: the device has the right
// programming file, and all the buffers are in place.
void acl_launch_kernel(void *user_data, acl_device_op_t *op) {
#ifdef MEM_DEBUG_MSG
  printf("acl_launch_kernel\n");
#endif
  cl_event event = op->info.event;
  acl_assert_locked();
  user_data = user_data; // Only used by the test mock.
  if (acl_event_is_valid(event) &&
      acl_command_queue_is_valid(event->command_queue)) {
    const acl_hal_t *hal = acl_get_hal();
    struct acl_kernel_invocation_wrapper_t *invocation_wrapper =
        event->cmd.info.ndrange_kernel.invocation_wrapper;

    // OpenCL 1.2 section 5.4.3 says it's the programmer's responsibility to
    // unmap buffers before any kernel might use them. (Well, if it's mapped for
    // read it can be used read-only by the kernels or copy/read/write buffer
    // commands.) So don't try hard to maintain consistency.

    // Some kernel arguments might be buffers accessible to
    // the host only.
    // Those buffer copies were enqueued on the device op queue
    // as part of the same operation group, and before
    // this kernel operation, so they've already completed.

    // All memory migration events should be complete by now.

    hal->launch_kernel(
        event->cmd.info.ndrange_kernel.device->def.physical_device_id,
        invocation_wrapper);
  } else {
    acl_set_device_op_execution_status(op, -1);
  }
}

// Called when we get a kernel interrupt indicating that profiling data is ready
void acl_profile_update(int activation_id) {
  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);

  if (activation_id >= 0 && activation_id < doq->max_ops) {
    // This address is stable, given a fixed activation_id.
    // So we don't run into race conditions.
    acl_device_op_t *op = doq->op + activation_id;

    (void)acl_process_profiler_scan_chain(op);
  }
}

// Handle a status update from within a HAL interrupt.
// We can't do much: only update a flag in the right spot.
void acl_receive_kernel_update(int activation_id, cl_int status) {
  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);

  // This function can potentially be called by a HAL that does not use the
  // ACL global lock, so we need to use acl_lock() instead of
  // acl_assert_locked(). However, the MMD HAL calls this function from a unix
  // signal handler, which can't lock mutexes, so we don't lock in that case.
  // All functions called from this one therefore have to use
  // acl_assert_locked_or_sig() instead of just acl_assert_locked().
  if (!acl_is_inside_sig()) {
    acl_lock();
  }

  if (activation_id >= 0 && activation_id < doq->max_ops) {

    // This address is stable, given a fixed activation_id.
    // So we don't run into race conditions.
    acl_device_op_t *op = doq->op + activation_id;

    // We should only have been called with status CL_RUNNING (1),
    // CL_COMPLETE (0), or negative for error.
    if (status > CL_RUNNING)
      status = ACL_ERR_INVALID_KERNEL_UPDATE;
    if (op->execution_status >
        status) { // Don't bother if status is not really updated.
      acl_set_device_op_execution_status(op, status);
    } else {
      (void)acl_process_profiler_scan_chain(op);
    }

    // Signal all waiters.
    acl_signal_device_update();
  }

  if (!acl_is_inside_sig()) {
    acl_unlock();
  }
}

// The kernel invocation has completed.
// Clean up.
// This is idempotent.
static void l_complete_kernel_execution(cl_event event) {
  acl_assert_locked();

  // Release the kernel object.  It was retained when we enqueued the
  // Task/NDRange.
  clReleaseKernel(event->cmd.info.ndrange_kernel.kernel);
}

// Check if the memory the kernel argument was compiled to supports
// expected_type. Returns CL_TRUE if memory type is supported. Returns CL_FALSE
// if it is not.
static cl_bool l_check_mem_type_support_on_kernel_arg(
    cl_kernel kernel, cl_uint arg_index,
    acl_system_global_mem_type_t expected_type) {
  cl_bool supported = CL_FALSE;
  const auto &arg_info = kernel->accel_def->iface.args[arg_index];

  // If buffer location attribute has been set on kernel arg
  if (!arg_info.buffer_location.empty()) {
    for (unsigned gmem_idx = 0;
         gmem_idx <
         kernel->dev_bin->get_devdef().autodiscovery_def.num_global_mem_systems;
         gmem_idx++) {
      // look for a buffer, if it matches, make sure it's expected type
      assert(!kernel->dev_bin->get_devdef()
                  .autodiscovery_def.global_mem_defs[gmem_idx]
                  .name.empty());
      if (arg_info.buffer_location ==
          kernel->dev_bin->get_devdef()
              .autodiscovery_def.global_mem_defs[gmem_idx]
              .name) {
        if (kernel->dev_bin->get_devdef()
                .autodiscovery_def.global_mem_defs[gmem_idx]
                .type == expected_type) {
          // Yes, kernel argument was compiled with access to expected memory
          supported = CL_TRUE;
        }
        break;
      }
    }
  } else {
    // buffer_location attribute was not set on kernel argument.
    // The compiler currently connects this to first (default) memeory,
    // so check if default is expected type.
    auto default_gmem_idx = static_cast<size_t>(
        acl_get_default_memory(kernel->dev_bin->get_devdef()));
    if (kernel->dev_bin->get_devdef()
            .autodiscovery_def.global_mem_defs[default_gmem_idx]
            .type == expected_type) {
      supported = CL_TRUE;
    }
  }

  return supported;
}

unsigned int l_get_kernel_arg_mem_id(const cl_kernel kernel,
                                     cl_uint arg_index) {
  /* Get expected buffer location of kernel argument */
  const acl_kernel_arg_info_t *arg_info =
      &(kernel->accel_def->iface.args[arg_index]);
  unsigned needed_mem_id;
  // If the arg_info defines a buffer location
  if (!arg_info->buffer_location.empty()) {
    for (unsigned gmem_idx = 0;
         gmem_idx <
         kernel->dev_bin->get_devdef().autodiscovery_def.num_global_mem_systems;
         gmem_idx++) {
      // look for a buffer, if we found this one, use it
      assert(!kernel->dev_bin->get_devdef()
                  .autodiscovery_def.global_mem_defs[gmem_idx]
                  .name.empty());
      if (arg_info->buffer_location ==
          kernel->dev_bin->get_devdef()
              .autodiscovery_def.global_mem_defs[gmem_idx]
              .name) {
        needed_mem_id = gmem_idx;
        return needed_mem_id;
      }
    }
    // If no buffer location specified, use the default.
    // We use default memory instead of default device global memory
    // because default memory is what the compiler already compiled
    // the argument to access.
    // In clSetKernelArg, we already verified that default memory
    // is of type ACL_GLOBAL_MEM_DEVICE_PRIVATE.
  } else {
    needed_mem_id =
        (unsigned)acl_get_default_memory(kernel->dev_bin->get_devdef());
    return needed_mem_id;
  }
  assert(0 && "Failed to get kernel argument mem id\n");
  return 0;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

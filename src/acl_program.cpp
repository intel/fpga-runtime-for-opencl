// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

// External library headers.
#include <CL/opencl.h>
#include <acl_hash/acl_hash.h>

// Internal headers.
#include <acl.h>
#include <acl_auto.h>
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_hostch.h>
#include <acl_icd_dispatch.h>
#include <acl_mem.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#define MAX_STRING_LENGTH 100000

// Programs
// ========
//
// Lifecycle of cl_program:
//    States are:
//       "new"    - initial state. no program is built, and kernels can't be
//       loaded "built"  - Program has been built, and kernels can be loaded.
//
//
// Faking the build process:
//
//    Note: Because we don't really have a CL compiler, we're
//    faking *everything* about program builds.  We only store
//    a "built" boolean flag, and that's it.
//
//    The only interesting bit comes a the time we enqueue a
//    kernel.
//    At that time we trace:
//       kernel -> program -> context -> devices
//    and
//       command_queue -> device
//    Once we verify a common device is present, we can look
//    up the kernel interface in the list of accelerator
//    definitions inside the device definition.
//
//
// Data model:
//
//    cl_program has:
//       - reference to context
//       - reference to list of devices
//          - For now, we punt on this.
//       - a representation:
//          - Normally source binaries are stored here
//          - But we're implementing an OpenCL embedded profile, so we
//          don't have a compiler.
//          - We're likely going to shortchange things here, i.e. store
//          nothing
//       - build info:
//          - info to support clGetProgramBuildInfo
//          - For now, this is just an overall build_status
//       - kernels
//          - instantiated kernels
//          - Each kernel has:
//             - reference to interface
//             - argument values

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_program);

//////////////////////////////
// Local functions

static void l_init_program(cl_program program, cl_context context);
static void l_free_program(cl_program program);
static acl_device_program_info_t *
l_create_dev_prog(cl_program program, cl_device_id device, size_t binary_len,
                  const unsigned char *binary);
static cl_int l_build_program_for_device(cl_program program,
                                         unsigned int dev_idx,
                                         const char *options);
static void l_compute_hash(cl_program program,
                           acl_device_program_info_t *dev_prog);
static cl_int l_build_from_source(acl_device_program_info_t *dev_prog);
static cl_int l_build_from_source_in_dir(acl_device_program_info_t *dev_prog,
                                         const char *dir);
static void l_try_to_eagerly_program_device(cl_program program);
static void
l_device_memory_definition_copy(acl_device_def_autodiscovery_t *dest_dev,
                                acl_device_def_autodiscovery_t *src_dev);
static cl_int
l_register_hostpipes_to_program(acl_device_program_info_t *dev_prog,
                                unsigned int physical_device_id,
                                cl_context context);
//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainProgramIntelFPGA(cl_program program) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }
  acl_retain(program);
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainProgram(cl_program program) {
  return clRetainProgramIntelFPGA(program);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseProgramIntelFPGA(cl_program program) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }
  acl_release(program);
  if (!acl_ref_count(program)) {
    acl_release(program->context);
    acl_untrack_object(program);
    // Make sure we clean up the elf files
    for (unsigned i = 0; i < program->num_devices; i++) {
      if (program->device[i]->loaded_bin != nullptr)
        program->device[i]->loaded_bin->unload_content();
      if (program->device[i]->last_bin != nullptr)
        program->device[i]->last_bin->unload_content();
    }
    l_free_program(program);
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseProgram(cl_program program) {
  return clReleaseProgramIntelFPGA(program);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clUnloadCompilerIntelFPGA(void) {
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clUnloadCompiler(void) {
  return clUnloadCompilerIntelFPGA();
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithSourceIntelFPGA(
    cl_context context, cl_uint count, const char **strings,
    const size_t *lengths, cl_int *errcode_ret) {
  cl_uint i;
  cl_uint idev;
  int pass;
  cl_program program = 0;
  struct acl_file_handle_t *capture_fp = NULL;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  if (count == 0) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Count parameter is zero");
  }
  if (strings == 0) {
    BAIL_INFO(CL_INVALID_VALUE, context, "No source strings specified");
  }
  for (i = 0; i < count; i++) {
    if (strings[i] == 0) {
      BAIL_INFO(CL_INVALID_VALUE, context, "A string pointers is NULL");
    }
  }

  // Go ahead and allocate it.
  program = acl_alloc_cl_program();
  if (program == 0) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a program object");
  }

  l_init_program(program, context);

  // Just copy devices straight over from the context.
  program->num_devices = context->num_devices;
  for (idev = 0; idev < context->num_devices; idev++) {
    program->device[idev] = context->device[idev];
  }

  // Now process the source text.
  // We have to store it, for later access via clGetProgramInfo.
  // We do two passes: first to determine the text size, the second
  // to actually store and possibly output the text.

  // Capture the source if we've been asked.
  capture_fp = NULL;
  if (acl_get_platform()->next_capture_id != ACL_OPEN) {
    std::stringstream name;
    name << acl_get_platform()->capture_base_path << "."
         << acl_get_platform()->next_capture_id << "\n";
    capture_fp = acl_fopen(name.str().c_str(), "w");
    acl_get_platform()->next_capture_id++;
  }

  // Use two passes:
  //    First pass computes the source size in bytes.
  //    Second pass allocates memory and stores the data.
  for (pass = 0; pass < 2; ++pass) {
    size_t offset = 0;
    if (pass == 1) {
      unsigned char *buffer;
      program->source_len++; // Must also reserve space for the terminating NUL.
      buffer = (unsigned char *)acl_malloc(program->source_len);
      if (buffer == 0) {
        acl_free_cl_program(program);
        if (capture_fp) {
          acl_fclose(capture_fp);
        }
        BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                  "Could not allocate memory to store program source");
      }
      program->source_text = buffer;
    }
    for (i = 0; i < count; i++) {
      size_t len = lengths ? lengths[i] : 0;
      if (len == 0)
        len = strnlen(strings[i], MAX_STRING_LENGTH);
      switch (pass) {
      case 0:
        program->source_len += len;
        break;
      case 1:
        safe_memcpy(program->source_text + offset, strings[i], len, len, len);
        if (capture_fp) {
          acl_fwrite(strings[i], sizeof(char), len, capture_fp);
        }
        offset += len;
        break;
      }
    }
    if (pass == 1)
      program->source_text[offset] = 0; // Terminating NUL
  }

  if (capture_fp) {
    acl_fclose(capture_fp);
  }

  acl_retain(program->context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_PROGRAM, program);

  return program;
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithSource(
    cl_context context, cl_uint count, const char **strings,
    const size_t *lengths, cl_int *errcode_ret) {
  return clCreateProgramWithSourceIntelFPGA(context, count, strings, lengths,
                                            errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBinaryIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const size_t *lengths, const unsigned char **binaries,
    cl_int *binary_status, cl_int *errcode_ret) {
  cl_uint i;
  cl_uint idev;
  cl_program program = 0;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  if (num_devices == 0 || device_list == 0) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid device list");
  }

  for (i = 0; i < num_devices; i++) {
    if (!acl_device_is_valid(device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
    }
    if (!acl_context_uses_device(context, device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context,
                "Device is not associated with the context");
    }
    if (lengths[i] == 0 || binaries[i] == 0) {
      if (binary_status) {
        binary_status[i] = CL_INVALID_VALUE;
      }
      BAIL_INFO(CL_INVALID_VALUE, context,
                lengths[i] == 0 ? "A binary length is zero"
                                : "A binary pointer is NULL");
    }
  }

  // Go ahead and allocate it.
  program = acl_alloc_cl_program();
  if (program == 0) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a program object");
  }

  l_init_program(program, context);

  // Copy devices from arguments and set status.
  program->num_devices = num_devices;
  for (idev = 0; idev < num_devices; idev++) {
    program->device[idev] = device_list[idev];

    // Save the binary in a new acl_device_program_info_t
    program->dev_prog[idev] = l_create_dev_prog(program, device_list[idev],
                                                lengths[idev], binaries[idev]);
    if (program->dev_prog[idev]) {
      if (context->programs_devices || context->uses_dynamic_sysdef) {
        if (!context->split_kernel) {
          // Load and validate the ELF package form.
          auto status =
              program->dev_prog[idev]->device_binary.load_binary_pkg(0, 1);
          if (status != CL_SUCCESS) {
            l_free_program(program);
            if (binary_status) {
              binary_status[idev] = CL_INVALID_BINARY;
            }
            BAIL_INFO(CL_INVALID_BINARY, context, "Invalid binary");
          }
        } else {
          assert(context->uses_dynamic_sysdef);
          // Allow disabling preloading of split binaries if the user requests
          // it. This is required in cases where there are many aocx files in
          // the directory and each cl_program that is created only uses a small
          // subset of them. Note that when preloading is disabled we load the
          // kernels when clCreateKernel is called. Also queries to
          // CL_PROGRAM_NUM_KERNELS and CL_PROGRAM_KERNEL_NAMES in
          // clGetProgramInfo will return inaccurate results unless all kernels
          // in the program are created. Preloading is enabled by default.
          const char *preload =
              acl_getenv("CL_PRELOAD_SPLIT_BINARIES_INTELFPGA");
          if (!preload || std::string(preload) != "0") {
            // In split_kernel mode we have to load all aocx files
            // in the specified directory which cumulatively contain all the
            // kernels.
            auto result = acl_glob(std::string(context->program_library_root) +
                                   std::string("/kernel_*.aocx"));
            for (const auto &filename : result) {
              // Trim ".aocx" file extension.
              auto kernel_name = filename.substr(0, filename.length() - 5);
              auto l = kernel_name.find_last_of("/");
              if (l != std::string::npos) {
                kernel_name = kernel_name.substr(l + 1);
              }

              auto &dev_bin =
                  program->dev_prog[idev]->add_split_binary(kernel_name);
              dev_bin.load_content(filename);
              auto status = dev_bin.load_binary_pkg(0, 1);
              if (status != CL_SUCCESS) {
                l_free_program(program);
                if (binary_status) {
                  binary_status[idev] = CL_INVALID_BINARY;
                }
                BAIL_INFO(CL_INVALID_BINARY, context, "Invalid binary");
              }

              // Need to unload the binary and only load it on an as needed
              // basis due to high memory usage when there are many split
              // binaries.
              dev_bin.unload_content();
            }
          }
        }
      } else {
        assert(!context->split_kernel);
        // Copy memory definition from initial device def to program in
        // CL_CONTEXT_COMPILER_MODE_INTELFPGA mode.
        l_device_memory_definition_copy(
            &(program->dev_prog[idev]
                  ->device_binary.get_devdef()
                  .autodiscovery_def),
            &(program->device[idev]->def.autodiscovery_def));
      }
    } else {
      // Release all the memory we've allocated.
      l_free_program(program);
      if (binary_status) {
        binary_status[idev] = CL_INVALID_VALUE;
      }
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate memory to store program binaries");
    }

    // Wait to set status until after failures may have occurred for this
    // device.
    if (binary_status) {
      binary_status[idev] = CL_SUCCESS;
    }
  }

  acl_retain(program->context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_PROGRAM, program);

  l_try_to_eagerly_program_device(program);

  // Register the program scoped hostpipe to each dev_prog
  for (idev = 0; idev < num_devices; idev++) {
    l_register_hostpipes_to_program(program->dev_prog[idev], idev, context);
  }

  return program;
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBinary(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const size_t *lengths, const unsigned char **binaries,
    cl_int *binary_status, cl_int *errcode_ret) {
  return clCreateProgramWithBinaryIntelFPGA(context, num_devices, device_list,
                                            lengths, binaries, binary_status,
                                            errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinaryAndProgramDeviceIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const size_t *lengths, const unsigned char **binaries,
    cl_int *binary_status, cl_int *errcode_ret) {
  cl_uint i;
  cl_uint idev;
  cl_program program = 0;
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  // split_kernel mode is not supported in this special extension API which is
  // not part of the OpenCL standard.
  assert(context->split_kernel == 0);

  if (num_devices == 0 || device_list == 0) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid device list");
  }

  for (i = 0; i < num_devices; i++) {
    if (!acl_device_is_valid(device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
    }
    if (!acl_context_uses_device(context, device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context,
                "Device is not associated with the context");
    }
    if (lengths[i] == 0 || binaries[i] == 0) {
      if (binary_status) {
        binary_status[i] = CL_INVALID_VALUE;
      }
      BAIL_INFO(CL_INVALID_VALUE, context,
                lengths[i] == 0 ? "A binary length is zero"
                                : "A binary pointer is NULL");
    }
  }

  program = acl_alloc_cl_program();
  if (program == 0) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a program object");
  }

  l_init_program(program, context);

  // Copy devices from arguments and set status.
  program->num_devices = num_devices;
  for (idev = 0; idev < num_devices; idev++) {
    program->device[idev] = device_list[idev];

    // Save the binary in a new acl_device_program_info_t
    program->dev_prog[idev] = l_create_dev_prog(program, device_list[idev],
                                                lengths[idev], binaries[idev]);
    if (program->dev_prog[idev]) {
      if (context->programs_devices || context->uses_dynamic_sysdef) {
        // Load and validate the ELF package form.
        auto status =
            program->dev_prog[idev]->device_binary.load_binary_pkg(0, 0);
        if (status != CL_SUCCESS) {
          l_free_program(program);
          if (binary_status) {
            binary_status[idev] = CL_INVALID_BINARY;
          }
          BAIL_INFO(CL_INVALID_BINARY, context, "Invalid binary");
        }
      } else {
        // Copy memory definition from initial device def to program in
        // CL_CONTEXT_COMPILER_MODE_INTELFPGA mode.
        l_device_memory_definition_copy(
            &(program->dev_prog[idev]
                  ->device_binary.get_devdef()
                  .autodiscovery_def),
            &(program->device[idev]->def.autodiscovery_def));
      }
    } else {
      // Release all the memory we've allocated.
      l_free_program(program);
      if (binary_status) {
        binary_status[idev] = CL_INVALID_VALUE;
      }
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate memory to store program binaries");
    }

    // Wait to set status until after failures may have occurred for this
    // device.
    if (binary_status) {
      binary_status[idev] = CL_SUCCESS;
    }
  }

  acl_retain(program->context);

  acl_track_object(ACL_OBJ_PROGRAM, program);

  for (idev = 0; idev < program->num_devices; idev++) {
    acl_device_program_info_t *dev_prog = program->dev_prog[idev];

    if (dev_prog && dev_prog->build_status == CL_BUILD_SUCCESS) {
      cl_int result;

      // Use a regular event with an internal command type.
      // We want a regular event so we get profiling info, so that we
      // have the infrastructure to measure delays, and the user
      // could in principle optimize this delay away.
      cl_event reprogram_event = 0;

      // Just use the auto_queue. We really only support one device
      // anyway.
      cl_command_queue cq = program->context->auto_queue;

      // Schedule an eager programming of the device.
      acl_print_debug_msg(
          "Device is not yet programmed: plan to eagerly program it\n");

      result = acl_create_event(cq, 0, 0, // Don't wait on other events
                                CL_COMMAND_PROGRAM_DEVICE_INTELFPGA,
                                &reprogram_event);

      if (result == CL_SUCCESS) {
        acl_device_op_t reprogram_op;
        reprogram_event->cmd.info.eager_program = &(dev_prog->device_binary);

        // Try scheduling it.
        reprogram_op.link = ACL_OPEN;
        acl_device_op_reset_device_op(&reprogram_op);

        reprogram_op.status = ACL_PROPOSED;
        reprogram_op.execution_status = ACL_PROPOSED;
        reprogram_op.info.type = ACL_DEVICE_OP_REPROGRAM;
        reprogram_op.info.event = reprogram_event;
        reprogram_op.info.index = 0;
        reprogram_op.conflict_type = acl_device_op_conflict_type(&reprogram_op);

        acl_program_device(NULL, &reprogram_op);

        if (reprogram_op.execution_status != CL_SUCCESS) {
          BAIL_INFO(CL_DEVICE_NOT_AVAILABLE, context,
                    "Reprogram of device failed");
        }

      } else {
        BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context, "Invalid binary");
      }
    } else {
      BAIL_INFO(CL_BUILD_PROGRAM_FAILURE, context,
                "Program is not built correctly");
    }
  }

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  return program;
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBuiltInKernelsIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *kernel_names, cl_int *errcode_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  if (num_devices == 0 || device_list == 0) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid device list");
  }

  if (kernel_names == NULL) {
    BAIL_INFO(CL_INVALID_VALUE, context, "kernel_names is NULL");
  }

  if (num_devices >= ACL_MAX_DEVICE) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "num_dives specified is great thatn ACL_MAX_DEVICES");
  }

  // list of semicolon delimited string of kernel names
  std::set<std::string> kernel_names_split;
  std::string token;
  std::istringstream tokenStream(kernel_names);
  while (std::getline(tokenStream, token, ';')) {
    kernel_names_split.insert(token);
  };

  for (cl_uint i = 0; i < num_devices; i++) {
    if (!acl_device_is_valid(device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
    }

    if (!acl_context_uses_device(context, device_list[i])) {
      BAIL_INFO(CL_INVALID_DEVICE, context,
                "Device is not associated with the context");
    }

    // make sure current device contains all the builtin kernels
    size_t find_count = kernel_names_split.size();
    for (acl_accel_def_t accel : device_list[i]->def.autodiscovery_def.accel) {

      if (kernel_names_split.count(accel.iface.name)) {
        // found one of the kernels the device needs to have
        find_count--;
      }

      if (find_count == 0)
        break;
    }
    if (find_count != 0) {
      BAIL_INFO(CL_INVALID_VALUE, context,
                "kernel_names contains a kernel name that is not "
                "supported by all of the devices in device_list");
    }
  }

  // Go ahead and allocate it.
  cl_program program = acl_alloc_cl_program();
  if (program == 0) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a program object");
  }

  l_init_program(program, context);

  // Copy devices from arguments and set status.
  program->num_devices = num_devices;
  for (cl_uint idev = 0; idev < num_devices; idev++) {
    program->device[idev] = device_list[idev];

    // Save the binary in a new acl_device_program_info_t
    program->dev_prog[idev] =
        l_create_dev_prog(program, device_list[idev], 0, NULL);
    if (program->dev_prog[idev]) {
      if (context->programs_devices || context->uses_dynamic_sysdef) {
        BAIL_INFO(CL_INVALID_VALUE, context, "No builtin kernels available\n");
      } else {

        // i put this here since dla flow makes call to clGetProgramInfo which
        // requires CL_BUILD_SUCCESS
        program->dev_prog[idev]->build_status = CL_BUILD_SUCCESS;

        // Copy memory definition from initial device def to program in
        // CL_CONTEXT_COMPILER_MODE_INTELFPGA mode.
        l_device_memory_definition_copy(
            &(program->dev_prog[idev]
                  ->device_binary.get_devdef()
                  .autodiscovery_def),
            &(program->device[idev]->def.autodiscovery_def));
      }
    } else {
      // Release all the memory we've allocated.
      l_free_program(program);
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate memory to store program binaries");
    }
  }

  program->uses_builtin_kernels = CL_TRUE;

  acl_retain(program->context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_PROGRAM, program);

  return program;
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBuiltInKernels(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *kernel_names, cl_int *errcode_ret) {
  return clCreateProgramWithBuiltInKernelsIntelFPGA(
      context, num_devices, device_list, kernel_names, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCompileProgramIntelFPGA(
    cl_program program, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_headers,
    const cl_program *input_headers, const char **header_include_names,
    acl_program_build_notify_fn_t pfn_notify, void *user_data) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_program_is_valid(program))
    return CL_INVALID_PROGRAM;

  // Suppress compiler warnings.
  num_devices = num_devices;
  device_list = device_list;
  options = options;
  num_input_headers = num_input_headers;
  input_headers = input_headers;
  header_include_names = header_include_names;
  pfn_notify = pfn_notify;
  user_data = user_data;

  ERR_RET(CL_COMPILER_NOT_AVAILABLE, program->context,
          "Device compiler is not available");
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCompileProgram(
    cl_program program, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_headers,
    const cl_program *input_headers, const char **header_include_names,
    acl_program_build_notify_fn_t pfn_notify, void *user_data) {
  return clCompileProgramIntelFPGA(program, num_devices, device_list, options,
                                   num_input_headers, input_headers,
                                   header_include_names, pfn_notify, user_data);
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clLinkProgramIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_programs,
    const cl_program *input_programs, acl_program_build_notify_fn_t pfn_notify,
    void *user_data, cl_int *errcode_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);
  // For the sake of MSVC compiler warnings.
  num_devices = num_devices;
  device_list = device_list;
  options = options;
  num_input_programs = num_input_programs;
  input_programs = input_programs;
  pfn_notify = pfn_notify;
  user_data = user_data;

  BAIL_INFO(CL_LINKER_NOT_AVAILABLE, context, "Device linker is not available");
}

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clLinkProgram(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_programs,
    const cl_program *input_programs, acl_program_build_notify_fn_t pfn_notify,
    void *user_data, cl_int *errcode_ret) {
  return clLinkProgramIntelFPGA(context, num_devices, device_list, options,
                                num_input_programs, input_programs, pfn_notify,
                                user_data, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramInfoIntelFPGA(
    cl_program program, cl_program_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  cl_context context;
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }
  context = program->context;
  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          context);

  RESULT_INIT;

  switch (param_name) {
  case CL_PROGRAM_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(program));
    break;
  case CL_PROGRAM_CONTEXT:
    RESULT_PTR(program->context);
    break;
  case CL_PROGRAM_NUM_DEVICES:
    RESULT_UINT(program->num_devices);
    break;
  case CL_PROGRAM_DEVICES:
    RESULT_BUF(program->device,
               program->num_devices * sizeof(program->device[0]));
    break;

  case CL_PROGRAM_SOURCE:
    // Program source could be empty if the program was created from
    // binary.
    RESULT_BUF(program->source_text, program->source_len);
    break;

  case CL_PROGRAM_BINARY_SIZES: {
    // Special case to copy the binary sizes.
    // We don't store it in the shape that this query wants it.
    // This returns early!
    if (param_value_size_ret) {
      *param_value_size_ret = program->num_devices * sizeof(size_t);
    }
    if (param_value) {
      // They actually want the values

      if (param_value_size < (program->num_devices * sizeof(size_t))) {
        ERR_RET(CL_INVALID_VALUE, context,
                "Parameter return buffer is too small");
      }
      for (unsigned i = 0; i < program->num_devices; i++) {
        // program->dev_prog[] could be NULL if a compile failed.
        auto *dev_prog = program->dev_prog[i];
        ((size_t *)param_value)[i] =
            dev_prog ? dev_prog->device_binary.get_binary_len() : 0;
      }
    }
    return CL_SUCCESS;
  }

  case CL_PROGRAM_BINARIES: {
    // Special case to copy a sequence of buffers.
    // This returns early!

    // Put the size copyback first in case we error out later.
    if (param_value_size_ret) {
      *param_value_size_ret = program->num_devices * sizeof(char *);
    }
    if (param_value) {
      // They actually want the values
      unsigned char **dest = (unsigned char **)param_value;
      if (param_value_size < (program->num_devices * sizeof(char *))) {
        ERR_RET(CL_INVALID_VALUE, context,
                "Parameter return buffer is too small");
      }
      for (unsigned i = 0; i < program->num_devices; ++i) {
        auto *dev_prog = program->dev_prog[i];
        if (dest[i] == nullptr) {
          // Spec says:
          // If an entry value in the array is NULL, the implementation skips
          // copying the program binary for the specific device identified by
          // the array index.
          continue;
        }
        // The dev_prog or binary could be NULL if an attempted compile failed.
        // But the call should still succeed.
        if (dev_prog && dev_prog->device_binary.get_binary_len() > 0) {
          const auto &db = dev_prog->device_binary;
          // The OpenCL spec implies that the user must ensure dest[i] has
          // enough allocated space to store the entire contents of the binary.
          std::copy(db.get_content(), db.get_content() + db.get_binary_len(),
                    dest[i]);
        }
      }
    }
    return CL_SUCCESS;
  }

  case CL_PROGRAM_NUM_KERNELS: {
    size_t kernel_cnt = 0;
    char exists_built_dev_prog =
        0; // a flag to indicate if any built dev_prog exists.
    for (cl_uint idev = 0; idev < program->num_devices; ++idev) {
      acl_device_program_info_t *dev_prog = program->dev_prog[idev];
      if (dev_prog && dev_prog->build_status == CL_BUILD_SUCCESS) {
        // We need to find the number of kernels on one successfully built
        // dev_prog.
        kernel_cnt =
            context->uses_dynamic_sysdef
                ? program->dev_prog[idev]->get_num_kernels()
                : program->device[idev]->def.autodiscovery_def.accel.size();

        exists_built_dev_prog = 1;
        break; // the rest, if any, will be repetitive
      }
    }
    if (!exists_built_dev_prog)
      ERR_RET(CL_INVALID_PROGRAM_EXECUTABLE, context,
              "A successfully built program executable was not found for any "
              "device in the list of devices associated with program");

    RESULT_SIZE_T(kernel_cnt);
    break;
  }
  case CL_PROGRAM_KERNEL_NAMES: {
    // Special case to copy the name of all kernels.
    // This returns early!
    size_t total_ret_len = 0; // we don't know the param_Value_size_ret yet.
    bool exists_built_dev_prog =
        0; // a flag to indicate if any built dev_prog exists.

    // Go through devices in this program for which the build status is
    // successful.

    // First, find the total return size:
    std::set<std::string> names;
    for (cl_uint idev = 0; idev < program->num_devices; idev++) {
      acl_device_program_info_t *dev_prog = program->dev_prog[idev];
      // finding a dev_prog that is built sucessfully.
      if (dev_prog && dev_prog->build_status == CL_BUILD_SUCCESS) {
        if (context->uses_dynamic_sysdef) {
          names = dev_prog->get_all_kernel_names();
        } else {
          for (const auto &a :
               program->device[idev]->def.autodiscovery_def.accel) {
            names.insert(a.iface.name);
          }
        }

        exists_built_dev_prog = true;
        for (const auto &n : names) {
          total_ret_len +=
              n.length() +
              1; //+1 is for the extra semi-colon to separate names.
        }
        break; // The rest, if any, will be repetitive.
      }
    }

    if (!exists_built_dev_prog)
      ERR_RET(CL_INVALID_PROGRAM_EXECUTABLE, context,
              "A successfully built program executable was not "
              "found for any device in the list of devices "
              "associated with program");

    // Based on the OpenCL 1.2 CTS api test, total_ret_len must include the
    // space for the null terminator.
    total_ret_len = total_ret_len > 0 ? total_ret_len : 1;
    if (param_value_size_ret) {
      *param_value_size_ret = total_ret_len;
    }

    if (param_value) {
      if (total_ret_len > param_value_size) {
        ERR_RET(CL_INVALID_VALUE, context,
                "Parameter return buffer is too small");
      }

      std::stringstream ss;
      size_t i = 0;
      for (const auto &n : names) {
        ss << n;
        if (i < names.size() - 1)
          ss << ";";
        ++i;
      }

      auto result_str = ss.str();
      assert(result_str.length() == total_ret_len - 1);
      safe_memcpy(param_value, result_str.c_str(), total_ret_len, total_ret_len,
                  total_ret_len);
    }
  }
    return CL_SUCCESS;

  default:
    ERR_RET(CL_INVALID_VALUE, context, "Invalid program info query");
  }
  // zero size result is valid!

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramInfo(cl_program program,
                                                 cl_program_info param_name,
                                                 size_t param_value_size,
                                                 void *param_value,
                                                 size_t *param_value_size_ret) {
  return clGetProgramInfoIntelFPGA(program, param_name, param_value_size,
                                   param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramBuildInfoIntelFPGA(
    cl_program program, cl_device_id device, cl_program_build_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  cl_uint dev_idx;
  cl_context context;
  acl_device_program_info_t *dev_prog;
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }
  context = program->context;
  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          context);

  RESULT_INIT;

  for (dev_idx = 0; dev_idx < program->num_devices; dev_idx++) {
    if (program->device[dev_idx] == device) {
      break;
    }
  }
  if (dev_idx >= program->num_devices) {
    ERR_RET(CL_INVALID_DEVICE, context,
            "The specified device is not associated with the program");
  }
  dev_prog = program->dev_prog[dev_idx];

  // The 1.0 conformance tests query CL_PROGRAM_BUILD_STATUS even if the build
  // failed.  It will ask the other queries only if the build succeeded.
  // So it's ok to return empty string for options and log if the build failed.
  switch (param_name) {
  case CL_PROGRAM_BUILD_STATUS:
    RESULT_ENUM(dev_prog ? dev_prog->build_status : CL_BUILD_NONE);
    break;
  case CL_PROGRAM_BUILD_OPTIONS: {
    const char *options = dev_prog ? dev_prog->build_options.c_str() : "";
    RESULT_STR(options);
  } break;
  case CL_PROGRAM_BUILD_LOG: {
    const char *log = dev_prog ? dev_prog->build_log.c_str() : "";
    RESULT_STR(log);
  } break;
  case CL_PROGRAM_BINARY_TYPE: {
    // If we don't have dev_prog, we just return CL_PROGRAM_BINARY_TYPE_NONE.
    RESULT_ENUM(dev_prog ? dev_prog->device_binary.get_binary_type()
                         : CL_PROGRAM_BINARY_TYPE_NONE);
  } break;

  default:
    ERR_RET(CL_INVALID_VALUE, context, "Invalid program build info query");
  }

  if (result.size == 0) {
    return CL_INVALID_VALUE;
  } // should already have signalled

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramBuildInfo(
    cl_program program, cl_device_id device, cl_program_build_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  return clGetProgramBuildInfoIntelFPGA(program, device, param_name,
                                        param_value_size, param_value,
                                        param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clBuildProgramIntelFPGA(
    cl_program program, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, acl_program_build_notify_fn_t pfn_notify,
    void *user_data) {
  cl_context context;
  cl_int status = CL_SUCCESS;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }
  context = program->context;
  acl_print_debug_msg("Building program...\n");

  if (program->num_kernels > 0) {
    ERR_RET(CL_INVALID_OPERATION, context,
            "At least one kernel is still attached to the program");
  }
  if (device_list && num_devices == 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid device list: num_devices is zero but device_list "
            "is specified");
  }
  if (0 == device_list && num_devices > 0) {
    ERR_RET(
        CL_INVALID_VALUE, context,
        "Invalid device list: num_devices is non-zero but device_list is NULL");
  }

  if (pfn_notify == 0 && user_data != 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "user_data is set but pfn_notify is not");
  }

  if (device_list) {
    // The supplied devices must be associated with the program.
    cl_uint idev, iprogdev;
    for (idev = 0; idev < num_devices; idev++) {
      // Optimize the common case: the caller is just using the same
      // device list as passed in to the program creation (or context
      // creation).
      int saw_it = idev < program->num_devices &&
                   program->device[idev] == device_list[idev];
      for (iprogdev = 0; iprogdev < program->num_devices && !saw_it;
           iprogdev++) {
        saw_it = (program->device[iprogdev] == device_list[idev]);
      }
      if (!saw_it) {
        ERR_RET(CL_INVALID_DEVICE, context,
                "A specified device is not associated with the program");
      }
    }
    // Ok, each device is associated with the program.
  } else {
    // Build for all devices in the program.
    num_devices = program->num_devices;
    device_list = program->device;
  }

  acl_print_debug_msg("Building program for each device\n");
  // Actually build the program for each device.
  // But we have to use the indexing of program->device[]
  {
    cl_uint idev, iprogdev;
    for (idev = 0; idev < num_devices && status == CL_SUCCESS; idev++) {
      // Optimize the common case: the caller is just using the same
      // device list as passed in to the program creation (or context
      // creation).
      if (idev < program->num_devices &&
          program->device[idev] == device_list[idev]) {
        status = l_build_program_for_device(program, idev, options);
      } else {
        for (iprogdev = 0; iprogdev < program->num_devices; iprogdev++) {
          if (program->device[iprogdev] == device_list[idev]) {
            status = l_build_program_for_device(program, iprogdev, options);
            break;
          }
        }
      }
    }
  }
  acl_print_debug_msg("Building program...status is %d\n", status);

  if (status == CL_SUCCESS)
    l_try_to_eagerly_program_device(program);

  // Call the notification callback.
  if (pfn_notify)
    pfn_notify(program, user_data);
  return status;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clBuildProgram(cl_program program, cl_uint num_devices,
               const cl_device_id *device_list, const char *options,
               acl_program_build_notify_fn_t pfn_notify, void *user_data) {
  return clBuildProgramIntelFPGA(program, num_devices, device_list, options,
                                 pfn_notify, user_data);
}
//////////////////////////////
// Internal

static void l_free_program(cl_program program) {
  unsigned i;
  kernel_list_t *curr_kernel_list;
  kernel_list_t *next_kernel_list;
  acl_assert_locked();

  acl_free(program->source_text);
  for (i = 0; i < sizeof(program->dev_prog) / sizeof(program->dev_prog[0]);
       i++) {
    if (program->dev_prog[i]) {
      acl_delete(program->dev_prog[i]);
    }
  }

  curr_kernel_list = program->kernel_list;
  while (curr_kernel_list != NULL) {
    next_kernel_list = curr_kernel_list->next;
    acl_free_cl_kernel(curr_kernel_list->kernel);
    acl_free(curr_kernel_list);
    curr_kernel_list = next_kernel_list;
  }
  curr_kernel_list = program->kernel_free_list;
  while (curr_kernel_list != NULL) {
    next_kernel_list = curr_kernel_list->next;
    acl_free_cl_kernel(curr_kernel_list->kernel);
    acl_free(curr_kernel_list);
    curr_kernel_list = next_kernel_list;
  }
  acl_free_cl_program(program);
}

// Just a regular old reference counted object.
int acl_program_is_valid(cl_program program) {
  acl_assert_locked();

  if (!acl_is_valid_ptr(program))
    return 0;
  if (!acl_ref_count(program))
    return 0;
  if (!acl_context_is_valid(program->context))
    return 0;
  return 1;
}

static void l_init_program(cl_program program, cl_context context) {
  acl_assert_locked();

  acl_retain(program);
  program->dispatch = &acl_icd_dispatch;
  program->context = context;
  program->kernel_list = NULL;
  program->kernel_free_list = NULL;
  program->num_kernels = 0;
  program->uses_builtin_kernels = CL_FALSE;
}

void acl_program_invalidate_builds(cl_program program) {
  acl_assert_locked();

  // Mark all builds as invalid, and delete them.
  // This is used for testing.
  if (acl_program_is_valid(program)) {
    cl_uint idev;
    for (idev = 0; idev < ACL_MAX_DEVICE; idev++) {
      if (program->dev_prog[idev])
        program->dev_prog[idev]->build_status = CL_BUILD_NONE;
    }
  }
}

// Create dev_prog.
// Use NULL for binary if you don't have one yet.
static acl_device_program_info_t *
l_create_dev_prog(cl_program program, cl_device_id device, size_t binary_len,
                  const unsigned char *binary) {
  acl_assert_locked();

  auto *result = acl_new<acl_device_program_info_t>();
  if (result == nullptr) {
    return nullptr;
  }
  result->program = program;
  result->device = device;
  result->build_status = CL_BUILD_NONE;

  if (binary) {
    result->device_binary.load_content(binary, binary_len);

    // The OpenCL 1.2 spec doesn't say whether the user has to call
    // clBuildProgram or clCompileProgram after loading from binary. The 1.2
    // conformance tests never build or compile after loading from binary. And
    // the goHDR example code does not do clBuildProgram.
    //
    // We don't do anything on "build" action.
    // So enhance usability and user expectations by automatically setting
    // build status to success.
    result->build_status = CL_BUILD_SUCCESS;
  }

  return result;
}

// Loop through auto-discovery string and store program scope hostpipe
// information in the device program info
static cl_int
l_register_hostpipes_to_program(acl_device_program_info_t *dev_prog,
                                unsigned int physical_device_id,
                                cl_context context) {

  host_pipe_t host_pipe_info;

  for (const auto &hostpipe : dev_prog->device_binary.get_devdef()
                                  .autodiscovery_def.hostpipe_mappings) {
    // Skip if the hostpipe doesn't have a logical name.
    // It's not the program scoped hostpipe.
    if (hostpipe.logical_name == "-") {
      continue;
    }
    // Skip if the hostpipe is already registered in the program
    auto search = dev_prog->program_hostpipe_map.find(hostpipe.logical_name);
    if (search != dev_prog->program_hostpipe_map.end()) {
      continue;
    }
    host_pipe_t host_pipe_info;
    host_pipe_info.m_physical_device_id = physical_device_id;
    if (hostpipe.is_read && hostpipe.is_write) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "Hostpipes don't allow both read and write operations from the "
              "host.");
    }
    if (!hostpipe.is_read && !hostpipe.is_write) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "The hostpipe direction is not set.");
    }

    if (hostpipe.implement_in_csr) {
      // CSR hostpipe read and write from the given CSR address directly
      host_pipe_info.implement_in_csr = true;
      host_pipe_info.csr_address = hostpipe.csr_address;
      // CSR pipe doesn't use m_channel_handle but we want to have it
      // initialized.
      host_pipe_info.m_channel_handle = -1;
    } else {
      host_pipe_info.implement_in_csr = false;
      host_pipe_info.m_channel_handle = acl_get_hal()->hostchannel_create(
          physical_device_id, (char *)hostpipe.physical_name.c_str(),
          hostpipe.pipe_depth, hostpipe.pipe_width,
          hostpipe.is_read); // If it's a read pipe, pass 1 to the
                             // hostchannel_create, which is HOST_TO_DEVICE
      if (host_pipe_info.m_channel_handle <= 0) {
        return CL_INVALID_VALUE;
      }
    }
    host_pipe_info.protocol = hostpipe.protocol;
    host_pipe_info.is_stall_free = hostpipe.is_stall_free;
    acl_mutex_init(&(host_pipe_info.m_lock), NULL);
    // The following property is not used by the program scoped hostpipe but we
    // don't want to leave it uninitialized
    host_pipe_info.binded = false;
    host_pipe_info.m_binded_kernel = NULL;
    host_pipe_info.size_buffered = 0;

    dev_prog->program_hostpipe_map[hostpipe.logical_name] = host_pipe_info;
  }

  // Start from 2024.1, Runtime receives sideband signals information
  for (const auto &sideband_signal_mapping :
       dev_prog->device_binary.get_devdef()
           .autodiscovery_def.sideband_signal_mappings) {

    // Skip if the sideband_signal doesn't have a logical name.
    // It's not the program scoped hostpipe.
    if (sideband_signal_mapping.logical_name == "-") {
      continue;
    }

    // The hostpipe hostpipe logical name must be found in the program
    auto search = dev_prog->program_hostpipe_map.find(
        sideband_signal_mapping.logical_name);
    if (search == dev_prog->program_hostpipe_map.end()) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Sideband signal is binded to non-exist hostpipe");
    }

    auto &host_pipe_info =
        dev_prog->program_hostpipe_map.at(sideband_signal_mapping.logical_name);
    if (sideband_signal_mapping.port_identifier != AvalonData) {
      host_pipe_info.num_side_band_signals++;
    }

    // Store the sideband info into the hostpipe info.
    sideband_signal_t sideband_signal_info;
    sideband_signal_info.port_identifier =
        sideband_signal_mapping.port_identifier;
    sideband_signal_info.port_offset = sideband_signal_mapping.port_offset;
    sideband_signal_info.side_band_size = sideband_signal_mapping.sideband_size;

    host_pipe_info.side_band_signals_vector.emplace_back(sideband_signal_info);
  }

  return CL_SUCCESS;
}

static cl_int l_build_program_for_device(cl_program program,
                                         unsigned int dev_idx,
                                         const char *options) {

  acl_device_program_info_t *dev_prog = 0;
  cl_context context;
  int build_status; // CL_BUILD_IN_PROGRESS, CL_BUILD_ERROR, or
                    // CL_BUILD_SUCCESS.
  cl_int status = CL_SUCCESS;
  acl_assert_locked();

  context = program->context;

  if (!program->source_text) {
    // Program was created from binary.
    dev_prog = program->dev_prog[dev_idx];
    // User might have provided a bad binary (e.g. random bytes).
    // Need to check that once we can.
    // So we can only do a NULL check, but
    // clCreateProgramWithBinary would already have failed due to NULL binary.
    if (!dev_prog)
      ERR_RET(CL_INVALID_BINARY, context, "No binary loaded for device");

    // Prep for re-build.
    dev_prog->build_status = CL_BUILD_ERROR;
  } else {
    // Program was created from source.
    assert(context->split_kernel == 0);

    // clBuildProgram should succeed for all but the offline case.
    // Easiest to check the enum instead of creating a new context
    // attribute.
    if (context->compiler_mode == CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA) {
      ERR_RET(CL_COMPILER_NOT_AVAILABLE, context,
              "Device code compiler is not available");
    }
    acl_delete(program->dev_prog[dev_idx]);
    program->dev_prog[dev_idx] = nullptr;

    dev_prog = l_create_dev_prog(program, program->device[dev_idx], 0, 0);
    if (!dev_prog) {
      ERR_RET(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate memory to store the program binary");
    }
    // num_global_mem_systems of device binary autodiscovery def will remain 0
    // only when CL_CONTEXT_COMPILER_MODE_INTELFPGA is
    // ACL_COMPILER_MODE_PRELOADED_BINARY_ONLY,
    // in these situations we should only use num_global_memory_system of device
    // autodiscovery def which would be initialized with acl_kernel_if_init by
    // reading the autodiscovery string of the bitstream currently programmed on
    // the board.
    dev_prog->device_binary.get_devdef()
        .autodiscovery_def.num_global_mem_systems = 0;
    program->dev_prog[dev_idx] = dev_prog;
  }
  // At this point dev_prog is a valid pointer, and an alias to
  // program->dev_prog[dev_idx]
  build_status = CL_BUILD_IN_PROGRESS;

  dev_prog->build_options = options ? options : "";
  auto last_pos = dev_prog->build_options.find_last_not_of(" ");
  if (last_pos != std::string::npos) {
    // Trim trailing spaces.
    dev_prog->build_options = dev_prog->build_options.substr(0, last_pos + 1);
  }

  if (program->source_text) {
    // Compiling from source.  Need a hash.
    l_compute_hash(program, dev_prog);
  } else {
    // Just keep what we loaded before.
    // Even if options changed, we're not changing the binary.
    // So the hash stays the same!
  }
  if (context->uses_program_library && program->source_text) {
    // Compile from source.
    acl_print_debug_msg("Compile from source\n");

    // In failure case, this will signal error.
    status = l_build_from_source(dev_prog);
    if (status != CL_SUCCESS) {
      build_status = CL_BUILD_ERROR;
    }
    if (dev_prog->build_log == "") {
      dev_prog->build_log = "Build failed, without log";
    }
  } else {
    dev_prog->build_log = "Trivial build";
  }

  // If no errors, then all is ok!
  if (build_status == CL_BUILD_IN_PROGRESS)
    build_status = CL_BUILD_SUCCESS;

  dev_prog->build_status = build_status;
  return status;
}

static void l_compute_hash(cl_program program,
                           acl_device_program_info_t *dev_prog) {
  acl_hash_context_t ctx;
  acl_assert_locked();

  acl_hash_init_sha1(&ctx);

  // Depends on board name.
  acl_hash_add(&ctx, dev_prog->device->def.autodiscovery_def.name.c_str(),
               dev_prog->device->def.autodiscovery_def.name.length());
  acl_hash_add(&ctx, (const void *)'\0', 1);

  // And compile options.
  acl_hash_add(&ctx, dev_prog->build_options.c_str(),
               dev_prog->build_options.length());
  acl_hash_add(&ctx, (const void *)'\0', 1);

  // And the source
  acl_hash_add(&ctx, program->source_text, program->source_len);

  // Finalize and store the result.
  char hash[ACL_HASH_SHA1_DIGEST_BUFSIZE];
  acl_hash_hexdigest(&ctx, hash, sizeof(hash));
  dev_prog->hash = std::string(hash);
}

cl_kernel acl_program_alloc_kernel(cl_program program) {
  cl_kernel result = 0;
  acl_assert_locked();

  if (acl_program_is_valid(program)) {
    kernel_list_t *tmp_kernel_list;
    if (program->kernel_free_list != NULL) {
      tmp_kernel_list = program->kernel_free_list;
      program->kernel_free_list = program->kernel_free_list->next;
    } else {
      cl_kernel kernel = acl_alloc_cl_kernel();
      if (!kernel) {
        return 0;
      }
      tmp_kernel_list = (kernel_list_t *)acl_malloc(sizeof(kernel_list_t));
      if (!tmp_kernel_list) {
        acl_free_cl_kernel(kernel);
        return 0;
      }
      tmp_kernel_list->kernel = kernel;
    }
    tmp_kernel_list->next = program->kernel_list;
    program->kernel_list = tmp_kernel_list;
    ++program->num_kernels;
    result = tmp_kernel_list->kernel;
  }

  return result;
}

void acl_program_forget_kernel(cl_program program, cl_kernel kernel) {
  acl_assert_locked();

  if (acl_program_is_valid(program)) {
    kernel_list_t *search_kernel_list = program->kernel_list;
    kernel_list_t *prev_kernel_list = NULL;
    while (search_kernel_list != NULL) {
      if (search_kernel_list->kernel == kernel) {
        if (prev_kernel_list == NULL) {
          program->kernel_list = search_kernel_list->next;
        } else {
          prev_kernel_list->next = search_kernel_list->next;
        }
        search_kernel_list->next = program->kernel_free_list;
        program->kernel_free_list = search_kernel_list;
        --program->num_kernels;
        break;
      }
      prev_kernel_list = search_kernel_list;
      search_kernel_list = search_kernel_list->next;
    }
  }
}

// This is the behaviour for the ACL_DEVICE_OP_REPROGRAM device operation.
// The device op queue guarantees that the device is not servicing any
// requests right now.
ACL_EXPORT
void acl_program_device(void *user_data, acl_device_op_t *op) {
  cl_event event = op->info.event;
  cl_context context = event->command_queue->context;
  int status = 0;

  // The underlying command is either:
  //    an eager programming command:  CL_COMMAND_PROGRAM_DEVICE_INTELFPGA,
  // or a kernel enqueue: CL_COMMAND_NDRANGE_KERNEL, or CL_COMMAND_TASK
  auto *dev_bin = event->cmd.type == CL_COMMAND_PROGRAM_DEVICE_INTELFPGA
                      ? event->cmd.info.eager_program
                      : event->cmd.info.ndrange_kernel.dev_bin;
  auto *dev_prog = dev_bin->get_dev_prog();

  acl_assert_locked();

  user_data = user_data; // Only used by the test mock.

  acl_set_device_op_execution_status(op, CL_RUNNING);

  acl_print_debug_msg(
      "Reprogram %s with program containing %d accelerators: %s ...\n",
      dev_prog->device->def.autodiscovery_def.name.c_str(),
      dev_prog->program->num_kernels,
      (dev_prog->program->num_kernels ? dev_prog->program->kernel_list->kernel
                                            ->accel_def->iface.name.c_str()
                                      : "<none>"));

  // the program flow will first attempt memory preserved programming, then
  // falls back to memory unpreserved programming if it fails the data save and
  // restore between device to host can be avoid if memory preserved programming
  // succeeds otherwise it requires to save and restore buffer before and after
  // the memory unpreserved programming
  if (context->programs_devices) {
    int interleave_changed = 0;
    acl_device_def_t current_devdef =
        dev_prog->device->loaded_bin
            ? dev_prog->device->loaded_bin->get_devdef()
            : dev_prog->device->def;

    for (unsigned int i = 0;
         i < current_devdef.autodiscovery_def.num_global_mem_systems; i++) {
      // as long as one mem system change interleave setting, save and restore
      // all mem objects
      if (current_devdef.autodiscovery_def.global_mem_defs[i]
              .burst_interleaved != dev_bin->get_devdef()
                                        .autodiscovery_def.global_mem_defs[i]
                                        .burst_interleaved) {
        interleave_changed = 1;
        break;
      }
    }
    if (interleave_changed &&
        context->saves_and_restores_buffers_for_reprogramming) {
      acl_copy_device_buffers_to_host_before_programming(
          context, dev_prog->device->def.physical_device_id,
          context->reprogram_buf_read_callback);
    }

    acl_print_debug_msg("Runtime program_device: Programming device with "
                        "memory preservation\n");

    status = acl_get_hal()->program_device(
        dev_prog->device->def.physical_device_id, &(dev_bin->get_devdef()),
        dev_bin->get_binary_pkg(), ACL_PROGRAM_PRESERVE_MEM);
    if (dev_prog->program->context->split_kernel) {
      // In split kernel mode we need to ensure that binaries are only loaded
      // in memory when they are needed because of high memory usage when many
      // split binaries are present.
      dev_bin->unload_content();
    }

    if (interleave_changed &&
        context->saves_and_restores_buffers_for_reprogramming &&
        status != ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM) {
      acl_copy_device_buffers_from_host_after_programming(
          context, dev_prog->device->def.physical_device_id,
          context->reprogram_buf_write_callback);
    }

    if (status && status != ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM) {
      // program failed, either because of the failure of memory unpreserved
      // programming, or the invalid binary. no need to try reprogram again
      acl_print_debug_msg(
          "Runtime program_device: Programming the device failed. Exited.\n");
      if (status > 0)
        status = -status;
      acl_set_device_op_execution_status(op, status);
      return;
    } else if (status == ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM) {
      // program failed because of the failure of memory preserved programming.
      // need to firstly enable data saving from device to host
      // and secondly reprogram the full chip
      acl_print_debug_msg(
          "Runtime program_device: Trying memory preserved programming failed, "
          "reprogramming the full device\n");
      if (!interleave_changed &&
          context->saves_and_restores_buffers_for_reprogramming) {
        // Important!  This will transfer all memory buffers on the device,
        // regardless of whether they belong to the same context.
        acl_print_debug_msg("Runtime program_device: Preserving memory before "
                            "the reprogramming\n");
        acl_copy_device_buffers_to_host_before_programming(
            context, dev_prog->device->def.physical_device_id,
            context->reprogram_buf_read_callback);
      }
      // Delegate to HAL.
      // It blocks.  We only support one device at a time anyway.
      // acl_get_hal()->program_device( event, <image> )
      acl_print_debug_msg("Reprogram it!\n");
      status = acl_get_hal()->program_device(
          dev_prog->device->def.physical_device_id, &(dev_bin->get_devdef()),
          dev_bin->get_binary_pkg(), ACL_PROGRAM_UNPRESERVE_MEM);
      if (dev_prog->program->context->split_kernel) {
        // In split kernel mode we need to ensure that binaries are only loaded
        // in memory when they are needed because of high memory usage when many
        // split binaries are present.
        dev_bin->unload_content();
      }

      if (status) {
        // It failed.  Bail out early and mark the operation as failed.
        // Ensure the status we pass back is negative, so it's seen as an
        // error.
        if (status > 0)
          status = -status;
        acl_set_device_op_execution_status(op, status);
        return;
      } else {
        // reprogram succeeded.
        acl_print_debug_msg("Runtime program_device: Memory unpreserved "
                            "programming succeeded.\n");
      }
      // and thirdly enable data restoring from host to device
      if (context->saves_and_restores_buffers_for_reprogramming) {
        // Important!  This will transfer all memory buffers on the device,
        // regardless of whether they belong to the same context.
        acl_print_debug_msg("Runtime program_device: Restoring memory after "
                            "the reprogramming\n");
        acl_copy_device_buffers_from_host_after_programming(
            context, dev_prog->device->def.physical_device_id,
            context->reprogram_buf_write_callback);
      }
    } else {
      // memory preserved programming successed.
      acl_print_debug_msg(
          "Runtime program_device: Memory preserved programming succeeded.\n");
    }
    dev_prog->device->def.autodiscovery_def =
        dev_bin->get_devdef().autodiscovery_def;

    if (acl_platform.offline_mode == ACL_CONTEXT_MPSIM) {
      // Override the device name to the simulator.
      // In function acl_device_binary_t::load_binary_pkg, the name member will
      // be checked against the .acl.board section of the aocx file, which would
      // contain ACL_MPSIM_DEVICE_NAME when the binary is compiled with
      // -march=simulator flag
      dev_prog->device->def.autodiscovery_def.name = ACL_MPSIM_DEVICE_NAME;
    }
  } else {
    acl_print_debug_msg(
        "Reprogram it... but context does not program devices, so skipping\n");
  }

  // Keep track of what binary is currently loaded.
  dev_prog->device->loaded_bin = dev_bin;
  acl_set_device_op_execution_status(op, CL_COMPLETE);
  dev_prog->device->def.autodiscovery_def.binary_rand_hash =
      dev_bin->get_devdef().autodiscovery_def.binary_rand_hash;

  if (context->programs_devices) {
    acl_bind_and_process_all_pipes_transactions(
        context, dev_prog->device, dev_prog->device->def.autodiscovery_def);
  }
}

// Build from "source", and load binary, and binary_pkg.
static cl_int l_build_from_source(acl_device_program_info_t *dev_prog) {
  cl_context context;
  cl_int status = CL_SUCCESS;

  acl_assert_locked();

  context = dev_prog->program->context;
  // split kernel mode not supported when building from source.
  assert(context->split_kernel == 0);

  auto hash_dir = acl_compute_hash_dir_name(context, dev_prog->hash);
  if (hash_dir.empty())
    ERR_RET(CL_BUILD_PROGRAM_FAILURE, context,
            "Internal error: needed a hash, but it doesn't exist.");

  if (!acl_make_path_to_dir(hash_dir)) {
    ERR_RET(CL_BUILD_PROGRAM_FAILURE, context,
            "Can't make path to offline compilation directory.");
  }

  status = l_build_from_source_in_dir(dev_prog, hash_dir.c_str());
  if (status == CL_SUCCESS &&
      (context->programs_devices || context->uses_dynamic_sysdef)) {
    status = dev_prog->device_binary.load_binary_pkg(1, 1);
    // Upon success we can say, there is a binary associated with the device.
    // The only type we have for now is CL_PROGRAM_BINARY_TYPE_EXECUTABLE.
    if (status == CL_SUCCESS)
      assert(dev_prog->device_binary.get_binary_type() ==
             CL_PROGRAM_BINARY_TYPE_EXECUTABLE);
  }

  return status;
}

std::string acl_compute_hash_dir_name(cl_context context,
                                      const std::string &hash) {
  acl_assert_locked();

  // We want to handle tens of thousands of files in the library.
  // For hash:
  //    abcdefg...z
  // We compute result:
  //    <prefix>/ab/cd/efg...z
  // So we need at least 4 characters in the hash
  if (hash.empty() || hash.length() < 4) {
    // (Internal error: needed a hash, but it doesn't exist.)
    return "";
  } else {
    std::stringstream ss;
    ss << context->program_library_root << "/" << hash[0] << hash[1] << "/"
       << hash[2] << hash[3] << "/" << hash.substr(4);
    return ss.str();
  }
}

static cl_int l_build_from_source_in_dir(acl_device_program_info_t *dev_prog,
                                         const char *dir) {
  // Remember the current directory, change to the given directory, and
  // run the compile command with -initial-dir set to the current
  // directory, then come back to the current directory.
  //
  // We change into the given (hash) directory because we don't trust the
  // compile command to keep tidy.  Too bad, eh?
  // Besides, it meshes better with manual runs of the kernel compiler.
  //
  // We need to use -initial-dir just in case the user's code does
  //    #include "foo.h"
  // and foo.h is in the current directory.  Or if the user supplies
  // include directory arguments that are relative paths, e.g.
  //    -I../foo
  // See the OpenCL conformance "compiler" test.

  cl_context context = dev_prog->program->context;
  // split_kernel mode is not supported when building from source.
  assert(context->split_kernel == 0);
  cl_int status = CL_SUCCESS;
  const char *clfile = "kernels.cl";
  // We define two kinds of command files.
  //    0 - Compile the program.
  //    1 - Compile the program for the current mode, if the current mode
  //        only builds a stub.
  // We only need the second one if the compiler mode compiles programs
  // incompletely.
  const char *cmdfile[2] = {"build.cmd", "build.stub.cmd"};
  unsigned num_build_cmds = context->compiles_programs_incompletely ? 2U : 1U;

  acl_assert_locked();

  // Save current working directory
  auto cur_dir = acl_realpath_existing(".");
  if (cur_dir == "") {
    acl_context_callback(context, "Allocation failure");
    status = CL_OUT_OF_HOST_MEMORY;
  }

  // Change to program library directory.
  if (status == CL_SUCCESS && !acl_chdir(dir)) {
    acl_context_callback(context,
                         "Can't change to program library directory: ");
    acl_context_callback(context, dir);
    acl_context_callback(context, acl_support_last_error());
    status = CL_BUILD_PROGRAM_FAILURE;
  }

  // Write source to file, in binary mode.
  // We want binary mode so that the compile hashes and results are
  // cross-OS compatible.
  if (status == CL_SUCCESS && context->compiles_programs) {
    struct acl_file_handle_t *clfp;
    if (0 != (clfp = acl_fopen(clfile, "wb"))) {
      cl_program program = dev_prog->program;
      size_t bytes_write = program->source_len - 1; // skip terminating NUL.
      size_t bytes_written =
          acl_fwrite(program->source_text, 1, bytes_write, clfp);
      if (bytes_written != bytes_write) {
        status = CL_BUILD_PROGRAM_FAILURE;
        acl_context_callback(context, "Can't write source file: ");
        acl_context_callback(context, acl_support_last_error());
      }
      acl_fclose(clfp); // close in any case.
    } else {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context, "Can't write source file: ");
      acl_context_callback(context, acl_support_last_error());
    }
  }
  if (status == CL_SUCCESS && context->compiles_programs) {
    auto absolute_clfile = acl_realpath_existing(clfile);
    if (absolute_clfile == "") {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context, "Can't find source file");
    }
  }

  // Construct the compile command.
  std::array<std::string, 2> cmd;
  if (status == CL_SUCCESS && context->compiles_programs) {
    // Need the absolute path to the kernels file because aoc will
    // chdir to -initial-dir before reading the .cl file.
    for (unsigned i = 0; i < num_build_cmds; i++) {
      std::stringstream ss;
      auto compile_command = context->compile_command;
      if (num_build_cmds == 2 && i == 0) {
        compile_command = "aoc -tidy"; // What we need to fully compile to .aocx
      }

      ss << compile_command << " >build.log 2>&1 -board="
         << dev_prog->device->def.autodiscovery_def.name
         << " -hash=" << dev_prog->hash << " -initial-dir=" << cur_dir << " "
         << dir << "/" << clfile << " " << dev_prog->build_options << "\n";
      cmd[i] = ss.str();
    }
  }

  // Write the compile script.
  if (status == CL_SUCCESS && context->compiles_programs) {
    for (unsigned i = 0; i < num_build_cmds; i++) {
      struct acl_file_handle_t *cmdfp = acl_fopen(cmdfile[i], "wb");
      if (debug_mode > 0) {
        printf(" cmdfp %p\n", cmdfp);
      }
      size_t bytes_written = 0;
      if (cmdfp) {
        bytes_written = (size_t)acl_fprintf(cmdfp, "%s", cmd[i].c_str());
        acl_fclose(cmdfp); // close in any case.
      }
      if (!cmdfp || (cmd[i].length() != bytes_written)) {
        if (debug_mode > 0) {
          printf(" cmdlen %d  bw %d\n", (int)cmd[i].length(),
                 (int)bytes_written);
        }
        status = CL_BUILD_PROGRAM_FAILURE;
        acl_context_callback(context, "Can't write command file: ");
        acl_context_callback(context, acl_support_last_error());
      }
    }
  }

  if (status == CL_SUCCESS && context->compiles_programs) {
    // Run the compile command for the current mode.
    auto cmd_status = acl_system(cmd[num_build_cmds - 1].c_str());
    if (cmd_status != 0) {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context, "Build failed");
      if (acl_has_error())
        acl_context_callback(context, acl_support_last_error());
    }

    // Always load the build log, whether or not the build worked.
    if (std::ifstream log{"build.log", std::ios::ate}) {
      auto size = log.tellg();
      log.seekg(0);
      dev_prog->build_log.resize(static_cast<size_t>(size), '\0');
      log.read(&(dev_prog->build_log[0]), static_cast<std::streamsize>(size));
    } else {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context, "Could not capture build log: ");
      acl_context_callback(context, dir);
      acl_context_callback(context, "build.log");
      if (acl_has_error())
        acl_context_callback(context, acl_support_last_error());
    }
  }

  if (status == CL_SUCCESS &&
      (context->programs_devices || context->uses_dynamic_sysdef)) {
    // Load the build output!
    // (compiles_program should imply we get into this case too).
    //
    // If possible, load the full binary (ends in "x").
    // Otherwise fall back to the partially compiled binary (ends in "r").

    if (context->programs_devices) {
      // If programming the device, then we truly need the full .aocx binary
      // because that has all the programming info we need.
      dev_prog->device_binary.load_content("kernels.aocx");
    } else if (context->uses_dynamic_sysdef) {
      // But if we only need the dynamic sysdef, then we can fall back on
      // using the partial compilation result.
      dev_prog->device_binary.load_content("kernels.aocr");
    }

    if (!dev_prog->device_binary.get_content())
      status = CL_BUILD_PROGRAM_FAILURE;

    if (status != CL_SUCCESS) {
      // Unload the binary, because we allocated it.
      dev_prog->device_binary.unload_content();
    }

    if (!dev_prog->device_binary.get_content()) {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context, "Could not load the binary: ");
      acl_context_callback(context, acl_support_last_error());
      acl_context_callback(context, dir);
      acl_context_callback(
          context,
          (context->programs_devices ? "kernels.aocx" : "kernels.aocr"));
    }
  }

  // Change back to original directory.
  if (!acl_chdir(cur_dir.c_str())) {
    if (status == CL_SUCCESS) {
      status = CL_BUILD_PROGRAM_FAILURE;
      acl_context_callback(context,
                           "Could not change back into source directory");
    }
  }

  return status;
}

void acl_program_dump_dev_prog(acl_device_program_info_t *dev_prog) {
  acl_assert_locked();

  acl_print_debug_msg("dev_prog: %p {\n", dev_prog);

  if (dev_prog && (debug_mode > 0)) {
    printf("        program[%p]\n", dev_prog->program);
    printf("        device [%d] %s\n", dev_prog->device->id,
           dev_prog->device->def.autodiscovery_def.name.c_str());
    printf("        status %d\n", dev_prog->build_status);
    printf("        options '%s'\n",
           (!dev_prog->build_options.empty() ? dev_prog->build_options.c_str()
                                             : "(nil)"));
    printf("        bin_len %lu\n",
           (unsigned long)dev_prog->device_binary.get_binary_len());
    printf("        bin     %p\n", dev_prog->device_binary.get_content());
    printf("        bin_pkg %p\n", dev_prog->device_binary.get_binary_pkg());
    printf("        hash    %s\n", dev_prog->hash.c_str());
  }

  acl_print_debug_msg("       }\n");
}

// Eagerly program the device.
// In most cases, we'll have only one program, and in fact, the first program
// to be created from binary, or built from source is the first one to be run.
static void l_try_to_eagerly_program_device(cl_program program) {
  cl_uint idev;
  acl_assert_locked();

  if (!program->context->eagerly_program_device_with_first_binary)
    return;
  for (idev = 0; idev < program->num_devices; idev++) {
    acl_device_program_info_t *dev_prog = program->dev_prog[idev];
    if (dev_prog && dev_prog->build_status == CL_BUILD_SUCCESS) {
      cl_device_id device = dev_prog->device;
      if (!device->last_bin) {
        // Nothing program is scheduled to be loaded onto the device.
        cl_int result;

        // Use a regular event with an internal command type.
        // We want a regular event so we get profiling info, so that we
        // have the infrastructure to measure delays, and the user
        // could in principle optimize this delay away.
        cl_event reprogram_event = 0;

        // Just use the auto_queue. We really only support one device
        // anyway.
        cl_command_queue cq = program->context->auto_queue;

        // Schedule an eager programming of the device.
        acl_print_debug_msg(
            "Device is not yet programmed: plan to eagerly program it\n");

        result = acl_create_event(cq, 0, 0, // Don't wait on other events
                                  CL_COMMAND_PROGRAM_DEVICE_INTELFPGA,
                                  &reprogram_event);

        if (result == CL_SUCCESS) {

          reprogram_event->cmd.info.eager_program = &(dev_prog->device_binary);

          // Need to retain the program until we've finished this
          // programming command.
          clRetainProgram(dev_prog->program);
          // Now prod the scheduler.
          // This should cause reprogramming to occur.
          // And that should update device->last_bin to be updated.
          acl_idle_update_queue(cq);
          // We don't need the handle to the event anymore.
          // This will also nudge both schedulers!
          clReleaseEvent(reprogram_event);
        }
      }
    }
  }
}

// Copy memory definition from src_dev to dest_dev.
static void
l_device_memory_definition_copy(acl_device_def_autodiscovery_t *dest_dev,
                                acl_device_def_autodiscovery_t *src_dev) {
  unsigned int idef;
  dest_dev->num_global_mem_systems = src_dev->num_global_mem_systems;
  for (idef = 0; idef < src_dev->num_global_mem_systems; ++idef) {
    dest_dev->global_mem_defs[idef].range =
        src_dev->global_mem_defs[idef].range;
    dest_dev->global_mem_defs[idef].type = src_dev->global_mem_defs[idef].type;
    dest_dev->global_mem_defs[idef].burst_interleaved =
        src_dev->global_mem_defs[idef].burst_interleaved;
    dest_dev->global_mem_defs[idef].config_addr =
        src_dev->global_mem_defs[idef].config_addr;
    dest_dev->global_mem_defs[idef].num_global_banks =
        src_dev->global_mem_defs[idef].num_global_banks;
    dest_dev->global_mem_defs[idef].name = src_dev->global_mem_defs[idef].name;
    dest_dev->global_mem_defs[idef].allocation_type =
        src_dev->global_mem_defs[idef].allocation_type;
    dest_dev->global_mem_defs[idef].primary_interface =
        src_dev->global_mem_defs[idef].primary_interface;
    dest_dev->global_mem_defs[idef].can_access_list =
        src_dev->global_mem_defs[idef].can_access_list;
    dest_dev->global_mem_defs[idef].id = src_dev->global_mem_defs[idef].id;
  }
}

// Schedule an eager programming of the device onto the device op queue.
// Return a positive number if we succeeded, 0 otherwise.
int acl_submit_program_device_op(cl_event event) {
  int result = 0;
  acl_assert_locked();

  // Only schedule if it is a valid event, and it isn't already scheduled.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  if (!acl_command_queue_is_valid(event->command_queue)) {
    return result;
  }
  if (!event->last_device_op) {
    acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
    acl_device_op_t *last_op = 0;

    // Precautionary, but it also nudges the device scheduler to try
    // to free up old operation slots.
    acl_forget_proposed_device_ops(doq);
    // Try scheduling it.
    last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_REPROGRAM, event);
    if (last_op) {
      // We managed to add this to the device op queue.
      // Record this program as the last one to be on the device
      // after all device ops up to this point are finished.
      auto *dev_bin = event->cmd.info.eager_program;
      dev_bin->get_dev_prog()->device->last_bin = dev_bin;

      // Mark this event not requiring another submit.
      event->last_device_op = last_op;

      // Commit and nudge the device op scheduler.
      acl_commit_proposed_device_ops(doq);
      result = 1;
    }
  }
  return result;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_CONTEXT_H
#define ACL_CONTEXT_H

#include <CL/opencl.h>

#include "acl_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Constants
static constexpr const char *ENV_CL_CONTEXT_ALLOW_MULTIPROCESSING_INTELFPGA =
    "CL_CONTEXT_ALLOW_MULTIPROCESSING_INTELFPGA";
static constexpr const char *ENV_AOCL_EAGERLY_LOAD_FIRST_BINARY =
    "AOCL_EAGERLY_LOAD_FIRST_BINARY";
static constexpr const char *ENV_CL_CONTEXT_COMPILER_MODE_INTELFPGA =
    "CL_CONTEXT_COMPILER_MODE_INTELFPGA";
static constexpr const char *ENV_CL_CONTEXT_COMPILER_MODE_ALTERA =
    "CL_CONTEXT_COMPILER_MODE_ALTERA";
static constexpr const char *ENV_CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA =
    "CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA";
static constexpr const char *ENV_CL_CONTEXT_COMPILE_COMMAND_INTELFPGA =
    "CL_CONTEXT_COMPILE_COMMAND_INTELFPGA";
static constexpr const char *ENV_ACL_CONTEXT_CALLBACK_DEBUG =
    "ACL_CONTEXT_CALLBACK_DEBUG";

// Update data structures in response to async msgs from devices.
// Do this as part of a waiting loop, along with acl_hal_yield().
void acl_update_context(cl_context context);
void acl_idle_update(cl_context context);

int acl_context_uses_device(cl_context context, cl_device_id device);

acl_kernel_invocation_wrapper_t *
acl_get_unused_kernel_invocation_wrapper(cl_context context);

void acl_context_print_hung_device_status(cl_context context);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif

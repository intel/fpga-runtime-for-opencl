/*******************************************************************************
 * Copyright (c) 2008-2020 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************/
/*****************************************************************************\

Copyright (c) 2013-2020 Intel Corporation All Rights Reserved.

THESE MATERIALS ARE PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THESE
MATERIALS, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

File Name: cl_ext_intelfpga.h

Abstract:

Notes:

\*****************************************************************************/

#ifndef __CL_EXT_INTELFPGA_H
#define __CL_EXT_INTELFPGA_H

#include <CL/cl.h>
#include <CL/cl_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************
* cl_intelfpga_mem_banks *
**************************/
/* cl_mem_flags - bitfield */
#define CL_CHANNEL_AUTO_INTELFPGA            (0 << 16)
#define CL_CHANNEL_1_INTELFPGA               (1 << 16)
#define CL_CHANNEL_2_INTELFPGA               (2 << 16)
#define CL_CHANNEL_3_INTELFPGA               (3 << 16)
#define CL_CHANNEL_4_INTELFPGA               (4 << 16)
#define CL_CHANNEL_5_INTELFPGA               (5 << 16)
#define CL_CHANNEL_6_INTELFPGA               (6 << 16)
#define CL_CHANNEL_7_INTELFPGA               (7 << 16) 

#define CL_MEM_HETEROGENEOUS_INTELFPGA       (1 << 19)

/**********************************
* cl_intelfpga_device_temperature *
***********************************/
#define cl_intelfpga_device_temperature 1
/* Enum query for clGetDeviceInfo to get the die temperature in Celsius as a cl_int.
 * If the device does not support the query then the result will be 0.
 */
#define CL_DEVICE_CORE_TEMPERATURE_INTELFPGA 0x40F3


/************************************
* cl_intelfpga_live_object_tracking *
*************************************/
#define cl_intelfpga_live_object_tracking 1

/* Call this to begin tracking CL API objects.
 * Ideally, do this immediately after getting the platform ID.
 * This takes extra space and time.
 */
extern CL_API_ENTRY void CL_API_CALL
clTrackLiveObjectsIntelFPGA(
    cl_platform_id   platform);

/* Call this to be informed of all the live CL API objects, with their
 * reference counts.
 * The type name argument to the callback will be the string form of the type name
 * e.g. "cl_event" for a cl_event.
 */
extern CL_API_ENTRY void CL_API_CALL
clReportLiveObjectsIntelFPGA(
    cl_platform_id         platform,
    void (CL_CALLBACK *    report_fn)(
        void *                 user_data,
        void *                 obj_ptr,
        const char *           type_name,
        cl_uint                refcount),
    void *                 user_data);


/**************************
* cl_intel_fpga_host_pipe *
***************************/
/* error codes */
#define CL_PIPE_FULL                         -1111
#define CL_PIPE_EMPTY                        -1112

typedef CL_API_ENTRY cl_int (CL_API_CALL *clReadPipeIntelFPGA_fn)(
    cl_mem         pipe,
    void *         ptr);
extern CL_API_ENTRY cl_int CL_API_CALL
clReadPipeIntelFPGA(
    cl_mem         pipe,
    void *         ptr);

typedef CL_API_ENTRY cl_int (CL_API_CALL *clWritePipeIntelFPGA_fn)(
    cl_mem         pipe,
    void *         ptr);
extern CL_API_ENTRY cl_int CL_API_CALL
clWritePipeIntelFPGA(
    cl_mem         pipe,
    void *         ptr);

typedef CL_API_ENTRY void * (CL_API_CALL *clMapHostPipeIntelFPGA_fn)(
    cl_mem         pipe,
    cl_map_flags   map_flags,
    size_t         requested_size,
    size_t *       mapped_size,
    cl_int *       errcode_ret);
extern CL_API_ENTRY void *CL_API_CALL
clMapHostPipeIntelFPGA(
    cl_mem         pipe,
    cl_map_flags   map_flags,
    size_t         requested_size,
    size_t *       mapped_size,
    cl_int *       errcode_ret);

typedef CL_API_ENTRY cl_int (CL_API_CALL *clUnmapHostPipeIntelFPGA_fn)(
    cl_mem         pipe,
    void *         mapped_ptr,
    size_t         size_to_unmap,
    size_t *       unmapped_size);
extern CL_API_ENTRY cl_int CL_API_CALL
clUnmapHostPipeIntelFPGA(
    cl_mem         pipe,
    void *         mapped_ptr,
    size_t         size_to_unmap,
    size_t *       unmapped_size);


/******************************
* Intel FPGA profiler support *
*******************************/
/* Call this to query the FPGA and collect dynamic profiling data
 * for a single kernel.
 *
 * The event passed to this call must be the event used
 * in the kernel clEnqueueNDRangeKernel call. If the kernel
 * completes execution before this function is invoked,
 * this function will return an event error code.
 *
 * NOTE:
 * Invoking this function while the kernel is running will
 * disable the profile counters for a given interval.
 * For example, on a PCIe-based system this was measured
 * to be approximately 100us.
 */
typedef CL_API_ENTRY cl_int (CL_API_CALL *clGetProfileInfoIntelFPGA_fn)(
    cl_event       kernel_event);
extern CL_API_ENTRY cl_int CL_API_CALL
clGetProfileInfoIntelFPGA(
    cl_event       kernel_event);

/* Call this to query the FPGA and collect dynamic profiling data
 * for all the kernels on the device.
 * A boolean can be used to gather all enqueued and/or all autorun
 * kernels, assuming there are profiling counters
 *
 * NOTE:
 * Invoking this function while the kernel is running will
 * disable the profile counters for a given interval.
 * For example, on a PCIe-based system this was measured
 * to be approximately 100us.
 */
typedef CL_API_ENTRY cl_int (CL_API_CALL *clGetProfileDataDeviceIntelFPGA_fn)(
    cl_device_id   device_id,
    cl_program     program,
    cl_bool        read_enqueue_kernels,
    cl_bool        read_auto_enqueued,
    cl_bool        clear_counters_after_readback,
    size_t         param_value_size,
    void *         param_value,
    size_t *       param_value_size_ret,
    cl_int *       errcode_ret);
extern CL_API_ENTRY cl_int CL_API_CALL
clGetProfileDataDeviceIntelFPGA(
    cl_device_id   device_id,
    cl_program     program,
    cl_bool        read_enqueue_kernels,
    cl_bool        read_auto_enqueued,
    cl_bool        clear_counters_after_readback,
    size_t         param_value_size,
    void *         param_value,
    size_t *       param_value_size_ret,
    cl_int *       errcode_ret);


/*****************************
* cl_intelfpga_compiler_mode *
******************************/
#define cl_intelfpga_compiler_mode 1

#define CL_CONTEXT_COMPILER_MODE_INTELFPGA 0x40F0

#define CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA 0
#define CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA 1
#define CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY_INTELFPGA 2
#define CL_CONTEXT_COMPILER_MODE_PRELOADED_BINARY_ONLY_INTELFPGA 3

/* This property is used to specify the root directory of
 * the executable program library for compiler modes
 * CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY and
 * CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY.
 * The value should be a pointer to a C-style character string naming
 * the directory. It can be relative, but will be resolved to an absolute
 * directory at context creation time.
 */
#define CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA 0x40F1

/* This property is used to emulate, as much as possible,
 * having a device that is actually not attached.
 * Kernels may be enqueued but their code will not be run,
 * so data coming back from the device may be invalid.
 * The value should be a pointer to a C-style character string with the
 * short name for the device.
 */
#define CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA 0x40F2


/*************************
* Intel FPGA FCD support *
**************************/
typedef CL_API_ENTRY void * (CL_API_CALL *clGetBoardExtensionFunctionAddressIntelFPGA_fn)(
    const char *   func_name,
    cl_device_id   device);
extern CL_API_ENTRY void * CL_API_CALL
clGetBoardExtensionFunctionAddressIntelFPGA(
    const char *   func_name,
    cl_device_id   device);


/*************************
* Intel FPGA ECC support *
**************************/
typedef cl_ulong CL_EXCEPTION_TYPE_INTEL;
#define CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL 1
#define CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL 2

typedef CL_API_ENTRY cl_int (CL_API_CALL *clSetDeviceExceptionCallbackIntelFPGA_fn)(
    cl_uint                   num_devices,
    const cl_device_id *      devices,
    CL_EXCEPTION_TYPE_INTEL   listen_mask,
    void (CL_CALLBACK *       pfn_exception_notify)(
        CL_EXCEPTION_TYPE_INTEL   exception_type,
        const void *              private_info,
        size_t                    cb,
        void *                    user_data),
    void *                    user_data);
extern CL_API_ENTRY cl_int CL_API_CALL
clSetDeviceExceptionCallbackIntelFPGA(
    cl_uint                   num_devices,
    const cl_device_id *      devices,
    CL_EXCEPTION_TYPE_INTEL   listen_mask,
    void (CL_CALLBACK *       pfn_exception_notify)(
        CL_EXCEPTION_TYPE_INTEL   exception_type,
        const void *              private_info,
        size_t                    cb,
        void *                    user_data),
    void *                    user_data);


/************************
* Intel FPGA extra APIs *
*************************/
typedef CL_API_ENTRY cl_program (CL_API_CALL *clCreateProgramWithBinaryAndProgramDeviceIntelFPGA_fn)(
    cl_context               context,
    cl_uint                  num_devices,
    const cl_device_id *     device_list,
    const size_t *           lengths,
    const unsigned char **   binaries,
    cl_int *                 binary_status,
    cl_int *                 errcode_ret) CL_API_SUFFIX__VERSION_1_0;
extern CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinaryAndProgramDeviceIntelFPGA(
    cl_context               context,
    cl_uint                  num_devices,
    const cl_device_id *     device_list,
    const size_t *           lengths,
    const unsigned char **   binaries,
    cl_int *                 binary_status,
    cl_int *                 errcode_ret) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_int (CL_API_CALL *clReconfigurePLLIntelFPGA_fn) (
    cl_device_id             device,
    const char *             pll_settings_str);
extern CL_API_ENTRY cl_int CL_API_CALL
clReconfigurePLLIntelFPGA(
    cl_device_id             device,
    const char *             pll_settings_str);

typedef CL_API_ENTRY cl_int (CL_API_CALL *clResetKernelsIntelFPGA_fn) (
    cl_context               context,
    cl_uint                  num_devices,
    const cl_device_id *     device_list);
extern CL_API_ENTRY cl_int CL_API_CALL
clResetKernelsIntelFPGA(
    cl_context               context,
    cl_uint                  num_devices,
    const cl_device_id *     device_list);

extern CL_API_ENTRY void CL_API_CALL
clSetBoardLibraryIntelFPGA(
    char *                   library_name);

#ifdef __cplusplus
}
#endif

#endif /* __CL_EXT_INTELFPGA_H */

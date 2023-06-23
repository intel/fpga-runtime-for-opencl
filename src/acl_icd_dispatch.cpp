// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <string.h>

// External library headers.
#include <CL/cl_ext_intelfpga.h>
#include <CL/opencl.h>

// Internal headers.
#include <acl_icd_dispatch.h>
#include <acl_thread.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#ifndef MAX_NAME_SIZE
#define MAX_NAME_SIZE 1204
#endif

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddressIntelFPGA(const char *func_name) {
// MSVC doesn't like the conversion from function pointer to data pointer,
// but that is what the OpenCL function requires so we allow it
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4054)
#endif
#define ADDFUNCTIONLOOKUP(name)                                                \
  if (strncmp(func_name, #name, MAX_NAME_SIZE) == 0)                           \
  return (void *)name

  ADDFUNCTIONLOOKUP(clIcdGetPlatformIDsKHR);
  ADDFUNCTIONLOOKUP(clTrackLiveObjectsIntelFPGA);
  ADDFUNCTIONLOOKUP(clReportLiveObjectsIntelFPGA);
  ADDFUNCTIONLOOKUP(clGetProfileInfoIntelFPGA);
  ADDFUNCTIONLOOKUP(clGetProfileDataDeviceIntelFPGA);
  ADDFUNCTIONLOOKUP(clGetBoardExtensionFunctionAddressIntelFPGA);
  ADDFUNCTIONLOOKUP(clReadPipeIntelFPGA);
  ADDFUNCTIONLOOKUP(clWritePipeIntelFPGA);
  ADDFUNCTIONLOOKUP(clMapHostPipeIntelFPGA);
  ADDFUNCTIONLOOKUP(clUnmapHostPipeIntelFPGA);
  ADDFUNCTIONLOOKUP(clSetDeviceExceptionCallbackIntelFPGA);
  ADDFUNCTIONLOOKUP(clCreateProgramWithBinaryAndProgramDeviceIntelFPGA);
  ADDFUNCTIONLOOKUP(clReconfigurePLLIntelFPGA);
  ADDFUNCTIONLOOKUP(clResetKernelsIntelFPGA);
  ADDFUNCTIONLOOKUP(clSetBoardLibraryIntelFPGA);
  ADDFUNCTIONLOOKUP(clCreateBufferWithPropertiesINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueReadHostPipeINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueWriteHostPipeINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueReadGlobalVariableINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueWriteGlobalVariableINTEL);

// USM APIs are not currently supported on 32bit devices
#ifndef __arm__
  // Add USM APIs
  ADDFUNCTIONLOOKUP(clHostMemAllocINTEL);
  ADDFUNCTIONLOOKUP(clDeviceMemAllocINTEL);
  ADDFUNCTIONLOOKUP(clSharedMemAllocINTEL);
  ADDFUNCTIONLOOKUP(clMemFreeINTEL);
  ADDFUNCTIONLOOKUP(clMemBlockingFreeINTEL);
  ADDFUNCTIONLOOKUP(clGetMemAllocInfoINTEL);
  ADDFUNCTIONLOOKUP(clSetKernelArgMemPointerINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueMemsetINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueMemFillINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueMemcpyINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueMigrateMemINTEL);
  ADDFUNCTIONLOOKUP(clEnqueueMemAdviseINTEL);
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

  return NULL;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clGetBoardExtensionFunctionAddressIntelFPGA(
    const char *func_name, cl_device_id device) {
  std::scoped_lock lock{acl_mutex_wrapper};
  {
    void *ret = acl_get_hal()->get_board_extension_function_address(
        func_name, device->def.physical_device_id);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddress(const char *func_name) {
  return clGetExtensionFunctionAddressIntelFPGA(func_name);
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddressForPlatformIntelFPGA(cl_platform_id platform,
                                                  const char *func_name) {
  // We currently only have one platform
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_platform_is_valid(platform)) {
    return NULL;
  }
  return clGetExtensionFunctionAddressIntelFPGA(func_name);
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clGetExtensionFunctionAddressForPlatform(
    cl_platform_id platform, const char *func_name) {
  return clGetExtensionFunctionAddressForPlatformIntelFPGA(platform, func_name);
}

cl_icd_dispatch acl_icd_dispatch = {
    // 0
    clGetPlatformIDsIntelFPGA, clGetPlatformInfoIntelFPGA,
    clGetDeviceIDsIntelFPGA, clGetDeviceInfoIntelFPGA, clCreateContextIntelFPGA,
    // 5
    clCreateContextFromTypeIntelFPGA, clRetainContextIntelFPGA,
    clReleaseContextIntelFPGA, clGetContextInfoIntelFPGA,
    clCreateCommandQueueIntelFPGA,
    // 10
    clRetainCommandQueueIntelFPGA, clReleaseCommandQueueIntelFPGA,
    clGetCommandQueueInfoIntelFPGA,
#ifdef CL_USE_DEPRECATED_OPENCL_1_0_APIS
    clSetCommandQueuePropertyIntelFPGA,
#else
    NULL,
#endif
    clCreateBufferIntelFPGA,
    // 15
    clCreateImage2DIntelFPGA, clCreateImage3DIntelFPGA,
    clRetainMemObjectIntelFPGA, clReleaseMemObjectIntelFPGA,
    clGetSupportedImageFormatsIntelFPGA,
    // 20
    clGetMemObjectInfoIntelFPGA, clGetImageInfoIntelFPGA,
    clCreateSamplerIntelFPGA, clRetainSamplerIntelFPGA,
    clReleaseSamplerIntelFPGA,
    // 25
    clGetSamplerInfoIntelFPGA, clCreateProgramWithSourceIntelFPGA,
    clCreateProgramWithBinaryIntelFPGA, clRetainProgramIntelFPGA,
    clReleaseProgramIntelFPGA,
    // 30
    clBuildProgramIntelFPGA, clUnloadCompilerIntelFPGA,
    clGetProgramInfoIntelFPGA, clGetProgramBuildInfoIntelFPGA,
    clCreateKernelIntelFPGA,
    // 35
    clCreateKernelsInProgramIntelFPGA, clRetainKernelIntelFPGA,
    clReleaseKernelIntelFPGA, clSetKernelArgIntelFPGA, clGetKernelInfoIntelFPGA,
    // 40
    clGetKernelWorkGroupInfoIntelFPGA, clWaitForEventsIntelFPGA,
    clGetEventInfoIntelFPGA, clRetainEventIntelFPGA, clReleaseEventIntelFPGA,
    // 45
    clGetEventProfilingInfoIntelFPGA, clFlushIntelFPGA, clFinishIntelFPGA,
    clEnqueueReadBufferIntelFPGA, clEnqueueWriteBufferIntelFPGA,
    // 50
    clEnqueueCopyBufferIntelFPGA, clEnqueueReadImageIntelFPGA,
    clEnqueueWriteImageIntelFPGA, clEnqueueCopyImageIntelFPGA,
    clEnqueueCopyImageToBufferIntelFPGA,
    // 55
    clEnqueueCopyBufferToImageIntelFPGA, clEnqueueMapBufferIntelFPGA,
    clEnqueueMapImageIntelFPGA, clEnqueueUnmapMemObjectIntelFPGA,
    clEnqueueNDRangeKernelIntelFPGA,
    // 60
    clEnqueueTaskIntelFPGA, clEnqueueNativeKernelIntelFPGA,
    clEnqueueMarkerIntelFPGA, clEnqueueWaitForEventsIntelFPGA,
    clEnqueueBarrierIntelFPGA,
    // 65
    clGetExtensionFunctionAddressIntelFPGA,

    /* cl_khr_gl_sharing */
    NULL, // clCreateFromGLBuffer;
    NULL, // clCreateFromGLTexture2D;
    NULL, // clCreateFromGLTexture3D;
    NULL, // clCreateFromGLRenderbuffer;
    NULL, // clGetGLObjectInfo;
    NULL, // clGetGLTextureInfo;
    NULL, // clEnqueueAcquireGLObjects;
    NULL, // clEnqueueReleaseGLObjects;
    NULL, // clGetGLContextInfoKHR;

    /* cl_khr_d3d10_sharing */
    NULL, // clGetDeviceIDsFromD3D10KHR;
    NULL, // clCreateFromD3D10BufferKHR;
    NULL, // clCreateFromD3D10Texture2DKHR;
    NULL, // clCreateFromD3D10Texture3DKHR;
    NULL, // clEnqueueAcquireD3D10ObjectsKHR;
    NULL, // clEnqueueReleaseD3D10ObjectsKHR;

    /* OpenCL 1.1 */
    clSetEventCallbackIntelFPGA, clCreateSubBufferIntelFPGA,
    clSetMemObjectDestructorCallbackIntelFPGA, clCreateUserEventIntelFPGA,
    clSetUserEventStatusIntelFPGA, clEnqueueReadBufferRectIntelFPGA,
    clEnqueueWriteBufferRectIntelFPGA, clEnqueueCopyBufferRectIntelFPGA,

    /* cl_ext_device_fission */
    NULL, // clCreateSubDevicesEXT;
    NULL, // clRetainDeviceEXT;
    NULL, // clReleaseDeviceEXT;

    /* cl_khr_gl_event */
    NULL, // clCreateEventFromGLsyncKHR;

    /* OpenCL 1.2 */
    clCreateSubDevicesIntelFPGA, // Not implemented
    clRetainDeviceIntelFPGA,     // Not implemented
    clReleaseDeviceIntelFPGA,    // Not implemented
    clCreateImageIntelFPGA,
    clCreateProgramWithBuiltInKernelsIntelFPGA, // Not implemented
    clCompileProgramIntelFPGA,                  // Not implemented
    clLinkProgramIntelFPGA,                     // Not implemented
    clUnloadPlatformCompilerIntelFPGA,          // Not implemented
    clGetKernelArgInfoIntelFPGA, clEnqueueFillBufferIntelFPGA,
    clEnqueueFillImageIntelFPGA, clEnqueueMigrateMemObjectsIntelFPGA,
    clEnqueueMarkerWithWaitListIntelFPGA,  // Not implemented
    clEnqueueBarrierWithWaitListIntelFPGA, // Not implemented
    clGetExtensionFunctionAddressForPlatformIntelFPGA,

    /* cl_khr_gl_sharing */
    NULL, // clCreateFromGLTexture;

    /* cl_khr_d3d11_sharing */
    NULL, // clGetDeviceIDsFromD3D11KHR;
    NULL, // clCreateFromD3D11BufferKHR;
    NULL, // clCreateFromD3D11Texture2DKHR;
    NULL, // clCreateFromD3D11Texture3DKHR;
    NULL, // clCreateFromDX9MediaSurfaceKHR;
    NULL, // clEnqueueAcquireD3D11ObjectsKHR;
    NULL, // clEnqueueReleaseD3D11ObjectsKHR;

    /* cl_khr_dx9_media_sharing */
    NULL, // clGetDeviceIDsFromDX9MediaAdapterKHR;
    NULL, // clEnqueueAcquireDX9MediaSurfacesKHR;
    NULL, // clEnqueueReleaseDX9MediaSurfacesKHR;

    /* cl_khr_egl_image */
    NULL, // clCreateFromEGLImageKHR;
    NULL, // clEnqueueAcquireEGLObjectsKHR;
    NULL, // clEnqueueReleaseEGLObjectsKHR;

    /* cl_khr_egl_event */
    NULL, // clCreateEventFromEGLSyncKHR;

    /* OpenCL 2.0 */
    clCreateCommandQueueWithPropertiesIntelFPGA, clCreatePipeIntelFPGA,
    clGetPipeInfoIntelFPGA, clSVMAllocIntelFPGA, clSVMFreeIntelFPGA,
    clEnqueueSVMFreeIntelFPGA, clEnqueueSVMMemcpyIntelFPGA,
    clEnqueueSVMMemFillIntelFPGA, clEnqueueSVMMapIntelFPGA,
    clEnqueueSVMUnmapIntelFPGA, clCreateSamplerWithPropertiesIntelFPGA,
    clSetKernelArgSVMPointerIntelFPGA,
    clSetKernelExecInfoIntelFPGA, // Not implemented

    /* cl_khr_sub_groups */
    NULL, // clGetKernelSubGroupInfoKHR;
};

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

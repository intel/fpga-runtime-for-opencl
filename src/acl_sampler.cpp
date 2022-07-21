// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <sstream>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl_globals.h>
#include <acl_util.h>

// Samplers and images are not supported.

// In case we ever fill this out, make sure our internals are somewhat
// hidden from user's code.  For 14.0, make it protected.
// Later, strongly consider upgrading to "hidden".

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

//////////////////////////////
// OpenCL API

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif
ACL_EXPORT
CL_API_ENTRY cl_sampler clCreateSamplerWithPropertiesIntelFPGA(
    cl_context context, const cl_sampler_properties *sampler_properties,
    cl_int *errcode_ret) {
  cl_sampler result = 0;
  size_t iprop;
  unsigned int idevice;
  cl_bool some_device_supports_images = CL_FALSE;
  int sampler_id;
  cl_sampler sampler;
  int next_free_sampler_head;

  std::scoped_lock lock{acl_mutex_wrapper};

  sampler_id = acl_platform.free_sampler_head;
  sampler = &(acl_platform.sampler[sampler_id]);
  next_free_sampler_head = acl_platform.sampler[sampler_id].link;

  // This should be redundant
  acl_reset_ref_count(sampler);
  sampler->normalized_coords = 0xFFFFFFFF;
  sampler->addressing_mode = 0xFFFFFFFF;
  sampler->filter_mode = 0xFFFFFFFF;

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }

  sampler->context = context;

  for (idevice = 0; idevice < context->num_devices; ++idevice) {
    cl_bool support_images;
    cl_int error_status;
    error_status =
        clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE_SUPPORT,
                        sizeof(cl_bool), &support_images, NULL);
    if (error_status != CL_SUCCESS) {
      BAIL(CL_OUT_OF_RESOURCES);
    }
    if (support_images) {
      some_device_supports_images = CL_TRUE;
      break;
    }
  }
  if (!some_device_supports_images) {
    BAIL_INFO(CL_INVALID_OPERATION, context,
              "No devices in context support images");
  }

  iprop = 0;
  while (sampler_properties[iprop] != 0) {
    if (sampler_properties[iprop] == CL_SAMPLER_NORMALIZED_COORDS) {
      ++iprop;
      if (sampler->normalized_coords != 0xFFFFFFFF) {
        BAIL_INFO(
            CL_INVALID_VALUE, context,
            "Normalized coords property specified more than once for sampler");
      }
      if (sampler_properties[iprop] != CL_FALSE &&
          sampler_properties[iprop] != CL_TRUE) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Invalid value for normalized coords property of sampler");
      }
      sampler->normalized_coords = sampler_properties[iprop];
    } else if (sampler_properties[iprop] == CL_SAMPLER_ADDRESSING_MODE) {
      ++iprop;
      if (sampler->addressing_mode != 0xFFFFFFFF) {
        BAIL_INFO(
            CL_INVALID_VALUE, context,
            "Addressing mode property specified more than once for sampler");
      }
      if (sampler_properties[iprop] != CL_ADDRESS_MIRRORED_REPEAT &&
          sampler_properties[iprop] != CL_ADDRESS_REPEAT &&
          sampler_properties[iprop] != CL_ADDRESS_CLAMP_TO_EDGE &&
          sampler_properties[iprop] != CL_ADDRESS_CLAMP &&
          sampler_properties[iprop] != CL_ADDRESS_NONE) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Invalid value for addressing mode property of sampler");
      }
      sampler->addressing_mode = sampler_properties[iprop];
    } else if (sampler_properties[iprop] == CL_SAMPLER_FILTER_MODE) {
      ++iprop;
      if (sampler->filter_mode != 0xFFFFFFFF) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Filter mode property specified more than once for sampler");
      }
      if (sampler_properties[iprop] != CL_FILTER_NEAREST &&
          sampler_properties[iprop] != CL_FILTER_LINEAR) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Invalid value for filter mode property of sampler");
      }
      sampler->filter_mode = sampler_properties[iprop];
    } else {
      std::stringstream msg;
      msg << "Invalid sampler property name " << sampler_properties[iprop]
          << "\n";
      BAIL_INFO(CL_INVALID_VALUE, context, msg.str().c_str());
    }
    ++iprop;
  }

  if (sampler->normalized_coords == 0xFFFFFFFF) {
    sampler->normalized_coords = CL_TRUE;
  }
  if (sampler->addressing_mode == 0xFFFFFFFF) {
    sampler->addressing_mode = CL_ADDRESS_CLAMP;
  }
  if (sampler->filter_mode == 0xFFFFFFFF) {
    sampler->filter_mode = CL_FILTER_NEAREST;
  }

  acl_platform.free_sampler_head = next_free_sampler_head;

  result = sampler;

  acl_retain(result);
  acl_retain(context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_MEM_OBJECT, result);

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_sampler clCreateSamplerWithProperties(
    cl_context context, const cl_sampler_properties *sampler_properties,
    cl_int *errcode_ret) {
  return clCreateSamplerWithPropertiesIntelFPGA(context, sampler_properties,
                                                errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_sampler CL_API_CALL
clCreateSamplerIntelFPGA(cl_context context, cl_bool normalized_coords,
                         cl_addressing_mode addressing_mode,
                         cl_filter_mode filter_mode, cl_int *errcode_ret) {
  cl_sampler_properties sampler_properties[7];

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[1] = normalized_coords;
  sampler_properties[2] = CL_SAMPLER_ADDRESSING_MODE;
  sampler_properties[3] = addressing_mode;
  sampler_properties[4] = CL_SAMPLER_FILTER_MODE;
  sampler_properties[5] = filter_mode;
  sampler_properties[6] = 0;

  return clCreateSamplerWithPropertiesIntelFPGA(context, sampler_properties,
                                                errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_sampler CL_API_CALL
clCreateSampler(cl_context context, cl_bool normalized_coords,
                cl_addressing_mode addressing_mode, cl_filter_mode filter_mode,
                cl_int *errcode_ret) {
  return clCreateSamplerIntelFPGA(context, normalized_coords, addressing_mode,
                                  filter_mode, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainSamplerIntelFPGA(cl_sampler sampler) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_sampler_is_valid(sampler)) {
    return CL_INVALID_SAMPLER;
  }
  acl_retain(sampler);

#if 1
  acl_print_debug_msg("Retain sampler[%d] %p now %u\n", sampler->id, sampler,
                      acl_ref_count(sampler));
#endif

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainSampler(cl_sampler sampler) {
  return clRetainSamplerIntelFPGA(sampler);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseSamplerIntelFPGA(cl_sampler sampler) {
  std::scoped_lock lock{acl_mutex_wrapper};
  // In the double-free case, we'll error out here because the reference count
  // will be 0.
  if (!acl_sampler_is_valid(sampler)) {
    return CL_INVALID_SAMPLER;
  }

  acl_release(sampler);

  acl_print_debug_msg("Release sampler[%d] %p now %u\n", sampler->id, sampler,
                      acl_ref_count(sampler));

  if (!acl_is_retained(sampler)) {
    cl_context context = sampler->context;

    // Put the cl_sampler object back onto the free list.
    sampler->link = acl_platform.free_sampler_head;
    acl_platform.free_sampler_head = sampler->id;

    // Allow later check of double-free, and correct calls to
    // clReleaseSampler and clRetainSampler in all cases.
    sampler->context = 0;

    sampler->normalized_coords = 0xFFFFFFFF;
    sampler->addressing_mode = 0xFFFFFFFF;
    sampler->filter_mode = 0xFFFFFFFF;

    acl_untrack_object(sampler);

    clReleaseContext(context);
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseSampler(cl_sampler sampler) {
  return clReleaseSamplerIntelFPGA(sampler);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSamplerInfoIntelFPGA(
    cl_sampler sampler, cl_sampler_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  cl_context context;
  RESULT_INIT;

  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_sampler_is_valid(sampler)) {
    return CL_INVALID_SAMPLER;
  }

  context = sampler->context;

  switch (param_name) {
  case CL_SAMPLER_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(sampler));
    break;
  case CL_SAMPLER_CONTEXT:
    RESULT_PTR(sampler->context);
    break;
  case CL_SAMPLER_NORMALIZED_COORDS:
    RESULT_BOOL((cl_bool)((sampler->normalized_coords == CL_TRUE) ? CL_TRUE
                                                                  : CL_FALSE));
    break;
  case CL_SAMPLER_ADDRESSING_MODE:
    RESULT_UINT((unsigned int)sampler->addressing_mode);
    break;
  case CL_SAMPLER_FILTER_MODE:
    RESULT_UINT((unsigned int)sampler->filter_mode);
    break;
  default:
    break;
  }

  if (result.size == 0)
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid or unsupported sampler object query");

  if (param_value) {
    if (param_value_size < result.size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Parameter return buffer is too small");
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSamplerInfo(cl_sampler sampler,
                                                 cl_sampler_info param_name,
                                                 size_t param_value_size,
                                                 void *param_value,
                                                 size_t *param_value_size_ret) {
  return clGetSamplerInfoIntelFPGA(sampler, param_name, param_value_size,
                                   param_value, param_value_size_ret);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

//////////////////////////////
// Internals
int acl_sampler_is_valid(cl_sampler sampler) {
#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  if (!acl_sampler_is_valid_ptr(sampler)) {
    return 0;
  }
  if (!acl_is_retained(sampler)) {
    return 0;
  }
  if (!acl_context_is_valid(sampler->context)) {
    return 0;
  }
  return 1;
#endif
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

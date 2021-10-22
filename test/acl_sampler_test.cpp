// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#if ACL_SUPPORT_IMAGES == 1

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <acl.h>
#include <acl_types.h>
#include <acl_util.h>

#include "acl_test.h"

MT_TEST_GROUP(acl_sampler) {
  enum { MAX_DEVICES = 100 };
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    m_callback_count = 0;
    m_callback_errinfo = 0;
    this->load();
  }
  void teardown() {
    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 3);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;

public:
  cl_ulong m_callback_count;
  const char *m_callback_errinfo;
};

static void CL_CALLBACK notify_me(const char *errinfo, const void *private_info,
                                  size_t cb, void *user_data) {
  CppUTestGroupacl_sampler *inst = (CppUTestGroupacl_sampler *)user_data;
  if (inst) {
    inst->m_callback_count++;
    inst->m_callback_errinfo = errinfo;
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST(acl_sampler, basic) {
  cl_sampler sampler;
  cl_int status;
  cl_sampler_properties sampler_properties[7];
  cl_uint ref_count;
  cl_context test_context;
  cl_addressing_mode test_addressing_mode;
  cl_filter_mode test_filter_mode;
  cl_bool test_normalized_coord;

  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  // Bad contexts
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSampler(0, CL_TRUE, CL_ADDRESS_REPEAT, CL_FILTER_NEAREST,
                            &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);

  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  status = CL_SUCCESS;
  struct _cl_context fake_context = {0};
  sampler = clCreateSampler(&fake_context, CL_TRUE, CL_ADDRESS_REPEAT,
                            CL_FILTER_NEAREST, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Invalid value
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSampler(context, CL_TRUE, CL_ADDRESS_REPEAT + 6,
                            CL_FILTER_NEAREST, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSampler(context, CL_TRUE, CL_ADDRESS_REPEAT,
                            CL_FILTER_NEAREST + 2, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSampler(context, CL_TRUE + 2, CL_ADDRESS_REPEAT,
                            CL_FILTER_NEAREST, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS + 5;
  sampler_properties[1] = CL_TRUE;
  sampler_properties[2] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[1] = CL_TRUE + 3;
  sampler_properties[2] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[1] = CL_TRUE;
  sampler_properties[2] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[3] = CL_TRUE;
  sampler_properties[4] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK_EQUAL(0, sampler);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Invalid sampler
  CHECK_EQUAL(CL_INVALID_SAMPLER, clReleaseSampler(0));
  CHECK_EQUAL(CL_INVALID_SAMPLER, clReleaseSampler((cl_sampler)&status));

  CHECK_EQUAL(CL_INVALID_SAMPLER,
              clGetSamplerInfo(0, CL_SAMPLER_REFERENCE_COUNT, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_SAMPLER,
              clGetSamplerInfo((cl_sampler)&status, CL_SAMPLER_REFERENCE_COUNT,
                               0, 0, 0));

  // Good sampler
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSampler(context, CL_TRUE, CL_ADDRESS_REPEAT,
                            CL_FILTER_NEAREST, &status);
  CHECK(sampler != 0);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clReleaseSampler(sampler);
  CHECK_EQUAL(CL_SUCCESS, status);

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[1] = CL_TRUE;
  sampler_properties[2] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK(sampler != 0);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clReleaseSampler(sampler);
  CHECK_EQUAL(CL_SUCCESS, status);

  sampler_properties[0] = CL_SAMPLER_NORMALIZED_COORDS;
  sampler_properties[1] = CL_FALSE;
  sampler_properties[2] = CL_SAMPLER_ADDRESSING_MODE;
  sampler_properties[3] = CL_ADDRESS_REPEAT;
  sampler_properties[4] = CL_SAMPLER_FILTER_MODE;
  sampler_properties[5] = CL_FILTER_LINEAR;
  sampler_properties[6] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK(sampler != 0);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clReleaseSampler(sampler);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(6, this->m_callback_count);

  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // let all threads get different samplers before continuing
  syncThreads();

  CHECK_EQUAL(CL_INVALID_SAMPLER, clRetainSampler(0));
  CHECK_EQUAL(CL_INVALID_SAMPLER, clRetainSampler((cl_sampler)&status));
  CHECK_EQUAL(CL_SUCCESS, clRetainSampler(sampler));
  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                                           sizeof(cl_uint), &ref_count, NULL));
  CHECK_EQUAL(2, ref_count);
  CHECK_EQUAL(CL_SUCCESS, clRetainSampler(sampler));
  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                                           sizeof(cl_uint), &ref_count, NULL));
  CHECK_EQUAL(3, ref_count);

  CHECK_EQUAL(CL_SUCCESS,
              clGetSamplerInfo(sampler, CL_SAMPLER_CONTEXT, sizeof(cl_context),
                               &test_context, NULL));
  CHECK_EQUAL(context, test_context);

  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_ADDRESSING_MODE,
                                           sizeof(cl_addressing_mode),
                                           &test_addressing_mode, NULL));
  CHECK_EQUAL(CL_ADDRESS_REPEAT, test_addressing_mode);

  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_FILTER_MODE,
                                           sizeof(cl_filter_mode),
                                           &test_filter_mode, NULL));
  CHECK_EQUAL(CL_FILTER_LINEAR, test_filter_mode);

  CHECK_EQUAL(CL_SUCCESS,
              clGetSamplerInfo(sampler, CL_SAMPLER_NORMALIZED_COORDS,
                               sizeof(cl_bool), &test_normalized_coord, NULL));
  CHECK_EQUAL(CL_FALSE, test_normalized_coord);

  CHECK_EQUAL(CL_SUCCESS, clReleaseSampler(sampler));
  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                                           sizeof(cl_uint), &ref_count, NULL));
  CHECK_EQUAL(2, ref_count);

  CHECK_EQUAL(CL_SUCCESS, clReleaseSampler(sampler));
  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                                           sizeof(cl_uint), &ref_count, NULL));
  CHECK_EQUAL(1, ref_count);

  CHECK_EQUAL(CL_SUCCESS, clReleaseSampler(sampler));
  CHECK_EQUAL(CL_INVALID_SAMPLER,
              clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                               sizeof(cl_uint), &ref_count, NULL));

  // don't let any thread create a new sampler before we check
  // CL_INVALID_SAMPLER above using the old sampler pointer
  syncThreads();

  // Check that default values are used when nothing is provided
  sampler_properties[0] = 0;
  sampler = (cl_sampler)1;
  status = CL_SUCCESS;
  sampler = clCreateSamplerWithProperties(context, sampler_properties, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // let all threads get different samplers before continuing
  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_ADDRESSING_MODE,
                                           sizeof(cl_addressing_mode),
                                           &test_addressing_mode, NULL));
  CHECK_EQUAL(CL_ADDRESS_CLAMP, test_addressing_mode);

  CHECK_EQUAL(CL_SUCCESS, clGetSamplerInfo(sampler, CL_SAMPLER_FILTER_MODE,
                                           sizeof(cl_filter_mode),
                                           &test_filter_mode, NULL));
  CHECK_EQUAL(CL_FILTER_NEAREST, test_filter_mode);

  CHECK_EQUAL(CL_SUCCESS,
              clGetSamplerInfo(sampler, CL_SAMPLER_NORMALIZED_COORDS,
                               sizeof(cl_bool), &test_normalized_coord, NULL));
  CHECK_EQUAL(CL_TRUE, test_normalized_coord);

  CHECK_EQUAL(CL_SUCCESS, clReleaseSampler(sampler));
  CHECK_EQUAL(CL_INVALID_SAMPLER,
              clGetSamplerInfo(sampler, CL_SAMPLER_REFERENCE_COUNT,
                               sizeof(cl_uint), &ref_count, NULL));

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}
#endif

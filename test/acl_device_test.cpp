// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

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
#include <acl_context.h>
#include <acl_platform.h>
#include <acl_types.h>
#include <acl_util.h>

#include <inttypes.h>

#include "acl_hal_test.h"
#include "acl_test.h"

MT_TEST_GROUP(devices){void setup(){
    if (threadNum() == 0){ACL_LOCKED(acl_test_setup_generic_system());
}
syncThreads();
}
void teardown() {
  syncThreads();
  if (threadNum() == 0) {
    ACL_LOCKED(acl_test_teardown_generic_system());
  }
  acl_test_run_standard_teardown_checks();
}
}
;

MT_TEST(devices, basic) {
  const cl_uint max = 20;
  cl_platform_id platform;
  cl_uint num_platforms = 0;
  cl_device_id d[max];

  cl_uint num_devices = 0;
  cl_device_type type = CL_DEVICE_TYPE_ALL;

  // First get the platform
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, &num_platforms));
  CHECK(num_platforms > 0);

  // Check invalid platform
  CHECK_EQUAL(CL_INVALID_PLATFORM,
              clGetDeviceIDs(0, type, 1, &d[0], &num_devices));
  CHECK_EQUAL(CL_INVALID_PLATFORM,
              clGetDeviceIDs((cl_platform_id)((char *)platform + 1), type, 0,
                             &d[0], &num_devices));

  // Check invalid type.  It's a bit mask.
  CHECK_EQUAL(CL_INVALID_DEVICE_TYPE,
              clGetDeviceIDs(platform, 0, 1, &d[0], &num_devices));
  // see cl.h for definition of CL_DEVICE_TYPE_ALL as 32-bit 1's.
  CHECK_EQUAL(CL_INVALID_DEVICE_TYPE,
              clGetDeviceIDs(platform, (CL_DEVICE_TYPE_ALL >> 1), 1, &d[0],
                             &num_devices));

  // Invalid fill-in-array args
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetDeviceIDs(platform, type, 0, &d[0], &num_devices));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetDeviceIDs(platform, type, 1, 0, &num_devices));
  // Must request *something*
  CHECK_EQUAL(CL_INVALID_VALUE, clGetDeviceIDs(platform, type, 0, 0, 0));

  // Now check return values.
  for (size_t i = 0; i < max; i++) {
    d[i] = 0;
  }
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceIDs(platform, type, max, &d[0], &num_devices));
  CHECK(num_devices > 0);
  for (size_t i = 0; i < MIN(max, num_devices); i++) {
    CHECK(d[i] != 0);
    ACL_LOCKED(CHECK(acl_device_is_valid_ptr(d[i])));
    ACL_LOCKED(CHECK(
        acl_device_is_valid(d[i]))); // of course, it's valid, but not in use
  }
  for (size_t i = MIN(max, num_devices); i < max; i++) {
    CHECK(d[i] == 0);
    ACL_LOCKED(CHECK(!acl_device_is_valid_ptr(d[i])));
    ACL_LOCKED(CHECK(!acl_device_is_valid(d[i])));
  }

  // Check if the reconfig API is visible
  CHECK_EQUAL(
      CL_SUCCESS,
      clReconfigurePLLIntelFPGA(
          d[0],
          "some setting sting")); // On test hal, this will always succeed.
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clReconfigurePLLIntelFPGA(0, "some setting sting"));
  CHECK_EQUAL(CL_INVALID_VALUE, clReconfigurePLLIntelFPGA(d[0], NULL));
}

MT_TEST(devices, sub_devices) {
  cl_platform_id platform;
  cl_device_id device;
  cl_device_id invalid_device = (cl_device_id)(-1);
  cl_device_id out_sub_devices[2];
  cl_uint num_platforms = 0;
  cl_uint num_devices = 0;
  cl_uint num_sub_devices = 0;
  cl_device_partition_property partition_properties[] = {
      CL_DEVICE_PARTITION_EQUALLY, 8, 0};

  // First try to get sub-devices without a valid device. Should return
  // CL_INVALID_DEVICE:
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clCreateSubDevices(invalid_device, partition_properties, 2,
                                 &out_sub_devices[0], &num_sub_devices));

  // Next we try to get sub-devices with a valid device:
  // First get the platform
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, &num_platforms));
  CHECK(num_platforms > 0);

  // Now, get a device:
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1,
                                         &device, &num_devices));
  CHECK(num_devices > 0);

  // Now try to get sub-devices. Should return CL_INVALID_VALUE because getting
  // sub-devices is not supported:
  CHECK_EQUAL(CL_INVALID_VALUE,
              clCreateSubDevices(device, partition_properties, 2,
                                 &out_sub_devices[0], &num_sub_devices));

  // Try to retain invalid device:
  CHECK_EQUAL(CL_INVALID_DEVICE, clRetainDevice(invalid_device));

  // Try to release invalid device:
  CHECK_EQUAL(CL_INVALID_DEVICE, clReleaseDevice(invalid_device));

  // Try to retain root-level device:
  CHECK_EQUAL(CL_SUCCESS, clRetainDevice(device));

  // Try to release root-level device:
  CHECK_EQUAL(CL_SUCCESS, clReleaseDevice(device));
}

static void CL_CALLBACK exception_notify_fn(CL_EXCEPTION_TYPE_INTEL,
                                            const void *private_info, size_t cb,
                                            void *) {
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST(devices, set_device_callback) {
  int user_data1 = 1;
  int user_data2 = 2;
  int user_data3 = 3;
  cl_platform_id platform;
  cl_device_id device[4];
  cl_uint num_platforms = 0;
  cl_uint num_devices = 0;

  // Next we try to get sub-devices with a valid device:
  // First get the platform
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, &num_platforms));
  CHECK(num_platforms > 0);

  // Now, get devices:
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 4,
                                         &device[0], &num_devices));
  CHECK(num_devices > 0);

  // Set 2 callbacks for 2 devices
  clSetDeviceExceptionCallbackIntelFPGA(
      1, &device[0], CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL,
      &exception_notify_fn, &user_data1);
  clSetDeviceExceptionCallbackIntelFPGA(
      1, &device[1], CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL,
      &exception_notify_fn, &user_data2);

  CHECK(device[0]->exception_notify_user_data == &user_data1);
  CHECK(device[1]->exception_notify_user_data == &user_data2);

  CHECK(device[0]->listen_mask == CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL);
  CHECK(device[1]->listen_mask ==
        CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL);

  CHECK(device[0]->exception_notify_fn == &exception_notify_fn);
  CHECK(device[1]->exception_notify_fn == &exception_notify_fn);

  // Set 1 callback for 3 devices (resetting for the first 2)
  clSetDeviceExceptionCallbackIntelFPGA(
      3, &device[0], CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL,
      &exception_notify_fn, &user_data3);

  CHECK(device[0]->exception_notify_user_data == &user_data3);
  CHECK(device[2]->exception_notify_user_data == &user_data3);

  CHECK(device[0]->listen_mask ==
        CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL);
  CHECK(device[2]->listen_mask ==
        CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL);

  CHECK(device[0]->exception_notify_fn == &exception_notify_fn);
  CHECK(device[2]->exception_notify_fn == &exception_notify_fn);

  // Callback is not set for the last device
  CHECK(device[3]->exception_notify_fn == NULL);
}

MT_TEST_GROUP(DeviceInfo) {
public:
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    clGetPlatformIDs(1, &m_platform, 0);
  }
  void teardown() {
    m_platform = 0;

    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }

    acl_test_run_standard_teardown_checks();
  }

protected:
  cl_platform_id m_platform;
};

MT_TEST(DeviceInfo, basic) {
  char str[1024]; // must be big enough to hold biggest property
  const size_t str_size = sizeof(str) / sizeof(str[0]);
  size_t size_ret;
  cl_device_id device;
  cl_uint num_devices;
  cl_device_info queries[] = {
      CL_DEVICE_ADDRESS_BITS, CL_DEVICE_AVAILABLE, CL_DEVICE_COMPILER_AVAILABLE,
      CL_DEVICE_ENDIAN_LITTLE, CL_DEVICE_ERROR_CORRECTION_SUPPORT,
      CL_DEVICE_EXTENSIONS, CL_DEVICE_GLOBAL_MEM_CACHE_TYPE,
      CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE,
      CL_DEVICE_GLOBAL_MEM_SIZE, CL_DEVICE_IMAGE_SUPPORT,
      CL_DEVICE_LOCAL_MEM_TYPE, CL_DEVICE_LOCAL_MEM_SIZE,
      CL_DEVICE_MAX_COMPUTE_UNITS, CL_DEVICE_MAX_CONSTANT_ARGS,
      CL_DEVICE_MAX_MEM_ALLOC_SIZE, CL_DEVICE_MAX_PARAMETER_SIZE,
      CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, CL_DEVICE_MAX_WORK_GROUP_SIZE,
      CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, CL_DEVICE_MAX_WORK_ITEM_SIZES,
      CL_DEVICE_MEM_BASE_ADDR_ALIGN, CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE,
      CL_DEVICE_NAME, CL_DEVICE_PLATFORM,

      CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR,
      CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT,
      CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT,
      CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG,
      CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT,
      CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE,
      CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR, CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT,
      CL_DEVICE_NATIVE_VECTOR_WIDTH_INT, CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG,
      CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT, CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE,

      CL_DEVICE_PROFILE, CL_DEVICE_PROFILING_TIMER_RESOLUTION,
      CL_DEVICE_QUEUE_PROPERTIES, CL_DEVICE_SINGLE_FP_CONFIG, CL_DEVICE_TYPE,
      CL_DEVICE_VENDOR_ID, CL_DEVICE_VERSION, CL_DRIVER_VERSION,

      // In 1.1
      CL_DEVICE_HOST_UNIFIED_MEMORY, CL_DEVICE_OPENCL_C_VERSION,

      // In cl_altera_device_temperature vendor extension
      CL_DEVICE_CORE_TEMPERATURE_INTELFPGA,

      // In 1.2, 2.0
      CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS, CL_DEVICE_IMAGE_MAX_BUFFER_SIZE,
      CL_DEVICE_IMAGE_PITCH_ALIGNMENT, CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT,
      CL_DEVICE_LINKER_AVAILABLE, CL_DEVICE_QUEUE_ON_HOST_PROPERTIES,
      CL_DEVICE_BUILT_IN_KERNELS, CL_DEVICE_PREFERRED_INTEROP_USER_SYNC,
      CL_DEVICE_PARENT_DEVICE, CL_DEVICE_PARTITION_MAX_SUB_DEVICES,
      CL_DEVICE_PARTITION_PROPERTIES, CL_DEVICE_PARTITION_AFFINITY_DOMAIN,
      CL_DEVICE_PARTITION_TYPE, CL_DEVICE_REFERENCE_COUNT,
      CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT,
      CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT,
      CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT};

  cl_device_info unsupported_queries[] = {
      0,
  };

  // Get a sample device.
  ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, 1,
                                         &device, &num_devices));
  CHECK(num_devices > 0);
  CHECK(device != 0);
  ACL_LOCKED(CHECK(acl_device_is_valid_ptr(device)));
  ACL_LOCKED(CHECK(
      acl_device_is_valid(device))); // valid but not yet used in a context

  // Check invalid device arg
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clGetDeviceInfo(0, queries[0], 1, &str[0], &size_ret));
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clGetDeviceInfo((cl_device_id)&str[0], queries[0], 1, &str[0],
                              &size_ret));

  // Good cases:
  // Ensure we can query all info enums, and in various combinations of
  // returning the values.
  for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceInfo(device, queries[i], 0, 0, &size_ret));
    CHECK(size_ret < str_size); // If this fails, just make str[] bigger.
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceInfo(device, queries[i], str_size, &str[0], 0));
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetDeviceInfo(device, queries[i], str_size,
                                            &str[0], &size_ret));

    CHECK(size_ret > 0);

    if (queries[i] == CL_DEVICE_EXTENSIONS) {
      CHECK(strcmp(str, acl_platform_extensions()) == 0);
    }

    switch (queries[i]) {
    case CL_DEVICE_VENDOR:
    case CL_DEVICE_VERSION:
    case CL_DRIVER_VERSION:
      str[size_ret] = 0;
      ACL_LOCKED(acl_print_debug_msg("%4x %s\n", queries[i], str));
      break;
    default:
      break;
    }

    // Check sanity of queue properties
    if (queries[i] == CL_DEVICE_QUEUE_PROPERTIES) {
      CHECK_EQUAL(sizeof(cl_bitfield), size_ret);
      cl_bitfield *ptr = (cl_bitfield *)&str[0];
      cl_bitfield value = *ptr;
      ACL_LOCKED(acl_print_debug_msg("queue prop %8" PRIx64 "\n", value));
      CHECK_EQUAL(CL_QUEUE_PROFILING_ENABLE |
                      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
                  value);
    }
    // Check sanity of fp config
    if (queries[i] == CL_DEVICE_SINGLE_FP_CONFIG) {
      CHECK_EQUAL(sizeof(cl_bitfield), size_ret);
      cl_bitfield *ptr = (cl_bitfield *)&str[0];
      cl_bitfield value = *ptr;
      ACL_LOCKED(acl_print_debug_msg("fp config %8" PRIx64 "\n", value));
      CHECK_EQUAL((CL_FP_INF_NAN | CL_FP_ROUND_TO_NEAREST), value);
    }
    // Check sanity of device avail
    if (queries[i] == CL_DEVICE_AVAILABLE) {
      CHECK_EQUAL(sizeof(cl_bool), size_ret);
      cl_bool *ptr = (cl_bool *)&str[0];
      cl_bool value = *ptr;
      ACL_LOCKED(acl_print_debug_msg("avail %s\n", (value ? "true" : "false")));
      CHECK_EQUAL(true, (value ? true : false));
    }
    // Must return false for these to queries to be conformant with our
    // implementation of clCompileProgram and clLinkProgram.
    if (queries[i] == CL_DEVICE_COMPILER_AVAILABLE ||
        queries[i] == CL_DEVICE_LINKER_AVAILABLE) {
      CHECK_EQUAL(sizeof(cl_bool), size_ret);
      cl_bool *ptr = (cl_bool *)&str[0];
      cl_bool value = *ptr;
      ACL_LOCKED(acl_print_debug_msg("compiler/linker avail %s\n",
                                     (value ? "true" : "false")));
      CHECK_EQUAL(false, (value ? true : false));
    }

    if (queries[i] == CL_DEVICE_BUILT_IN_KERNELS) {
      cl_device_id devices[5];
      CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, 5,
                                             devices, NULL));

      char builtin_kernels[1024];
      // testing device with no kernels
      CHECK_EQUAL(CL_SUCCESS,
                  clGetDeviceInfo(devices[4], CL_DEVICE_BUILT_IN_KERNELS,
                                  sizeof(builtin_kernels), builtin_kernels,
                                  NULL));
      CHECK(builtin_kernels[0] == '\0');
    }
  }

  // Check sanity of max work group size vs. max work item sizes.
  size_t max_work_group_size, max_work_item_sizes[3];
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                                          sizeof(max_work_group_size),
                                          &max_work_group_size, 0));
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_SIZES,
                                          sizeof(max_work_item_sizes),
                                          &max_work_item_sizes[0], 0));
  // This is how we set it for now.
  CHECK_EQUAL(max_work_group_size, size_t((1lu << 31) - 1));
  CHECK_EQUAL(max_work_group_size, max_work_item_sizes[0]);
  CHECK_EQUAL(max_work_group_size, max_work_item_sizes[1]);
  CHECK_EQUAL(max_work_group_size, max_work_item_sizes[2]);

  // Bad query cases
  for (size_t i = 0;
       i < sizeof(unsupported_queries) / sizeof(unsupported_queries[0]); i++) {
    CHECK_EQUAL(
        CL_INVALID_VALUE,
        clGetDeviceInfo(device, unsupported_queries[i], 0, 0, &size_ret));
    size_ret = 0;
    CHECK_EQUAL(CL_INVALID_VALUE,
                clGetDeviceInfo(device, unsupported_queries[i], str_size,
                                &str[0], &size_ret));

    CHECK_EQUAL(0, size_ret);
  }

  // Check retvalue args
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetDeviceInfo(device, queries[0], 0, &str[0], &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetDeviceInfo(device, queries[0], 1, 0, &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetDeviceInfo(device, queries[0], 1, &str[0],
                              &size_ret)); // too short to hold result.

  // That constraint is oddly missing from the API spec, even though other
  // query APIs have it.
  CHECK_EQUAL(CL_INVALID_VALUE, clGetDeviceInfo(device, queries[0], 0, 0, 0));

  // Check SVM Capabilities. Set the device to support different combinations
  // and make sure that the expected types are returned by clGetDeviceInfo
  int svm_capabilities_returned, svm_capabilities_expected;

  syncThreads();
  if (threadNum() == 0) {
    acl_test_hal_set_physical_memory_support(true);
    acl_test_hal_set_svm_memory_support(0);
  }
  syncThreads();
  svm_capabilities_expected = 0;
  clGetDeviceInfo(device, CL_DEVICE_SVM_CAPABILITIES,
                  sizeof(svm_capabilities_returned), &svm_capabilities_returned,
                  0);
  CHECK_EQUAL(svm_capabilities_expected, svm_capabilities_returned);

  syncThreads();
  if (threadNum() == 0) {
    acl_test_hal_set_physical_memory_support(true);
    acl_test_hal_set_svm_memory_support((int)CL_DEVICE_SVM_COARSE_GRAIN_BUFFER);
  }
  syncThreads();
  svm_capabilities_expected = CL_DEVICE_SVM_COARSE_GRAIN_BUFFER;
  clGetDeviceInfo(device, CL_DEVICE_SVM_CAPABILITIES,
                  sizeof(svm_capabilities_returned), &svm_capabilities_returned,
                  0);
  CHECK_EQUAL(svm_capabilities_expected, svm_capabilities_returned);

  syncThreads();
  if (threadNum() == 0) {
    acl_test_hal_set_physical_memory_support(true);
    acl_test_hal_set_svm_memory_support(
        (int)(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
              CL_DEVICE_SVM_FINE_GRAIN_BUFFER));
  }
  syncThreads();
  svm_capabilities_expected =
      (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER | CL_DEVICE_SVM_FINE_GRAIN_BUFFER);
  clGetDeviceInfo(device, CL_DEVICE_SVM_CAPABILITIES,
                  sizeof(svm_capabilities_returned), &svm_capabilities_returned,
                  0);
  CHECK_EQUAL(svm_capabilities_expected, svm_capabilities_returned);

  syncThreads();
  if (threadNum() == 0) {
    acl_test_hal_set_physical_memory_support(true);
    acl_test_hal_set_svm_memory_support(
        (int)(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
              CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
              CL_DEVICE_SVM_FINE_GRAIN_SYSTEM));
  }
  syncThreads();
  svm_capabilities_expected =
      (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER | CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
       CL_DEVICE_SVM_FINE_GRAIN_SYSTEM);
  clGetDeviceInfo(device, CL_DEVICE_SVM_CAPABILITIES,
                  sizeof(svm_capabilities_returned), &svm_capabilities_returned,
                  0);
  CHECK_EQUAL(svm_capabilities_expected, svm_capabilities_returned);
}

MT_TEST(DeviceInfo, shipped_devices) {
  enum { N = 100 };
  cl_device_id device[N];
  cl_platform_id platform;
  cl_uint num_devices;

  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, 0));
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(platform, CL_DEVICE_TYPE_ACCELERATOR,
                                         N, device, &num_devices));

  // Check for presence
  for (unsigned i = 0; i < num_devices; i++) {
    char name[100];
    name[0] = '\0';
    CHECK_EQUAL(CL_SUCCESS, clGetDeviceInfo(device[i], CL_DEVICE_NAME,
                                            sizeof(name), name, 0));
    ACL_LOCKED(acl_print_debug_msg(" got name %-20s present %d \n", name,
                                   device[i]->present));

    CHECK(strlen(name) > 0);
  }
}

// Test for clGetDeviceInfo(CL_DEVICE_AVAILABLE)
MT_TEST(DeviceInfo, device_available) {
  cl_device_id device;
  cl_uint num_devices = 0;
  cl_bool is_available;
  cl_context context[2];
  cl_int status;

  // Now, get device.
  CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, 1,
                                         &device, &num_devices));
  CHECK(num_devices > 0);

  /*
   * Test for CL_DEVICE_AVAILABLE = true
   */

  // Nothing was done to device, and it's a valid device, so it should be
  // available
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceInfo(device, CL_DEVICE_AVAILABLE, sizeof(is_available),
                              &is_available, NULL));
  CHECK_EQUAL(CL_TRUE, is_available);

  // In the single-process scenario, even after a context was created, the
  // device is still available in the viewpoint of that process
  context[0] = clCreateContext(0, 1, &device, NULL, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceInfo(device, CL_DEVICE_AVAILABLE, sizeof(is_available),
                              &is_available, NULL));
  CHECK_EQUAL(CL_TRUE, is_available);

  // Doesn't matter how many contexts are created, device is still available
  context[1] = clCreateContext(0, 1, &device, NULL, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceInfo(device, CL_DEVICE_AVAILABLE, sizeof(is_available),
                              &is_available, NULL));
  CHECK_EQUAL(CL_TRUE, is_available);

  // Release the contexts (twice). device is available
  clReleaseContext(context[0]);
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceInfo(device, CL_DEVICE_AVAILABLE, sizeof(is_available),
                              &is_available, NULL));
  CHECK_EQUAL(CL_TRUE, is_available);

  clReleaseContext(context[1]);
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceInfo(device, CL_DEVICE_AVAILABLE, sizeof(is_available),
                              &is_available, NULL));
  CHECK_EQUAL(CL_TRUE, is_available);

  /*
   * There're 2 scenarios when CL_DEVICE_AVAILABLE = false:
   *   1. opened_count == 0, and the clCreateContext inside clGetDeviceInfo
   * fails. This is hard to replicate here because I can't force the
   * clCreateContext() to fail
   *   2. On windows HW machine, device list always have 128 devices regardless
   * of availability. Querying one of these unavailable devices should give
   * CL_DEVICE_AVAILABLE = false This is hard to replicate here because most
   * machines don't have aboard and so don't have the "128 situation" above So I
   * can't test for CL_DEVICE_AVAILABLE = false
   */
}

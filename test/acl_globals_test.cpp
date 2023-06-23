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

#include <vector>

#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_support.h>
#include <acl_thread.h>

#include "acl_globals_test.h"
#include "acl_hal_test.h"
#include "acl_test.h"

// Worst case alignment

// Make these double to ensure alignment.
static ACL_ALIGNED double acltest_global[1024 * 1024 * 4];

static ACL_ALIGNED double
    acltest_devicelocal[14][16384 / sizeof(double)]; // min permitted local mem
                                                     // size is 16K

// Assumes device only has 32-bit addresses
#define LOCAL_PTR_SIZE_IN_CRA (4)

static std::vector<acl_local_aspace_info> acltest_laspace_info = {{4, 2048},
                                                                  {5, 16768}};

static acl_kernel_interface_t acltest_kernels[] = {
    {// interface
     "kernel0_copy_vecin_vecout",
     {{ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_NONE, ACL_ARG_BY_VALUE, sizeof(cl_uint), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_LOCAL, ACL_ARG_BY_VALUE, LOCAL_PTR_SIZE_IN_CRA, 5, 8192},
      {ACL_ARG_ADDR_CONSTANT, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0}}},
    {// interface
     "kernel1_vecadd_vecin_vecin_vecout",
     {{ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0}}},
    {// interface
     "kernel2_vecscale_vecin_scalar_vecout",
     {{ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_NONE, ACL_ARG_BY_VALUE, sizeof(float), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0}}},
    {// interface
     "kernel3_locals",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
         {ACL_ARG_ADDR_LOCAL, ACL_ARG_BY_VALUE, LOCAL_PTR_SIZE_IN_CRA, 4, 1024},
         {ACL_ARG_ADDR_LOCAL, ACL_ARG_BY_VALUE, LOCAL_PTR_SIZE_IN_CRA, 4, 2048},
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
     }},
    {// interface
     "kernel4_task_double",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
     }},
    {// interface
     "kernel5_double",
     {
         {ACL_ARG_ADDR_NONE, ACL_ARG_BY_VALUE, 4, 0, 0},
     }},
    {// interface
     "kernel6_profiletest",
     {{ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0}}},
    {// interface
     "kernel7_emptyprofiletest",
     {{ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
      {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0}}},
    {// interface
     "kernel8_svm_args",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0},
     }},
    {// interface
     "kernel9_image_args",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
         {ACL_ARG_ADDR_NONE, ACL_ARG_SAMPLER, sizeof(int), 0, 0},
     }},
    {// interface
     "kernel11_task_double",
     {
         {ACL_ARG_ADDR_NONE, ACL_ARG_BY_VALUE, 4, 0, 0},
     }},
    {// interface
     "kernel12_task_double",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
     }},
    {// interface
     "kernel13_multi_vec_lane",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(cl_mem), 0, 0},
     }},
    {// interface
     "kernel14_svm_arg_alignment",
     {
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0, 1},
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0, 4},
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0, 8},
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0, 16},
         {ACL_ARG_ADDR_GLOBAL, ACL_ARG_MEM_OBJ, sizeof(int *), 0, 0, 1024},
     }}};

template <typename T, std::size_t N>
static inline constexpr acl_addr_range_t ACL_RANGE_FROM_ARRAY(T (&a)[N]) {
  return {reinterpret_cast<void *>(&a[0]),
          reinterpret_cast<void *>(
              (reinterpret_cast<char *>(&a[0]) + N * sizeof(T)))};
}

static acl_system_def_t acltest_empty_system = {
    // Device definitions.
    0,
};

// accel for acltest_simple_system -> fpga0
static std::vector<acl_accel_def_t> acltest_simple_system_accel = {
    {0,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[0]),
     acltest_kernels[0],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     /*is_workitem_invariant*/ 0,
     /* num_vector_lanes */ 0,
     /*num_profiling_counters*/ 149,
     32768}};

static acl_system_def_t acltest_simple_system = {

    // Device definitions.
    1,
    {{nullptr,
      0,
      1,
      1,
      1, /* half duplex memory transfers */
      0,
      0,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {"fpga0",
       "sample40byterandomhash000000000000000000",
       0,
       acltest_simple_system_accel, /* accel */
       {},                          /* hal_info */
       1,                           // number of global memory systems
       {
           /* global mem info array */
           {
               /* global mem info for memory 0 */
               /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
               /* acl_system_global_mem_type_t */ ACL_GLOBAL_MEM_DEVICE_PRIVATE,
               /* num_global_bank */ 2,
               /* burst_interleaved */ 1,
           },
       }}}}};

// accel definition for acltest_complex_system->fpga0
static std::vector<acl_accel_def_t> acltest_complex_system_device0_accel = {
    {0,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[0]),
     acltest_kernels[0],
     acltest_laspace_info,
     {4, 2, 4},
     0,
     0,
     4,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {1,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[1]),
     acltest_kernels[1],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {2,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[2]),
     acltest_kernels[2],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {3,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[11]),
     acltest_kernels[4],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {4,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[12]),
     acltest_kernels[3],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test ptr-to-local
    ,
    {5,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[13]),
     acltest_kernels[5],
     acltest_laspace_info,
     {0, 0, 0},
     1,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test invariant_workgroup,
    ,
    {6,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[7]),
     acltest_kernels[6],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     149,
     32768,
     2,
     {},
     {32768, 0, 0},
     1} // profiler testing
    ,
    {7,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[4]),
     acltest_kernels[7],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     0,
     {},
     {32768, 0, 0},
     1},
    {8,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[3]),
     acltest_kernels[8],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     1,
     {},
     {32768, 0, 0},
     1},
    {9,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[5]),
     acltest_kernels[9],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {10,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[6]),
     acltest_kernels[10],
     acltest_laspace_info,
     {0, 0, 0},
     1,
     1,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test invariant_workitem,
    ,
    {11,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[11]),
     acltest_kernels[11],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1,
     1} // a task that can be fast relaunched
    ,
    {12,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[12]),
     acltest_kernels[13],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     0,
     0} // a task that can be fast relaunched
    ,
    {13,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[11]),
     acltest_kernels[12],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     3,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test wrong vector lane size
};

// accel definition for acltest_complex_system->fpga1
static std::vector<acl_accel_def_t> acltest_complex_system_device1_accel = {
    {0,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[3]),
     acltest_kernels[0],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {1,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[4]),
     acltest_kernels[2],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {2,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[12]),
     acltest_kernels[3],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test ptr-to-local
};

// accel definition for acltest_complex_system->fpga2
static std::vector<acl_accel_def_t> acltest_complex_system_device2_accel = {
    {0,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[5]),
     acltest_kernels[0],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {1,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[6]),
     acltest_kernels[4],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {2,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[7]),
     acltest_kernels[2],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {3,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[12]),
     acltest_kernels[3],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test ptr-to-local
};

// accel definition for acltest_complex_system->fpga3
static std::vector<acl_accel_def_t> acltest_complex_system_device3_accel = {
    {0,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[9]),
     acltest_kernels[4],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {1,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[10]),
     acltest_kernels[2],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {2,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[8]),
     acltest_kernels[0],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1},
    {3,
     ACL_RANGE_FROM_ARRAY(acltest_devicelocal[12]),
     acltest_kernels[3],
     acltest_laspace_info,
     {0, 0, 0},
     0,
     0,
     1,
     0,
     32768,
     3,
     {},
     {32768, 0, 0},
     1} // test ptr-to-local
};

static acl_system_def_t acltest_complex_system = {

    /////////// Device definitions
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /* num_devices */ 5,
    {// All of these have kernels 0 and 2 in common.  This is used for
     // testing clCreateKernels
     {nullptr,
      0,
      1,
      1,
      1, /* half duplex memory transfers */
      1,
      1,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {
          "fpga0",
          "sample40byterandomhash000000000000000000",
          0,
          acltest_complex_system_device0_accel, /* accel */
          {},                                   /* hal_info */
          1, // number of global memory systems
          {
              /* global mem info array */
              {
                  /* global mem info for memory 0 */
                  /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
                  /* acl_system_global_mem_type_t */
                  ACL_GLOBAL_MEM_DEVICE_PRIVATE,
                  /* num_global_bank */ 2,
                  /* burst_interleaved */ 1,
              },
          },
          {}, // hostpipe info
          {
              // device_global_mem_defs map
              {"dev_global_name",
               {0x1024, 2048, ACL_DEVICE_GLOBAL_HOST_ACCESS_TYPE_COUNT, 0, 0,
                0}},
          },
      }},
     {nullptr,
      1,
      1,
      1,
      2, /* full duplex memory transfers */
      0,
      0,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {
          "fpga1",
          "sample40byterandomhash000000000000000001",
          0,
          acltest_complex_system_device1_accel, /* accel */
          {},                                   /* hal_info */
          1, // number of global memory systems
          {
              /* global mem info array */
              {
                  /* global mem info for memory 0 */
                  /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
                  /* acl_system_global_mem_type_t */
                  ACL_GLOBAL_MEM_DEVICE_PRIVATE,
                  /* num_global_bank */ 2,
                  /* burst_interleaved */ 1,
              },
          },
          {}, // hostpipe info
          {
              // device_global_mem_defs map
              {"dev_global_name",
               {0x1024, 2048, ACL_DEVICE_GLOBAL_HOST_ACCESS_TYPE_COUNT, 0, 0,
                0}},
          },
      }},
     {nullptr,
      2,
      1,
      1,
      2, /* full duplex memory transfers */
      0,
      0,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {"fpga2",
       "sample40byterandomhash000000000000000002",
       0,
       acltest_complex_system_device2_accel, /* accel */
       {},                                   /* hal_info */
       1,                                    // number of global memory systems
       {
           /* global mem info array */
           {
               /* global mem info for memory 0 */
               /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
               /* acl_system_global_mem_type_t */ ACL_GLOBAL_MEM_DEVICE_PRIVATE,
               /* num_global_bank */ 2,
               /* burst_interleaved */ 1,
           },
       }}},
     {nullptr,
      3,
      1,
      1,
      1, /* half duplex memory transfers */
      0,
      0,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {"fpga3",
       "sample40byterandomhash000000000000000003",
       0,
       acltest_complex_system_device3_accel, /* accel */
       {},                                   /* hal_info */
       1,                                    // number of global memory systems
       {
           /* global mem info array */
           {
               /* global mem info for memory 0 */
               /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
               /* acl_system_global_mem_type_t */ ACL_GLOBAL_MEM_DEVICE_PRIVATE,
               /* num_global_bank */ 2,
               /* burst_interleaved */ 1,
           },
       }}},
     {nullptr,
      4,
      1,
      1,
      1, /* half duplex memory transfers */
      0,
      0,
      0, /* alloc capabilities */
      0, /* min_host_mem_alignment */
      {"fpga4",
       "sample40byterandomhash000000000000000004",
       0,
       {}, /* accel */
       {}, /* hal_info */
       1,  // number of global memory systems
       {
           /* global mem info array */
           {
               /* global mem info for memory 0 */
               /* global mem */ ACL_RANGE_FROM_ARRAY(acltest_global),
               /* acl_system_global_mem_type_t */ ACL_GLOBAL_MEM_DEVICE_PRIVATE,
               /* num_global_bank */ 2,
               /* burst_interleaved */ 1,
           },
       }}

     }}};

// For use by other tests
const acl_system_def_t *acl_test_get_complex_system_def() {
  return &acltest_complex_system;
}

const acl_system_def_t *acl_test_get_empty_system_def() {
  return &acltest_empty_system;
}

TEST_GROUP(acl_globals_undef){void setup(){acl_mutex_wrapper.lock();
CHECK(acl_set_hal(acl_test_get_simple_hal()));
}
void teardown() {
  acl_reset_hal();
  acl_mutex_wrapper.unlock();
  acl_test_run_standard_teardown_checks();
}

void misalign_ptr(void **ptr) { *ptr = ((char *)*ptr) + 1; }
}
;

TEST(acl_globals_undef, zero_when_unint) {
  CHECK(0 == acl_present_board_def());
  CHECK(0 == acl_present_board_is_valid());
}

TEST(acl_globals_undef, valid_init_simple) {
  CHECK(1 == acl_init(&acltest_simple_system));
  CHECK(0 != acl_present_board_def());
  CHECK(0 != acl_present_board_is_valid());
  // Teardown
  acl_reset();
  CHECK(0 == acl_present_board_def());
  CHECK(0 == acl_present_board_is_valid());
}

TEST(acl_globals_undef, valid_init_empty) {
  CHECK(1 == acl_init(&acltest_empty_system));
  CHECK(0 != acl_present_board_def());
  CHECK(0 != acl_present_board_is_valid());
  // Teardown
  acl_reset();
  CHECK(0 == acl_present_board_def());
  CHECK(0 == acl_present_board_is_valid());
}

TEST(acl_globals_undef, valid_init_complex) {
  CHECK_EQUAL(1, acl_init(&acltest_complex_system));
  CHECK(0 != acl_present_board_def());
  // Teardown
  acl_reset();
  CHECK_EQUAL(0, acl_present_board_def());
}

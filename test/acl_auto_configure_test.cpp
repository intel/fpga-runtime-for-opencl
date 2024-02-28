// Copyright (C) 2011-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <iostream>
#include <stdlib.h>
#include <string>

#include <acl_auto.h>
#include <acl_auto_configure.h>
#include <acl_auto_configure_version.h>
#include <acl_thread.h>
#include <acl_version.h>

#include "acl_test.h"

TEST_GROUP(auto_configure) {
public:
  void setup() {}
  void teardown() { acl_test_run_standard_teardown_checks(); }

protected:
  acl_device_def_t m_device_def;
};

TEST(auto_configure, simple) {

#define VERSIONIDSTRINGIFY(x) #x
#define VERSIONIDTOSTR(x) VERSIONIDSTRINGIFY(x)
#define DEVICE_FIELDS " 23"
#define DEVICE_FIELDS_DEV_GLOBAL " 38"
#define DEVICE_FIELDS_OLD " 18"
#define BOARDNAME "de4_gen2x4_swdimm"
#define BOARDNAME2 "pcie385_a7"
#define RANDOM_HASH " sample40byterandomhash000000000000000000"
#define IS_BIG_ENDIAN " 1"
#define IS_NOT_BIG_ENDIAN " 0"
#define MEM_BACKWARDS_COMP " 1 6 DDR 2 1 2 0 2048"
#define MEM " 1 10 DDR 2 1 2 0 2048 0 - 0 0"
#define HOSTPIPE " 1 5 pipe_name 1 0 32 32768"
#define KERNEL_ARG_INFO_NONE " 0"
#define ARG_INT " 6 0 0 4 1 0 0"
#define ARG_LONG " 6 0 0 8 1 0 0"
#define ARG_LOCAL " 8 1 1 4 1024 0 5 16768 0"
#define ARG_GLOBAL " 6 2 1 4 1024 0 0"
#define ARG_CONST " 6 3 1 4 1024 0 0"
#define ARG_PROF ARG_GLOBAL
#define ARGS_LOCAL_GLOBAL_INT " 3" ARG_LOCAL ARG_GLOBAL ARG_INT
  // the last four args are for printf(2) and profiling(2)
#define ARGS_LOCAL_GLOBAL_LONG_PROF                                            \
  " 7" ARG_LOCAL ARG_GLOBAL ARG_LONG ARG_GLOBAL ARG_INT ARG_PROF ARG_PROF

#define KERNEL_FIELDS " 72"
#define KERNEL_CRA " 64 128" // Address(offset) and number of bytes
#define KERNEL_FAST_LAUNCH_DEPTH " 0"
#define KERNEL_PERF_MON " 192 256" // Address(offset) and number of bytes
#define KERNEL_WORKGROUP_INVARIANT " 1"
#define KERNEL_WORKITEM_INVARIANT " 1"
#define KERNEL_WORKGROUP_VARIANT " 0"
#define KERNEL_WORKITEM_VARIANT " 0"
#define KERNEL_NUM_VECTOR_LANES1 " 1"
#define KERNEL_NUM_VECTOR_LANES2 " 2"

#define KERNEL_PRINTF_FORMAT1 " 0 d"
#define KERNEL_PRINTF_FORMAT2 " 1 d"
#define KERNEL_PRINTF_FORMATSTRINGS                                            \
  " 2 2" KERNEL_PRINTF_FORMAT1 KERNEL_PRINTF_FORMAT2
#define KERNEL_PROFILE_SCANCHAIN_LENGTH " 5"

// statically determined demand for local memory
#define LD_1024 " 2 2 5 1024 6 2048"
#define LD_0 " 0 0"

// kernel attribute reqd_work_group_size(x,y,z). 0,0,0 means not specified.
#define KERNEL_REQD_WORK_GROUP_SIZE_NONE " 0 0 0"
#define KERNEL_REQD_WORK_GROUP_SIZE_235 " 2 3 5"

// kernel attribute max_work_group_size(x). 0 means not specified.
#define KERNEL_MAX_WORK_GROUP_SIZE_NONE " 1 0"
#define KERNEL_MAX_WORK_GROUP_SIZE_1024 " 3 32 8 4"

// kernel attribute max_global_work_dim(n). 3 means not specified (or 3).
#define KERNEL_MAX_GLOBAL_WORK_DIM_NONE " 3"
#define KERNEL_MAX_GLOBAL_WORK_DIM_ZERO " 0"
#define KERNEL_USES_GLOBAL_WORK_OFFSET_ENABLED " 1"
#define KERNEL_USES_GLOBAL_WORK_OFFSET_DISABLED " 0"

// sycl compile
#define IS_SYCL_COMPILE " 1"
#define IS_NOT_SYCL_COMPILE " 0"

// Device global autodiscovery entries
#define NUM_DEV_GLOBAL " 2"
#define NUM_DEV_GLOBAL_FIELD " 7"
// The 7 fields are dev_globa_name, address, size, host_access,
//  can_skip_programming, implement_in_csr, reset_on_reuse
#define DEV_GLOBAL_1 " kernel15_dev_global 0x1000 2048 3 0 0 0"
#define DEV_GLOBAL_2 " kernel15_dev_global2 0x800 1024 1 0 1 0"

  int parsed;
  std::string err_str;
  std::string autodiscovery = std::string(
      VERSIONIDTOSTR(ACL_AUTO_CONFIGURE_VERSIONID)
          DEVICE_FIELDS_DEV_GLOBAL RANDOM_HASH
      " " BOARDNAME IS_NOT_BIG_ENDIAN MEM HOSTPIPE KERNEL_ARG_INFO_NONE
          NUM_DEV_GLOBAL NUM_DEV_GLOBAL_FIELD DEV_GLOBAL_1 DEV_GLOBAL_2
      " 1 82 foo" KERNEL_CRA KERNEL_FAST_LAUNCH_DEPTH KERNEL_PERF_MON
          KERNEL_WORKGROUP_VARIANT KERNEL_WORKITEM_VARIANT
              KERNEL_NUM_VECTOR_LANES1 KERNEL_PROFILE_SCANCHAIN_LENGTH
                  ARGS_LOCAL_GLOBAL_LONG_PROF KERNEL_PRINTF_FORMATSTRINGS
                      LD_1024 KERNEL_REQD_WORK_GROUP_SIZE_NONE
                          KERNEL_MAX_WORK_GROUP_SIZE_NONE
                              KERNEL_MAX_GLOBAL_WORK_DIM_NONE
                                  KERNEL_USES_GLOBAL_WORK_OFFSET_ENABLED
                                      IS_SYCL_COMPILE);
  ACL_LOCKED(parsed = acl_load_device_def_from_str(
                 autodiscovery, m_device_def.autodiscovery_def, err_str));
  CHECK_EQUAL(1, parsed);

  CHECK_EQUAL(1, m_device_def.autodiscovery_def.num_global_mem_systems);
  CHECK_EQUAL(0, m_device_def.autodiscovery_def.global_mem_defs[0].range.begin);
  CHECK_EQUAL((void *)2048,
              m_device_def.autodiscovery_def.global_mem_defs[0].range.next);
  CHECK_EQUAL(
      (acl_system_global_mem_allocation_type_t)0,
      m_device_def.autodiscovery_def.global_mem_defs[0].allocation_type);
  CHECK("" ==
        m_device_def.autodiscovery_def.global_mem_defs[0].primary_interface);
  CHECK_EQUAL(
      0,
      m_device_def.autodiscovery_def.global_mem_defs[0].can_access_list.size());

  CHECK(BOARDNAME == m_device_def.autodiscovery_def.name);

  CHECK_EQUAL(0, (int)m_device_def.autodiscovery_def.is_big_endian);

  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.accel.size());
  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.hal_info.size());

  // Check HAL's view
  CHECK("foo" == m_device_def.autodiscovery_def.hal_info[0].name);
  CHECK_EQUAL(64, (int)m_device_def.autodiscovery_def.hal_info[0].csr.address);
  CHECK_EQUAL(128,
              (int)m_device_def.autodiscovery_def.hal_info[0].csr.num_bytes);
  CHECK_EQUAL(192,
              (int)m_device_def.autodiscovery_def.hal_info[0].perf_mon.address);
  CHECK_EQUAL(
      256, (int)m_device_def.autodiscovery_def.hal_info[0].perf_mon.num_bytes);

  // Check hostpipe info
  CHECK_EQUAL(1, m_device_def.autodiscovery_def.acl_hostpipe_info.size());
  CHECK("pipe_name" ==
        m_device_def.autodiscovery_def.acl_hostpipe_info[0].name);
  CHECK_EQUAL(
      true, m_device_def.autodiscovery_def.acl_hostpipe_info[0].is_host_to_dev);
  CHECK_EQUAL(
      false,
      m_device_def.autodiscovery_def.acl_hostpipe_info[0].is_dev_to_host);
  CHECK_EQUAL(32,
              m_device_def.autodiscovery_def.acl_hostpipe_info[0].data_width);
  CHECK_EQUAL(
      32768,
      m_device_def.autodiscovery_def.acl_hostpipe_info[0].max_buffer_depth);

  // Check ACL's view
  CHECK_EQUAL(0, m_device_def.autodiscovery_def.accel[0].id);
  CHECK_EQUAL(0, m_device_def.autodiscovery_def.accel[0].mem.begin);
  CHECK_EQUAL(
      (void *)0x020000,
      m_device_def.autodiscovery_def.accel[0]
          .mem.next); // Not sure why this isn't 16KB like OpenCL spec minimum

  CHECK_EQUAL(
      2, (int)m_device_def.autodiscovery_def.accel[0].local_aspaces.size());
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].local_aspaces[0].aspace_id);
  CHECK_EQUAL(1024, (int)m_device_def.autodiscovery_def.accel[0]
                        .local_aspaces[0]
                        .static_demand);
  CHECK_EQUAL(
      6,
      (int)m_device_def.autodiscovery_def.accel[0].local_aspaces[1].aspace_id);
  CHECK_EQUAL(2048, (int)m_device_def.autodiscovery_def.accel[0]
                        .local_aspaces[1]
                        .static_demand);

  CHECK("foo" == m_device_def.autodiscovery_def.accel[0].iface.name);
  CHECK_EQUAL(
      0, (int)m_device_def.autodiscovery_def.accel[0].is_workgroup_invariant);
  CHECK_EQUAL(
      0, (int)m_device_def.autodiscovery_def.accel[0].is_workitem_invariant);
  CHECK_EQUAL(3,
              (int)m_device_def.autodiscovery_def.accel[0].max_global_work_dim);
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].profiling_words_to_readback);
  CHECK_EQUAL(7,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args.size());

  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[0].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[0].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[0].size);
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].iface.args[0].aspace_number);
  CHECK_EQUAL(16768, (int)m_device_def.autodiscovery_def.accel[0]
                         .iface.args[0]
                         .lmem_size_bytes);

  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[1].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[1].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[1].size);

  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[2].addr_space);
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[2].category);
  CHECK_EQUAL(8,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[2].size);

  // printf buffer start address
  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[3].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[3].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[3].size);

  // printf buffer size
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[4].addr_space);
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[4].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[4].size);

  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[5].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[5].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[5].size);

  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[0]);
  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[1]);
  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[2]);

  CHECK_EQUAL(0,
              (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size);
  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.accel[0].is_sycl_compile);

  // Checks for device global entry.
  CHECK_EQUAL(2, m_device_def.autodiscovery_def.device_global_mem_defs.size());
  const auto kernel15_dev_global =
      m_device_def.autodiscovery_def.device_global_mem_defs.find(
          "kernel15_dev_global");
  const auto kernel15_dev_global2 =
      m_device_def.autodiscovery_def.device_global_mem_defs.find(
          "kernel15_dev_global2");
  CHECK(kernel15_dev_global !=
        m_device_def.autodiscovery_def.device_global_mem_defs.end());
  CHECK(kernel15_dev_global2 !=
        m_device_def.autodiscovery_def.device_global_mem_defs.end());
  CHECK_EQUAL(4096, kernel15_dev_global->second.address);
  CHECK_EQUAL(2048, kernel15_dev_global->second.size);
  CHECK_EQUAL(ACL_DEVICE_GLOBAL_HOST_ACCESS_NONE,
              kernel15_dev_global->second.host_access);
  CHECK_EQUAL(false, kernel15_dev_global->second.can_skip_programming);
  CHECK_EQUAL(false, kernel15_dev_global->second.implement_in_csr);
  CHECK_EQUAL(false, kernel15_dev_global->second.reset_on_reuse);
  CHECK_EQUAL(2048, kernel15_dev_global2->second.address);
  CHECK_EQUAL(1024, kernel15_dev_global2->second.size);
  CHECK_EQUAL(ACL_DEVICE_GLOBAL_HOST_ACCESS_WRITE_ONLY,
              kernel15_dev_global2->second.host_access);
  CHECK_EQUAL(false, kernel15_dev_global2->second.can_skip_programming);
  CHECK_EQUAL(true, kernel15_dev_global2->second.implement_in_csr);
  CHECK_EQUAL(false, kernel15_dev_global2->second.reset_on_reuse);

  // Check a second parsing.
  // It should allocate a new string for the name.
  ACL_LOCKED(
      parsed = acl_load_device_def_from_str(
          std::string(
              VERSIONIDTOSTR(ACL_AUTO_CONFIGURE_VERSIONID)
                  DEVICE_FIELDS RANDOM_HASH
              " " BOARDNAME2 IS_BIG_ENDIAN MEM HOSTPIPE KERNEL_ARG_INFO_NONE
              " 1 52 bar" KERNEL_CRA KERNEL_FAST_LAUNCH_DEPTH KERNEL_PERF_MON
                  KERNEL_WORKGROUP_INVARIANT KERNEL_WORKITEM_INVARIANT
                      KERNEL_NUM_VECTOR_LANES2 KERNEL_PROFILE_SCANCHAIN_LENGTH
                          ARGS_LOCAL_GLOBAL_INT KERNEL_PRINTF_FORMATSTRINGS LD_0
                              KERNEL_REQD_WORK_GROUP_SIZE_235
                                  KERNEL_MAX_WORK_GROUP_SIZE_1024
                                      KERNEL_MAX_GLOBAL_WORK_DIM_ZERO
                                          KERNEL_USES_GLOBAL_WORK_OFFSET_DISABLED
                                              IS_NOT_SYCL_COMPILE),
          m_device_def.autodiscovery_def, err_str));
  CHECK_EQUAL(1, parsed);

  CHECK(BOARDNAME2 == m_device_def.autodiscovery_def.name);

  CHECK_EQUAL(
      1, (int)m_device_def.autodiscovery_def.accel[0].is_workgroup_invariant);
  CHECK_EQUAL(
      1, (int)m_device_def.autodiscovery_def.accel[0].is_workitem_invariant);
  CHECK("bar" == m_device_def.autodiscovery_def.hal_info[0].name);

  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.is_big_endian);

  CHECK_EQUAL(
      0, (int)m_device_def.autodiscovery_def.accel[0].local_aspaces.size());

  CHECK_EQUAL(
      2,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[0]);
  CHECK_EQUAL(
      3,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[1]);
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[2]);

  CHECK_EQUAL(1024,
              (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size);
  CHECK_EQUAL(
      32,
      (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size_arr[0]);
  CHECK_EQUAL(
      8,
      (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size_arr[1]);
  CHECK_EQUAL(
      4,
      (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size_arr[2]);

  CHECK_EQUAL(0,
              (int)m_device_def.autodiscovery_def.accel[0].max_global_work_dim);
  CHECK_EQUAL(0, (int)m_device_def.autodiscovery_def.accel[0].is_sycl_compile);

  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].profiling_words_to_readback);

  // Backwards-compatibility test (last backward compatible aoc version: 20.1,
  // version id: 23)
  ACL_LOCKED(
      parsed = acl_load_device_def_from_str(
          std::string(
              VERSIONIDTOSTR(
                  ACL_AUTO_CONFIGURE_BACKWARDS_COMPATIBLE_WITH_VERSIONID) " " DEVICE_FIELDS_OLD
                  RANDOM_HASH " " BOARDNAME IS_NOT_BIG_ENDIAN MEM_BACKWARDS_COMP
                      HOSTPIPE " 1 81 foo" KERNEL_CRA KERNEL_FAST_LAUNCH_DEPTH
                          KERNEL_PERF_MON KERNEL_WORKGROUP_VARIANT KERNEL_WORKITEM_VARIANT
                              KERNEL_NUM_VECTOR_LANES1 KERNEL_PROFILE_SCANCHAIN_LENGTH
                                  ARGS_LOCAL_GLOBAL_LONG_PROF KERNEL_PRINTF_FORMATSTRINGS
                                      LD_1024 KERNEL_REQD_WORK_GROUP_SIZE_NONE
                                          KERNEL_MAX_WORK_GROUP_SIZE_NONE
                                              KERNEL_MAX_GLOBAL_WORK_DIM_NONE
                                                  KERNEL_USES_GLOBAL_WORK_OFFSET_ENABLED),
          m_device_def.autodiscovery_def, err_str));
  CHECK_EQUAL(1, parsed);

  CHECK_EQUAL(1, m_device_def.autodiscovery_def.num_global_mem_systems);
  CHECK_EQUAL(0, m_device_def.autodiscovery_def.global_mem_defs[0].range.begin);
  CHECK_EQUAL((void *)2048,
              m_device_def.autodiscovery_def.global_mem_defs[0].range.next);

  CHECK(BOARDNAME == m_device_def.autodiscovery_def.name);

  CHECK_EQUAL(0, (int)m_device_def.autodiscovery_def.is_big_endian);

  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.accel.size());
  CHECK_EQUAL(1, (int)m_device_def.autodiscovery_def.hal_info.size());

  CHECK_EQUAL(0, m_device_def.autodiscovery_def.accel[0].id);
  CHECK_EQUAL(0, m_device_def.autodiscovery_def.accel[0].mem.begin);
  CHECK_EQUAL(
      (void *)0x020000,
      m_device_def.autodiscovery_def.accel[0]
          .mem.next); // Not sure why this isn't 16KB like OpenCL spec minimum

  CHECK_EQUAL(
      2, (int)m_device_def.autodiscovery_def.accel[0].local_aspaces.size());
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].local_aspaces[0].aspace_id);
  CHECK_EQUAL(1024, (int)m_device_def.autodiscovery_def.accel[0]
                        .local_aspaces[0]
                        .static_demand);
  CHECK_EQUAL(
      6,
      (int)m_device_def.autodiscovery_def.accel[0].local_aspaces[1].aspace_id);
  CHECK_EQUAL(2048, (int)m_device_def.autodiscovery_def.accel[0]
                        .local_aspaces[1]
                        .static_demand);

  CHECK("foo" == m_device_def.autodiscovery_def.accel[0].iface.name);
  CHECK_EQUAL(
      0, (int)m_device_def.autodiscovery_def.accel[0].is_workgroup_invariant);
  CHECK_EQUAL(
      0, (int)m_device_def.autodiscovery_def.accel[0].is_workitem_invariant);
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].profiling_words_to_readback);
  CHECK_EQUAL(7,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args.size());

  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[0].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[0].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[0].size);
  CHECK_EQUAL(
      5,
      (int)m_device_def.autodiscovery_def.accel[0].iface.args[0].aspace_number);
  CHECK_EQUAL(16768, (int)m_device_def.autodiscovery_def.accel[0]
                         .iface.args[0]
                         .lmem_size_bytes);

  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[1].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[1].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[1].size);

  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[2].addr_space);
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[2].category);
  CHECK_EQUAL(8,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[2].size);

  // printf buffer start address
  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[3].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[3].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[3].size);

  // printf buffer size
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[4].addr_space);
  CHECK_EQUAL(0,
              m_device_def.autodiscovery_def.accel[0].iface.args[4].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[4].size);

  CHECK_EQUAL(2,
              m_device_def.autodiscovery_def.accel[0].iface.args[5].addr_space);
  CHECK_EQUAL(1,
              m_device_def.autodiscovery_def.accel[0].iface.args[5].category);
  CHECK_EQUAL(4,
              (int)m_device_def.autodiscovery_def.accel[0].iface.args[5].size);

  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[0]);
  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[1]);
  CHECK_EQUAL(
      0,
      (int)m_device_def.autodiscovery_def.accel[0].compile_work_group_size[2]);

  CHECK_EQUAL(0,
              (int)m_device_def.autodiscovery_def.accel[0].max_work_group_size);
}

TEST(auto_configure, many_ok_forward_compatibility) {
  // From example_designs/merge_sort with extra fields at the end of each
  // sections and subsections to check forward compatibility

  std::string str(VERSIONIDTOSTR(
      ACL_AUTO_CONFIGURE_VERSIONID) " 52 "
                                    "sample40byterandomhash000000000000000000 "
                                    "a10gx 0 1 17 DDR 2 1 6 0 2147483648 100 "
                                    "100 100 100 0 - 0 200 200 200 200 0 0 0 "
                                    "2 10 " // Two device global below each has
                                            // 10 fields
                                    "ms_dev_global1 0x800 1024 3 0 0 0 300 300 "
                                    "300 "
                                    "ms_dev_global2 0x1000 1024 1 1 1 0 300 "
                                    "300 300 "
                                    "0 " // cra_ring_root_exist
                                    "0 " // num of hostpipe mappings
                                    "0 " // num of hostpipe mapping field
                                    "0 " // Number of groups of sideband signals
                                    "400 47 " // future fields
                                    "40 external_sort_stage_0 0 128 1 0 0 1 0 "
                                    "1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 0 "
                                    "0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 external_sort_stage_1 256 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "40 external_sort_stage_2 512 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "40 external_sort_stage_3 768 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "40 external_sort_stage_4 1024 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "40 external_sort_stage_5 1280 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "40 external_sort_stage_6 1536 128 1 0 0 1 "
                                    "0 1 0 1 10 0 0 4 1 0 0 0 500 500 500 0 "
                                    "0 0 0 1 1 1 3 1 1 1 3 1 0 0 800 800 "
                                    "800 "
                                    "38 external_stream_writer0 1792 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer1 2048 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer2 2304 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer3 2560 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer4 2816 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer5 3072 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 external_stream_writer6 3328 256 1 0 0 "
                                    "0 0 1 0 1 10 2 1 8 1024 0 0 0 500 500 "
                                    "500 0 0 0 0 0 0 0 1 2147483647 3 1 0 "
                                    "0 800 800 800 "
                                    "38 input_reader 3584 256 1 0 0 0 0 1 0 1 "
                                    "10 2 1 8 1024 0 0 0 500 500 500 0 0 0 0 "
                                    "0 0 0 1 2147483647 3 1 0 0 800 800 "
                                    "800 "
                                    "38 output_writer 3840 256 1 0 0 0 0 1 0 1 "
                                    "10 2 1 8 1024 0 0 0 500 500 500 0 0 0 0 "
                                    "0 0 0 1 2147483647 3 1 0 0 800 800 "
                                    "800 "
                                    "40 sort_stage_1 4096 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_10 4352 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_11 4608 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_12 4864 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_13 5120 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_14 5376 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_15 5632 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_16 5888 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_17 6144 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_2 6400 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_3 6656 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_4 6912 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_5 7168 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_6 7424 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_7 7680 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_8 7936 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "40 sort_stage_9 8192 128 1 0 0 1 0 1 0 1 "
                                    "10 0 0 4 1 0 0 0 500 500 500 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 "
                                    "38 stream_reader_A0 8448 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A1 8704 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A2 8960 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A3 9216 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A4 9472 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A5 9728 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_A6 9984 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B0 10240 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B1 10496 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B2 10752 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B3 11008 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B4 11264 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B5 11520 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 "
                                    "38 stream_reader_B6 11776 256 1 0 0 0 0 1 "
                                    "0 1 10 2 1 8 1024 0 0 0 500 500 500 0 0 "
                                    "0 0 0 0 0 1 2147483647 3 1 0 0 800 "
                                    "800 800 900 900 900 900 900");

  std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
  for (auto &device_def : device_defs) {
    int parsed;
    std::string err_str;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   str, device_def.autodiscovery_def, err_str));
    CHECK_EQUAL(1, parsed);

    CHECK("a10gx" == device_def.autodiscovery_def.name);
    CHECK("sample40byterandomhash000000000000000000" ==
          device_def.autodiscovery_def.binary_rand_hash);

    CHECK_EQUAL(47, (int)device_def.autodiscovery_def.accel.size());
    CHECK_EQUAL(47, (int)device_def.autodiscovery_def.hal_info.size());
  }
}

TEST(auto_configure, many_limit_check) {
  std::string str(VERSIONIDTOSTR(
      ACL_AUTO_CONFIGURE_VERSIONID) " 19 "
                                    "sample40byterandomhash000000000000000000 "
                                    "a10gx 0 1 9 DDR 2 1 2 0 2147483648 0 - 0 "
                                    "0 0 0 0 0 75 " // 75 kernels
                                    "31 external_sort_stage_0 0 128 1 0 0 1 0 "
                                    "1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 "
                                    "3 1 "
                                    "31 external_sort_stage_1 256 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_2 512 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_3 768 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_4 1024 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_5 1280 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_6 1536 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "29 external_stream_writer0 1792 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer1 2048 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer2 2304 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer3 2560 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer4 2816 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer5 3072 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer6 3328 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 input_reader 3584 256 1 0 0 0 0 1 0 1 "
                                    "6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 output_writer 2931 256 1 0 0 0 0 1 0 1 "
                                    "6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "31 sort_stage_1 3196 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_10 4352 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_11 4608 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_12 4864 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_13 5120 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_14 5376 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_15 5632 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_16 5888 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_17 6144 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_2 6310 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_3 6656 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_4 6912 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_5 7168 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_6 7424 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_7 7680 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_8 7936 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_9 8192 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "29 stream_reader_A0 8448 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A1 8704 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A2 8960 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A3 9216 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A4 9472 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A5 9728 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_A6 9984 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B0 10231 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B1 10496 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B2 10752 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B3 11008 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B4 11264 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B5 11520 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 stream_reader_B6 11776 256 1 0 0 0 0 1 "
                                    "0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "31 external_sort_stage_0 0 128 1 0 0 1 0 "
                                    "1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 "
                                    "3 1 "
                                    "31 external_sort_stage_1 256 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_2 512 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_3 768 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_4 1024 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_5 1280 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "31 external_sort_stage_6 1536 128 1 0 0 1 "
                                    "0 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 "
                                    "1 3 1 "
                                    "29 external_stream_writer0 1792 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer1 2048 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer2 2304 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer3 2560 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer4 2816 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer5 3072 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 external_stream_writer6 3328 256 1 0 0 "
                                    "0 0 1 0 1 6 2 1 8 1024 0 0 0 0 0 0 0 0 0 "
                                    "1 2147483647 3 1 "
                                    "29 input_reader 3584 256 1 0 0 0 0 1 0 1 "
                                    "6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "29 output_writer 2931 256 1 0 0 0 0 1 0 1 "
                                    "6 2 1 8 1024 0 0 0 0 0 0 0 0 0 1 "
                                    "2147483647 3 1 "
                                    "31 sort_stage_1 3196 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_10 4352 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_11 4608 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_12 4864 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_13 5120 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_14 5376 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_15 5632 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_16 5888 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_17 6144 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_2 6310 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_3 6656 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_4 6912 128 1 0 0 1 0 1 0 1 "
                                    "6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1");

  // Verify that we can handle 75 kernels
  std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
  for (auto &device_def : device_defs) {
    int parsed;
    std::string err;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   str, device_def.autodiscovery_def, err));
    CHECK_EQUAL(1, parsed);

    // But we should write 75 kernels to the requested physical_device_id entry.
    CHECK_EQUAL(75, (int)device_def.autodiscovery_def.accel.size());
    CHECK_EQUAL(75, (int)device_def.autodiscovery_def.hal_info.size());
  }
}

TEST(auto_configure, bad_config) {
  std::vector<std::string> strs = {
      std::string(
          "0 pcie385n_a7 0 0 2 1024 0 2147483648 2147483648 4294967296 0 0 0 2 "
          "sort_stage_1 0 128 0 0 0 1 0 1 0 1 0 0 4 1 0 0 1 1 1 1 "
          "sort_stage_2 128 128 0 0 0 1 0 1 0 1 0 0 4 1 0 0 1 1 1 1"),
      std::string("Error: The accelerator hardware currently programmed is "
                  "incompatible with this\nversion of the runtime (" ACL_VERSION
                  " Commit " ACL_GIT_COMMIT
                  "). Recompile the hardware with\nthe same version of "
                  "the compiler and program that onto the board.\n"),

      // Bad workgroup/workitem_invariant combination
      std::string(VERSIONIDTOSTR(
          ACL_AUTO_CONFIGURE_VERSIONID) " 15 "
                                        "sample40byterandomhash0000000000000000"
                                        "00 a10gx 0 1 7 DDR 2 1 2 0 2147483648 "
                                        "0 0 0 0 1 "
                                        "31 external_sort_stage_0 0 128 1 0 0 "
                                        "0 1" /*workgroup_invariant = 0,
                                                 workitem_invariant =1 */
                                        " 1 0 1 6 0 0 4 1 0 0 0 0 0 0 1 1 1 3 "
                                        "1 1 1 3 1"),
      std::string(
          "FAILED to read auto-discovery string at byte 132: kernel cannot be "
          "workitem-invariant while it is workgroup-variant. Full "
          "auto-discovery string value is " VERSIONIDTOSTR(
              ACL_AUTO_CONFIGURE_VERSIONID) " 15 "
                                            "sample40byterandomhash000000000000"
                                            "000000 a10gx 0 1 7 DDR 2 1 2 0 "
                                            "2147483648 0 0 0 0 1 "
                                            "31 external_sort_stage_0 0 128 1 "
                                            "0 0 0 1 1 0 1 6 0 0 4 1 0 0 0 0 0 "
                                            "0 1 1 1 3 1 1 1 3 1\n")};

  for (unsigned istr = 0; istr < strs.size(); istr += 2) {
    int parsed;
    std::string err;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   strs[istr], m_device_def.autodiscovery_def, err));
    CHECK_EQUAL(0, parsed);

    const auto &expect = strs[istr + 1];
    for (unsigned ichar = 0; err[ichar] && expect[ichar] &&
                             ichar < err.length() && ichar < expect.length();
         ichar++) {
      if (err[ichar] != expect[ichar]) {
        std::cout << "Failed at char " << ichar << " '" << err[ichar]
                  << "' vs '" << expect[ichar] << "'\n";
      }
    }

    if (expect != err) {
      std::cout << istr / 2 << ": err string is " << err << "\n";
      std::cout << istr / 2 << ": exp string is " << expect << "\n";
    }

    CHECK(expect == err);

    CHECK_EQUAL(0, (int)m_device_def.autodiscovery_def.accel.size());
    CHECK_EQUAL(0, (int)m_device_def.autodiscovery_def.hal_info.size());
  }
}

TEST(auto_configure, multi_mem_config) {
  std::string str(VERSIONIDTOSTR(
      ACL_AUTO_CONFIGURE_VERSIONID) " 56 "
                                    "sample40byterandomhash000000000000000000 "
                                    "pcie385n_a7 0 "
                                    "4 "
                                    "10 SVM 0 1 2 0 1073741824 0 - 1 SVM2 "
                                    "11 DDR 2 2 24 1 2 1073741824 3221225472 "
                                    "3221225472 5368709120 0 "
                                    "15 QDR 2 4 48 0 2 5368709120 5369757696 "
                                    "5369757696 5370806272 5370806272 "
                                    "5371854848 5371854848 5372903424 4 "
                                    "9 SVM2 0 1 2 0 1073741824 0 SVM 0 "
                                    "0 0 0 2 "
                                    "31 sort_stage_1 0 128 1 0 0 1 0 1 0 1 6 0 "
                                    "0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                    "31 sort_stage_2 128 128 1 0 0 1 0 1 0 1 6 "
                                    "0 0 4 1 0 0 0 0 0 0 1 1 1 3 1 1 1 3 1");

  std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
  for (auto &device_def : device_defs) {
    int parsed;
    std::string err_str;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   str, device_def.autodiscovery_def, err_str));
    CHECK_EQUAL(1, parsed);

    CHECK("pcie385n_a7" == device_def.autodiscovery_def.name);
    CHECK("sample40byterandomhash000000000000000000" ==
          device_def.autodiscovery_def.binary_rand_hash);

    CHECK_EQUAL(4, device_def.autodiscovery_def.num_global_mem_systems);

    CHECK("SVM" == device_def.autodiscovery_def.global_mem_defs[0].name);
    CHECK_EQUAL(ACL_GLOBAL_MEM_SHARED_VIRTUAL,
                device_def.autodiscovery_def.global_mem_defs[0].type);
    CHECK_EQUAL(
        1, device_def.autodiscovery_def.global_mem_defs[0].burst_interleaved);
    CHECK_EQUAL(0, device_def.autodiscovery_def.global_mem_defs[0].config_addr);
    CHECK_EQUAL(
        1, device_def.autodiscovery_def.global_mem_defs[0].num_global_banks);
    CHECK("" ==
          device_def.autodiscovery_def.global_mem_defs[0].primary_interface);
    CHECK_EQUAL(
        1,
        device_def.autodiscovery_def.global_mem_defs[0].can_access_list.size());
    CHECK(
        "SVM2" ==
        device_def.autodiscovery_def.global_mem_defs[0].can_access_list.at(0));

    CHECK("DDR" == device_def.autodiscovery_def.global_mem_defs[1].name);
    CHECK_EQUAL(ACL_GLOBAL_MEM_DEVICE_PRIVATE,
                device_def.autodiscovery_def.global_mem_defs[1].type);
    CHECK_EQUAL(
        1, device_def.autodiscovery_def.global_mem_defs[1].burst_interleaved);
    CHECK_EQUAL(0x18,
                device_def.autodiscovery_def.global_mem_defs[1].config_addr);
    CHECK_EQUAL(
        2, device_def.autodiscovery_def.global_mem_defs[1].num_global_banks);
    CHECK(ACL_GLOBAL_MEM_UNDEFINED_ALLOCATION ==
          device_def.autodiscovery_def.global_mem_defs[1].allocation_type);

    CHECK("QDR" == device_def.autodiscovery_def.global_mem_defs[2].name);
    CHECK_EQUAL(ACL_GLOBAL_MEM_DEVICE_PRIVATE,
                device_def.autodiscovery_def.global_mem_defs[2].type);
    CHECK_EQUAL(
        0, device_def.autodiscovery_def.global_mem_defs[2].burst_interleaved);
    CHECK_EQUAL(0x30,
                device_def.autodiscovery_def.global_mem_defs[2].config_addr);
    CHECK_EQUAL(
        4, device_def.autodiscovery_def.global_mem_defs[2].num_global_banks);
    CHECK(ACL_GLOBAL_MEM_DEVICE_ALLOCATION ==
          device_def.autodiscovery_def.global_mem_defs[2].allocation_type);

    CHECK("SVM2" == device_def.autodiscovery_def.global_mem_defs[3].name);
    CHECK_EQUAL(ACL_GLOBAL_MEM_SHARED_VIRTUAL,
                device_def.autodiscovery_def.global_mem_defs[3].type);
    CHECK_EQUAL(
        1, device_def.autodiscovery_def.global_mem_defs[3].burst_interleaved);
    CHECK_EQUAL(0, device_def.autodiscovery_def.global_mem_defs[3].config_addr);
    CHECK_EQUAL(
        1, device_def.autodiscovery_def.global_mem_defs[3].num_global_banks);
    CHECK("SVM" ==
          device_def.autodiscovery_def.global_mem_defs[3].primary_interface);
    CHECK_EQUAL(
        0,
        device_def.autodiscovery_def.global_mem_defs[3].can_access_list.size());

    CHECK_EQUAL(2, (int)device_def.autodiscovery_def.accel.size());
    CHECK_EQUAL(2, (int)device_def.autodiscovery_def.hal_info.size());
  }
}

TEST(auto_configure, kernel_arg_info) {
  std::vector<std::string> strs = {
      std::string(VERSIONIDTOSTR(
          ACL_AUTO_CONFIGURE_VERSIONID) " 15 "
                                        "sample40byterandomhash0000000000000000"
                                        "00 a10gx 0 1 7 DDR 2 1 2 0 2147483648 "
                                        "0 0 0 1 2 "
                                        "34 external_sort_stage_0 0 128 1 0 0 "
                                        "1 0 1 0 2 9 0 0 4 1 0 0 arg_one "
                                        "type_one 1 9 0 0 4 1 0 0 arg_two "
                                        "type_two 2 0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                        "34 external_sort_stage_1 256 128 1 0 "
                                        "0 1 0 1 0 2 9 0 0 4 1 0 0 arg_three "
                                        "type_three 1 9 0 0 4 1 0 0 arg_four "
                                        "type_four 2 0 0 0 0 1 1 1 3 1 1 1 3 "
                                        "1 "),
      std::string(VERSIONIDTOSTR(
          ACL_AUTO_CONFIGURE_VERSIONID) " 15 "
                                        "sample40byterandomhash0000000000000000"
                                        "00 a10gx 0 1 7 DDR 2 1 2 0 2147483648 "
                                        "0 0 0 0 2 "
                                        "34 external_sort_stage_0 0 128 1 0 0 "
                                        "1 0 1 0 2 6 0 0 4 1 0 0 6 0 0 4 1 0 0 "
                                        "0 0 0 0 1 1 1 3 1 1 1 3 1 "
                                        "34 external_sort_stage_1 256 128 1 0 "
                                        "0 1 0 1 0 2 6 0 0 4 1 0 0 6 0 0 4 1 0 "
                                        "0 0 0 0 0 1 1 1 3 1 1 1 3 1 ")};

  // kernel arg info available
  {
    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err_str;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     strs[0], device_def.autodiscovery_def, err_str));
      CHECK_EQUAL(1, parsed);

      CHECK("a10gx" == device_def.autodiscovery_def.name);
      CHECK("sample40byterandomhash000000000000000000" ==
            device_def.autodiscovery_def.binary_rand_hash);

      CHECK_EQUAL(1, device_def.autodiscovery_def.num_global_mem_systems);
      CHECK("DDR" == device_def.autodiscovery_def.global_mem_defs[0].name);

      CHECK_EQUAL(2, (int)device_def.autodiscovery_def.accel.size());
      CHECK_EQUAL(2, (int)device_def.autodiscovery_def.hal_info.size());
      CHECK_EQUAL(2,
                  (int)device_def.autodiscovery_def.accel[0].iface.args.size());
      CHECK_EQUAL(2,
                  (int)device_def.autodiscovery_def.accel[1].iface.args.size());

      CHECK("arg_one" ==
            device_def.autodiscovery_def.accel[0].iface.args[0].name);
      CHECK("type_one" ==
            device_def.autodiscovery_def.accel[0].iface.args[0].type_name);
      CHECK_EQUAL(
          1,
          device_def.autodiscovery_def.accel[0].iface.args[0].access_qualifier);

      CHECK("arg_two" ==
            device_def.autodiscovery_def.accel[0].iface.args[1].name);
      CHECK("type_two" ==
            device_def.autodiscovery_def.accel[0].iface.args[1].type_name);
      CHECK_EQUAL(
          2,
          device_def.autodiscovery_def.accel[0].iface.args[1].access_qualifier);

      CHECK("arg_three" ==
            device_def.autodiscovery_def.accel[1].iface.args[0].name);
      CHECK("type_three" ==
            device_def.autodiscovery_def.accel[1].iface.args[0].type_name);
      CHECK_EQUAL(
          1,
          device_def.autodiscovery_def.accel[1].iface.args[0].access_qualifier);

      CHECK("arg_four" ==
            device_def.autodiscovery_def.accel[1].iface.args[1].name);
      CHECK("type_four" ==
            device_def.autodiscovery_def.accel[1].iface.args[1].type_name);
      CHECK_EQUAL(
          2,
          device_def.autodiscovery_def.accel[1].iface.args[1].access_qualifier);
    }
  }

  // kernel arg info not available
  {
    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err_str;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     strs[1], device_def.autodiscovery_def, err_str));
      CHECK_EQUAL(1, parsed);

      CHECK("a10gx" == device_def.autodiscovery_def.name);
      CHECK("sample40byterandomhash000000000000000000" ==
            device_def.autodiscovery_def.binary_rand_hash);

      CHECK_EQUAL(1, device_def.autodiscovery_def.num_global_mem_systems);
      CHECK("DDR" == device_def.autodiscovery_def.global_mem_defs[0].name);

      CHECK_EQUAL(2, (int)device_def.autodiscovery_def.accel.size());
      CHECK_EQUAL(2, (int)device_def.autodiscovery_def.hal_info.size());
      CHECK_EQUAL(2,
                  (int)device_def.autodiscovery_def.accel[0].iface.args.size());
      CHECK_EQUAL(2,
                  (int)device_def.autodiscovery_def.accel[1].iface.args.size());

      CHECK("" == device_def.autodiscovery_def.accel[0].iface.args[0].name);
      CHECK("" ==
            device_def.autodiscovery_def.accel[0].iface.args[0].type_name);
      CHECK_EQUAL(
          0,
          device_def.autodiscovery_def.accel[0].iface.args[0].access_qualifier);

      CHECK("" == device_def.autodiscovery_def.accel[0].iface.args[1].name);
      CHECK("" ==
            device_def.autodiscovery_def.accel[0].iface.args[1].type_name);
      CHECK_EQUAL(
          0,
          device_def.autodiscovery_def.accel[0].iface.args[1].access_qualifier);

      CHECK("" == device_def.autodiscovery_def.accel[1].iface.args[0].name);
      CHECK("" ==
            device_def.autodiscovery_def.accel[1].iface.args[0].type_name);
      CHECK_EQUAL(
          0,
          device_def.autodiscovery_def.accel[1].iface.args[0].access_qualifier);

      CHECK("" == device_def.autodiscovery_def.accel[1].iface.args[1].name);
      CHECK("" ==
            device_def.autodiscovery_def.accel[1].iface.args[1].type_name);
      CHECK_EQUAL(
          0,
          device_def.autodiscovery_def.accel[1].iface.args[1].access_qualifier);
    }
  }
}

TEST(auto_configure, hostpipe) {
  std::string str(VERSIONIDTOSTR(
      ACL_AUTO_CONFIGURE_VERSIONID) " 50 "
                                    "sample40byterandomhash000000000000000000 "
                                    "a10gx_hostpipe 0 1 15 DDR 2 1 6 0 "
                                    "2147483648 0 100 100 100 100 200 200 200 "
                                    "200 "
                                    "2 9 host_to_dev 1 0 32 32768 300 300 300 "
                                    "300 dev_to_host 0 1 32 32768 300 300 300 "
                                    "300 400 1 7 dev_global_3 0x400 2048 0 0 0 "
                                    "0 "
                                    "1 29 foo 0 128 1 0 0 1 0 1 0 0 0 0 0 0 1 "
                                    "1 1 3 1 1 1 3 1 0 0 800 800 800 900 "
                                    "900"

  );

  std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
  for (auto &device_def : device_defs) {
    int parsed;
    std::string err_str;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   str, device_def.autodiscovery_def, err_str));
    CHECK_EQUAL(1, parsed);

    CHECK("a10gx_hostpipe" == device_def.autodiscovery_def.name);
    CHECK("sample40byterandomhash000000000000000000" ==
          device_def.autodiscovery_def.binary_rand_hash);

    CHECK_EQUAL(1, device_def.autodiscovery_def.num_global_mem_systems);
    CHECK("foo" == device_def.autodiscovery_def.hal_info[0].name);
    CHECK("DDR" == device_def.autodiscovery_def.global_mem_defs[0].name);

    CHECK_EQUAL(1, (int)device_def.autodiscovery_def.accel.size());
    CHECK_EQUAL(1, (int)device_def.autodiscovery_def.hal_info.size());
  }
}

TEST(auto_configure, streaming) {
  const std::string config_str{
      "23 30 " RANDOM_HASH
      " pac_a10 0 1 13 DDR 2 2 24 1 2 0 4294967296 4294967296 8589934592 0 - 0 "
      "0 0 0 1 7 device_global_name 0x100 128 0 0 0 0 1 105 _ZTS3CRCILi0EE 0 "
      "256 "
      "1 0 0 1 0 1 0 9 8 0 0 8 1 0 0 1 k0_ZTS3CRCILi0EE_arg0 8 2 1 8 1024 0 3 "
      "1 k0_ZTS3CRCILi0EE_arg1 8 0 0 8 1 0 0 1 k0_ZTS3CRCILi0EE_arg2 7 0 0 8 1 "
      "0 0 0 7 0 0 8 1 0 0 0 7 2 1 8 1024 0 2 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 "
      "0 7 0 0 8 1 0 0 0 0 0 1 2 64 4096 1 1 1 3 1 1 1 3 1 0 1 "
      "k0_ZTS3CRCILi0EE_streaming_start k0_ZTS3CRCILi0EE_streaming_done "};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(1, devdef.accel.size());

  CHECK(!devdef.accel[0].is_sycl_compile);
  CHECK(devdef.accel[0].streaming_control_info_available);
  CHECK("k0_ZTS3CRCILi0EE_streaming_start" ==
        devdef.accel[0].streaming_control_info.start);
  CHECK("k0_ZTS3CRCILi0EE_streaming_done" ==
        devdef.accel[0].streaming_control_info.done);

  const auto &args = devdef.accel[0].iface.args;
  CHECK_EQUAL(9, args.size());

  CHECK(args[0].streaming_arg_info_available);
  CHECK("k0_ZTS3CRCILi0EE_arg0" == args[0].streaming_arg_info.interface_name);

  CHECK(args[1].streaming_arg_info_available);
  CHECK("k0_ZTS3CRCILi0EE_arg1" == args[1].streaming_arg_info.interface_name);

  CHECK(args[2].streaming_arg_info_available);
  CHECK("k0_ZTS3CRCILi0EE_arg2" == args[2].streaming_arg_info.interface_name);

  for (size_t i = 3; i < args.size(); ++i) {
    CHECK(!args[i].streaming_arg_info_available);
  }
}

TEST(auto_configure, one_streaming_arg_and_streaming_kernel) {
  const std::string config_str{
      "23 27 531091a097f0d7096b21f349b4b283f9e206ebc0 pac_s10 0 1 17 DDR 2 4 "
      "24 1 2 0 8589934592 8589934592 17179869184 17179869184 25769803776 "
      "25769803776 34359738368 0 - 0 0 0 0 0 0 1 125 _ZTS15binomial_kernel 0 "
      "256 0 0 0 0 0 1 0 8 7 2 1 8 1024 0 2 0 8 0 0 8 1 0 0 1 "
      "k0_ZTS15binomial_kernel_arg1 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 2 1 8 "
      "1024 0 2 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 0 0 16 2 64 "
      "8196 65 8196 66 8196 67 8196 68 8196 69 8196 70 8196 71 8196 72 8196 73 "
      "8196 74 8196 75 8196 76 8196 77 8196 78 8196 79 8196 1 1 1 3 1 1 1 3 1 "
      "1 1 k0_ZTS15binomial_kernel_streaming_start "
      "k0_ZTS15binomial_kernel_streaming_done "};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(1, devdef.accel.size());

  CHECK(devdef.accel[0].streaming_control_info_available);
  CHECK("k0_ZTS15binomial_kernel_streaming_start" ==
        devdef.accel[0].streaming_control_info.start);
  CHECK("k0_ZTS15binomial_kernel_streaming_done" ==
        devdef.accel[0].streaming_control_info.done);

  const auto &args = devdef.accel[0].iface.args;
  CHECK_EQUAL(8, args.size());

  CHECK(!args[0].streaming_arg_info_available);

  CHECK(args[1].streaming_arg_info_available);
  CHECK("k0_ZTS15binomial_kernel_arg1" ==
        args[1].streaming_arg_info.interface_name);

  for (size_t i = 2; i < args.size(); ++i) {
    CHECK(!args[i].streaming_arg_info_available);
  }
}

TEST(auto_configure, two_streaming_args_and_streaming_kernel) {
  const std::string config_str{
      "23 27 531091a097f0d7096b21f349b4b283f9e206ebc0 pac_s10 0 1 17 DDR 2 4 "
      "24 1 2 0 8589934592 8589934592 17179869184 17179869184 25769803776 "
      "25769803776 34359738368 0 - 0 0 0 0 0 0 1 126 _ZTS15binomial_kernel 0 "
      "256 0 0 0 0 0 1 0 8 8 2 1 8 1024 0 2 1 k0_ZTS15binomial_kernel_arg0 8 0 "
      "0 8 1 0 0 1 k0_ZTS15binomial_kernel_arg1 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 "
      "0 7 2 1 8 1024 0 2 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 0 "
      "0 16 2 64 8196 65 8196 66 8196 67 8196 68 8196 69 8196 70 8196 71 8196 "
      "72 8196 73 8196 74 8196 75 8196 76 8196 77 8196 78 8196 79 8196 1 1 1 3 "
      "1 1 1 3 1 1 1 k0_ZTS15binomial_kernel_streaming_start "
      "k0_ZTS15binomial_kernel_streaming_done "};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(1, devdef.accel.size());

  CHECK(devdef.accel[0].is_sycl_compile);
  CHECK(devdef.accel[0].streaming_control_info_available);
  CHECK("k0_ZTS15binomial_kernel_streaming_start" ==
        devdef.accel[0].streaming_control_info.start);
  CHECK("k0_ZTS15binomial_kernel_streaming_done" ==
        devdef.accel[0].streaming_control_info.done);

  const auto &args = devdef.accel[0].iface.args;
  CHECK_EQUAL(8, args.size());

  CHECK(args[0].streaming_arg_info_available);
  CHECK("k0_ZTS15binomial_kernel_arg0" ==
        args[0].streaming_arg_info.interface_name);

  CHECK(args[1].streaming_arg_info_available);
  CHECK("k0_ZTS15binomial_kernel_arg1" ==
        args[1].streaming_arg_info.interface_name);

  for (size_t i = 2; i < args.size(); ++i) {
    CHECK(!args[i].streaming_arg_info_available);
  }
}

TEST(auto_configure, two_streaming_args_and_non_streaming_kernel) {
  const std::string config_str{
      "23 27 531091a097f0d7096b21f349b4b283f9e206ebc0 pac_s10 0 1 17 DDR 2 4 "
      "24 1 2 0 8589934592 8589934592 17179869184 17179869184 25769803776 "
      "25769803776 34359738368 0 - 0 0 0 0 0 0 1 124 _ZTS15binomial_kernel 0 "
      "256 0 0 0 0 0 1 0 8 8 2 1 8 1024 0 2 1 k0_ZTS15binomial_kernel_arg0 8 0 "
      "0 8 1 0 0 1 k0_ZTS15binomial_kernel_arg1 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 "
      "0 7 2 1 8 1024 0 2 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 0 "
      "0 16 2 64 8196 65 8196 66 8196 67 8196 68 8196 69 8196 70 8196 71 8196 "
      "72 8196 73 8196 74 8196 75 8196 76 8196 77 8196 78 8196 79 8196 1 1 1 3 "
      "1 1 1 3 1 1 0"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(1, devdef.accel.size());

  CHECK(devdef.accel[0].is_sycl_compile);
  CHECK(!devdef.accel[0].streaming_control_info_available);

  const auto &args = devdef.accel[0].iface.args;
  CHECK_EQUAL(8, args.size());

  CHECK(args[0].streaming_arg_info_available);
  CHECK("k0_ZTS15binomial_kernel_arg0" ==
        args[0].streaming_arg_info.interface_name);

  CHECK(args[1].streaming_arg_info_available);
  CHECK("k0_ZTS15binomial_kernel_arg1" ==
        args[1].streaming_arg_info.interface_name);

  for (size_t i = 2; i < args.size(); ++i) {
    CHECK(!args[i].streaming_arg_info_available);
  }
}

TEST(auto_configure, cra_ring_root_not_exist) {
  const std::string config_str{
      "23 50 2ccb683dee8e34a004c1a27e6d722090a8cc684d custom_ipa 0 1 9 0 2 1 2 "
      "0 2199023255552 3 "
      "- 0 6 5 ZTSZ4mainE4MyIP_arg_input_a 1 0 8 32768 "
      "ZTSZ4mainE4MyIP_arg_input_b 1 0 8 32768 ZTSZ4mainE4MyIP_arg_input_c"
      " 1 0 8 32768 ZTSZ4mainE4MyIP_arg_n 1 0 4 32768 "
      "ZTSZ4mainE4MyIP_streaming_start 1 0 0 32768 "
      "ZTSZ4mainE4MyIP_streaming_done"
      " 0 1 0 32768 0 0 0 0 1 64 _ZTSZ4mainE4MyIP 0 128 1 0 0 1 0 1 0 4 8 2 1 "
      "8 4 0 0 1 ZTSZ4mainE4MyIP_arg_input_a 8"
      " 2 1 8 4 0 0 1 ZTSZ4mainE4MyIP_arg_input_b 8 2 1 8 4 0 0 1 "
      "ZTSZ4mainE4MyIP_arg_input_c"
      " 8 0 0 4 1 0 0 1 ZTSZ4mainE4MyIP_arg_n 0 0 0 0 1 1 1 3 1 1 1 3 1 1 1 "
      "ZTSZ4mainE4MyIP_streaming_start"
      " ZTSZ4mainE4MyIP_streaming_done"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(0, devdef.cra_ring_root_exist);
}

TEST(auto_configure, cra_ring_root_exist) {
  const std::string config_str{
      "23 50 2ccb683dee8e34a004c1a27e6d722090a8cc684d custom_ipa 0 1 9 0 2 1 2 "
      "0 2199023255552 3 "
      "- 0 6 5 ZTSZ4mainE4MyIP_arg_input_a 1 0 8 32768 "
      "ZTSZ4mainE4MyIP_arg_input_b 1 0 8 32768 ZTSZ4mainE4MyIP_arg_input_c"
      " 1 0 8 32768 ZTSZ4mainE4MyIP_arg_n 1 0 4 32768 "
      "ZTSZ4mainE4MyIP_streaming_start 1 0 0 32768 "
      "ZTSZ4mainE4MyIP_streaming_done"
      " 0 1 0 32768 0 0 0 1 1 64 _ZTSZ4mainE4MyIP 0 128 1 0 0 1 0 1 0 4 8 2 1 "
      "8 4 0 0 1 ZTSZ4mainE4MyIP_arg_input_a 8"
      " 2 1 8 4 0 0 1 ZTSZ4mainE4MyIP_arg_input_b 8 2 1 8 4 0 0 1 "
      "ZTSZ4mainE4MyIP_arg_input_c"
      " 8 0 0 4 1 0 0 1 ZTSZ4mainE4MyIP_arg_n 0 0 0 0 1 1 1 3 1 1 1 3 1 1 1 "
      "ZTSZ4mainE4MyIP_streaming_start"
      " ZTSZ4mainE4MyIP_streaming_done"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(1, devdef.cra_ring_root_exist);
}

TEST(auto_configure, hostpipe_mappings) {
  const std::string config_str{
      "23 76 " RANDOM_HASH
      " pac_a10 0 1 13 DDR 2 2 24 1 2 0 4294967296 4294967296 8589934592 0 - 0 "
      "0 0 0 0 0 1 5 10 " // 5 Hostpipes, 10 in each mapping
      "pipe_logical_name1 pipe_physical_name1 1 12345 0 1 4 10 0 0 "
      "pipe_logical_name2 pipe_physical_name2 0 12323 1 0 8 20 1 1 "
      "pipe_logical_name3 pipe_physical_name1 1 12313 0 1 4 10 2 0 "
      "pipe_logical_name5 pipe_physical_name1 0 12316 1 0 8 20 3 1 "
      "pipe_logical_name4 pipe_physical_name3 0 12342 0 1 4 10 3 0 "
      "3 90 "
      "_ZTS3CRCILi0EE 512 256 1 0 0 1 0 1 0 9 6 0 0 8 1 0 0 6 2 1 8 1024 0 3 6 "
      "0 0 8 1 0 0 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 "
      "6 0 0 8 1 0 0 6 0 0 8 1 0 0 0 0 1 2 64 4096 1 1 1 3 1 1 1 3 1 0 64 "
      "_ZTS11LZReductionILi0EE 0 256 1 0 0 0 0 1 0 5 6 0 0 8 1 0 0 6 2 1 8 "
      "1024 0 3 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 0 0 8 1 0 0 0 0 2 2 64 131072 65 "
      "32768 1 1 1 3 1 1 1 3 1 0 125 _ZTS13StaticHuffmanILi0EE 256 256 1 0 0 1 "
      "0 1 0 10 6 0 0 8 1 0 0 6 0 0 4 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 6 0 "
      "0 8 1 0 0 6 0 0 8 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 "
      "0 0 8 1 0 0 0 0 15 2 64 116 65 116 66 1152 67 512 68 256 69 120 70 120 "
      "71 1152 72 116 73 1152 74 512 75 256 76 120 77 120 78 1152 1 1 1 3 1 1 "
      "1 3 1 0"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(5, devdef.hostpipe_mappings.size());

  CHECK(devdef.hostpipe_mappings[0].logical_name == "pipe_logical_name1");
  CHECK(devdef.hostpipe_mappings[0].physical_name == "pipe_physical_name1");
  CHECK(devdef.hostpipe_mappings[0].implement_in_csr);
  CHECK(devdef.hostpipe_mappings[0].csr_address == "12345");
  CHECK(!devdef.hostpipe_mappings[0].is_read);
  CHECK(devdef.hostpipe_mappings[0].is_write);
  CHECK(devdef.hostpipe_mappings[0].pipe_width == 4);
  CHECK(devdef.hostpipe_mappings[0].pipe_depth == 10);
  CHECK(devdef.hostpipe_mappings[0].protocol == 0);
  CHECK(devdef.hostpipe_mappings[0].is_stall_free == 0);

  CHECK(devdef.hostpipe_mappings[1].logical_name == "pipe_logical_name2");
  CHECK(devdef.hostpipe_mappings[1].physical_name == "pipe_physical_name2");
  CHECK(!devdef.hostpipe_mappings[1].implement_in_csr);
  CHECK(devdef.hostpipe_mappings[1].csr_address == "12323");
  CHECK(devdef.hostpipe_mappings[1].is_read);
  CHECK(!devdef.hostpipe_mappings[1].is_write);
  CHECK(devdef.hostpipe_mappings[1].pipe_width == 8);
  CHECK(devdef.hostpipe_mappings[1].pipe_depth == 20);
  CHECK(devdef.hostpipe_mappings[1].protocol == 1);
  CHECK(devdef.hostpipe_mappings[1].is_stall_free == 1);

  CHECK(devdef.hostpipe_mappings[2].logical_name == "pipe_logical_name3");
  CHECK(devdef.hostpipe_mappings[2].physical_name == "pipe_physical_name1");
  CHECK(devdef.hostpipe_mappings[2].implement_in_csr);
  CHECK(devdef.hostpipe_mappings[2].csr_address == "12313");
  CHECK(!devdef.hostpipe_mappings[2].is_read);
  CHECK(devdef.hostpipe_mappings[2].is_write);
  CHECK(devdef.hostpipe_mappings[2].pipe_width == 4);
  CHECK(devdef.hostpipe_mappings[2].pipe_depth == 10);
  CHECK(devdef.hostpipe_mappings[2].protocol == 2);
  CHECK(devdef.hostpipe_mappings[2].is_stall_free == 0);

  CHECK(devdef.hostpipe_mappings[3].logical_name == "pipe_logical_name5");
  CHECK(devdef.hostpipe_mappings[3].physical_name == "pipe_physical_name1");
  CHECK(!devdef.hostpipe_mappings[3].implement_in_csr);
  CHECK(devdef.hostpipe_mappings[3].csr_address == "12316");
  CHECK(devdef.hostpipe_mappings[3].is_read);
  CHECK(!devdef.hostpipe_mappings[3].is_write);
  CHECK(devdef.hostpipe_mappings[3].pipe_width == 8);
  CHECK(devdef.hostpipe_mappings[3].pipe_depth == 20);
  CHECK(devdef.hostpipe_mappings[3].protocol == 3);
  CHECK(devdef.hostpipe_mappings[3].is_stall_free == 1);

  CHECK(devdef.hostpipe_mappings[4].logical_name == "pipe_logical_name4");
  CHECK(devdef.hostpipe_mappings[4].physical_name == "pipe_physical_name3");
  CHECK(!devdef.hostpipe_mappings[4].implement_in_csr);
  CHECK(devdef.hostpipe_mappings[4].csr_address == "12342");
  CHECK(!devdef.hostpipe_mappings[4].is_read);
  CHECK(devdef.hostpipe_mappings[4].is_write);
  CHECK(devdef.hostpipe_mappings[4].pipe_width == 4);
  CHECK(devdef.hostpipe_mappings[4].pipe_depth == 10);
  CHECK(devdef.hostpipe_mappings[4].protocol == 3);
  CHECK(devdef.hostpipe_mappings[4].is_stall_free == 0);
}

TEST(auto_configure, sideband_mappings) {
  const std::string config_str{
      "23 107 " RANDOM_HASH
      " pac_a10 0 1 13 DDR 2 2 24 1 2 0 4294967296 4294967296 8589934592 0 - 0 "
      "0 0 0 0 0 1 5 10 " // 5 Hostpipes, 10 in each mapping
      "pipe_logical_name1 pipe_physical_name1 1 12345 0 1 4 10 0 0 "
      "pipe_logical_name2 pipe_physical_name2 0 12323 1 0 8 20 1 1 "
      "pipe_logical_name3 pipe_physical_name1 1 12313 0 1 4 10 2 0 "
      "pipe_logical_name5 pipe_physical_name1 0 12316 1 0 8 20 3 1 "
      "pipe_logical_name4 pipe_physical_name3 0 12342 0 1 4 10 3 0 "
      "2 " // 2 Sideband groups
      "pipe_logical_name1 4 3 0 0 320 1 320 8 2 328 8 3 352 32 "
      "pipe_logical_name2 4 3 0 0 320 1 320 8 2 328 8 3 352 32 "
      // Kernel section starts below
      "3 90 "
      "_ZTS3CRCILi0EE 512 256 1 0 0 1 0 1 0 9 6 0 0 8 1 0 0 6 2 1 8 1024 0 3 6 "
      "0 0 8 1 0 0 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 "
      "6 0 0 8 1 0 0 6 0 0 8 1 0 0 0 0 1 2 64 4096 1 1 1 3 1 1 1 3 1 0 64 "
      "_ZTS11LZReductionILi0EE 0 256 1 0 0 0 0 1 0 5 6 0 0 8 1 0 0 6 2 1 8 "
      "1024 0 3 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 0 0 8 1 0 0 0 0 2 2 64 131072 65 "
      "32768 1 1 1 3 1 1 1 3 1 0 125 _ZTS13StaticHuffmanILi0EE 256 256 1 0 0 1 "
      "0 1 0 10 6 0 0 8 1 0 0 6 0 0 4 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 6 0 "
      "0 8 1 0 0 6 0 0 8 1 0 0 6 2 1 8 1024 0 2 6 0 0 8 1 0 0 6 0 0 8 1 0 0 6 "
      "0 0 8 1 0 0 0 0 15 2 64 116 65 116 66 1152 67 512 68 256 69 120 70 120 "
      "71 1152 72 116 73 1152 74 512 75 256 76 120 77 120 78 1152 1 1 1 3 1 1 "
      "1 3 1 0"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK_EQUAL(8, devdef.sideband_signal_mappings.size());

  CHECK(devdef.sideband_signal_mappings[0].logical_name ==
        "pipe_logical_name1");
  CHECK(devdef.sideband_signal_mappings[0].port_identifier == 0);
  CHECK(devdef.sideband_signal_mappings[0].port_offset == 0);
  CHECK(devdef.sideband_signal_mappings[0].sideband_size == 320);

  CHECK(devdef.sideband_signal_mappings[1].logical_name ==
        "pipe_logical_name1");
  CHECK(devdef.sideband_signal_mappings[1].port_identifier == 1);
  CHECK(devdef.sideband_signal_mappings[1].port_offset == 320);
  CHECK(devdef.sideband_signal_mappings[1].sideband_size == 8);

  CHECK(devdef.sideband_signal_mappings[2].logical_name ==
        "pipe_logical_name1");
  CHECK(devdef.sideband_signal_mappings[2].port_identifier == 2);
  CHECK(devdef.sideband_signal_mappings[2].port_offset == 328);
  CHECK(devdef.sideband_signal_mappings[2].sideband_size == 8);

  CHECK(devdef.sideband_signal_mappings[3].logical_name ==
        "pipe_logical_name1");
  CHECK(devdef.sideband_signal_mappings[3].port_identifier == 3);
  CHECK(devdef.sideband_signal_mappings[3].port_offset == 352);
  CHECK(devdef.sideband_signal_mappings[3].sideband_size == 32);

  CHECK(devdef.sideband_signal_mappings[4].logical_name ==
        "pipe_logical_name2");
  CHECK(devdef.sideband_signal_mappings[4].port_identifier == 0);
  CHECK(devdef.sideband_signal_mappings[4].port_offset == 0);
  CHECK(devdef.sideband_signal_mappings[4].sideband_size == 320);

  CHECK(devdef.sideband_signal_mappings[5].logical_name ==
        "pipe_logical_name2");
  CHECK(devdef.sideband_signal_mappings[5].port_identifier == 1);
  CHECK(devdef.sideband_signal_mappings[5].port_offset == 320);
  CHECK(devdef.sideband_signal_mappings[5].sideband_size == 8);

  CHECK(devdef.sideband_signal_mappings[6].logical_name ==
        "pipe_logical_name2");
  CHECK(devdef.sideband_signal_mappings[6].port_identifier == 2);
  CHECK(devdef.sideband_signal_mappings[6].port_offset == 328);
  CHECK(devdef.sideband_signal_mappings[6].sideband_size == 8);

  CHECK(devdef.sideband_signal_mappings[7].logical_name ==
        "pipe_logical_name2");
  CHECK(devdef.sideband_signal_mappings[7].port_identifier == 3);
  CHECK(devdef.sideband_signal_mappings[7].port_offset == 352);
  CHECK(devdef.sideband_signal_mappings[7].sideband_size == 32);
}

TEST(auto_configure, global_mem_id) {
  const std::string config_str{
      "23 46 " RANDOM_HASH " custom_ipa 0 3 "
      " 10 1 2 1 2 2199023255552 2233382993920 4 - 0 1"   // Global memory 1
      " 10 3 2 1 2 6597069766656 6631429505024 4 - 0 3"   // Global memory 2
      " 10 5 2 1 2 10995116277760 11029476016128 4 - 0 5" // Global memory 3
      " 0 0 0 0 0 1 0 0"
      " 0 2 133 _ZTS10SimpleVAddIiE 0 256 1 0 0 1 0 1 0 13 8 2 1 8"
      " 1 1 5 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 8"
      " 2 1 8 1 1 3 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0"
      " 0 0 8 2 1 8 1 1 1 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0"
      " 8 1 0 0 0 7 0 0 4 1 0 0 0 0 0 0 0 1 1 1 3 1 1 1 0 1 1 0 133 "
      " _ZTS10SimpleVAddIfE 256 256 1 0 0 1 0 1 0 13 8 2 1 8 1 1 5 0"
      " 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 8 2 1 8 1 1"
      " 3 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 8 2 1 8"
      " 1 1 1 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0 0 8 1 0 0 0 7 0"
      " 0 4 1 0 0 0 0 0 0 0 1 1 1 3 1 1 1 0 1 1 0"};

  acl_device_def_autodiscovery_t devdef;
  {
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    CHECK(result);
  }

  CHECK("1" == devdef.global_mem_defs[0].name);
  CHECK("1" == devdef.global_mem_defs[0].id);
  CHECK("3" == devdef.global_mem_defs[1].name);
  CHECK("3" == devdef.global_mem_defs[1].id);
  CHECK("5" == devdef.global_mem_defs[2].name);
  CHECK("5" == devdef.global_mem_defs[2].id);
}

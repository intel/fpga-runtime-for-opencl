// Copyright (C) 2011-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/*
  This is a fuzz test that is built based off the original unit test version.
  Many CHECK() are commented out because if these failed, then the program would
  exit immediately without correctly destructing non-primitive type variables
  (i.e. strings), which causes Address Sanitizer (ASAN) errors. However, in fuzz
  testing, it is very common for these CHECKs to fail as the input data is
  mutated. Therefore, CHECK is replaced with check_condition() from
  fuzz_testing.h. For most tests, we capture the content of the test inside a
  scope (i.e. {}), then use "break" to break out of scope when check_condition
  fails. Non-primitive variables defined inside the scope will be destructed at
  that time. Finally, we use a final CHECK to see if the test passes, as it is
  required by the unit test framework (CppUTest). This workaround prevents the
  fuzz testing infrastructure to catch ASAN errors that are introduced by the
  unit test framework instead of the source code.
*/

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
#include <string.h>
#include <string>

#include <acl_auto.h>
#include <acl_auto_configure.h>
#include <acl_auto_configure_version.h>
#include <acl_thread.h>
#include <acl_version.h>

#include "acl_fuzz_test.h"

#include "../fuzz_src/fuzz_testing.h"
#include <vector>

TEST_GROUP(auto_configure) {
public:
  // preload_data is a function that is specific to fuzz test only (See
  // fuzz_testing.h)
  void setup() { preload_data("acl_auto_configure_fuzz_test"); }
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
  char *err_str_c;
  {
    std::string autodiscovery =
        load_fuzzed_value("auto_configure", "simple", "autodiscovery");
    std::string err_str;
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   autodiscovery, m_device_def.autodiscovery_def, err_str));
    // Using char* to save err_str because char* is primitive type which
    // will be destructed properly when CHECK fails
    err_str_c = const_cast<char *>(err_str.c_str());
  }

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
  {
    std::string autodiscovery2 =
        load_fuzzed_value("auto_configure", "simple", "autodiscovery2");
    std::string err_str = std::string(err_str_c);
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   autodiscovery2, m_device_def.autodiscovery_def, err_str));
  }

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
  {
    std::string autodiscovery3 =
        load_fuzzed_value("auto_configure", "simple", "autodiscovery3");
    std::string err_str = std::string(err_str_c);
    ACL_LOCKED(parsed = acl_load_device_def_from_str(
                   autodiscovery3, m_device_def.autodiscovery_def, err_str));
  }

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
  bool check = true;
  {
    // From example_designs/merge_sort with extra fields at the end of each
    // sections and subsections to check forward compatibility

    std::string str = load_fuzzed_value("auto_configure",
                                        "many_ok_forward_compatibility", "str");
    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err_str;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     str, device_def.autodiscovery_def, err_str));
      if (!check_condition(1 == parsed, check))
        break;

      if (!check_condition("a10gx" == device_def.autodiscovery_def.name, check))
        break;
      if (!check_condition("sample40byterandomhash000000000000000000" ==
                               device_def.autodiscovery_def.binary_rand_hash,
                           check))
        break;

      if (!check_condition(47 == (int)device_def.autodiscovery_def.accel.size(),
                           check))
        break;
      if (!check_condition(
              47 == (int)device_def.autodiscovery_def.hal_info.size(), check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, many_limit_check) {
  bool check = true;
  {
    std::string str =
        load_fuzzed_value("auto_configure", "many_limit_check", "str");
    // Verify that we can handle 75 kernels
    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     str, device_def.autodiscovery_def, err));
      if (!check_condition(1 == parsed, check))
        break;

      // But we should write 75 kernels to the requested physical_device_id
      // entry.
      if (!check_condition(75 == (int)device_def.autodiscovery_def.accel.size(),
                           check))
        break;
      if (!check_condition(
              75 == (int)device_def.autodiscovery_def.hal_info.size(), check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, bad_config) {
  bool check = true;
  {
    std::string str1 =
        load_fuzzed_value("auto_configure", "bad_config", "str1");
    std::string str2 =
        "Error: The accelerator hardware currently programmed is "
        "incompatible with this\nversion of the runtime (" ACL_VERSION
        " Commit " ACL_GIT_COMMIT
        "). Recompile the hardware with\nthe same version of "
        "the compiler and program that onto the board.\n";
    std::string str3 =
        load_fuzzed_value("auto_configure", "bad_config", "str3");
    std::string str4 =
        "FAILED to read auto-discovery string at byte 132: kernel cannot be "
        "workitem-invariant while it is workgroup-variant. Full "
        "auto-discovery string value is " VERSIONIDTOSTR(
            ACL_AUTO_CONFIGURE_VERSIONID) " 15 "
                                          "sample40byterandomhash000000000000"
                                          "000000 a10gx 0 1 7 DDR 2 1 2 0 "
                                          "2147483648 0 0 0 0 1 "
                                          "31 external_sort_stage_0 0 128 1 "
                                          "0 0 0 1 1 0 1 6 0 0 4 1 0 0 0 0 0 "
                                          "0 1 1 1 3 1 1 1 3 1\n";
    std::vector<std::string> strs = {str1, str2, str3, str4};

    for (unsigned istr = 0; istr < strs.size(); istr += 2) {
      int parsed;
      std::string err;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     strs[istr], m_device_def.autodiscovery_def, err));
      if (!check_condition(0 == parsed, check))
        break;

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

      if (!check_condition(expect == err, check))
        break;

      if (!check_condition(
              0 == (int)m_device_def.autodiscovery_def.accel.size(), check))
        break;
      if (!check_condition(
              0 == (int)m_device_def.autodiscovery_def.hal_info.size(), check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, multi_mem_config) {
  bool check = true;
  {
    std::string str =
        load_fuzzed_value("auto_configure", "multi_mem_config", "str");

    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err_str;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     str, device_def.autodiscovery_def, err_str));

      if (!check_condition(1 == parsed, check))
        break;

      if (!check_condition("pcie385n_a7" == device_def.autodiscovery_def.name,
                           check))
        break;
      if (!check_condition("sample40byterandomhash000000000000000000" ==
                               device_def.autodiscovery_def.binary_rand_hash,
                           check))
        break;

      if (!check_condition(
              4 == device_def.autodiscovery_def.num_global_mem_systems, check))
        break;

      if (!check_condition(
              "SVM" == device_def.autodiscovery_def.global_mem_defs[0].name,
              check))
        break;
      if (!check_condition(
              ACL_GLOBAL_MEM_SHARED_VIRTUAL ==
                  device_def.autodiscovery_def.global_mem_defs[0].type,
              check))
        break;

      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[0]
                                    .burst_interleaved,
                           check))
        break;
      if (!check_condition(
              0 == device_def.autodiscovery_def.global_mem_defs[0].config_addr,
              check))
        break;
      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[0]
                                    .num_global_banks,
                           check))
        break;
      if (!check_condition("" == device_def.autodiscovery_def.global_mem_defs[0]
                                     .primary_interface,
                           check))
        break;
      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[0]
                                    .can_access_list.size(),
                           check))
        break;
      if (!check_condition("SVM2" ==
                               device_def.autodiscovery_def.global_mem_defs[0]
                                   .can_access_list.at(0),
                           check))
        break;

      if (!check_condition(
              "DDR" == device_def.autodiscovery_def.global_mem_defs[1].name,
              check))
        break;
      if (!check_condition(
              ACL_GLOBAL_MEM_DEVICE_PRIVATE ==
                  device_def.autodiscovery_def.global_mem_defs[1].type,
              check))
        break;
      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[1]
                                    .burst_interleaved,
                           check))
        break;
      if (!check_condition(
              0x18 ==
                  device_def.autodiscovery_def.global_mem_defs[1].config_addr,
              check))
        break;
      if (!check_condition(2 == device_def.autodiscovery_def.global_mem_defs[1]
                                    .num_global_banks,
                           check))
        break;
      if (!check_condition(ACL_GLOBAL_MEM_UNDEFINED_ALLOCATION ==
                               device_def.autodiscovery_def.global_mem_defs[1]
                                   .allocation_type,
                           check))
        break;

      if (!check_condition(
              "QDR" == device_def.autodiscovery_def.global_mem_defs[2].name,
              check))
        break;
      if (!check_condition(
              ACL_GLOBAL_MEM_DEVICE_PRIVATE ==
                  device_def.autodiscovery_def.global_mem_defs[2].type,
              check))
        break;
      if (!check_condition(0 == device_def.autodiscovery_def.global_mem_defs[2]
                                    .burst_interleaved,
                           check))
        break;
      if (!check_condition(
              0x30 ==
                  device_def.autodiscovery_def.global_mem_defs[2].config_addr,
              check))
        break;
      if (!check_condition(4 == device_def.autodiscovery_def.global_mem_defs[2]
                                    .num_global_banks,
                           check))
        break;
      if (!check_condition(ACL_GLOBAL_MEM_DEVICE_ALLOCATION ==
                               device_def.autodiscovery_def.global_mem_defs[2]
                                   .allocation_type,
                           check))
        break;

      if (!check_condition(
              "SVM2" == device_def.autodiscovery_def.global_mem_defs[3].name,
              check))
        break;
      if (!check_condition(
              ACL_GLOBAL_MEM_SHARED_VIRTUAL ==
                  device_def.autodiscovery_def.global_mem_defs[3].type,
              check))
        break;
      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[3]
                                    .burst_interleaved,
                           check))
        break;
      if (!check_condition(
              0 == device_def.autodiscovery_def.global_mem_defs[3].config_addr,
              check))
        break;
      if (!check_condition(1 == device_def.autodiscovery_def.global_mem_defs[3]
                                    .num_global_banks,
                           check))
        break;
      if (!check_condition("SVM" ==
                               device_def.autodiscovery_def.global_mem_defs[3]
                                   .primary_interface,
                           check))
        break;
      if (!check_condition(0 == device_def.autodiscovery_def.global_mem_defs[3]
                                    .can_access_list.size(),
                           check))
        break;
      if (!check_condition(2 == (int)device_def.autodiscovery_def.accel.size(),
                           check))
        break;
      if (!check_condition(
              2 == (int)device_def.autodiscovery_def.hal_info.size(), check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, kernel_arg_info) {
  bool check = true;
  {
    std::string str1 =
        load_fuzzed_value("auto_configure", "kernel_arg_info", "str1");
    std::string str2 =
        load_fuzzed_value("auto_configure", "kernel_arg_info", "str2");
    std::vector<std::string> strs = {str1, str2};

    // kernel arg info available
    {
      std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
      for (auto &device_def : device_defs) {
        int parsed;
        std::string err_str;
        ACL_LOCKED(parsed = acl_load_device_def_from_str(
                       strs[0], device_def.autodiscovery_def, err_str));
        if (!check_condition(1 == parsed, check))
          break;

        if (!check_condition("a10gx" == device_def.autodiscovery_def.name,
                             check))
          break;
        if (!check_condition("sample40byterandomhash000000000000000000" ==
                                 device_def.autodiscovery_def.binary_rand_hash,
                             check))
          break;

        if (!check_condition(
                1 == device_def.autodiscovery_def.num_global_mem_systems,
                check))
          break;
        if (!check_condition(
                "DDR" == device_def.autodiscovery_def.global_mem_defs[0].name,
                check))
          break;

        if (!check_condition(
                2 == (int)device_def.autodiscovery_def.accel.size(), check))
          break;
        if (!check_condition(
                2 == (int)device_def.autodiscovery_def.hal_info.size(), check))
          break;
        if (!check_condition(2 == (int)device_def.autodiscovery_def.accel[0]
                                      .iface.args.size(),
                             check))
          break;
        if (!check_condition(2 == (int)device_def.autodiscovery_def.accel[1]
                                      .iface.args.size(),
                             check))
          break;

        if (!check_condition(
                "arg_one" ==
                    device_def.autodiscovery_def.accel[0].iface.args[0].name,
                check))
          if (!check_condition(
                  check &=
                  "type_one" ==
                  device_def.autodiscovery_def.accel[0].iface.args[0].type_name,
                  check))
            break;
        if (!check_condition(1 == device_def.autodiscovery_def.accel[0]
                                      .iface.args[0]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "arg_two" ==
                    device_def.autodiscovery_def.accel[0].iface.args[1].name,
                check))
          break;
        if (!check_condition("type_two" == device_def.autodiscovery_def.accel[0]
                                               .iface.args[1]
                                               .type_name,
                             check))
          break;
        if (!check_condition(2 == device_def.autodiscovery_def.accel[0]
                                      .iface.args[1]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "arg_three" ==
                    device_def.autodiscovery_def.accel[1].iface.args[0].name,
                check))
          break;
        if (!check_condition(
                "arg_three" ==
                    device_def.autodiscovery_def.accel[1].iface.args[0].name,
                check))
          break;
        if (!check_condition("type_three" ==
                                 device_def.autodiscovery_def.accel[1]
                                     .iface.args[0]
                                     .type_name,
                             check))
          break;
        if (!check_condition(1 == device_def.autodiscovery_def.accel[1]
                                      .iface.args[0]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "arg_four" ==
                    device_def.autodiscovery_def.accel[1].iface.args[1].name,
                check))
          break;
        if (!check_condition("type_four" ==
                                 device_def.autodiscovery_def.accel[1]
                                     .iface.args[1]
                                     .type_name,
                             check))
          break;
        if (!check_condition(2 == device_def.autodiscovery_def.accel[1]
                                      .iface.args[1]
                                      .access_qualifier,
                             check))
          break;
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
        if (!check_condition(1 == parsed, check))
          break;

        if (!check_condition("a10gx" == device_def.autodiscovery_def.name,
                             check))
          break;
        if (!check_condition("sample40byterandomhash000000000000000000" ==
                                 device_def.autodiscovery_def.binary_rand_hash,
                             check))
          break;

        if (!check_condition(
                1 == device_def.autodiscovery_def.num_global_mem_systems,
                check))
          break;
        if (!check_condition(
                "DDR" == device_def.autodiscovery_def.global_mem_defs[0].name,
                check))
          break;

        if (!check_condition(
                2 == (int)device_def.autodiscovery_def.accel.size(), check))
          break;
        if (!check_condition(
                2 == (int)device_def.autodiscovery_def.hal_info.size(), check))
          break;
        if (!check_condition(2 == (int)device_def.autodiscovery_def.accel[0]
                                      .iface.args.size(),
                             check))
          break;
        if (!check_condition(2 == (int)device_def.autodiscovery_def.accel[1]
                                      .iface.args.size(),
                             check))
          break;

        if (!check_condition(
                "" == device_def.autodiscovery_def.accel[0].iface.args[0].name,
                check))
          break;
        if (!check_condition("" == device_def.autodiscovery_def.accel[0]
                                       .iface.args[0]
                                       .type_name,
                             check))
          break;
        if (!check_condition(0 == device_def.autodiscovery_def.accel[0]
                                      .iface.args[0]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "" == device_def.autodiscovery_def.accel[0].iface.args[1].name,
                check))
          break;
        if (!check_condition("" == device_def.autodiscovery_def.accel[0]
                                       .iface.args[1]
                                       .type_name,
                             check))
          break;
        if (!check_condition(0 == device_def.autodiscovery_def.accel[0]
                                      .iface.args[1]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "" == device_def.autodiscovery_def.accel[1].iface.args[0].name,
                check))
          break;
        if (!check_condition("" == device_def.autodiscovery_def.accel[1]
                                       .iface.args[0]
                                       .type_name,
                             check))
          break;
        if (!check_condition(0 == device_def.autodiscovery_def.accel[1]
                                      .iface.args[0]
                                      .access_qualifier,
                             check))
          break;

        if (!check_condition(
                "" == device_def.autodiscovery_def.accel[1].iface.args[1].name,
                check))
          break;
        if (!check_condition("" == device_def.autodiscovery_def.accel[1]
                                       .iface.args[1]
                                       .type_name,
                             check))
          break;
        if (!check_condition(0 == device_def.autodiscovery_def.accel[1]
                                      .iface.args[1]
                                      .access_qualifier,
                             check))
          break;
      }
    }
  }
  CHECK(check);
}

TEST(auto_configure, hostpipe_basic) {
  bool check = true;
  {
    std::string str(
        load_fuzzed_value("auto_configure", "hostpipe_basic", "str"));

    std::vector<acl_device_def_t> device_defs(ACL_MAX_DEVICE);
    for (auto &device_def : device_defs) {
      int parsed;
      std::string err_str;
      ACL_LOCKED(parsed = acl_load_device_def_from_str(
                     str, device_def.autodiscovery_def, err_str));

      if (!check_condition(check &= 1 == parsed, check))
        break;
      if (!check_condition(check &= "a10gx_hostpipe" ==
                                    device_def.autodiscovery_def.name,
                           check))
        break;
      if (!check_condition(check &=
                           "sample40byterandomhash000000000000000000" ==
                           device_def.autodiscovery_def.binary_rand_hash,
                           check))
        break;

      if (!check_condition(
              check &= 1 == device_def.autodiscovery_def.num_global_mem_systems,
              check))
        break;
      if (!check_condition(
              check &= "foo" == device_def.autodiscovery_def.hal_info[0].name,
              check))
        break;
      if (!check_condition(check &=
                           "DDR" ==
                           device_def.autodiscovery_def.global_mem_defs[0].name,
                           check))
        break;

      if (!check_condition(check &=
                           1 == (int)device_def.autodiscovery_def.accel.size(),
                           check))
        break;
      if (!check_condition(
              check &= 1 == (int)device_def.autodiscovery_def.hal_info.size(),
              check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, streaming_basic) {
  bool check = 1;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str =
        load_fuzzed_value("auto_configure", "streaming_basic", "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;

    if (!check_condition(check &= 1 == devdef.accel.size(), check))
      break;

    if (!check_condition(check &= !devdef.accel[0].is_sycl_compile, check))
      break;
    if (!check_condition(
            check &= devdef.accel[0].streaming_control_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS3CRCILi0EE_streaming_start" ==
                                  devdef.accel[0].streaming_control_info.start,
                         check))
      break;
    if (!check_condition(check &= "k0_ZTS3CRCILi0EE_streaming_done" ==
                                  devdef.accel[0].streaming_control_info.done,
                         check))
      break;

    const auto &args = devdef.accel[0].iface.args;
    if (!check_condition(check &= 9 == args.size(), check))
      break;

    if (!check_condition(check &= args[0].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS3CRCILi0EE_arg0" ==
                                  args[0].streaming_arg_info.interface_name,
                         check))
      break;

    if (!check_condition(check &= args[1].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS3CRCILi0EE_arg1" ==
                                  args[1].streaming_arg_info.interface_name,
                         check))
      break;

    if (!check_condition(check &= args[2].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS3CRCILi0EE_arg2" ==
                                  args[2].streaming_arg_info.interface_name,
                         check))
      break;

    for (size_t i = 3; i < args.size(); ++i) {
      if (!check_condition(check &= !args[i].streaming_arg_info_available,
                           check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, one_streaming_arg_and_streaming_kernel) {
  bool check = 1;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str = load_fuzzed_value(
        "auto_configure", "one_streaming_arg_and_streaming_kernel",
        "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;

    if (!check_condition(check &= 1 == devdef.accel.size(), check))
      break;

    if (!check_condition(
            check &= devdef.accel[0].streaming_control_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_streaming_start" ==
                                  devdef.accel[0].streaming_control_info.start,
                         check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_streaming_done" ==
                                  devdef.accel[0].streaming_control_info.done,
                         check))
      break;

    const auto &args = devdef.accel[0].iface.args;
    if (!check_condition(check &= 8 == args.size(), check))
      break;

    if (!check_condition(check &= !args[0].streaming_arg_info_available, check))
      break;

    if (!check_condition(check &= args[1].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_arg1" ==
                                  args[1].streaming_arg_info.interface_name,
                         check))
      break;

    for (size_t i = 2; i < args.size(); ++i) {
      if (!check_condition(check &= !args[i].streaming_arg_info_available,
                           check))
        break;
    }
  }

  CHECK(check);
}

TEST(auto_configure, two_streaming_args_and_streaming_kernel) {
  bool check = true;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str = load_fuzzed_value(
        "auto_configure", "two_streaming_args_and_streaming_kernel",
        "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;

    if (!check_condition(check &= 1 == devdef.accel.size(), check))
      break;

    if (!check_condition(check &= devdef.accel[0].is_sycl_compile, check))
      break;
    if (!check_condition(
            check &= devdef.accel[0].streaming_control_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_streaming_start" ==
                                  devdef.accel[0].streaming_control_info.start,
                         check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_streaming_done" ==
                                  devdef.accel[0].streaming_control_info.done,
                         check))
      break;

    const auto &args = devdef.accel[0].iface.args;
    if (!check_condition(check &= 8 == args.size(), check))
      break;

    if (!check_condition(check &= args[0].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_arg0" ==
                                  args[0].streaming_arg_info.interface_name,
                         check))
      break;

    if (!check_condition(check &= args[1].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_arg1" ==
                                  args[1].streaming_arg_info.interface_name,
                         check))
      break;

    for (size_t i = 2; i < args.size(); ++i) {
      if (!check_condition(check &= !args[i].streaming_arg_info_available,
                           check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, two_streaming_args_and_non_streaming_kernel) {
  bool check = 1;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str = load_fuzzed_value(
        "auto_configure", "two_streaming_args_and_non_streaming_kernel",
        "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;

    if (!check_condition(check &= 1 == devdef.accel.size(), check))
      break;

    if (!check_condition(check &= devdef.accel[0].is_sycl_compile, check))
      break;
    if (!check_condition(
            check &= !devdef.accel[0].streaming_control_info_available, check))
      break;

    const auto &args = devdef.accel[0].iface.args;
    if (!check_condition(check &= 8 == args.size(), check))
      break;

    if (!check_condition(check &= args[0].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_arg0" ==
                                  args[0].streaming_arg_info.interface_name,
                         check))
      break;

    if (!check_condition(check &= args[1].streaming_arg_info_available, check))
      break;
    if (!check_condition(check &= "k0_ZTS15binomial_kernel_arg1" ==
                                  args[1].streaming_arg_info.interface_name,
                         check))
      break;

    for (size_t i = 2; i < args.size(); ++i) {
      if (!check_condition(check &= !args[i].streaming_arg_info_available,
                           check))
        break;
    }
  }
  CHECK(check);
}

TEST(auto_configure, cra_ring_root_not_exist) {
  bool check = true;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str = load_fuzzed_value(
        "auto_configure", "cra_ring_root_not_exist", "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result = acl_load_device_def_from_str(std::string(config_str),
                                                     devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;
    if (!check_condition(check &= 0 == devdef.cra_ring_root_exist, check))
      break;
  }
  CHECK(check);
}

TEST(auto_configure, cra_ring_root_exist) {
  bool check = true;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str = load_fuzzed_value(
        "auto_configure", "cra_ring_root_exist", "config_str");
    acl_device_def_autodiscovery_t devdef;

    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;
    if (!check_condition(check &= 1 == devdef.cra_ring_root_exist, check))
      break;
  }
  CHECK(check);
}

TEST(auto_configure, hostpipe_mappings) {
  bool check = true;
  // Dummy for loop
  for (int i = 0; i < 1; i++) {
    std::string config_str =
        load_fuzzed_value("auto_configure", "hostpipe_mappings", "config_str");
    acl_device_def_autodiscovery_t devdef;
    bool result;
    std::string err_str;
    ACL_LOCKED(result =
                   acl_load_device_def_from_str(config_str, devdef, err_str));
    std::cerr << err_str;
    if (!check_condition(check &= result, check))
      break;

    if (!check_condition(check &= 5 == devdef.hostpipe_mappings.size(), check))
      break;

    if (!check_condition(check &= devdef.hostpipe_mappings[0].logical_name ==
                                  "pipe_logical_name1",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[0].physical_name ==
                                  "pipe_physical_name1",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[0].implement_in_csr,
                         check))
      break;
    if (!check_condition(
            check &= devdef.hostpipe_mappings[0].csr_address == "12345", check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[0].is_read, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[0].is_write, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[0].pipe_width == 4,
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[0].pipe_depth == 10,
                         check))
      break;

    if (!check_condition(check &= devdef.hostpipe_mappings[1].logical_name ==
                                  "pipe_logical_name2",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[1].physical_name ==
                                  "pipe_physical_name2",
                         check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[1].implement_in_csr,
                         check))
      break;
    if (!check_condition(
            check &= devdef.hostpipe_mappings[1].csr_address == "12323", check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[1].is_read, check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[1].is_write, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[1].pipe_width == 8,
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[1].pipe_depth == 20,
                         check))
      break;

    if (!check_condition(check &= devdef.hostpipe_mappings[2].logical_name ==
                                  "pipe_logical_name3",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[2].physical_name ==
                                  "pipe_physical_name1",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[2].implement_in_csr,
                         check))
      break;
    if (!check_condition(
            check &= devdef.hostpipe_mappings[2].csr_address == "12313", check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[2].is_read, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[2].is_write, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[2].pipe_width == 4,
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[2].pipe_depth == 10,
                         check))
      break;

    if (!check_condition(check &= devdef.hostpipe_mappings[3].logical_name ==
                                  "pipe_logical_name5",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[3].physical_name ==
                                  "pipe_physical_name1",
                         check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[3].implement_in_csr,
                         check))
      break;
    if (!check_condition(
            check &= devdef.hostpipe_mappings[3].csr_address == "12316", check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[3].is_read, check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[3].is_write, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[3].pipe_width == 8,
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[3].pipe_depth == 20,
                         check))
      break;

    if (!check_condition(check &= devdef.hostpipe_mappings[4].logical_name ==
                                  "pipe_logical_name4",
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[4].physical_name ==
                                  "pipe_physical_name3",
                         check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[4].implement_in_csr,
                         check))
      break;
    if (!check_condition(
            check &= devdef.hostpipe_mappings[4].csr_address == "12342", check))
      break;
    if (!check_condition(check &= !devdef.hostpipe_mappings[4].is_read, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[4].is_write, check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[4].pipe_width == 4,
                         check))
      break;
    if (!check_condition(check &= devdef.hostpipe_mappings[4].pipe_depth == 10,
                         check))
      break;
  }
  CHECK(check);
}

// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_TEST_H
#define ACL_TEST_H

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#include <CppUTest/SimpleString.h>

#include <CL/opencl.h>

#include <acl_auto.h>

#include <MMD/aocl_mmd.h>

// Use the default board for the SDK, but with less memory for unit test
// purposes. Must also update the table of hashes in acl_program_test.cpp
#if ACDS_PRO == 0
#define ACLTEST_DEFAULT_BOARD "s5_net_small"
#else
#define ACLTEST_DEFAULT_BOARD "a10_ref_small"
#endif

// Default setup and teardown.
void acl_test_setup_generic_system();
void acl_test_setup_empty_system();
void acl_test_setup_sample_default_board_system(void);
void acl_test_teardown_generic_system(void);
void acl_test_teardown_system(void);
void acl_test_teardown_sample_default_board_system(void);
void acl_hal_test_setup_generic_system(void);
void acl_hal_test_teardown_generic_system(void);
void acl_test_run_standard_teardown_checks();

void acl_test_unsetenv(const char *var);
void acl_test_setenv(const char *var, const char *value);

cl_context_properties *acl_test_context_prop_preloaded_binary_only(void);

const unsigned char *acl_test_get_example_binary(size_t *binary_len);

SimpleString StringFrom(cl_uint x);
SimpleString StringFrom(cl_ulong x);
SimpleString StringFrom(size_t x);

#ifdef _WIN32
SimpleString StringFrom(intptr_t x);
#endif

// Context error notify callback function.
void CL_CALLBACK acl_test_notify_print(const char *errinfo,
                                       const void *private_info, size_t cb,
                                       void *user_data);

#define ACL_LOCKED(...)                                                        \
  do {                                                                         \
    acl_lock();                                                                \
    { __VA_ARGS__; }                                                           \
    acl_unlock();                                                              \
  } while (0)

/* CAUTION.  These are only used in self-tests.
 * The runtime does not use these constants any more.
 */
#define ACL_MAX_EVENT (1024 * 16)
#define ACL_MAX_COMMAND                                                        \
  ACL_MAX_EVENT /* each event refers to a command. if same number of them,     \
                   then there is less worry about running out of commands when \
                   creating an event */

typedef struct mem_data_s mem_data_t;

typedef struct mem_data_s {
  int mmd_interface;
  size_t offset;
  size_t size;
  void *data;
  mem_data_t *next;
} mem_data_t;
typedef struct {
  cl_bool is_active;
  mem_data_t *mem_data;
  aocl_mmd_interrupt_handler_fn kernel_interrupt;
  void *interrupt_user_data;
  aocl_mmd_status_handler_fn kernel_status;
  void *status_user_data;
} acl_hal_device_test;

// This must match the define in acl_kernel_if.c
#define KERNEL_VERSION_ID (0xa0c00001)

// These must match the defines in acl_kernel_if.c
#define OFFSET_VERSION_ID ((dev_addr_t)0x0000)
#define OFFSET_KERNEL_CRA_SEGMENT ((dev_addr_t)0x0020)
#define OFFSET_SW_RESET ((dev_addr_t)0x0030)
// Default mem_org address.
// Runtime is now using one loaded from autodiscovery,
// rather than hard coded value.
// For tests, autodiscovery will still have the default value.
#define OFFSET_MEM_ORG ((dev_addr_t)0x0018)
#define OFFSET_KERNEL_CRA ((dev_addr_t)0x1000)
#define OFFSET_CONFIGURATION_ROM ((dev_addr_t)0x2000)

// These must match the defines in acl_pll.c
#define OFFSET_ROM ((dev_addr_t)0x400)
#define OFFSET_RECONFIG_CTRL ((dev_addr_t)0x200)
#define OFFSET_COUNTER ((dev_addr_t)0x100)
#define OFFSET_RESET ((dev_addr_t)0x110)
#define OFFSET_LOCK ((dev_addr_t)0x120)

// This must match the define in acl_pll.c
#define MAX_KNOWN_SETTINGS 100

#endif

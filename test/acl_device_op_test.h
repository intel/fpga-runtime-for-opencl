// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_DEVICE_OP_TEST_H
#define ACL_DEVICE_OP_TEST_H

#include <CL/opencl.h>

#include <acl.h>
#include <acl_types.h>

struct acl_device_op_test_ctx_t {
  enum { MAX_OPS = 1000 };
  void (*launch_kernel)(void *, acl_device_op_t *);
  void (*transfer_buffer)(void *, acl_device_op_t *);
  void (*process_printf)(void *, acl_device_op_t *);
  void (*program_device)(void *, acl_device_op_t *);
  void (*log_update)(void *, acl_device_op_t *, int);
  void *sub_user_data;
  acl_device_op_t before[MAX_OPS];
  acl_device_op_t after[MAX_OPS];
  int num_ops;
};

void acl_dot_push(acl_device_op_test_ctx_t *ctx, acl_device_op_queue_t *doq);

void acl_dot_launch_kernel(void *, acl_device_op_t *);
void acl_dot_transfer_buffer(void *, acl_device_op_t *);
void acl_dot_process_printf(void *, acl_device_op_t *);
void acl_dot_program_device(void *, acl_device_op_t *);
void acl_dot_log_update(void *, acl_device_op_t *, int);

#endif

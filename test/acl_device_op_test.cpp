// Copyright (C) 2012-2021 Intel Corporation
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
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_types.h>
#include <acl_util.h>

#include "acl_device_op_test.h"
#include "acl_test.h"

///////// Utilities for logging device op stuff.
//
//

void acl_dot_launch_kernel(void *the_ctx, acl_device_op_t *op) {
  acl_device_op_test_ctx_t *ctx = (acl_device_op_test_ctx_t *)the_ctx;
  int idx = ctx->num_ops++;
  acl_print_debug_msg("devop[%d] op[%d] launch kernel\n", idx, op->id);
  ctx->before[idx] = *op;
  if (ctx->launch_kernel)
    ctx->launch_kernel(ctx->sub_user_data, op);
  ctx->after[idx] = *op;
}

void acl_dot_transfer_buffer(void *the_ctx, acl_device_op_t *op) {
  acl_device_op_test_ctx_t *ctx = (acl_device_op_test_ctx_t *)the_ctx;
  int idx = ctx->num_ops++;
  acl_print_debug_msg("devop[%d] op[%d] transfer buffer\n", idx, op->id);
  ctx->before[idx] = *op;
  if (ctx->transfer_buffer)
    ctx->transfer_buffer(ctx->sub_user_data, op);
  ctx->after[idx] = *op;
}

void acl_dot_process_printf(void *the_ctx, acl_device_op_t *op) {
  acl_device_op_test_ctx_t *ctx = (acl_device_op_test_ctx_t *)the_ctx;
  int idx = ctx->num_ops++;
  acl_print_debug_msg("devop[%d] op[%d] process printf\n", idx, op->id);
  ctx->before[idx] = *op;
  if (ctx->process_printf)
    ctx->process_printf(ctx->sub_user_data, op);
  ctx->after[idx] = *op;
}

void acl_dot_program_device(void *the_ctx, acl_device_op_t *op) {
  acl_device_op_test_ctx_t *ctx = (acl_device_op_test_ctx_t *)the_ctx;
  int idx = ctx->num_ops++;
  acl_print_debug_msg("devop[%d] op[%d] program device\n", idx, op->id);
  ctx->before[idx] = *op;
  if (ctx->program_device)
    ctx->program_device(ctx->sub_user_data, op);
  ctx->after[idx] = *op;
}

static const char *tn[] = {"NONE",
                           "KERNEL",
                           "MEM_TRANSFER_READ",
                           "MEM_TRANSFER_WRITE",
                           "MEM_TRANSFER_COPY",
                           "REPROGRAM",
                           "MEM_MIGRATION",
                           "<num_dev_op_types>"};

static const char *status_name[] = {"CL_COMPLETE", "CL_RUNNING", "CL_SUBMITTED",
                                    "CL_QUEUED", "ACL_PROPOSED"};

void acl_dot_log_update(void *the_ctx, acl_device_op_t *op, int new_status) {
  acl_device_op_test_ctx_t *ctx = (acl_device_op_test_ctx_t *)the_ctx;
  int idx = ctx->num_ops++;
  CHECK(0 <= new_status);
  CHECK(new_status < ACL_NUM_DEVICE_OP_TYPES);
  acl_print_debug_msg("devop[%d] op[%d] update %s status %d %s\n", idx, op->id,
                      tn[op->info.type], new_status,
                      (new_status >= 0 ? status_name[new_status] : "<err>"));
  ctx->before[idx] = *op;
  if (ctx->log_update)
    ctx->log_update(ctx->sub_user_data, op, new_status);
  ctx->after[idx] = *op;
}

void acl_dot_push(acl_device_op_test_ctx_t *result,
                  acl_device_op_queue_t *doq) {
  result->num_ops = 0;

  result->sub_user_data = doq->user_data;
  doq->user_data = result;

  result->launch_kernel = doq->launch_kernel;
  doq->launch_kernel = acl_dot_launch_kernel;
  result->transfer_buffer = doq->transfer_buffer;
  doq->transfer_buffer = acl_dot_transfer_buffer;
  result->process_printf = doq->process_printf;
  doq->process_printf = acl_dot_process_printf;
  result->program_device = doq->program_device;
  doq->program_device = acl_dot_program_device;
  result->log_update = doq->log_update;
  doq->log_update = acl_dot_log_update;
}

/////////
// dumper
static void l_dump_op(const char *str, acl_device_op_t *op) {
  if (op) {
    acl_print_debug_msg(" %s op[%d] { %s %d %s .link %d }\n", (str ? str : ""),
                        op->id, tn[op->info.type], op->status,
                        (op->status >= 0 ? status_name[op->status] : "<err>"),
                        op->link);
  } else {
    acl_print_debug_msg(" %s op NULL\n", (str ? str : ""));
  }
}

///////// Tests
//

// events used to test multidevice configurations
static const unsigned int EVENT_NUM = 6;
static struct _cl_event myevents[EVENT_NUM] = {{0}};

TEST_GROUP(device_op) {
  virtual void setup() {
    acl_mutex_wrapper.lock();
    acl_test_setup_generic_system();
    acl_init_device_op_queue(&m_doq);
    clear_queue_callbacks(&m_doq);
    load();

    // different contexts allow to test multi-device configurations
    // as well as full duplex vs half duplex configurations
    myevents[0].context = m_context;
    myevents[1].context = m_context_fd;
    myevents[2].context = m_context_hd_hd;
    myevents[3].context = m_context_hd_fd;
    myevents[4].context = m_context_fd_hd;
    myevents[5].context = m_context_fd_fd;
  }

  virtual void teardown() {
    unload();
    acl_mutex_wrapper.unlock();
    acl_test_teardown_generic_system();
    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    CHECK(acl_platform_is_valid(m_platform));
    CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, 5,
                                           &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 5);

    cl_int status;
    m_context =
        clCreateContext(0, 1, &m_device[0], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context);
    CHECK_EQUAL(1, m_context->device[0]->def.max_inflight_mem_ops);

    m_context_fd =
        clCreateContext(0, 1, &m_device[1], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_fd);
    CHECK_EQUAL(2, m_context_fd->device[0]->def.max_inflight_mem_ops);

    m_context_hd_hd =
        clCreateContext(0, 2, &m_device[3], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_hd_hd);
    CHECK_EQUAL(1, m_context_hd_hd->device[0]->def.max_inflight_mem_ops);
    CHECK_EQUAL(1, m_context_hd_hd->device[1]->def.max_inflight_mem_ops);

    m_context_hd_fd =
        clCreateContext(0, 2, &m_device[0], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_hd_fd);
    CHECK_EQUAL(1, m_context_hd_fd->device[0]->def.max_inflight_mem_ops);
    CHECK_EQUAL(2, m_context_hd_fd->device[1]->def.max_inflight_mem_ops);

    m_context_fd_hd =
        clCreateContext(0, 2, &m_device[2], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_fd_hd);
    CHECK_EQUAL(2, m_context_fd_hd->device[0]->def.max_inflight_mem_ops);
    CHECK_EQUAL(1, m_context_fd_hd->device[1]->def.max_inflight_mem_ops);

    m_context_fd_fd =
        clCreateContext(0, 2, &m_device[1], acl_test_notify_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_fd_fd);
    CHECK_EQUAL(2, m_context_fd_fd->device[0]->def.max_inflight_mem_ops);
    CHECK_EQUAL(2, m_context_fd_fd->device[1]->def.max_inflight_mem_ops);
  }

  void unload(void) {
    clReleaseContext(m_context);
    clReleaseContext(m_context_fd);
    clReleaseContext(m_context_hd_hd);
    clReleaseContext(m_context_hd_fd);
    clReleaseContext(m_context_fd_hd);
    clReleaseContext(m_context_fd_fd);
  }

  void clear_queue_callbacks(acl_device_op_queue_t * doq) {
    doq->user_data = 0;
    doq->launch_kernel = 0;
    doq->transfer_buffer = 0;
    doq->process_printf = 0;
    doq->program_device = 0;
    doq->log_update = 0;
  }

protected:
  acl_device_op_queue_t m_doq;

  cl_platform_id m_platform;
  cl_device_id m_device[5];
  cl_uint m_num_devices;
  cl_context m_context;    // single device; half-duplex
  cl_context m_context_fd; // single device; full-duplex
  cl_context
      m_context_hd_hd; // multi device; dev[0]:half_duplex, dev[1]half-duplex
  cl_context
      m_context_fd_hd; // multi device; dev[0]:full_duplex, dev[1]half-duplex
  cl_context
      m_context_hd_fd; // multi device; dev[0]:half_duplex, dev[1]full-duplex
  cl_context
      m_context_fd_fd; // multi device; dev[0]:full_duplex, dev[1]full-duplex
};

TEST(device_op, assumed_values) {
  // Some data structures and code assumes these values.
  CHECK_EQUAL(2, CL_SUBMITTED);
  CHECK_EQUAL(1, CL_RUNNING);
  CHECK_EQUAL(0, CL_COMPLETE);
}

TEST(device_op, link) {
  acl_init_device_op_queue(0);
  CHECK_EQUAL(0, acl_propose_device_op(0, ACL_DEVICE_OP_NONE, 0));
  CHECK_EQUAL(0, acl_propose_indexed_device_op(0, ACL_DEVICE_OP_NONE, 0, 0));
  CHECK_EQUAL(0, acl_update_device_op_queue(0));
  acl_submit_device_op(0, 0);
  acl_commit_proposed_device_ops(0);
  acl_forget_proposed_device_ops(0);
  acl_post_status_to_owning_event(0, 0);
  CHECK_EQUAL(ACL_CONFLICT_NONE, acl_device_op_conflict_type(0));
  acl_set_device_op_execution_status(0, 0);
}

TEST(device_op, init) {
  CHECK_EQUAL(ACL_MAX_DEVICE_OPS, m_doq.max_ops);
  CHECK_EQUAL(-1, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));

  acl_init_device_op_queue_limited(&m_doq, 12);
  CHECK_EQUAL(12, m_doq.max_ops);
}

TEST(device_op, conflict_type) {
  acl_device_op_t *op;

  op = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK(op);

  CHECK_EQUAL(ACL_CONFLICT_NONE, acl_device_op_conflict_type(op));

  op->info.type = ACL_DEVICE_OP_MEM_TRANSFER_READ;
  CHECK_EQUAL(ACL_CONFLICT_MEM_READ, acl_device_op_conflict_type(op));
  op->info.type = ACL_DEVICE_OP_MEM_TRANSFER_WRITE;
  CHECK_EQUAL(ACL_CONFLICT_MEM_WRITE, acl_device_op_conflict_type(op));
  op->info.type = ACL_DEVICE_OP_MEM_TRANSFER_COPY;
  CHECK_EQUAL(ACL_CONFLICT_MEM, acl_device_op_conflict_type(op));

  op->info.type = ACL_DEVICE_OP_KERNEL;
  CHECK_EQUAL(ACL_CONFLICT_KERNEL, acl_device_op_conflict_type(op));
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending);
  op->info.num_printf_bytes_pending = 1;
  CHECK_EQUAL(ACL_CONFLICT_MEM, acl_device_op_conflict_type(op));

  op->info.type = ACL_DEVICE_OP_REPROGRAM;
  CHECK_EQUAL(ACL_CONFLICT_REPROGRAM, acl_device_op_conflict_type(op));
}

// Check that a "submission" of a device op does the right thing.
// For this we need to override the callbacks.

static acl_device_op_type_t submit_kind;
static int unblocked;
static void (*the_call)(void *, acl_device_op_t *) = 0;
static void l_launch_kernel(void *ud, acl_device_op_t *op) {
  the_call = l_launch_kernel;
  ud = ud;
  submit_kind = ACL_DEVICE_OP_KERNEL;
  op = op;
}
static void l_transfer_buffer(void *ud, acl_device_op_t *op) {
  the_call = l_transfer_buffer;
  ud = ud;
  submit_kind = ACL_DEVICE_OP_MEM_TRANSFER_WRITE;
  op = op;
}
static void l_process_printf(void *ud, acl_device_op_t *op) {
  the_call = l_process_printf;
  submit_kind = op->info.type;
  if (op->info.num_printf_bytes_pending)
    *(int *)ud = 1;
  else
    *(int *)ud = 0;
  op->info.num_printf_bytes_pending = 0;
}
static void l_program_device(void *ud, acl_device_op_t *op) {
  the_call = l_program_device;
  ud = ud;
  submit_kind = ACL_DEVICE_OP_REPROGRAM;
  op = op;
}

TEST(device_op, submit_action) {
  submit_kind = ACL_DEVICE_OP_NONE;
  unblocked = 0;

  cl_ulong prev = acl_get_hal()->get_timestamp();
  cl_ulong latest = 0;

  m_doq.user_data = &unblocked;
  m_doq.launch_kernel = l_launch_kernel;
  m_doq.transfer_buffer = l_transfer_buffer;
  m_doq.process_printf = l_process_printf;
  m_doq.program_device = l_program_device;

  acl_device_op_t *op = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  acl_commit_proposed_device_ops(&m_doq);
  CHECK(op);

  // Already submitted because committing nudges device opthe scheduler.
  CHECK_EQUAL(CL_SUBMITTED, op->status);

  // submit kernel
  the_call = 0;
  op->info.type = ACL_DEVICE_OP_KERNEL;
  op->status = op->execution_status = CL_QUEUED;
  acl_submit_device_op(&m_doq, op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, submit_kind);
  CHECK_EQUAL(op->info.type, submit_kind);
  CHECK_EQUAL(CL_SUBMITTED, op->status);
  CHECK_EQUAL(CL_SUBMITTED, op->execution_status);
  CHECK(l_launch_kernel == the_call);
  CHECK_EQUAL(0, unblocked);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  CHECK(prev < (latest = op->timestamp[CL_SUBMITTED]));
  prev = latest;

  // submit mem xfer
  the_call = 0;
  op->info.type = ACL_DEVICE_OP_MEM_TRANSFER_WRITE;
  op->status = op->execution_status = CL_QUEUED;
  acl_submit_device_op(&m_doq, op);
  CHECK_EQUAL(ACL_DEVICE_OP_MEM_TRANSFER_WRITE, submit_kind);
  CHECK_EQUAL(op->info.type, submit_kind);
  CHECK_EQUAL(CL_SUBMITTED, op->status);
  CHECK_EQUAL(CL_SUBMITTED, op->execution_status);
  CHECK(l_transfer_buffer == the_call);
  CHECK_EQUAL(0, unblocked);
  CHECK(prev < (latest = op->timestamp[CL_SUBMITTED]));
  prev = latest;

  // submit reprogram
  the_call = 0;
  op->info.type = ACL_DEVICE_OP_REPROGRAM;
  op->status = op->execution_status = CL_QUEUED;
  acl_submit_device_op(&m_doq, op);
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, submit_kind);
  CHECK_EQUAL(op->info.type, submit_kind);
  CHECK_EQUAL(CL_SUBMITTED, op->status);
  CHECK_EQUAL(CL_SUBMITTED, op->execution_status);
  CHECK(l_program_device == the_call);
  CHECK_EQUAL(0, unblocked);
  CHECK(prev < (latest = op->timestamp[CL_SUBMITTED]));
  prev = latest;

  // Check unblocking a kernel stalled on printf
  the_call = 0;
  cl_ulong kernel_submit_time = latest;
  op->info.type = ACL_DEVICE_OP_KERNEL;
  op->status = op->execution_status = CL_RUNNING; // Required
  op->info.num_printf_bytes_pending = 1;
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  acl_submit_device_op(&m_doq, op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, submit_kind);
  CHECK_EQUAL(op->info.type, submit_kind);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(CL_RUNNING, op->execution_status);
  CHECK(l_process_printf == the_call);
  CHECK_EQUAL(1, unblocked);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending); // no longer marked as
                                                     // stalled on printf.
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  // We didn't change the submit time.
  CHECK_EQUAL(kernel_submit_time, op->timestamp[CL_SUBMITTED]);
  prev = latest;

  // Similar, need to process printf when have bytes to printf even when
  // complete.
  the_call = 0;
  op->info.type = ACL_DEVICE_OP_KERNEL;
  op->status = op->execution_status = CL_COMPLETE; // Required
  op->info.num_printf_bytes_pending = 1;
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  acl_submit_device_op(&m_doq, op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, submit_kind);
  CHECK_EQUAL(op->info.type, submit_kind);
  CHECK_EQUAL(CL_COMPLETE, op->status);
  CHECK_EQUAL(CL_COMPLETE, op->execution_status);
  CHECK(l_process_printf == the_call);
  CHECK_EQUAL(1, unblocked);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending); // no longer marked as
                                                     // stalled on printf.
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  // We didn't change the submit time.
  CHECK_EQUAL(kernel_submit_time, op->timestamp[CL_SUBMITTED]);
  prev = latest;
}

TEST(device_op, post_status) {
  cl_event e = clCreateUserEvent(m_context, 0);
  CHECK(e);

  acl_device_op_t *op = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e);
  CHECK_EQUAL(e, op->info.event);

  e->execution_status = CL_COMPLETE;
  op->timestamp[CL_QUEUED] = 40;
  op->timestamp[CL_SUBMITTED] = 41;
  op->timestamp[CL_RUNNING] = 42;
  op->timestamp[CL_COMPLETE] = 43;

  op->execution_status = CL_SUBMITTED;
  acl_post_status_to_owning_event(op, CL_SUBMITTED);
  CHECK_EQUAL(CL_SUBMITTED, e->execution_status);
  CHECK_EQUAL(41, e->timestamp[CL_SUBMITTED]);

  op->execution_status = CL_RUNNING;
  acl_post_status_to_owning_event(op, CL_RUNNING);
  CHECK_EQUAL(CL_RUNNING, e->execution_status);
  CHECK_EQUAL(42, e->timestamp[CL_RUNNING]);

  op->execution_status = CL_COMPLETE;
  acl_post_status_to_owning_event(op, CL_COMPLETE);
  CHECK_EQUAL(CL_COMPLETE, e->execution_status);
  CHECK_EQUAL(43, e->timestamp[CL_COMPLETE]);

  clReleaseEvent(e);

  cl_event e2 = clCreateUserEvent(m_context, 0);
  acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e2);

  op2->timestamp[CL_COMPLETE] = 13;
  op2->execution_status = -5;
  acl_post_status_to_owning_event(op2, -5);
  CHECK_EQUAL(-5, e2->execution_status);
  CHECK_EQUAL(13, e2->timestamp[CL_COMPLETE]);

  clReleaseEvent(e2);
}

TEST(device_op, err_status) {
  acl_device_op_test_ctx_t devops;
  acl_dot_push(&devops, &m_doq);

  // When an event owns multiple device ops, but the event errors out,
  // then only the last op should propagate the error status up to the
  // event.
  // AND, all the remaining device ops just fail cancel out and are never
  // submitted.
  cl_event e = clCreateUserEvent(m_context, 0);
  CHECK(e);

  // Two operations in the same group.
  acl_device_op_t *op0 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e);
  acl_device_op_t *op1 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e);
  acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e);
  CHECK_EQUAL(e, op0->info.event);
  CHECK_EQUAL(e, op1->info.event);
  CHECK_EQUAL(e, op2->info.event);
  acl_commit_proposed_device_ops(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_QUEUED, op1->status);
  CHECK_EQUAL(CL_QUEUED, op2->status);

  CHECK_EQUAL(op0, e->current_device_op);

  acl_print_debug_msg(" setting status -55\n");
  acl_set_execution_status(e, -55);
  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(-55, op0->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, e->execution_status);
  acl_update_device_op_queue(&m_doq);

  // Errors propagate down.
  CHECK_EQUAL(CL_COMPLETE, op0->status);
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_COMPLETE, op2->status);
  CHECK_EQUAL(-55, op1->execution_status); // was propagated down!
  CHECK_EQUAL(-55, op2->execution_status); // was propagated down!

  CHECK_EQUAL(-55, e->execution_status);

  // Check the log.
  CHECK_EQUAL(6, devops.num_ops);

  CHECK_EQUAL(0, devops.before[0].id);
  CHECK_EQUAL(CL_RUNNING, devops.before[0].status);
  CHECK_EQUAL(-55, devops.before[0].execution_status);
  CHECK_EQUAL(0, devops.before[1].id);
  CHECK_EQUAL(CL_COMPLETE, devops.before[1].status);
  CHECK_EQUAL(-55, devops.before[1].execution_status);

  CHECK_EQUAL(1, devops.before[2].id);
  CHECK_EQUAL(CL_RUNNING, devops.before[2].status);
  CHECK_EQUAL(-55, devops.before[2].execution_status);
  CHECK_EQUAL(1, devops.before[3].id);
  CHECK_EQUAL(CL_COMPLETE, devops.before[3].status);
  CHECK_EQUAL(-55, devops.before[3].execution_status);

  CHECK_EQUAL(2, devops.before[4].id);
  CHECK_EQUAL(CL_RUNNING, devops.before[4].status);
  CHECK_EQUAL(-55, devops.before[4].execution_status);
  CHECK_EQUAL(2, devops.before[5].id);
  CHECK_EQUAL(CL_COMPLETE, devops.before[5].status);
  CHECK_EQUAL(-55, devops.before[5].execution_status);

  clReleaseEvent(e); // release the user event
}

TEST(device_op, simple_exhaust) {
  // Checks that the device_op_queue can handle maximum permitted ops
  // and will not permit more ops to be added than it can handle
  const int max_allowed = 5;
  acl_init_device_op_queue_limited(&m_doq, max_allowed);
  clear_queue_callbacks(&m_doq);

  for (int i = 0; i < max_allowed; i++) {
    acl_device_op_t *op = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
    CHECK(op);
  }
  acl_device_op_t *op = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK_EQUAL(0, op);
}

TEST(device_op, exhaust) {
  // Much like simple_exhaust; tests the boundry conditions of device_op_queue
  // 1. Will forgetting an op violate the max allowed ops
  // 2. Will device_op_queue propagate all the attributes properly
  acl_get_hal()->get_timestamp(); // make sure it's non-zero

  // Prevents crashing if invalid events are used
  acl_set_allow_invalid_type<cl_event>(1);

  acl_device_op_t *ops[ACL_MAX_DEVICE_OPS];

  // Hard external reset...
  acl_init_device_op_queue_limited(&m_doq, ACL_MAX_DEVICE_OPS);
  clear_queue_callbacks(&m_doq);

  for (int i = 0; i < ACL_MAX_DEVICE_OPS; i++) { // one per dev op.
    for (unsigned j = 0; j < 2; j++) {           // forget first, then commit
      bool do_commit = (j > 0);
      cl_event e = myevents + (i % 4); // Fake events. 4 is the size of myevents

      CHECK_EQUAL(i, (int)m_doq.num_committed);

      // Propose a new operation.
      acl_device_op_t *op =
          (i & 1) ? acl_propose_indexed_device_op(&m_doq, ACL_DEVICE_OP_NONE, e,
                                                  (unsigned)i)
                  : acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, e);
      CHECK(op);
      ops[i] = op;
      CHECK(op);
      acl_print_debug_msg(
          "i %d num_committed %d num_proposed %d id %d size 0x%x\n", i,
          m_doq.num_committed, m_doq.num_proposed, op->id,
          (int)sizeof(acl_device_op_t));

      // Ensure contents are good.
      CHECK_EQUAL(m_doq.op + i, op);
      CHECK_EQUAL(
          i,
          op->id); // ensures we get distinct ones, and they all have right IDs.
      CHECK_EQUAL(ACL_PROPOSED, op->status);
      CHECK_EQUAL(ACL_PROPOSED, op->execution_status);
      CHECK_EQUAL(0, op->timestamp[0]);
      CHECK_EQUAL(0, op->timestamp[1]);
      CHECK_EQUAL(0, op->timestamp[2]);
      CHECK_EQUAL(e, op->info.event); // ensures we stash the event properly.
      CHECK_EQUAL(ACL_DEVICE_OP_NONE, op->info.type);

      if (i & 1) {
        CHECK_EQUAL((unsigned)i, op->info.index);
      } else {
        CHECK_EQUAL(0, op->info.index);
      }

      // Stomp on internal contents.
      op->execution_status = CL_RUNNING;
      op->timestamp[0] = 4;
      op->timestamp[1] = 5;

      // Internal bookkeeping.
      CHECK_EQUAL(0, m_doq.first_live);
      CHECK_EQUAL(i, m_doq.num_committed);
      CHECK_EQUAL(1, m_doq.num_proposed);

      if (do_commit) {
        acl_commit_proposed_device_ops(&m_doq);
        CHECK_EQUAL(i + 1, (int)m_doq.num_committed);
        CHECK_EQUAL(0, m_doq.first_live);
        CHECK_EQUAL(0, m_doq.num_proposed);

        // Since these are NONE operations, they immediately go into
        // submitted state.
        CHECK_EQUAL(CL_SUBMITTED, op->status);
        CHECK_EQUAL(CL_SUBMITTED, op->execution_status);
        CHECK_EQUAL(i, op->group_id);
        CHECK_EQUAL(1, op->first_in_group);
        CHECK_EQUAL(1, op->last_in_group);

        CHECK_EQUAL(4, op->timestamp[0]);
        CHECK_EQUAL(5, op->timestamp[1]);
        CHECK(0 < op->timestamp[2]);
      } else {
        acl_forget_proposed_device_ops(&m_doq);
        CHECK_EQUAL(i, m_doq.num_committed);
        CHECK_EQUAL(0, m_doq.num_proposed);
        CHECK_EQUAL((i ? ops[0]->id : -1), m_doq.first_live);
      }
    }
  }
  // Can't get any more.
  CHECK_EQUAL(0, acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0));
  CHECK_EQUAL(0,
              acl_propose_indexed_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0, 0));

  acl_set_allow_invalid_type<cl_event>(0);
}

TEST(device_op, multi) {
  // Check enqueue of multiple operations in a single group.

  acl_init_device_op_queue_limited(&m_doq, 5);
  clear_queue_callbacks(&m_doq);

  // a group with 2 operations
  acl_device_op_t *op0 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(1, m_doq.num_proposed);
  acl_device_op_t *op1 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(2, m_doq.num_proposed);
  acl_commit_proposed_device_ops(&m_doq);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(1, op0->group_id);
  CHECK_EQUAL(1, op0->first_in_group);
  CHECK_EQUAL(0, op0->last_in_group);

  CHECK_EQUAL(1, op1->id);
  CHECK_EQUAL(1, op1->group_id);
  CHECK_EQUAL(0, op1->first_in_group);
  CHECK_EQUAL(1, op1->last_in_group);

  // We automatically submit the first op, and the second one
  // waits because it's in a group.
  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_QUEUED, op1->status);

  // a group with 1 operations
  acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(1, m_doq.num_proposed);
  acl_commit_proposed_device_ops(&m_doq);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(3, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  CHECK_EQUAL(2, op2->id);
  CHECK_EQUAL(2, op2->group_id);
  CHECK_EQUAL(1, op2->first_in_group);
  CHECK_EQUAL(1, op2->last_in_group);

  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_QUEUED, op1->status);
  CHECK_EQUAL(CL_SUBMITTED, op2->status); // Auto-submit the new one.

  // Now check wraparound of a group.  complete and flush the first
  // two operations
  op0->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  // Check that the queueing has been updated.
  CHECK_EQUAL(1, m_doq.first_live);
  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  CHECK_EQUAL(CL_COMPLETE, op0->status);
  CHECK_EQUAL(CL_SUBMITTED, op1->status);
  CHECK_EQUAL(CL_SUBMITTED, op2->status); // Auto-submit the new one.

  op1->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_SUBMITTED, op2->status); // Auto-submit the new one.

  // Check that the queueing has been updated.
  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  // Enqueue 4 more.  This wraps, occupying indices 3,4,0,1
  acl_device_op_t *op3 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  acl_device_op_t *op4 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  acl_device_op_t *op5 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  acl_device_op_t *op6 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);

  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(4, m_doq.num_proposed);

  // We can't enqueue any more.
  CHECK_EQUAL(0, acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0));

  acl_forget_proposed_device_ops(&m_doq);
  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  op3 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  op4 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  op5 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  op6 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK_EQUAL(
      1, op3 - m_doq.op); // Strange indices due to push/pop freelist handling.
  CHECK_EQUAL(0, op4 - m_doq.op);
  CHECK_EQUAL(3, op5 - m_doq.op);
  CHECK_EQUAL(4, op6 - m_doq.op);

  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(4, m_doq.num_proposed);

  acl_commit_proposed_device_ops(&m_doq);
  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(5, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  // Can't do any more.
  CHECK_EQUAL(0, acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0));

  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(5, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  // Check contents of these new ops.
  CHECK_EQUAL(1, op3 - m_doq.op);
  CHECK_EQUAL(1, op3->id);
  CHECK_EQUAL(4, op3->group_id); // The id of the last in the group.
  CHECK_EQUAL(1, op3->first_in_group);
  CHECK_EQUAL(0, op3->last_in_group);
  CHECK_EQUAL(0, op4 - m_doq.op);
  CHECK_EQUAL(0, op4->id);
  CHECK_EQUAL(4, op4->group_id); // The id of the last in the group.
  CHECK_EQUAL(0, op4->first_in_group);
  CHECK_EQUAL(0, op4->last_in_group);
  CHECK_EQUAL(3, op5 - m_doq.op);
  CHECK_EQUAL(3, op5->id);
  CHECK_EQUAL(4, op5->group_id); // The id of the last in the group.
  CHECK_EQUAL(0, op5->first_in_group);
  CHECK_EQUAL(0, op5->last_in_group);
  CHECK_EQUAL(4, op6 - m_doq.op);
  CHECK_EQUAL(4, op6->id);
  CHECK_EQUAL(4, op6->group_id); // The id of the last in the group.
  CHECK_EQUAL(0, op6->first_in_group);
  CHECK_EQUAL(1, op6->last_in_group);

  op2->execution_status = CL_COMPLETE;
  op3->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(CL_COMPLETE, op2->status);
  CHECK_EQUAL(CL_COMPLETE, op3->status);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(3, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);

  op4->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(CL_COMPLETE, op4->status);

  op5->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(CL_COMPLETE, op5->status);

  op6->execution_status = CL_COMPLETE;
  CHECK(0 < acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(&m_doq));
  CHECK_EQUAL(CL_COMPLETE, op6->status);

  CHECK_EQUAL(-1, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
}

TEST(device_op, prune) {
  // Check the life cycle of device ops.
  // When created, they are in PROPOSED
  cl_event e0 = clCreateUserEvent(m_context, 0);
  CHECK(e0);

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);

  acl_device_op_t *op0 = acl_propose_device_op(doq, ACL_DEVICE_OP_NONE, e0);
  acl_device_op_t *op1 = acl_propose_device_op(doq, ACL_DEVICE_OP_NONE, e0);
  acl_commit_proposed_device_ops(doq);

  CHECK_EQUAL(0, doq->first_live);
  CHECK_EQUAL(2, doq->num_committed);
  CHECK_EQUAL(0, doq->num_proposed);
  CHECK_EQUAL(op0, doq->op + 0);
  CHECK_EQUAL(op1, doq->op + 1);

  CHECK(op0);
  CHECK_EQUAL(e0, op0->info.event);
  CHECK(op1);
  CHECK_EQUAL(e0, op1->info.event);

  CHECK_EQUAL(CL_SUBMITTED, op0->execution_status);
  CHECK_EQUAL(CL_QUEUED, op1->execution_status);

  CHECK_EQUAL(0, acl_update_device_op_queue(doq));

  op0->execution_status = CL_RUNNING;
  op0->timestamp[CL_RUNNING] = acl_get_hal()->get_timestamp();

  CHECK(0 < acl_update_device_op_queue(doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(doq));

  // status and timstamp get copied from the op to the event.
  CHECK_EQUAL(CL_RUNNING, op0->status);
  CHECK_EQUAL(CL_RUNNING, op0->execution_status);
  CHECK_EQUAL(CL_RUNNING, op0->info.event->execution_status);
  CHECK_EQUAL(op0->info.event->timestamp[CL_RUNNING],
              op0->timestamp[CL_RUNNING]);

  CHECK_EQUAL(0, doq->first_live);
  CHECK_EQUAL(2, doq->num_committed);
  CHECK_EQUAL(0, doq->num_proposed);

  op0->execution_status = CL_COMPLETE;
  op0->timestamp[CL_COMPLETE] = acl_get_hal()->get_timestamp();

  CHECK(0 < acl_update_device_op_queue(doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(doq));

  // Should have pruned the first one.
  CHECK_EQUAL(1, doq->first_live);
  CHECK_EQUAL(1, doq->num_committed);
  CHECK_EQUAL(0, doq->num_proposed);

  // Still marked as running because only the firt device op is copmlete.
  CHECK_EQUAL(CL_RUNNING, op0->info.event->execution_status);

  // Now flush out the second op.
  op1->execution_status = CL_RUNNING;
  op1->timestamp[CL_RUNNING] = acl_get_hal()->get_timestamp();
  CHECK(0 < acl_update_device_op_queue(doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(doq));
  CHECK_EQUAL(CL_RUNNING, op1->info.event->execution_status);
  // Still have the op0 timestamp.
  CHECK_EQUAL(op1->info.event->timestamp[CL_RUNNING],
              op0->timestamp[CL_RUNNING]);

  op1->execution_status = CL_COMPLETE;
  op1->timestamp[CL_COMPLETE] = acl_get_hal()->get_timestamp();
  CHECK(0 < acl_update_device_op_queue(doq));
  CHECK_EQUAL(0, acl_update_device_op_queue(doq));
  CHECK_EQUAL(CL_COMPLETE, op1->info.event->execution_status);
  CHECK_EQUAL(op1->info.event->timestamp[CL_COMPLETE],
              op1->timestamp[CL_COMPLETE]);

  // nothing left on the list.
  CHECK_EQUAL(-1, doq->first_live);
  CHECK_EQUAL(0, doq->num_committed);
  CHECK_EQUAL(0, doq->num_proposed);

  clReleaseEvent(e0);
}

TEST(device_op, inter_group_blocking) {
  // Example:
  //    Group 0 (from cq 0)
  //          MEM 0
  //    Group 1 (from cq 1)
  //          PROGRAM
  //          MEM 1
  //          KERNEL
  //          MEM 2

  // Should not launch kernel  before either PROGRAM or MEM 1 are done.
  // In fact, should completely serialize.

  acl_device_op_t *op0 =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_READ, 0);
  CHECK(op0);
  acl_commit_proposed_device_ops(&m_doq);

  acl_device_op_t *op1 =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_REPROGRAM, 0);
  acl_device_op_t *op3 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  CHECK(op1);
  CHECK(op3);
  acl_commit_proposed_device_ops(&m_doq);

  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_QUEUED, op1->status);
  CHECK_EQUAL(CL_QUEUED, op3->status);

  acl_set_device_op_execution_status(op0, CL_RUNNING);
  acl_set_device_op_execution_status(op0, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_COMPLETE, op0->status);
  CHECK_EQUAL(CL_SUBMITTED, op1->status);
  CHECK_EQUAL(CL_QUEUED, op3->status);

  acl_set_device_op_execution_status(op1, CL_RUNNING);
  acl_set_device_op_execution_status(op1, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_COMPLETE, op1->status);

  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, op3->status);

  acl_set_device_op_execution_status(op3, CL_RUNNING);
  acl_set_device_op_execution_status(op3, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_COMPLETE, op3->status);

  acl_update_device_op_queue(&m_doq);

  // Now check timestamps.

  CHECK(op0->timestamp[CL_RUNNING] < op0->timestamp[CL_COMPLETE]);
  CHECK(op1->timestamp[CL_RUNNING] < op1->timestamp[CL_COMPLETE]);
  CHECK(op3->timestamp[CL_RUNNING] < op3->timestamp[CL_COMPLETE]);
  CHECK(op0->timestamp[CL_COMPLETE] < op1->timestamp[CL_RUNNING]);
}

TEST(device_op, inter_group_all_conflict_types) {
  //    Group 0 (from cq 0)
  //          KERNEL 0.0    RUNNING   (from program 0 already flushed from
  //          queue)
  //    Group 1 (from cq 1)
  //          PROGRAM 1     QUEUED    (program 1)
  //          KERNEL  1.0   QUEUED
  //    Group 2 (from cq 2)
  //          KERNEL  1.1

  // Should not launch kernel K 1.1 !!!!
  // It will try to interact with the program 0 instead of program 1
  // where it belongs.

  acl_device_op_t *k00 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  acl_commit_proposed_device_ops(&m_doq);
  CHECK_EQUAL(CL_SUBMITTED, k00->status);

  acl_set_device_op_execution_status(k00, CL_RUNNING);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_RUNNING, k00->status);

  acl_device_op_t *p1 =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_REPROGRAM, 0);
  acl_device_op_t *k10 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  acl_commit_proposed_device_ops(&m_doq);

  CHECK_EQUAL(CL_RUNNING, k00->status);
  CHECK_EQUAL(CL_QUEUED, p1->status);
  CHECK_EQUAL(CL_QUEUED, k10->status);

  acl_device_op_t *k11 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  acl_commit_proposed_device_ops(&m_doq);

  CHECK_EQUAL(CL_RUNNING, k00->status);
  CHECK_EQUAL(CL_QUEUED, p1->status);
  CHECK_EQUAL(CL_QUEUED, k10->status);
  CHECK_EQUAL(CL_QUEUED, k11->status); // this is the bug!

  acl_set_device_op_execution_status(k00, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, p1->status);
  CHECK_EQUAL(CL_QUEUED, k10->status);
  CHECK_EQUAL(CL_QUEUED, k11->status);

  acl_set_device_op_execution_status(p1, CL_RUNNING);
  acl_set_device_op_execution_status(p1, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  // The two kernels on program 1 can run concurrently.
  CHECK_EQUAL(CL_SUBMITTED, k10->status);
  CHECK_EQUAL(CL_SUBMITTED, k11->status);
}

TEST(device_op, inter_group_concurrent_kernels) {
  // These should be concurrent, assuming they're on different accelerator
  // blocks.
  //    Group 0 (from cq 0)
  //          KERNEL 0
  //    Group 1 (from cq 1)
  //          KERNEL 1
  //          KERNEL 2

  acl_device_op_t *op0 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  CHECK(op0);
  acl_commit_proposed_device_ops(&m_doq);

  acl_device_op_t *op1 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_KERNEL, 0);
  CHECK(op1);
  CHECK(op2);
  acl_commit_proposed_device_ops(&m_doq);

  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_SUBMITTED, op1->status);
  CHECK_EQUAL(CL_QUEUED,
              op2->status); // still queued because of intra-group conflict

  // Let's pretend we have out of order completion.
  acl_set_device_op_execution_status(op1, CL_RUNNING);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_SUBMITTED, op0->status);
  CHECK_EQUAL(CL_RUNNING, op1->status);
  CHECK_EQUAL(CL_QUEUED, op2->status);

  acl_set_device_op_execution_status(op0, CL_RUNNING);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_RUNNING, op0->status);
  CHECK_EQUAL(CL_RUNNING, op1->status);
  CHECK_EQUAL(CL_QUEUED, op2->status);

  acl_set_device_op_execution_status(op1, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_RUNNING, op0->status);
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_SUBMITTED, op2->status); // aha

  acl_set_device_op_execution_status(op2, CL_RUNNING);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_RUNNING, op0->status);
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_RUNNING, op2->status);

  acl_set_device_op_execution_status(op2, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_RUNNING, op0->status);
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_COMPLETE, op2->status);

  acl_set_device_op_execution_status(op0, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);

  CHECK_EQUAL(CL_COMPLETE, op0->status);
  CHECK_EQUAL(CL_COMPLETE, op1->status);
  CHECK_EQUAL(CL_COMPLETE, op2->status);
}

TEST(device_op, three_lists) {

  // Initial state
  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(-1, m_doq.first_live);
  CHECK_EQUAL(-1, m_doq.last_committed);
  CHECK_EQUAL(-1, m_doq.last_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(0, m_doq.first_free);

  // One proposed op
  acl_device_op_t *op0 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);

  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(1, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(-1, m_doq.last_committed);
  CHECK_EQUAL(0, m_doq.last_live);
  CHECK_EQUAL(0, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(1, m_doq.first_free);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(-1, op0->link);

  // Two proposed ops
  acl_device_op_t *op1 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);

  CHECK_EQUAL(0, m_doq.num_committed);
  CHECK_EQUAL(2, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(-1, m_doq.last_committed);
  CHECK_EQUAL(1, m_doq.last_live);
  CHECK_EQUAL(0, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(2, m_doq.first_free);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(1, op0->link);
  CHECK_EQUAL(1, op1->id);
  CHECK_EQUAL(-1, op1->link);

  // Two committed ops
  acl_commit_proposed_device_ops(&m_doq);

  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.last_committed);
  CHECK_EQUAL(1, m_doq.last_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(2, m_doq.first_free);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(1, op0->link);
  CHECK_EQUAL(1, op1->id);
  CHECK_EQUAL(-1, op1->link);

  // Two committed ops, one proposed
  acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);

  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(1, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.last_committed);
  CHECK_EQUAL(2, m_doq.last_live);
  CHECK_EQUAL(2, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(3, m_doq.first_free);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(1, op0->link);
  CHECK_EQUAL(1, op1->id);
  CHECK_EQUAL(2, op1->link);
  CHECK_EQUAL(2, op2->id);
  CHECK_EQUAL(-1, op2->link);

  // Two committed ops, two proposed
  acl_device_op_t *op3 = acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);

  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(2, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(1, m_doq.last_committed);
  CHECK_EQUAL(3, m_doq.last_live);
  CHECK_EQUAL(2, acl_first_proposed_device_op_idx(&m_doq));
  CHECK_EQUAL(4, m_doq.first_free);

  CHECK_EQUAL(0, op0->id);
  CHECK_EQUAL(1, op0->link);
  CHECK_EQUAL(1, op1->id);
  CHECK_EQUAL(2, op1->link);
  CHECK_EQUAL(2, op2->id);
  CHECK_EQUAL(3, op2->link);
  CHECK_EQUAL(3, op3->id);
  CHECK_EQUAL(-1, op3->link);

  // Retire the second committed op, then prune
  acl_print_debug_msg("retire op 1\n");
  op1->status = CL_COMPLETE;
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(1, m_doq.first_free);

  CHECK_EQUAL(2, op0->link);
  CHECK_EQUAL(4, op1->link); // it's on the free list.

  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(2, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.last_committed);
  CHECK_EQUAL(3, m_doq.last_live);
  CHECK_EQUAL(2, acl_first_proposed_device_op_idx(&m_doq));

  CHECK_EQUAL(2, op2->id);
  CHECK_EQUAL(3, op2->link);
  CHECK_EQUAL(3, op3->id);
  CHECK_EQUAL(-1, op3->link);

  // Commit the proposed ops. Should have three
  acl_commit_proposed_device_ops(&m_doq);

  CHECK_EQUAL(1, m_doq.first_free);
  CHECK_EQUAL(2, op0->link);
  CHECK_EQUAL(4, op1->link); // it's on the free list.
  CHECK_EQUAL(3, op2->link);
  CHECK_EQUAL(-1, op3->link);

  CHECK_EQUAL(3, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(3, m_doq.last_committed);
  CHECK_EQUAL(3, m_doq.last_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));

  // Retire the first op, then prune
  acl_print_debug_msg("retire op 0\n");
  op0->status = CL_COMPLETE;
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(0, m_doq.first_free);
  CHECK_EQUAL(1, op0->link); // it's on the free list.
  CHECK_EQUAL(4, op1->link); // it's on the free list.
  CHECK_EQUAL(3, op2->link);
  CHECK_EQUAL(-1, op3->link);

  CHECK_EQUAL(2, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(2, m_doq.first_live);
  CHECK_EQUAL(3, m_doq.last_committed);
  CHECK_EQUAL(3, m_doq.last_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));

  l_dump_op("foo", op0);
}

TEST(device_op, prune_between) {
  // Long running or infinite-running device ops should not prevent
  // later operations from being pruned.
  // Something that should work:
  //    1. An infinite running kernel
  //    2. many thousands of transient operations (more than
  //    ACL_MAX_DEVICE_OPS).

  // We just need num_groups * num_per_group - 1 > ACL_MAX_DEVICE_OPS.
  const int num_groups = 2 * ACL_MAX_DEVICE_OPS;
  const int num_per_group = 3;

  // Hard external reset...
  acl_init_device_op_queue(&m_doq);
  clear_queue_callbacks(&m_doq);

  // Enqueue an "infinite" operation.
  acl_device_op_t *infinite_op =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0);
  CHECK(infinite_op);
  CHECK_EQUAL(0, acl_first_proposed_device_op_idx(&m_doq));
  l_dump_op("infinite proposed ", infinite_op);
  acl_commit_proposed_device_ops(&m_doq);
  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));
  l_dump_op("infinite committed ", infinite_op);

  for (int igroup = 0; igroup < num_groups; igroup++) {
    acl_device_op_t *ops[num_per_group];

    acl_print_debug_msg("group %d\n", igroup);

    // Propose a new group.
    for (int i = 0; i < num_per_group; i++) {

      // We flush each group after creating it, so:
      CHECK_EQUAL(1, m_doq.num_committed);
      CHECK_EQUAL(0, m_doq.first_live);

      // Since we flush after each group, the following must be true.
      // Also, need to respect the alternating push/pop free list ordering.
      // The nodes are pushed onto the free list in alternating orders.
      // First group is pushed on reverse (and peeled off in that
      // order). Second group is pushed on in forward order.
      int expected_id = (igroup & 1) ? (3 - i) : (i + 1);
      CHECK_EQUAL((i ? ((igroup & 1) ? 3 : 1) : -1),
                  acl_first_proposed_device_op_idx(&m_doq));

      // Propose a new operation.
      ops[i] = acl_propose_indexed_device_op(&m_doq, ACL_DEVICE_OP_NONE, 0,
                                             (unsigned)i);
      CHECK(ops[i]);
      l_dump_op("infinite committed ", infinite_op);
      CHECK_EQUAL(i + 1, m_doq.num_proposed);
      CHECK_EQUAL(1, m_doq.num_committed);

      CHECK_EQUAL(((igroup & 1) ? 3 : 1),
                  acl_first_proposed_device_op_idx(&m_doq));

      // Ensure contents are good.
      CHECK_EQUAL(expected_id, ops[i]->id); // ensures we get distinct ones, and
                                            // they all have right IDs.
      CHECK_EQUAL(m_doq.op + expected_id, ops[i]);
      CHECK_EQUAL(ACL_PROPOSED, ops[i]->status);
      CHECK_EQUAL(ACL_PROPOSED, ops[i]->execution_status);
      CHECK_EQUAL(ACL_DEVICE_OP_NONE, ops[i]->info.type);

      CHECK_EQUAL((unsigned)i, ops[i]->info.index);
    }
    for (int i = 0; i < num_per_group; i++) {
      l_dump_op("prop ", ops[i]);
    }

    // Commit them.
    acl_commit_proposed_device_ops(&m_doq);
    CHECK_EQUAL(num_per_group + 1, (int)m_doq.num_committed);
    CHECK_EQUAL(0, m_doq.num_proposed);
    CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));

    // Check commits, and processing into submitted state
    for (int i = 0; i < num_per_group; i++) {
      // Since these are NONE operations, the first one immediately goes
      // into submitted state.  The others wait due to intra-group
      // exclusion.
      l_dump_op("committed ", ops[i]);
      CHECK_EQUAL((i ? CL_QUEUED : CL_SUBMITTED), ops[i]->status);
      CHECK_EQUAL((i ? CL_QUEUED : CL_SUBMITTED), ops[i]->execution_status);
      CHECK_EQUAL(((igroup & 1) ? 1 : 3),
                  ops[i]->group_id); // id of last op in the group.
      CHECK_EQUAL(i == 0, ops[i]->first_in_group);
      CHECK_EQUAL(i == (num_per_group - 1), ops[i]->last_in_group);
    }

    // Now mark as complete, and prune them.
    for (int i = 0; i < num_per_group; i++) {
      ops[i]->status = CL_COMPLETE;
    }

    for (int i = 0; i < num_per_group; i++) {
      l_dump_op("done ", ops[i]);
    }

    // Process the queue and prune.
    acl_update_device_op_queue(&m_doq);
  }

  CHECK_EQUAL(1, m_doq.num_committed);
  CHECK_EQUAL(0, m_doq.num_proposed);
  CHECK_EQUAL(0, m_doq.first_live);
  CHECK_EQUAL(-1, acl_first_proposed_device_op_idx(&m_doq));
}

TEST(device_op, full_duplex) {
  // Prevents crashing if invalid events are used
  acl_set_allow_invalid_type<cl_event>(1);

  // Single Device checks
  // 1. can schedule a read and write in parallel;
  //    a rw can't be scheduled if a r or w is running
  cl_event event = &myevents[1];
  CHECK_EQUAL(m_context_fd, event->context);

  acl_device_op_t *op_read =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_READ, event);
  acl_commit_proposed_device_ops(&m_doq);
  acl_device_op_t *op_write =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_WRITE, event);
  acl_commit_proposed_device_ops(&m_doq);
  acl_device_op_t *op_rw =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_COPY, event);
  acl_commit_proposed_device_ops(&m_doq);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_SUBMITTED, op_read->status);
  CHECK_EQUAL(CL_SUBMITTED, op_write->status);
  CHECK_EQUAL(CL_QUEUED, op_rw->status);

  acl_set_device_op_execution_status(op_read, CL_RUNNING);
  acl_set_device_op_execution_status(op_write, CL_RUNNING);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_RUNNING, op_read->status);
  CHECK_EQUAL(CL_RUNNING, op_write->status);
  CHECK_EQUAL(CL_QUEUED, op_rw->status);

  acl_set_device_op_execution_status(op_write, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_RUNNING, op_read->status);
  CHECK_EQUAL(CL_COMPLETE, op_write->status);
  CHECK_EQUAL(CL_QUEUED, op_rw->status);

  acl_set_device_op_execution_status(op_read, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_COMPLETE, op_read->status);
  CHECK_EQUAL(CL_COMPLETE, op_write->status);
  CHECK_EQUAL(CL_SUBMITTED, op_rw->status);

  // 2. a rw blocks the queue
  acl_set_device_op_execution_status(op_rw, CL_RUNNING);
  op_read =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_READ, event);
  acl_commit_proposed_device_ops(&m_doq);
  op_write =
      acl_propose_device_op(&m_doq, ACL_DEVICE_OP_MEM_TRANSFER_WRITE, event);
  acl_commit_proposed_device_ops(&m_doq);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_RUNNING, op_rw->status);
  CHECK_EQUAL(CL_QUEUED, op_read->status);
  CHECK_EQUAL(CL_QUEUED, op_write->status);

  acl_set_device_op_execution_status(op_rw, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_COMPLETE, op_rw->status);
  CHECK_EQUAL(CL_SUBMITTED, op_read->status);
  CHECK_EQUAL(CL_SUBMITTED, op_write->status);

  acl_set_device_op_execution_status(op_read, CL_COMPLETE);
  acl_set_device_op_execution_status(op_write, CL_COMPLETE);
  acl_update_device_op_queue(&m_doq);
  CHECK_EQUAL(CL_COMPLETE, op_rw->status);
  CHECK_EQUAL(CL_COMPLETE, op_read->status);
  CHECK_EQUAL(CL_COMPLETE, op_write->status);

  acl_set_allow_invalid_type<cl_event>(0);
}

TEST(device_op, multi_device_rw_conflict) {
  // Checks that reads, writes, and RW conflict as expected in single and
  // multidevice scenarios
  const unsigned int OP_NUM = 3;
  const acl_device_op_type_t ops[OP_NUM] = {ACL_DEVICE_OP_MEM_TRANSFER_READ,
                                            ACL_DEVICE_OP_MEM_TRANSFER_WRITE,
                                            ACL_DEVICE_OP_MEM_TRANSFER_COPY};
  // CL_SUBMITTED == 2; CL_QUEUED == 3
  const int expected_results[OP_NUM][OP_NUM][EVENT_NUM] = {
      /*R2*/ /*W2*/ /*RW2*/
      /*R1*/ {{3, 3, 3, 3, 3, 3}, {3, 2, 3, 3, 3, 2}, {3, 3, 3, 3, 3, 3}},
      /*W1*/ {{3, 2, 3, 3, 3, 2}, {3, 3, 3, 3, 3, 3}, {3, 3, 3, 3, 3, 3}},
      /*RW1*/ {{3, 3, 3, 3, 3, 3}, {3, 3, 3, 3, 3, 3}, {3, 3, 3, 3, 3, 3}}};
  unsigned int i, j, k;
  cl_event event;

  // Prevents crashing if invalid events are used
  acl_set_allow_invalid_type<cl_event>(1);

  for (i = 0; i < OP_NUM; i++) {
    for (j = 0; j < OP_NUM; j++) {
      for (k = 0; k < EVENT_NUM; k++) {
        // Hard external reset...
        acl_init_device_op_queue(&m_doq);
        clear_queue_callbacks(&m_doq);

        event = &myevents[k];

        acl_device_op_t *op1 = acl_propose_device_op(&m_doq, ops[i], event);
        acl_commit_proposed_device_ops(&m_doq);
        acl_device_op_t *op2 = acl_propose_device_op(&m_doq, ops[j], event);
        acl_commit_proposed_device_ops(&m_doq);
        acl_update_device_op_queue(&m_doq);
        CHECK_EQUAL(CL_SUBMITTED, op1->status);
        CHECK_EQUAL(expected_results[i][j][k], op2->status);
      }
    }
  }
}

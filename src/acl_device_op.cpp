// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <stdio.h>
#include <string.h>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl_command_queue.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_hostch.h>
#include <acl_kernel.h>
#include <acl_mem.h>
#include <acl_printf.h>
#include <acl_profiler.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_svm.h>
#include <acl_types.h>
#include <acl_usm.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#ifdef ACL_DEVICE_OP_STATS
#define STATS(X) X
#else
#define STATS(X)
#endif

//
// Device operations
// =================
//
// All interactions with the device should occur through a scheduled device
// operation.  We use a single platform-wide queue to manage these
// operations, to make it simpler to manage exclusion.
//
// For example, running a kernel could imply reprogramming the device, and
// not other activity can occur during reprogramming.
//
// Also, it's platform-wide because must support multiple contexts at the
// same time, all of which can use the same device.
//
// An operation is added to the device operation queue when a
// user-level event/command is *submitted*.
// That is, no user-level event dependencies are blocking
// the execution of the device operation.  This means that only device
// operation blocks forward progress on the device operation queue.
//
static unsigned l_update_device_op_queue_once(acl_device_op_queue_t *doq);
static void l_record_milestone(acl_device_op_t *op,
                               cl_profiling_info milestone);
static int l_first_proposed(acl_device_op_queue_t *doq);
static int l_is_noop_migration(acl_device_op_t *op);
static int l_is_mem_in_use(acl_device_op_t *op, acl_device_op_queue_t *doq);

// We must support a mixture of concurrent and serialized device
// operations:
//
//    - Memory transfers must be serialized with respect to each other
//    due to limitations in our PCIe driver.
//
//    - We should support multiple kernels in flight from different user
//    command queues.
//
//    - We must serialize device reprogramming with all other operations.
//
//    - Some kernels may have printf calls, and they may stall during
//    kernel execution.  The stall is resolved by transferring the printf
//    buffer from the device and printing it out.  No other memory
//    operations may be in flight during this time.
//
// We have several ways to control concurrency:
//
// 1. We use the concept of an "exclusion group".
//    Sometimes a single user-level command requires a sequence of
//    device operations, which we can think of as a transaction.
//    The operations in a group must occur in sequence, and not overlap.
//    They appear on the device queue in sequence (without other operations
//    in between) and are all marked with an identifying exclusion group id.
//    The group id is just the id of the last device operation in the
//    group.
//
// 2. We use a "conflict matrix" to tell us whether a pair of device
//    operations are forbidden to run concurrently.
//    The indices are the device operation conflict type, which depends
//    only on the operation type, and the printf-stall state if it's a
//    kernel.
//
// 3. The reprogramming operation is very SPECIAL.
//    It can move unbounded numbers of buffers to and from the device, and
//    it must reprogram the device.
//    We don't bother to make micro-operations for the buffer moves: it's
//    just too much bookkeeping, for no benefit.
//
//

static unsigned char conflict_matrix_half_duplex
    [ACL_NUM_CONFLICT_TYPES][ACL_NUM_CONFLICT_TYPES] = {

        //                      NONE, MEM_READ, MEM_WRITE, MEM_RW, KERNEL,
        //                      PROGRAM, HOSTPIPE_READ, HOSTPIPE_WRITE
        // NONE vs.
        {0, 0, 0, 0, 0, 1, 0, 0}
        // MEM_READ  vs.
        ,
        {0, 1, 1, 1, 0, 1, 1, 1}
        // MEM_WRITE  vs.
        ,
        {0, 1, 1, 1, 0, 1, 1, 1}
        // MEM_RW  vs.
        ,
        {0, 1, 1, 1, 0, 1, 1, 1}
        // KERNEL vs.
        ,
        {0, 0, 0, 0, 0, 1, 0, 0}
        // PROGRAM vs.
        ,
        {1, 1, 1, 1, 1, 1, 1, 1},
        // HOSTPIPE_READ vs.
        {0, 1, 1, 1, 0, 1, 0, 0},
        // HOSTPIPE_WRITE vs.
        {0, 1, 1, 1, 0, 1, 0, 0}};

static unsigned char conflict_matrix_full_duplex
    [ACL_NUM_CONFLICT_TYPES][ACL_NUM_CONFLICT_TYPES] = {

        //                      NONE, MEM_READ, MEM_WRITE, MEM_RW, KERNEL,
        //                      PROGRAM, HOSTPIPE_READ, HOSTPIPE_WRITE
        // NONE vs.
        {0, 0, 0, 0, 0, 1, 0, 0}
        // MEM_READ  vs.
        ,
        {0, 1, 0, 1, 0, 1, 1, 1}
        // MEM_WRITE  vs.
        ,
        {0, 0, 1, 1, 0, 1, 1, 1}
        // MEM_RW  vs.
        ,
        {0, 1, 1, 1, 0, 1, 1, 1}
        // KERNEL vs.
        ,
        {0, 0, 0, 0, 0, 1, 0, 0}
        // PROGRAM vs.
        ,
        {1, 1, 1, 1, 1, 1, 1, 1},
        // HOSTPIPE_READ vs.
        {0, 1, 1, 1, 0, 1, 0, 0},
        // HOSTPIPE_WRITE vs.
        {0, 1, 1, 1, 0, 1, 0, 0}};

static const char *l_type_name(int op_type) {
  switch (op_type) {
  case ACL_DEVICE_OP_NONE:
    return "NONE";
    break;
  case ACL_DEVICE_OP_KERNEL:
    return "KERNEL";
    break;
  case ACL_DEVICE_OP_MEM_TRANSFER_READ:
    return "MEM_TRANSFER_READ";
    break;
  case ACL_DEVICE_OP_MEM_TRANSFER_WRITE:
    return "MEM_TRANSFER_WRITE";
    break;
  case ACL_DEVICE_OP_MEM_TRANSFER_COPY:
    return "MEM_TRANSFER_COPY";
    break;
  case ACL_DEVICE_OP_REPROGRAM:
    return "REPROGRAM";
    break;
  case ACL_DEVICE_OP_MEM_MIGRATION:
    return "MIGRATE_MEM_OBJECT";
    break;
  case ACL_DEVICE_OP_USM_MEMCPY:
    return "USM_MEMCPY";
    break;
  case ACL_DEVICE_OP_HOSTPIPE_READ:
    return "HOSTPIPE_READ";
    break;
  case ACL_DEVICE_OP_HOSTPIPE_WRITE:
    return "HOSTPIPE_WRITE";
    break;
  default:
    return "<err>";
    break;
  }
}

// Return the index of the first proposed operation, if any.
static int l_first_proposed(acl_device_op_queue_t *doq) {
  acl_assert_locked();
  if (!doq)
    return ACL_OPEN;
  if (doq->last_committed != ACL_OPEN)
    return doq->op[doq->last_committed].link;
  return doq->first_live; // This is ACL_OPEN if there are no proposed ops.
}

int acl_first_proposed_device_op_idx(acl_device_op_queue_t *doq) {
  return l_first_proposed(doq);
}

static void l_dump_op(const char *str, acl_device_op_t *op) {
  acl_assert_locked();

  if (debug_mode > 0) {
    static const char *status_name[] = {"CL_COMPLETE", "CL_RUNNING",
                                        "CL_SUBMITTED", "CL_QUEUED",
                                        "ACL_PROPOSED"};

    if (op) {
      acl_print_debug_msg(
          "%s op[%d] { type:%s status:%s(%d) execStatus:%s(%d) group:%d "
          "first:%d last:%d link:%d printfBytes:%d }\n",
          (str ? str : ""), op->id, l_type_name(op->info.type),
          (op->status >= 0 ? status_name[op->status] : "ERR"), op->status,
          (op->execution_status >= 0 ? status_name[op->execution_status]
                                     : "ERR"),
          op->execution_status, op->group_id, op->first_in_group,
          op->last_in_group, op->link, op->info.num_printf_bytes_pending);
    }
  }
}

///////////////////////
// External functions

void acl_init_device_op_queue(acl_device_op_queue_t *doq) {
  acl_init_device_op_queue_limited(doq, ACL_MAX_DEVICE_OPS);
}

void acl_init_device_op_queue_limited(acl_device_op_queue_t *doq,
                                      int max_allowed) {
  int i;
  acl_assert_locked();

  if (!doq)
    return;
  doq->num_committed = 0;
  doq->num_proposed = 0;
  doq->max_ops = max_allowed;
  for (i = 0; i < max_allowed; i++) {
    doq->op[i].id = i;
    acl_device_op_reset_device_op(doq->op + i);
  }

  // The live lists are all empty.
  doq->first_live = ACL_OPEN;
  doq->last_committed = ACL_OPEN;
  doq->last_live = ACL_OPEN;

  // Thread the free list
  doq->first_free = 0;
  for (i = 0; i < max_allowed - 1; i++) {
    doq->op[i].link = i + 1;
  }
  doq->op[max_allowed - 1].link = ACL_OPEN;

  // Set callback info.
  // Set up the standard functions.
  // These are overridden during mock testing.
  doq->user_data = 0;
  doq->launch_kernel = acl_launch_kernel;
  doq->transfer_buffer = acl_mem_transfer_buffer;
  doq->process_printf = acl_process_printf_buffer;
  doq->program_device = acl_program_device;
  doq->migrate_buffer = acl_mem_migrate_buffer;
  doq->usm_memcpy = acl_usm_memcpy;
  doq->hostpipe_read = acl_read_program_hostpipe;
  doq->hostpipe_write = acl_write_program_hostpipe;
  doq->log_update = 0;

  for (i = 0; i < ACL_MAX_DEVICE; i++) {
    doq->devices[i] = NULL;
  }

  STATS(doq->stats = acl_device_op_stats_t());
}

acl_device_op_conflict_type_t acl_device_op_conflict_type(acl_device_op_t *op) {
  acl_device_op_conflict_type_t result = ACL_CONFLICT_NONE;
  cl_event event = NULL;
  acl_assert_locked();

  if (op) {
    switch (op->info.type) {
    case ACL_DEVICE_OP_KERNEL:
      result = op->info.num_printf_bytes_pending ? ACL_CONFLICT_MEM
                                                 : ACL_CONFLICT_KERNEL;
      break;
    case ACL_DEVICE_OP_MEM_TRANSFER_READ:
      result = ACL_CONFLICT_MEM_READ;
      break;
    case ACL_DEVICE_OP_MEM_TRANSFER_WRITE:
      result = ACL_CONFLICT_MEM_WRITE;
      break;
    case ACL_DEVICE_OP_MEM_TRANSFER_COPY:
      result = ACL_CONFLICT_MEM;
      break;
    case ACL_DEVICE_OP_MEM_MIGRATION:
      result = ACL_CONFLICT_MEM;
      break;
    case ACL_DEVICE_OP_USM_MEMCPY:
      event = op->info.event;
      if (acl_event_is_valid(event)) {
        if (event->cmd.info.usm_xfer.src_on_host) {
          result = (event->cmd.info.usm_xfer.dst_on_host)
                       ? ACL_CONFLICT_NONE
                       : ACL_CONFLICT_MEM_WRITE;
        } else {
          result = (event->cmd.info.usm_xfer.dst_on_host)
                       ? ACL_CONFLICT_MEM_READ
                       : ACL_CONFLICT_MEM;
        }
      }
      break;
    case ACL_DEVICE_OP_REPROGRAM:
      result = ACL_CONFLICT_REPROGRAM;
      break;
    case ACL_DEVICE_OP_HOSTPIPE_READ:
      result = ACL_CONFLICT_HOSTPIPE_READ;
      break;
    case ACL_DEVICE_OP_HOSTPIPE_WRITE:
      result = ACL_CONFLICT_HOSTPIPE_WRITE;
      break;
    case ACL_DEVICE_OP_NONE:
    case ACL_NUM_DEVICE_OP_TYPES:
      result = ACL_CONFLICT_NONE;
      break;

      // Intentionally omit the default clause so the compiler
      // enforces adding a case for new op types
    }
  }
  return result;
}

acl_device_op_t *acl_propose_device_op(acl_device_op_queue_t *doq,
                                       acl_device_op_type_t type,
                                       cl_event event) {
  return acl_propose_indexed_device_op(doq, type, event, 0);
}

acl_device_op_t *acl_propose_indexed_device_op(acl_device_op_queue_t *doq,
                                               acl_device_op_type_t type,
                                               cl_event event,
                                               unsigned int index) {
  acl_device_op_t *result;
  int idx = 0;
  acl_assert_locked();
  if (!doq)
    return 0;

  // Determine where we'll write this one.
  if (doq->first_free == ACL_OPEN) {
    // No space.
    return 0;
  }
  // Use the first node from the free list.
  idx = doq->first_free;
  result = doq->op + idx;
  doq->first_free = result->link;
  result->link = ACL_OPEN;
  if (doq->first_live == ACL_OPEN) {
    doq->first_live = idx;
  }
  if (doq->last_live != ACL_OPEN) {
    doq->op[doq->last_live].link = idx;
  }
  doq->last_live = idx;
  if ((doq->last_committed != ACL_OPEN) &&
      (doq->op[doq->last_committed].link == ACL_OPEN)) {
    doq->op[doq->last_committed].link = idx;
  }

  doq->num_proposed++;

  acl_device_op_reset_device_op(result);

  result->status = ACL_PROPOSED;
  result->execution_status = ACL_PROPOSED;
  result->info.type = type;
  result->info.event = event;
  result->info.index = index;
  result->conflict_type = acl_device_op_conflict_type(result);

  return result;
}

void acl_forget_proposed_device_ops(acl_device_op_queue_t *doq) {
  // Forget all proposed operations.
  // Two things to do:
  //    - reset first_proposed
  //    - roll back num_ops

  acl_assert_locked();

  if (!doq)
    return;
  doq->num_proposed = 0;
  if ((doq->last_live != ACL_OPEN) && (doq->last_live != doq->last_committed)) {
    // There are proposed operations.
    // Put them back on the free list.
    // Connect the end of the proposed list to start of the free list.
    doq->op[doq->last_live].link = doq->first_free;
    // Connect the free list head to the start of the proposed list.
    doq->first_free = l_first_proposed(doq);

    doq->last_live = doq->last_committed;
    if (doq->last_committed == ACL_OPEN) {
      doq->first_live = ACL_OPEN;
    } else {
      doq->op[doq->last_committed].link = ACL_OPEN;
    }
  }

  // Nudge the scheduler.
  // Free up any completed commands from the front of the queue.
  (void)acl_update_device_op_queue(doq);
}

void acl_commit_proposed_device_ops(acl_device_op_queue_t *doq) {
  // Convert proposed operations into committed operations.
  int idx, i;
  int group_id;
  acl_assert_locked();

  if (!doq)
    return;
  if (!doq->num_proposed)
    return;

  // All the device ops get a group id that is the same as the op id of
  // the last op.
  group_id = doq->last_live;

  // Now sweep through all proposed operations and update status and group id.
  for (idx = l_first_proposed(doq), i = 0; idx != ACL_OPEN;
       idx = doq->op[idx].link, i++) {
    acl_device_op_t *op = doq->op + idx;

    op->status = CL_QUEUED;
    op->execution_status = CL_QUEUED;
    op->group_id = group_id;
    op->first_in_group = (i == 0);
    op->last_in_group = (i == doq->num_proposed - 1);
    l_dump_op("commit", op);
  }
  doq->num_committed += doq->num_proposed;
  doq->num_proposed = 0;

  doq->last_committed = doq->last_live;

  // Nudge the scheduler.
  acl_update_device_op_queue(doq);
}

unsigned acl_update_device_op_queue(acl_device_op_queue_t *doq) {
  unsigned num_updates = 0;
  unsigned local_num_updates = 0;
  acl_assert_locked();
  do {
    local_num_updates = l_update_device_op_queue_once(doq);
    num_updates += local_num_updates;
  } while (local_num_updates);
  return num_updates;
}

// Find out which devices are affected by a device op
// The device op must still be active, or you could dereference an invalid
// pointer through a dead or recycled cl_event object.
static unsigned
l_get_devices_affected_for_op(acl_device_op_t *op, unsigned int physical_ids[],
                              acl_device_op_conflict_type_t conflicts[]) {
  unsigned int num_devices_affected = 0;
  // The precondition of the function is device op must be active
  assert(op);
  cl_event event = op->info.event;
  acl_assert_locked();

  if (op) {
    switch (op->info.type) {
    case ACL_DEVICE_OP_KERNEL: {
      if (acl_event_is_valid(event)) {
        physical_ids[0] =
            event->cmd.info.ndrange_kernel.device->def.physical_device_id;
        conflicts[0] = ACL_CONFLICT_KERNEL;
        num_devices_affected = 1;
      }
    } break;
    case ACL_DEVICE_OP_REPROGRAM: {
      if (acl_event_is_valid(event)) {
        auto *dev_prog =
            event->cmd.type == CL_COMMAND_PROGRAM_DEVICE_INTELFPGA
                ? event->cmd.info.eager_program->get_dev_prog()
                : event->cmd.info.ndrange_kernel.dev_bin->get_dev_prog();

        physical_ids[0] = dev_prog->device->def.physical_device_id;
        conflicts[0] = ACL_CONFLICT_REPROGRAM;
        num_devices_affected = 1;
      }
    } break;
    case ACL_DEVICE_OP_MEM_TRANSFER_READ:
    case ACL_DEVICE_OP_MEM_TRANSFER_WRITE:
    case ACL_DEVICE_OP_MEM_TRANSFER_COPY:
      if (acl_event_is_valid(event) &&
          acl_command_queue_is_valid(event->command_queue)) {
        // Check the IDs of devices from the memory locations associated with
        // the transfer
        cl_mem src_mem = event->cmd.info.mem_xfer.src_mem;
        cl_mem dst_mem = event->cmd.info.mem_xfer.dst_mem;
        cl_bool device_supports_any_svm = acl_svm_device_supports_any_svm(
            event->command_queue->device->def.physical_device_id);
        cl_bool device_supports_physical_memory =
            acl_svm_device_supports_physical_memory(
                event->command_queue->device->def.physical_device_id);
        num_devices_affected = 0;

        // Memory is on the device if all of these are true:
        //   The memory is not SVM or the device does not support SVM.
        //   The device support physical memory
        //   The memory is in the global memory region
        // Note that a very similar check is made in
        // l_get_devices_affected_for_op, l_mem_transfer_buffer_explicitly and
        // l_dump_printf_buffer
        if ((!src_mem->is_svm || !device_supports_any_svm) &&
            (device_supports_physical_memory) &&
            (src_mem->block_allocation->region == &(acl_platform.global_mem))) {
          // src is on host
          assert(!src_mem->allocation_deferred);
          physical_ids[num_devices_affected] =
              ACL_GET_PHYSICAL_ID(src_mem->block_allocation->range.begin);
          conflicts[num_devices_affected] = ACL_CONFLICT_MEM_WRITE;
          num_devices_affected++;
        }
        if ((!dst_mem->is_svm || !device_supports_any_svm) &&
            (device_supports_physical_memory) &&
            (dst_mem->block_allocation->region == &(acl_platform.global_mem))) {
          // dst is on host
          assert(!dst_mem->allocation_deferred);
          physical_ids[num_devices_affected] =
              ACL_GET_PHYSICAL_ID(dst_mem->block_allocation->range.begin);
          conflicts[num_devices_affected] = ACL_CONFLICT_MEM_READ;
          num_devices_affected++;
        }
        // intra device copy
        if (num_devices_affected == 2 && physical_ids[0] == physical_ids[1]) {
          num_devices_affected = 1;
          conflicts[0] = ACL_CONFLICT_MEM;
        }
      }
      break;
    case ACL_DEVICE_OP_MEM_MIGRATION: {
      if (acl_event_is_valid(event) &&
          acl_command_queue_is_valid(event->command_queue)) {
        cl_bool physical_id_affected[ACL_MAX_DEVICE];
        acl_mem_migrate_t memory_migration;
        // Check the IDs of devices from the memory locations associated with
        // the transfer
        acl_mem_migrate_wrapper_t *src_mem_list;
        cl_bool device_supports_any_svm = acl_svm_device_supports_any_svm(
            event->command_queue->device->def.physical_device_id);
        cl_bool device_supports_physical_memory =
            acl_svm_device_supports_physical_memory(
                event->command_queue->device->def.physical_device_id);
        unsigned int i;
        num_devices_affected = 0;

        if (event->cmd.type == CL_COMMAND_MIGRATE_MEM_OBJECTS) {
          memory_migration = event->cmd.info.memory_migration;
        } else {
          memory_migration = event->cmd.info.ndrange_kernel.memory_migration;
        }

        src_mem_list = memory_migration.src_mem_list;
        for (i = 0; i < ACL_MAX_DEVICE; ++i) {
          physical_id_affected[i] = CL_FALSE;
        }

        for (i = 0; i < memory_migration.num_mem_objects; ++i) {
          // Source memory is on a device so that device is affected
          if ((!src_mem_list[i].src_mem->is_svm || !device_supports_any_svm) &&
              device_supports_physical_memory &&
              src_mem_list[i].src_mem->block_allocation->region ==
                  &(acl_platform.global_mem)) {
            physical_id_affected[ACL_GET_PHYSICAL_ID(
                src_mem_list[i].src_mem->block_allocation->range.begin)] =
                CL_TRUE;
          }

          // destination memory is affected:
          physical_id_affected[src_mem_list[i].destination_physical_device_id] =
              CL_TRUE;
        }

        for (i = 0; i < ACL_MAX_DEVICE; ++i) {
          if (physical_id_affected[i]) {
            physical_ids[num_devices_affected] = i;
            conflicts[num_devices_affected] = ACL_CONFLICT_MEM;
            num_devices_affected++;
          }
        }
      }
    } break;
    case ACL_DEVICE_OP_USM_MEMCPY:
      if (acl_event_is_valid(event) &&
          acl_command_queue_is_valid(event->command_queue)) {
        physical_ids[0] = event->command_queue->device->def.physical_device_id;
        conflicts[0] = acl_device_op_conflict_type(op);
        num_devices_affected = 1;
      }
      break;
    case ACL_DEVICE_OP_HOSTPIPE_READ:
    case ACL_DEVICE_OP_HOSTPIPE_WRITE:
      if (acl_event_is_valid(event) &&
          acl_command_queue_is_valid(event->command_queue)) {
        physical_ids[0] = event->command_queue->device->def.physical_device_id;
        conflicts[0] = acl_device_op_conflict_type(op);
        num_devices_affected = 1;
      }
      break;
    case ACL_DEVICE_OP_NONE:
    case ACL_NUM_DEVICE_OP_TYPES:
      break;
    }
  }
  if (num_devices_affected == 0) {
    // This case is only valid for unit tests
    // Make assumptions on which devices are affected
    // Possible TODO to add for Hostpipe read and write
    if (event && event->context && op) {
      if (event->context->num_devices >= 2 &&
          op->info.type != ACL_DEVICE_OP_KERNEL &&
          op->info.type != ACL_DEVICE_OP_REPROGRAM) {
        switch (op->info.type) {
        case ACL_DEVICE_OP_MEM_TRANSFER_READ:
          conflicts[0] = ACL_CONFLICT_MEM_READ;
          conflicts[1] = ACL_CONFLICT_MEM_WRITE;
          break;
        case ACL_DEVICE_OP_MEM_TRANSFER_WRITE:
          conflicts[0] = ACL_CONFLICT_MEM_WRITE;
          conflicts[1] = ACL_CONFLICT_MEM_READ;
          break;
        default:
          conflicts[0] = acl_device_op_conflict_type(op);
          conflicts[1] = conflicts[0];
          break;
        }
        physical_ids[0] = event->context->device[0]->def.physical_device_id;
        physical_ids[1] = event->context->device[1]->def.physical_device_id;
        num_devices_affected = 2;
      } else {
        physical_ids[0] = event->context->device[0]->def.physical_device_id;
        conflicts[0] = acl_device_op_conflict_type(op);
        num_devices_affected = 1;
      }
    } else {
      physical_ids[0] = 0;
      conflicts[0] = acl_device_op_conflict_type(op);
      num_devices_affected = 1;
    }
  }
  return num_devices_affected;
}

/**
 * Follows the same checking pattern as acl_mem.c:acl_mem_migrate_buffer(),
 * except doesn't execute the memory operation. This check is used to tell if a
 * migration associated with an op is safe to execute at the current time. It is
 * safe to execute the memory operation if memory is already at destination, or
 * no other running kernel is using that memory reagion.
 */
static int l_is_noop_migration(acl_device_op_t *op) {
  cl_event event = op->info.event;
  acl_assert_locked();

  assert(acl_event_is_valid(event));
  assert(acl_command_queue_is_valid(event->command_queue));
  acl_mem_migrate_t memory_migration;
  unsigned int physical_id =
      event->command_queue->device->def.physical_device_id;
  cl_bool device_supports_any_svm =
      acl_svm_device_supports_any_svm(physical_id);
  cl_bool device_supports_physical_memory =
      acl_svm_device_supports_physical_memory(physical_id);
  unsigned int index = op->info.index;

  assert(op->info.type == ACL_DEVICE_OP_MEM_MIGRATION);
  assert(event->cmd.type !=
         CL_COMMAND_MIGRATE_MEM_OBJECTS); // This is a task or NDRange
  memory_migration = event->cmd.info.ndrange_kernel.memory_migration;
  assert(index < memory_migration.num_mem_objects);

  const cl_mem src_mem = memory_migration.src_mem_list[index].src_mem;
  acl_block_allocation_t *src_mem_block;
  const unsigned int dest_device =
      memory_migration.src_mem_list[index].destination_physical_device_id;
  const unsigned int dest_mem_id =
      memory_migration.src_mem_list[index].destination_mem_id;

  assert(src_mem->reserved_allocations[dest_device].size() > dest_mem_id);
  assert(src_mem->reserved_allocations[dest_device][dest_mem_id] != NULL);
  src_mem_block = src_mem->block_allocation;

  if (src_mem->allocation_deferred) {
    src_mem_block = src_mem->reserved_allocations[dest_device][dest_mem_id];
  }

  // Explicitly not checking if memory is mapped or unmapped. CommandQueue
  // deals with it is safe to execute migrates with subbuffers or not in case of
  // fast kernel relaunch, otherwise it is the users responsibility
  if ((src_mem->is_svm && device_supports_any_svm) ||
      (!device_supports_physical_memory)) {
    return 1; // Do nothing for SVM
  } else if (src_mem_block ==
                 src_mem->reserved_allocations[dest_device][dest_mem_id] &&
             src_mem->mem_cpy_host_ptr_pending != 1) {
    // If memory is already at the destination, do nothing
    // Make sure any pending migrations have happened!
    return 1;
  }

  return 0;
}

/**
 * Iterates over all kernel_ops in the device_op_queue and returns true if any
 * of them are operating on the same memory regions as the provided MEM_MIGRATE.
 */
static int l_is_mem_in_use(acl_device_op_t *base_op,
                           acl_device_op_queue_t *doq) {
  // Get memory blocks
  acl_assert_locked();
  cl_event event = base_op->info.event;
  assert(acl_event_is_valid(event));
  acl_mem_migrate_t memory_migration_base;
  unsigned int index = base_op->info.index;
  assert(base_op->info.type == ACL_DEVICE_OP_MEM_MIGRATION);
  memory_migration_base = event->cmd.info.ndrange_kernel.memory_migration;
  assert(index < memory_migration_base.num_mem_objects);

  const cl_mem src_mem = memory_migration_base.src_mem_list[index].src_mem;
  acl_block_allocation_t *src_mem_block = src_mem->block_allocation;
  const unsigned int dest_device =
      memory_migration_base.src_mem_list[index].destination_physical_device_id;
  const unsigned int dest_mem_id =
      memory_migration_base.src_mem_list[index].destination_mem_id;
  acl_block_allocation_t *dst_mem_block =
      src_mem->reserved_allocations[dest_device][dest_mem_id];

  // Iterate over ops, comparing memory blocks
  acl_mem_migrate_t memory_migration;
  acl_device_op_t *op;
  for (int i = doq->first_live; i != ACL_OPEN && i != l_first_proposed(doq);
       i = doq->op[i].link) {
    op = doq->op + i;

    if (op->status > CL_SUBMITTED || op->status <= CL_COMPLETE ||
        op->info.type != ACL_DEVICE_OP_KERNEL) {
      continue;
    }

    memory_migration = op->info.event->cmd.info.ndrange_kernel.memory_migration;
    for (unsigned j = 0; j < memory_migration.num_mem_objects; j++) {
      if (memory_migration.src_mem_list[j].src_mem->block_allocation ==
              src_mem_block ||
          memory_migration.src_mem_list[j].src_mem->block_allocation ==
              dst_mem_block) {
        return 1;
      }
    }
  }
  return 0;
}

// Update the device operation queue.
//
// Perform several tasks:
//
// - Clean up complete operations.
//
// - Start (or restart) queued operations if they don't conflict
// with running operations.
//
//
// Note that the conflict type of an operation is dynamic.  So we must take
// special care when deciding to start an operation (or unstall a printf
// kernel).
//
// The algorithm is as follows:
//    - Scan all operations on the queue, looking for pending operations,
//    and noting their conflict types.
//    - Then kick off (or restart) the earliest unsubmitted operation which
//    does not conflict with a pending operation.
//    (And that kicked off operation now becomes in flight.)
unsigned l_update_device_op_queue_once(acl_device_op_queue_t *doq) {
  unsigned num_updates = 0;
  unsigned some_ops_need_submission = 0;
  int idx;
  unsigned k;
  int last_live_group;
  int keep_going;
  acl_assert_locked();

  // We need to know whether any operations are pending, or waiting to
  // run, organized by conflict type.
  // This is the id of the earliest pending operation, by conflict
  // type.
  int conflicts_on_device[ACL_MAX_DEVICE][ACL_NUM_CONFLICT_TYPES];

  STATS(acl_device_op_stats_t *stats = 0;)
  acl_assert_locked();

  // Reset pending IDs, and full duplex matrix
  memset(conflicts_on_device, 0,
         ACL_MAX_DEVICE * ACL_NUM_CONFLICT_TYPES * sizeof(int));

  if (!doq)
    return 0;

  STATS(stats = &(doq->stats);)
  STATS(stats->num_queue_updates++;)

#define FOREACH_OP                                                             \
  for (idx = doq->first_live, keep_going = 1;                                  \
       keep_going && idx != ACL_OPEN && idx != l_first_proposed(doq);          \
       idx = doq->op[idx].link) {                                              \
    acl_device_op_t *op = doq->op + idx;

#define FOREACH_OP_END }

  // First pass.
  // Submit (or unstall) operations as possible.
  //
  // For speed, we do a prescan to see if any operations *might* need
  // submitting (or printf processing).  In the common case there are none
  // and we can skip the relatively expensive checks for conflicts.
  //
  // Determine the earliest operation that needs something done but
  // which does not conflict with a pending operation.
  // There are two cases:
  //    The operation is CL_QUEUED and it does not conflict with a
  //    pending operation.
  //
  //    The operation is a CL_RUNNING kernel that is stalled on printf.
  //    It notionally conflicts with *itself*, but we can detect that
  //    case

  // See if any operations need to be submitted (or have printf bytes
  // picked up).
  // It's tempting to just keep a count of number of operations in
  // CL_QUEUED state.  That's easy.
  // But the tricky part is knowing how many require printf processing.
  // The num_printf_bytes_pending field is updated during an IRQ and in an
  // IRQ we can't grab a mutex to protect a *shared* counter variable.
  // Also, the device op queue needs to handle operations for multiple device
  // binaries so we can't easily short circuit the check for the common
  // case where there are no printfs in a *particular* binary.
  // So just do the basic scan.
  some_ops_need_submission = 0;
  FOREACH_OP {
    if ((CL_QUEUED == op->status) || op->info.num_printf_bytes_pending) {
      some_ops_need_submission = 1;
      keep_going = 0;
    }
  }
  FOREACH_OP_END

  if (some_ops_need_submission) {
    // What is the group id of the last live operation we've looked at?
    // This is used to make sure only one operation is active per group.
    last_live_group = -1; // Initialize with an invalid group id.
    unsigned block_noop_pruning = 0;

    FOREACH_OP {
      // Check exclusion groups. Only one operation from a group may be in
      // flight at a time.
      unsigned j;
      int is_conflicting = (last_live_group == op->group_id);
      unsigned int num_devices_affected, physical_ids[ACL_MAX_DEVICE];
      acl_device_op_conflict_type_t conflicts[ACL_MAX_DEVICE];
      STATS(stats->num_exclusion_checks++;)

      if (op->status <= CL_QUEUED) {
        num_devices_affected =
            l_get_devices_affected_for_op(op, physical_ids, conflicts);

        // cache the devices to check their support for concurrent operations
        for (k = 0; k < num_devices_affected && !is_conflicting; k++) {
          if (!doq->devices[physical_ids[k]] && op->info.event &&
              op->info.event->context) {
            for (j = 0; j < op->info.event->context->num_devices; j++) {
              if (op->info.event->context->device[j]->def.physical_device_id ==
                  physical_ids[k]) {
                doq->devices[physical_ids[k]] =
                    &(op->info.event->context->device[j]->def);
              }
            }
          }
        }

        if (!is_conflicting &&
            (CL_QUEUED == op->status || op->info.num_printf_bytes_pending)) {
          // Potentially need to submit it now.
          STATS(stats->num_conflict_checks++;)

          // See if it conflicts with an pending operation.
          for (k = 0; k < num_devices_affected && !is_conflicting; k++) {
            for (j = 0; j < ACL_NUM_CONFLICT_TYPES && !is_conflicting; j++) {
              unsigned int physical_id = physical_ids[k];
              if (conflicts_on_device[physical_id][j] > 0) {
                const acl_device_op_conflict_type_t conflict_type =
                    conflicts[k];
                if (doq->devices[physical_id] &&
                    doq->devices[physical_id]->max_inflight_mem_ops == 2) {
                  is_conflicting =
                      is_conflicting ||
                      conflict_matrix_full_duplex[conflict_type][j];
                } else {
                  is_conflicting =
                      is_conflicting ||
                      conflict_matrix_half_duplex[conflict_type][j];
                }
              }
            }
          }

          if (op->info.event &&
              (op->info.event->cmd.type == CL_COMMAND_TASK ||
               op->info.event->cmd.type == CL_COMMAND_NDRANGE_KERNEL)) {
            const cl_kernel kernel =
                op->info.event->cmd.info.ndrange_kernel.kernel;
            const cl_device_id device =
                op->info.event->cmd.info.ndrange_kernel.device;
            unsigned int fast_launch_depth =
                kernel->accel_def->fast_launch_depth;
            if (op->info.type == ACL_DEVICE_OP_MEM_MIGRATION) {
              if (l_is_noop_migration(op) && !block_noop_pruning) {
                // ignore if we conflict on the conflict matrix
                // this is a noop and safe to execute
                is_conflicting = 0;
              } else {
                // Prevents moving memory that is currently being used
                is_conflicting |= l_is_mem_in_use(op, doq);
                block_noop_pruning = 1;
              }
            } else if (op->info.type == ACL_DEVICE_OP_KERNEL &&
                       op->status == CL_QUEUED) {
              // If Kernel-op make sure the relaunch buffer is not full
              // Ignore this check if this is getting resubmitted due to a
              // printf (status will not be QUEUED)

              // Check all the submitted and running kernels if they are running
              // on the same accelerator if they are make sure we don't surpass
              // the fast relaunch depth.
              unsigned num_on_device = 0;
              for (int i = doq->first_live;
                   i != ACL_OPEN && i != l_first_proposed(doq);
                   i = doq->op[i].link) {
                acl_device_op_t *searched_op = doq->op + i;
                if (searched_op->id == op->id) {
                  break; // reached the current op
                }
                if (searched_op->info.type == ACL_DEVICE_OP_KERNEL &&
                    (searched_op->status == CL_SUBMITTED ||
                     searched_op->status == CL_RUNNING)) {
                  const cl_kernel searched_kernel =
                      searched_op->info.event->cmd.info.ndrange_kernel.kernel;
                  const cl_device_id searched_device =
                      searched_op->info.event->cmd.info.ndrange_kernel.device;
                  if (searched_kernel->accel_def->id == kernel->accel_def->id &&
                      searched_device->id == device->id) {
                    // same accelerator, same device, different op
                    num_on_device++;
                  }
                }
              }
              if (num_on_device > fast_launch_depth) {
                is_conflicting = 1;
              }
            }
          } else if (op->info.event &&
                     (op->info.type == ACL_DEVICE_OP_MEM_TRANSFER_READ ||
                      op->info.type == ACL_DEVICE_OP_MEM_TRANSFER_WRITE ||
                      op->info.type == ACL_DEVICE_OP_MEM_TRANSFER_COPY ||
                      op->info.type == ACL_DEVICE_OP_HOSTPIPE_READ ||
                      op->info.type == ACL_DEVICE_OP_HOSTPIPE_WRITE)) {
            if (!acl_mem_op_requires_transfer(op->info.event->cmd)) {
              is_conflicting = 0;
            }
          }

          if (!is_conflicting) {
            // Good to go (or go again).
            acl_submit_device_op(doq, op);
            num_updates++;
            STATS(stats->num_submits++;)

            // Don't try to submit another command on this round.
            // I don't want to have to bother with updating the
            // inflight_id.  ( Could just set inflight_id[ conflict_type ] = idx
            // ? )
            keep_going = 0;
            last_live_group = op->group_id;
          }
        }
        if (CL_RUNNING <= op->status) {
          // It's pending
          last_live_group = op->group_id;
          STATS(stats->num_live_op_pending_calcs++;)

          // Force this operation to potentially conflict with any later
          // operation. This is done to prevent kernels executing before a
          // reprogram
          for (k = 0; k < num_devices_affected; k++) {
            conflicts_on_device[physical_ids[k]][conflicts[k]]++;
          }
        }
      }
    }
    FOREACH_OP_END
  }

  // Second pass.
  // Process asynchronous status updates from the device.
  FOREACH_OP {
    switch (op->status) {
    default:
      keep_going = 0;
      break;

    case CL_QUEUED:
      STATS(stats->num_queued++;)
      break; // No status updates possible.

    case CL_SUBMITTED:
      // Check for status transition
      STATS(stats->num_submitted++;)
      if (op->execution_status <= CL_RUNNING) {
        op->status = CL_RUNNING;
        l_dump_op("status", op);

        // Running state transition happens when the first op in the group
        // signals that it has execution state of RUNNING
        // EXCEPT(!) for groups that contain kernel-ops. Those get only marked
        // as running when the kernel-op itself is running. This is done so that
        // only a single event in the command_queue is ever in a running state
        // event if the next kernel is doing its setup tasks (MEM_MIGRATEs)
        // in parallel with the previous kernel invocation. -Fast Kernel
        // Relaunch Feature
        if (op->info.event) { // protects against segfaults in unit tests, as no
                              // events are attached
          if (op->info.type == ACL_DEVICE_OP_KERNEL ||
              (op->first_in_group &&
               op->info.event->cmd.type != CL_COMMAND_TASK &&
               op->info.event->cmd.type != CL_COMMAND_NDRANGE_KERNEL)) {
            acl_post_status_to_owning_event(op, CL_RUNNING);
          }
        }

        if (op->info.type == ACL_DEVICE_OP_KERNEL && op->info.event) {
          acl_command_info_t cmd = op->info.event->cmd;
          int can_reset_invocation_wrapper = 1;
          // Release the invocation image.
          // But just once, in case someone else grabs it soon after the first
          // update. The event cmd.info should still be valid since only our own
          // host thread can release it.
          if (cmd.info.ndrange_kernel.serialization_wrapper) {
            // Only release if this event is not part of kernel serialized
            // execution, or it is the last time the event is submitted as part
            // of serialized exec. Otherwise, the event will be resubmitted with
            // the same invocation wrapper, so it is still needed.
            acl_dev_kernel_invocation_image_t *invocation =
                cmd.info.ndrange_kernel.serialization_wrapper->image;
            cl_ulong x = invocation->global_work_offset[0],
                     y = invocation->global_work_offset[1],
                     z = invocation->global_work_offset[2];
            if (x == invocation->global_work_size[0] - 1 &&
                y == invocation->global_work_size[1] - 1 &&
                z == invocation->global_work_size[2] - 1)
              can_reset_invocation_wrapper = 0;
          }
          if (can_reset_invocation_wrapper) {
            acl_kernel_invocation_wrapper_t *invocation_wrapper =
                cmd.info.ndrange_kernel.invocation_wrapper;
            if (invocation_wrapper->image->arg_value) {
              acl_delete_arr(invocation_wrapper->image->arg_value);
              invocation_wrapper->image->arg_value = 0;
            }
            acl_reset_ref_count(invocation_wrapper);
          }
        }

        // For test purposes.
        if (doq->log_update)
          doq->log_update(doq->user_data, op, CL_RUNNING);

        num_updates++;
      }
      break;

    case CL_RUNNING:
      // Check for status transition
      STATS(stats->num_running++;)
      if (op->execution_status <= CL_COMPLETE) {
        cl_event event = op->info.event;
        if (op->execution_status == CL_COMPLETE &&
            op->info.type == ACL_DEVICE_OP_KERNEL && event) {
          // If this is a kernel event, we might need to resubmit the kernel as
          // a part of serialization of workitem invariant ndranges.
          if (op->info.event->cmd.info.ndrange_kernel.serialization_wrapper) {
            acl_dev_kernel_invocation_image_t *invocation =
                op->info.event->cmd.info.ndrange_kernel.serialization_wrapper
                    ->image;
            cl_ulong x = invocation->global_work_offset[0],
                     y = invocation->global_work_offset[1],
                     z = invocation->global_work_offset[2];
            int resubmit_needed = 0;
            acl_print_debug_msg("global_work_offset = (%lu, %lu, %lu)\n", x, y,
                                z);
            assert(x < invocation->global_work_size[0] &&
                   y < invocation->global_work_size[1] &&
                   z < invocation->global_work_size[2]);
            if (z < invocation->global_work_size[2] - 1) {
              z++;
              resubmit_needed = 1;
            } else if (y < invocation->global_work_size[1] - 1) {
              z = 0;
              y++;
              resubmit_needed = 1;
            } else if (x < invocation->global_work_size[0] - 1) {
              z = 0;
              y = 0;
              x++;
              resubmit_needed = 1;
            }
            // else, no resubmit_needed since x ==
            // invocation->global_work_size[0]-1 && y == x <
            // invocation->global_work_size[1]-1 && z <
            // invocation->global_work_size[2]-1
            if (resubmit_needed) {
              //  - Keep track of how many times we have relaunched so far
              //  - Set the execution_status back to CL_RUNNING
              //  - Relaunch the kernel.
              invocation->global_work_offset[0] = x;
              invocation->global_work_offset[1] = y;
              invocation->global_work_offset[2] = z;
              // No changes to dependencies or conflicts are expected since the
              // event was already running.
              op->execution_status = CL_RUNNING;
              doq->launch_kernel(doq->user_data, op);
              break;
            } else {
              acl_kernel_invocation_wrapper_t *serialization_wrapper =
                  op->info.event->cmd.info.ndrange_kernel.serialization_wrapper;
              if (serialization_wrapper->image->arg_value) {
                acl_delete_arr(serialization_wrapper->image->arg_value);
                serialization_wrapper->image->arg_value = 0;
              }
              acl_reset_ref_count(serialization_wrapper);
            }
          }
        }
        // Get profiler data from performance counters in the kernel.
        // This must come before posting status to the owning event, or
        // we will never get the profile data out.
        (void)acl_process_profiler_scan_chain(op);

        if (op->last_in_group) {
          acl_post_status_to_owning_event(op, op->execution_status);
        }

        if (op->info.type == ACL_DEVICE_OP_KERNEL && event) {
          // Mark this accelerator as no longer being occupied.
          event->cmd.info.ndrange_kernel.dev_bin->get_dev_prog()->current_event
              [event->cmd.info.ndrange_kernel.kernel->accel_def] = nullptr;
        }

        // Cut the backpointer. No more interrupts will come.
        if (event)
          event->current_device_op = 0;

        // Make sure we don't post again.
        // This must come after reading profile data since it checks
        // the event status.
        op->status = CL_COMPLETE;
        l_dump_op("status", op);

        // For test purposes.
        if (doq->log_update)
          doq->log_update(doq->user_data, op, CL_COMPLETE);

        num_updates++;
      }
      break;

    case CL_COMPLETE:
      // Just waiting to be pruned.
      // Continue looking at the events that follow.
      STATS(stats->num_complete++;)
      break;
    }
  }
  FOREACH_OP_END

  // Third pass:  Prune events.
  //
  // A kernel can be in CL_COMPLETE state *and* have printf bytes pending.
  // (This is the normal case of a kernel that doesn't print much, and so
  // it doesn't overfill the printf buffer.)
  // Do not prune the kernel in that case. Wait for the next schedule
  // update loop to flush that printf buffer out.
  //
  // Need to iterate differently since we will be removing ops from the
  // committed list while we iterate.
  {
    int prev_live = ACL_OPEN;
    idx = doq->first_live;
    acl_print_debug_msg("   pruning: (%d) %d fp %d\n", prev_live, idx,
                        l_first_proposed(doq));
    while (idx != ACL_OPEN && idx != l_first_proposed(doq)) {
      acl_device_op_t *op = doq->op + idx;
      int nxt_idx =
          op->link; // Need to get this before mutating the operation link.

      if (debug_mode > 0)
        l_dump_op("   consider", op);
      if (op->status <= CL_COMPLETE &&
          (op->info.num_printf_bytes_pending == 0)) {
        // This operation is finished.  We can prune it.

        if (debug_mode > 0)
          l_dump_op("    pruning", op);

        // Tricky case. Propagate an error condition from one op to all
        // the remaining ones in the group.
        if (op->execution_status < 0 && !op->last_in_group &&
            op->id != doq->last_committed) {
          // Just update the next one.  It in turn will keep on propagating.
          int next_idx = op->link;
          doq->op[next_idx].execution_status = op->execution_status;
          doq->op[next_idx].timestamp[CL_COMPLETE] = op->timestamp[CL_COMPLETE];
          acl_print_debug_msg(" PROP op[%d] exec_status <-- %d\n", next_idx,
                              op->execution_status);
        }

        // Now pull this op off the committed list and put it back on the
        // free list.
        if (doq->first_live == idx) {
          doq->first_live = op->link;
        }
        if (doq->last_committed == idx) {
          doq->last_committed = prev_live;
        }
        if (doq->last_live == idx) {
          doq->last_live = prev_live;
        }

        if (prev_live != ACL_OPEN) {
          doq->op[prev_live].link = op->link;
        }
        op->link = doq->first_free;
        doq->first_free = idx;

        if (debug_mode > 0)
          l_dump_op("    pruned", op);

        doq->num_committed--;
        num_updates++;
      } else {
        prev_live = idx;
      }

      // Advance
      idx = nxt_idx;

      acl_print_debug_msg("   pruning: (%d) %d fp %d\n", prev_live, idx,
                          l_first_proposed(doq));
    }
  }
  return num_updates;
#undef FOREACH_OP
#undef FOREACH_OP_END
}

void acl_submit_device_op(acl_device_op_queue_t *doq, acl_device_op_t *op) {
  acl_assert_locked();

  if (!doq || !op)
    return;

  if (op->status == CL_QUEUED) {
    // The regular case.
    cl_event event = op->info.event;
    op->status = CL_SUBMITTED;
    op->timestamp[CL_SUBMITTED] = acl_get_hal()->get_timestamp();
    l_dump_op("status", op);
    // Only submit if not an error already.
    if (op->execution_status >= 0) {
      op->execution_status = CL_SUBMITTED;
      if (op->first_in_group) {
        acl_post_status_to_owning_event(op, CL_SUBMITTED);
      }

      // Establish the backpointer before actually starting the operation.
      // This backpointer is used to route HAL interrupt-based status
      // updates, so it must be in place before those interrupts can be
      // triggered.
      if (event)
        event->current_device_op = op;

#define DOIT(CALL, X)                                                          \
  if (doq->CALL)                                                               \
  doq->CALL(doq->user_data, X)
      switch (op->info.type) {
      case ACL_DEVICE_OP_KERNEL:
        DOIT(launch_kernel, op);
        break;
      case ACL_DEVICE_OP_MEM_TRANSFER_READ:
      case ACL_DEVICE_OP_MEM_TRANSFER_WRITE:
      case ACL_DEVICE_OP_MEM_TRANSFER_COPY:
        DOIT(transfer_buffer, op);
        break;
      case ACL_DEVICE_OP_REPROGRAM:
        DOIT(program_device, op);
        break;
      case ACL_DEVICE_OP_MEM_MIGRATION:
        DOIT(migrate_buffer, op);
        break;
      case ACL_DEVICE_OP_USM_MEMCPY:
        DOIT(usm_memcpy, op);
        break;
      case ACL_DEVICE_OP_HOSTPIPE_READ:
        DOIT(hostpipe_read, op);
        break;
      case ACL_DEVICE_OP_HOSTPIPE_WRITE:
        DOIT(hostpipe_write, op);
        break;
      default:
        break;
      }
    }
  } else if (op->info.num_printf_bytes_pending) {
    // Special case for a kernel stalled on printf, or just completed but
    // with some printf material to write out.
    // This entails a blocking memory transfer deep down the callstack.
    DOIT(process_printf, op);
    // Can't clear num_printf_bytes_pending here because that sets up a
    // race with the kernel.
    // Why? The process_printf call will unstall the kernel, which could
    // then very quickly block again and issue an IRQ.
    // So we have to clear the printf byte count before unstalling the
    // kernel. So that has to happen at a deeper level.
  }
#undef DOIT
}

void acl_set_device_op_execution_status(acl_device_op_t *op,
                                        cl_int new_status) {
  acl_assert_locked_or_sig();

  if (op) {
    cl_int effective_status = new_status;

    switch (new_status) {
    case CL_QUEUED:
    case CL_SUBMITTED:
    case CL_RUNNING:
    case CL_COMPLETE:
      l_record_milestone(op, (cl_profiling_info)new_status);
      break;
    default:
      if (new_status >= 0) {
        // Not given a valid status.  Internal error?
        // Change to a negative status so command queue processing works.

        // we can't call a user callback from inside a signal handler
        if (!acl_is_inside_sig()) {
          cl_context context = op->info.event->command_queue->context;
          if (context) {
            acl_context_callback(
                context, "Internal error: Setting invalid operation status "
                         "with positive value");
          }
        }
        effective_status = ACL_INVALID_EXECUTION_STATUS; // this is negative
      }
      if (effective_status < 0) {
        // An error condition.  Record as complete.
        l_record_milestone(op, CL_COMPLETE);
      }
      break;
    }

    op->execution_status = effective_status;
  }
}

void acl_post_status_to_owning_event(acl_device_op_t *op, int new_status) {
  acl_assert_locked();
  if (!op) {
    return;
  }
  cl_event event = op->info.event;
  if (!acl_event_is_valid(event)) {
    return;
  }
  if (!acl_command_queue_is_valid(event->command_queue)) {
    return;
  }
  cl_command_queue command_queue = event->command_queue;

  // Send the lower level execution_status because it might be
  // negative, to signal error.
  event->execution_status = new_status;
  if (new_status <= CL_COMPLETE) {
    event->timestamp[CL_COMPLETE] = op->timestamp[CL_COMPLETE];

    if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
      command_queue->completed_commands.push_back(event);
    }
    command_queue->num_commands_submitted--;
  } else {
    event->timestamp[new_status] = op->timestamp[new_status];

    if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
      acl_try_FastKernelRelaunch_ooo_queue_event_dependents(event);
    }
  }
}

///////////////////////
// Internal

void acl_device_op_reset_device_op(acl_device_op_t *op) {
  acl_assert_locked();

  op->status = CL_COMPLETE;
  op->execution_status = CL_COMPLETE;
  op->timestamp[CL_SUBMITTED] = 0;
  op->timestamp[CL_RUNNING] = 0;
  op->timestamp[CL_COMPLETE] = 0;
  op->timestamp[CL_QUEUED] = 0;
  op->first_in_group = 1;
  op->last_in_group = 1;
  op->group_id = op->id; // Normally each operation is in its own group.
  op->info.type = ACL_DEVICE_OP_NONE;
  op->info.event = 0;
  op->info.index = 0;
  op->info.num_printf_bytes_pending = 0;
}

static void l_record_milestone(acl_device_op_t *op,
                               cl_profiling_info milestone) {
  acl_assert_locked_or_sig();

  if (op) {
    cl_ulong ts = acl_get_hal()->get_timestamp();
    switch (milestone) {
    case CL_QUEUED:
    case CL_SUBMITTED:
    case CL_RUNNING:
    case CL_COMPLETE:
      acl_print_debug_msg(" devop[%d]->timestamp[%d] = %lu\n", op->id,
                          (int)(milestone), (unsigned long)(ts));
      op->timestamp[milestone] = ts;
      break;
    default:
      break;
    }
  }
}

ACL_EXPORT
void acl_device_op_dump_stats(void) {
#ifdef ACL_DEVICE_OP_STATS
  acl_device_op_stats_t *stats = &(acl_platform.device_op_queue.stats);
  acl_assert_locked();
  printf("Device op stats:\n");

#define PF(X)                                                                  \
  printf("  %-25s %12u %12.6f\n", #X, stats->num_##X,                          \
         ((float)stats->num_##X / (float)stats->num_queue_updates));
  PF(queue_updates)
  PF(exclusion_checks)
  PF(conflict_checks)
  PF(submits)
  PF(live_op_pending_calcs)
  PF(queued)
  PF(running)
  PF(complete)
  fflush(stdout);
#undef PF
#else
  printf("Device op stats are not available\n");
#endif
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

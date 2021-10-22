// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_DEVICE_OP_H
#define ACL_DEVICE_OP_H

#include "acl_types.h"
#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void acl_init_device_op_queue(acl_device_op_queue_t *device_op_queue);
void acl_init_device_op_queue_limited(acl_device_op_queue_t *device_op_queue,
                                      int max_allowed);

// Get the next device op, and claim it as a proposed device operation.
// Return NULL when there are no more.
acl_device_op_t *acl_propose_device_op(acl_device_op_queue_t *device_op_queue,
                                       acl_device_op_type_t type,
                                       cl_event event);
acl_device_op_t *
acl_propose_indexed_device_op(acl_device_op_queue_t *device_op_queue,
                              acl_device_op_type_t type, cl_event event,
                              unsigned int index);
int acl_first_proposed_device_op_idx(acl_device_op_queue_t *device_op_queue);

void acl_commit_proposed_device_ops(acl_device_op_queue_t *device_op_queue);
void acl_forget_proposed_device_ops(acl_device_op_queue_t *device_op_queue);

// Update the queue to reflect asynchronous completion of operations.
// Returns number of updates made.
unsigned acl_update_device_op_queue(acl_device_op_queue_t *device_op_queue);

// Set execution status.  This is to be called by the HAL.
void acl_set_device_op_execution_status(acl_device_op_t *op, cl_int new_status);

//////////////////
// These functions are used only internally to the module, but are exposed
// for test purposes.

// This is the standard callback the HAL should use to communicate most updates.
void acl_submit_device_op(acl_device_op_queue_t *device_op_queue,
                          acl_device_op_t *op);
void acl_post_status_to_owning_event(acl_device_op_t *op, int new_status);
acl_device_op_conflict_type_t acl_device_op_conflict_type(acl_device_op_t *op);
void acl_device_op_reset_device_op(acl_device_op_t *op);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

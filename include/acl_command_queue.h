// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_COMMAND_QUEUE_H
#define ACL_COMMAND_QUEUE_H

#include "acl.h"
#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Caller must have already checked that they we can add another.
void acl_command_queue_add_event(cl_command_queue command_queue,
                                 cl_event event);

#if defined(__cplusplus)
extern "C" {
#endif

// Process updates on the queue.
// This is part of the main event loop for the runtime.
//    Respond to status updates from devices
//    Process event completion
//
// Returns number of things updated.
// If it's 0 then we're idle and can "wait" a bit longer perhaps.
// If it's greater than zero, then we should continue updating the
// contexts's queues because perhaps we have finished a command and removed
// some event dependencies.  That might allow dependent events to become
// ready and hence we can launch their associated commands.
int acl_update_queue(cl_command_queue command_queue);
int acl_update_inorder_queue(cl_command_queue command_queue);
int acl_update_ooo_queue(cl_command_queue command_queue);

// Update data structures in response to async msgs from devices.
void acl_idle_update_queue(cl_command_queue command_queue);

// Delete the command queue if the command queue is no longer retained
void acl_delete_command_queue(cl_command_queue command_queue);

#if defined(__cplusplus)
} /* extern "C" */
#endif

/**
 *  Tries to fast-kernel-launch a dependent event. This can only happen under a
 *  set of conditions:
 *  * The parent is a submitted kernel
 *  * The dependent is the same kernel as the provided parent
 *  * The dependent's only unresolved dependency is the parent
 *  @param parent The event who's dependent are being assesed for fast-kernel-
 *  launch eligibility
 */
void acl_try_FastKernelRelaunch_ooo_queue_event_dependents(cl_event parent);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

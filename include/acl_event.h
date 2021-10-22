// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_EVENT_H
#define ACL_EVENT_H

#include "acl.h"
#include "acl_types.h"
#include "acl_visibility.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

void acl_reset_event(cl_event event);

// Create an event that depends on the given events.
// Execution status begins as CL_QUEUED.
// Consider this a step in what would be an Event constructor.
//
// Return CL_SUCCESS if all is ok.
// Can return
//    CL_OUT_OF_HOST_MEMORY when resources are exhausted
//    CL_INVALID_COMMAND_QUEUE if the command queue is invalid
//    CL_INVALID_CONTEXT if not all events are on the same context.
cl_int acl_create_event(cl_command_queue command_queue, cl_uint num_events,
                        const cl_event *events, // array of events to depend on
                        cl_command_type command_type, cl_event *new_event_ret);

// Return CL_SUCCESS if the list of events is non-empty and valid,
// and all are associated with the given context.
cl_int acl_check_events_in_context(
    cl_context context, cl_uint num_events,
    const cl_event *events // array of events to depend on
);
// Return CL_SUCCESS if the list of events is non-empty and valid
// and have consistent context.
// Otherwise return CL_INVALID_VALUE or other error status.
cl_int acl_check_events(cl_uint num_events,
                        const cl_event *events // array of events to depend on
);

// Delete an event if nothing else refers to it.
// Returns non-zero if it was deleted, 0 if not.
int acl_maybe_delete_event(cl_event event);

// Remove this event from all the dependency lists of downstream events.
// Returns number of notifications.
int acl_notify_dependent_events(cl_event event);

// Removes the dependency link between the given event
// and the first event that depends on it
// returns the dependent event removed
cl_event acl_remove_first_event_dependency(cl_event event);

// Is this event ready to be submitted?
// Make this a macro so that in the common case we don't incur
// a function call overhead.
#define acl_event_is_ready_to_run(E)                                           \
  ((E)->late_link == ACL_OPEN && (E)->execution_status == CL_QUEUED &&         \
   acl_event_resources_are_available(E))

// Are all the resources required by this command available?
// This is used to make sure we don't run the same kernel simultaneously
// from different command queues, because we only have one hardware
// block for the given kernel.
int acl_event_resources_are_available(cl_event event);

// The function that will notify the callback event functions registered via
// clSetEventCallback
void acl_event_callback(cl_event event, cl_int event_command_exec_status);

// This callback is used by the HAL to indicate progress from queued
// to running, and from running to complete.
ACL_EXPORT void acl_set_execution_status(cl_event event, int new_status);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif

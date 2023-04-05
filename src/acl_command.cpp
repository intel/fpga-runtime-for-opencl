// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <stdio.h>
#include <vector>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_command.h>
#include <acl_context.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_hostch.h>
#include <acl_kernel.h>
#include <acl_mem.h>
#include <acl_program.h>
#include <acl_svm.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_usm.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Commands
// ========

//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueBarrierIntelFPGA(cl_command_queue command_queue) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  // For in order queue, since every event is executed in sequence,
  // there is an implicit barrier after each event.
  // enqueue barrier does not need to do anything
  if (!(command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)) {
    return CL_SUCCESS;
  }
  // OpenCL 1.2 spec: If event_wait_list is NULL, then this particular command
  // waits until all previous enqueued commands to command_queue have completed.
  cl_int status = clEnqueueBarrierWithWaitList(command_queue, 0, 0, NULL);
  return status;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueBarrier(cl_command_queue command_queue) {
  return clEnqueueBarrierIntelFPGA(command_queue);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueMarkerIntelFPGA(cl_command_queue command_queue, cl_event *event) {
  cl_int result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (!event)
    return CL_INVALID_VALUE;

  result = acl_create_event(command_queue, 0, 0, CL_COMMAND_MARKER, event);

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMarker(cl_command_queue command_queue,
                                                cl_event *event) {
  return clEnqueueMarkerIntelFPGA(command_queue, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWaitForEventsIntelFPGA(
    cl_command_queue command_queue, cl_uint num_event, const cl_event *events) {
  cl_int result;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (num_event == 0 || events == 0) {
    return CL_INVALID_VALUE;
  }
  cl_event event = NULL;
  result = acl_create_event(command_queue, num_event, events,
                            CL_COMMAND_WAIT_FOR_EVENTS_INTELFPGA, &event);
  // release the user event
  clReleaseEvent(event);

  // Adjust for weird irregularity in the APIs...
  if (result == CL_INVALID_EVENT_WAIT_LIST) {
    result = CL_INVALID_EVENT;
  }

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWaitForEvents(
    cl_command_queue command_queue, cl_uint num_event, const cl_event *events) {
  return clEnqueueWaitForEventsIntelFPGA(command_queue, num_event, events);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clWaitForEventsIntelFPGA(cl_uint num_events, const cl_event *event_list) {
  int num_live;
  size_t num_iters = 0;
  cl_int result = CL_SUCCESS;
  const acl_hal_t *hal = acl_get_hal();
  cl_context context;
  bool first_yield_to_hal = true;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (num_events == 0 || event_list == 0) {
    return CL_INVALID_VALUE;
  }

#ifndef REMOVE_VALID_CHECKS
  result = acl_check_events(num_events, event_list);
  if (result != CL_SUCCESS) {
    return CL_INVALID_EVENT;
  }
#endif

  // Cache the context, in case the first event ends up being thrown away
  context = event_list[0]->context;

  do {
    cl_uint i;
    num_live = 0;

    // Update data structures in response to async msgs.
    acl_idle_update(context);

    for (i = 0; i < num_events && num_live == 0; i++) {
      if (!acl_event_is_done(event_list[i])) {
        acl_dump_event(event_list[i]);
        num_live++;
      }
    }

    if (num_live > 0) {
      // Wait until signaled, without burning CPU.
      if (!hal->yield) {
        acl_wait_for_device_update(context);
      } else {
        // If all events we are waiting for didn't complete after yielding
        // to hal once, let other threads run too
        if (!first_yield_to_hal) {
          acl_yield_lock_and_thread();
        }
        first_yield_to_hal = false;
        hal->yield(context->num_devices, context->device);
      }
    }

    if (debug_mode > 0) {
      num_iters++;
    }

  } while (num_live > 0);

  if (debug_mode > 0) {
    printf("waitforevents %zu iters\n", num_iters);
    fflush(stdout);
  }

#ifdef ACL_120
  {
    // In OpenCL 1.1 and later we return a special status if any event has
    // negative execution status (signalling an error). But for 1.0 there is no
    // special result.
    cl_uint i = 0;
    for (i = 0; i < num_events; ++i) {
      if (event_list[i]->execution_status < 0)
        return CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST;
    }
  }
#endif

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clWaitForEvents(cl_uint num_events,
                                                const cl_event *event_list) {
  return clWaitForEventsIntelFPGA(num_events, event_list);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMarkerWithWaitListIntelFPGA(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_int result;
  cl_event ret_event = NULL;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  // Spec says:
  // "enqueues a marker command which waits for either a list of events to
  // complete, or if the list is empty it waits for all commands previously
  // enqueued in command_queue to complete before it completes"
  if (!event) {
    event = &ret_event;
  }
  if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE &&
      num_events_in_wait_list == 0 && !command_queue->commands.empty()) {
    size_t num_events = command_queue->commands.size();
    std::vector<cl_event> all_events;
    all_events.reserve((size_t)num_events);
    for (cl_event e : command_queue->commands) {
      all_events.push_back(e);
    }
    result = acl_create_event(command_queue, (cl_uint)num_events,
                              all_events.data(), CL_COMMAND_MARKER, event);
  } else {
    // with empty event list in-order queues will just work, because Marker
    // event will be added to end of queue and only complete when everytjing
    // infront of it has completed
    result = acl_create_event(command_queue, num_events_in_wait_list,
                              event_wait_list, CL_COMMAND_MARKER, event);
  }

  if (ret_event)
    clReleaseEvent(ret_event); // free the ret event if the caller doesn't want
                               // to return it
  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMarkerWithWaitList(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueMarkerWithWaitListIntelFPGA(
      command_queue, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueBarrierWithWaitListIntelFPGA(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_int result;
  cl_event local_event;
  std::scoped_lock lock{acl_mutex_wrapper};

  result = clEnqueueMarkerWithWaitList(command_queue, num_events_in_wait_list,
                                       event_wait_list, &local_event);
  if (result != CL_SUCCESS) {
    return result;
  }

  if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
    command_queue->last_barrier = local_event;
  }

  if (event) {
    *event = local_event;
  } else {
    clReleaseEvent(local_event);
  }
  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueBarrierWithWaitList(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueBarrierWithWaitListIntelFPGA(
      command_queue, num_events_in_wait_list, event_wait_list, event);
}

//////////////////////////////
// Internals

// Submit a command.
// Return a number bigger than zero if we made forward progress, and
// zero otherwise.
int acl_submit_command(cl_event event) {
  int result = 0;
  acl_assert_locked();
  acl_print_debug_msg("   submit event %p %d \n", event, event->id);

  if (event->execution_status == CL_QUEUED) {

    // Each action must set CL_SUBMITTED if it actually does something.
    // Otherwise it can fail and back off without updating any state at
    // all, in which case the execution status should remain at
    // CL_QUEUED.

    switch (event->cmd.type) {
    // Treat USM migrate and memadvise like sync primitives
    // until they do something.
    case CL_COMMAND_MIGRATEMEM_INTEL:
    case CL_COMMAND_MEMADVISE_INTEL:
    // These are synchronization primitives only: just set execution status.
    case CL_COMMAND_WAIT_FOR_EVENTS_INTELFPGA:
    case CL_COMMAND_MARKER:
      acl_set_execution_status(event, CL_SUBMITTED);
      acl_set_execution_status(event, CL_RUNNING);
      acl_set_execution_status(event, CL_COMPLETE);
      result = 1;
      break;

    // Map and unmap have trivial cases that we treat differently:
    // they don't entail enqueueing a device operation.
    // But the non-trivial cases do enqueue a device operation.
    case CL_COMMAND_MAP_BUFFER:
      result = acl_mem_map_buffer(event);
      break;
    case CL_COMMAND_UNMAP_MEM_OBJECT:
      result = acl_mem_unmap_mem_object(event);
      break;

    // Read, Write, Copy buffer map directly to a single memory
    // transfer device operation.
    case CL_COMMAND_READ_BUFFER:
    case CL_COMMAND_WRITE_BUFFER:
    case CL_COMMAND_COPY_BUFFER:
      result = acl_submit_mem_transfer_device_op(event);
      break;

    case CL_COMMAND_SVM_MEMCPY:
    case CL_COMMAND_SVM_MEMFILL:
    case CL_COMMAND_SVM_MAP:
    case CL_COMMAND_SVM_UNMAP:
    case CL_COMMAND_SVM_FREE:
      result = acl_svm_op(event);
      break;

    case CL_COMMAND_MEMCPY_INTEL:
    case CL_COMMAND_MEMFILL_INTEL:
      result = acl_submit_usm_memcpy(event);
      break;

    // Enqueue a kernel launch device operation.
    case CL_COMMAND_TASK:
    case CL_COMMAND_NDRANGE_KERNEL:
      result = acl_submit_kernel_device_op(event);
      break;

    // Enqueue an eager programming of the device.
    case CL_COMMAND_PROGRAM_DEVICE_INTELFPGA:
      result = acl_submit_program_device_op(event);
      break;

    case CL_COMMAND_MIGRATE_MEM_OBJECTS:
      result = acl_submit_migrate_mem_device_op(event);
      break;

    case CL_COMMAND_READ_HOST_PIPE_INTEL:
      result = acl_submit_read_program_hostpipe_device_op(event);
      break;

    case CL_COMMAND_WRITE_HOST_PIPE_INTEL:
      result = acl_submit_write_program_hostpipe_device_op(event);
      break;

    default:
      acl_print_debug_msg("    acl_submit_command: unknown cmd type %d\n",
                          event->cmd.type);
      break;
    }

    if (result) {
      event->is_on_device_op_queue = 1;
    }
  } else {
    acl_set_execution_status(event, ACL_INVALID_EXECUTION_TRANSITION);
    result = 1;
  }
  return result;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <new>
#include <string.h>
#include <vector>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_command.h>
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_icd_dispatch.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Events
// ======
// Events are the fundamental dependency and synchronization primitive in
// OpenCL.
//
// Events track the lifecycle and completion of:
//    Buffer transfers (and buffer mapping)
//    NDRange/Task execution
//    Completion of previously created events (synchronization "join" point)
//       An explicitly provided list
//       Or all previously scheduled commands on a command queue
//
// Some events (those that track buffer transfers and task execution by
// devices) are the host-side interface for a system-wide process.
// The system therefore needs a host-independent way to refer to a unique event,
// so we give each event is own integer ID.
// (we could have passed pointers, but that's less friendly for debugging)
//
// The lifecycle of an Event is:
//
//       Create( command_type, depend on a set of existing events )
//
//       Add dependency from a later event to myself.
//
//       Update( status update )
//          In case of successful completion:
//             trigger notify dependent commands,
//             downstream dependent events.
//
//       Retain:  increase reference count
//       Release:  decrease reference count.
//          Whe reference count is 0, associated command is completed, and
//          no downstream dependent events are waiting for this event
//          (the dependency has been broken)
//

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_event);

//////////////////////////////
// Local functions
// Release all implicitly retained cl_objects used by this cmd.
static void l_release_command_resources(acl_command_info_t &cmd);
// Returns an unused event_object, or 0 if we ran out of memory.
static cl_event l_get_unused_event(cl_context context);
// Return the event back into a free pool
static void l_return_event_to_free_pool(cl_event event);
// Edits the event timing info to record a state change
static void l_record_milestone(cl_event event, cl_profiling_info milestone);
//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainEventIntelFPGA(cl_event event) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }
  acl_retain(event);
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainEvent(cl_event event) {
  return clRetainEventIntelFPGA(event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseEventIntelFPGA(cl_event event) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }
  if (!acl_is_retained(event)) {
    ERR_RET(CL_INVALID_EVENT, event->context,
            "Trying to release an event that is not retained");
  }
  acl_release(event);

  if (!acl_is_retained(event)) {
    // We can't delete the event here.
    // It will be deleted by the command queue that owns it.

    // But prod that command queue.
    // It's a valid command queue because the ACL_VALIDATE_EVENT check.
    //
    // Don't do full acl_idle_update because it will take a very long time.
    // But that also means the user may have to run clFinish more often
    // than you'd like.
    //
    // Note that if an event is not alive and hence being popped (deleted) from
    // the queue the refcount of the command queue should be decremented by one
    if (event->not_popped) {
      acl_update_queue(event->command_queue);
    } else {

      // For events that were previously popped, they wont be deleted through
      // acl_update_queue since they are no longer in the queue.
      //
      // Manually call acl_maybe_delete_event on popped events
      // Note that since the event is no longer in the queue (had been popped
      // out before), no need to release the queue this time
      acl_maybe_delete_event(event);
    }
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseEvent(cl_event event) {
  return clReleaseEventIntelFPGA(event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventInfoIntelFPGA(
    cl_event event, cl_event_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }

  // Give the scheduler a nudge.
  if (param_name == CL_EVENT_COMMAND_EXECUTION_STATUS &&
      event->execution_status > CL_COMPLETE) {
    acl_idle_update(event->context);
  }

  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          event->context);

  RESULT_INIT;

  switch (param_name) {
  case CL_EVENT_COMMAND_QUEUE:
    if (event->cmd.type == CL_COMMAND_USER) {
      // The OpenCL 1.2 spec for clGetEventInfo states that queries to
      // CL_EVENT_COMMAND_QUEUE for user events should always return NULL. We
      // place user events on a special user_event_queue internally but we
      // should not expose that to the user.
      RESULT_PTR(NULL);
    } else {
      RESULT_PTR(event->command_queue);
    }
    break;
  case CL_EVENT_CONTEXT:
    RESULT_PTR(event->context);
    break;
  case CL_EVENT_COMMAND_TYPE:
    RESULT_ENUM(event->cmd.type);
    break;
  case CL_EVENT_COMMAND_EXECUTION_STATUS:
    RESULT_ENUM(event->execution_status);
    break;
  case CL_EVENT_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(event));
    break;
  default:
    break;
  }

  if (result.size == 0) {
    ERR_RET(CL_INVALID_VALUE, event->context,
            "Invalid or unsupported event query");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, event->context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventInfo(cl_event event,
                                               cl_event_info param_name,
                                               size_t param_value_size,
                                               void *param_value,
                                               size_t *param_value_size_ret) {
  return clGetEventInfoIntelFPGA(event, param_name, param_value_size,
                                 param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventProfilingInfoIntelFPGA(
    cl_event event, cl_profiling_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }
  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          event->context);

  // check if the event supports the profiling and error out accordingly
  if (event->cmd.type == CL_COMMAND_USER) {
    ERR_RET(CL_PROFILING_INFO_NOT_AVAILABLE, event->context,
            "Profiling information is not available for user events");
  } else if (!event->support_profiling) {
    // since user event will not have command_queue set, no need to check again
    ERR_RET(
        CL_PROFILING_INFO_NOT_AVAILABLE, event->context,
        "Profiling information is not available because "
        "CL_QUEUE_PROFILING_ENABLE was not set on the event's command queue");
  }

  RESULT_INIT;

  switch (param_name) {
  case CL_PROFILING_COMMAND_QUEUED:
    RESULT_ULONG(event->timestamp[3]);
    break;
  case CL_PROFILING_COMMAND_SUBMIT:
    RESULT_ULONG(event->timestamp[2]);
    break;
  case CL_PROFILING_COMMAND_START:
    RESULT_ULONG(event->timestamp[1]);
    break;
  case CL_PROFILING_COMMAND_END:
    RESULT_ULONG(event->timestamp[0]);
    break;
  default:
    break;
  }

  if (result.size == 0) {
    ERR_RET(CL_INVALID_VALUE, event->context, "Invalid event profiling query");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, event->context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventProfilingInfo(
    cl_event event, cl_profiling_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  return clGetEventProfilingInfoIntelFPGA(event, param_name, param_value_size,
                                          param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_event CL_API_CALL
clCreateUserEventIntelFPGA(cl_context context, cl_int *errcode_ret) {
  cl_event result = 0;
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  // Create the user event on the user_event_queue.
  // In our model, every event is attached to some command queue.
  // But user events should never block scheduler progress.
  // So they must go on their own out-of-order command queue.
  status = acl_create_event(context->user_event_queue, 0,
                            0, // depends on nothing else.
                            CL_COMMAND_USER, &result);
  if (status != CL_SUCCESS)
    BAIL(status); // already signaled error

  // As per spec.
  acl_set_execution_status(result, CL_SUBMITTED);

  if (errcode_ret)
    *errcode_ret = CL_SUCCESS;

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_event CL_API_CALL clCreateUserEvent(cl_context context,
                                                    cl_int *errcode_ret) {
  return clCreateUserEventIntelFPGA(context, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clSetUserEventStatusIntelFPGA(cl_event event, cl_int execution_status) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }

  // Either negative, or CL_COMPLETE (which itself is 0)
  if (execution_status <= CL_COMPLETE) {
    if (event->execution_status <= CL_COMPLETE) {
      ERR_RET(
          CL_INVALID_OPERATION, event->context,
          "User event has already been completed or terminated with an error");
    }

    acl_set_execution_status(event, execution_status);

    // Nudge the scheduler.
    acl_idle_update(event->context);

    return CL_SUCCESS;
  } else {
    ERR_RET(CL_INVALID_VALUE, event->context, "Invalid execution status");
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetUserEventStatus(cl_event event,
                                                     cl_int execution_status) {
  return clSetUserEventStatusIntelFPGA(event, execution_status);
}

// registers a user callback function for a specific command execution status.
ACL_EXPORT
CL_API_ENTRY cl_int clSetEventCallbackIntelFPGA(
    cl_event event, cl_int command_exec_callback_type,
    void(CL_CALLBACK *pfn_event_notify)(cl_event event,
                                        cl_int event_command_exec_status,
                                        void *user_data),
    void *user_data) {
  acl_event_user_callback *cb;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    return CL_INVALID_EVENT;
  }

  if (pfn_event_notify == NULL) {
    return CL_INVALID_VALUE;
  }
  if (command_exec_callback_type != CL_SUBMITTED &&
      command_exec_callback_type != CL_RUNNING &&
      command_exec_callback_type != CL_COMPLETE) {
    return CL_INVALID_VALUE;
  }

  cb = (acl_event_user_callback *)acl_malloc(sizeof(acl_event_user_callback));
  if (!cb)
    return CL_OUT_OF_HOST_MEMORY;

  cb->notify_user_data = user_data;
  cb->event_notify_fn = pfn_event_notify;
  cb->registered_exec_status = command_exec_callback_type;
  cb->next = event->callback_list;
  event->callback_list = cb;
  clRetainEvent(event); // Retain the event once for each registered callback.
                        // Will release once after each callback.

  // If the function is registered for a state that is already completed we
  // should do the callback now. calling the callbacks to make sure no excess
  // status is already passed.
  acl_event_callback(event, event->execution_status);

  return CL_SUCCESS;
}

// registers a user callback function for a specific command execution status.
ACL_EXPORT
CL_API_ENTRY cl_int clSetEventCallback(
    cl_event event, cl_int command_exec_callback_type,
    void(CL_CALLBACK *pfn_event_notify)(cl_event event,
                                        cl_int event_command_exec_status,
                                        void *user_data),
    void *user_data) {
  return clSetEventCallbackIntelFPGA(event, command_exec_callback_type,
                                     pfn_event_notify, user_data);
}
//////////////////////////////
// Internals

void acl_event_callback(cl_event event, cl_int event_command_exec_status) {
  // This function should not be called from a signal handler, but from within a
  // lock, as it calls user defined callback functions. So a lock is required.
  acl_assert_locked();

  // Call the notifcation function registered via clSetEventCallback calls.
  // if event is valid all the list will be searched and all functions with
  // execution status equal to or past the status specified by
  // command_exec_status will be called. However, each function should be called
  // one and only once. So it will be removed afterwards.
  if (acl_event_is_valid(event)) {
    acl_event_user_callback *cb_head = event->callback_list;
    acl_event_user_callback *pre = NULL, *temp;
    int release = 0;
    while (cb_head) {
      cl_bool cond_1 = (cl_bool)(event_command_exec_status <
                                 0); // Abnormal termination should be Treated
                                     // as CL_COMPLETE, while passing the error
                                     // code. All functions must be called!
      cl_bool cond_2 = (cl_bool)(event_command_exec_status >= 0 &&
                                 cb_head->registered_exec_status >=
                                     event_command_exec_status);
      if (cond_1 || cond_2) {
        acl_event_notify_fn_t event_notify_fn = cb_head->event_notify_fn;
        void *notify_user_data = cb_head->notify_user_data;
        // removing that callback from the list and calling it.
        if (pre == NULL)
          event->callback_list = cb_head->next;
        else
          pre->next = cb_head->next;
        temp = cb_head;
        cb_head = cb_head->next;
        acl_free(temp);
        {
          acl_suspend_lock_guard lock{acl_mutex_wrapper};
          event_notify_fn(event, event_command_exec_status, notify_user_data);
        }
        release++;
      } else {
        pre = cb_head;
        cb_head = cb_head->next;
      }
    }
    // Decrement the ref_count afte the callbacks are called.
    while (release--) {
      assert(acl_is_retained(event));
      acl_release(event);
    }
  }
}

// Is this event in use by the system?
// (If it's in use by the user, i.e. has refcount > 0, then it is
// considered in use by the system.)
int acl_event_is_valid(cl_event event) {
  acl_assert_locked();

#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  if (!acl_is_valid_ptr(event)) {
    return 0;
  }
  if (!acl_is_valid_ptr(event->context)) {
    return 0;
  }
  cl_context context = event->context;
  if (event->id > context->num_events) {
    return 0;
  }
  if (context->used_events.find(event) == context->used_events.end()) {
    return 0;
  }
  return 1;
#endif
}

void acl_reset_event(cl_event event) {
  int i;
  acl_assert_locked();

  acl_reset_ref_count(event);
  event->context = 0;
  event->command_queue = 0;
  event->cmd = {0};
  event->cmd.type = CL_COMMAND_MARKER; // should be innocuous
  event->execution_status = CL_QUEUED; // Initial state is queued
  event->ptr_hashtable.clear();

  for (i = 0; i < ACL_NUM_PROFILE_TIMESTAMPS; i++) {
    event->timestamp[i] = 0;
  }

  // Reset the event dependency lists.
  event->depend_on.clear();
  event->depend_on_me.clear();

  event->is_on_device_op_queue = 0;

  // All callbacks are expected to be called before here. So if callback_list is
  // not empty, either it is not initialized correctly, or some callbacks still
  // remain uncalled.
  assert(event->callback_list == NULL);
}

int acl_event_is_live(cl_event event) {
  acl_assert_locked();

  // Assume the pointer is valid.
  if (!event->depend_on_me.empty()) {
    return 1;
  } // Some other event depends on me
  if (!event->depend_on.empty()) {
    return 1;
  } // I depend on some other event.
  if (!acl_event_is_done(event)) {
    return 1;
  }
  return 0;
}

// Return non-zero if the event was removed.
int acl_maybe_delete_event(cl_event event) {
  acl_assert_locked();

  if (acl_event_is_valid(event) && !acl_event_is_live(event)) {
    // Release the implicitly retained cl_objects that were used by this event
    // as soon as the event is complete.
    // Even if the event itself is retained by the user we shouldn't retain any
    // other cl_objects.
    l_release_command_resources(event->cmd);

    // this will be called only when the event is popped from the queue, set the
    // command queue to NULL
    event->command_queue = NULL;

    // check to see if the event is retained to see if we should only pop it
    // from queue or delete it completely popped events will do this step later
    // when they are being deleted, events being deleted will do it now
    if (!acl_is_retained(event)) {
      cl_context context;

      acl_print_debug_msg(
          "    Deleting event [%d], so releasing command resources\n",
          event->id);

      acl_untrack_object(event);

      context = event->context;
      // The event is already removed from all dependency lists.
      //
      // Just go back onto the free list for the context.

      acl_print_debug_msg(" adding event %d to free event pool\n", event->id);
      context->used_events.erase(event);
      context->free_events.push_back(event);
    }
    return 1;
  }
  return 0;
}

static void l_release_command_resources(acl_command_info_t &cmd) {
  acl_assert_locked();

  switch (cmd.type) {
  case CL_COMMAND_READ_BUFFER:
  case CL_COMMAND_WRITE_BUFFER:
  case CL_COMMAND_COPY_BUFFER:
    if (cmd.info.mem_xfer.src_mem) {
      clReleaseMemObject(cmd.info.mem_xfer.src_mem);
      cmd.info.mem_xfer.src_mem = nullptr;
    }
    if (cmd.info.mem_xfer.dst_mem) {
      clReleaseMemObject(cmd.info.mem_xfer.dst_mem);
      cmd.info.mem_xfer.dst_mem = nullptr;
    }
    break;

  case CL_COMMAND_MAP_BUFFER:
  case CL_COMMAND_UNMAP_MEM_OBJECT:
    if (cmd.trivial && cmd.info.trivial_mem_mapping.mem) {
      clReleaseMemObject(cmd.info.trivial_mem_mapping.mem);
      cmd.info.trivial_mem_mapping.mem = nullptr;
    } else if (!cmd.trivial) {
      // This goes through the regular mem copy flow.
      if (cmd.info.mem_xfer.src_mem) {
        clReleaseMemObject(cmd.info.mem_xfer.src_mem);
        cmd.info.mem_xfer.src_mem = nullptr;
      }
      if (cmd.info.mem_xfer.dst_mem) {
        clReleaseMemObject(cmd.info.mem_xfer.dst_mem);
        cmd.info.mem_xfer.dst_mem = nullptr;
      }
    }
    break;

  case CL_COMMAND_TASK:
  case CL_COMMAND_NDRANGE_KERNEL:
    if (cmd.info.ndrange_kernel.memory_migration.num_mem_objects != 0 &&
        cmd.info.ndrange_kernel.memory_migration.src_mem_list) {
      // src_mem should be user-provided buffers, users are responsible for
      // releasing them Just free the src memory list here
      acl_free(cmd.info.ndrange_kernel.memory_migration.src_mem_list);
      cmd.info.ndrange_kernel.memory_migration.src_mem_list = nullptr;
    }
    // Cleanup is handled via the completion callback.
    break;

  case CL_COMMAND_PROGRAM_DEVICE_INTELFPGA:
    // Balance out the retain when we scheduled the command.
    if (cmd.info.eager_program) {
      clReleaseProgram(cmd.info.eager_program->get_dev_prog()->program);
      cmd.info.eager_program = nullptr;
    }
    break;

  case CL_COMMAND_MIGRATE_MEM_OBJECTS:
    for (size_t i = 0; i < cmd.info.memory_migration.num_mem_objects; ++i) {
      clReleaseMemObject(cmd.info.memory_migration.src_mem_list[i].src_mem);
      cmd.info.memory_migration.src_mem_list[i].src_mem = nullptr;
    }

    if (cmd.info.memory_migration.src_mem_list) {
      acl_free(cmd.info.memory_migration.src_mem_list);
      cmd.info.memory_migration.src_mem_list = nullptr;
    }

    cmd.info.memory_migration.num_mem_objects = 0;
    cmd.info.memory_migration.num_alloc = 0;
    break;

  case CL_COMMAND_READ_HOST_PIPE_INTEL:
  case CL_COMMAND_WRITE_HOST_PIPE_INTEL:
    // Nothing to cleanup
    break;
  default:
    break;
  }
}

// Update event status.
// Can be called from within a HAL interrupt OR user thread.
// We can't do much: just set a flag in the right spot.
// A later pass of the cooperative scheduler will take action.
void acl_set_execution_status(cl_event event, int new_status) {
  // This function can potentially be called by a HAL that does not use the
  // ACL global lock, so we need to use acl_lock() instead of
  // acl_assert_locked(). However, the MMD HAL calls this function from a unix
  // signal handler, which can't lock mutexes, so we don't lock in that case.
  // All functions called from this one therefore have to use
  // acl_assert_locked_or_sig() instead of just acl_assert_locked().
  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }

  if (event) { // just being defensive
    if (event->current_device_op) {
      // Update the device operation, instead of this event directly.
      acl_set_device_op_execution_status(event->current_device_op, new_status);
    } else {
      // Update this device operation directly.
      cl_int effective_status = new_status;
      switch (new_status) {
      case CL_QUEUED:
      case CL_SUBMITTED:
      case CL_RUNNING:
      case CL_COMPLETE:
        l_record_milestone(event, (cl_profiling_info)new_status);
        if (!acl_is_inside_sig())
          acl_event_callback(event, new_status);
        break;
      default:
        if (new_status >= 0) {
          // Not given a valid status.  Internal error?
          // Change to a negative status so command queue processing works.

          // we can't call the user context callback from a signal handler
          if (!acl_is_inside_sig()) {
            if (event->command_queue) {
              acl_context_callback(
                  event->context,
                  "Internal error: Setting invalid event status "
                  "with positive value");
            }
          }
          effective_status = ACL_INVALID_EXECUTION_STATUS; // this is negative
        }
        if (effective_status < 0) {
          // An error condition.  Record as complete.
          l_record_milestone(event, CL_COMPLETE);
          if (!acl_is_inside_sig())
            acl_event_callback(
                event, effective_status); // abnormal termination; making all
                                          // the uncalled callbacks till
                                          // CL_COMPLETE but sending error code
                                          // instead of command_exec_status
        }
        break;
      }

      acl_print_debug_msg("       Event [%d] %p status <- %d (%d)\n", event->id,
                          event, effective_status, new_status);

      event->execution_status = effective_status;
      // Update queues
      if (effective_status <= CL_COMPLETE) {
        cl_command_queue command_queue = event->command_queue;
        assert(command_queue != NULL);

        if (command_queue->properties &
            CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
          command_queue->completed_commands.push_back(event);
        } else {
          if (effective_status == CL_COMPLETE ||
              effective_status == ACL_INVALID_EXECUTION_STATUS) {
            command_queue->num_commands_submitted--;
          }
        }
      }
    }

    // Signal all waiters.
    acl_signal_device_update();
  }
}

static void l_record_milestone(cl_event event, cl_profiling_info milestone) {
  acl_assert_locked_or_sig();

  if (acl_event_is_valid(event) &&
      acl_command_queue_is_valid(event->command_queue) &&
      (event->command_queue->properties & CL_QUEUE_PROFILING_ENABLE)) {
    cl_ulong ts = acl_get_hal()->get_timestamp();

    acl_print_debug_msg("         Event [%d] status <- %d @ TS %lu\n",
                        event->id, milestone, ts);

    switch (milestone) {
    case CL_QUEUED:
    case CL_SUBMITTED:
    case CL_RUNNING:
    case CL_COMPLETE:
      event->timestamp[milestone] = ts;
      break;
    default:
      break;
    }
  }
}

// Create an event that depends on the given events.
// It's the caller's job to fill in the rest, e.g. execution status.
// Consider this a step in what would be an Event constructor.
//
// The caller must pass a valid return event pointer.
// A caller should be responsible for the event he/she created.
//
// Return 0 if we ran out of memory.
//
// The result is undefined (or even crash!) if the passed-in events are
// invalid in any way, e.g. not actually live.
//
// If successful, the returned event is already implicitly retained.
cl_int acl_create_event(cl_command_queue command_queue, cl_uint num_events,
                        const cl_event *events, cl_command_type command_type,
                        cl_event *new_event_ret) {
  cl_event event;
  cl_int result;
  cl_context context;
  acl_assert_locked();

  // defensively guarding that the reference of return event passed by the
  // caller should be valid
  assert(new_event_ret &&
         "The reference of the return event is invalid. A caller must pass a "
         "valid event pointer when creates a new event");

#ifndef REMOVE_VALID_CHECKS
  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  result =
      acl_check_events_in_context(command_queue->context, num_events, events);
  switch (result) {
  case CL_SUCCESS:
    break;
  case CL_INVALID_CONTEXT:
    return result;
  default:
    return CL_INVALID_EVENT_WAIT_LIST;
  }
#else
  result = CL_SUCCESS;
#endif

  context = command_queue->context;

  event = l_get_unused_event(context);
  if (event == 0) {
    acl_context_callback(context, "Could not allocate an event or command");
    return CL_OUT_OF_HOST_MEMORY;
  }

  event->dispatch = &acl_icd_dispatch;

  // No callback by default.
  event->completion_callback = 0;

  // No device operation, by default.
  event->last_device_op = 0;

  // The currently executing device op servicing this event/command.
  event->current_device_op = 0;

  if (events && num_events) {
    try {
      // The event dependency logic requires unique events.
      std::set<cl_event> active_dependency;
      // Special case: add barrier dependency if present.
      if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE &&
          command_queue->last_barrier) {
        active_dependency.insert(command_queue->last_barrier);
      }

      // Fill active_dependency[] by removing duplicates from events[].
      for (cl_uint i = 0; i < num_events; i++) {
        if (active_dependency.find(events[i]) == active_dependency.end()) {
          // If an event is already finished, no need to add a dependency on it
          if (!acl_event_is_done(events[i])) {
            active_dependency.insert(events[i]);
          }
        }
      }
      // Now add dependency arcs.
      for (cl_event dependency : active_dependency) {
        event->depend_on.insert(dependency);
        dependency->depend_on_me.push_back(event);
      }
    } catch (const std::bad_alloc &) {
      // Remove added dependencies
      for (int i = static_cast<int>(num_events) - 1; i >= 0; i--) {
        if (events[i]->depend_on_me.back()->id == event->id) {
          events[i]->depend_on_me.pop_back();
        }
      }

      // Return event back to free pool
      event->depend_on.clear();
      l_return_event_to_free_pool(event);
      event = 0; // Signal that we're going to fail.
    }
  }

  if (event) {
    // Success!

    // Normally, the command info union usage is determined only
    // by the command type.  But in the case of CL_COMMAND_MAP_BUFFER
    // and CL_COMMAND_UNMAP_MEM_OBJECT, we need this extra bit to know
    // whether to use the mem_xfer or trivial_mem_mapping subfields.
    // By default, consider it "non-trivial".
    event->cmd.trivial = 0;

    event->not_popped = true;

    // Set top-level command info.
    event->cmd.type = command_type; // trust that it's a valid value.
    // check if this event can be profiled based on the command_queue info
    // note that if it is a user event, the user_event_queue doesn't support
    // profiler
    event->support_profiling =
        (command_queue->properties & CL_QUEUE_PROFILING_ENABLE) != 0;

    try {
      acl_command_queue_add_event(command_queue, event);
    } catch (const std::bad_alloc &) {
      if (events && num_events) {
        // Remove added dependencies
        for (int i = static_cast<int>(num_events) - 1; i >= 0; i--) {
          if (events[i]->depend_on_me.back()->id == event->id) {
            events[i]->depend_on_me.pop_back();
          }
        }
      }

      // Return event back to free pool
      event->depend_on.clear();
      l_return_event_to_free_pool(event);

      acl_context_callback(context, "Could not add event to command queue");
      return CL_OUT_OF_HOST_MEMORY;
    }

    // Retention
    acl_retain(event);
    acl_retain(command_queue); // retain the referred command queue as well when
                               // an event is created attached to the queue
    *new_event_ret = event;

    if (debug_mode > 0) {
      printf("  Notify: Event [%d] has been created:\n", event->id);
      acl_dump_event(event);
    }

    acl_track_object(ACL_OBJ_EVENT, event);
  } else {
    acl_context_callback(context, "Could not allocate an event");
    return CL_OUT_OF_HOST_MEMORY;
  }

  return result;
}

// Returns an unused event_object, or NULL if we ran out of memory.
// We allocate the *pointers* to the event structs in batches that roughly
// double in size each time we need more.
// We allocate individual structures one-by-one.
static cl_event l_get_unused_event(cl_context context) {
  acl_assert_locked();
  cl_event result = 0;
  bool reused_event = false;
  if (context->free_events.empty()) {
    // create event and directly push to used_events
    result = acl_alloc_cl_event();
    if (!result)
      return 0;
    result->id = context->num_events;
    context->num_events++;
    result->callback_list = NULL; // initializing the callback list.
  } else {
    // remove event from the pool of avaliable ones
    result = context->free_events.front();
    context->free_events.pop_front();
    reused_event = true;
  }

  try {
    acl_reset_event(result);
    result->context = context;
    context->used_events.insert(result);
  } catch (const std::bad_alloc &) {
    if (reused_event) {
      context->free_events.push_back(result);
    } else {
      acl_free_cl_event(result);
      context->num_events--;
    }
    result = 0;
  }
  return result;
}

// Returns an event object to the unused pool
static void l_return_event_to_free_pool(cl_event event) {
  acl_assert_locked();
  cl_context context = event->context;
  context->used_events.erase(event);
  context->free_events.push_back(event);
}

cl_int acl_check_events_in_context(cl_context context, cl_uint num_events,
                                   const cl_event *events) {
  cl_uint i;
  acl_assert_locked();

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }
  if (num_events > 0 && events == 0) {
    acl_context_callback(
        context, "Event count is positive but event array is not specified");
    return CL_INVALID_VALUE;
  }
  if (num_events == 0 && events != 0) {
    acl_context_callback(context,
                         "Event count is zero but event array is specified");
    return CL_INVALID_VALUE;
  }

  for (i = 0; i < num_events; i++) {
    cl_event event = events[i];

    // check the event and context based on the event status
    if (!acl_event_is_valid(event)) {
      acl_context_callback(context, "Invalid event specified");
      return CL_INVALID_EVENT;
    }

    // Check for consistent context
    if (event->context != context) {
      acl_context_callback(context, "Event from incorrect context");
      return CL_INVALID_CONTEXT;
    }
  }
  return CL_SUCCESS;
}

// Return CL_SUCCESS if the list of events is non-empty and valid
// and have consistent context.
// Otherwise return CL_INVALID_VALUE or other error status.
cl_int acl_check_events(cl_uint num_events, const cl_event *events) {
  acl_assert_locked();

  if (num_events == 0) {
    return CL_INVALID_VALUE;
  }
  if (events == 0) {
    return CL_INVALID_VALUE;
  }

  // Get the context of the first event to send into
  // acl_check_events_with_context.
  // Don't check the callback because we can't trust the context just yet.
  if (!acl_event_is_valid(events[0])) {
    return CL_INVALID_EVENT;
  }

  // This will call the error callback as necessary.
  return acl_check_events_in_context(events[0]->context, num_events, events);
}

//////////////////////////////
// Internals - Event dependencies

// Remove this event from all the dependency lists of downstream events.
// Helper function only used by acl_update_inorder_queue()
// Returns number of notifications.
int acl_notify_dependent_events(cl_event event) {
  acl_assert_locked();
  if (!acl_event_is_valid(event)) {
    return 0;
  }

  if (debug_mode > 0) {
    if (!event->depend_on.empty()) {
      printf("  Notify: Event [%d] does not have dependent events\n",
             event->id);
    } else {
      printf("  Notify: Event [%d] has dependent events. Before:\n", event->id);
      acl_dump_event(event);
    }
  }

  for (cl_event dependent : event->depend_on_me) {
    dependent->depend_on.erase(event);

    // According to the OpenCL spec for clSetUserEventStatus,
    // when a user event's execution status is set to be negative it
    // causes all enqueued commands that wait on the user event to be
    // terminated.
    if (event->cmd.type == CL_COMMAND_USER && event->execution_status < 0) {
      acl_set_execution_status(dependent, event->execution_status);
    }

    // Submit the event if it has no dependencies and is partt of an
    // Out-of-order queue
    if (dependent->command_queue->properties &
            CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE &&
        dependent->depend_on.empty() &&
        dependent->cmd.type != CL_COMMAND_USER) {
      dependent->command_queue->num_commands_submitted++;
      acl_submit_command(dependent);
    }
  }

  int num_updates = static_cast<int>(event->depend_on_me.size());
  event->depend_on_me.clear();

  if ((debug_mode > 0) && num_updates) {
    printf("  Notify: Event [%d] updated %d dependent events. After:\n",
           event->id, num_updates);
    acl_dump_event(event);
  }

  return num_updates;
}

// Removes the dependency link between the given event
// and the first event that depends on it
cl_event acl_remove_first_event_dependency(cl_event event) {
  acl_assert_locked();

  assert(acl_event_is_valid(event));
  assert(event->depend_on.empty());

  cl_event dependent = event->depend_on_me.front();
  dependent->depend_on.erase(event);
  event->depend_on_me.pop_front();
  return dependent;
}

// Are the resources required to run this event available?
// Used to avoid contention on kernel hardware blocks.
int acl_event_resources_are_available(cl_event event) {
  int result = 1;
  acl_assert_locked();

  switch (event->cmd.type) {
  case CL_COMMAND_TASK:
  case CL_COMMAND_NDRANGE_KERNEL: {
    // Is the accelerator block currently occupied?
    const auto kernel = event->cmd.info.ndrange_kernel.kernel;
    auto *dev_prog = event->cmd.info.ndrange_kernel.dev_bin->get_dev_prog();
    result = (nullptr == dev_prog->current_event[kernel->accel_def]);
  } break;
  default:
    break;
  }
  return result;
}

#ifdef ACL_DEBUG
void acl_dump_event(cl_event event) {
  const char *type_name = "";
  acl_assert_locked();

  cl_command_queue cq = event->command_queue;
  cl_context context = event->context;
#define NNN(T)                                                                 \
  case T:                                                                      \
    type_name = #T;                                                            \
    break;
  switch (event->cmd.type) {
    NNN(CL_COMMAND_TASK)
    NNN(CL_COMMAND_NDRANGE_KERNEL)
    NNN(CL_COMMAND_MARKER)
    NNN(CL_COMMAND_READ_BUFFER)
    NNN(CL_COMMAND_WRITE_BUFFER)
    NNN(CL_COMMAND_COPY_BUFFER)
    NNN(CL_COMMAND_USER)
    NNN(CL_COMMAND_MAP_BUFFER)
    NNN(CL_COMMAND_WAIT_FOR_EVENTS_INTELFPGA)
    NNN(CL_COMMAND_PROGRAM_DEVICE_INTELFPGA)
    NNN(CL_COMMAND_READ_HOST_PIPE_INTEL)
    NNN(CL_COMMAND_WRITE_HOST_PIPE_INTEL)
  default:
    break;
  }
  acl_print_debug_msg("       Event [%d] %p = {\n", event->id, event);
  acl_print_debug_msg("          .refcnt            %u\n",
                      acl_ref_count(event));
  // the event can be popped, therefore the queue can be invalid
  if (cq)
    acl_print_debug_msg(
        "          .cq                %d %s\n", event->command_queue->id,
        (cq == context->user_event_queue
             ? "user_event_queue"
             : (cq == context->auto_queue ? "auto_queue" : "")));
  else
    acl_print_debug_msg("the event doesn't belong to any command queue");
  acl_print_debug_msg("          .execution_status  %d\n",
                      event->execution_status);
  acl_print_debug_msg("          .cmd_type          0x%4X %s\n",
                      event->cmd.type, type_name);
  acl_print_debug_msg("          .depend_on_me = {");
  for (cl_event dependent : event->depend_on_me) {
    if (dependent) {
      acl_print_debug_msg(" %d,", dependent->id);
    } else {
      acl_print_debug_msg(" null,");
    }
  }
  acl_print_debug_msg(" }\n");
  acl_print_debug_msg("          .i_depend_on = {");
  for (cl_event depender : event->depend_on) {
    if (depender) {
      acl_print_debug_msg(" %d,", depender->id);
    } else {
      acl_print_debug_msg(" null,");
    }
  }
  acl_print_debug_msg(" }\n");
  acl_print_debug_msg("          .times { %d %d %d %d } \n",
                      event->timestamp[0], event->timestamp[1],
                      event->timestamp[2], event->timestamp[3]);
  acl_print_debug_msg("       }\n");
}
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

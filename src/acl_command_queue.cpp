// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <string.h>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl_command.h>
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_icd_dispatch.h>
#include <acl_kernel.h>
#include <acl_platform.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Command Queues
// ==============
//
// A command is represented by its associated event(!).
// (We create an event for a command even if the user didn't ask for it.)
//
// A command is in one of the following execution states:
//    q = queued
//          It's on the command queue, waiting until:
//             prior events have completed (including queue barriers),
//             prior commands on this queue have completed.
//
//    s = submitted
//          The runtime has sent the "start the command" message
//          to the HAL.
//
//    r = running
//          The HAL has responded, indicating that the command is being
//          executed by the hardware.
//
//    c = complete
//          The HAL has responded, indicating that the command execution
//          has finished.
//
//    An error state (value < 0)
//
// Notes:
//    Only events in "queued" state may depend on other (earlier) events.
//
//    The normal progression is: q -> s -> r -> c
//
//    The transitions s->r and r->c are made asynchronously, i.e. possibly
//    from outside main thread of control.  They occur in response to messages
//    being received from the hardware.
//
//
// Data structures
// ===============
//
// We maintain a circular buffer of commands, ordered by the time at which
// they were queud.
//
// The status of events in the queue is always in the following pattern,
// even if out-of-order execution is (would be) allowed:
//
//    c* ( c | r | s )* q*
//
// The first segment of "complete" events can be removed if they are not
// "live".  (An event is "live" if it is retained, or the bookkeeping still
// says other events depend on it, or if its execution status is neither
// complete nor error.)  See acl_event_is_live. Such events are redundant, and
// should be removed to free up bookkeeping resources.
//
// The next segment of "complete/running/submitted" are being updated
// asynchronously when (another thread) receives messages from acclerator
// devices.
//
//
// Algorithms
// ==========
//
// Users enqueue commands and check their status via queries on the
// associated events.  See:  clEnqueue*  and  clGetEventInfo*
//
// The system asynchronously updates execution status of submitted commands.
//
// In between the two, the runtime must update the queue based, responding
// to the asynchronous status updates.  See: acl_update_queue

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_command_queue);

static int l_init_queue(cl_command_queue cq,
                        cl_command_queue_properties properties,
                        cl_context context, cl_device_id device);

//////////////////////////////
// OpenCL API

// Create a command queue
ACL_EXPORT
CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueueWithPropertiesIntelFPGA(
    cl_context context, cl_device_id device,
    const cl_queue_properties *properties, cl_int *errcode_ret) {
  cl_command_queue result = 0;
  cl_command_queue_properties cq_properties = 0;
  cl_uint q_size_properties = 0, idx = 0;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }
  if (!acl_device_is_valid(device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
  }
  if (!acl_context_uses_device(context, device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context,
              "Device is not associated with the context");
  }

  // Get the properties. Only two possible properties: CL_QUEUE_PROPERTIES and
  // CL_QUEUE_SIZE. Combining if any repetitive CL_QUEUE_PROPERTIES, but bailing
  // on repetitive CL_QUEUE_SIZE The end of the properties list is specified
  // with a zero.
  while (properties != NULL && properties[idx] != 0) {
    if (properties[idx] == CL_QUEUE_PROPERTIES) {
      cq_properties |= properties[idx + 1];
    } else if (properties[idx] == CL_QUEUE_SIZE) {
      if (q_size_properties == 0)
        q_size_properties = (cl_uint)properties[idx + 1];
      else // This property was already given.
        BAIL_INFO(CL_INVALID_VALUE, context, "Invalid queue properties");
    } else {
      BAIL_INFO(CL_INVALID_VALUE, context, "Invalid queue properties");
    }
    idx += 2;
  }

  // Check property bits.
  {
    // What is valid to say in the API?
    const cl_command_queue_properties valid_properties =
        CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
        CL_QUEUE_ON_DEVICE | CL_QUEUE_ON_DEVICE_DEFAULT;
    if (cq_properties & ~(valid_properties)) {
      BAIL_INFO(CL_INVALID_VALUE, context, "Invalid queue properties");
    }
    // Also check the dependency of options:
    if (((cq_properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) == 0 &&
         (cq_properties & CL_QUEUE_ON_DEVICE)) ||
        ((cq_properties & CL_QUEUE_ON_DEVICE) == 0 &&
         (cq_properties & CL_QUEUE_ON_DEVICE_DEFAULT))) {
      BAIL_INFO(CL_INVALID_VALUE, context, "Invalid queue properties");
    }
  }
  {
    cl_command_queue_properties device_props = 0;
    // We currently don't support CL_QUEUE_ON_DEVICE &
    // CL_QUEUE_ON_DEVICE_DEFAULT. The availability of these features cannot be
    // queried from current version of clGetDeviceInfo. So manually failing on
    // those properties for now.
    if (cq_properties & (CL_QUEUE_ON_DEVICE | CL_QUEUE_ON_DEVICE_DEFAULT))
      BAIL_INFO(CL_INVALID_QUEUE_PROPERTIES, context,
                "Device does not support the specified queue properties");
    if (q_size_properties != 0) { // not supported yet.
      BAIL_INFO(CL_INVALID_QUEUE_PROPERTIES, context,
                "Device does not support the specified queue properties");
    }

    // Internal user may want to turn off support for OOO Queues
    const char *disable_oooq =
        acl_getenv("CL_CONTEXT_DISABLE_OOO_QUEUES_INTELFPGA");
    if (disable_oooq &&
        (cq_properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)) {
      BAIL_INFO(CL_INVALID_QUEUE_PROPERTIES, context,
                "Device does not support the specified queue properties");
    }

    // What does the device support?
    // No, we're not checking the return result. It would be an internal
    // error for this to fail.  But initializing device_props to 0 is a
    // fail-safe.
    clGetDeviceInfo(device, CL_DEVICE_QUEUE_PROPERTIES, sizeof(device_props),
                    &device_props, 0);
    if (cq_properties & ~(device_props)) {
      BAIL_INFO(CL_INVALID_QUEUE_PROPERTIES, context,
                "Device does not support the specified queue properties");
    }
  }

  // Now actually allocate the command queue.
  result = acl_alloc_cl_command_queue();
  if (result == 0) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a command queue");
  }

  // Fail to double the capacity of the pointer array
  if (!l_init_queue(result, cq_properties, context, device)) {
    acl_free_cl_command_queue(result);
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a command queue");
  }

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_COMMAND_QUEUE, result);
  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_command_queue CL_API_CALL clCreateCommandQueueWithProperties(
    cl_context context, cl_device_id device,
    const cl_queue_properties *properties, cl_int *errcode_ret) {
  return clCreateCommandQueueWithPropertiesIntelFPGA(context, device,
                                                     properties, errcode_ret);
}

// Create a command queue. (This is opencl 1.0 API and is deprecated in 2.0)
ACL_EXPORT
CL_API_ENTRY cl_command_queue CL_API_CALL clCreateCommandQueueIntelFPGA(
    cl_context context, cl_device_id device,
    cl_command_queue_properties properties, cl_int *errcode_ret) {
  cl_queue_properties q_properties[] = {CL_QUEUE_PROPERTIES, 0, 0};
  q_properties[1] = (cl_command_queue_properties)
      properties; // Couldn't initialize this element in array, due to MSVC
                  // compiler warning: non-constant aggregate initializer
  return clCreateCommandQueueWithPropertiesIntelFPGA(context, device,
                                                     q_properties, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_command_queue CL_API_CALL clCreateCommandQueue(
    cl_context context, cl_device_id device,
    cl_command_queue_properties properties, cl_int *errcode_ret) {
  return clCreateCommandQueueIntelFPGA(context, device, properties,
                                       errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueueIntelFPGA(cl_command_queue command_queue) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  acl_retain(command_queue);
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueue(cl_command_queue command_queue) {
  return clRetainCommandQueueIntelFPGA(command_queue);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueueIntelFPGA(cl_command_queue command_queue) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  acl_release(command_queue);
  acl_print_debug_msg("Release command queue %d %p\n", command_queue->id,
                      command_queue);

  // delete the command queue if the queue is no longer retained
  if (!acl_is_retained(command_queue)) {
    acl_delete_command_queue(command_queue);
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueue(cl_command_queue command_queue) {
  return clReleaseCommandQueueIntelFPGA(command_queue);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetCommandQueueInfoIntelFPGA(
    cl_command_queue command_queue, cl_command_queue_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  RESULT_INIT;

  switch (param_name) {
  case CL_QUEUE_CONTEXT:
    RESULT_PTR(command_queue->context);
    break;
  case CL_QUEUE_DEVICE:
    RESULT_PTR(command_queue->device);
    break;
  case CL_QUEUE_REFERENCE_COUNT:
    RESULT_UINT(acl_ref_count(command_queue));
    break;
  case CL_QUEUE_PROPERTIES:
    RESULT_BITFIELD(command_queue->properties);
    break;
  default:
    break;
  }

  if (result.size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Invalid or unsupported command queue property");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
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
CL_API_ENTRY cl_int CL_API_CALL clGetCommandQueueInfo(
    cl_command_queue command_queue, cl_command_queue_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  return clGetCommandQueueInfoIntelFPGA(command_queue, param_name,
                                        param_value_size, param_value,
                                        param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetCommandQueuePropertyIntelFPGA(
    cl_command_queue command_queue, cl_command_queue_properties properties,
    cl_bool enable, cl_command_queue_properties *old_properties) {
  cl_command_queue_properties bad_properties;
  std::scoped_lock lock{acl_mutex_wrapper};

  bad_properties =
      ~((cl_command_queue_properties)CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
        (cl_command_queue_properties)CL_QUEUE_PROFILING_ENABLE);

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  // Internal user may want to turn off support for OOO Queues
  const char *disable_oooq =
      acl_getenv("CL_CONTEXT_DISABLE_OOO_QUEUES_INTELFPGA");
  if (disable_oooq && (properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)) {
    ERR_RET(CL_INVALID_QUEUE_PROPERTIES, command_queue->context,
            "Can't set CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE property, "
            "unsupported");
  }

  if (old_properties) {
    *old_properties = command_queue->properties;
  }

  if (properties & bad_properties) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Invalid or unsupported command queue property");
  }

  if (enable) {
    command_queue->properties |= properties;
  } else {
    command_queue->properties &= ~properties;
  }

  // No queue synchronization is required because we don't support
  // out-of-order execution.

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetCommandQueueProperty(
    cl_command_queue command_queue, cl_command_queue_properties properties,
    cl_bool enable, cl_command_queue_properties *old_properties) {
  return clSetCommandQueuePropertyIntelFPGA(command_queue, properties, enable,
                                            old_properties);
}

// Wait until all previous commands have been issued to the device.
// We don't wait until they are complete.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clFlushIntelFPGA(cl_command_queue command_queue) {
  bool any_queued = false;
  const acl_hal_t *hal = acl_get_hal();
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  // Context is valid too.  Force a schedule update.
  cl_context context = command_queue->context;

  do {
    any_queued = 0;
    acl_idle_update(context);
    if (command_queue->num_commands == 0) {
      return CL_SUCCESS;
    }

    // Find if at least one event is not SUBMITTED
    if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
      for (auto it = command_queue->commands.begin();
           it != command_queue->commands.end() && !any_queued; ++it) {
        any_queued = ((*it)->execution_status == CL_QUEUED);
      }
    } else {
      // Events added to end of in-order queue, just check the last event
      cl_event event = command_queue->inorder_commands.back();
      any_queued = (event->execution_status == CL_QUEUED);
    }

    if (any_queued) {
      // Wait until signaled, without burning CPU.
      if (!hal->yield) {
        acl_wait_for_device_update(context);
      } else {
        hal->yield(context->num_devices, context->device);
      }
    }

  } while (any_queued);

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clFlush(cl_command_queue command_queue) {
  return clFlushIntelFPGA(command_queue);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clFinishIntelFPGA(cl_command_queue command_queue) {
  cl_event event = 0;
  cl_int result;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  // Spec says:
  // "enqueues a marker command which waits for either a list of events to
  // complete, or if the list is empty it waits for all commands previously
  // enqueued in command_queue to complete before it completes"
  result = clEnqueueMarkerWithWaitList(command_queue, 0, 0, &event);

  if (result == CL_SUCCESS) {
    result = clWaitForEvents(1, &event);
    clReleaseEvent(event);
  }
  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clFinish(cl_command_queue command_queue) {
  return clFinishIntelFPGA(command_queue);
}

//////////////////////////////
// Internals

// This function does 2 things:
// 1) Initialize the various fields inside "cq"
// 2) If cq->context->command_queue[] (i.e. array of queue pointers in the
// context) reaches capacity, double that capacity
static int l_init_queue(cl_command_queue cq,
                        cl_command_queue_properties properties,
                        cl_context context, cl_device_id device) {
  acl_assert_locked();

  // If we've used up all available command queue pointers, time to allocate for
  // more
  if (context->num_command_queues == context->num_command_queues_allocated) {
    int old_capacity = context->num_command_queues_allocated;
    int new_capacity = 2 * old_capacity;
    if (new_capacity < old_capacity) {
      return 0;
    } // overflow.

    cl_command_queue *command_queue = (cl_command_queue *)acl_realloc(
        context->command_queue, new_capacity * sizeof(cl_command_queue));
    if (!command_queue) {
      return 0;
    }

    context->num_command_queues_allocated = new_capacity;
    context->command_queue = command_queue;
  }
  cq->magic = get_magic_val<cl_command_queue>();
  cq->id = (unsigned)(context->num_command_queues);
  context->command_queue[context->num_command_queues++] = cq;

  /* Initialize the queue */
  acl_reset_ref_count(cq);
  acl_retain(cq);
  acl_retain(device);
  cq->device = device;
  acl_retain(context);
  cq->context = context;

  cq->dispatch = &acl_icd_dispatch;
  cq->properties = properties;

  // All user command queues submit commands.
  cq->submits_commands = 1;

  // Empty out the queue
  cq->num_commands = 0;
  cq->num_commands_submitted = 0;
  cq->last_barrier = NULL;

  return 1;
}

int acl_command_queue_is_valid(cl_command_queue command_queue) {
  acl_assert_locked();

  if (!acl_is_valid_ptr(command_queue)) {
    return 0;
  }
  if (!acl_is_retained(command_queue)) {
    return 0;
  }
  if (!acl_context_is_valid(command_queue->context)) {
    return 0;
  }
  // Check that the id field is set correctly
  if (command_queue !=
      command_queue->context->command_queue[command_queue->id]) {
    return 0;
  }
  return 1;
}

void acl_command_queue_add_event(cl_command_queue command_queue,
                                 cl_event event) {
  acl_assert_locked();
  event->command_queue = command_queue;
  acl_set_execution_status(event, CL_QUEUED);

  if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
    command_queue->commands.insert(event);
    try {
      command_queue->new_commands.push_back(event);
    } catch (const std::bad_alloc &) {
      // Remove inserted command before allowing error to be thrown
      command_queue->commands.erase(event);
      throw;
    }
  } else {
    command_queue->inorder_commands.push_back(event);
  }
  command_queue->num_commands++;
}

/**
 * Compares if the two events are pointing to the same kernel and accelerator
 * @param kernel_event is an already established kernel event, this is our
 * target to match
 * @param event this is the event in question. Is it a kernel? If so does it
 * point to same accelearator as kernel_event
 * @return true(1) iff event is a kernel and has the same acceleartor as the
 * kernel_event, else false(0)
 */
int l_is_same_kernel_event(const cl_event kernel_event,
                           const cl_event other_event) {
  assert(kernel_event->cmd.type == CL_COMMAND_TASK ||
         kernel_event->cmd.type == CL_COMMAND_NDRANGE_KERNEL);

  if (other_event->cmd.type == CL_COMMAND_TASK ||
      other_event->cmd.type == CL_COMMAND_NDRANGE_KERNEL) {
    // it's a kernel

    if (other_event->cmd.info.ndrange_kernel.device->id ==
            kernel_event->cmd.info.ndrange_kernel.device->id &&
        other_event->cmd.info.ndrange_kernel.kernel->accel_def->id ==
            kernel_event->cmd.info.ndrange_kernel.kernel->accel_def->id) {
      // same acceleartor
      return 1;
    }
  }
  return 0;
}

int acl_update_queue(cl_command_queue command_queue) {
  acl_assert_locked();
  // If command_queue is empty return 0 right away
  if (!(command_queue->num_commands)) {
    return 0;
  }

  // First nudge the device operation scheduler.
  acl_update_device_op_queue(&(acl_platform.device_op_queue));

  if (command_queue->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
    return acl_update_ooo_queue(command_queue);
  } else {
    return acl_update_inorder_queue(command_queue);
  }
}

// Try to submit a kernel even if it has unfinished dependences using fast kernel relaunch
// Returns true on success, false on failure
bool acl_fast_relaunch_kernel(cl_event event) {
  if (!(event->command_queue->properties &
        CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE))
    return false;
  
  if (event->depend_on.size() != 1)
    return false;

  cl_event parent = *(event->depend_on.begin());
  
  if (!(parent->command_queue->properties &
        CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE))
    return false;
 
  if (parent->cmd.type != CL_COMMAND_TASK &&
      parent->cmd.type != CL_COMMAND_NDRANGE_KERNEL)
    return false;
  
  if (parent->execution_status > CL_SUBMITTED ||
      parent->last_device_op->status > CL_SUBMITTED)
    return false;
  
  if (!l_is_same_kernel_event(parent, event)) {
    // dependent on a different kernel than parent,
    // must wait for dependency to be resolved
    // OR the dependent is not on the same device,
    // not safe to preemptively push dependent to device_op_queue
    return false;
  }
    
  // Special case: if subbuffers are present they may(!) cause a
  // migration while another kernel is using that data.
  if (acl_kernel_has_unmapped_subbuffers(
        &(event->cmd.info.ndrange_kernel.memory_migration)))
    return false;

  // Fast Kernel Relaunch: submitting is safe even though has dependency
  // If submission succeeds, remove dependency
  bool success = acl_submit_command(event);
  if (!success)
    return false;
  event->depend_on.erase(parent);
  parent->depend_on_me.remove(event);
  return true;
}

int acl_update_ooo_queue(cl_command_queue command_queue) {
  int num_updates = 0;

  // First, remove dependencies on completed events, 
  // as this may unblock other evevnts
  // Completed events should be returned to the free pool
  while (!command_queue->completed_commands.empty()) {
    cl_event event = command_queue->completed_commands.front();

    while (!event->depend_on_me.empty()) {
      cl_event dependent = acl_remove_first_event_dependency(event);

      if (event->cmd.type == CL_COMMAND_USER && event->execution_status < 0) {
        // According to the OpenCL spec for clSetUserEventStatus,
        // when a user event's execution status is set to be negative it
        // causes all enqueued commands that wait on the user event to be
        // terminated.
        acl_set_execution_status(dependent, event->execution_status);
        if (dependent->command_queue->properties &
            CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
          dependent->command_queue->completed_commands.push_back(
              dependent); // dependent might be on another queue
        }
      } 
    }

    // Return completed event to free pool
    if (debug_mode > 0) {
      assert(!command_queue->completed_commands
                  .empty()); // assert the list is not empty in the debug mode
    }
    command_queue->completed_commands.pop_front();
    acl_event_callback(event, event->execution_status); // notify user
    if (event->completion_callback) {
      event->completion_callback(event);
    } // clean up resources
    event->completion_callback =
        0; // Ensure the completion callback is only called once.
    if (event == command_queue->last_barrier) {
      command_queue->last_barrier = NULL;
    }
    acl_maybe_delete_event(event);
    if (command_queue->waiting_for_events) {
      // We are in the middle of traversing command_queue->commands, defer
      // the removal till later to avoid corruption
      event->defer_removal = true;
    } else {
      command_queue->commands.erase(event);
    }
    event->not_popped = false;
    num_updates++;
    command_queue->num_commands--;
    acl_release(command_queue);
  }

  // Next try to submit any events with no dependencies
  // or whose only dependences can be handled by fast kernel relaunch
  // unless they are on a user_event queue which never submits events
  for (auto event_iter = command_queue->new_commands.begin(); 
      event_iter != command_queue->new_commands.end(); ) {
    cl_event event = *event_iter;
    int success = 0;
    if (!command_queue->submits_commands)
      success = 1;
    else {
      if (event->depend_on.empty()) {
        success = acl_submit_command(event);
      } else {
        success = acl_fast_relaunch_kernel(event);
      }
    }
   
    // Increment before removal so we don't invalidate the iterator
    event_iter++;
    if (success) {
      // num_commands_submitted isn't used for ooo queues today
      // but keep it up-to-date in case someone wants to use it in the future
      command_queue->num_commands_submitted++;
      command_queue->new_commands.remove(event);
      num_updates++;
    }
  }

  return num_updates;
}

// Process updates on the queue.
// This is part of the main event loop for the runtime.
//    Respond to status updates from devices
//    Process event completion
//
// Returns number of things updated.
// If it's 0 then we're idle and can "wait" a bit longer perhaps.
// If it's greater than zero, then we should continue updating the
// context's queues because perhaps we have finished a command and
// removed some event dependencies.
// That might allow dependent events to become
// ready and hence we can launch their associated commands.
int acl_update_inorder_queue(cl_command_queue command_queue) {
  int num_updates = 0;
  acl_assert_locked();

  // Process completed events in the queue.
  // When we break out of the loop, the loop counter will equal the index of the
  // first non-completed event in the queue.
  unsigned queue_idx = 0;
  while (queue_idx < command_queue->inorder_commands.size()) {
    cl_event event = command_queue->inorder_commands[queue_idx];

    if (acl_event_is_done(event)) {
      // Remove this event from its dependencies.
      num_updates += acl_notify_dependent_events(event);

      // Call all its user-based callbacks.
      acl_event_callback(event, event->execution_status);

      // Process event completion.
      if (acl_event_is_valid(event) && event->completion_callback) {
        event->completion_callback(event);
        // Ensure the completion callback is only called once.
        event->completion_callback = 0;
      }

      // We can only delete this completed event if there's no more
      // dependencies.
      if (acl_maybe_delete_event(event) && event->not_popped) {
        // Note: if event is marked as CL_COMPLETE inside of a callback this
        // will trigger us to update all the queues and subsequently pop this
        // event, while processing the callback. Hence when we come back to the
        // original call the event might already be popped, and we need to avoid
        // popping it the second time.

        // Deleted event. Remove it from our queue.
        if (command_queue->waiting_for_events) {
          // We are in the middle of traversing command_queue->inorder_commands,
          // defer the removal till later to avoid corruption
          event->defer_removal = true;
        } else {
          command_queue->inorder_commands.erase(
              command_queue->inorder_commands.begin() + queue_idx);
        }
        command_queue->num_commands--;
        num_updates++;
        event->not_popped = false;
        // since the event is popped out (deleted), it is safe to
        // decrease the refcount of the command queue by one
        acl_release(command_queue);
        acl_print_debug_msg("  Deleted event.  num_updates is now %d\n",
                            num_updates);
      } else {
        // If an event is deleted from the queue, the same value of the pointer
        // would now point to the next event in the queue. Hence we only
        // increment when no deletion took place.
        queue_idx++;
      }
    } else {
      break;
    }
  }

  // See if we should submit a command to the accelerator
  if (command_queue->submits_commands) {
    // Skip the completed events. Check the front of the queue.
    for (; queue_idx < command_queue->inorder_commands.size(); queue_idx++) {
      cl_event event = command_queue->inorder_commands[queue_idx];

      if (event->execution_status != CL_QUEUED ||
          event->is_on_device_op_queue) {
        continue;
      }

      if (event->cmd.type == CL_COMMAND_TASK ||
          event->cmd.type == CL_COMMAND_NDRANGE_KERNEL) {

        unsigned int safe_to_submit = 1;

        // iterate over dependencies, make sure only dependent
        // on events with the same accelerator as itself
        for (cl_event i_depend_on : event->depend_on) {
          if (!l_is_same_kernel_event(event, i_depend_on) ||
              i_depend_on->last_device_op == NULL ||
              i_depend_on->last_device_op->status > CL_SUBMITTED) {
            // Dependent on a different kernel than itself,
            // must wait for dependency to be resolved
            // OR the kernel dependency is not on the device,
            // not safe to preemptively push event to device_op_queue
            // Assumption: if depend_on is a kernel then last device_op is a
            // kernel
            safe_to_submit = 0;
            break;
          }
        }

        // no need to go on to the next check, unsafe to submit
        if (!safe_to_submit) {
          break;
        }

        // Special case: if subbuffers are present they may(!) cause a
        // migration while another kernel is using that data.
        if (acl_kernel_has_unmapped_subbuffers(
                &(event->cmd.info.ndrange_kernel.memory_migration)) &&
            (!event->depend_on.empty() ||
             command_queue->num_commands_submitted != 0)) {
          break;
        }

        if (command_queue->num_commands_submitted == 0) {
          int local_updates = acl_submit_command(event);
          command_queue->num_commands_submitted += local_updates;
          num_updates += local_updates;
          continue; // there might be another kernel behind us that can be
                    // submitted aswell
        } else {
          // Oh-oh, there is something else already submitted. Need to check
          // that it is safe to submit this as well 1) submitted is a kernel
          // event + it has the same accelerator as itself 2) submitted
          // kernel-op is on the device Note: we can cheat for this in-order
          // queue and only check the last submitted event, because if there are
          // more, they will be the same

          // If there is a command that is submitted, then it must still be in
          // our queue
          cl_event submitted_event =
              command_queue->inorder_commands[queue_idx - 1];

          if (l_is_same_kernel_event(event, submitted_event)) {
            if (submitted_event->last_device_op->status <= CL_SUBMITTED) {
              // Assumption: last device_op of the submitted kernel event is a
              // kernel_op
              int local_updates = acl_submit_command(event);
              command_queue->num_commands_submitted += local_updates;
              num_updates += local_updates;
              continue; // there might be another kernel behind us that can be
                        // submitted aswell
            }
          }
        }
        // Can't be submitted, we shouldn't check things behind it: in-order
        // queue limitation
        break;
      } else { // Not a kernel event
        if (command_queue->num_commands_submitted == 0 &&
            event->depend_on.empty()) {
          // it is safe to submit: nothing else submitted AND all dependencies
          // are resolved
          int local_updates = acl_submit_command(event);
          command_queue->num_commands_submitted += local_updates;
          num_updates += local_updates;
        }
        break; // no more events can be submitted
      }
    }
  }
  return num_updates;
}

// Update internal data structures in response to external messages.
// Do this as part of a waiting loop, along with acl_hal_yield();
void acl_idle_update_queue(cl_command_queue command_queue) {
  int num_updates;
  unsigned iters = 0;
  const unsigned max_iters = 10000; // This is unreasonably high.
  const unsigned id = command_queue->id;
  acl_assert_locked();

  do {
    num_updates = acl_update_queue(command_queue);
    acl_print_debug_msg(" cq[%d] had %d updates\n", id, num_updates);
  } while ((num_updates > 0) && (++iters < max_iters) &&
           acl_is_retained(command_queue));

  if (num_updates) {
    acl_print_debug_msg(" cq[%d] idle update: still had %d 'updates' after %u "
                        "iters. Breaking out\n",
                        id, num_updates, iters);
  }
}

// Delete the command queue if the command queue is no longer retained.
// This function will be called either in (1) clReleaseCommandQueue; or (2)
// acl_idle_update. On the one hand, for most cases, after the host is done,
// there will be no alive event/command left inside the queue, a user calls (1)
// to release and delete the command queue; On the other hand, in some special
// cases, the user would call (1) before the last alive event/command is done,
// and the queue will be retained by that event until it is completed. Runtime
// will keep calling (2) to update all the command queues and deleting those not
// being retained any more (no live event left and having been called to (1) by
// the user already).
void acl_delete_command_queue(cl_command_queue command_queue) {
  acl_assert_locked();

  if (!acl_is_retained(command_queue)) {

    acl_print_debug_msg("Delete command queue %d %p\n", command_queue->id,
                        command_queue);

    cl_device_id device = command_queue->device;

    acl_release(device);

    // Remove queue from context's command queue pointer list
    //  (*) At all time, we want all valid command queue pointers to be at the
    //  start of context->command_queue[],
    //       and the number of valid command queue pointers is always
    //       context->num_command_queues
    //  For example, say initially num_command_queues = 5, and we're releasing
    //  the queue pointed to by command_queue[2], Satisfying (*) means after the
    //  release, num_command_queues = 4, and the first 4 pointers point to valid
    //  queues. To do this, first set command_queue[2] to point to the same
    //  queue as the last pointer i.e. command_queue[4] Then we can safely
    //  disregard command_queue[4] by doing "num_command_queues--". Now
    //  num_command_queues = 4
    cl_context context = command_queue->context;
    unsigned id = command_queue->id;
    context->command_queue[id] =
        context->command_queue[--context->num_command_queues];
    // Reset the id field accordingly since this queue is now pointed to by a
    // different pointer
    context->command_queue[id]->id = id;
    context->last_command_queue_idx_with_update = -1;
    clReleaseContext(context);

    // No need to clean up anything, destructors will take care of it.
    assert(command_queue->new_commands.empty());
    assert(command_queue->completed_commands.empty());
    assert(command_queue->commands.empty());
    assert(command_queue->inorder_commands.empty());

    acl_untrack_object(command_queue);
    acl_free_cl_command_queue(command_queue);
  }
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

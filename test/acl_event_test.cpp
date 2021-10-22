// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _MSC_VER
#pragma GCC diagnostic ignored "-Wconversion-null"
#endif
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
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_event.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>
#include <unref.h>

#include "acl_test.h"

MT_TEST_GROUP(acl_event) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100 };
  void setup() {
    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_setup_generic_system());
    }

    syncThreads();

    m_callback_count = 0;
    m_callback_errinfo = 0;
    this->load();
  }
  void teardown() {
    syncThreads();

    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_teardown_generic_system());
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 3);

    // We cannot check the device presence here, since the context is not
    // created yet. We will check them after context is created.
    m_num_devices = 3;
  }

  void kill_event(cl_event event) {
    if (event->execution_status > CL_COMPLETE)
      ACL_LOCKED(acl_set_execution_status(event, CL_COMPLETE));
    clReleaseEvent(event);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;

public:
  cl_ulong m_callback_count;
  const char *m_callback_errinfo;
};

static void CL_CALLBACK my_fn_event_notify(cl_event event,
                                           cl_int event_command_exec_status,
                                           void *user_data) {
  int *a = (int *)user_data;
  UNREFERENCED_PARAMETER(event);
  *a = event_command_exec_status; // just sets the user data to the excec
                                  // status.
}
// used for checking if once cal register multiple callback functions to
// clSetEventCallback
static void CL_CALLBACK my_fn_event_notify2(cl_event event,
                                            cl_int event_command_exec_status,
                                            void *user_data) {
  int *a = (int *)user_data;
  UNREFERENCED_PARAMETER(event);
  UNREFERENCED_PARAMETER(event_command_exec_status);
  *a = *a + 1;
}

static void CL_CALLBACK notify_me(const char *errinfo, const void *private_info,
                                  size_t cb, void *user_data) {
  CppUTestGroupacl_event *inst = (CppUTestGroupacl_event *)user_data;
  if (inst) {
    inst->m_callback_count++;
    inst->m_callback_errinfo = errinfo;
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST_GROUP(acl_event_default_config) {
  // Like above, but also defines a context with three command queues.
  enum { MAX_DEVICES = 100, NUM_QUEUES = 3 };
  void setup() {
    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_setup_generic_system());
    }

    syncThreads();

    this->load();
  }
  void teardown() {
    for (int i = 0; i < NUM_QUEUES; i++) {
      CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(m_cq[i]));
    }
    CHECK_EQUAL(CL_SUCCESS, clReleaseContext(m_context));

    syncThreads();
    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_teardown_generic_system());
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 3);

    cl_int status;
    m_context = clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

    // Just grab devices that are present.
    CHECK(m_device[0]);
    CHECK(m_device[0]->present);
    CHECK(m_device[1]);
    CHECK(m_device[1]->present);
    CHECK(m_device[2]);
    CHECK(m_device[2]->present);
    m_num_devices = 3;

    for (int i = 0; i < NUM_QUEUES; i++) {
      m_cq[i] = clCreateCommandQueue(m_context, m_device[0],
                                     CL_QUEUE_PROFILING_ENABLE, &status);
      CHECK_EQUAL(CL_SUCCESS, status);
      ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq[i])));
    }
  }

  void kill_event(cl_event event) {
    ACL_LOCKED(acl_set_execution_status(event, CL_COMPLETE));
    ACL_LOCKED(acl_idle_update(event->command_queue->context));
    clReleaseEvent(event);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;
  cl_context m_context;
  cl_command_queue m_cq[NUM_QUEUES];
};

MT_TEST(acl_event, assumed_enums) {
  // We assume these enum values in acl_event.c:l_record_milestone
  CHECK_EQUAL(3, CL_QUEUED);
  CHECK_EQUAL(2, CL_SUBMITTED);
  CHECK_EQUAL(1, CL_RUNNING);
  CHECK_EQUAL(0, CL_COMPLETE);
}

MT_TEST(acl_event, check_ptr) {
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  struct _cl_event fake_event = {0};
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_event>(0)));
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_event>(&fake_event)));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
}

MT_TEST(acl_event, acl_check_events) {
  // Test negative cases of acl_check_events.
  // Positive cases of acl_check_events tested via clWaitForEvents.
  cl_event event[3] = {0, 0, 0};

  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  ////// acl_check_events_in_context

  // Bad contexts
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  ACL_LOCKED(
      CHECK_EQUAL(CL_INVALID_CONTEXT, acl_check_events_in_context(0, 0, 0)));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_CONTEXT,
                         acl_check_events_in_context(&fake_context, 0, 0)));
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Bad input event args.
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_VALUE,
                         acl_check_events_in_context(context, 1, 0)));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_VALUE,
                         acl_check_events_in_context(context, 0, &event[0])));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_EVENT,
                         acl_check_events_in_context(context, 1, &event[0])));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_EVENT,
                         acl_check_events_in_context(context, 2, &event[0])));

  // Can't create more than one context yet (see README.txt), so can't
  // test context consistency...

  // Need to test case where we have valid real event objects.

  ////// acl_check_events

  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_VALUE, acl_check_events(0, 0)));

  // Bad input event args.
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_VALUE, acl_check_events(1, 0)));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_VALUE, acl_check_events(0, &event[0])));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_EVENT, acl_check_events(1, &event[0])));
  ACL_LOCKED(CHECK_EQUAL(CL_INVALID_EVENT, acl_check_events(2, &event[0])));

  // Shutdown context
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event, acl_create_event) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  const int max_events = ACL_MAX_EVENT + 5; // to push the limits
  cl_event event[max_events] = {0, 0, 0};
  cl_event user_event;

  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  cl_command_queue cq0 = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq0);

  // Bad command queue
  struct _cl_command_queue fake_cq = {0};
  acl_lock();
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              acl_create_event(0, 0, 0, CL_COMMAND_MARKER, &user_event));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              acl_create_event(&fake_cq, 0, 0, CL_COMMAND_MARKER, &user_event));

  cl_ulong before_count2 = this->m_callback_count;
  // Bad input event args
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
              acl_create_event(cq0, 1, 0, CL_COMMAND_MARKER, &user_event));
  CHECK_EQUAL(
      CL_INVALID_EVENT_WAIT_LIST,
      acl_create_event(cq0, 0, &event[0], CL_COMMAND_MARKER, &user_event));
  CHECK_EQUAL(2, this->m_callback_count - before_count2);

  // Resource exhaustion
  int num_events = 0;
  for (int i = 0; i < max_events; i++) {
    cl_ulong before_count = this->m_callback_count;
    status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event[num_events]);
    if (status == CL_SUCCESS) {
      cl_event the_event = event[num_events];
      num_events++;
      CHECK_EQUAL(1, acl_ref_count(the_event));
      CHECK(acl_is_valid_ptr(the_event));
      CHECK(acl_event_is_valid(the_event));
      CHECK(acl_event_is_live(the_event));
      CHECK_EQUAL(0, the_event->depend_on.size());
      CHECK_EQUAL(0, the_event->depend_on_me.size());

      // Check the command queue too.
      CHECK_EQUAL(cq0, the_event->command_queue);
      CHECK(cq0);
      CHECK_EQUAL(num_events, cq0->num_commands);
      CHECK_EQUAL(the_event, cq0->inorder_commands.back()); // no recycling yet
    } else {
      CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);
      CHECK_EQUAL(1, this->m_callback_count - before_count);
    }
  }
  CHECK(10 < num_events); // got *some*
  CHECK(ACL_MAX_COMMAND < max_events);
  CHECK_EQUAL(max_events, num_events); // did get as many as requested
  CHECK(cq0);
  acl_update_queue(cq0);

  acl_unlock();

  // Check the callback functions initialization.
  CHECK_EQUAL(NULL, event[0]->callback_list);

  // Check release/retention counts
  CHECK_EQUAL(CL_SUCCESS, clRetainEvent(event[0]));
  CHECK_EQUAL(2, acl_ref_count(event[0]));
  CHECK_EQUAL(CL_SUCCESS, clRetainEvent(event[0]));
  CHECK_EQUAL(3, acl_ref_count(event[0]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[0]));
  CHECK_EQUAL(2, acl_ref_count(event[0]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[0]));
  CHECK_EQUAL(1, acl_ref_count(event[0]));

  // Can't call retain/release on invalid pointer
  CHECK_EQUAL(CL_INVALID_EVENT, clRetainEvent(0));
  CHECK_EQUAL(CL_INVALID_EVENT, clReleaseEvent(0));

  // Check distinctness
  for (int i = 0; i < num_events; i++) {
    for (int j = 0; j < i; j++) {
      CHECK(event[i] != event[j]);
    }
  }
  // Now destroy them
  for (int i = 0; i < num_events; i++) {
    cl_event the_event = event[i];
    this->kill_event(the_event);
    CHECK_EQUAL(0, acl_ref_count(the_event));
    ACL_LOCKED(CHECK(acl_is_valid_ptr(the_event)));
    ACL_LOCKED(CHECK(!acl_event_is_valid(
        the_event))); // We know have perfect information as to whether an event
                      // is in use by the system.
    ACL_LOCKED(CHECK(!acl_event_is_live(the_event)));

    the_event->command_queue = 0;
    ACL_LOCKED(CHECK(!acl_event_is_valid(the_event)));
  }

  // Shutdown context and queue
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event, get_info) {
  cl_event event;

  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  cl_command_queue cq0 = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq0);
  ACL_LOCKED(status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event));
  CHECK_EQUAL(CL_SUCCESS, status);

  // Check clGetEventInfo
  cl_command_queue info_cq = 0;
  cl_context info_ctx = 0;
  cl_command_type info_ct = (cl_command_type)-1;
  cl_int info_es = -1;
  cl_uint info_refcnt = 0xdeadbeef;
  size_t size_out;
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  struct _cl_event fake_event = {0};
  CHECK_EQUAL(CL_INVALID_EVENT,
              clGetEventInfo(0, CL_EVENT_COMMAND_QUEUE, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_EVENT,
              clGetEventInfo(&fake_event, CL_EVENT_COMMAND_QUEUE, 0, 0, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
  // Check bad return params
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventInfo(event, CL_EVENT_COMMAND_QUEUE, 0, &info_cq, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventInfo(event, CL_EVENT_COMMAND_QUEUE, 1, 0, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventInfo(event, CL_EVENT_COMMAND_QUEUE, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventInfo(event, CL_EVENT_COMMAND_QUEUE, 1, &info_cq,
                             0)); // size is too small

  // Now actually get some values out.
  CHECK_EQUAL(CL_SUCCESS, clGetEventInfo(event, CL_EVENT_COMMAND_QUEUE,
                                         sizeof(info_cq), &info_cq, &size_out));
  CHECK_EQUAL(sizeof(info_cq), size_out);
  CHECK_EQUAL(cq0, info_cq);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventInfo(event, CL_EVENT_CONTEXT, sizeof(info_ctx),
                             &info_ctx, &size_out));
  CHECK_EQUAL(sizeof(info_ctx), size_out);
  CHECK_EQUAL(context, info_ctx);
  CHECK_EQUAL(CL_SUCCESS, clGetEventInfo(event, CL_EVENT_COMMAND_TYPE,
                                         sizeof(info_ct), &info_ct, &size_out));
  CHECK_EQUAL(sizeof(info_ct), size_out);
  CHECK_EQUAL(CL_COMMAND_MARKER, info_ct);
  CHECK_EQUAL(CL_QUEUED, event->execution_status);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventInfo(event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                             sizeof(info_es), &info_es, &size_out));
  // Querying command execution status will nudge the scheduler.
  // Since the marker is not waiting on anything, then it just
  // completes right away.
  CHECK_EQUAL(CL_COMPLETE, event->execution_status);
  CHECK_EQUAL(sizeof(info_es), size_out);
  CHECK_EQUAL(CL_COMPLETE, info_es);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventInfo(event, CL_EVENT_REFERENCE_COUNT,
                             sizeof(info_refcnt), &info_refcnt, &size_out));
  CHECK_EQUAL(sizeof(info_refcnt), size_out);
  CHECK_EQUAL(acl_ref_count(event), info_refcnt);

  // Shutdown
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event, event_profiling) {
  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  cl_command_queue cq0 = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq0);

  ACL_LOCKED(
      acl_get_hal()
          ->get_timestamp()); // In the default HAL, this sets to non-zero.
  cl_event event;
  ACL_LOCKED(status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event));
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_event_is_valid(event)));
  ACL_LOCKED(acl_set_execution_status(event, CL_SUBMITTED));

  cl_ulong times[4];
  size_t size_out;
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  struct _cl_event fake_event = {0};
  CHECK_EQUAL(CL_INVALID_EVENT,
              clGetEventProfilingInfo(0, CL_PROFILING_COMMAND_QUEUED, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_EVENT,
              clGetEventProfilingInfo(&fake_event, CL_PROFILING_COMMAND_QUEUED,
                                      0, 0, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
  // Check bad return params
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED, 0,
                                      &times[0], 0));
  CHECK_EQUAL(
      CL_INVALID_VALUE,
      clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED, 1, 0, 0));
  CHECK_EQUAL(
      CL_INVALID_VALUE,
      clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED, 1,
                                      &times[0], 0)); // size is too small

  // Check queued and submitted times
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], &size_out));
  CHECK_EQUAL(sizeof(times[0]), size_out);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], &size_out));
  CHECK_EQUAL(sizeof(times[1]), size_out);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], &size_out));
  CHECK_EQUAL(sizeof(times[2]), size_out);
  CHECK_EQUAL(CL_SUCCESS,
              clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], &size_out));
  CHECK_EQUAL(sizeof(times[3]), size_out);

  CHECK(0 < times[0]);
  CHECK(times[0] < times[1]);
  CHECK_EQUAL(0, times[2]); // never got to this stage
  CHECK_EQUAL(0, times[3]); // never got to this stage

  cl_command_queue cq1 = clCreateCommandQueue(context, m_device[0], 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq1);

  cl_event event_no_profile;
  ACL_LOCKED(status = acl_create_event(cq1, 0, 0, CL_COMMAND_MARKER,
                                       &event_no_profile));
  ACL_LOCKED(CHECK(acl_event_is_valid(event_no_profile)));
  ACL_LOCKED(acl_set_execution_status(event_no_profile, CL_SUBMITTED));

  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(event_no_profile,
                                      CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], &size_out));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(event_no_profile,
                                      CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], &size_out));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(event_no_profile,
                                      CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], &size_out));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(event_no_profile,
                                      CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], &size_out));

  this->kill_event(event);
  this->kill_event(event_no_profile);

  // Shutdown context and queue
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event, event_liveness) {
  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  cl_command_queue cq0 = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq0);

  acl_lock();
  cl_event event;
  status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(acl_event_is_valid(event));
  CHECK(!acl_event_is_done(event));
  CHECK(acl_event_is_live(event));

  // Check event_is_done
  acl_set_execution_status(event, CL_SUBMITTED);
  CHECK(!acl_event_is_done(event));
  acl_set_execution_status(event, CL_RUNNING);
  CHECK(!acl_event_is_done(event));
  acl_set_execution_status(event, CL_COMPLETE);
  CHECK(acl_event_is_done(event));
  acl_set_execution_status(event, -1);
  CHECK(acl_event_is_done(event));

  // Check event_is_live
  // First, check preconditions
  acl_set_execution_status(event, CL_QUEUED);
  CHECK_EQUAL(1, acl_ref_count(event));
  CHECK_EQUAL(0, event->depend_on.size());
  CHECK_EQUAL(0, event->depend_on_me.size());

  acl_reset_ref_count(event);
  acl_set_execution_status(event, CL_COMPLETE);
  CHECK(!acl_event_is_live(event));

  acl_retain(event);
  acl_reset_ref_count(event);
  cl_event tmp = 0;
  event->depend_on.insert(tmp);
  CHECK(acl_event_is_live(event));
  event->depend_on.clear();
  event->depend_on_me.push_back(tmp);
  CHECK(acl_event_is_live(event));
  event->depend_on_me.clear();
  acl_set_execution_status(event, CL_SUBMITTED);
  CHECK(acl_event_is_live(event));
  acl_set_execution_status(event, CL_RUNNING);
  CHECK(acl_event_is_live(event));
  acl_set_execution_status(event, CL_QUEUED);
  CHECK(acl_event_is_live(event));
  acl_retain(event);
  acl_unlock();

  this->kill_event(event);

  // Shutdown context and queue
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event, event_callbacks) {
  // Get a context
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);

  // Just grab devices that are present.
  CHECK(m_device[0]);
  CHECK(m_device[0]->present);
  CHECK(m_device[1]);
  CHECK(m_device[1]->present);
  CHECK(m_device[2]);
  CHECK(m_device[2]->present);
  m_num_devices = 3;

  cl_command_queue cq0 = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cq0);

  acl_lock();
  cl_event event, event2, event3;
  status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(acl_event_is_valid(event));
  CHECK(!acl_event_is_done(event));
  CHECK(acl_event_is_live(event));

  status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event2);
  CHECK_EQUAL(CL_SUCCESS, status);

  status = acl_create_event(cq0, 0, 0, CL_COMMAND_MARKER, &event3);
  CHECK_EQUAL(CL_SUCCESS, status);

  // registering callbacks
  int my_data = -1, my_data2;
  CHECK_EQUAL(0, event->callback_list);
  int states[] = {CL_COMPLETE, CL_RUNNING, CL_SUBMITTED, CL_COMPLETE,
                  CL_COMPLETE};
  // successful ones
  for (unsigned int i = 0; i < 3; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clSetEventCallback(event, states[i], my_fn_event_notify,
                                   (void *)&my_data));
    CHECK_EQUAL(-1, my_data);
    CHECK_EQUAL((void *)&my_data, event->callback_list->notify_user_data);
    CHECK_EQUAL((void *)my_fn_event_notify,
                event->callback_list->event_notify_fn);
    CHECK_EQUAL(states[i], event->callback_list->registered_exec_status);
    CHECK_EQUAL(CL_SUCCESS,
                clSetEventCallback(event2, states[i], my_fn_event_notify,
                                   (void *)&my_data2));
  }
  // failed ones
  CHECK_EQUAL(
      CL_INVALID_EVENT,
      clSetEventCallback(0, CL_COMPLETE, my_fn_event_notify, (void *)&my_data));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clSetEventCallback(event, CL_COMPLETE, 0, (void *)&my_data));
  CHECK_EQUAL(
      CL_INVALID_VALUE,
      clSetEventCallback(event, 999999, my_fn_event_notify, (void *)&my_data));

  // Check if being called back. The user data (my_data) should be updated each
  // time
  my_data = -1;
  acl_set_execution_status(event, CL_SUBMITTED);
  CHECK_EQUAL(CL_SUBMITTED, my_data);
  my_data = -1;
  acl_set_execution_status(event, CL_RUNNING);
  CHECK_EQUAL(CL_RUNNING, my_data);
  my_data = -1;
  acl_set_execution_status(event, CL_COMPLETE);
  CHECK_EQUAL(CL_COMPLETE, my_data);
  my_data = -1;

  // Abnormal termination.
  my_data2 = -1;
  acl_set_execution_status(event2, -10);
  CHECK_EQUAL(-10, my_data2);

  // create a UserEvent and make sure callback of all states are walked through
  // (no skipping)
  status = CL_INVALID_EVENT;
  cl_event user_event = clCreateUserEvent(context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(user_event != 0);

  int call_flags_user_event[] = {
      0, 0, 0, 0,
      0}; // Checking if all functions are called once and only once.
  for (unsigned int i = 0; i < 5;
       i++) { // registering the same function more than once for CL_COMPLETE.
    CHECK_EQUAL(CL_SUCCESS,
                clSetEventCallback(
                    user_event, states[i], my_fn_event_notify2,
                    (void *)&call_flags_user_event[i])); // same function,
                                                         // different user data.
    if (user_event->execution_status <=
        states[i]) // If the function was called for a state that is passes, it
                   // will be called right away and will be removed from the
                   // list. So it doesn't exist anymore to be checked!
      continue;

    CHECK_EQUAL((void *)&call_flags_user_event[i],
                user_event->callback_list->notify_user_data);
    CHECK_EQUAL((void *)my_fn_event_notify2,
                (void *)(user_event->callback_list->event_notify_fn));
    CHECK_EQUAL(states[i], user_event->callback_list->registered_exec_status);
  }

  int call_flags_event3[] = {
      0, 0, 0, 0,
      0}; // Checking if all functions are called once and only once.
  for (unsigned int i = 0; i < 5;
       i++) { // same as above but for event instead of user event.
    CHECK_EQUAL(CL_SUCCESS,
                clSetEventCallback(
                    event3, states[i], my_fn_event_notify2,
                    (void *)&call_flags_event3[i])); // same function, different
                                                     // user data.
    CHECK_EQUAL((void *)&call_flags_event3[i],
                event3->callback_list->notify_user_data);
    CHECK_EQUAL((void *)my_fn_event_notify2,
                (void *)(event3->callback_list->event_notify_fn));
    CHECK_EQUAL(states[i], event3->callback_list->registered_exec_status);
  }

  // Setting the userEvent to complete. all of the callbacks should be called
  // once it is complete.
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(user_event, CL_COMPLETE));
  acl_set_execution_status(event3, CL_COMPLETE);
  for (unsigned int i = 0; i < 5; i++) {
    CHECK_EQUAL(1, call_flags_user_event[i]);
    CHECK_EQUAL(1, call_flags_event3[i]);
  }
  clReleaseEvent(user_event);
  acl_unlock();

  this->kill_event(event);
  this->kill_event(event2);
  this->kill_event(event3);

  // Shutdown context and queue
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

MT_TEST(acl_event_default_config, event_dependency_exec_order) {
  // Check event dependency, and event lifecycle inspectors.

  // Create 3 events on different queues.
  // And stimulate them in backward order.
  // ("Backward" because lower-numbered command_queues are processed first.)
  // Then check timestamps from the HAL to ensure they were exectued in
  // dependency-implied order.
  cl_event event[NUM_QUEUES];

  // Ensure the first timestamp is positive.
  ACL_LOCKED(
      acl_get_hal()
          ->get_timestamp()); // In the default HAL, this sets to non-zero.

  // The trial is done 3 times.
  // The first time, we wait on the last event, then observe and flush.
  // The second time, we wait on the second-last event, then observe and flush.
  // etc.
  for (int waited_on = NUM_QUEUES - 1; waited_on; waited_on--) {
    acl_print_debug_msg("Trial waiting on event %d\n", waited_on);

    for (cl_uint i = 0; i < NUM_QUEUES; i++) {
      // event[i] depends on all events that preceeded it.
      ACL_LOCKED(CHECK_EQUAL(CL_SUCCESS,
                             acl_create_event(m_cq[NUM_QUEUES - i - 1], i,
                                              (i > 0 ? &event[0] : 0),
                                              CL_COMMAND_MARKER, &event[i])));
    }

    // Check initial stage in lifecycle
    for (int i = 0; i < NUM_QUEUES; i++) {
      ACL_LOCKED(CHECK(acl_is_valid_ptr(event[i])));
      ACL_LOCKED(CHECK(acl_event_is_valid(event[i])));
      ACL_LOCKED(CHECK(acl_event_is_live(event[i])));
      ACL_LOCKED(CHECK(!acl_event_is_done(event[i])));
    }

    CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event[waited_on]));

    // Because we waited on the event that depended on all previous
    // events, everything up to that point is done,
    // and the runtime system has flushed them all out
    // of the system.
    for (int i = 0; i < NUM_QUEUES; i++) {
      // acl_dump_event(event[i]);
      ACL_LOCKED(CHECK(acl_is_valid_ptr(event[i])));
      ACL_LOCKED(CHECK(acl_event_is_valid(
          event[i]))); // still valid because we hold a reference
      // still live because we hold a reference.

      // All of them are done, because when we do clWaitForEvents,
      // we trigger submission of the first event, and that
      // knocks down all the dominoes, so to speak, whether or not we
      // *waited* for the later elements.
      ACL_LOCKED(CHECK(acl_event_is_done(event[i])));
    }

    // Check start and end times.
    cl_ulong queued_times[NUM_QUEUES];
    cl_ulong submit_times[NUM_QUEUES];
    cl_ulong start_times[NUM_QUEUES];
    cl_ulong end_times[NUM_QUEUES];
    for (int i = 0; i < NUM_QUEUES; i++) {
      CHECK_EQUAL(CL_SUCCESS, clGetEventProfilingInfo(
                                  event[i], CL_PROFILING_COMMAND_QUEUED,
                                  sizeof(cl_ulong), &queued_times[i], 0));
      CHECK_EQUAL(CL_SUCCESS, clGetEventProfilingInfo(
                                  event[i], CL_PROFILING_COMMAND_SUBMIT,
                                  sizeof(cl_ulong), &submit_times[i], 0));
      CHECK_EQUAL(CL_SUCCESS, clGetEventProfilingInfo(
                                  event[i], CL_PROFILING_COMMAND_START,
                                  sizeof(cl_ulong), &start_times[i], 0));
      CHECK_EQUAL(CL_SUCCESS,
                  clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                          sizeof(cl_ulong), &end_times[i], 0));
    }
    for (int i = 0; i < NUM_QUEUES; i++) {
      // Internally consistent event profile.
      // Recall the example HAL bumps up the timestamp on every call.
      CHECK(queued_times[i] < submit_times[i]);
      acl_print_debug_msg(" Event[%d] times = { %d %d %d %d }\n", i,
                          queued_times[i], submit_times[i], start_times[i],
                          end_times[i]);
      CHECK(submit_times[i] < start_times[i]);
      CHECK(start_times[i] < end_times[i]);

      if (i > 0) {
        // Queued in event order
        CHECK(queued_times[i - 1] < queued_times[i]);

        // Submitted in event order, even
        // though the command queues were checked in reverse order.
        CHECK(submit_times[i - 1] < submit_times[i]);

        // Check separation between dependent events.
        // That is, they actually waited for the prior events.
        CHECK(end_times[i - 1] < submit_times[i]);
      }
    }

    // Now wait for the last one.
    // This will not submit any more commands (because they're all
    // executed).
    // The difference is in what objects are cleaned up:
    //    If waited_on < NUM_QUEUES-1 then we haven't
    // last object invalid (returned to free list).
    CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event[NUM_QUEUES - 1]));

    acl_print_debug_msg("waiting on last one...\n");
    ACL_LOCKED(acl_dump_event(event[NUM_QUEUES - 1]));

    ACL_LOCKED(CHECK(acl_is_valid_ptr(event[NUM_QUEUES - 1])));
    ACL_LOCKED(CHECK(acl_event_is_valid(event[NUM_QUEUES - 1])));

    // Release
    for (int i = 0; i < NUM_QUEUES; i++) {
      acl_print_debug_msg("Releasing Event[%d] %p\n", i, event[i]);
      CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
    }
    CHECK_EQUAL(0, acl_ref_count(event[NUM_QUEUES - 1]));
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[NUM_QUEUES - 1])));

    acl_print_debug_msg("Done trial waiting on event %d\n", waited_on);
  }
}

MT_TEST(acl_event_default_config, event_dependency_recycle) {
  // Make sure we don't run out of dependencies

  // Create NUM_QUEUES events on different queues.
  // Each depends on the previous event.
  cl_event event[NUM_QUEUES];

  // We consume NUM_QUEUES-1 dependencies on each trial.
  // As long as NUM_QUEUES > 1 this limit will do to ensure we don't leak
  // *all* the event dependency objects.
  const int num_trials = 1000;

  // Ensure the first timestamp is positive.
  ACL_LOCKED(
      acl_get_hal()
          ->get_timestamp()); // In the default HAL, this sets to non-zero.

  for (int trial = 0; trial < num_trials; trial++) {
    acl_print_debug_msg("Start trial %d\n", trial);

    for (cl_uint i = 0; i < NUM_QUEUES; i++) {
      // event[i] depends on the preceding event.
      ACL_LOCKED(CHECK_EQUAL(CL_SUCCESS,
                             acl_create_event(m_cq[i], (cl_uint)(i > 0 ? 1 : 0),
                                              (i > 0 ? &event[i - 1] : 0),
                                              CL_COMMAND_MARKER, &event[i])));
    }
    for (int i = 0; i < NUM_QUEUES; i++) {
      CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
    }

    // Flush them through the system by waiting on the last one.
    // Cheats a bit because we told the runtime system we forgot about
    ACL_LOCKED(CHECK(acl_event_is_valid(event[NUM_QUEUES - 1])));
    CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event[NUM_QUEUES - 1]));
  }
}

MT_TEST(acl_event_default_config, enque_marker) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // Check clEnqueueMarker, clWaitForEvents, clFlush and clFinish
  enum { NUM_EVENTS = 3 };

  // Create NUM_EVENTS events on the *same* queue.
  // None of them depend directly on previous commands.
  // But because they are makers they wait for previous events on the same
  // queue.
  cl_event event[NUM_EVENTS];

  // Bad cases.
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clEnqueueMarker(0, &event[0]));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueMarker((cl_command_queue)&m_platform, &event[0]));
  CHECK_EQUAL(CL_INVALID_VALUE, clWaitForEvents(0, &event[0]));
  CHECK_EQUAL(CL_INVALID_VALUE, clWaitForEvents(1, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  struct _cl_event fake_event = {0};
  cl_event fake_event_ptr = &fake_event;
  CHECK_EQUAL(CL_INVALID_EVENT, clWaitForEvents(1, &fake_event_ptr));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clFlush(0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clFlush((cl_command_queue)&m_platform));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clFinish(0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clFinish((cl_command_queue)&m_platform));

  // Ensure the first timestamp is positive.
  ACL_LOCKED(
      acl_get_hal()
          ->get_timestamp()); // In the default HAL, this sets to non-zero.

  for (int trial = 0; trial < 3; trial++) {
    for (int i = 0; i < NUM_EVENTS; i++) {
      // test passing in 0 for event that expects CL_INVALID_VALUE as return
      CHECK_EQUAL(CL_INVALID_VALUE, clEnqueueMarker(m_cq[0], 0));
      // event[i] depends on the preceding event that I know about
      CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event[i]));
    }
    for (int i = 0; i < NUM_EVENTS; i++) {
      ACL_LOCKED(CHECK(acl_event_is_valid(event[i])));
      ACL_LOCKED(CHECK(!acl_event_is_done(event[i])));
    }
    // Trigger bookkeeping rounds, which will make them all execute.
    switch (trial) {
    case 0:
      CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event[0]));
      break;
    case 1:
      CHECK_EQUAL(CL_SUCCESS, clFlush(m_cq[0]));
      break;
    case 2:
      CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq[0]));
      break;
    }
    for (int i = 0; i < NUM_EVENTS; i++) {
      ACL_LOCKED(CHECK(acl_event_is_valid(event[i])));
      ACL_LOCKED(CHECK(acl_event_is_done(event[i]))); // they *all* fell down.
    }

    for (int i = 0; i < NUM_EVENTS; i++) {
      CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
      ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
      ACL_LOCKED(CHECK(acl_event_is_done(event[i]))); // they *all* fell down.
    }
  }
}

MT_TEST(acl_event_default_config, enque_wait_for_events_bad) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // Check basic arg checking.

  enum { NUM_EVENTS = 3 };
  cl_event event[NUM_EVENTS];

  ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq[0])));
  // Bad cases.
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueWaitForEvents(0, 1, &event[0]));
  CHECK_EQUAL(
      CL_INVALID_COMMAND_QUEUE,
      clEnqueueWaitForEvents((cl_command_queue)&m_platform, 1, &event[0]));
  CHECK_EQUAL(CL_INVALID_VALUE, clEnqueueWaitForEvents(m_cq[0], 0, &event[0]));
  CHECK_EQUAL(CL_INVALID_VALUE, clEnqueueWaitForEvents(m_cq[0], 1, 0));
  CHECK_EQUAL(CL_INVALID_VALUE, clEnqueueWaitForEvents(m_cq[0], 0, &event[0]));

  struct _cl_event fake_events[2] = {{0}};
  cl_event event_array[2] = {&fake_events[0], &fake_events[1]};

  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  CHECK_EQUAL(CL_INVALID_EVENT,
              clEnqueueWaitForEvents(m_cq[0], 1, &event_array[0]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event_array[0]));
  CHECK_EQUAL(CL_INVALID_EVENT,
              clEnqueueWaitForEvents(m_cq[1], 2, &event_array[0]));
  // Flush out the event
  clReleaseEvent(event_array[0]);
  clWaitForEvents(1, &event_array[0]);
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
}

MT_TEST(acl_event_default_config, enque_wait_for_events_good) {
  // Check clEnqueueWaitForEvents

  enum { NUM_EVENTS = 4 };
  cl_event event[NUM_EVENTS];

  cl_ulong queued_times[NUM_EVENTS];
  cl_ulong submit_times[NUM_EVENTS];
  cl_ulong start_times[NUM_EVENTS];
  cl_ulong end_times[NUM_EVENTS];

  // Need non-monotonicity to check waiting semantics.
  //
  // An artefact of the scheduler it is round-robin: It will submit at
  // most one command from a command queue and then move on to look at the
  // next command queue, and then return.
  //
  // cq[0] has events 0 and 2
  // cq[1] has events 1 and 3
  //
  // Without dependencies the execution order is 0,1,2,3
  //
  // Before queueing 1 and 3, let's insert a wait for event 2, onto
  // cq[1].
  // Then execution order should be 0 2 1 3.

  ACL_LOCKED(acl_get_hal()->get_timestamp());

  acl_print_debug_msg("Part 1: schedule without dependencies\n");
  // PART 1:  Schedule without dependencies
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event[0]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[1], &event[1]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event[2]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[1], &event[3]));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(4, &event[0]));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(cl_ulong), &queued_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(cl_ulong), &submit_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  // Without waiting, we expect ordering to be 0 1 2 3
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(acl_dump_event(event[i]));
    CHECK(0 < start_times[i]);
    CHECK(start_times[i] < end_times[i]);
  }
  CHECK(end_times[0] < start_times[1]);
  CHECK(end_times[1] < start_times[2]);
  CHECK(end_times[2] < start_times[3]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  clFinish(m_cq[0]);
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
  }

  // PART 2:  Schedule with dependencies
  acl_print_debug_msg("Part 2: schedule with dependencies\n");
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event[0]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[0], &event[2]));
  // We don't receive the event back.. It's implicit.
  CHECK_EQUAL(CL_SUCCESS, clEnqueueWaitForEvents(m_cq[1], 1, &event[2]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[1], &event[1]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarker(m_cq[1], &event[3]));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(4, &event[0]));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(cl_ulong), &queued_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(cl_ulong), &submit_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  // We expect ordering to be 0 2 1 3
  for (int i = 0; i < NUM_EVENTS; i++) {
    acl_print_debug_msg("EVENT %d IS\n", i);
    ACL_LOCKED(acl_dump_event(event[i]));
    CHECK(0 < start_times[i]);
    CHECK(start_times[i] < end_times[i]);
  }
  CHECK(end_times[0] < start_times[2]);
  CHECK(end_times[2] < start_times[1]);
  CHECK(end_times[1] < start_times[3]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  clFinish(m_cq[0]);
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
  }
}

MT_TEST(acl_event_default_config, enqueue_marker_wait_list_bad) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // Check basic arg checking.

  enum { NUM_EVENTS = 3 };
  cl_event event[NUM_EVENTS];

  ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq[0])));

  // Insert one valid event:
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[0]));

  // Bad cases.
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueMarkerWithWaitList(0, 0, NULL, &event[1]));

  // CL_INVALID_EVENT_WAIT_LIST if:
  //    - num_events_in_wait_list == 0 and event_wait_list != NULL
  //    - num_events_in_wait_list > 0 and event_wait_list == NULL
  //    - event objects in event_wait_list are not valid events
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, &event[0], &event[1]));
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
              clEnqueueMarkerWithWaitList(m_cq[0], 1, NULL, &event[1]));

  struct _cl_event fake_events[2] = {{0}};
  cl_event event_array[2] = {&fake_events[0], &fake_events[1]};
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  ACL_LOCKED(acl_set_execution_status(event[0], CL_COMPLETE));
  clReleaseEvent(event[0]);
  syncThreads();
  CHECK_EQUAL(
      CL_INVALID_EVENT_WAIT_LIST,
      clEnqueueMarkerWithWaitList(m_cq[0], 2, &event_array[0], &event[1]));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
}

MT_TEST(acl_event_default_config, enqueue_marker_wait_list_good) {
  enum { NUM_EVENTS = 4 };
  cl_event event[NUM_EVENTS];

  cl_ulong queued_times[NUM_EVENTS];
  cl_ulong submit_times[NUM_EVENTS];
  cl_ulong start_times[NUM_EVENTS];
  cl_ulong end_times[NUM_EVENTS];

  /*
  Queue 0: Events 0 and 2
  Queue 1: Events 1 and 3

  In part 1, there are no dependencies. The only constraint is that 0 happens
  before 2 and 1 happens before 3. In part 2, we add a wait list for Event 1
  consisting of Event 2, so the total order of events must be 0 2 1 3
  */

  acl_print_debug_msg("Part 1: schedule without dependencies\n");
  // PART 1:  Schedule without dependencies
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[0]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[2]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[1]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[3]));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(4, &event[0]));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(cl_ulong), &queued_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(cl_ulong), &submit_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  // Without waiting, we expect ordering to be 0 before 2 and 1 before 3
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(acl_dump_event(event[i]));
    CHECK(0 < start_times[i]);
    CHECK(start_times[i] < end_times[i]);
  }
  CHECK(end_times[0] < start_times[2]);
  CHECK(end_times[1] < start_times[3]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  clFinish(m_cq[0]);
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
  }

  acl_print_debug_msg("Part 2: schedule with dependencies\n");
  // PART 2: Event 1 has a wait list consisting of Event 2, so the expected
  // order of events is: 0 2 1 3
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[0]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[2]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 1, &event[2], &event[1]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[3]));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(4, &event[0]));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(cl_ulong), &queued_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(cl_ulong), &submit_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  // We expect ordering to be 0 2 1 3
  for (int i = 0; i < NUM_EVENTS; i++) {
    acl_print_debug_msg("EVENT %d IS\n", i);
    ACL_LOCKED(acl_dump_event(event[i]));
    CHECK(0 < start_times[i]);
    CHECK(start_times[i] < end_times[i]);
  }
  CHECK(end_times[0] < start_times[2]);
  CHECK(end_times[2] < start_times[1]);
  CHECK(end_times[1] < start_times[3]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  clFinish(m_cq[0]);
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
  }
}

MT_TEST(acl_event_default_config, enqueue_barrier_bad) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // Check basic arg checking.

  // Bad cases.
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clEnqueueBarrier(0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueBarrier((cl_command_queue)&m_platform));
}

// Since we don't support out-of-order execution just yet barriers are
// pretty trivial.
MT_TEST(acl_event_default_config, enqueue_barrier_exhaustion) {
  // Check that we trap resource exhaustion properly.
  // Two trials to ensure we don't leak something.
  for (int trial = 0; trial < 2; trial++) {
    const int max_events = 100000; // 100K is enough.
    int num_events = 0;

    for (int i = 0; i < max_events; i++) {
      cl_int status = clEnqueueBarrier(m_cq[0]);
      if (status == CL_SUCCESS) {
        num_events++;
      } else {
        CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);
        break;
      }
    }

    CHECK(0 < num_events);
    CHECK(ACL_MAX_EVENT < max_events);

    // NEW:  We can create many may of them!!!!
    CHECK_EQUAL(max_events, num_events);

    syncThreads();

    // Flush'm all out. Can't just call clFinish because that wants another
    // event, which we've just exhausted!
    // So use an internal API instead
    ACL_LOCKED(acl_idle_update(m_context));
  }
}

MT_TEST(acl_event_default_config, enqueue_barrier_wait_list_bad) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // Check basic arg checking.
  enum { NUM_EVENTS = 3 };
  cl_event event[NUM_EVENTS];

  ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq[0])));
  // Insert one valid event:
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[0]));

  // Bad cases.
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueBarrierWithWaitList(0, 0, NULL, &event[1]));
  // CL_INVALID_EVENT_WAIT_LIST if:
  //    - num_events_in_wait_list == 0 and event_wait_list != NULL
  //    - num_events_in_wait_list > 0 and event_wait_list == NULL
  //    - event objects in event_wait_list are not valid events
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
              clEnqueueBarrierWithWaitList(m_cq[0], 0, &event[0], &event[1]));
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
              clEnqueueBarrierWithWaitList(m_cq[0], 1, NULL, &event[1]));

  syncThreads();
  acl_set_allow_invalid_type<cl_event>(1);
  syncThreads();
  struct _cl_event fake_events[2] = {{0}};
  cl_event event_array[2] = {&fake_events[0], &fake_events[1]};

  // must manually set the status of the valid and alive event to CL_COMPLETE
  ACL_LOCKED(acl_set_execution_status(event[0], CL_COMPLETE));
  clReleaseEvent(event[0]);
  CHECK_EQUAL(
      CL_INVALID_EVENT_WAIT_LIST,
      clEnqueueBarrierWithWaitList(m_cq[0], 2, &event_array[0], &event[1]));
  syncThreads();
  acl_set_allow_invalid_type<cl_event>(0);
  syncThreads();
}

MT_TEST(acl_event_default_config, enqueue_barrier_wait_ooo) {
  // OOO Queue: Marker1, Barrier, Marker2
  // Tests that Marker2 does not execute before Marker1

  const int NUM_EVENTS = 3;
  cl_event event[NUM_EVENTS];
  cl_ulong start_times[NUM_EVENTS];
  cl_ulong end_times[NUM_EVENTS];
  cl_int status = CL_SUCCESS;

  cl_command_queue cq_ooo = clCreateCommandQueue(
      m_context, m_device[0],
      CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
      &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq_ooo)));

  cl_event user = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(cq_ooo, 1, &user, &event[0]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueBarrierWithWaitList(cq_ooo, 1, &event[0], &event[1]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(cq_ooo, 1, &user, &event[2]));
  ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq_ooo)));

  CHECK_EQUAL(
      CL_QUEUED,
      event[2]->execution_status); // didn't get submitted ahead of event[0]

  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(user, CL_COMPLETE));
  ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq_ooo)));
  CHECK_EQUAL(CL_SUCCESS, clFinish(cq_ooo));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  CHECK(end_times[0] < start_times[1]);
  CHECK(end_times[1] < start_times[2]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(user));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq_ooo));
}

MT_TEST(acl_event_default_config, enqueue_barrier_wait_list_good) {
  enum { NUM_EVENTS = 6 };
  cl_event event[NUM_EVENTS];

  cl_ulong queued_times[NUM_EVENTS];
  cl_ulong submit_times[NUM_EVENTS];
  cl_ulong start_times[NUM_EVENTS];
  cl_ulong end_times[NUM_EVENTS];

  /*
  Note: Test is written to work independent of in-order or out-of-order
  execution
  */

  /*
  Queue 0: Events 0 1 2
  Queue 1: Events 3 4 5
  Event 1 is a barrier, so 2 can't execute until it is complete

  Dependencies: event 2 depends on event 0 to complete, event 1 depends on event
  5 to complete

  The order could be either 0,3,4,5,1,2 or 3,0,4,5,1,2

  For in-order execution, there's no difference between markers and barriers.
  For out-of-order execution, the difference is that barriers block events
  enqueued after them until they are executed. If it weren't for this, it would
  be possible for event 2 to leapfrog over event 1 after event 0 finishes but
  before events 3/4/5 finish. The test checks that the barrier prevents this
  from happening.
  */

  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[3]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[4]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[1], 0, NULL, &event[5]));

  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 0, NULL, &event[0]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueBarrierWithWaitList(m_cq[0], 1, &event[5], &event[1]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueMarkerWithWaitList(m_cq[0], 1, &event[0], &event[2]));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(6, &event[0]));

  // Read event times.
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(cl_ulong), &queued_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(cl_ulong), &submit_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_START,
                                        sizeof(cl_ulong), &start_times[i], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event[i], CL_PROFILING_COMMAND_END,
                                        sizeof(cl_ulong), &end_times[i], 0));
  }

  for (int i = 0; i < NUM_EVENTS; i++) {
    acl_print_debug_msg("EVENT %d IS\n", i);
    ACL_LOCKED(acl_dump_event(event[i]));
    CHECK(0 < start_times[i]);
    CHECK(start_times[i] < end_times[i]);
  }

  CHECK(end_times[3] < start_times[4]);
  CHECK(end_times[4] < start_times[5]);
  CHECK(end_times[5] < start_times[1]);
  CHECK(end_times[0] < start_times[1]);
  CHECK(end_times[1] < start_times[2]);

  // Flush these events out of the system
  for (int i = 0; i < NUM_EVENTS; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(event[i]));
  }
  clFinish(m_cq[0]);
  for (int i = 0; i < NUM_EVENTS; i++) {
    ACL_LOCKED(CHECK(!acl_event_is_valid(event[i])));
  }
}

MT_TEST(acl_event_default_config, user_event_bad_context) {
  CHECK_EQUAL(0, clCreateUserEvent(0, 0));

  cl_int status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateUserEvent(0, &status));
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);

  status = CL_SUCCESS;
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  CHECK_EQUAL(0, clCreateUserEvent(&fake_context, &status));
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();
}

MT_TEST(acl_event_default_config, user_event_ok_complete) {
  // Create a real event.
  ACL_LOCKED(acl_get_hal()->get_timestamp()); // ensure not zero
  cl_int status = CL_INVALID_EVENT;
  cl_event E = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(E != 0);

  CHECK_EQUAL(1, acl_ref_count(E));
  CHECK_EQUAL(CL_SUBMITTED, E->execution_status);

  // All user events go on a special out of order queue.
  CHECK_EQUAL(m_context->user_event_queue, E->command_queue);
  CHECK_EQUAL(0, m_context->user_event_queue->submits_commands);
  CHECK_EQUAL(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
              (m_context->user_event_queue->properties &
               CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE));
  ACL_LOCKED(CHECK(m_context->user_event_queue->commands.find(E) !=
                   m_context->user_event_queue->commands.end()));

  // According to the spec clGetEventInfo should always return NULL for
  // CL_EVENT_COMMAND_QUEUE queries on user events.
  size_t size_out;
  cl_command_queue info_cq;
  CHECK_EQUAL(CL_SUCCESS, clGetEventInfo(E, CL_EVENT_COMMAND_QUEUE,
                                         sizeof(info_cq), &info_cq, &size_out));
  CHECK_EQUAL(sizeof(info_cq), size_out);
  CHECK_EQUAL(NULL, info_cq);

  // Check clGetEventInfo for the case where execution status stays at queued.
  cl_int info_es;
  CHECK_EQUAL(CL_SUCCESS, clGetEventInfo(E, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                         sizeof(info_es), &info_es, &size_out));
  // Querying command execution status will nudge the scheduler.
  // But nothing happens with a user event until the user says
  // so, so it stays at submitted.
  CHECK_EQUAL(CL_SUBMITTED, E->execution_status);
  CHECK_EQUAL(sizeof(info_es), size_out);
  CHECK_EQUAL(CL_SUBMITTED, info_es);

  // User events should not have profiling info returned since they are not
  // supposed to be a part of a command queue according the the OpenCL spec.
  cl_ulong times[4];
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], 0));

  // Bad completions.
  CHECK_EQUAL(CL_INVALID_VALUE, clSetUserEventStatus(E, CL_QUEUED));
  CHECK_EQUAL(CL_INVALID_VALUE, clSetUserEventStatus(E, CL_SUBMITTED));
  CHECK_EQUAL(CL_INVALID_VALUE, clSetUserEventStatus(E, CL_RUNNING));
  CHECK_EQUAL(CL_INVALID_VALUE, clSetUserEventStatus(E, 4));

  // Good completion
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(E, CL_COMPLETE));

  CHECK_EQUAL(1, acl_ref_count(E));
  CHECK_EQUAL(CL_COMPLETE, E->execution_status);

  // Check clGetEventInfo with execution status query.
  CHECK_EQUAL(CL_SUCCESS, clGetEventInfo(E, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                         sizeof(info_es), &info_es, &size_out));
  CHECK_EQUAL(sizeof(info_es), size_out);
  CHECK_EQUAL(CL_COMPLETE, info_es);

  // User events should not have profiling info returned since they are not
  // supposed to be a part of a command queue according the the OpenCL spec.
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], 0));

  clReleaseEvent(E);
  ACL_LOCKED(CHECK_EQUAL(0, acl_event_is_live(E)));
}

MT_TEST(acl_event_default_config, user_event_complete_err) {
  // Create a real event.
  ACL_LOCKED(acl_get_hal()->get_timestamp()); // ensure not zero
  cl_int status = CL_INVALID_EVENT;
  cl_event E = clCreateUserEvent(m_context, &status);
  CHECK(E != 0);

  CHECK_EQUAL(1, acl_ref_count(E));
  CHECK_EQUAL(CL_SUBMITTED, E->execution_status);

  // User events should not have profiling info returned since they are not
  // supposed to be a part of a command queue according the the OpenCL spec.
  cl_ulong times[4];
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], 0));

  // Complete as error.
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(E, -42));

  CHECK_EQUAL(1, acl_ref_count(E));
  CHECK_EQUAL(-42, E->execution_status);

  // User events should not have profiling info returned since they are not
  // supposed to be a part of a command queue according the the OpenCL spec.
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_QUEUED,
                                      sizeof(times[0]), &times[0], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_SUBMIT,
                                      sizeof(times[1]), &times[1], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_START,
                                      sizeof(times[2]), &times[2], 0));
  CHECK_EQUAL(CL_PROFILING_INFO_NOT_AVAILABLE,
              clGetEventProfilingInfo(E, CL_PROFILING_COMMAND_END,
                                      sizeof(times[3]), &times[3], 0));

  clReleaseEvent(E);
  ACL_LOCKED(CHECK_EQUAL(0, acl_event_is_live(E)));
}

MT_TEST(acl_event_default_config, user_event_out_of_order) {
  // Ensure that user events don't block progress elsewhere.
  // Have to process them in an out-of-order special queue.
  ACL_LOCKED(acl_get_hal()->get_timestamp()); // ensure not zero

  // EA and EB will both go on the same out-of-order queue.

  cl_int status = CL_INVALID_EVENT;
  cl_event EA = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = CL_INVALID_EVENT;

  cl_event EB = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = CL_INVALID_EVENT;

  // Should be able to signal B, then wait for the event to complete.
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(EB, CL_COMPLETE));
  // This will terminate if and only if we process the user event queue
  // out-of-order.
  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &EB));

  // Should be able to signal A, then wait for the event to complete.
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(EA, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &EA));

  // clean up
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(EA));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(EB));
}

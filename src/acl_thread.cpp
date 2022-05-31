// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <thread>

// External library headers.
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_hal.h>
#include <acl_platform.h>
#include <acl_thread.h>

// Global locking data.
// Just like members of acl_locking_data_t but with
// "static" on the l_acl_global_condvar and ACL_TLS on
// lock_counts and sig_flag.
ACL_TLS int acl_global_lock_count = 0;
ACL_TLS int acl_inside_sig_flag = 0;
ACL_TLS int acl_inside_sig_old_lock_count = 0;

static struct acl_condvar_s l_acl_global_condvar;

// l_init_once() is defined in an OS-specific section below
static void l_init_once();

void acl_lock(acl_locking_data_t *locking_data) {

  if (locking_data == nullptr) {
    // Condvar is not specified, so use the global one
    l_init_once();
    if (acl_global_lock_count == 0) {
      acl_acquire_condvar(&l_acl_global_condvar);
    }
    acl_global_lock_count++;

  } else {

    // Locking data (condvar and associated counters) is given.
    // This condvar must have already been initialized during its owner's
    // creation time.
    if (locking_data->lock_count == 0) {
      acl_acquire_condvar(&locking_data->condvar);
    }
    locking_data->lock_count++;
  }
}

void acl_unlock(acl_locking_data_t *locking_data) {

  if (locking_data == nullptr) {
    acl_assert_locked();
    acl_global_lock_count--;
    if (acl_global_lock_count == 0) {
      acl_release_condvar(&l_acl_global_condvar);
    }
  } else {
    acl_assert_locked(locking_data);
    locking_data->lock_count--;
    if (locking_data->lock_count == 0) {
      acl_release_condvar(&locking_data->condvar);
    }
  }
}

int acl_is_locked_callback(acl_locking_data_t *locking_data) { 
  if (locking_data == nullptr) {
    return (acl_global_lock_count > 0); 
  } else {
    return (locking_data->lock_count > 0); 
  }
}

int acl_suspend_lock(acl_locking_data_t *locking_data) {

  if (locking_data == nullptr) {
    int old_lock_count = acl_global_lock_count;
    acl_global_lock_count = 0;
    if (old_lock_count > 0)
      acl_release_condvar(&l_acl_global_condvar);
    return old_lock_count;
  } else {
    int old_lock_count = locking_data->lock_count;
    locking_data->lock_count = 0;
    if (old_lock_count > 0)
      acl_release_condvar(&locking_data->condvar);
    return old_lock_count;
  }
}

void acl_resume_lock(int lock_count, acl_locking_data_t *locking_data) {
  acl_assert_unlocked(locking_data);
  if (locking_data == nullptr) {
    if (lock_count > 0) {
      acl_acquire_condvar(&l_acl_global_condvar);
    }
    acl_global_lock_count = lock_count;
  } else {
    if (lock_count > 0) {
      acl_acquire_condvar(&locking_data->condvar);
    }
    locking_data->lock_count = lock_count;
  }
}

void acl_wait_for_device_update(cl_context context) {
  acl_locking_data_t *locking_data = get_device_op_queue_locking_data_from_context(context);
  //acl_assert_locked(locking_data);
  acl_assert_locked();
  if (acl_get_hal()->get_debug_verbosity &&
      acl_get_hal()->get_debug_verbosity() > 0) {
    unsigned timeout = 5; // Seconds
    // Keep waiting until signal is received
    //while (acl_timed_wait_condvar(&locking_data->condvar, timeout))
    while (acl_timed_wait_condvar(&l_acl_global_condvar, timeout))
      acl_context_print_hung_device_status(context);
  } else {
    //acl_wait_condvar(&locking_data->condvar);
    acl_wait_condvar(&l_acl_global_condvar);
  }
}

void acl_signal_device_update(acl_locking_data_t *locking_data) { 
  if (locking_data == nullptr) {
    acl_signal_condvar(&l_acl_global_condvar); 
  } else {
    acl_signal_condvar(&locking_data->condvar); 
  }
}

#ifdef __linux__

#include <linux/unistd.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

ACL_TLS sigset_t acl_sigset; // Keeps the previous set of masked signals, when a
                             // call to acl_sig_block_signals is made

#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 30)
// From gettid(2): The gettid() system call first appeared on Linux in
// kernel 2.4.11. Library support was added in glibc 2.30. (Earlier
// glibc versions did not provide a wrapper for this system call,
// necessitating the use of syscall(2).)
static inline pid_t gettid(void) { return (pid_t)syscall(__NR_gettid); }
#endif

int acl_get_thread_id() { return (int)gettid(); }

int acl_get_pid() { return (int)getpid(); }

static void l_init_once() {
  // on windows this function does something
}

__attribute__((constructor)) static void l_global_lock_init() {
  acl_init_condvar(&l_acl_global_condvar);
}

__attribute__((destructor)) static void l_global_lock_uninit() {
  acl_reset_condvar(&l_acl_global_condvar);
}

#else // !LINUX

#include <windows.h>

int acl_get_thread_id() { return (int)GetCurrentThreadId(); }

int acl_get_pid() { return (int)GetCurrentProcessId(); }

INIT_ONCE l_init_once_state = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK l_init_once_callback(PINIT_ONCE InitOnce, PVOID Parameter,
                                          PVOID *Context) {
  // avoid compile errors caused by not using these arguments
  (void)(InitOnce);
  (void)(Parameter);
  (void)(Context);

  acl_init_condvar(&l_acl_global_condvar);
  return TRUE;
}

static void l_init_once() {
  BOOL ret =
      InitOnceExecuteOnce(&l_init_once_state, l_init_once_callback, NULL, NULL);
  assert(ret);
}

#endif // !LINUX

// Current thread releases mutex lock and sleeps briefly to allow other threads
// a chance to execute. This function is useful for multithreaded hosts with
// e.g. polling BSPs (using yield) to prevent one thread from hogging the mutex
// while waiting for something like clFinish.
void acl_yield_lock_and_thread(acl_locking_data_t *locking_data) {
  int lock_count;
  lock_count = acl_suspend_lock(locking_data);
#ifdef __arm__
  // arm-linux-gnueabihf-g++ version used is 4.7.1.
  // std::this_thread::yield can be enabled for it by defining
  // _GLIBCXX_USE_SCHED_YIELD, but there are no known SoC BSPs that uses MMD
  // yield. Leaving it to continue using nanosleep.
  struct timespec ts = {0, 1}; // sleep for 1ns and 0s
  nanosleep(&ts, NULL);
#else
  std::this_thread::yield();
#endif
  acl_resume_lock(lock_count, locking_data);
}

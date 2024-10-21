// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// External library headers.
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_hal.h>
#include <acl_thread.h>

ACL_TLS int acl_global_lock_count = 0;
ACL_TLS int acl_inside_sig_flag = 0;
ACL_TLS int acl_inside_sig_old_lock_count = 0;
acl_mutex_wrapper_t acl_mutex_wrapper;

static struct acl_condvar_s l_acl_global_condvar;

// l_init_once() is defined in an OS-specific section below
static void l_init_once();

void acl_mutex_wrapper_t::lock() {
  l_init_once();
  if (acl_global_lock_count == 0) {
    acl_acquire_condvar(&l_acl_global_condvar);
  }
  acl_global_lock_count++;
}

void acl_mutex_wrapper_t::unlock() {
  acl_assert_locked();
  acl_global_lock_count--;
  if (acl_global_lock_count == 0) {
    acl_release_condvar(&l_acl_global_condvar);
  }
}

int acl_mutex_wrapper_t::suspend_lock() {
  int old_lock_count = acl_global_lock_count;
  acl_global_lock_count = 0;
  if (old_lock_count > 0)
    acl_release_condvar(&l_acl_global_condvar);
  return old_lock_count;
}

void acl_mutex_wrapper_t::resume_lock(int lock_count) {
  acl_assert_unlocked();
  if (lock_count > 0)
    acl_acquire_condvar(&l_acl_global_condvar);
  acl_global_lock_count = lock_count;
}

void acl_wait_for_device_update(cl_context context) {
  acl_assert_locked();
  if (acl_get_hal()->get_debug_verbosity &&
      acl_get_hal()->get_debug_verbosity() > 0) {
    unsigned timeout = 5; // Seconds
    // Keep waiting until signal is received
    while (acl_timed_wait_condvar(&l_acl_global_condvar, timeout))
      acl_context_print_hung_device_status(context);
  } else {
    acl_wait_condvar(&l_acl_global_condvar);
  }
}

void acl_signal_device_update() { acl_signal_condvar(&l_acl_global_condvar); }

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
  acl_mutex_wrapper = acl_mutex_wrapper_t();
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
  acl_mutex_wrapper = acl_mutex_wrapper_t();
  return TRUE;
}

static void l_init_once() {
  BOOL ret =
      InitOnceExecuteOnce(&l_init_once_state, l_init_once_callback, NULL, NULL);
  assert(ret);
}

#endif // !LINUX

// Blocking/Unblocking signals (Actual implementation only for Linux)
acl_signal_blocker::acl_signal_blocker() {
#ifdef __linux__
  sigset_t mask;
  if (sigfillset(&mask)) {
    assert(0 && "Error in creating signal mask in status handler");
  }
  if (pthread_sigmask(SIG_BLOCK, &mask, &acl_sigset)) {
    assert(0 && "Error in blocking signals in status handler");
  }
#endif
}

acl_signal_blocker::~acl_signal_blocker() {
#ifdef __linux__
  if (pthread_sigmask(SIG_SETMASK, &acl_sigset, NULL)) {
    assert(0 && "Error in unblocking signals in status handler");
  }
#endif
}

// Current thread releases mutex lock and sleeps briefly to allow other threads
// a chance to execute. This function is useful for multithreaded hosts with
// e.g. polling BSPs (using yield) to prevent one thread from hogging the mutex
// while waiting for something like clFinish.
void acl_yield_lock_and_thread() {
  acl_suspend_lock_guard lock{acl_mutex_wrapper};
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
}

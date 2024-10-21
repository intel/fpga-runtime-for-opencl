// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_THREAD_H
#define ACL_THREAD_H

#include "acl.h"
#include "acl_context.h"
#include "acl_types.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

// System headers.
#include <mutex>
#include <thread>

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __linux__
#include <signal.h>
#define ACL_TLS __thread
#else // WINDOWS
#define ACL_TLS __declspec(thread)
#endif

// Foward declaration
class acl_mutex_wrapper_t;

extern ACL_TLS int acl_global_lock_count;
extern ACL_TLS int acl_inside_sig_flag;
extern ACL_TLS int acl_inside_sig_old_lock_count;

extern acl_mutex_wrapper_t acl_mutex_wrapper;

// -- signal handler functions --
// When we enter a signal handler, we save "acl_global_lock_count" to
// "acl_inside_sig_old_lock_count" temporarily. This is because the signal
// handler will run inside one of the existing threads randomly and so will get
// the value of the lock count that that thread had. However, it's misleading
// because conceptually the signal handler doesn't ever really have the lock.
// Therefore we temporarily change the lock count to 0 while inside the signal
// handler so that things like "acl_assert_locked()" will operate as expected.
// If a function needs an assert that passes if either the lock is held or
// inside a signal handler, it can use "acl_assert_locked_or_sig()".

static inline int acl_is_inside_sig() { return acl_inside_sig_flag; }

static inline void acl_assert_inside_sig() { assert(acl_is_inside_sig()); }

static inline void acl_assert_outside_sig() { assert(!acl_is_inside_sig()); }

static inline void acl_sig_started() {
  assert(!acl_inside_sig_flag);
  acl_inside_sig_flag = 1;
  acl_inside_sig_old_lock_count = acl_global_lock_count;
  acl_global_lock_count = 0;
}

static inline void acl_sig_finished() {
  assert(acl_inside_sig_flag);
  acl_inside_sig_flag = 0;
  acl_global_lock_count = acl_inside_sig_old_lock_count;
}

// Blocking/Unblocking signals (Only implemented for Linux)
class acl_signal_blocker {
public:
  acl_signal_blocker &operator=(const acl_signal_blocker &) = delete;
  acl_signal_blocker(const acl_signal_blocker &) = delete;
  acl_signal_blocker();
  ~acl_signal_blocker();
};

// -- global lock functions --

void acl_wait_for_device_update(cl_context context);
void acl_signal_device_update();

static inline int acl_is_locked() { return (acl_global_lock_count > 0); }

static inline void acl_assert_locked() { assert(acl_is_locked()); }

static inline void acl_assert_locked_or_sig() {
  assert(acl_is_locked() || acl_is_inside_sig());
}

static inline void acl_assert_unlocked() { assert(!acl_is_locked()); }

// -- misc functions --

int acl_get_thread_id();
int acl_get_pid();
void acl_yield_lock_and_thread();

#if defined(__cplusplus)
} /* extern "C" */
#endif

// -- RAII wrapper classes --

// To follow RAII, provide a mutex class acl_mutex_wrapper_t which may be used
// with std::scoped_lock and std::unique_lock. Note that std::scoped_lock may
// only be constructed with a single instance of acl_mutex_wrapper_t since the
// latter only implements BasicLockable but not Lockable, due to a lack of
// try_lock() functionality in acl_threadsupport.
class acl_mutex_wrapper_t {
public:
  void lock();
  void unlock();
  int suspend_lock();
  void resume_lock(int lock_count);
};

class acl_suspend_lock_guard {
public:
  explicit acl_suspend_lock_guard(acl_mutex_wrapper_t &mutex) : mutex(mutex) {
    lock_count = mutex.suspend_lock();
  };
  ~acl_suspend_lock_guard() { mutex.resume_lock(lock_count); }

  // Delete copy constructor and copy assignment
  acl_suspend_lock_guard(const acl_suspend_lock_guard &) = delete;
  acl_suspend_lock_guard &operator=(const acl_suspend_lock_guard &) = delete;

private:
  int lock_count;
  acl_mutex_wrapper_t &mutex;
};

#endif // ACL_THREAD_H

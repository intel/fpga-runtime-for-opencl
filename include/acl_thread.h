// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_THREAD_H
#define ACL_THREAD_H

// System headers.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

// External library headers.
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include "acl.h"


#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __linux__
#include <signal.h>
#define ACL_TLS __thread
#else // WINDOWS
#define ACL_TLS __declspec(thread)
#endif


/* An opaque type for critical section + condition variable.
 * Use indirection here so we don't force every module in the world to pull
 * in windows.h.
 */
// typedef struct acl_condvar_s *acl_condvar_t;

typedef struct acl_locking_data_s acl_locking_data_t;
struct acl_locking_data_s {
  struct acl_condvar_s condvar;
  int lock_count;
  int inside_sig_flag;
  int inside_sig_old_lock_count;
};


extern ACL_TLS int acl_global_lock_count;
extern ACL_TLS int acl_inside_sig_flag;
extern ACL_TLS int acl_inside_sig_old_lock_count;

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

static inline int acl_is_inside_sig(acl_locking_data_t *locking_data = nullptr) { 
  if (locking_data == nullptr) {
    return acl_inside_sig_flag; 
  } else {
    return locking_data->inside_sig_flag;
  }
}

static inline void acl_assert_inside_sig(acl_locking_data_t *locking_data = nullptr) { 
  assert(acl_is_inside_sig(locking_data)); 
}

static inline void acl_assert_outside_sig(acl_locking_data_t *locking_data = nullptr) { 
  assert(!acl_is_inside_sig(locking_data)); 
}

static inline void acl_sig_started(acl_locking_data_t *locking_data = nullptr) {
  if (locking_data == nullptr) {
    assert(!acl_inside_sig_flag);
    acl_inside_sig_flag = 1;
    acl_inside_sig_old_lock_count = acl_global_lock_count;
    acl_global_lock_count = 0;
  } else {
    assert(!locking_data->inside_sig_flag);
    locking_data->inside_sig_flag = 1;
    locking_data->inside_sig_old_lock_count = locking_data->lock_count;
    locking_data->lock_count = 0;
  }
}

static inline void acl_sig_finished(acl_locking_data_t *locking_data = nullptr) {
  if (locking_data == nullptr) {
    assert(acl_inside_sig_flag);
    acl_inside_sig_flag = 0;
    acl_global_lock_count = acl_inside_sig_old_lock_count;
  } else {
    assert(locking_data->inside_sig_flag);
    locking_data->inside_sig_flag = 0;
    locking_data->lock_count = locking_data->inside_sig_old_lock_count;
  }
}

// Blocking/Unblocking signals (Only implemented for Linux)
#ifdef __linux__
extern ACL_TLS sigset_t acl_sigset;
static inline void acl_sig_block_signals() {
  sigset_t mask;
  if (sigfillset(&mask))
    assert(0 && "Error in creating signal mask in status handler");
  if (pthread_sigmask(SIG_BLOCK, &mask, &acl_sigset))
    assert(0 && "Error in blocking signals in status handler");
}
static inline void acl_sig_unblock_signals() {
  if (pthread_sigmask(SIG_SETMASK, &acl_sigset, NULL))
    assert(0 && "Error in unblocking signals in status handler");
}
#endif

// -- global lock functions --

void acl_lock(acl_locking_data_t *locking_data = nullptr);
void acl_unlock(acl_locking_data_t *locking_data = nullptr);
int  acl_suspend_lock(acl_locking_data_t *locking_data = nullptr);
void acl_resume_lock(int lock_count, acl_locking_data_t *locking_data = nullptr);
void acl_wait_for_device_update(cl_context context);
void acl_signal_device_update(acl_locking_data_t *locking_data = nullptr);

static inline int acl_is_locked(acl_locking_data_t *locking_data = nullptr) {
  if (locking_data == nullptr) {
    return acl_global_lock_count > 0;
  } else {
    return (locking_data->lock_count > 0); 
  }
}

// Used by dynamically loaded libs to check lock status.
int acl_is_locked_callback(acl_locking_data_t *locking_data = nullptr);

static inline void acl_assert_locked(acl_locking_data_t *locking_data = nullptr) { 
  assert(acl_is_locked(locking_data)); 
}

static inline void acl_assert_locked_or_sig(acl_locking_data_t *locking_data = nullptr) {
  assert(acl_is_locked(locking_data) || acl_is_inside_sig(locking_data));
}

static inline void acl_assert_unlocked(acl_locking_data_t *locking_data = nullptr) { 
  assert(!acl_is_locked(locking_data)); 
}

// -- misc functions --

int acl_get_thread_id();
int acl_get_pid();
void acl_yield_lock_and_thread(acl_locking_data_t *locking_data = nullptr);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif // ACL_THREAD_H

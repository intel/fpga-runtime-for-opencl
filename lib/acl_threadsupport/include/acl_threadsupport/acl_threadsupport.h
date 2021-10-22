// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_THREADSUPPORT_H
#define ACL_THREADSUPPORT_H

// HALs for use when we emulate the kernels on (primarily x86).
//
// Kernels launch and complete right away, after spawing an enqueueing thread,
// which will terminate when all kernel threads have terminated
// Kernel fiber schaeduler is also run as a thread. And it will not terminate
// until the host terminates.
// Kernel threads will be run in sequence, possible switching between kernel
// threads at barriers and channel calls

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __linux__
#include <pthread.h>
#include <sched.h> // sched_setaffinity
#include <semaphore.h>
#include <ucontext.h>
#else
#pragma warning(push)
#pragma warning(disable : 4668)
#include <windows.h>
#pragma warning(pop)
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#ifdef __linux__
typedef pthread_t acl_thread_t;
typedef sem_t acl_sem_t;
#else
typedef HANDLE acl_thread_t;
typedef HANDLE acl_sem_t;
#endif

int acl_thread_create(acl_thread_t *newthread, void *attr,
                      void *(*start_routine)(void *), void *arg);

int acl_thread_yield(void);

int acl_thread_join(acl_thread_t *threadp);

#ifdef __linux__
typedef ucontext_t *acl_fiber_t;
#else
typedef void *acl_fiber_t;
#endif

int acl_init_fibers(acl_fiber_t *fiberp);

int acl_fiber_create(acl_fiber_t *fiberp, acl_fiber_t *masterp,
                     void (*func)(void), size_t fiber_stack_size, int argc,
                     int arg);

// Only used by acl_emulator_test.cpp for now. Live Linux has the teardown
// co-allocated with returning the workitem data
int acl_fiber_delete(acl_fiber_t *fiberp);

int acl_destroy_fibers(acl_fiber_t *fiberp);

int acl_fiber_swapcontext(acl_fiber_t *currentcp, const acl_fiber_t *nextp);

// This can be used to limit the cpu cores that the process is running on (used
// for emulation). If it fails, the mask is ignored.
void acl_set_process_affinity_mask(unsigned long long process_affinity_mask);

// Mutex type used to syncronize acl_threads, or rather preventing different
// threads from updating data structures in a way that would introduce data
// races. Note that it is not needed to protect fiber accesses since they are
// already completly serialized vs each other
#ifdef __linux__
typedef pthread_mutex_t acl_mutex_t;
#else
typedef HANDLE acl_mutex_t;
#endif

int acl_mutex_init(acl_mutex_t *mutex, void *mutexattr);

int acl_mutex_lock(acl_mutex_t *mutex);

int acl_mutex_unlock(acl_mutex_t *mutex);

int acl_mutex_destroy(acl_mutex_t *mutex);

// Thread Local Storage
#ifdef __linux__
typedef pthread_key_t acl_tls_key_t;
#else
typedef DWORD acl_tls_key_t;
#endif

int acl_tls_new(acl_tls_key_t *key);

int acl_tls_delete(acl_tls_key_t key);

void *acl_tls_get(acl_tls_key_t key);

int acl_tls_set(acl_tls_key_t key, const void *data);

// Reads/writes to memory cannot cross this call.
// Requires cooperation from the compiler and the machine.
void acl_memory_barrier(void);

////////////////
// Condition variables with async-safe broadcast.
//
// Intended lifecycle (as regular expressions):
//
//    ( init ( acquire wait | acquire release | signal )* reset )*
//
// The required behaviours are:
//
//    - At most one thread could have done "acquire" without also
//      doing either "wait" or "release". (Mutual exclusion)
//
//    - A waiting thread will not busy-wait (burn CPU needlessly).
//
//    - The signal call may be concurrent with either
//      acquire-release or acquire-wait.
//
//    - We need broadcast behaviour: A single signal should
//      wake up (unblock) all pending waiters.
//
//    - The signal call must be safe to use inside an OS-level signal handler
//      which might be blocking OS signals.
//      So in particular you can't use a pthread_mutex or condition variable
//      for the signal wakeup.  The only safe thing to use is sem_post
//      which increments the count on a semaphore.
//
// Note that "init" does not automatically call "reset".

struct acl_condvar_s;

void acl_reset_condvar(struct acl_condvar_s *);
void acl_init_condvar(struct acl_condvar_s *);
void acl_acquire_condvar(struct acl_condvar_s *);
void acl_release_condvar(struct acl_condvar_s *);
void acl_wait_condvar(struct acl_condvar_s *);
int acl_timed_wait_condvar(struct acl_condvar_s *, unsigned timeout_period);
void acl_signal_condvar(
    struct acl_condvar_s *); // Safe to use in a signal handler.

// Dump internal state.
void acl_dump_condvar(const char *str, struct acl_condvar_s *);
void acl_set_dump_condvar(struct acl_condvar_s *);

int acl_init_sem(acl_sem_t *sem);
int acl_sem_wait(acl_sem_t *sem);
int acl_sem_trywait(acl_sem_t *sem);
int acl_sem_timedwait(acl_sem_t *sem, unsigned timeout);
int acl_sem_post(acl_sem_t *sem);
int acl_sem_destroy(acl_sem_t *sem);

#ifdef __linux__
#define P(S)                                                                   \
  do {                                                                         \
    while (sem_wait(&S))                                                       \
      ;                                                                        \
  } while (0)
#define V(S)                                                                   \
  do {                                                                         \
    sem_post(&S);                                                              \
  } while (0)
#else
#define P(S)                                                                   \
  do {                                                                         \
    while (WaitForSingleObject(S, INFINITE))                                   \
      ;                                                                        \
  } while (0)
#define V(S)                                                                   \
  do {                                                                         \
    ReleaseSemaphore(S, 1, 0);                                                 \
  } while (0)
#endif

/// Implementation details.
//
// The very tricky part is that the signaler can only use
// semaphore-increment, and therefore *does not have a lock*.
//
// See this Microsoft Research paper on how to implement condition
// variables with only semaphores
//    http://research.microsoft.com/pubs/64242/implementingcvs.pdf
// It's veyr instructive, but we can't use its implementation because:
//    - The signaler acquires a mutex
//    - It keeps an explicit linked list of waiters
//
// But we can reuse much of the thinking.  If you've read the paper, then
// the twist we use is that instead of having an explicit list of waiters,
// we'll implicitly segregate waiters into groups by having them wait on
// different counter-based wakeup semaphores.  Also, we implicitly choose a
// "designated waker" as being the first waiter in its group.
//
// Secondly, we only support broadcast signalling.
//
//
// Description of the solution:
//
// A "potential waiter" is a thread that may want exclusive access to
// some shared client state, but needs to be woken up whenever a signal
// is generated.
//
// A potential waiter is in one of four states, as below.
// Only one potential waiter at a time can make a transition out of state A.
// Only one potential waiter at a time can make a transition out of state B.
//
//    A: Initial state.
//       Before acquire.
//       Actions:
//          acquire:  Move to state B.
//
//    B: Acquired.
//       Only one potential waiter can be in this state.
//       It can look at shared data to determine whether to wait or release.
//       Actions:
//          release:  Move to state A.
//          wait:  Become an active waiter (move to state C)
//             or  become a passive waiter (move to state D)
//
//    C: Active waiter.
//       This is the "Paul Revere" of its cohort, the first of its waiting
//       group. It is a waiter who needs to wait for the signal to come by. When
//       that happens, it will signal all current passive waiters to wake up.
//       Actions:
//          wake up:
//                Signal a wakeup for all passive waiters in my group,
//                   (This completes delivery of the signal)
//                Move to state A.
//
//    D: Passive waiter.
//       Actions:
//          wake up:  Move to state A.
//
// A "group of waiters" is the set of threads which transition from
// B->C or B->D between two events:
//    Between "init" and the complete delivery of the first signal, or
//    Between complete delivery of two successive signals, or
//    Between complete delivery of a signal and "reset".
//
// How does a thread know whether it should be the active or passive
// waiter?  The active waiter is always the *first* waiter in its group.
// All others are passive waiters.

struct acl_condvar_s {
  // The waiter_mutex is the mutex that protects transitions in the waiter
  // state machine (A -> B, B -> {C|D}, {C|D}->A),
  // and protects the num_waiters and entry_q variables in this structure.
#ifdef __linux__
  pthread_mutex_t waiter_mutex;
#else
  CRITICAL_SECTION waiter_mutex;
#endif

  acl_sem_t signal_sem; // sem_post this when receiving a signal.

  // Lowest bit of signal arrival count.
  // Effectively divides sequence of arriving signals into successive
  // groups.
  int entry_q;

  // We need to maintain *two* semaphores for passive waiters to account
  // for the fact that passive waiters may be scheduled by the OS in an
  // arbitrary order.  We want to keep the promise that a wakeup signal
  // will be delivered to *all* passive waiters in the same group
  // as the active waiter.
  //
  // So we steer each passive waiter into one of two queues: one that
  // has the same group as the active waiter, or a different group to
  // be awoken by the next active waiter to come along.
  //
  // This handles the following scenario (e.g. where entry_q starts out as 0):
  //    - After getting the async wakeup from the signal semaphore,
  //    the active waiter in group 0 raises the semaphore for all the passive
  //    waiters in group 0.
  //    - But then before the scheduler wakes them all up
  //    another group of waiters can start to accumulate, including a few
  //    passive waiters in group 1.
  //    - The passive waiters in group 0 should all have been signalled
  //    before any passive waiters in group 1 are signaled.
  //    (It's still indeterminate when the scheduler will wake them.)

  // Number of waiters in a group.
  unsigned num_waiters[2];

  // Semaphores on which passive waiters wait.
  // Let Q be the value of entry_q this waiter observed on the transition
  // out of state B (and into C or D).
  //
  // A passive waiter will wait on passive_sem[Q]
  // An active waiter will signal passive_sem[Q] num_waiters[Q] times.
  acl_sem_t passive_sem[2];

  // Set by the Active waiter.
  // Only the active waiter will wait for semaphore with timeout.
  // Once it times out, it will signal all passive waiters, and notify them
  // of timeout through this.
  unsigned timedout[2];
};

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif

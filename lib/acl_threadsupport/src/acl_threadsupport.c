// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// HALs for use when we emulate the kernels on (primarily x86).
//
// Kernels launch and complete right away, after spawing an enqueueing thread,
// which will terminate when all kernel threads have terminated
// Kernel fiber schaeduler is also run as a thread. And it will not terminate
// until the host terminates.
// Kernel threads will be run in sequence, possible switching between kernel
// threads at barriers and channel calls

// This should be included at the very top, for sched_setaffinity to work.
#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "acl_threadsupport/acl_threadsupport.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

////////////////
// Memory barriers
//
// A memory barrier ensures both the compiler and the CPU do not reorder
// loads or stores across the barrier.
//
// We're doing something very simple.  We're not doing C11 or C++11 atomics.

#ifdef _WIN32
#define ACL_MEMORY_BARRIER MemoryBarrier
#else // GCC
#define ACL_MEMORY_BARRIER __sync_synchronize
#endif
///////////

#ifdef __linux__
// Thread type used for OS threads. Preemptive but light weight

int acl_thread_create(acl_thread_t *newthread, void *attr,
                      void *(*start_routine)(void *), void *arg) {

  int result = pthread_create(newthread, attr, start_routine, arg);
  return result;
}

int acl_thread_yield(void) { return sched_yield(); }

int acl_thread_join(acl_thread_t *threadp) { return pthread_join(*threadp, 0); }

int acl_fiber_create(acl_fiber_t *fiberp, acl_fiber_t *masterp,
                     void (*func)(void), size_t fiber_stack_size, int argc,
                     int arg) {
  int result = 0;
  acl_fiber_t fiber = *fiberp;
  result = getcontext(fiber);
  if (result != 0)
    return result;
  fiber->uc_link = *masterp;
  makecontext(fiber, func, argc, arg);
  return 0;
}

int acl_init_fibers(acl_fiber_t *fiberp) {
  *fiberp = malloc(sizeof(ucontext_t));
  return getcontext(*fiberp);
}

int acl_fiber_delete(acl_fiber_t *fiberp) {
  fiberp = 0;
  return 0;
}

int acl_destroy_fibers(acl_fiber_t *fiberp) {
  free(*fiberp);
  fiberp = 0;
  return 0;
}

int acl_fiber_swapcontext(acl_fiber_t *currentp, const acl_fiber_t *nextp) {
  return swapcontext(*currentp, *nextp);
}

// Mutex type used to syncronize acl_threads, or rather preventing different
// threads from updating data structures in a way that would introduce data
// races. Note that it is not needed to protect fiber accesses since they are
// already completly serialized vs each other

int acl_mutex_init(acl_mutex_t *mutex, void *mutexattr) {
  return pthread_mutex_init(mutex, mutexattr);
}

int acl_mutex_lock(acl_mutex_t *mutex) { return pthread_mutex_lock(mutex); }

int acl_mutex_unlock(acl_mutex_t *mutex) { return pthread_mutex_unlock(mutex); }
int acl_mutex_destroy(acl_mutex_t *mutex) {
  return pthread_mutex_destroy(mutex);
}

int acl_tls_new(acl_tls_key_t *key) {
  return pthread_key_create(key, NULL) != 0;
}

int acl_tls_delete(acl_tls_key_t key) { return pthread_key_delete(key) != 0; }

void *acl_tls_get(acl_tls_key_t key) { return pthread_getspecific(key); }

int acl_tls_set(acl_tls_key_t key, const void *data) {
  return pthread_setspecific(key, data) != 0;
}

void acl_set_process_affinity_mask(unsigned long long process_affinity_mask) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  // While the Linux man page for CPU_SET(3) states that the cpu argument
  // is of type int, glibc changed this macro to use type size_t instead.
  // https://man7.org/linux/man-pages/man3/CPU_SET.3.html
  // https://sourceware.org/git/?p=glibc.git;a=commit;h=2b7e92df930b8ed1ace659bf6e0b8dff41d65bf0
  size_t cpu = 0;
  while (process_affinity_mask > 0) {
    if (process_affinity_mask % 2)
      CPU_SET(cpu, &mask);
    cpu++;
    process_affinity_mask >>= 1;
  }
  sched_setaffinity(0, sizeof(mask), &mask);
}

#else // Windows

int acl_thread_create(acl_thread_t *newthread, void *attr,
                      void *(*start_routine)(void *), void *arg) {
  *newthread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg,
                            DETACHED_PROCESS, NULL);
  attr = 0;
  if (*newthread != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_thread_yield(void) {
  YieldProcessor();
  return 0;
}

int acl_thread_join(acl_thread_t *threadp) {
  return WaitForMultipleObjects(1, threadp, TRUE, INFINITE);
}

int acl_fiber_getcontext(acl_fiber_t *fiberp) {
  fiberp = 0;
  return 0;
}

int acl_init_fibers(acl_fiber_t *fiberp) {
  *(fiberp) = ConvertThreadToFiber(NULL);
  if (*(fiberp) != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_fiber_create(acl_fiber_t *fiberp, acl_fiber_t *masterp,
                     void (*func)(void), size_t fiber_stack_size, int argc,
                     int arg) {
  assert(argc == 1);
  *(fiberp) = CreateFiber((SIZE_T)fiber_stack_size, (void (*)(void *))func,
                          (void *)arg);
  masterp = 0;
  if (*(fiberp) != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_fiber_delete(acl_fiber_t *fiberp) {
  DeleteFiber(*fiberp);
  return 0;
}
int acl_destroy_fibers(acl_fiber_t *fiberp) {
  BOOL res;
  fiberp = 0;
  res = ConvertFiberToThread();
  if (res != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_fiber_swapcontext(acl_fiber_t *currentp, const acl_fiber_t *nextp) {
  SwitchToFiber(*nextp);
  currentp = 0;
  return 0;
}

int acl_mutex_init(acl_mutex_t *mutex, void *mutexattr) {
  *mutex = CreateMutex(NULL, FALSE, NULL);
  mutexattr = 0;
  if (*(mutex) != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_mutex_lock(acl_mutex_t *mutex) {
  return WaitForSingleObject(*mutex, INFINITE);
}

int acl_mutex_unlock(acl_mutex_t *mutex) {
  BOOL res = ReleaseMutex(*mutex);
  if (res != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_mutex_destroy(acl_mutex_t *mutex) {
  BOOL res = CloseHandle(*mutex);
  if (res != 0) {
    return 0;
  } else {
    return GetLastError();
  }
}

int acl_tls_new(acl_tls_key_t *key) {
  *key = TlsAlloc();
  return (*key == TLS_OUT_OF_INDEXES);
}

int acl_tls_delete(acl_tls_key_t key) { return TlsFree(key) == 0; }

void *acl_tls_get(acl_tls_key_t key) { return TlsGetValue(key); }

int acl_tls_set(acl_tls_key_t key, const void *data) {
  return TlsSetValue(key, (LPVOID)data) == 0;
}

void acl_set_process_affinity_mask(unsigned long long process_affinity_mask) {
  SetProcessAffinityMask(GetCurrentProcess(), process_affinity_mask);
}

#endif

void acl_memory_barrier(void) { ACL_MEMORY_BARRIER(); }

////////////////
// Condition variables with async-safe broadcast

typedef struct acl_condvar_s *acl_condvar_t;

// For debugging purposes, this is a condition variable
// we dump on every transition.
static acl_condvar_t s_dump_condvar = 0;
void acl_set_dump_condvar(struct acl_condvar_s *C) { s_dump_condvar = C; }
static void l_dump(const char *str) {
  if (s_dump_condvar) {
    acl_dump_condvar(str, s_dump_condvar);
  }
}

void acl_reset_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;
  l_dump("Reset");

#ifdef _WIN32
#define DESTROY(S)                                                             \
  if (S) {                                                                     \
    BOOL ret = CloseHandle(S);                                                 \
    assert(ret);                                                               \
    S = 0;                                                                     \
  }
  DeleteCriticalSection(&(C->waiter_mutex));
  DESTROY(C->signal_sem);
  DESTROY(C->passive_sem[0]);
  DESTROY(C->passive_sem[1]);
#undef DESTROY
#else
  {
    int ret = 0;
    // Try to unlock then unlock. This is done just in case it is already
    // locked, because if the mutex is locked we can't destroy it. This may
    // happen in cases where an asssert causes an exit, etc.
    pthread_mutex_trylock(&(C->waiter_mutex));
    pthread_mutex_unlock(&(C->waiter_mutex));
    ret |= pthread_mutex_destroy(&(C->waiter_mutex));
    ret |= sem_destroy(&(C->signal_sem));
    ret |= sem_destroy(&(C->passive_sem[0]));
    ret |= sem_destroy(&(C->passive_sem[1]));
    assert(ret == 0);
  }
#endif

  C->num_waiters[0] = C->num_waiters[1] = 0;
  C->entry_q = 0;
}

// Initialize the mutex/critical section and condition variable.
void acl_init_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;

  // We don't automatically call acl_reset_condvar because we can't trust
  // initial values of members.
  memset(C, 0, sizeof(*C));

  {
#ifdef _WIN32
    InitializeCriticalSection(&(C->waiter_mutex));
    C->signal_sem = CreateSemaphore(0, 0, LONG_MAX, 0);
    C->passive_sem[0] = CreateSemaphore(0, 0, LONG_MAX, 0);
    C->passive_sem[1] = CreateSemaphore(0, 0, LONG_MAX, 0);
    assert(C->signal_sem && C->passive_sem[0] && C->passive_sem[1]);
#else
    int ret;
    // Use 0 for second argument to indicate these semaphores are not
    // shared between processes.
    // Third argument is initial value.
    ret = pthread_mutex_init(&(C->waiter_mutex), NULL) ||
          sem_init(&(C->signal_sem), 0, 0) ||
          sem_init(&(C->passive_sem[0]), 0, 0) ||
          sem_init(&(C->passive_sem[1]), 0, 0);
    if (ret != 0) {
      perror("Failed to initialize semaphores");
    }
    assert(ret == 0);
#endif
  }

  l_dump("Init");
}

// Acquire the waiter_mutex.
void acl_acquire_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;

  l_dump("Acquire begin");
#ifdef _WIN32
  EnterCriticalSection(&(C->waiter_mutex));
#else
  {
    int ret = pthread_mutex_lock(&(C->waiter_mutex));
    assert(ret == 0);
  }
#endif
  l_dump("Acquire end");
}

// Release waiter_mutex without becoming a waiter.
void acl_release_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;

  l_dump("Release begin");
#ifdef _WIN32
  LeaveCriticalSection(&(C->waiter_mutex));
#else
  {
    int ret = pthread_mutex_unlock(&(C->waiter_mutex));
    assert(ret == 0);
  }
#endif
  l_dump("Release end");
}

// Wait on the condition variable, then release the mutex.
// You must have acquired the mutex first. (Not checked.)
// If timeout_period is != 0, this call will timeout after specified time in
// seconds if signal is not received. Return 0 if signal was received, non-zero
// value otherwise.
int acl_timed_wait_condvar(struct acl_condvar_s *C, unsigned timeout_period) {
  int my_entry_q;
  unsigned int timed_out = 0;
  if (!C)
    return -1;

  l_dump("Wait begin");
  ACL_MEMORY_BARRIER();

  my_entry_q = C->entry_q;

  if (0 == C->num_waiters[my_entry_q]++) {
    // I'm the active waiter.
    l_dump("Active waiter ");
    acl_release_condvar(C); // Release waiter mutex

    if (timeout_period) {
      if (acl_sem_timedwait(&C->signal_sem, timeout_period)) {
        timed_out = 1;
      }
    } else {
      P(C->signal_sem); // Wait for async signal.
    }

    acl_acquire_condvar(C); // Acquire waiter mutex

    l_dump("Wake begin");
    // Now schedule wakeup of all passive waiters in my group.
    // Note that I'm at index 0, so pre-decrement before the test
    // against zero.
    while (--C->num_waiters[my_entry_q])
      V(C->passive_sem[my_entry_q]);

    // This completes the activities for this group.
    // Switch groups.
    C->entry_q = 1 - C->entry_q;

    C->timedout[my_entry_q] = timed_out;

    l_dump("Wake end");

  } else {
    // I'm a passive waiter.
    // If active waiter does not have a timeout_period, passive waiter will not
    // timeout too
    l_dump("Passive waiter ");
    acl_release_condvar(C); // Give up the waiter mutex

    // But wait for the wakeup from the active waiter in my group.
    P(C->passive_sem[my_entry_q]);

    acl_acquire_condvar(C); // Re-acquire the waiter mutex
    timed_out = C->timedout[my_entry_q];

    l_dump("Passive woken ");
  }

  // Still have the waiter mutex!
  return (int)timed_out;
}

// Wait on the condition variable, then release the mutex.
// You must have acquired the mutex first. (Not checked.)
// Same as acl_timed_wait_condvar except without timeout_period.
void acl_wait_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;
  while (acl_timed_wait_condvar(C, 0))
    ;
}

// Signal (wake up) *all* threads waiting on the condition variable.
// Rather, schedule a wakeup for later.
// This must be async-signal safe.
// We can't call system calls or wait on signals.
void acl_signal_condvar(struct acl_condvar_s *C) {
  if (!C)
    return;

  ACL_MEMORY_BARRIER();
  V(C->signal_sem);
}

void acl_dump_condvar(const char *str, struct acl_condvar_s *C) {
  printf("\ncondvar %p %s ", C, (str ? str : "(nil)"));
  if (C) {
#ifdef __linux__
    int sval;
#endif
    printf("{\n");
#ifdef __linux__
    sem_getvalue(&(C->signal_sem), &sval);
    printf("  signal_sem       %d\n", sval);
#endif
    printf("  entry_q:         %d\n", C->entry_q);
    printf("  num_waiters[0]:  %d\n", C->num_waiters[0]);
#ifdef __linux__
    sem_getvalue(&(C->passive_sem[0]), &sval);
    printf("  passive_sem[0]:  %d\n", sval);
#endif
    printf("  num_waiters[1]:  %d\n", C->num_waiters[0]);
#ifdef __linux__
    sem_getvalue(&(C->passive_sem[1]), &sval);
    printf("  passive_sem[1]:  %d\n", sval);
#endif
  }
  fflush(stdout);
}

// Initialize acl semaphore (acl_sem_t)
// Return 1 if successful, 0 otherwise
int acl_init_sem(acl_sem_t *sem) {
  int result;
  if (!sem)
    return 0;

  memset(sem, 0, sizeof(*sem));

  {
#ifdef _WIN32
    *sem = CreateSemaphore(0, 0, LONG_MAX, 0);
    result = (*sem != NULL);
#else
    int status;
    // Use 0 for second argument to indicate these semaphores are not
    // shared between processes.
    // Third argument is initial value.
    status = sem_init(sem, 0, 0);
    result = (0 == status);
#endif
  }

  return result;
}

// returns 0 if sucessfully waited until received proper signal. non zero return
// values depend on platform.
int acl_sem_wait(acl_sem_t *sem) {
#ifdef _WIN32
  return (int)WaitForSingleObject(*sem, INFINITE);
#else
  while (1) {
    if (sem_wait(sem) == 0)
      return 0;
    else if (errno == EINTR)
      continue; // restart the wait if interrupt received
    else
      return -1;
  }
#endif
}

// same as acl_sem_wait, but always returns immediately
// returns 0 if the proper signal was received without waiting. non-zero
// otherwise.
int acl_sem_trywait(acl_sem_t *sem) {
#ifdef _WIN32
  return (int)WaitForSingleObject(*sem, 0);
#else
  while (1) {
    if (sem_trywait(sem) == 0)
      return 0;
    else if (errno == EINTR)
      continue; // restart the wait if interrupt received
    else
      return -1;
  }
#endif
}

// Same as acl_sem_wait, but times out after timeout.
// Returns 0 if signal was received, and non-zero value otherwise.
int acl_sem_timedwait(acl_sem_t *sem, unsigned timeout) {
#ifdef _WIN32
  return WaitForSingleObject(*sem, timeout * 1000);
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
    return -1;
  } else {
    ts.tv_sec += timeout;
    while (1) {
      if (sem_timedwait(sem, &ts) == 0)
        return 0;
      else if (errno == EINTR)
        continue; // restart the wait if interrupt received
      else
        return -1;
    }
  }
#endif
}

// Return 1 if successful, 0 otherwise
int acl_sem_post(acl_sem_t *sem) {
#ifdef _WIN32
  return (ReleaseSemaphore(*sem, 1, 0) != 0);
#else
  return (sem_post(sem) == 0);
#endif
}

// Return 1 if successful, 0 otherwise
int acl_sem_destroy(acl_sem_t *sem) {
#ifdef _WIN32
  return (CloseHandle(*sem) != 0);
#else
  return (sem_destroy(sem) == 0);
#endif
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

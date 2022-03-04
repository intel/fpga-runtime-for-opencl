// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "CppUTest/TestHarness.h"

#include "acl_test.h"
#include "acl_threadsupport/acl_threadsupport.h"
#include <stdio.h>

#define TESTSIZE 10
#define TRUE 1
#define FALSE 0

acl_fiber_t master, slave;
typedef struct acl_condvar_s *acl_condvar_t;

TEST_GROUP(threadsupport){void setup(){} void teardown(){
#ifdef _WIN32
    acl_destroy_fibers(&master);
#endif
}
}
;

TEST(threadsupport, mutex) {
  // Just basic sanity, actual functionality tested as part of the
  // other two tests below
  int res = 0;
  acl_mutex_t testmutex;
  res = acl_mutex_init(&testmutex, 0);
  CHECK_EQUAL(0, res);
  res = acl_mutex_lock(&testmutex);
  CHECK_EQUAL(0, res);
  res = acl_mutex_unlock(&testmutex);
  CHECK_EQUAL(0, res);
  res = acl_mutex_destroy(&testmutex);
  CHECK_EQUAL(0, res);
}

#define CLIENT_STATE 0
#define SERVER_STATE 1
#define END_STATE 2

static volatile int fibertest_state = CLIENT_STATE;
static volatile int fibervar = 0;

void bouncefiber(void) {
  while (fibertest_state != END_STATE) {
    fibervar += 19;
    acl_fiber_swapcontext(&slave, &master);
  }
  acl_fiber_swapcontext(&slave, &master);
}

TEST(threadsupport, fibers) {
  // Setup
  int res = 0;
#ifdef __linux__
  ucontext_t master_impl, slave_impl;
  master = &master_impl;
  slave = &slave_impl;

  char mystack[4096];
  slave->uc_stack.ss_sp = mystack;
  slave->uc_stack.ss_size = sizeof(mystack);
#endif
  res = acl_init_fibers(&master);
  CHECK_EQUAL(0, res);

  res = acl_fiber_create(&slave, &master, (void (*)(void))bouncefiber, 4096, 1,
                         0);
  CHECK_EQUAL(0, res);

  fibervar = 99;

  res = acl_fiber_swapcontext(&master, &slave);
  CHECK_EQUAL(0, res);
  CHECK_EQUAL(99 + 19, fibervar);

  res = acl_fiber_swapcontext(&master, &slave);
  CHECK_EQUAL(0, res);
  CHECK_EQUAL(99 + 19 + 19, fibervar);
  fibertest_state = END_STATE;
  res = acl_fiber_swapcontext(&master, &slave);
  CHECK_EQUAL(0, res);

  res = acl_fiber_delete(&slave);
  CHECK_EQUAL(0, res);
  CHECK_EQUAL(99 + 19 + 19, fibervar);
}

acl_mutex_t mymutex;
volatile int threadtest_state = CLIENT_STATE;
volatile int threadvar = 0;

void *test_thread(void *) {
  while (1) {
    while (true) {
      acl_mutex_lock(&mymutex);
      const bool is_server_state = (threadtest_state == SERVER_STATE);
      acl_mutex_unlock(&mymutex);
      if (!is_server_state)
        break;
      acl_thread_yield();
    }
    if (threadtest_state == END_STATE) {
      return 0;
    }
    // CLIENT_STATE
    acl_mutex_lock(&mymutex);
    threadtest_state = SERVER_STATE;
    threadvar += 13;
    acl_mutex_unlock(&mymutex);
  }
}

TEST(threadsupport, threads) {
  // Setup
  int res = 0;
  acl_thread_t thread;
  res = acl_mutex_init(&mymutex, 0);
  CHECK_EQUAL(0, res);
  res = acl_mutex_lock(&mymutex);
  CHECK_EQUAL(0, res);
  threadvar = 7;
  threadtest_state = CLIENT_STATE;
  res = acl_thread_create(&thread, 0, test_thread, 0);
  CHECK_EQUAL(0, res);

  CHECK_EQUAL(7, threadvar); // fiber still waiting for lock
  res = acl_mutex_unlock(&mymutex);
  CHECK_EQUAL(0, res);

  while (true) {
    acl_mutex_lock(&mymutex);
    const bool is_server_state = (threadtest_state == SERVER_STATE);
    acl_mutex_unlock(&mymutex);
    if (is_server_state)
      break;
    acl_thread_yield();
  }
  res = acl_mutex_lock(&mymutex);
  CHECK_EQUAL(0, res);
  CHECK_EQUAL(threadvar, 7 + 13);
  threadtest_state = CLIENT_STATE;
  res = acl_mutex_unlock(&mymutex);
  CHECK_EQUAL(0, res);
  while (true) {
    acl_mutex_lock(&mymutex);
    const bool is_server_state = (threadtest_state == SERVER_STATE);
    acl_mutex_unlock(&mymutex);
    if (is_server_state)
      break;
    acl_thread_yield();
  }
  res = acl_mutex_lock(&mymutex);
  CHECK_EQUAL(0, res);
  threadtest_state = END_STATE;
  res = acl_mutex_unlock(&mymutex);
  CHECK_EQUAL(0, res);

  res = acl_thread_join(&thread);
  CHECK_EQUAL(0, res);
  CHECK_EQUAL(7 + 13 + 13, threadvar);
}

acl_sem_t sem;

void *test_sem_thread(void *arg) {
  // signal here:
  int res = 0;
  printf("posting semaphore\n");
  res = acl_sem_post(&sem);
  CHECK_EQUAL(1, res);
  return NULL;
}

TEST(threadsupport, semaphore) {
  int res = 0;
  acl_thread_t thread;

  res = acl_init_sem(&sem);
  CHECK_EQUAL(1, res);
  res = acl_thread_create(&thread, 0, test_sem_thread, 0);
  CHECK_EQUAL(0, res);

  // wait here for test_sem_thread
  printf("\nwaiting for semaphore\n");
  res = acl_sem_wait(&sem);
  CHECK_EQUAL(0, res);
  printf("received semaphore\n");
  res = acl_sem_destroy(&sem);
  CHECK_EQUAL(1, res);

  // join the thread for preventing race condition
  acl_thread_join(&thread);
}

TEST(threadsupport, nonblocking_semaphore) {
  int res = 0;
  acl_sem_t nb_sem;

  res = acl_init_sem(&nb_sem);
  CHECK_EQUAL(1, res);

  // try wait on unsignaled semaphore
  res = acl_sem_trywait(&nb_sem);
  CHECK(0 != res);

  res = acl_sem_post(&nb_sem);
  CHECK_EQUAL(1, res);

  // try wait on signaled semaphore
  res = acl_sem_trywait(&nb_sem);
  CHECK_EQUAL(0, res);

  res = acl_sem_destroy(&nb_sem);
  CHECK_EQUAL(1, res);
}

TEST(threadsupport, timed_semaphore) {
  int res = 0;
  acl_sem_t nb_sem;

  res = acl_init_sem(&nb_sem);
  CHECK_EQUAL(1, res);

  // try wait on unsignaled semaphore
  res = acl_sem_timedwait(&nb_sem, 1);
  CHECK(0 != res);

  res = acl_sem_post(&nb_sem);
  CHECK_EQUAL(1, res);

  // try wait on signaled semaphore
  res = acl_sem_timedwait(&nb_sem, 1);
  CHECK_EQUAL(0, res);

  res = acl_sem_destroy(&nb_sem);
  CHECK_EQUAL(1, res);
}

TEST(threadsupport, tls) {
  // Setup
  int res = 0;
  acl_tls_key_t key;
  void *data = 0;

  res = acl_tls_new(&key);
  CHECK_EQUAL(0, res);
  res = acl_tls_set(key, (void *)7);
  CHECK_EQUAL(0, res);
  data = acl_tls_get(key);
  CHECK_EQUAL(7, (size_t)data);

  res = acl_tls_delete(key);
  CHECK_EQUAL(0, res);
}

TEST(threadsupport, cpu_affinity) {
  acl_set_process_affinity_mask(1);
  {
#ifdef _WIN32
    unsigned long long user_mask, system_mask;
    GetProcessAffinityMask(GetCurrentProcess(), &user_mask, &system_mask);
    CHECK_EQUAL(1, user_mask);
    CHECK(0 != system_mask);
#else
    cpu_set_t mask;
    int cpu_count = 0;
    CPU_ZERO(&mask);
    sched_getaffinity(0, sizeof(mask), &mask);
    cpu_count = CPU_COUNT(&mask);
    CHECK_EQUAL(1, cpu_count);
#endif
  }
}
// Critical section and condition variables.

#if 0
static void l_err_fn(const char* errinfo, const void*private_info, size_t cb, void* user_data)
{
   private_info = private_info;
   cb = cb;
   if ( user_data ) *(int*)user_data = 1;
   if ( errinfo ) printf("Condvar error: %s\n", errinfo );
}
#endif

TEST_GROUP(condvar){void setup(){} void teardown(){}};

TEST(condvar, link) {
  // Make sure all functions are present, and do sane things with NULL.
  acl_reset_condvar(0);
  acl_init_condvar(0);
  acl_acquire_condvar(0);
  acl_wait_condvar(0);
  acl_timed_wait_condvar(0, 0);
  acl_signal_condvar(0);
  acl_release_condvar(0);
}

TEST(condvar, reset_init) {
  // Check initialization and reset
  struct acl_condvar_s cc;

  cc.num_waiters[0] = 42;
  cc.num_waiters[1] = 42;
  cc.entry_q = 1;

  acl_init_condvar(&cc);
  CHECK_EQUAL(0, cc.num_waiters[0]);
  CHECK_EQUAL(0, cc.num_waiters[1]);
  CHECK_EQUAL(0, cc.entry_q);

  cc.num_waiters[0] = 42;
  cc.num_waiters[1] = 42;
  cc.entry_q = 1;

  acl_reset_condvar(&cc);
  CHECK_EQUAL(0, cc.num_waiters[0]);
  CHECK_EQUAL(0, cc.num_waiters[1]);
  CHECK_EQUAL(0, cc.entry_q);
}

TEST(condvar, acquire_release) {
  // Sane behaviour for acquire release. Does not test threadedness.
  struct acl_condvar_s cc;

  acl_init_condvar(&cc);
  acl_acquire_condvar(&cc);
  acl_release_condvar(&cc);
  acl_reset_condvar(&cc);
}

// A bit of a stress test. Make sure we don't run out of memory or
// something stupid like that.
// Also, should not be too slow.
TEST(condvar, repeat_acquire_release) {
  struct acl_condvar_s cc;
  const unsigned COUNT = 10000000;

  printf("%d condvar acquire/release", COUNT);
  acl_init_condvar(&cc);
  for (unsigned i = 0; i < COUNT; i++) {
    if (0 == i % (COUNT / 10)) {
      printf("-");
      fflush(stdout);
    }
    acl_acquire_condvar(&cc);
    acl_release_condvar(&cc);
  }
  acl_reset_condvar(&cc);
}

const unsigned WAIT_LIMIT = 1000;

// These two varaibles are used to count how many times
// the waiter has waited and how many times the signaller has
// signaled.
//
// The waiter will wait WAIT_LIMIT times.
// The signalier will signal WAIT_LIMIT times.
//
static unsigned wait_count = 0;
static unsigned signal_count = 0;

// Wait exactly WAIT_LIMIT times.
void *waiter_func(void *ccv) {
  acl_condvar_t cc = (acl_condvar_t)ccv;
  acl_acquire_condvar(cc);
  printf("waiter ");
  fflush(stdout);
  acl_release_condvar(cc);

  unsigned long long i;

  for (i = 0; wait_count < WAIT_LIMIT; i++) {
    acl_acquire_condvar(cc);

    if (0 == i % (WAIT_LIMIT / 10)) {
      printf("w");
      fflush(stdout);
    }

    wait_count++;
    acl_wait_condvar(cc);

    acl_release_condvar(cc);
  }

  acl_acquire_condvar(cc);
  printf("waiter finished after %llu iters\n", i);
  fflush(stdout);
  acl_release_condvar(cc);

  return 0;
}

// Signal exactly WAIT_LIMIT times.
void *signaler_func(void *ccv) {
  acl_condvar_t cc = (acl_condvar_t)ccv;
  printf("signaler ");
  fflush(stdout);

  unsigned long long i;

  for (i = 0; signal_count < WAIT_LIMIT; i++) {

    if (0 == i % (WAIT_LIMIT / 10)) {
      printf("s");
      fflush(stdout);
    }

    // Signal one more time.
    signal_count++;
    acl_signal_condvar(cc);

    acl_thread_yield();
  }

  printf("\nsignaler finished after %llu iters\n", i);
  fflush(stdout);

  return 0;
}

static acl_condvar_s *s_emerg_condvar;

// A bit of a stress test for waiting and signaling
TEST(condvar, repeat_acquire_wait_signal_release) {
  acl_thread_t waiter;
  acl_thread_t signaler;
  wait_count = 0;
  signal_count = 0;

  struct acl_condvar_s cc;
  s_emerg_condvar = &cc;

  // For debugging:
  //   acl_set_dump_condvar(s_emerg_condvar);

  printf("condvar waiting test (%d iters):", WAIT_LIMIT);
  acl_init_condvar(&cc);

  CHECK_EQUAL(0, acl_thread_create(&waiter, 0, waiter_func, (void *)&cc));
  CHECK_EQUAL(0, acl_thread_create(&signaler, 0, signaler_func, (void *)&cc));

  CHECK_EQUAL(0, acl_thread_join(&waiter));
  CHECK_EQUAL(0, acl_thread_join(&signaler));

  CHECK_EQUAL(WAIT_LIMIT, wait_count);
  CHECK_EQUAL(WAIT_LIMIT, signal_count);

  acl_reset_condvar(&cc);
  acl_set_dump_condvar(0);
}

TEST(condvar, wait_signal_with_timeout) {
  struct acl_condvar_s cc;
  acl_init_condvar(&cc);

  acl_acquire_condvar(&cc);
  CHECK_EQUAL(1, acl_timed_wait_condvar(&cc, 1));
  CHECK_EQUAL(1, cc.timedout[0]);
  acl_release_condvar(&cc);

  acl_acquire_condvar(&cc);
  acl_signal_condvar(&cc);
  acl_release_condvar(&cc);

  acl_acquire_condvar(&cc);
  CHECK_EQUAL(0, acl_timed_wait_condvar(&cc, 1));
  CHECK_EQUAL(0, cc.timedout[1]);
  acl_release_condvar(&cc);

  acl_reset_condvar(&cc);
}

// For debugging
extern "C" void emerg(void) { acl_dump_condvar("emergency", s_emerg_condvar); }

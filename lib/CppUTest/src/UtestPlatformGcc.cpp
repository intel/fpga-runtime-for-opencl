/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
 * Copyright (c) 2015, 2021, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE EARLIER MENTIONED AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __GNUC__ // this file is only used with gcc

#include <stdlib.h>
#include "CppUTest/TestHarness.h"
#undef malloc
#undef free
#undef calloc
#undef realloc

#include "CppUTest/TestRegistry.h"
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>

#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 30)
// From gettid(2): The gettid() system call first appeared on Linux in
// kernel 2.4.11. Library support was added in glibc 2.30. (Earlier
// glibc versions did not provide a wrapper for this system call,
// necessitating the use of syscall(2).)
static inline pid_t gettid(void) { return (pid_t)syscall(__NR_gettid); }
#endif

#include "CppUTest/PlatformSpecificFunctions.h"

static __thread jmp_buf test_exit_jmp_buf[10];
static __thread int jmp_buf_index = 0;

bool Utest::executePlatformSpecificSetup()
{
   if (0 == setjmp(test_exit_jmp_buf[jmp_buf_index])) {
      jmp_buf_index++;
      setup();
      jmp_buf_index--;
      return true;
   }
   return false;
}

void Utest::executePlatformSpecificTestBody()
{
   if (0 == setjmp(test_exit_jmp_buf[jmp_buf_index])) {
      jmp_buf_index++;
      testBody();
      jmp_buf_index--;
   }
}

void Utest::executePlatformSpecificTeardown()
{
   if (0 == setjmp(test_exit_jmp_buf[jmp_buf_index])) {
      jmp_buf_index++;
      teardown();
      jmp_buf_index--;
   }
}

void Utest::executePlatformSpecificRunOneTest(TestPlugin* plugin, TestResult& result)
{
    if (0 == setjmp(test_exit_jmp_buf[jmp_buf_index])) {
       jmp_buf_index++;
       runOneTest(plugin, result);
       jmp_buf_index--;
    }
}

void Utest::executePlatformSpecificExitCurrentTest()
{
   jmp_buf_index--;
   longjmp(test_exit_jmp_buf[jmp_buf_index], 1);
}


///////////// Thread pool

static pthread_barrier_t l_threadPoolBarrier;
static Utest*            l_currentTest = NULL;
static int               l_numThreads = 0;
static __thread int      l_currentThreadNum;

static void* l_PlatformSpecificThreadEntry(void* arg)
{
    int ret;
    l_currentThreadNum = (int)(intptr_t)arg;

    while (true) {
        ret = pthread_barrier_wait(&l_threadPoolBarrier);
        assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);

        l_currentTest->runInThreadOnCopy();

        ret = pthread_barrier_wait(&l_threadPoolBarrier);
        assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);
    }

    return NULL;
}

void PlatformSpecificStartThreadPool(int numThreads)
{
    l_numThreads = numThreads;

    if (l_numThreads > 1) {
        int ret;
        ret = pthread_barrier_init(&l_threadPoolBarrier, 0, l_numThreads+1);
        assert(ret == 0);

        for (int i = 0; i < l_numThreads; ++i) {
            pthread_t thread;
            ret = pthread_create(&thread, 0, l_PlatformSpecificThreadEntry,
                    (void*)(intptr_t)i);
            assert(ret == 0);
        }
    }
}

int PlatformSpecificGetThreadPoolSize()
{
    return l_numThreads;
}

int PlatformSpecificGetThreadId()
{
    return (int)gettid();
}

int PlatformSpecificGetThreadNum()
{
    return l_numThreads > 1 ? l_currentThreadNum : 0;
}

void Utest::executePlatformSpecificRunInThreads()
{
    assert(l_numThreads >= 1);

    if (l_numThreads == 1) {
        runInThreadOnCopy();
    }
    else {
        l_currentTest = this;

        int ret;
        ret = pthread_barrier_wait(&l_threadPoolBarrier);
        assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);

        ret = pthread_barrier_wait(&l_threadPoolBarrier);
        assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);
    }
}

///////////// Mutex

struct Mutex::Impl {
    pthread_mutex_t pthreadMutex;
};

Mutex::Mutex()
{
    impl_ = new Impl;

    int ret = pthread_mutex_init(&(impl_->pthreadMutex), 0);
    assert(ret == 0);
}

Mutex::~Mutex()
{
    int ret = pthread_mutex_destroy(&(impl_->pthreadMutex));
    assert(ret == 0);
    delete impl_;
}

void Mutex::lock()
{
    int ret = pthread_mutex_lock(&(impl_->pthreadMutex));
    assert(ret == 0);
}

void Mutex::unlock()
{
    int ret = pthread_mutex_unlock(&(impl_->pthreadMutex));
    assert(ret == 0);
}

///////////// AtomicUInt

unsigned int AtomicUInt::fetchAndAdd(unsigned int add_value)
{
    return __sync_fetch_and_add(&value_, add_value);
}

///////////// ThreadBarrier

struct ThreadBarrier::Impl
{
    pthread_barrier_t pthreadBarrier;
    int numThreads;
};

ThreadBarrier::ThreadBarrier()
{
    impl_ = new Impl;
    impl_->numThreads = 0;
}

ThreadBarrier::ThreadBarrier(int numThreads)
{
    impl_ = new Impl;
    impl_->numThreads = numThreads;

    if (numThreads > 1) {
        int ret = pthread_barrier_init(&(impl_->pthreadBarrier), NULL, numThreads);
        assert(ret == 0);
    }
}

ThreadBarrier::~ThreadBarrier()
{
    delete impl_;
}

int ThreadBarrier::numThreads()
{
    return impl_->numThreads;
}

void ThreadBarrier::setNumThreads(int numThreads) {
    if (impl_->numThreads != numThreads) {
        if (impl_->numThreads > 1) {
            int ret = pthread_barrier_destroy(&(impl_->pthreadBarrier));
            assert(ret == 0);
        }
        if (numThreads > 1) {
            int ret = pthread_barrier_init(&(impl_->pthreadBarrier), NULL, numThreads);
            assert(ret == 0);
        }
        impl_->numThreads = numThreads;
    }
}

void ThreadBarrier::wait()
{
    assert(impl_->numThreads > 0);

    if (impl_->numThreads > 1) {
        int ret = pthread_barrier_wait(&(impl_->pthreadBarrier));
        assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);
    }
}

///////////// Time in millis

static long TimeInMillisImplementation()
{
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	return (tv.tv_sec * 1000) + (long)((double)tv.tv_usec * 0.001);
}

static long (*timeInMillisFp) () = TimeInMillisImplementation;

long GetPlatformSpecificTimeInMillis()
{
	return timeInMillisFp();
}

void SetPlatformSpecificTimeInMillisMethod(long (*platformSpecific) ())
{
	timeInMillisFp = (platformSpecific == 0) ? TimeInMillisImplementation : platformSpecific;
}

///////////// Time in String

static const char* TimeStringImplementation()
{
	time_t tm = time(NULL);
	return ctime(&tm);
}

static const char* (*timeStringFp) () = TimeStringImplementation;

const char* GetPlatformSpecificTimeString()
{
	return timeStringFp();
}

void SetPlatformSpecificTimeStringMethod(const char* (*platformMethod) ())
{
	timeStringFp = (platformMethod == 0) ? TimeStringImplementation : platformMethod;
}

int PlatformSpecificAtoI(const char*str)
{
   return atoi(str);
}

size_t PlatformSpecificStrLen(const char* str)
{
   return strlen(str);
}

char* PlatformSpecificStrCat(char* s1, const char* s2)
{
   return strcat(s1, s2);
}

char* PlatformSpecificStrCpy(char* s1, const char* s2)
{
   return strcpy(s1, s2);
}

char* PlatformSpecificStrNCpy(char* s1, const char* s2, size_t size)
{
   return strncpy(s1, s2, size);
}

int PlatformSpecificStrCmp(const char* s1, const char* s2)
{
   return strcmp(s1, s2);
}

int PlatformSpecificStrNCmp(const char* s1, const char* s2, size_t size)
{
   return strncmp(s1, s2, size);
}
char* PlatformSpecificStrStr(const char* s1, const char* s2)
{
   return (char*) strstr(s1, s2);
}

int PlatformSpecificVSNprintf(char *str, unsigned int size, const char* format, va_list args)
{
   return vsnprintf( str, size, format, args);
}

PlatformSpecificFile PlatformSpecificFOpen(const char* filename, const char* flag)
{
   return fopen(filename, flag);
}

void PlatformSpecificFPuts(const char* str, PlatformSpecificFile file)
{
   fputs(str, (FILE*)file);
}

void PlatformSpecificFClose(PlatformSpecificFile file)
{
   fclose((FILE*)file);
}

void PlatformSpecificFlush()
{
  fflush(stdout);
}

int PlatformSpecificPutchar(int c)
{
  return putchar(c);
}

void* PlatformSpecificMalloc(size_t size)
{
   return malloc(size);
}

void* PlatformSpecificRealloc (void* memory, size_t size)
{
   return realloc(memory, size);
}

void PlatformSpecificFree(void* memory)
{
   free(memory);
}

void* PlatformSpecificMemCpy(void* s1, const void* s2, size_t size)
{
   return memcpy(s1, s2, size);
}

int PlatformSpecificAtExit(void (*func) ())
{
   return atexit(func);
}

double PlatformSpecificFabs(double d)
{
   return fabs(d);
}

#endif // __GNUC__

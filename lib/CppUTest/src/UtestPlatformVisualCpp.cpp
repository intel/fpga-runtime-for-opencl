/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
 * Copyright (c) 2015, 2016, Intel Corporation
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

#ifdef _MSC_VER // this file is only used with Visual Studio

#include <stdlib.h>
#include "CppUTest/TestHarness.h"
#undef malloc
#undef free
#undef calloc
#undef realloc

#include "CppUTest/TestRegistry.h"
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "CppUTest/PlatformSpecificFunctions.h"

#include <windows.h>
#include <mmsystem.h>


#if 0 //from GCC
static jmp_buf test_exit_jmp_buf[10];
static int jmp_buf_index = 0;

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

#endif

///////////// Thread pool

static CONDITION_VARIABLE     l_testReadyCondition, l_testFinishedCondition;
static CRITICAL_SECTION       l_criticalSection;
static int                    l_globalCounter = 0;
static int                    l_threadsFinished = 0;
static Utest*                 l_currentTest = NULL;
static int                    l_numThreads = 0;
static __declspec(thread) int l_currentThreadNum;

static DWORD WINAPI l_PlatformSpecificThreadEntry(LPVOID lpParam)
{
    int         localCounter = 0;
    Utest*      currentTest = NULL;

    l_currentThreadNum = (int)(intptr_t)lpParam;

    while (true) {
        EnterCriticalSection(&l_criticalSection);
        while (localCounter == l_globalCounter) {
            BOOL success = SleepConditionVariableCS(&l_testReadyCondition,
                    &l_criticalSection, INFINITE);
            assert(success);
        }
        localCounter      = l_globalCounter;
        currentTest       = l_currentTest;
        LeaveCriticalSection(&l_criticalSection);

        currentTest->runInThreadOnCopy();

        EnterCriticalSection(&l_criticalSection);
        l_threadsFinished++;
        WakeAllConditionVariable(&l_testFinishedCondition);
        LeaveCriticalSection(&l_criticalSection);
    }

    return 0;
}

void PlatformSpecificStartThreadPool(int numThreads)
{
    assert(l_numThreads == 0);
    l_numThreads = numThreads;

    if (l_numThreads > 1) {
        InitializeConditionVariable(&l_testReadyCondition);
        InitializeConditionVariable(&l_testFinishedCondition);
        InitializeCriticalSection(&l_criticalSection);

        for (int i = 0; i < l_numThreads; ++i) {
            HANDLE threadHandle = CreateThread(
                    NULL, 0, l_PlatformSpecificThreadEntry, (void*)(intptr_t)i,
                    0, NULL);
            assert(threadHandle != NULL);
        }
    }
}

int PlatformSpecificGetThreadPoolSize()
{
    return l_numThreads;
}

int PlatformSpecificGetThreadId()
{
    return (int)GetCurrentThreadId();
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
        EnterCriticalSection(&l_criticalSection);
        l_currentTest = this;
        l_threadsFinished = 0;
        l_globalCounter++;
        WakeAllConditionVariable(&l_testReadyCondition);
        while (l_threadsFinished < l_numThreads) {
            BOOL success = SleepConditionVariableCS(&l_testFinishedCondition,
                    &l_criticalSection, INFINITE);
            assert(success);
        }
        LeaveCriticalSection(&l_criticalSection);
    }
}

///////////// Mutex

struct Mutex::Impl {
    CRITICAL_SECTION criticalSection;
};

Mutex::Mutex()
{
    impl_ = new Impl;
    InitializeCriticalSection(&(impl_->criticalSection));
}

Mutex::~Mutex()
{
    DeleteCriticalSection(&(impl_->criticalSection));
    delete impl_;
}

void Mutex::lock()
{
    EnterCriticalSection(&(impl_->criticalSection));
}

void Mutex::unlock()
{
    LeaveCriticalSection(&(impl_->criticalSection));
}

///////////// AtomicUInt

unsigned int AtomicUInt::fetchAndAdd(unsigned int add_value)
{
    return InterlockedExchangeAdd(&value_, add_value);
}

///////////// ThreadBarrier

struct ThreadBarrier::Impl
{
    HANDLE enterSem;
    HANDLE exitSem;
    LONG volatile enterCount;
    LONG volatile exitCount;
    int numThreads;
};

ThreadBarrier::ThreadBarrier()
{
    impl_ = new Impl;
    impl_->enterSem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    impl_->exitSem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    impl_->enterCount = 0;
    impl_->exitCount = 0;
    impl_->numThreads = 0;
}

ThreadBarrier::ThreadBarrier(int numThreads)
{
    impl_ = new Impl;
    impl_->enterSem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    impl_->exitSem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    impl_->enterCount = 0;
    impl_->exitCount = 0;
    impl_->numThreads = numThreads;
}

ThreadBarrier::~ThreadBarrier()
{
    CloseHandle(impl_->enterSem);
    CloseHandle(impl_->exitSem);
    delete impl_;
}

int ThreadBarrier::numThreads()
{
    return impl_->numThreads;
}

void ThreadBarrier::setNumThreads(int numThreads) {
    impl_->numThreads = numThreads;
}

void ThreadBarrier::wait()
{
    assert(impl_->numThreads > 0);

    if (impl_->numThreads > 1) {
        // wait for all threads to enter the barrier
        if (InterlockedIncrement(&impl_->enterCount) < impl_->numThreads) {
            while (WaitForSingleObject(impl_->enterSem, INFINITE) != WAIT_OBJECT_0);
        } else {
            impl_->exitCount = 0;
            BOOL ret = ReleaseSemaphore(impl_->enterSem, impl_->numThreads-1, NULL);
            assert(ret);
        }

        // wait for all threads to exit the barrier
        if (InterlockedIncrement(&impl_->exitCount) < impl_->numThreads) {
            while (WaitForSingleObject(impl_->exitSem, INFINITE) != WAIT_OBJECT_0);
        } else {
            impl_->enterCount = 0;
            BOOL ret = ReleaseSemaphore(impl_->exitSem, impl_->numThreads-1, NULL);
            assert(ret);
        }
    }
}

///////////// Time in millis

static long TimeInMillisImplementation()
{
    return timeGetTime();
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
	return "Windows time needs work";
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


////// taken from gcc

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
	char* buf = 0;
	int sizeGuess = size;

	int result = _vsnprintf( str, size, format, args);
	str[size-1] = 0;
	while (result == -1)
	{
		if (buf != 0)
			free(buf);
		sizeGuess += 10;
		buf = (char*)malloc(sizeGuess);
		result = _vsnprintf( buf, sizeGuess, format, args);
	}
	
	if (buf != 0)
		free(buf);
	return result;

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





/////// clean up the rest

#if 0

void TestRegistry::platformSpecificRunOneTest(Utest* test, TestResult& result)
{
    try {
        runOneTest(test, result) ;
    }
    catch (int) {
        //exiting test early
    }

}

void Utest::executePlatformSpecificTestBody()
{
	testBody();
}

void PlatformSpecificExitCurrentTestImpl()
{
    throw(1);
}

#endif

int PlatformSpecificVSNprintf(char *str, unsigned int size, const char* format, void* args)
{
   return _vsnprintf( str, size, format, (va_list) args);
}


//platform specific test running stuff
#if 1
#include <setjmp.h>

static TLS jmp_buf test_exit_jmp_buf[10];
static TLS int jmp_buf_index = 0;

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

/*
void PlatformSpecificExitCurrentTestImpl()
{
   jmp_buf_index--;
   longjmp(test_exit_jmp_buf[jmp_buf_index], 1);
}
*/
#endif

//static jmp_buf test_exit_jmp_buf[10];
//static int jmp_buf_index = 0;


#if 0
bool Utest::executePlatformSpecificSetup()
{
	try {
      setup();
	}
	catch (int) {
		return false;
	}
    return true;
}

void Utest::executePlatformSpecificTestBody()
{
	try {
      testBody();
	}
	catch (int) {
	}

}

void Utest::executePlatformSpecificTeardown()
{
	try {
      teardown();
	}
	catch (int) {
	}

}

void PlatformSpecificExitCurrentTestImpl()
{
	throw(1);
}


void (*PlatformSpecificExitCurrentTest)() = PlatformSpecificExitCurrentTestImpl;

void FakePlatformSpecificExitCurrentTest()
{
}

#endif

#endif // _MSC_VER

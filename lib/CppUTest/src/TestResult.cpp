/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
 * Copyright (c) 2015, Intel Corporation
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

#include "CppUTest/TestHarness.h"
#include "CppUTest/TestResult.h"
#include "CppUTest/Failure.h"
#include "CppUTest/TestOutput.h"
#include "CppUTest/PlatformSpecificFunctions.h"

TestResult::TestResult(TestOutput& p) :
	output(p), testCount(0), runCount(0), checkCount(0), failureCount(0),
			filteredOutCount(0), ignoredCount(0), totalExecutionTime(0),
			timeStarted(0), currentTestTimeStarted(0),
			currentTestTotalExecutionTime(0), currentGroupTimeStarted(0),
			currentGroupTotalExecutionTime(0)
{
}

void TestResult::setProgressIndicator(const char* indicator)
{
	ScopedLock lock(mutex);
	output.setProgressIndicator(indicator);
}

TestResult::~TestResult()
{
}

void TestResult::currentGroupStarted(Utest* test)
{
	ScopedLock lock(mutex);
	output.printCurrentGroupStarted(*test);
	currentGroupTimeStarted = GetPlatformSpecificTimeInMillis();
}

void TestResult::currentGroupEnded(Utest* test)
{
	ScopedLock lock(mutex);
	currentGroupTotalExecutionTime = GetPlatformSpecificTimeInMillis()
			- currentGroupTimeStarted;
	output.printCurrentGroupEnded(*this);
}

void TestResult::currentTestStarted(Utest* test)
{
	ScopedLock lock(mutex);
	output.printCurrentTestStarted(*test);
	currentTestTimeStarted = GetPlatformSpecificTimeInMillis();
}

void TestResult::print(const char* text)
{
	ScopedLock lock(mutex);
	output.print(text);
}

void TestResult::currentTestEnded(Utest* test)
{
	ScopedLock lock(mutex);
	currentTestTotalExecutionTime = GetPlatformSpecificTimeInMillis()
			- currentTestTimeStarted;
	output.printCurrentTestEnded(*this);

}

void TestResult::addFailure(const Failure& failure)
{
	ScopedLock lock(mutex);
	output.print(failure);
	failureCount++;
}

void TestResult::countTest()
{
	ScopedLock lock(mutex);
	testCount++;
}

void TestResult::countRun()
{
	ScopedLock lock(mutex);
	runCount++;
}

void TestResult::countCheck()
{
	ScopedLock lock(mutex);
	checkCount++;
}

void TestResult::countFilteredOut()
{
	ScopedLock lock(mutex);
	filteredOutCount++;
}

void TestResult::countIgnored()
{
	ScopedLock lock(mutex);
	ignoredCount++;
}

void TestResult::testsStarted()
{
	ScopedLock lock(mutex);
	timeStarted = GetPlatformSpecificTimeInMillis();
	output.printTestsStarted();
}

void TestResult::testsEnded()
{
	ScopedLock lock(mutex);
	long timeEnded = GetPlatformSpecificTimeInMillis();
	totalExecutionTime = timeEnded - timeStarted;
	output.printTestsEnded(*this);
}

long TestResult::getTotalExecutionTime() const
{
	return totalExecutionTime;
}

void TestResult::setTotalExecutionTime(long exTime)
{
	ScopedLock lock(mutex);
	totalExecutionTime = exTime;
}

long TestResult::getCurrentTestTotalExecutionTime() const
{
	return currentTestTotalExecutionTime;
}

long TestResult::getCurrentGroupTotalExecutionTime() const
{
	return currentGroupTotalExecutionTime;
}


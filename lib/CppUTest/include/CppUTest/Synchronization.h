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

#ifndef D_Synchronization_h
#define D_Synchronization_h

class AtomicUInt
{
public:
	explicit AtomicUInt(unsigned int initialValue = 0) { value_ = initialValue; }
	void unsafeReset(unsigned int resetValue = 0) { value_ = resetValue; }
	unsigned int fetchAndAdd(unsigned int add_value = 1);
	operator unsigned int() { return value_; }

private:
	volatile unsigned int value_;
};

class ThreadBarrier
{
public:
	ThreadBarrier();
	explicit ThreadBarrier(int numThreads);
	~ThreadBarrier();

	int numThreads();
	void setNumThreads(int numThreads);
	void wait();

private:
	// non-copyable
	ThreadBarrier(const ThreadBarrier&);
	const ThreadBarrier& operator=(const ThreadBarrier&);

	struct Impl;
	Impl* impl_;
};

class Mutex
{
public:
	Mutex();
	~Mutex();

	void lock();
	void unlock();

private:
	// non-copyable
	Mutex(const Mutex&);
	const Mutex& operator=(const Mutex&);

	struct Impl;
	Impl* impl_;
};

class ScopedLock
{
public:
	ScopedLock(Mutex& mutex) : mutex_(mutex)
	{
		mutex_.lock();
	}

	~ScopedLock()
	{
		mutex_.unlock();
	}

private:
	// non-copyable
	ScopedLock(const ScopedLock&);
	const ScopedLock& operator=(const ScopedLock&);

	Mutex& mutex_;
};

#endif

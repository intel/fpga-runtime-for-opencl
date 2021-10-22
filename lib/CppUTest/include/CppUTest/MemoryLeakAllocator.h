/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
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

#ifndef D_MemoryLeakAllocator_h
#define D_MemoryLeakAllocator_h

class MemoryLeakAllocator
{
public:
	virtual char* alloc_memory(size_t size)=0;
	virtual void free_memory(char* memory)=0;

	virtual const char* name()=0;
	virtual const char* alloc_name()=0;
	virtual const char* free_name()=0;

	virtual int isOfEqualType(MemoryLeakAllocator* allocator);
	virtual ~MemoryLeakAllocator()
	{
	}
	;

	static void setCurrentNewAllocator(MemoryLeakAllocator* allocator);
	static MemoryLeakAllocator* getCurrentNewAllocator();
	static void setCurrentNewAllocatorToDefault();

	static void setCurrentNewArrayAllocator(MemoryLeakAllocator* allocator);
	static MemoryLeakAllocator* getCurrentNewArrayAllocator();
	static void setCurrentNewArrayAllocatorToDefault();

	static void setCurrentMallocAllocator(MemoryLeakAllocator* allocator);
	static MemoryLeakAllocator* getCurrentMallocAllocator();
	static void setCurrentMallocAllocatorToDefault();

private:
	static MemoryLeakAllocator* currentNewAllocator;
	static MemoryLeakAllocator* currentNewArrayAllocator;
	static MemoryLeakAllocator* currentMallocAllocator;
};

class StandardMallocAllocator: public MemoryLeakAllocator
{
public:
	char* alloc_memory(size_t size);
	void free_memory(char* memory);

	const char* name();
	const char* alloc_name();
	const char* free_name();

	static MemoryLeakAllocator* defaultAllocator();
};

class StandardNewAllocator: public MemoryLeakAllocator
{
public:
	char* alloc_memory(size_t size);
	void free_memory(char* memory);

	const char* name();
	const char* alloc_name();
	const char* free_name();

	static MemoryLeakAllocator* defaultAllocator();
};

class StandardNewArrayAllocator: public MemoryLeakAllocator
{
public:
	char* alloc_memory(size_t size);
	void free_memory(char* memory);

	const char* name();
	const char* alloc_name();
	const char* free_name();

	static MemoryLeakAllocator* defaultAllocator();
};

class NullUnknownAllocator: public MemoryLeakAllocator
{
public:
	char* alloc_memory(size_t size);
	void free_memory(char* memory);

	const char* name();
	const char* alloc_name();
	const char* free_name();

	static MemoryLeakAllocator* defaultAllocator();
};

#endif

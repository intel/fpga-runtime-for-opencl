// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "CppUTest/SimpleString.h"

#ifdef _WIN32
typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
#else
#include <stdint.h>
#endif

SimpleString StringFrom(uint32_t x);
SimpleString StringFrom(uint64_t x);
SimpleString StringFrom(intptr_t x);
SimpleString StringFrom(size_t x);

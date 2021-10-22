// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/SimpleString.h"
#include "CppUTest/TestHarness.h"

#include "acl_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char **argv) {
  return CommandLineTestRunner::RunAllTests(argc, argv);
}

#ifdef _WIN32
#define snprintf sprintf_s
#endif

SimpleString StringFrom(uint32_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%u", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#if ACL_TARGET_BIT == 32
SimpleString StringFrom(uint64_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%lu", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif
#ifdef _WIN64
SimpleString StringFrom(intptr_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%lu", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif
// If ACL_TARGET_BIT is 32, then size_t == cl_ulong == cl_uint, and we've
// already got a body for that.
#if ACL_TARGET_BIT > 32
SimpleString StringFrom(size_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%zd", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif

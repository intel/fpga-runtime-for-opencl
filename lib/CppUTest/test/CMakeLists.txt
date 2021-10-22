# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_executable(CppUTestTests
  AllTests.cpp
  CommandLineArgumentsTest.cpp
  CommandLineTestRunnerTest.cpp
  FailureTest.cpp
  JUnitOutputTest.cpp
  MemoryLeakAllocatorTest.cpp
  MemoryLeakDetectorTest.cpp
  MemoryLeakWarningTest.cpp
  NullTestTest.cpp
  PluginTest.cpp
  SetPluginTest.cpp
  SimpleStringTest.cpp
  TestHarness_cTest.cpp
  TestInstallerTest.cpp
  TestOutputTest.cpp
  TestRegistryTest.cpp
  TestResultTest.cpp
  UtestTest.cpp
  )
target_link_libraries(CppUTestTests PRIVATE CppUTest)
add_test(NAME CppUTestTests COMMAND CppUTestTests -v)

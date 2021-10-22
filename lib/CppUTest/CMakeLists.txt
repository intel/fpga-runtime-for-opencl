# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(CppUTest STATIC
  src/CommandLineArguments.cpp
  src/CommandLineTestRunner.cpp
  src/Failure.cpp
  src/JUnitTestOutput.cpp
  src/MemoryLeakAllocator.cpp
  src/MemoryLeakDetector.cpp
  src/MemoryLeakWarningPlugin.cpp
  src/SimpleString.cpp
  src/TestHarness_c.cpp
  src/TestOutput.cpp
  src/TestPlugin.cpp
  src/TestRegistry.cpp
  src/TestResult.cpp
  src/Utest.cpp
  src/UtestPlatformGcc.cpp
  src/UtestPlatformVisualCpp.cpp
  )
# These macros are used in CppUTest public headers
target_compile_definitions(CppUTest PUBLIC
  UT_NEW_MACROS_DISABLED
  UT_NEW_OVERRIDES_DISABLED
  UT_MALLOC_MACROS_DISABLED
  )
target_link_libraries(CppUTest PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(CppUTest PRIVATE winmm)
endif()
target_include_directories(CppUTest PUBLIC include)
set_property(TARGET CppUTest PROPERTY PUBLIC_HEADER
  include/CppUTest/CommandLineArguments.h
  include/CppUTest/CommandLineTestRunner.h
  include/CppUTest/Failure.h
  include/CppUTest/JUnitTestOutput.h
  include/CppUTest/MemoryLeakAllocator.h
  include/CppUTest/MemoryLeakDetector.h
  include/CppUTest/MemoryLeakWarningPlugin.h
  include/CppUTest/PlatformSpecificFunctions.h
  include/CppUTest/SimpleString.h
  include/CppUTest/Synchronization.h
  include/CppUTest/TestHarness.h
  include/CppUTest/TestHarness_c.h
  include/CppUTest/TestOutput.h
  include/CppUTest/TestPlugin.h
  include/CppUTest/TestRegistry.h
  include/CppUTest/TestResult.h
  include/CppUTest/TestTestingFixture.h
  include/CppUTest/Utest.h
  include/CppUTest/UtestMacros.h
  include/CppUTest/VirtualCall.h
  )

install(TARGETS CppUTest
  COMPONENT CppUTest
  EXCLUDE_FROM_ALL
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/CppUTest"
  )

add_subdirectory(test)

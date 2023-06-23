# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(test_acl_test_header INTERFACE)
target_include_directories(test_acl_test_header INTERFACE .)

add_executable(acl_test
  $<TARGET_OBJECTS:acl_objs>
  acl_auto_configure_test.cpp
  acl_command_queue_test.cpp
  acl_context_test.cpp
  acl_device_op_test.cpp
  acl_device_test.cpp
  acl_event_test.cpp
  acl_globals_test.cpp
  acl_hal_mmd_test.cpp
  acl_hal_test.cpp
  acl_kernel_test.cpp
  acl_mem_test.cpp
  acl_platform_test.cpp
  acl_profiler_test.cpp
  acl_program_test.cpp
  acl_sampler_test.cpp
  acl_support_test.cpp
  acl_svm_test.cpp
  acl_test.cpp
  acl_thread_test.cpp
  acl_usm_test.cpp
  acl_mem_device_global_test.cpp
  )
set_target_properties(acl_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(acl_test PRIVATE cxx_std_11)
target_compile_definitions(acl_test PRIVATE
  _GLIBCXX_USE_CXX11_ABI=0
  "ACL_TARGET_BIT=${ACL_TARGET_BIT}"
  CL_USE_DEPRECATED_OPENCL_1_0_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_1_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_2_APIS=1
  CL_TARGET_OPENCL_VERSION=300
  )
target_include_directories(acl_test PRIVATE
  "${CMAKE_BINARY_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src"
  )
target_link_libraries(acl_test PRIVATE
  acl_headers
  acl_check_sys_cmd
  acl_hash
  acl_threadsupport
  CppUTest
  pkg_editor
  )
add_test(NAME acl_test COMMAND acl_test -v)
set_property(TEST acl_test PROPERTY ENVIRONMENT
  "AOCL_BOARD_PACKAGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/board/a10_ref"
  )

add_subdirectory(fake_bsp)

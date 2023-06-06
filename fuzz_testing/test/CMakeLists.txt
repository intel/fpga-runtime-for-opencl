# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(test_acl_fuzz_test_header INTERFACE)
target_include_directories(test_acl_fuzz_test_header INTERFACE .)

add_executable(acl_fuzz_test
  $<TARGET_OBJECTS:acl_objs>
  acl_auto_configure_fuzz_test.cpp
  acl_globals_fuzz_test.cpp
  acl_hal_fuzz_test.cpp
  acl_fuzz_test.cpp
  )
set_target_properties(acl_fuzz_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(acl_fuzz_test PRIVATE cxx_std_11)
target_compile_definitions(acl_fuzz_test PRIVATE
  _GLIBCXX_USE_CXX11_ABI=0
  "ACL_TARGET_BIT=${ACL_TARGET_BIT}"
  CL_USE_DEPRECATED_OPENCL_1_0_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_1_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_2_APIS=1
  CL_TARGET_OPENCL_VERSION=300
  )
target_include_directories(acl_fuzz_test PRIVATE
  "${CMAKE_BINARY_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src"
  )
target_link_libraries(acl_fuzz_test PRIVATE
  acl_headers
  acl_check_sys_cmd
  acl_hash
  acl_threadsupport
  CppUTest
  pkg_editor
  )

# Fuzz tests should not be added to regular unit testing, it should be explicitly called
# add_test(NAME acl_fuzz_test COMMAND acl_fuzz_test -v)
# set_property(TEST acl_fuzz_test PROPERTY ENVIRONMENT
# "AOCL_BOARD_PACKAGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/board/a10_ref"
# )

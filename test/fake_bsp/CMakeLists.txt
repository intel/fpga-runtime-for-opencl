# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(fakegoodbsp SHARED fakegoodbsp.cpp)
target_include_directories(fakegoodbsp PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fakegoodbsp PRIVATE acl_headers CppUTest test_acl_test_header)
target_compile_definitions(fakegoodbsp PRIVATE
  CL_TARGET_OPENCL_VERSION=300
  )
if(WIN32)
  target_compile_definitions(fakegoodbsp PRIVATE AOCL_MMD_CALL=__declspec\(dllexport\))
endif()

add_library(missingfuncbsp SHARED missingfuncbsp.cpp)
target_include_directories(missingfuncbsp PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(missingfuncbsp PRIVATE acl_headers CppUTest test_acl_test_header)
target_compile_definitions(missingfuncbsp PRIVATE
  CL_TARGET_OPENCL_VERSION=300
  )
if(WIN32)
  target_compile_definitions(missingfuncbsp PRIVATE AOCL_MMD_CALL=__declspec\(dllexport\))
endif()

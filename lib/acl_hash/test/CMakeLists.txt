# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_executable(acl_hash_test acl_test.cpp acl_hash_test.cpp)
target_include_directories(acl_hash_test PUBLIC ../include ../src)
target_link_libraries(acl_hash_test PRIVATE CppUTest)
target_compile_definitions(acl_hash_test PRIVATE
  "ACL_TARGET_BIT=${ACL_TARGET_BIT}"
)
add_test(acl_hash_test acl_hash_test -v)

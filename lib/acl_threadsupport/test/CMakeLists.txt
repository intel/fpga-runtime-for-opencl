# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_executable(acl_threadsupport_test acl_test.cpp acl_threadsupport_test.cpp)
target_link_libraries(acl_threadsupport_test PRIVATE acl_threadsupport CppUTest)
target_compile_definitions(acl_threadsupport_test PRIVATE
  "ACL_TARGET_BIT=${ACL_TARGET_BIT}"
  )
add_test(acl_threadsupport_test acl_threadsupport_test -v)

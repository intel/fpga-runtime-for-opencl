# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_executable(acl_check_sys_cmd_test acl_check_sys_cmd_test.cpp)
target_link_libraries(acl_check_sys_cmd_test PRIVATE acl_check_sys_cmd)
add_test(acl_check_sys_cmd_test acl_check_sys_cmd_test -v)

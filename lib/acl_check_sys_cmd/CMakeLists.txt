# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(acl_check_sys_cmd STATIC src/acl_check_sys_cmd.c)
target_include_directories(acl_check_sys_cmd PUBLIC include)
set_target_properties(acl_check_sys_cmd PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  )
set_property(TARGET acl_check_sys_cmd PROPERTY PUBLIC_HEADER
  include/acl_check_sys_cmd/acl_check_sys_cmd.h
  )

install(TARGETS acl_check_sys_cmd
  COMPONENT acl_check_sys_cmd
  EXCLUDE_FROM_ALL
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/acl_check_sys_cmd"
  )

add_subdirectory(test)

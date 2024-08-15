# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(acl_threadsupport STATIC src/acl_threadsupport.c)
target_include_directories(acl_threadsupport PUBLIC include)
set_target_properties(acl_threadsupport PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_property(TARGET acl_threadsupport PROPERTY PUBLIC_HEADER
  include/acl_threadsupport/acl_threadsupport.h
  )

define_file_basename_for_sources(acl_threadsupport)

install(TARGETS acl_threadsupport
  COMPONENT acl_threadsupport
  EXCLUDE_FROM_ALL
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/acl_threadsupport"
  )

add_subdirectory(test)

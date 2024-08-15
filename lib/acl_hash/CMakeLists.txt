# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(acl_hash STATIC src/acl_hash.c)
target_include_directories(acl_hash PUBLIC include)
set_target_properties(acl_hash PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_property(TARGET acl_hash PROPERTY PUBLIC_HEADER
  include/acl_hash/acl_hash.h
  )


define_file_basename_for_sources(acl_hash)

install(TARGETS acl_hash
  COMPONENT acl_hash
  EXCLUDE_FROM_ALL
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/acl_hash"
  )

add_subdirectory(test)

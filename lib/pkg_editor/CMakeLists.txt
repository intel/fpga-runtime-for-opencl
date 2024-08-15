# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_library(pkg_editor STATIC src/pkg_editor.c)
target_link_libraries(pkg_editor PRIVATE "${CMAKE_DL_LIBS}") # dl for dl_open
target_link_libraries(pkg_editor PRIVATE Elf::Elf)
target_include_directories(pkg_editor PUBLIC include)
set_target_properties(pkg_editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_property(TARGET pkg_editor PROPERTY PUBLIC_HEADER
  include/pkg_editor/pkg_editor.h
  )

if(ZLIB_FOUND)
  target_compile_definitions(pkg_editor PRIVATE USE_ZLIB)
  # Customers need to install zlib on windows when using stand-alone runtime.
  if (WIN32)
    get_filename_component(WINDOWS_ZLIB_DLL_PATH "${ZLIB_LIBRARY}" DIRECTORY)
    set(WINDOWS_ZLIB_DLL_PATH "${WINDOWS_ZLIB_DLL_PATH}/zlib1.dll")
    string(REPLACE "/" "\\\\" WINDOWS_ZLIB_DLL_PATH "${WINDOWS_ZLIB_DLL_PATH}")
    target_compile_definitions(pkg_editor PRIVATE WINDOWS_ZLIB_PATH="${WINDOWS_ZLIB_DLL_PATH}")
  endif()
  target_link_libraries(pkg_editor PRIVATE ZLIB::ZLIB)
endif()

define_file_basename_for_sources(pkg_editor)

install(TARGETS pkg_editor
  COMPONENT pkg_editor
  EXCLUDE_FROM_ALL
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pkg_editor"
  )

add_subdirectory(test)

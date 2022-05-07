# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_executable(pkg_editor_test pkg_editor_test.cpp)
target_link_libraries(pkg_editor_test PRIVATE CppUTest pkg_editor)
add_test(pkg_editor_test pkg_editor_test -v)

if(ZLIB_FOUND)
  target_compile_definitions(pkg_editor_test PRIVATE USE_ZLIB)
endif()

# Copy some (but not all) source files to the binary test directory that
# are used as test inputs. The copying is retriggered as part of the
# build whenever the source files are updated. This could be done in the
# tests once the minimum supported compilers implement std::filesystem.
foreach(path
  include/pkg_editor/pkg_editor.h
  src/pkg_editor.c
  test/pkg_editor_test.cpp
  )
  configure_file("../${path}" "${path}" COPYONLY)
endforeach()

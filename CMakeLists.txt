# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.10)

project(fpga-runtime-for-opencl C CXX)

# Set default C++ standard property of targets.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# https://gitlab.kitware.com/cmake/community/-/wikis/doc/cpack/Packaging-With-CPack
# https://cmake.org/cmake/help/v3.10/module/CPack.html
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Intel(R) FPGA Runtime for OpenCL(TM) Software Technology")
set(CPACK_PACKAGE_VENDOR "Intel Corporation")
set(CPACK_PACKAGE_VERSION_MAJOR "2022")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Enforce keeping the source directory clean by building in a separate
# directory. This avoids unnecessary corner cases, e.g., copying files
# from source to binary directory with identical relative paths.
if(PROJECT_BINARY_DIR STREQUAL PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "build directory must be different from source directory")
endif()

# We exclusively support Ninja as the generator used to build the project.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
  message(FATAL_ERROR "unsupported cmake generator, please use `cmake -G Ninja`")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")

find_package(Elf REQUIRED)
find_package(ZLIB)
find_package(Git)

set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

math(EXPR ACL_TARGET_BIT "${CMAKE_SIZEOF_VOID_P} * 8")
message(STATUS "Target pointer size in bits: ${ACL_TARGET_BIT}")

################################################################################
### Modified Default flags
################################################################################
# Use  "multithread-specific and DLL-specific version of the run-time library" (Windows)
# There's a better way for CMake >= 3.15
# See https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html
if(WIN32 AND "${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MD")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD")
endif()

# Flag for coverage scans
option(ACL_CODE_COVERAGE "Build with coverage" OFF)
message(STATUS "Build with coverage: ${ACL_CODE_COVERAGE}")
if(ACL_CODE_COVERAGE)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
  else()
    message(FATAL_ERROR "cannot build with coverage tools due to unsupported CXX compiler")
  endif()
endif()

################################################################################
### Modified Debug flags
################################################################################
# pkg_editor_test and acl_test have trouble with the debug version of the multithread Windows runtime library
# There's a better way for CMake >= 3.15
# See https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html
string(REGEX REPLACE "/MDd" "/MD" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "/MDd" "/MD" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
# pkg_editor_test errors out if we enable runtime error checking
string(REGEX REPLACE "( /RTC1$|/RTC1 )" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "( /RTC1$|/RTC1 )" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

# -DNDEBUG will disable calls to assert() in our unit tests
foreach(flags
  CMAKE_C_FLAGS_RELWITHDEBINFO
  CMAKE_CXX_FLAGS_RELWITHDEBINFO
  CMAKE_C_FLAGS_RELEASE
  CMAKE_CXX_FLAGS_RELEASE
  CMAKE_C_FLAGS_MINSIZEREL
  CMAKE_CXX_FLAGS_MINSIZEREL
  )
  string(REGEX REPLACE "(^|[ \t])[/-][dD][ \t]*NDEBUG($|[ \t])" " " "${flags}" "${${flags}}")
endforeach()

# Windows debug builds frequently crash mspdbsrv.exe, resulting in fatal
# error C1090: PDB API call failed, error code '23': (0x000006BA). Work
# around this unresolved issue by disabling separate PDB files (`/Zi`)
# and instead embedding debugging symbols in object files (`/Z7`).
#
# https://bugs.chromium.org/p/chromium/issues/detail?id=659439
# https://developercommunity.visualstudio.com/t/c1090-pdb-api-call-failed-error-code-23/48897
# https://docs.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-160
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  foreach(flags
    CMAKE_C_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    )
    string(REGEX REPLACE "(^|[ \t])/Zi($|[ \t])" " /Z7 " "${flags}" "${${flags}}")
  endforeach()
endif()

# https://clang.llvm.org/docs/AddressSanitizer.html
option(ACL_WITH_ASAN "Build with address sanitizer" OFF)
message(STATUS "Build with address sanitizer: ${ACL_WITH_ASAN}")
if(ACL_WITH_ASAN)
  foreach(lang C CXX)
    if(CMAKE_${lang}_COMPILER_ID MATCHES "^(Clang|GNU)$")
      set(CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    else()
      message(FATAL_ERROR "cannot build with address sanitizer due to unsupported ${lang} compiler")
    endif()
  endforeach()
endif()

include(CPack)
include(CTest)

# Get CMAKE_INSTALL_<dir> variables to enable custom library, binary, and include paths
# See https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html
include(GNUInstallDirs)

# Windows defines its own versions of min() and max() which conflict with the
# versions from the standard library.  Disable the Windows versions by defining
# the NOMINMAX macro.  This must be set before including acl.h, which
# eventually leads to the inclusion of windows.h, where the min and max macros
# are defined.
set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS NOMINMAX)

# acl_hostxml includes acl.h, but we don't want to create a cyclic dependency,
# so we create a header only library here
add_library(acl_headers INTERFACE)
target_include_directories(acl_headers INTERFACE include)

# Generate header containing the runtime version. The OUTPUT file *.phony is in
# fact never written and ensures ninja always considers this target out of date.
add_custom_command(
  OUTPUT
    "${CMAKE_BINARY_DIR}/include/acl_version.h"
    "${CMAKE_BINARY_DIR}/include/acl_version.h.phony"
  DEPENDS "${CMAKE_SOURCE_DIR}/include/acl_version.h.in"
  COMMAND "${CMAKE_COMMAND}"
    -D "ACL_VERSION_INPUT_FILE=${CMAKE_SOURCE_DIR}/include/acl_version.h.in"
    -D "ACL_VERSION_OUTPUT_FILE=${CMAKE_BINARY_DIR}/include/acl_version.h"
    -D "GIT_FOUND=${GIT_FOUND}"
    -D "GIT_EXECUTABLE=${GIT_EXECUTABLE}"
    -P "${CMAKE_CURRENT_LIST_DIR}/cmake/scripts/version.cmake"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_library(acl_objs OBJECT
  "${CMAKE_BINARY_DIR}/include/acl_version.h"
  src/acl_auto_configure.cpp
  src/acl_bsp_io.cpp
  src/acl_command.cpp
  src/acl_command_queue.cpp
  src/acl_context.cpp
  src/acl_device.cpp
  src/acl_device_binary.cpp
  src/acl_device_op.cpp
  src/acl_device_program_info.cpp
  src/acl_event.cpp
  src/acl_globals.cpp
  src/acl_hal.cpp
  src/acl_hal_mmd.cpp
  src/acl_hostch.cpp
  src/acl_icd_dispatch.cpp
  src/acl_kernel.cpp
  src/acl_kernel_if.cpp
  src/acl_mem.cpp
  src/acl_offline_hal.cpp
  src/acl_platform.cpp
  src/acl_pll.cpp
  src/acl_printf.cpp
  src/acl_profiler.cpp
  src/acl_program.cpp
  src/acl_sampler.cpp
  src/acl_shared_aligned_ptr.cpp
  src/acl_support.cpp
  src/acl_svm.cpp
  src/acl_thread.cpp
  src/acl_usm.cpp
  src/check_copy_overlap.c
  )
set_target_properties(acl_objs PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  )
target_compile_features(acl_objs PRIVATE cxx_std_11)
# These compile_definitions need to be public, since we're building an OBJECT library (?)
target_compile_definitions(acl_objs PUBLIC
  # Revert to old pre-C++11 ABI for std::string and std::list in libstdc++.
  # This is needed for ABI compatibility with the Intel® FPGA Add-On for
  # oneAPI Base Toolkit, particularly the simulation flow, which accesses
  # the internal runtime API that exposes C++ structures and functions.
  # https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
  _GLIBCXX_USE_CXX11_ABI=0
  ACL_SUPPORT_DOUBLE=0
  ACL_HAS_STDLIB_STDIO
  CL_USE_DEPRECATED_OPENCL_1_0_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_1_APIS=1
  CL_USE_DEPRECATED_OPENCL_1_2_APIS=1
  CL_TARGET_OPENCL_VERSION=300
  )
target_include_directories(acl_objs PUBLIC include)
target_include_directories(acl_objs PRIVATE
  "${CMAKE_BINARY_DIR}/include"
  src
  )
# "Linking" libraries to an OBJECT library is the preferred approach, but it is supported only after CMake 3.12
# The oldest CMake version we need to support is 3.10
# As older OS support becomes deprecated and we don't need to worry about CMake < 3.12, we can change this
# See: https://cmake.org/cmake/help/v3.12/command/target_link_libraries.html#linking-object-libraries
target_include_directories(acl_objs PRIVATE
  "${CMAKE_SOURCE_DIR}/lib/acl_check_sys_cmd/include"
  "${CMAKE_SOURCE_DIR}/lib/acl_hash/include"
  "${CMAKE_SOURCE_DIR}/lib/acl_threadsupport/include"
  "${CMAKE_SOURCE_DIR}/lib/pkg_editor/include"
  )

add_library(acl SHARED $<TARGET_OBJECTS:acl_objs>)
set_property(TARGET acl PROPERTY OUTPUT_NAME "alteracl")
target_link_libraries(acl PRIVATE
  acl_check_sys_cmd
  acl_hash
  acl_threadsupport
  pkg_editor
  Threads::Threads
  )
if(UNIX)
  # we need this flag to expose symbols, otherwise linking will fail with:
  # "relocation against protected symbol X can not be used when making a shared object"
  set_target_properties(acl PROPERTIES LINK_FLAGS "-Wl,-Bsymbolic")
endif()

set_property(TARGET acl PROPERTY PUBLIC_HEADER
  include/acl.h
  include/acl_auto.h
  include/acl_auto_configure.h
  include/acl_auto_configure_version.h
  include/acl_context.h
  include/acl_device_binary.h
  include/acl_globals.h
  include/acl_hal.h
  include/acl_hal_mmd.h
  include/acl_hostch.h
  include/acl_icd_dispatch.h
  include/acl_shared_aligned_ptr.h
  include/acl_support.h
  include/acl_thread.h
  include/acl_types.h
  include/acl_util.h
  include/acl_visibility.h
  )

set_property(TARGET acl PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)

install(TARGETS acl
  COMPONENT acl
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )

# Workaround for CL/[name].h and MMD/[name].h headers
# There's a better way for CMake >= 3.15
# Change this to be a header-only library and use PUBLIC_HEADER
# see https://cmake.org/cmake/help/latest/command/add_library.html#interface-libraries

install(FILES
    include/CL/cl.h
    include/CL/cl_d3d10.h
    include/CL/cl_d3d11.h
    include/CL/cl_dx9_media_sharing.h
    include/CL/cl_dx9_media_sharing_intel.h
    include/CL/cl_egl.h
    include/CL/cl_ext.h
    include/CL/cl_ext_intel.h
    include/CL/cl_ext_intelfpga.h
    include/CL/cl_gl.h
    include/CL/cl_gl_ext.h
    include/CL/cl_half.h
    include/CL/cl_icd.h
    include/CL/cl_platform.h
    include/CL/cl_va_api_media_sharing_intel.h
    include/CL/cl_version.h
    include/CL/opencl.h
    include/CL/opencl.hpp
  COMPONENT CL
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/CL"
  )

install(FILES
    include/MMD/aocl_mmd.h
    include/MMD/aocl_mmd_deprecated.h
  COMPONENT MMD
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/MMD"
  )

add_subdirectory(lib)
add_subdirectory(test)

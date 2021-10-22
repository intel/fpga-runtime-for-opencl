# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

find_path(Elf_INCLUDE_DIR NAMES libelf/libelf.h libelf.h)

if(Elf_INCLUDE_DIR AND EXISTS "${Elf_INCLUDE_DIR}/libelf/libelf.h")
  set(Elf_HAVE_LIBELF_LIBELF TRUE)
else()
  set(Elf_HAVE_LIBELF_LIBELF FALSE)
endif()

find_library(Elf_LIBRARY NAMES elf libelf)

mark_as_advanced(Elf_INCLUDE_DIR Elf_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Elf REQUIRED_VARS Elf_INCLUDE_DIR Elf_LIBRARY)

if(Elf_FOUND)
  add_library(Elf::Elf UNKNOWN IMPORTED)
  set_target_properties(Elf::Elf PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES C
    IMPORTED_LOCATION "${Elf_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${Elf_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS "$<$<BOOL:${Elf_HAVE_LIBELF_LIBELF}>:HAVE_LIBELF_LIBELF>"
    )
endif()

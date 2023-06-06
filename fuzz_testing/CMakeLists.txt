# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

add_subdirectory(test)
file(COPY original_inputs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY script DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

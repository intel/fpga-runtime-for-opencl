// Copyright (C) 2011-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_AUTO_CONFIGURE_H
#define ACL_AUTO_CONFIGURE_H

#include "acl.h"
#include "acl_types.h"
#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Load info about a compiled program from its system description string into
// devdef.
// Return true if successful, false otherwise.
// If an error occurred err_str will contain an error message describing the
// cause of the failure.
bool acl_load_device_def_from_str(const std::string &config_str,
                                  acl_device_def_autodiscovery_t &devdef,
                                  std::string &err_str) noexcept;

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

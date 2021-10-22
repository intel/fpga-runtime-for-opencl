// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_OFFLINE_HAL_H
#define ACL_OFFLINE_HAL_H

// An offline HAL, for use with offline devices.

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#include "acl_hal.h"

// Return a pointer to an HAL suitable for use with an offline device.
const acl_hal_t *acl_get_offline_hal(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

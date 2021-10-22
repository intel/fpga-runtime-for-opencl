// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_COMMAND_H
#define ACL_COMMAND_H

#include "acl.h"
#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Launch a command if we can.
// Return a number bigger than zero if we made forward progress, false
// otherwise.
int acl_submit_command(cl_event event);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

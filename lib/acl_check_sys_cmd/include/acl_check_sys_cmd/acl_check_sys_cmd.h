// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_CHECK_SYS_CMD_H
#define ACL_CHECK_SYS_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

// Check whether "cmd" only contains the following characters:
// letters, digits
//  '\t', '\n', ' ', '"', '&', '\'', '-', '.', '/', ':', ';',
//  '=', '>', '@', '\\', '_', '~'
// If yes, return 1; otherwise, return 0;
int system_cmd_is_valid(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif

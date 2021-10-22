// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/*  Format is board name, then sys description, then terminated by two NULLs */
static const char *acl_shipped_board_cfgs[2 * (ACL_MAX_DEVICE + 1)] = {
    "a10gx",
    "23 16 sample40byterandomhash000000000000000000 a10gx 0 1 9 DDR 2 1 2 0 "
    "2147483648 0 - 0 0 0 0 0 ",
    "a10_ref_small",
    "23 16 sample40byterandomhash000000000000000000 a10_ref_small 0 1 9 DDR 2 "
    "1 2 0 134217728 0 - 0 0 0 0 0 ",
    "s10gx",
    "23 16 sample40byterandomhash000000000000000000 s10gx_ea 0 1 9 DDR 2 1 2 0 "
    "2147483648 0 - 0 0 0 0 0 ",
    0,
    0};

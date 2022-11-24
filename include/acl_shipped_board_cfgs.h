// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include <array>

/*  Format is std::array of struct containing board name and sys description */
struct shipped_board_cfg {
  const char *name;
  const char *cfg;
};

static const std::array<shipped_board_cfg, 3> acl_shipped_board_cfgs = {{
    {"a10gx", "23 16 sample40byterandomhash000000000000000000 a10gx 0 1 9 "
              "DDR 2 1 2 0 2147483648 0 - 0 0 0 0 0 "},
    {"a10_ref_small",
     "23 16 sample40byterandomhash000000000000000000 a10_ref_small 0 1 9 "
     "DDR 2 1 2 0 134217728 0 - 0 0 0 0 0 "},
    {"s10gx", "23 16 sample40byterandomhash000000000000000000 s10gx_ea 0 "
              "1 9 DDR 2 1 2 0 2147483648 0 - 0 0 0 0 0 "},
}};

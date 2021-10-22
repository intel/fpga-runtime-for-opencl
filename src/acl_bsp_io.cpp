// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// Internal headers.
#include <acl_bsp_io.h>
#include <acl_thread.h>

int acl_bsp_io_is_valid(acl_bsp_io *bsp_io) {
  acl_assert_locked_or_sig();
  return ((bsp_io) && (bsp_io->read) && (bsp_io->write) &&
          (bsp_io->get_time_ns))
             ? 1
             : 0;
}

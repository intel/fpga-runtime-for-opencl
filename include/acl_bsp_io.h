// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_BSP_IO_H
#define ACL_BSP_IO_H

#ifndef _WIN32
#include <stdint.h>
#endif
#include <stdlib.h>

#include "acl_hal_mmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long time_ns;

// Board addresses are independent of host, so define type for device addresses
typedef uintptr_t dev_addr_t;

struct acl_bsp_io_t;
typedef struct acl_bsp_io_t acl_bsp_io;

struct acl_bsp_io_t {

  acl_mmd_device_t *device_info;

  // Read accessor - returns number of bytes read
  size_t (*read)(acl_bsp_io *io, dev_addr_t src, char *dest, size_t size);

  // Write accessor - returns number of bytes written
  size_t (*write)(acl_bsp_io *io, dev_addr_t dest, const char *src,
                  size_t size);

  time_ns (*get_time_ns)();

  int (*printf)(const char *fmt, ...);

  int debug_verbosity;
};

int acl_bsp_io_is_valid(acl_bsp_io *bsp_io);

#ifdef __cplusplus
}
#endif

#endif // ACL_BSP_IO_H

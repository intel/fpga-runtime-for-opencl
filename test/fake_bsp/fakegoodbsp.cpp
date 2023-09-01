// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// A simple BSP for unit tests

#include <sstream>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <MMD/aocl_mmd.h>
#include <MMD/aocl_mmd_deprecated.h>

#include "acl.h"
#include "acl_bsp_io.h"
#include "acl_pll.h"
#include "acl_test.h"
#include "unref.h"

#define ACL_VENDOR_NAME "Intel(R) Corporation"
#define ACL_BOARD_NAME "Unit Test Board"
#define MMD_VERSION AOCL_MMD_VERSION_STRING
#define ACL_PCI_INTELFPGA_VENDOR_ID 0xFFFF
#define ACL_PCI_SUBSYSTEM_VENDOR_ID 0xFFFE
#define ACL_PCI_SUBSYSTEM_DEVICE_ID 0xAAAA

typedef enum {
  AOCL_MMD_KERNEL = 0, // Control interface into kernel interface
  AOCL_MMD_MEMORY = 1, // Data interface to device memory
  AOCL_MMD_PLL = 2,    // Interface for reconfigurable PLL
} aocl_mmd_interface_t;

#define RESULT_INT(X)                                                          \
  {                                                                            \
    *((int *)param_value) = X;                                                 \
    if (param_size_ret)                                                        \
      *param_size_ret = sizeof(int);                                           \
  }
#define RESULT_UINT(X)                                                         \
  {                                                                            \
    *((unsigned int *)param_value) = X;                                        \
    if (param_size_ret)                                                        \
      *param_size_ret = sizeof(unsigned int);                                  \
  }
#define RESULT_STR(X)                                                          \
  do {                                                                         \
    size_t Xlen = strlen(X) + 1;                                               \
    memcpy((void *)param_value, X,                                             \
           (param_value_size <= Xlen) ? param_value_size : Xlen);              \
    if (param_size_ret)                                                        \
      *param_size_ret = Xlen;                                                  \
  } while (0)
#define RESULT_FLOAT(X)                                                        \
  {                                                                            \
    *((float *)param_value) = X;                                               \
    if (param_size_ret)                                                        \
      *param_size_ret = sizeof(float);                                         \
  }

int next_device_id = 0;

acl_hal_device_test devices[ACL_MAX_DEVICE + 1];

unsigned int sw_reset = 1;
unsigned int reset = 1;
unsigned int offset_counter = 0;
unsigned int lock = 0;
unsigned int reconfig_ctrl = 0;
unsigned int offset_mem_org = 0;
pll_setting_t fake_pll_setting = {100000, 1, 1, 1, 2, 1, 500, 5, 1};
pll_setting_t empty_pll_setting = {0, 1, 1, 1, 2, 1, 1, 1, 1};

typedef struct {
  unsigned int pll_mode;
  unsigned int pll_status;
  unsigned int pll_start;
  unsigned int pll_n;
  unsigned int pll_m;
  unsigned int pll_c;
  unsigned int pll_phase;
  unsigned int pll_k;
  unsigned int pll_r;
  unsigned int pll_cp;
  unsigned int pll_div;
  unsigned int pll_c0;
  unsigned int pll_c1;
} pll_reconfig_t;
pll_reconfig_t pll_reconfig = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

int AOCL_MMD_CALL aocl_mmd_get_offline_info(
    aocl_mmd_offline_info_t requested_info_id, size_t param_value_size,
    void *param_value, size_t *param_size_ret) {
  switch (requested_info_id) {
  case AOCL_MMD_VERSION:
    RESULT_STR(MMD_VERSION);
    break;
  case AOCL_MMD_NUM_BOARDS:
    RESULT_INT(ACL_MAX_DEVICE);
    break;
  case AOCL_MMD_BOARD_NAMES: {
    // Construct a list of all possible devices supported by this MMD layer
    std::ostringstream boards;
    for (unsigned i = 0; i < ACL_MAX_DEVICE; i++) {
      boards << "acl" << i;
      if (i < ACL_MAX_DEVICE - 1)
        boards << ";";
    }
    RESULT_STR(boards.str().c_str());
    break;
  }
  case AOCL_MMD_VENDOR_NAME: {
    RESULT_STR(ACL_VENDOR_NAME);
    break;
  }
  case AOCL_MMD_VENDOR_ID:
    RESULT_INT(ACL_PCI_INTELFPGA_VENDOR_ID);
    break;
  case AOCL_MMD_USES_YIELD:
    RESULT_INT(0);
    break;
  case AOCL_MMD_MEM_TYPES_SUPPORTED:
    RESULT_INT(AOCL_MMD_PHYSICAL_MEMORY);
    break;
  }
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_get_info(int handle,
                                    aocl_mmd_info_t requested_info_id,
                                    size_t param_value_size, void *param_value,
                                    size_t *param_size_ret) {
  char pcie_info_str[1024];
  UNREFERENCED_PARAMETER(handle);
  snprintf(pcie_info_str, sizeof(pcie_info_str) / sizeof(pcie_info_str[0]),
           "dev_id = %04X, bus:slot.func = %s, at Gen %u with %u lanes", 0, "1",
           1, 1);

  switch (requested_info_id) {
  case AOCL_MMD_BOARD_NAME:
    RESULT_STR(ACL_BOARD_NAME);
    break;
  case AOCL_MMD_NUM_KERNEL_INTERFACES:
    RESULT_INT(1);
    break;
  case AOCL_MMD_KERNEL_INTERFACES:
    RESULT_INT(AOCL_MMD_KERNEL);
    break;
  case AOCL_MMD_PLL_INTERFACES:
    RESULT_INT(AOCL_MMD_PLL);
    break;
  case AOCL_MMD_MEMORY_INTERFACE:
    RESULT_INT(AOCL_MMD_MEMORY);
    break;
  case AOCL_MMD_PCIE_INFO:
    RESULT_STR(pcie_info_str);
    break;
  case AOCL_MMD_TEMPERATURE:
    RESULT_FLOAT(25);
    break;
  case AOCL_MMD_CONCURRENT_READS:
    RESULT_UINT(1);
    break;
  case AOCL_MMD_CONCURRENT_WRITES:
    RESULT_UINT(1);
    break;
  case AOCL_MMD_CONCURRENT_READS_OR_WRITES:
    RESULT_UINT(1);
    break;
  case AOCL_MMD_MIN_HOST_MEMORY_ALIGNMENT:
    RESULT_UINT(1024);
    break;
  case AOCL_MMD_HOST_MEM_CAPABILITIES:
    RESULT_UINT(0);
    break;
  case AOCL_MMD_SHARED_MEM_CAPABILITIES:
    RESULT_UINT(0);
    break;
  case AOCL_MMD_DEVICE_MEM_CAPABILITIES:
    RESULT_UINT(0);
    break;
  case AOCL_MMD_HOST_MEM_CONCURRENT_GRANULARITY:
    RESULT_UINT(0);
    break;
  case AOCL_MMD_SHARED_MEM_CONCURRENT_GRANULARITY:
    RESULT_UINT(0);
    break;
  case AOCL_MMD_DEVICE_MEM_CONCURRENT_GRANULARITY:
    RESULT_UINT(0);
    break;

  // currently not supported
  case AOCL_MMD_BOARD_UNIQUE_ID:
    return -1;
  }
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_open(const char *name) {
  int return_val = next_device_id;
  UNREFERENCED_PARAMETER(name);
  ++next_device_id;
  if (return_val >= ACL_MAX_DEVICE) {
    return -1;
  }
  if (devices[return_val].is_active) {
    return -1;
  }
  devices[return_val].is_active = CL_TRUE;

  return return_val;
}

AOCL_MMD_CALL int aocl_mmd_close(int handle) {
  if (!devices[handle].is_active) {
    return -1;
  }
  devices[handle].is_active = CL_FALSE;
  return 0;
}

AOCL_MMD_CALL int
aocl_mmd_set_interrupt_handler(int handle, aocl_mmd_interrupt_handler_fn fn,
                               void *user_data) {
  devices[handle].kernel_interrupt = fn;
  devices[handle].interrupt_user_data = user_data;
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_set_status_handler(int handle,
                                              aocl_mmd_status_handler_fn fn,
                                              void *user_data) {
  devices[handle].kernel_status = fn;
  devices[handle].status_user_data = user_data;
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_yield(int handle) {
  UNREFERENCED_PARAMETER(handle);
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_read(int handle, aocl_mmd_op_t op, size_t len,
                                void *dst, int mmd_interface, size_t offset) {
  mem_data_t *next_mem_data;
  size_t new_offset;
  char config_str[] =
      "14 s5_net 0 0 2 1024 0 4294967296 4294967296 8589934592 0 0 ";
  UNREFERENCED_PARAMETER(op);
  switch (offset) {
  case OFFSET_KERNEL_VERSION_ID:
    if (len == sizeof(unsigned int)) {
      unsigned int version_id = KERNEL_VERSION_ID;
      memcpy(dst, &version_id, len);
      return 0;
    } else {
      return -1;
    }
    break;
  case OFFSET_KERNEL_CRA_SEGMENT:
    fprintf(stderr, "Error: Not handling read of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_SW_RESET:
    if (len == sizeof(unsigned int)) {
      memcpy(dst, &sw_reset, len);
      // Simulate coming out of reset after the first read
      if (sw_reset == 0) {
        sw_reset = 1;
      }
      return 0;
    } else {
      return -1;
    }
  case OFFSET_MEM_ORG:
    if (len == sizeof(unsigned int)) {
      memcpy(dst, &offset_mem_org, len);
      return 0;
    } else {
      return -1;
    }
  case OFFSET_KERNEL_CRA:
    fprintf(stderr, "Error: Not handling read of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_CONFIGURATION_ROM: {
    const size_t config_len = strlen(config_str);
    if (!(config_len < len)) {
      return -1;
    }
    memcpy(dst, config_str, config_len + 1);
    return 0;
  }
  case OFFSET_COUNTER:
    if (len == sizeof(unsigned int)) {
      memcpy(dst, &offset_counter, len);
      return 0;
    } else {
      return -1;
    }
  case OFFSET_RESET:
    if (len == sizeof(unsigned int)) {
      memcpy(dst, &reset, len);
      // Simulate coming out of reset after the first read
      if (reset == 0) {
        reset = 1;
      }
      return 0;
    } else {
      return -1;
    }
  case OFFSET_LOCK:
    if (len == sizeof(unsigned int)) {
      memcpy(dst, &lock, len);
      // Simulate locking after the first read
      if (lock == 0) {
        lock = 1;
      }
      return 0;
    } else {
      return -1;
    }
  }

  if (offset >= OFFSET_ROM &&
      offset + len <
          OFFSET_ROM + (MAX_KNOWN_SETTINGS + 1) * sizeof(pll_setting_t)) {
    size_t pll_setting_offset = (offset - OFFSET_ROM) % sizeof(pll_setting_t);
    if (offset < OFFSET_ROM + sizeof(pll_setting_t)) {
      assert(len <= sizeof(fake_pll_setting) - pll_setting_offset);
      memcpy(dst, ((char *)&fake_pll_setting + pll_setting_offset), len);
    } else {
      assert(len <= sizeof(empty_pll_setting) - pll_setting_offset);
      memcpy(dst, ((char *)&empty_pll_setting + pll_setting_offset), len);
    }
    return 0;
  }

  if (offset >= OFFSET_RECONFIG_CTRL &&
      offset + len < OFFSET_RECONFIG_CTRL + (11 * 4)) {
    size_t pll_reconfig_offset =
        (offset - OFFSET_RECONFIG_CTRL) % sizeof(pll_reconfig_t);
    if (offset < OFFSET_RECONFIG_CTRL + sizeof(pll_reconfig_t)) {
      assert(len <= sizeof(pll_reconfig) - pll_reconfig_offset);
      memcpy(dst, ((char *)&pll_reconfig + pll_reconfig_offset), len);
    } else {
      fprintf(stderr,
              "Error: Only handle one set of pll reconfig data in unit test\n");
      return -1;
    }
    return 0;
  }

  next_mem_data = devices[handle].mem_data;

  if (next_mem_data != NULL) {
    if (!(mmd_interface == next_mem_data->mmd_interface &&
          offset >= next_mem_data->offset &&
          offset < next_mem_data->offset + next_mem_data->size)) {
      fprintf(stderr, "Error: The offset is invalid\n");
    }
  }

  if (next_mem_data == NULL) {
    fprintf(stderr,
            "Error: Trying to read from 0x%x in unit test but that location "
            "has not been initialized\n",
            (unsigned int)offset);
    return -1;
  }

  new_offset = offset - next_mem_data->offset;
  if (new_offset + len > next_mem_data->offset + next_mem_data->size) {
    return -1;
  }

  memcpy(dst, (void *)((char *)next_mem_data->data + new_offset), len);

  return 0;
}

AOCL_MMD_CALL int aocl_mmd_write(int handle, aocl_mmd_op_t op, size_t len,
                                 const void *src, int mmd_interface,
                                 size_t offset) {
  mem_data_t *next_mem_data;
  size_t new_offset;
  UNREFERENCED_PARAMETER(op);
  switch (offset) {
  case OFFSET_KERNEL_VERSION_ID:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_KERNEL_CRA_SEGMENT:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_SW_RESET:
    if (len == sizeof(unsigned int)) {
      sw_reset = *(unsigned int *)src;
      return 0;
    } else {
      return -1;
    }
  case OFFSET_MEM_ORG:
    if (len == sizeof(unsigned int)) {
      offset_mem_org = *(unsigned int *)src;
      return 0;
    } else {
      return -1;
    }
  case OFFSET_KERNEL_CRA:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_CONFIGURATION_ROM:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_ROM:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  case OFFSET_COUNTER:
    if (len == sizeof(unsigned int)) {
      offset_counter = *(unsigned int *)src;
      return 0;
    } else {
      return -1;
    }
  case OFFSET_RESET:
    if (len == sizeof(unsigned int)) {
      reset = *(unsigned int *)src;
      return 0;
    } else {
      return -1;
    }
  case OFFSET_LOCK:
    fprintf(stderr, "Error: Not handling write of 0x%x in unit test\n",
            (unsigned int)offset);
    return -1;
  }

  if (offset >= OFFSET_RECONFIG_CTRL &&
      offset + len < OFFSET_RECONFIG_CTRL + (11 * 4)) {
    size_t pll_reconfig_offset =
        (offset - OFFSET_RECONFIG_CTRL) % sizeof(pll_reconfig_t);
    if (offset < OFFSET_RECONFIG_CTRL + sizeof(pll_reconfig_t)) {
      assert(len <= sizeof(pll_reconfig) - pll_reconfig_offset);
      memcpy(((char *)&pll_reconfig + pll_reconfig_offset), src, len);
    } else {
      fprintf(stderr,
              "Error: Only handle one set of pll reconfig data in unit test\n");
      return -1;
    }
    return 0;
  }

  next_mem_data = devices[handle].mem_data;

  if (next_mem_data != NULL) {
    if (!(mmd_interface == next_mem_data->mmd_interface &&
          offset >= next_mem_data->offset &&
          offset < next_mem_data->offset + next_mem_data->size)) {
      fprintf(stderr, "Error: The offset is invalid\n");
    }
  }

  if (next_mem_data == NULL) {
    next_mem_data = (mem_data_t *)malloc(sizeof(mem_data_t));
    if (!next_mem_data)
      return -1; // malloc failed
    next_mem_data->data = (void *)malloc(len);
    next_mem_data->mmd_interface = mmd_interface;
    next_mem_data->offset = offset;
    next_mem_data->size = len;

    new_offset = 0;
  } else {
    new_offset = offset - next_mem_data->offset;
    if (new_offset + len > next_mem_data->offset + next_mem_data->size) {
      return -1;
    }
  }

  memcpy((void *)((char *)next_mem_data->data + new_offset), src, len);

  if (next_mem_data)
    free(next_mem_data->data);
  free(next_mem_data);
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_copy(int handle, aocl_mmd_op_t op, size_t len,
                                int mmd_interface, size_t src_offset,
                                size_t dst_offset) {
  mem_data_t *next_src_mem_data;
  size_t new_src_offset;
  mem_data_t *next_dst_mem_data;
  size_t new_dst_offset;
  UNREFERENCED_PARAMETER(op);
  switch (src_offset) {
  case OFFSET_KERNEL_VERSION_ID:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_KERNEL_CRA_SEGMENT:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_SW_RESET:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_MEM_ORG:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_KERNEL_CRA:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_CONFIGURATION_ROM:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_ROM:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_RECONFIG_CTRL:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_COUNTER:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_RESET:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  case OFFSET_LOCK:
    fprintf(stderr, "Error: Not handling copy from 0x%x in unit test\n",
            (unsigned int)src_offset);
    return -1;
  }
  switch (dst_offset) {
  case OFFSET_KERNEL_VERSION_ID:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_KERNEL_CRA_SEGMENT:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_SW_RESET:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_MEM_ORG:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_KERNEL_CRA:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_CONFIGURATION_ROM:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_ROM:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_RECONFIG_CTRL:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_COUNTER:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_RESET:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  case OFFSET_LOCK:
    fprintf(stderr, "Error: Not handling copy to 0x%x in unit test\n",
            (unsigned int)dst_offset);
    return -1;
  }

  next_src_mem_data = devices[handle].mem_data;

  if (next_src_mem_data != NULL) {
    if (!(mmd_interface == next_src_mem_data->mmd_interface &&
          src_offset >= next_src_mem_data->offset &&
          src_offset < next_src_mem_data->offset + next_src_mem_data->size)) {
      fprintf(stderr, "Error: The src offset is invalid\n");
    }
  }

  if (next_src_mem_data == NULL) {
    fprintf(stderr,
            "Error: Trying to copy from 0x%x in unit test but that location "
            "has not been initialized\n",
            (unsigned int)src_offset);
    return -1;
  }

  new_src_offset = src_offset - next_src_mem_data->offset;
  if (new_src_offset + len >
      next_src_mem_data->offset + next_src_mem_data->size) {
    return -1;
  }

  next_dst_mem_data = devices[handle].mem_data;

  if (next_dst_mem_data != NULL) {
    if (!(mmd_interface == next_dst_mem_data->mmd_interface &&
          dst_offset >= next_dst_mem_data->offset &&
          dst_offset < next_dst_mem_data->offset + next_dst_mem_data->size)) {
      fprintf(stderr, "Error: The dst offset is invalid\n");
    }
  }

  if (next_dst_mem_data == NULL) {
    next_dst_mem_data = (mem_data_t *)malloc(sizeof(mem_data_t));
    if (!next_dst_mem_data)
      return -1; // malloc failed
    next_dst_mem_data->data = (void *)malloc(len);
    next_dst_mem_data->mmd_interface = mmd_interface;
    next_dst_mem_data->offset = dst_offset;
    next_dst_mem_data->size = len;

    new_dst_offset = 0;
  } else {
    new_dst_offset = dst_offset - next_dst_mem_data->offset;
    if (new_dst_offset + len >
        next_dst_mem_data->offset + next_dst_mem_data->size) {
      return -1;
    }
  }

  memcpy((void *)((char *)next_dst_mem_data->data + new_dst_offset),
         (void *)((char *)next_src_mem_data->data + new_src_offset), len);

  if (next_dst_mem_data)
    free(next_dst_mem_data->data);
  free(next_dst_mem_data);
  return 0;
}

AOCL_MMD_CALL int aocl_mmd_program(int handle, void *user_data, size_t size,
                                   aocl_mmd_program_mode_t program_mode) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(user_data);
  UNREFERENCED_PARAMETER(size);
  UNREFERENCED_PARAMETER(program_mode);
  return 0;
}

AOCL_MMD_CALL void *
aocl_mmd_shared_mem_alloc(int handle, size_t size,
                          unsigned long long *device_ptr_out) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(size);
  UNREFERENCED_PARAMETER(device_ptr_out);
  return NULL;
}

AOCL_MMD_CALL void aocl_mmd_shared_mem_free(int handle, void *host_ptr,
                                            size_t size) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(host_ptr);
  UNREFERENCED_PARAMETER(size);
  return;
}

AOCL_MMD_CALL int aocl_mmd_reprogram(int handle, void *user_data, size_t size) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(user_data);
  UNREFERENCED_PARAMETER(size);
  return 0;
}

AOCL_MMD_CALL void *aocl_mmd_shared_alloc(int handle, size_t size,
                                          size_t alignment,
                                          unsigned long *properties,
                                          int *error) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(size);
  UNREFERENCED_PARAMETER(alignment);
  UNREFERENCED_PARAMETER(properties);
  UNREFERENCED_PARAMETER(error);
  return 0;
}

void *aocl_mmd_host_alloc(int *handles, size_t num_devices, size_t size,
                          size_t alignment,
                          aocl_mmd_mem_properties_t *properties, int *error) {
  UNREFERENCED_PARAMETER(handles);
  UNREFERENCED_PARAMETER(num_devices);
  UNREFERENCED_PARAMETER(size);
  UNREFERENCED_PARAMETER(alignment);
  UNREFERENCED_PARAMETER(properties);
  UNREFERENCED_PARAMETER(error);
  return NULL;
}

int aocl_mmd_free(void *mem) {
  UNREFERENCED_PARAMETER(mem);
  return 0;
}

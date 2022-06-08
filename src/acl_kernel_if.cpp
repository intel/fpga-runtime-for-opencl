// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <cassert>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

// Internal headers.
#include <acl_auto_configure.h>
#include <acl_globals.h>
#include <acl_kernel_if.h>
#include <acl_profiler.h>
#include <acl_shipped_board_cfgs.h>
#include <acl_support.h>
#include <acl_thread.h>

#undef TEST_PROFILING_HARDWARE
#ifdef TEST_PROFILING_HARDWARE
extern int acl_hal_mmd_reset_profile_counters(unsigned int physical_device_id,
                                              unsigned int accel_id);
extern int acl_hal_mmd_get_profile_data(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t *data,
                                        unsigned int length);
extern int acl_hal_mmd_disable_profile_counters(unsigned int physical_device_id,
                                                unsigned int accel_id);
extern int acl_hal_mmd_enable_profile_counters(unsigned int physical_device_id,
                                               unsigned int accel_id);
#endif
extern int acl_process_profiler_scan_chain(acl_device_op_t *op);
extern int
acl_process_autorun_profiler_scan_chain(unsigned int physical_device_id,
                                        unsigned int accel_id);

// Versioning: This value must be read from addr 0
// For unit tests to work, this defines must match the one in the unit test
// header file
#define KERNEL_VERSION_ID (0xa0c00001)
#define KERNEL_ROM_VERSION_ID (0xa0c00002)
// Version number in the top 16-bits of the 32-bit status register.  Used
// to verify that the hardware and HAL have an identical view of the CSR
// address map.
#define CSR_VERSION_ID_18_1 (3)
#define CSR_VERSION_ID_19_1 (4)
#define CSR_VERSION_ID_2022_3 (5)
#define CSR_VERSION_ID CSR_VERSION_ID_2022_3

// Address map
// For unit tests to work, these defines must match those in the unit test
// header file
#define OFFSET_VERSION_ID ((dev_addr_t)0x0000)
#define OFFSET_KERNEL_CRA_SEGMENT ((dev_addr_t)0x0020)
#define OFFSET_SW_RESET ((dev_addr_t)0x0030)
#define OFFSET_KERNEL_CRA ((dev_addr_t)0x1000)
#define OFFSET_CONFIGURATION_ROM ((dev_addr_t)0x2000)

// Addressses for Kernel System ROM
#define OFFSET_KERNEL_ROM_LOCATION_MSB (0x3ffffff8)
#define OFFSET_KERNEL_ROM_LOCATION_LSB (0x3ffffffc)
#define OFFSET_KERNEL_MAX_ADDRESS (0x3fffffff)

#define KERNEL_CRA_SEGMENT_SIZE (0x1000)
#define KERNEL_ROM_SIZE_BYTES_READ 4
#define KERNEL_ROM_SIZE_BYTES 8

// Byte offsets into the CRA:
#define KERNEL_OFFSET_CSR 0
#define KERNEL_OFFSET_PRINTF_BUFFER_SIZE 0x4
#define KERNEL_OFFSET_CSR_PROFILE_CTRL 0xC
#define KERNEL_OFFSET_CSR_PROFILE_DATA 0x10
#define KERNEL_OFFSET_CSR_PROFILE_START_CYCLE 0x18
#define KERNEL_OFFSET_CSR_PROFILE_STOP_CYCLE 0x20
#define KERNEL_OFFSET_FINISH_COUNTER 0x28
#define KERNEL_OFFSET_INVOCATION_IMAGE 0x30

// Backwards compatibility with CSR_VERSION_ID 3
#define KERNEL_OFFSET_INVOCATION_IMAGE_181 0x28

// Bit positions
#define KERNEL_CSR_GO 0
#define KERNEL_CSR_DONE 1
#define KERNEL_CSR_STALLED 3
#define KERNEL_CSR_UNSTALL 4
#define KERNEL_CSR_PROFILE_TEMPORAL_STATUS 5
#define KERNEL_CSR_PROFILE_TEMPORAL_RESET 6
#define KERNEL_CSR_LAST_STATUS_BIT KERNEL_CSR_PROFILE_TEMPORAL_RESET
#define KERNEL_CSR_STATUS_BITS_MASK                                            \
  ((unsigned)((1 << (KERNEL_CSR_LAST_STATUS_BIT + 1)) - 1))
#define KERNEL_CSR_LMEM_INVALID_BANK 11
#define KERNEL_CSR_LSU_ACTIVE 12
#define KERNEL_CSR_WR_ACTIVE 13
#define KERNEL_CSR_BUSY 14
#define KERNEL_CSR_RUNNING 15
#define KERNEL_CSR_FIRST_VERSION_BIT 16
#define KERNEL_CSR_LAST_VERSION_BIT 31

#define KERNEL_CSR_PROFILE_SHIFT64_BIT 0
#define KERNEL_CSR_PROFILE_RESET_BIT 1
#define KERNEL_CSR_PROFILE_ALLOW_PROFILING_BIT 2
#define KERNEL_CSR_PROFILE_LOAD_BUFFER_BIT 3
#define KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1 4
#define KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2 5

#define CONFIGURATION_ROM_BYTES 4096

#define RESET_TIMEOUT (2 * 1000 * 1000 * 1000)

#define ACL_KERNEL_READ_BIT(w, b) (((w) >> (b)) & 1)
#define ACL_KERNEL_READ_BIT_RANGE(w, h, l)                                     \
  (((w) >> (l)) & ((1 << ((h) - (l) + 1)) - 1))
#define ACL_KERNEL_SET_BIT(w, b) ((w) |= (1 << (b)))
#define ACL_KERNEL_CLEAR_BIT(w, b) ((w) &= (~(1 << (b))))
#define ACL_KERNEL_GET_BIT(b) (unsigned)(1 << (b))

#define ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(k, verbosity, m, ...)                  \
  if (k->io.printf && (k->io.debug_verbosity) >= verbosity)                    \
    do {                                                                       \
      k->io.printf((m), ##__VA_ARGS__);                                        \
  } while (0)
#define ACL_KERNEL_IF_DEBUG_MSG(k, m, ...)                                     \
  if (k->io.printf && k->io.debug_verbosity > 0)                               \
    do {                                                                       \
      k->io.printf((m), ##__VA_ARGS__);                                        \
  } while (0)

//#define POLLING
//#define POLLING_PERIOD (10000) // polling period in ns

typedef time_ns time_us;

// Function declarations
static int acl_kernel_if_read_32b(acl_kernel_if *kern, unsigned int addr,
                                  unsigned int *val);
static int acl_kernel_if_read_64b(acl_kernel_if *kern, unsigned int addr,
                                  uint64_t *val);
static int acl_kernel_if_write_32b(acl_kernel_if *kern, unsigned int addr,
                                   unsigned int val);
static int acl_kernel_if_write_64b(acl_kernel_if *kern, unsigned int addr,
                                   uint64_t val);
static int acl_kernel_rom_cra_read_32b(acl_kernel_if *kern, unsigned int addr,
                                       unsigned int *val);
static int acl_kernel_rom_cra_read_block(acl_kernel_if *kern, unsigned int addr,
                                         char *config_rom, size_t size);

// **************************************************************************
// **************************** Callback Functions **************************
// **************************************************************************

acl_kernel_update_callback acl_kernel_if_update_fn = NULL;
acl_profile_callback acl_kernel_profile_fn = NULL;
acl_process_printf_buffer_callback acl_process_printf_buffer_fn = NULL;

void acl_kernel_if_register_callbacks(
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_callback,
    acl_process_printf_buffer_callback process_printf) {
  acl_assert_locked();
  acl_kernel_if_update_fn = kernel_update;
  acl_kernel_profile_fn = profile_callback;
  acl_process_printf_buffer_fn = process_printf;
}

// **************************************************************************
// **************************** Utility Functions ***************************
// **************************************************************************
// Returns 0 on success, -1 on failure
static int check_version_id(acl_kernel_if *kern) {
  unsigned int version = 0;
  int r;
  acl_assert_locked();

  r = acl_kernel_if_read_32b(kern, OFFSET_VERSION_ID, &version);
  ACL_KERNEL_IF_DEBUG_MSG(kern, "Version ID check, read: %08x\n", version);
  if (r != 0 ||
      (version != KERNEL_VERSION_ID && version != KERNEL_ROM_VERSION_ID)) {
    kern->io.printf(
        "  HAL Kern: Version mismatch! Expected 0x%x but read 0x%x\n",
        KERNEL_VERSION_ID, version);
    return -1;
  } else
    return 0;
}

// Loads auto-discovery config string. Returns 0 if was successful.
static int get_auto_discovery_string(acl_kernel_if *kern, char *config_str) {
  unsigned int version, rom_address;
  char description_size_msb[KERNEL_ROM_SIZE_BYTES_READ + 1];
  char description_size_lsb[KERNEL_ROM_SIZE_BYTES_READ + 1];
  unsigned int size_location;
  unsigned int size, temp_size, rom_size;
  size_t r;
  int result;
  acl_assert_locked();

  kern->cur_segment = 0xffffffff;

  version = 0;
  acl_kernel_if_read_32b(kern, OFFSET_VERSION_ID, &version);
  if (version == KERNEL_ROM_VERSION_ID) {
    // Read autodiscovery size
    size_location = OFFSET_KERNEL_ROM_LOCATION_MSB;
    acl_kernel_rom_cra_read_32b(kern, size_location,
                                (unsigned int *)description_size_msb);
    description_size_msb[KERNEL_ROM_SIZE_BYTES_READ] = '\0';
    size = (unsigned int)strtol(description_size_msb, NULL, 16);
    size = size << 16;

    size_location = OFFSET_KERNEL_ROM_LOCATION_LSB;
    acl_kernel_rom_cra_read_32b(kern, size_location,
                                (unsigned int *)description_size_lsb);
    description_size_lsb[KERNEL_ROM_SIZE_BYTES_READ] = '\0';
    size += (unsigned int)strtol(description_size_lsb, NULL, 16);

    // Find beginning address of ROM
    temp_size = size + KERNEL_ROM_SIZE_BYTES;
    rom_size = 1;
    while (temp_size > 0) {
      temp_size = temp_size >> 1;
      rom_size = rom_size << 1;
    }
    rom_address = OFFSET_KERNEL_MAX_ADDRESS - rom_size + 1;
    config_str[0] = '\0';
    result = acl_kernel_rom_cra_read_block(kern, rom_address, config_str,
                                           (size_t)size);

    if (result < 0)
      return -1;
    // Pad autodiscovery with NULL
    *(config_str + (unsigned int)size) = '\0';
    // Read is verified above
    // Set rom_size and r equal to support old flow and big-endian shuffle
    rom_size = size;
    r = rom_size;
  } else if (version == KERNEL_VERSION_ID) {
    rom_size = CONFIGURATION_ROM_BYTES;
    r = kern->io.read(&kern->io, (dev_addr_t)OFFSET_CONFIGURATION_ROM,
                      config_str, (size_t)CONFIGURATION_ROM_BYTES);
  } else {
    kern->io.printf("  HAL Kern: Version ID incorrect\n");
    return -1;
  }
  ACL_KERNEL_IF_DEBUG_MSG(kern, "Read %d bytes from kernel auto discovery", r);

  return (r == rom_size) ? 0 : -1;
}

int acl_kernel_if_is_valid(acl_kernel_if *kern) {
  acl_assert_locked_or_sig();
  if (kern == NULL || !acl_bsp_io_is_valid(&kern->io))
    return 0;
  return 1;
}

time_us acl_kernel_if_get_time_us(acl_kernel_if *kern) {
  acl_assert_locked_or_sig();
  // Cheaply divide by 1000 by actually dividing by 1024
  // Could be Nios II's out there without the div enabled
  return kern->io.get_time_ns() >> 10;
}

// **************************************************************************
// *************************** Read/write Functions *************************
// **************************************************************************

// 32-bit read and write calls with error checking

// returns 0 on success, -ve on error
static int acl_kernel_if_read_32b(acl_kernel_if *kern, unsigned int addr,
                                  unsigned int *val) {
  size_t size;
  size_t r;
  acl_assert_locked_or_sig();

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

  size = sizeof(unsigned int);
  r = kern->io.read(&kern->io, (dev_addr_t)addr, (char *)val, (size_t)size);
  if (r < size) {
    kern->io.printf(
        "HAL Kern Error: Read failed from addr %x, read %d expected %d\n", addr,
        r, size);
    return -1;
  }
  return 0;
}

// 64-bit read and write calls with error checking

// returns 0 on success, -ve on error
static int acl_kernel_if_read_64b(acl_kernel_if *kern, unsigned int addr,
                                  uint64_t *val) {
  int size;
  int r;
  acl_assert_locked_or_sig();

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

  size = sizeof(uint64_t);
  r = (int)kern->io.read(&kern->io, (dev_addr_t)addr, (char *)val,
                         (size_t)size);
  if (r < size) {
    kern->io.printf(
        "HAL Kern Error: Read failed from addr %x, read %d expected %d\n", addr,
        r, size);
    return -1;
  }
  return 0;
}

// returns 0 on success, -ve on error
static int acl_kernel_rom_read_block(acl_kernel_if *kern, unsigned int addr,
                                     char *config_rom, size_t size) {
  size_t r;

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

  r = kern->io.read(&kern->io, (dev_addr_t)addr, config_rom, (size_t)size);
  if (r < size) {
    kern->io.printf(
        "HAL Kern Error: Read failed from addr %x, read %d expected %d\n", addr,
        r, size);
    return -1;
  }
  return 0;
}

// returns 0 on success, -ve on error
static int acl_kernel_if_write_32b(acl_kernel_if *kern, unsigned int addr,
                                   unsigned int val) {
  int size;
  int r;
  acl_assert_locked_or_sig();

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

  size = sizeof(unsigned int);
  r = (int)kern->io.write(&kern->io, (dev_addr_t)addr, (char *)&val,
                          (size_t)size);
  if (r < size) {
    kern->io.printf("HAL Kern Error: Write failed to addr %x with value %x, "
                    "wrote %d expected %d\n",
                    addr, val, r, size);
    return -1;
  }
  return 0;
}

// returns 0 on success, -ve on error
static int acl_kernel_if_write_64b(acl_kernel_if *kern, unsigned int addr,
                                   uint64_t val) {
  int size;
  int r;
  acl_assert_locked();

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

  size = sizeof(uint64_t);
  r = (int)kern->io.write(&kern->io, (dev_addr_t)addr, (char *)&val,
                          (size_t)size);
  if (r < size) {
    kern->io.printf("HAL Kern Error: Write failed to addr %x with value %x, "
                    "wrote %d, expected %d\n",
                    addr, val, r, size);
    return -1;
  }
  return 0;
}

// return 0 on success, -ve on error
static int acl_kernel_if_write_block(acl_kernel_if *kern, unsigned int addr,
                                     unsigned int *val, size_t size) {
  size_t r;
  unsigned int *temp_val = val;
  size_t aligned_size = size;

  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Invalid kernel handle used");
    return -1;
  }

// In Windows S5 MMD, block writes to kernel interface must have write size
// divisible by 4 bytes If not divisible by 4, subtract the modulos and add 4
// bytes to the write size.
#ifdef _WIN32
  if (size % 4) {
    aligned_size = size - (size % 4) + 4;
    temp_val = (unsigned int *)acl_malloc(aligned_size + 1);
    memcpy(temp_val, val, size);
  }
#endif

  r = kern->io.write(&kern->io, (dev_addr_t)addr, (char *)temp_val,
                     (size_t)aligned_size);

#ifdef _WIN32
  if (size % 4) {
    acl_free(temp_val);
  }
#endif

  if (r < aligned_size) {
    kern->io.printf("HAL Kern Error: Write failed to addr %x with value %x, "
                    "wrote %d expected $d\n",
                    addr, val, r, size);
    return -1;
  }
  return 0;
}

// The kernel cra is segmented (or windowed) using a Qsys address expander.
// We must first write the upper bits of the address we want to access to the
// extender, then perform our read/write operation using the offset within
// that segment as the address.
//
// Sets the segment window bits and returns the offset needed within
static uintptr_t acl_kernel_cra_set_segment(acl_kernel_if *kern,
                                            unsigned int accel_id,
                                            unsigned int addr) {
  uintptr_t logical_addr =
      kern->accel_csr[accel_id].address + addr - OFFSET_KERNEL_CRA;
  uintptr_t segment = logical_addr & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));
  uintptr_t segment_offset = logical_addr % KERNEL_CRA_SEGMENT_SIZE;
  acl_assert_locked_or_sig();

  // The kernel cra master is hardcoded to 30 addr bits, so we can use 32-bit
  // interface here.
  if (kern->cur_segment != segment) {
    acl_kernel_if_write_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                            (unsigned int)segment);
    kern->cur_segment = segment;
    acl_kernel_if_read_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                           (unsigned int *)&segment);
  }

  return segment_offset;
}

// Set CRA segment for ROM. No accel_id
static uintptr_t acl_kernel_cra_set_segment_rom(acl_kernel_if *kern,
                                                unsigned int addr) {
  uintptr_t segment = addr & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));
  uintptr_t segment_offset = addr % KERNEL_CRA_SEGMENT_SIZE;

  if (kern->cur_segment != segment) {
    acl_kernel_if_write_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                            (unsigned int)segment);
    kern->cur_segment = segment;
    acl_kernel_if_read_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                           (unsigned int *)&segment);
  }

  return segment_offset;
}

static int acl_kernel_cra_read(acl_kernel_if *kern, unsigned int accel_id,
                               unsigned int addr, unsigned int *val) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment(kern, accel_id, addr);
  acl_assert_locked_or_sig();
  return acl_kernel_if_read_32b(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val);
}

int acl_kernel_cra_read_64b(acl_kernel_if *kern, unsigned int accel_id,
                            unsigned int addr, uint64_t *val) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment(kern, accel_id, addr);
  acl_assert_locked_or_sig();
  return acl_kernel_if_read_64b(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val);
}

// Read 32b from kernel ROM
static int acl_kernel_rom_cra_read_32b(acl_kernel_if *kern, unsigned int addr,
                                       unsigned int *val) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment_rom(kern, addr);
  return acl_kernel_if_read_32b(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val);
}

// Read size number of bytes from ROM
static int acl_kernel_rom_cra_read_block(acl_kernel_if *kern, unsigned int addr,
                                         char *config_rom, size_t size) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment_rom(kern, addr);
  uintptr_t segment = addr & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));
  uintptr_t segment_end =
      (addr + size) & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));

  unsigned int step = 0;
  unsigned int transfer = 0;
  int r = 0;
  if (segment != segment_end) {
    while (step < size) {
      transfer = ((unsigned int)size > (step + KERNEL_CRA_SEGMENT_SIZE))
                     ? KERNEL_CRA_SEGMENT_SIZE
                     : (unsigned int)size - step;

      r = acl_kernel_rom_read_block(
          kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset,
          config_rom, transfer);
      config_rom += transfer;
      step += transfer;
      segment_offset = acl_kernel_cra_set_segment_rom(kern, addr + step);
      if (r < 0)
        return r;
    }
  } else {
    r = acl_kernel_rom_read_block(
        kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset,
        config_rom, size);
  }

  return r;
}

static int acl_kernel_cra_write(acl_kernel_if *kern, unsigned int accel_id,
                                unsigned int addr, unsigned int val) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment(kern, accel_id, addr);
  acl_assert_locked_or_sig();
  return acl_kernel_if_write_32b(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val);
}

static int acl_kernel_cra_write_64b(acl_kernel_if *kern, unsigned int accel_id,
                                    unsigned int addr, uint64_t val) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment(kern, accel_id, addr);
  acl_assert_locked();
  return acl_kernel_if_write_64b(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val);
}

static int acl_kernel_cra_write_block(acl_kernel_if *kern,
                                      unsigned int accel_id, unsigned int addr,
                                      unsigned int *val, size_t size) {
  uintptr_t segment_offset = acl_kernel_cra_set_segment(kern, accel_id, addr);
  uintptr_t logical_addr =
      kern->accel_csr[accel_id].address + addr - OFFSET_KERNEL_CRA;
  uintptr_t segment = logical_addr & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));

  uintptr_t logical_addr_end =
      kern->accel_csr[accel_id].address + addr + size - OFFSET_KERNEL_CRA;
  uintptr_t segment_end =
      logical_addr_end & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));

  unsigned int step = 0;
  if (segment != segment_end) {
    ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(
        kern, 2, ":: Segment change during block write detected.\n");
    while (step < size) {
      segment = (logical_addr + step) & ((size_t)0 - (KERNEL_CRA_SEGMENT_SIZE));
      if (kern->cur_segment != segment) {
        acl_kernel_if_write_block(
            kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val,
            step);
        segment_offset =
            acl_kernel_cra_set_segment(kern, accel_id, addr + step);
        logical_addr =
            kern->accel_csr[accel_id].address + addr + step - OFFSET_KERNEL_CRA;
        val += step;
        size -= step;
        step = 0;
      } else {
        step += (unsigned)sizeof(int);
      }
    }
  }

  return acl_kernel_if_write_block(
      kern, (unsigned)OFFSET_KERNEL_CRA + (unsigned)segment_offset, val, size);
}

// Private utility function to issue a command to the profile hardware
static int acl_kernel_if_issue_profile_hw_command(acl_kernel_if *kern,
                                                  cl_uint accel_id,
                                                  unsigned bit_id,
                                                  int set_bit) {
  unsigned int profile_ctrl_val;
  int status;
  acl_assert_locked_or_sig();
  assert(acl_kernel_if_is_valid(kern));
  status = acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_CTRL,
                               &profile_ctrl_val);
  if (status)
    return status;
  ACL_KERNEL_IF_DEBUG_MSG(
      kern, "::   Issue profile HW command:: Accelerator %d old csr is %x.\n",
      accel_id, profile_ctrl_val);

  if (set_bit) {
    ACL_KERNEL_SET_BIT(profile_ctrl_val, bit_id);
  } else { // Clear bit
    ACL_KERNEL_CLEAR_BIT(profile_ctrl_val, bit_id);
  }
  ACL_KERNEL_IF_DEBUG_MSG(
      kern, "::   Issue profile HW command:: Accelerator %d new csr is %x.\n",
      accel_id, profile_ctrl_val);
  status = acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_CTRL,
                                profile_ctrl_val);
  if (status)
    return status;
  return 0;
}

// Private utility function to issue a command to the profile hardware
// See ip/src/common/lsu_top.v.tmpl, ip/src/common/hld_iord.sv.tmpl, &
// ip/src/common/hld_iowr.sv.tmpl for what 0-3 represents
int acl_kernel_if_set_profile_shared_control(acl_kernel_if *kern,
                                             cl_uint accel_id) {
  acl_assert_locked();
  ACL_KERNEL_IF_DEBUG_MSG(
      kern,
      ":: Set the shared control for the profile counters:: Accelerator %d.\n",
      accel_id);
  int return_val = 0;
  int set_to = get_env_profile_shared_counter_val();

  // Valid control options are 0-3. If set to -1 shared counters are off.
  // Anything else the control hasn't been pulled from the ENV variable
  if (set_to >= 0 && set_to <= 3) {
    if (set_to == 0) {
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1, 0);
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2, 0);
    } else if (set_to == 1) {
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1, 1);
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2, 0);
    } else if (set_to == 2) {
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1, 0);
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2, 1);
    } else if (set_to == 3) {
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1, 1);
      return_val |= acl_kernel_if_issue_profile_hw_command(
          kern, accel_id, KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2, 1);
    } else {
      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              ":: Setting shared control value - env variable "
                              "was not in the range 0-3 %d.\n",
                              accel_id);
    }
  } else if (set_to == -1) {
    ACL_KERNEL_IF_DEBUG_MSG(kern,
                            ":: Not setting shared control value - shared "
                            "counters not enabled %d.\n",
                            accel_id);
  } else {
    ACL_KERNEL_IF_DEBUG_MSG(
        kern,
        ":: Not setting shared control value - env variable was not set %d.\n",
        accel_id);
  }
  return return_val;
}

// **************************************************************************
// ************** Utility Functions that use Read/Write calls ***************
// **************************************************************************

// **************************************************************************
// ***************************** Public Functions ***************************
// **************************************************************************

// This routine queries the PCIe bus for devices that match our ACL supported
// board configurations and populates the acl_system_def and
// ACL_PCIE_DEVICE_DESRIPTION structures in the ACL and HAL layers respectively.
//
// Returns 0 on success, -ve otherwise
int acl_kernel_if_init(acl_kernel_if *kern, acl_bsp_io bsp_io,
                       acl_system_def_t *sysdef) {
  char description_size_msb[KERNEL_ROM_SIZE_BYTES_READ + 1];
  char description_size_lsb[KERNEL_ROM_SIZE_BYTES_READ + 1];
  unsigned int size_location, version, size;
  int result = 0;
  int use_offline_only = 0;
  acl_assert_locked();

  assert(acl_bsp_io_is_valid(&bsp_io));
  kern->io = bsp_io;

  kern->num_accel = 0;
  kern->cur_segment = 0xffffffff;

  kern->accel_csr = NULL;
  kern->accel_perf_mon = NULL;
  kern->accel_num_printfs = NULL;

  kern->csr_version = 0;

  kern->autorun_profiling_kernel_id = -1;

  // The simulator doesn't have any kernel interface information until the aocx
  // is loaded, which happens later.
  acl_get_offline_device_user_setting(&use_offline_only);
  if (use_offline_only == ACL_CONTEXT_MPSIM) {
    std::string err_msg;
    auto parse_result = acl_load_device_def_from_str(
        std::string(acl_shipped_board_cfgs[1]),
        sysdef->device[0].autodiscovery_def, err_msg);
    // Fill in definition for all device global memory
    // Simulator does not have any global memory interface information until the
    // actual aocx is loaded. (Note this is only a problem for simulator not
    // hardware run, in hardware run, we can communicate with BSP to query
    // memory interface information). In the flow today, the USM device
    // allocation call happens before aocx is loaded. The aocx is loaded when
    // clCreateProgram is called, which typically happen on first kernel launch
    // in sycl runtime. In order to prevent the USM device allocation from
    // failing on  mutli global memory system, initialize as much global memory
    // system as possible for simulation flow. However there are a few downside:
    // 1. The address range/size may not be exactly the same as the one that is
    // in aocx, but this is not too large of a problem because runtime first fit
    // allocation algorithm will fill the lowest address range first. Unless
    // user requested more than what is availble.
    // 2. it potentially occupied more space than required
    // 3. will not error out when user requested a non-existing device global
    // memory because we are using ACL_MAX_GLOBAL_MEM for num_global_mem_systems
    sysdef->device[0].autodiscovery_def.num_global_mem_systems =
        ACL_MAX_GLOBAL_MEM;
    for (int i = 0; i < ACL_MAX_GLOBAL_MEM; i++) {
      sysdef->device[0].autodiscovery_def.global_mem_defs[i] =
          sysdef->device[0].autodiscovery_def.global_mem_defs[0];
    }
    if (parse_result)
      sysdef->num_devices = 1;
    // Override the device name to the simulator.
    sysdef->device[0].autodiscovery_def.name = ACL_MPSIM_DEVICE_NAME;
    return 0;
  }

  if (check_version_id(kern) != 0) {
    kern->io.printf("Hardware version ID differs from version expected by "
                    "software.  Either:\n");
    kern->io.printf("   a) Ensure your compiled design was generated by the "
                    "same ACL build\n");
    kern->io.printf("      currently in use, OR\n");
    kern->io.printf(
        "   b) The host can not communicate with the compiled kernel.\n");
    assert(0);
  }

  version = 0;
  acl_kernel_if_read_32b(kern, OFFSET_VERSION_ID, &version);
  ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(kern, 2, "::   Kernel version 0x%x\n",
                                  version);
  size_t config_str_len = 0;
  if (version == KERNEL_ROM_VERSION_ID) {
    acl_kernel_if_reset(kern);
    size_location = OFFSET_KERNEL_ROM_LOCATION_MSB;
    result = acl_kernel_rom_cra_read_32b(kern, size_location,
                                         (unsigned int *)description_size_msb);
    description_size_msb[KERNEL_ROM_SIZE_BYTES_READ] = '\0';

    if (result < 0 || ((unsigned int)*description_size_msb == 0xFFFF)) {
      ACL_KERNEL_IF_DEBUG_MSG(
          kern, "MSB ROM size not found. No response from PCIe.\n");
      return -1;
    }

    size = (unsigned int)strtol(description_size_msb, NULL, 16);
    size = size << 16;

    size_location = OFFSET_KERNEL_ROM_LOCATION_LSB;
    result = acl_kernel_rom_cra_read_32b(kern, size_location,
                                         (unsigned int *)description_size_lsb);
    description_size_lsb[KERNEL_ROM_SIZE_BYTES_READ] = '\0';

    if (result < 0 || ((unsigned int)*description_size_lsb == 0xFFFF)) {
      ACL_KERNEL_IF_DEBUG_MSG(
          kern, "LSB ROM size not found. No response from PCIe.\n");
      return -1;
    }

    size += (unsigned int)strtol(description_size_lsb, NULL, 16);

    if ((size == 0) || (size == 0xffffffff)) {
      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              "ROM size is invalid. ROM does not exist\n");
      ACL_KERNEL_IF_DEBUG_MSG(kern, "ROM size read was: %i\n", size);
      return -1;
    }

    ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(kern, 2, "::   ROM Size is: 0x%s%s : %i\n",
                                    description_size_msb, description_size_lsb,
                                    size);

    config_str_len = size + 1;
  } else if (version == KERNEL_VERSION_ID) {
    config_str_len = CONFIGURATION_ROM_BYTES + 1;
  } else {
    kern->io.printf("  HAL Kern: Version ID incorrect\n");
    return -1;
  }

  std::vector<char> config_str(config_str_len);
  result |= get_auto_discovery_string(kern, config_str.data());
  std::string config_string{config_str.data()};

  if (result != 0) {
    ACL_KERNEL_IF_DEBUG_MSG(kern, "Failed to read from kernel auto discovery");
    return -1;
  }
  ACL_KERNEL_IF_DEBUG_MSG(kern, "\nPCIE Auto-Discovered Param String: %s\n",
                          config_str.data());

  // Returns 1 if success
  std::string auto_config_err_str;
  auto load_result = acl_load_device_def_from_str(
      config_string, sysdef->device[kern->physical_device_id].autodiscovery_def,
      auto_config_err_str);
  if (load_result)
    ++sysdef->num_devices;
  result |= load_result ? 0 : -1;

  if (result != 0) {
    kern->io.printf("%s\n", auto_config_err_str.c_str());
    ACL_KERNEL_IF_DEBUG_MSG(kern, "First 16 values:\n  ");
    for (unsigned i = 0; i < 16; i++)
      ACL_KERNEL_IF_DEBUG_MSG(kern, "%02x ", config_str[i]);
    ACL_KERNEL_IF_DEBUG_MSG(kern, "\n");
    return -1;
  }

  result = acl_kernel_if_update(
      sysdef->device[kern->physical_device_id].autodiscovery_def, kern);

  return result;
}

// Given a devdef, update the kernel's internal state
// Returns 0 on success
int acl_kernel_if_update(const acl_device_def_autodiscovery_t &devdef,
                         acl_kernel_if *kern) {
  acl_assert_locked();

  // Setup the accelerators
  if (kern->io.debug_verbosity > 0) {
    for (const auto &acc : devdef.accel) {
      ACL_KERNEL_IF_DEBUG_MSG(kern, "Found Kernel {%s}\n",
                              acc.iface.name.c_str());
      // Show info about the arguments themselves
      for (const auto &arg : acc.iface.args) {
        // These are strictly for debug, but we need a way to keep this up to
        // date
        static const char *acl_addr_space_names[] = {
            "ACL_ARG_ADDR_NONE", "ACL_ARG_ADDR_LOCAL", "ACL_ARG_ADDR_GLOBAL",
            "ACL_ARG_ADDR_CONSTANT"};
        static const char *acl_arg_categories[] = {
            "ACL_ARG_BY_VALUE", "ACL_ARG_MEM_OBJ", "ACL_ARG_SAMPLER"};

        ACL_KERNEL_IF_DEBUG_MSG(
            kern, " ... param %s (addr_space=%s,category=%s,size=%d)\n",
            arg.name.c_str(), acl_addr_space_names[arg.addr_space],
            acl_arg_categories[arg.category], arg.size);
      }

      // Show other info about kernel.
      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              " ... compile work-group size = (%d, %d, %d)\n",
                              (int)acc.compile_work_group_size[0],
                              (int)acc.compile_work_group_size[1],
                              (int)acc.compile_work_group_size[2]);
      ACL_KERNEL_IF_DEBUG_MSG(kern, " ... is work-group invariant = %d\n",
                              acc.is_workgroup_invariant);
      ACL_KERNEL_IF_DEBUG_MSG(kern, " ... num vectors lanes = %d\n",
                              acc.num_vector_lanes);
      ACL_KERNEL_IF_DEBUG_MSG(kern, " ... max work-group size = %d\n",
                              acc.max_work_group_size);
    }
  }

  acl_kernel_if_close(kern);

  // Setup the PCIe HAL Structures
  kern->num_accel = static_cast<unsigned>(devdef.accel.size());
  ACL_KERNEL_IF_DEBUG_MSG(kern, "Number of Accelerators : %d\n",
                          kern->num_accel);

  if (kern->num_accel > 0) {
    // Allocations for each kernel
    kern->accel_csr = (acl_kernel_if_addr_range *)acl_malloc(
        kern->num_accel * sizeof(acl_kernel_if_addr_range));
    assert(kern->accel_csr);
    kern->accel_perf_mon = (acl_kernel_if_addr_range *)acl_malloc(
        kern->num_accel * sizeof(acl_kernel_if_addr_range));
    assert(kern->accel_perf_mon);
    kern->accel_num_printfs =
        (unsigned int *)acl_malloc(kern->num_accel * sizeof(unsigned int));
    assert(kern->accel_num_printfs);

    // The Kernel CSR registers
    // The new and improved config ROM give us the address *offsets* from
    // the first kernel CSR, and the range of each kernel CSR.
    for (unsigned ii = 0; ii < devdef.accel.size(); ++ii) {
      kern->accel_csr[ii].address =
          OFFSET_KERNEL_CRA + devdef.hal_info[ii].csr.address;
      kern->accel_csr[ii].bytes = devdef.hal_info[ii].csr.num_bytes;

      ACL_KERNEL_IF_DEBUG_MSG(kern, "Kernel_%s CSR { 0x%08x, 0x%08x }\n",
                              devdef.accel[ii].iface.name.c_str(),
                              kern->accel_csr[ii].address,
                              kern->accel_csr[ii].bytes);
    }

    // The Kernel performance monitor registers
    for (unsigned ii = 0; ii < devdef.accel.size(); ++ii) {
      kern->accel_perf_mon[ii].address =
          OFFSET_KERNEL_CRA + devdef.hal_info[ii].perf_mon.address;
      kern->accel_perf_mon[ii].bytes = devdef.hal_info[ii].perf_mon.num_bytes;

      ACL_KERNEL_IF_DEBUG_MSG(kern, "Kernel_%s perf_mon { 0x%08x, 0x%08x }\n",
                              devdef.accel[ii].iface.name.c_str(),
                              kern->accel_perf_mon[ii].address,
                              kern->accel_perf_mon[ii].bytes);

      // printf info
      kern->accel_num_printfs[ii] =
          static_cast<unsigned>(devdef.accel[ii].printf_format_info.size());

      ACL_KERNEL_IF_DEBUG_MSG(kern, "Kernel_%s(id=%d) num_printfs is %d\n",
                              devdef.accel[ii].iface.name.c_str(), ii,
                              kern->accel_num_printfs[ii]);
    }

    // Find which of the kernels (if any) is the autorun profiling kernel
    for (unsigned ii = 0; ii < devdef.accel.size(); ++ii) {
      if (devdef.accel[ii].iface.name == ACL_PROFILE_AUTORUN_KERNEL_NAME) {
        kern->autorun_profiling_kernel_id = (int)ii;
        break;
      }
    }
  }

  // Do reset
  acl_kernel_if_reset(kern);

  // Set interleaving mode based on autodiscovery
  for (unsigned ii = 0; ii < devdef.num_global_mem_systems; ii++) {
    if ((unsigned int)devdef.global_mem_defs[ii].config_addr > 0 &&
        (unsigned int)devdef.global_mem_defs[ii].num_global_banks > 1) {
      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              "Configuring interleaving for memory %d (%s), "
                              "burst_interleaved = %d\n",
                              ii, devdef.global_mem_defs[ii].name.c_str(),
                              devdef.global_mem_defs[ii].burst_interleaved);
      acl_kernel_if_write_32b(
          kern, (unsigned int)devdef.global_mem_defs[ii].config_addr,
          devdef.global_mem_defs[ii].burst_interleaved ? 0x0 : 0x1u);
    }
  }

  kern->last_kern_update = 0;

  // Set up the structures to store state information about the device
  if (kern->num_accel > 0) {
    kern->accel_job_ids =
        (int volatile **)acl_malloc(kern->num_accel * sizeof(int *));
    assert(kern->accel_job_ids);

    kern->accel_invoc_queue_depth =
        (unsigned int *)acl_malloc(kern->num_accel * sizeof(unsigned int));
    assert(kern->accel_invoc_queue_depth);

    // Kernel IRQ is a separate thread. Need to use circular buffer to make this
    // multithread safe.
    kern->accel_queue_front = (int *)acl_malloc(kern->num_accel * sizeof(int));
    assert(kern->accel_queue_front);
    kern->accel_queue_back = (int *)acl_malloc(kern->num_accel * sizeof(int));
    assert(kern->accel_queue_back);

    for (unsigned a = 0; a < kern->num_accel; ++a) {
      unsigned int max_same_accel_launches =
          devdef.accel[a].fast_launch_depth + 1;
      // +1, because fast launch depth does not account for the running kernel
      kern->accel_invoc_queue_depth[a] = max_same_accel_launches;
      kern->accel_job_ids[a] =
          (int *)acl_malloc(max_same_accel_launches * sizeof(int));
      kern->accel_queue_front[a] = -1;
      kern->accel_queue_back[a] = -1;
      for (unsigned b = 0; b < max_same_accel_launches; ++b) {
        kern->accel_job_ids[a][b] = -1;
      }
    }
  }

  return 0;
}

// Post-PLL config init function - at this point it's safe to talk to
// the kernel CSR registers.
// Returns 0 on success
int acl_kernel_if_post_pll_config_init(acl_kernel_if *kern) {
  unsigned int csr, version;
  unsigned k;
  char *profile_start_cycle = getenv("ACL_PROFILE_START_CYCLE");
  char *profile_stop_cycle = getenv("ACL_PROFILE_STOP_CYCLE");
  acl_assert_locked();

  // Readback kernel version from status register and store in kernel data
  // structure.  We cache this value now so that don't do PCIe readback on
  // every kernel launch (when we actually check the value).
  // Safe to read only first kernel because all will have the same version
  if (!acl_kernel_if_is_valid(kern)) {
    kern->io.printf("HAL Kern Error: Post PLL init: Invalid kernel handle");
    assert(0 && "Invalid kernel handle");
  }
  if (kern->num_accel > 0) {
    acl_kernel_cra_read(kern, 0, KERNEL_OFFSET_CSR, &csr);
    version = ACL_KERNEL_READ_BIT_RANGE(csr, KERNEL_CSR_LAST_VERSION_BIT,
                                        KERNEL_CSR_FIRST_VERSION_BIT);
    kern->csr_version = version;
    ACL_KERNEL_IF_DEBUG_MSG(kern,
                            "Read CSR version from kernel 0: Version = %u\n",
                            kern->csr_version);
  } else {
    kern->csr_version = 0;
  }

  // If environment variables set, configure the profile hardware
  // start/stop cycle registers for *every* kernel.  The runtime can then
  // call into the HAL to override values for the kernels individually.
  if (profile_start_cycle) {
#ifdef _WIN32
    uint64_t start_cycle =
        (uint64_t)_strtoui64(profile_start_cycle, NULL, 10); // Base 10
    kern->io.printf(
        "Setting profiler start cycle from environment variable: %I64u\n",
        start_cycle);
#else // Linux
    uint64_t start_cycle =
        (uint64_t)strtoull(profile_start_cycle, NULL, 10); // Base 10
    kern->io.printf(
        "Setting profiler start cycle from environment variable: %llu\n",
        start_cycle);
#endif
    for (k = 0; k < kern->num_accel; ++k) {
      if (acl_kernel_if_set_profile_start_cycle(kern, k, start_cycle)) {
        kern->io.printf("Failed setting profiler start cycle for kernel %u\n",
                        k);
        return 1;
      }
    }
  }
  if (profile_stop_cycle) {
#ifdef _WIN32
    uint64_t stop_cycle =
        (uint64_t)_strtoui64(profile_stop_cycle, NULL, 10); // Base 10
    kern->io.printf(
        "Setting profiler stop cycle from environment variable: %I64u\n",
        stop_cycle);
#else // Linux
    uint64_t stop_cycle =
        (uint64_t)strtoull(profile_stop_cycle, NULL, 10); // Base 10
    kern->io.printf(
        "Setting profiler stop cycle from environment variable: %llu\n",
        stop_cycle);
#endif
    for (k = 0; k < kern->num_accel; ++k) {
      if (acl_kernel_if_set_profile_stop_cycle(kern, k, stop_cycle)) {
        kern->io.printf("Failed setting profiler stop cycle for kernel %u\n",
                        k);
        return 1;
      }
    }
  }

  // Set the profiler shared control and mark the start time if kern has autorun
  // profiler kernel
  if (kern->autorun_profiling_kernel_id != -1) {
    kern->io.printf(
        "kernel space has profiler kernel, setting shared control\n");
    // get the variable from ENV and set the profiler global var
    set_env_shared_counter_val();
    // set the profiler kernel's CRA shared control
    acl_kernel_if_set_profile_shared_control(
        kern, cl_uint(kern->autorun_profiling_kernel_id));
    // Reset the counters now that the shared control is set
    acl_kernel_if_reset_profile_counters(
        kern, cl_uint(kern->autorun_profiling_kernel_id));
    // Record the start time for these autorun kernels
    acl_set_autorun_start_time();
  }
  return 0;
}

void acl_kernel_if_reset(acl_kernel_if *kern) {
  unsigned int sw_resetn = 0; // Any write will cause reset - data is don't care
  time_ns reset_time;
  acl_assert_locked();

  ACL_KERNEL_IF_DEBUG_MSG(kern, " KERNEL: Issuing kernel reset\n");

  acl_kernel_if_write_32b(kern, OFFSET_SW_RESET, sw_resetn);

  // This will stall while circuit is being reset.  Executing this read
  // therefore ensures this circuit has come out of reset before proceeding.
  acl_kernel_if_read_32b(kern, OFFSET_SW_RESET, &sw_resetn);

  // Just in case, verify that value readback shows reset deasserted
  reset_time = kern->io.get_time_ns();
  while (sw_resetn == 0) {
    if (kern->io.get_time_ns() - reset_time > (time_ns)(RESET_TIMEOUT)) {
      kern->io.printf("Kernel failed to come out of reset. Read 0x%x\n",
                      sw_resetn);
      assert(sw_resetn != 0);
    }

    acl_kernel_if_read_32b(kern, OFFSET_SW_RESET, &sw_resetn);
  }
}

// Launch a task on the specified kernel
// It is assumed that the kernel is idle and ready to accept new tasks.
void acl_kernel_if_launch_kernel_on_custom_sof(
    acl_kernel_if *kern, cl_uint accel_id,
    acl_dev_kernel_invocation_image_t *image, cl_int activation_id) {
  acl_assert_locked();

  // Enforce CSR register map version number between the HAL and hardware.
  // Version is defined in upper 16-bits of the status register and cached
  // in the kern->csr_version field.  The value is cached after PLL init to
  // avoid reading back on every kernel launch, which would add overhead.
  if (!(kern->csr_version >= CSR_VERSION_ID_18_1 &&
        kern->csr_version <= CSR_VERSION_ID)) {
    kern->io.printf("Hardware CSR version ID differs from version expected by "
                    "software.  Either:\n");
    kern->io.printf("   a) Ensure your compiled design was generated by the "
                    "same ACL build\n");
    kern->io.printf("      currently in use, OR\n");
    kern->io.printf("   b) The host can not communicate properly with the "
                    "compiled kernel.\n");
    kern->io.printf("Saw version=%u, expected=%u.\n", kern->csr_version,
                    CSR_VERSION_ID);
    assert(0); // Assert here because no way to pass an error up to the user.
               // clEnqueue has already returned.
  }

  ACL_KERNEL_IF_DEBUG_MSG(
      kern, ":: Launching kernel %d on accelerator %d, device %d.\n",
      activation_id, accel_id, kern->physical_device_id);

  // Reset our expectations for timeout
  kern->last_kern_update = acl_kernel_if_get_time_us(kern);

  // Assert that the requested accellerator is currently empty
  int next_launch_index = 0;
  if (kern->accel_queue_front[accel_id] ==
      (int)kern->accel_invoc_queue_depth[accel_id] - 1) {
    next_launch_index = 0;
  } else {
    next_launch_index = kern->accel_queue_front[accel_id] + 1;
  }
  if (kern->accel_job_ids[accel_id][next_launch_index] >= 0) {
    kern->io.printf(
        "Kernel launch requested when kernel not idle on accelerator %d\n",
        accel_id);
    kern->io.printf("   kernel physical id = %d\n", kern->physical_device_id);
    assert(0);
  }

  // Option 3 CSRs start with the work_dim data member,
  // and continue with the rest of the invocation image.
  uintptr_t image_p;

  // Set the shared control for the profiler
  acl_kernel_if_set_profile_shared_control(kern, accel_id);

  size_t image_size_static;
  unsigned int offset;
  acl_dev_kernel_invocation_image_181_t image_181;
  if (kern->csr_version == CSR_VERSION_ID_18_1) {
    offset = (unsigned int)KERNEL_OFFSET_INVOCATION_IMAGE_181;
    image_p = (uintptr_t)&image_181;
    image_size_static =
        (size_t)((uintptr_t) & (image_181.arg_value) - (uintptr_t)&image_181) +
        image->arg_value_size;

    // Copy all relevant attributes from the default invocation image to
    // the 18.1 compatible invocation image.
    image_181.work_dim = image->work_dim;
    image_181.work_group_size = image->work_group_size;
    for (unsigned i = 0; i < 3; ++i) {
      image_181.global_work_size[i] = image->global_work_size[i];
      image_181.num_groups[i] = image->num_groups[i];
      image_181.local_work_size[i] = image->local_work_size[i];
      // Always 0 for 18.1 CRAs since global_work_offset was not supported in
      // that version.
      image_181.global_work_offset[i] = 0;
    }
    for (cl_uint i = 0; i < image->arg_value_size; ++i) {
      image_181.arg_value[i] = image->arg_value[i];
    }

  } else {
    offset = (unsigned int)KERNEL_OFFSET_INVOCATION_IMAGE;
    image_p = (uintptr_t) & (image->work_dim);
    image_size_static =
        (size_t)((uintptr_t) & (image->arg_value) - (uintptr_t) &
                 (image->work_dim));
  }

  if ((kern->io.debug_verbosity) >= 2) {
    for (uintptr_t p = 0; p < image_size_static; p += sizeof(int)) {
      unsigned int pword = *(unsigned int *)(image_p + p);
      ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(
          kern, 2, "::   Writing inv image [%2d] @%8p := %4x\n", (int)(p),
          (void *)(offset + p), pword);
    }

    if (kern->csr_version != CSR_VERSION_ID_18_1) {
      for (uintptr_t p = 0; p < image->arg_value_size; p += sizeof(int)) {
        unsigned int pword = *(unsigned int *)(image->arg_value + p);
        ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(
            kern, 2, "::   Writing inv image [%2d] @%8p := %4x\n", (int)(p),
            (void *)(offset + image_size_static + p), pword);
      }
    }
  }

  // When csr version is 18.1, the kernel args is part of the image. otherwise,
  // it is in dynamic memory
  acl_kernel_cra_write_block(kern, accel_id, offset, (unsigned int *)image_p,
                             image_size_static);
  if (kern->csr_version != CSR_VERSION_ID_18_1) {
    acl_kernel_cra_write_block(
        kern, accel_id, offset + (unsigned int)image_size_static,
        (unsigned int *)image->arg_value, image->arg_value_size);
  }

  kern->accel_job_ids[accel_id][next_launch_index] = (int)activation_id;

  // If kernel IRQ comes at this point for this kernel and this kernel was
  // next to launch, its status will be set to CL_RUNNING and below call
  // to update status will do nothing
  if (kern->accel_queue_front[accel_id] == kern->accel_queue_back[accel_id]) {
    acl_kernel_if_update_fn((int)(activation_id), CL_RUNNING);
  }
  kern->accel_queue_front[accel_id] = next_launch_index;

  unsigned int new_csr = 0;
  acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &new_csr);
  ACL_KERNEL_SET_BIT(new_csr, KERNEL_CSR_GO);
  acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR, new_csr);

  // IRQ handler takes care of the completion event through
  // acl_kernel_if_update_status()
}

void acl_kernel_if_launch_kernel(acl_kernel_if *kern,
                                 acl_kernel_invocation_wrapper_t *wrapper) {
  cl_int activation_id;
  cl_uint accel_id;
  acl_dev_kernel_invocation_image_t *image;
  acl_assert_locked();

  // Verify the callbacks are valid
  assert(acl_kernel_if_update_fn != NULL);
  assert(acl_process_printf_buffer_fn != NULL);

  // Grab the parameters we are interested in from host memory
  image = wrapper->image;
  activation_id = image->activation_id;
  accel_id = image->accel_id;

  acl_kernel_if_launch_kernel_on_custom_sof(kern, accel_id, image,
                                            activation_id);
}

// Queries status of pending kernel invocations. Returns the number of finished
// kernel invocations in `finish_counter`, or 0 if no invocations have finished.
static void acl_kernel_if_update_status_query(acl_kernel_if *kern,
                                              const unsigned int accel_id,
                                              const int activation_id,
                                              unsigned int &finish_counter,
                                              unsigned int &printf_size) {
  // Default return value.
  finish_counter = 0;

  // Read the accelerator's status register
  unsigned int csr = 0;
  acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &csr);

  // Ignore non-status bits.
  // Required by Option 3 wrappers which now have a version info in
  // top 16 bits.
  csr = ACL_KERNEL_READ_BIT_RANGE(csr, KERNEL_CSR_LAST_STATUS_BIT, 0);

  // Check for updated status bits
  if (0 == (csr & KERNEL_CSR_STATUS_BITS_MASK)) {
    return;
  }

  // Clear the status bits that we read
  ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Accelerator %d reporting status %x.\n",
                          accel_id, csr);

  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_DONE) == 1) {
    ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Accelerator %d is done.\n", accel_id);
  }
  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_STALLED) == 1) {
    ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Accelerator %d is stalled.\n", accel_id);
  }
  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_UNSTALL) == 1) {
    ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Accelerator %d is unstalled.\n",
                            accel_id);
  }
  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_PROFILE_TEMPORAL_STATUS) == 1) {
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Accelerator %d ready for temporal profile readback.\n",
        accel_id);
  }

  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_DONE) == 0 &&
      ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_STALLED) == 0 &&
      ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_PROFILE_TEMPORAL_STATUS) == 0) {
    return;
  }

  // read the printf buffer size from the kernel cra, just after the
  // kernel arguments
  printf_size = 0;
  if (kern->accel_num_printfs[accel_id] > 0) {
    acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_PRINTF_BUFFER_SIZE,
                        &printf_size);
    assert(printf_size <= ACL_PRINTF_BUFFER_TOTAL_SIZE);
    ACL_KERNEL_IF_DEBUG_MSG(kern,
                            ":: Accelerator %d printf buffer size is %d.\n",
                            accel_id, printf_size);

    // kernel is stalled because the printf buffer is full
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_STALLED) == 1) {
      // clear interrupt
      unsigned int new_csr = 0;
      acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &new_csr);
      ACL_KERNEL_CLEAR_BIT(new_csr, KERNEL_CSR_STALLED);

      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              ":: Calling acl_process_printf_buffer_fn with "
                              "activation_id=%d and printf_size=%u.\n",
                              activation_id, printf_size);
      // update status, which will dump the printf buffer, set
      // debug_dump_printf = 0
      acl_process_printf_buffer_fn(activation_id, (int)printf_size, 0);

      ACL_KERNEL_IF_DEBUG_MSG(
          kern, ":: Accelerator %d new csr is %x.\n", accel_id,
          ACL_KERNEL_READ_BIT_RANGE(new_csr, KERNEL_CSR_LAST_STATUS_BIT, 0));

      acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR, new_csr);
      return;
    }
  }

  // Start profile counter readback if profile interrupt and not done
  if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_PROFILE_TEMPORAL_STATUS) != 0 &&
      ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_DONE) == 0) {
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Issuing profile reset command:: Accelerator %d.\n", accel_id);

    // Reset temporal profiling counter
    unsigned int ctrl_val;
    if (acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &ctrl_val)) {
      ACL_KERNEL_IF_DEBUG_MSG(
          kern, ":: Got bad status reading CSR ctrl reg:: Accelerator %d.\n",
          accel_id);
    }
    ACL_KERNEL_SET_BIT(ctrl_val, KERNEL_CSR_PROFILE_TEMPORAL_RESET);
    if (acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR, ctrl_val)) {
      ACL_KERNEL_IF_DEBUG_MSG(
          kern, ":: Got bad status writing CSR ctrl reg:: Accelerator %d.\n",
          accel_id);
    }

    if (activation_id < 0) {
      // This is an autorun kernel
      acl_process_autorun_profiler_scan_chain(kern->physical_device_id,
                                              accel_id);
    } else {
      acl_kernel_profile_fn(activation_id);
    }
    return;
  }

  if (kern->csr_version == CSR_VERSION_ID_18_1) {
    // Only expect single completion for older csr version
    finish_counter = 1;
  } else {
    acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_FINISH_COUNTER,
                        &finish_counter);
    ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Accelerator %d has %d finishes.\n",
                            accel_id, finish_counter);
  }
}

// Processes finished kernel invocation.
static void acl_kernel_if_update_status_finish(acl_kernel_if *kern,
                                               const unsigned int accel_id,
                                               const int activation_id,
                                               const unsigned int printf_size) {
#ifdef TEST_PROFILING_HARDWARE
  // Test readback of fake profile data using the acl_hal_mmd function that
  // would be called from the acl runtime.
  ACL_KERNEL_IF_DEBUG_MSG(kern, ":: testing profile hardware on accel_id=%u.\n",
                          accel_id);

  uint64_t data[10];
  acl_hal_mmd_get_profile_data(kern->physical_device_id, accel_id, data, 6);
  acl_hal_mmd_reset_profile_counters(kern->physical_device_id, accel_id);
  acl_hal_mmd_get_profile_data(kern->physical_device_id, accel_id, data, 6);
#endif

  // Just clear the "done" bit.  The "go" bit should already have been
  // cleared, but this is harmless anyway.
  // Since csr version 19, done bit is cleared when finish counter is read.
  // Since csr version 2022.3, done bit needs to be cleared explicitly.
  if (kern->csr_version == CSR_VERSION_ID_18_1 ||
      kern->csr_version >= CSR_VERSION_ID_2022_3) {
    unsigned int dum;
    acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR, 0);
    acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &dum);
  }

  if (kern->accel_num_printfs[accel_id] > 0) {
    ACL_KERNEL_IF_DEBUG_MSG(kern,
                            ":: Calling acl_process_printf_buffer_fn with "
                            "activation_id=%d and printf_size=%u.\n",
                            activation_id, printf_size);
    acl_process_printf_buffer_fn(activation_id, (int)printf_size, 0);
  }
}

// Called when we receive a kernel status interrupt.  Cycle through all of
// the running accelerators and check for updated status.
void acl_kernel_if_update_status(acl_kernel_if *kern) {
  acl_assert_locked_or_sig();

#ifdef POLLING
  ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(kern, 10, ":: Updating kernel status.\n");
#else
  ACL_KERNEL_IF_DEBUG_MSG_VERBOSE(kern, 5, ":: Updating kernel status.\n");
#endif

  // Get the state of kernel_cra address span extender segment prior to IRQ in
  // hardware If IRQ is received in middle of segment change, segment value in
  // cache and hardware could go out of sync
  unsigned int segment;
  acl_kernel_if_read_32b(kern, OFFSET_KERNEL_CRA_SEGMENT, &segment);

  // Zero upper 32-bits on 64-bit machines
  kern->cur_segment = segment & 0xffffffff;
  uintptr_t segment_pre_irq = kern->cur_segment;

  // Check which accelerators are done and update their status appropriately
  for (unsigned int accel_id = 0; accel_id < kern->num_accel; ++accel_id) {
    int next_queue_back;
    if (kern->accel_queue_back[accel_id] ==
        (int)kern->accel_invoc_queue_depth[accel_id] - 1) {
      next_queue_back = 0;
    } else {
      next_queue_back = kern->accel_queue_back[accel_id] + 1;
    }

    // Skip idle kernel
    if (kern->accel_job_ids[accel_id][next_queue_back] < 0) {
      // If this is the autorun profiling kernel, we want to read back profiling
      // data from it, so don't 'continue' (this kernel is always 'idle').
      if (accel_id != (unsigned)kern->autorun_profiling_kernel_id) {
        continue;
      }
    }

    const int activation_id = kern->accel_job_ids[accel_id][next_queue_back];

    unsigned int finish_counter = 0;
    unsigned int printf_size = 0;
    acl_kernel_if_update_status_query(kern, accel_id, activation_id,
                                      finish_counter, printf_size);

    if (!(finish_counter > 0)) {
      continue;
    }

    kern->last_kern_update = acl_kernel_if_get_time_us(kern);

    for (unsigned int i = 0; i < finish_counter; i++) {
      const int activation_id = kern->accel_job_ids[accel_id][next_queue_back];

      // Tell the host library this job is done
      kern->accel_job_ids[accel_id][next_queue_back] = -1;

      acl_kernel_if_update_status_finish(kern, accel_id, activation_id,
                                         printf_size);

      // Executing the following update after reading from performance
      // and efficiency monitors will clobber the throughput reported by
      // the host.  This is because setting CL_COMPLETE triggers the
      // completion timestamp - reading performance results through slave
      // ports before setting CL_COMPLETE adds to the apparent kernel time.
      //
      acl_kernel_if_update_fn(activation_id, CL_COMPLETE);
      kern->accel_queue_back[accel_id] = next_queue_back;

      if (kern->accel_queue_back[accel_id] ==
          (int)kern->accel_invoc_queue_depth[accel_id] - 1) {
        next_queue_back = 0;
      } else {
        next_queue_back = kern->accel_queue_back[accel_id] + 1;
      }

      if (kern->accel_job_ids[accel_id][next_queue_back] > -1) {
        acl_kernel_if_update_fn(kern->accel_job_ids[accel_id][next_queue_back],
                                CL_RUNNING);
      }
    }
  }

  // Restore value of kernel cra address span extender segment to that of prior
  // to IRQ
  if (kern->cur_segment != segment_pre_irq) {
    acl_kernel_if_write_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                            (unsigned int)segment_pre_irq);
    kern->cur_segment = segment_pre_irq;
    acl_kernel_if_read_32b(kern, OFFSET_KERNEL_CRA_SEGMENT,
                           (unsigned int *)&segment_pre_irq);
  }
}

void acl_kernel_if_debug_dump_printf(acl_kernel_if *kern, unsigned k) {
  acl_assert_locked();
  unsigned int printf_size = 0;
  int activation_id;
  unsigned int next_queue_back;

  if (kern->accel_queue_back[k] == (int)kern->accel_invoc_queue_depth[k] - 1)
    next_queue_back = 0;
  else
    next_queue_back = kern->accel_queue_back[k] + 1;

  if (kern->accel_num_printfs[k] > 0) {
    acl_kernel_cra_read(kern, k, KERNEL_OFFSET_PRINTF_BUFFER_SIZE,
                        &printf_size);
    assert(printf_size <= ACL_PRINTF_BUFFER_TOTAL_SIZE);
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Accelerator %d printf buffer size is %d.\n", k, printf_size);
    activation_id = kern->accel_job_ids[k][next_queue_back];
    ACL_KERNEL_IF_DEBUG_MSG(kern,
                            ":: Calling acl_process_printf_buffer_fn with "
                            "activation_id=%d and printf_size=%u.\n",
                            activation_id, printf_size);

    // set debug_dump_printf to 1
    acl_process_printf_buffer_fn(activation_id, (int)printf_size, 1);
  }
}

void acl_kernel_if_dump_status(acl_kernel_if *kern) {
  int expect_kernel = 0;
  unsigned k, i;
  acl_assert_locked();

  for (k = 0; k < kern->num_accel; ++k) {
    for (i = 0; i < kern->accel_invoc_queue_depth[k]; ++i) {
      if (kern->accel_job_ids[k][i] >= 0) {
        expect_kernel = 1;
      }
    }
  }

  if (!expect_kernel)
    return;

  for (k = 0; k < kern->num_accel; ++k) {
    unsigned int csr;

    // Read the accelerator's status register
    acl_kernel_cra_read(kern, k, KERNEL_OFFSET_CSR, &csr);

    kern->io.printf("  Kernel %2u Status: 0x%08x", k, csr);
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_RUNNING) &&
        !ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_STALLED))
      kern->io.printf(" running");
    else if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_STALLED))
      kern->io.printf(" stalled");
    else
      kern->io.printf(" idle");
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_DONE))
      kern->io.printf(" finish-pending");
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_BUSY))
      kern->io.printf(" busy");
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_LSU_ACTIVE))
      kern->io.printf(" lsu_active");
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_WR_ACTIVE))
      kern->io.printf(" write_active");
    if (ACL_KERNEL_READ_BIT(csr, KERNEL_CSR_LMEM_INVALID_BANK))
      kern->io.printf(" lm_bank_exception");

    // Dump the printf buffer to stdout
    acl_kernel_if_debug_dump_printf(kern, k);

    unsigned buffered_kernel_invocation = 0;
    for (i = 0; i < kern->accel_invoc_queue_depth[k]; ++i) {
      unsigned int next_queue_back;
      if (kern->accel_queue_back[k] + 1 + i >=
          kern->accel_invoc_queue_depth[k]) {
        // Loop back
        next_queue_back = kern->accel_queue_back[k] + 1 + i -
                          kern->accel_invoc_queue_depth[k];
      } else {
        next_queue_back = kern->accel_queue_back[k] + 1 + i;
      }
      if (kern->accel_job_ids[k][next_queue_back] >= 0) {
        if (i > 0) {
          buffered_kernel_invocation++;
        }
      }
    }
    // Don't need to print this if fast relaunch is disabled
    if (kern->accel_invoc_queue_depth[k] > 1) {
      printf("\n    Hardware Invocation Buffer: %u queued",
             buffered_kernel_invocation);
    }

    kern->io.printf("\n");
  }

  kern->io.printf("\n");
}

// Called by the host program when there are spare cycles or
// if the host has been waiting for a while in debug mode.
void acl_kernel_if_check_kernel_status(acl_kernel_if *kern) {
#ifdef POLLING
  static time_ns last_update;
#endif
  acl_assert_locked();

  if (kern->last_kern_update != 0 &&
      (acl_kernel_if_get_time_us(kern) - kern->last_kern_update >
       10 * 1000000)) {
    kern->last_kern_update = acl_kernel_if_get_time_us(kern);
    kern->io.printf(
        "No kernel updates in approximately 10 seconds for device %u",
        kern->physical_device_id);
    kern->io.printf(" ... a kernel may be hung?\n");
    acl_kernel_if_dump_status(kern);
  } else if (kern->io.debug_verbosity >= 3) {
    // If ACL_HAL_DEBUG >= 3, the status will be printed even the server isn't
    // hang every 10 seconds.
    if (acl_kernel_if_get_time_us(kern) - kern->last_printf_dump >
        10 * 1000000) {
      kern->last_printf_dump = acl_kernel_if_get_time_us(kern);
      acl_kernel_if_dump_status(kern);
    }
  }

#ifdef POLLING
  time_ns time = kern->io.get_time_ns();
  if (time > last_update + POLLING_PERIOD || time < last_update) {
    last_update = time;
    acl_kernel_if_update_status(kern);
  }
#endif
}

// Called when a printf buffer processing is done for kernel, and
// kernel can continue execution
void acl_kernel_if_unstall_kernel(acl_kernel_if *kern, int activation_id) {
  unsigned int k, i;
  acl_assert_locked();

  // Check which accelerator will be unstalled
  for (k = 0; k < kern->num_accel; ++k) {
    for (i = 0; i < kern->accel_invoc_queue_depth[k]; ++i) {

      if (activation_id == kern->accel_job_ids[k][i]) {
        // un-stall the kernel by writing to unstall bit
        unsigned int new_csr = 0;
        acl_kernel_cra_read(kern, k, KERNEL_OFFSET_CSR, &new_csr);

        ACL_KERNEL_SET_BIT(new_csr, KERNEL_CSR_UNSTALL);
        ACL_KERNEL_IF_DEBUG_MSG(
            kern, ":: Accelerator %d new csr is %x.\n", k,
            ACL_KERNEL_READ_BIT_RANGE(new_csr, KERNEL_CSR_LAST_STATUS_BIT, 0));

        acl_kernel_cra_write(kern, k, KERNEL_OFFSET_CSR, new_csr);
      }
    }
  }
}

void acl_kernel_if_close(acl_kernel_if *kern) {
  acl_assert_locked();
  // De-Allocations for each kernel
  if (kern->accel_csr)
    acl_free(kern->accel_csr);
  if (kern->accel_perf_mon)
    acl_free(kern->accel_perf_mon);
  if (kern->accel_num_printfs)
    acl_free(kern->accel_num_printfs);
  if (kern->accel_job_ids) {
    for (unsigned int a = 0; a < kern->num_accel; a++) {
      if (kern->accel_job_ids[a]) {
        acl_free((void *)kern->accel_job_ids[a]);
        kern->accel_job_ids[a] = NULL;
      }
    }
    acl_free((void *)kern->accel_job_ids);
  }
  if (kern->accel_invoc_queue_depth)
    acl_free(kern->accel_invoc_queue_depth);
  if (kern->accel_queue_front)
    acl_free(kern->accel_queue_front);
  if (kern->accel_queue_back)
    acl_free(kern->accel_queue_back);
  kern->accel_csr = NULL;
  kern->accel_perf_mon = NULL;
  kern->accel_num_printfs = NULL;
  kern->accel_job_ids = NULL;
  kern->accel_invoc_queue_depth = NULL;
  kern->accel_queue_front = NULL;
  kern->accel_queue_back = NULL;
  kern->autorun_profiling_kernel_id = -1;
}

// Private utility function to get a single 64-bit word from the
// profile_data register
static uint64_t acl_kernel_if_get_profile_data_word(acl_kernel_if *kern,
                                                    cl_uint accel_id) {
  uint64_t read_result;
  int status;
  acl_assert_locked_or_sig();

  assert(acl_kernel_if_is_valid(kern));

#ifdef _WIN32
  // Use 32-bit reads on Windows.
  unsigned int low_word, high_word;
  status = acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_DATA,
                               &low_word);
  if (status)
    return (uint64_t)status;
  ACL_KERNEL_IF_DEBUG_MSG(kern,
                          ":: Read profile hardware:: Accelerator %d "
                          "profile_data low_word is %x.\n",
                          accel_id, low_word);
  status = acl_kernel_cra_read(kern, accel_id,
                               KERNEL_OFFSET_CSR_PROFILE_DATA + 4, &high_word);
  if (status)
    return (uint64_t)status;
  ACL_KERNEL_IF_DEBUG_MSG(kern,
                          ":: Read profile hardware:: Accelerator %d "
                          "profile_data high_word is %x.\n",
                          accel_id, high_word);
  read_result =
      ((((uint64_t)high_word) & 0xFFFFFFFF) << 32) | (low_word & 0xFFFFFFFF);
#else
  status = acl_kernel_cra_read_64b(
      kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_DATA, &read_result);
  if (status)
    return (uint64_t)status;
  ACL_KERNEL_IF_DEBUG_MSG(kern,
                          ":: Read profile hardware:: Accelerator %d "
                          "profile_data read_result is 0x%llx.\n",
                          accel_id, read_result);
#endif
  return read_result;
}

// Internal HAL function that retrieves profile data from the hardware scan
// chain through CSR accesses.
// Returns 0 on success.
int acl_kernel_if_get_profile_data(acl_kernel_if *kern, cl_uint accel_id,
                                   uint64_t *data, unsigned int length) {
  uint64_t i;
  acl_assert_locked_or_sig();

  if (length > 0) {
    // Buffer the data
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Issuing profile load_buffer command:: Accelerator %d.\n",
        accel_id);
    acl_kernel_if_issue_profile_hw_command(
        kern, accel_id, KERNEL_CSR_PROFILE_LOAD_BUFFER_BIT, 1);

    for (i = 0; i < length; i++) {
      data[i] = acl_kernel_if_get_profile_data_word(kern, accel_id);
      ACL_KERNEL_IF_DEBUG_MSG(kern,
                              ":: Read profile hardware:: Accelerator %d "
                              "profile_data word [%u] is 0x%016llx.\n",
                              accel_id, i, data[i]);
    }

    // The interrupt has been serviced - tell HW to deassert irq.
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Issuing profile temporal reset command:: Accelerator %d.\n",
        accel_id);
    unsigned int ctrl_val;
    int status;
    status = acl_kernel_cra_read(kern, accel_id, KERNEL_OFFSET_CSR, &ctrl_val);
    ACL_KERNEL_IF_DEBUG_MSG(
        kern, ":: Finished reading profiler data from %d.\n", accel_id);
    if (status)
      return status;
    ACL_KERNEL_SET_BIT(ctrl_val, KERNEL_CSR_PROFILE_TEMPORAL_RESET);
    status = acl_kernel_cra_write(kern, accel_id, KERNEL_OFFSET_CSR, ctrl_val);
    if (status)
      return status;
  }

  return 0;
}

// Internal HAL function that resets the profile scan-chain hardware by
// setting a bit in the profile_ctrl CSR.
// Returns 0 on success.
int acl_kernel_if_reset_profile_counters(acl_kernel_if *kern,
                                         cl_uint accel_id) {
  acl_assert_locked_or_sig();
  ACL_KERNEL_IF_DEBUG_MSG(kern, ":: Reset profile hardware:: Accelerator %d.\n",
                          accel_id);
  return acl_kernel_if_issue_profile_hw_command(
      kern, accel_id, KERNEL_CSR_PROFILE_RESET_BIT, 1);
}

// Internal HAL function that disables the profile hardware counters by
// clearing a bit in the profile_ctrl CSR.
// Returns 0 on success.
int acl_kernel_if_disable_profile_counters(acl_kernel_if *kern,
                                           cl_uint accel_id) {
  acl_assert_locked();
  ACL_KERNEL_IF_DEBUG_MSG(
      kern, ":: Disable profile counters:: Accelerator %d.\n", accel_id);
  return acl_kernel_if_issue_profile_hw_command(
      kern, accel_id, KERNEL_CSR_PROFILE_ALLOW_PROFILING_BIT, 0);
}

// Internal HAL function that enables the profile hardware counters by
// setting a bit in the profile_ctrl CSR.
// Returns 0 on success.
int acl_kernel_if_enable_profile_counters(acl_kernel_if *kern,
                                          cl_uint accel_id) {
  acl_assert_locked();
  ACL_KERNEL_IF_DEBUG_MSG(
      kern, ":: Enable profile counters:: Accelerator %d.\n", accel_id);
  return acl_kernel_if_issue_profile_hw_command(
      kern, accel_id, KERNEL_CSR_PROFILE_ALLOW_PROFILING_BIT, 1);
}

// Private utility function to set start cycle in the profile hardware
// Returns 0 on success.
int acl_kernel_if_set_profile_start_cycle(acl_kernel_if *kern, cl_uint accel_id,
                                          uint64_t value) {
  int status;
  acl_assert_locked();
  assert(acl_kernel_if_is_valid(kern));
  assert(accel_id < kern->num_accel);
  status = acl_kernel_cra_write_64b(
      kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_START_CYCLE, value);

  return status;
}

// Private utility function to set stop cycle in the profile hardware
// Returns 0 on success.
int acl_kernel_if_set_profile_stop_cycle(acl_kernel_if *kern, cl_uint accel_id,
                                         uint64_t value) {
  int status;
  acl_assert_locked();
  assert(acl_kernel_if_is_valid(kern));
  assert(accel_id < kern->num_accel);
  status = acl_kernel_cra_write_64b(
      kern, accel_id, KERNEL_OFFSET_CSR_PROFILE_STOP_CYCLE, value);
  return status;
}

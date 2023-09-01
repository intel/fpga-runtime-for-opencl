// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <sstream>

// Internal headers.
#include <acl_pll.h>

#define PLL_VERSION_ID (0xa0c00001)
#define PLL_VERSION_ID_20NM (0xa0c00002)

#define ACL_PLL_DEBUG_MSG_VERBOSE(p, verbosity, m, ...)                        \
  if (p->io.printf && (p->io.debug_verbosity) >= verbosity)                    \
    do {                                                                       \
      p->io.printf((m), ##__VA_ARGS__);                                        \
  } while (0)

// PLL reconfig Register Address map (SV, CV)
const unsigned int PLL_MODE_REG = 0 * 4;
const unsigned int PLL_STATUS_REG = 1 * 4;
const unsigned int PLL_START_REG = 2 * 4;
const unsigned int PLL_N_REG = 3 * 4;
const unsigned int PLL_M_REG = 4 * 4;
const unsigned int PLL_C_REG = 5 * 4;
const unsigned int PLL_PHASE_REG = 6 * 4;
const unsigned int PLL_K_REG = 7 * 4;
const unsigned int PLL_R_REG = 8 * 4;
const unsigned int PLL_CP_REG = 9 * 4;
const unsigned int PLL_DIV_REG = 28 * 4;
const unsigned int PLL_C0_READ_REG = 10 * 4;
const unsigned int PLL_C1_READ_REG = 11 * 4;

// PLL reconfig Register Address map (A10)
const unsigned int PLL_M_REG_20NM = 144 * 4;
const unsigned int PLL_N_REG_20NM = 160 * 4;
const unsigned int PLL_C0_REG_20NM = 192 * 4;
const unsigned int PLL_C1_REG_20NM = 193 * 4;
const unsigned int PLL_LF_REG_20NM = 64 * 4;
const unsigned int PLL_CP_REG_20NM = 32 * 4;

/*************************** Function headers ***********************/

static void acl_pll_read_from_elf(acl_pll *pll,
                                  const std::string &pkg_pll_config,
                                  pll_setting_t *P);
static int get_version_id(acl_pll *pll);
static int _acl_pll_read(acl_pll *pll, dev_addr_t addr, unsigned int *val);
static int _acl_pll_write(acl_pll *pll, unsigned int addr, unsigned int val);
int read_pll_settings(acl_pll *pll, unsigned int i, pll_setting_t *P,
                      const std::string &pkg_pll_config);
static float _acl_pll_get_kernel_freq(acl_pll *pll, unsigned clocksel);
static int wait_on_lock(acl_pll *pll);

/*************************** Utility Functions ***********************/

void acl_pll_read_from_elf(acl_pll *pll, const std::string &pkg_pll_config,
                           pll_setting_t *P) {
  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1,
                            "PLL: Parsing pll_config.bin ELF string: %s\n",
                            pkg_pll_config.c_str());

  // Tokenize pkg_pll_config using spaces as delimiters.
  std::vector<std::string> tokens;
  size_t start = 0;
  size_t end = 0;
  while (end != std::string::npos) {
    end = pkg_pll_config.find(' ', start);
    tokens.push_back(pkg_pll_config.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    if (end != std::string::npos) {
      start = end + 1;
    }
  }

  assert(tokens.size() == 7);

  auto *pll_settings = P;
  pll_settings->freq_khz = (unsigned)atoi(tokens[0].c_str());
  pll_settings->m = (unsigned)atoi(tokens[1].c_str());
  pll_settings->n = (unsigned)atoi(tokens[2].c_str());
  pll_settings->c0 = (unsigned)atoi(tokens[3].c_str());
  pll_settings->c1 = (unsigned)atoi(tokens[4].c_str());
  pll_settings->k = (unsigned)atoi(tokens[5].c_str());
  pll_settings->cp = (unsigned)atoi(tokens[6].c_str());

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1,
                            "PLL: Done parsing pll_config.bin ELF string\n");
}

// Returns 0 on success, -1 on failure
static int get_version_id(acl_pll *pll) {
  unsigned int version = 0;
  int r;

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "PLL: Reading PLL version ID\n");
  r = _acl_pll_read(pll, OFFSET_PLL_VERSION_ID, &version);
  if (r != 0 || (version != PLL_VERSION_ID && version != PLL_VERSION_ID_20NM)) {
    pll->io.printf(
        "  PLL: Version mismatch! Expected 0x%x or 0x%x but read 0x%x\n",
        PLL_VERSION_ID, PLL_VERSION_ID_20NM, version);
    return -1;
  } else {
    pll->version_id = version;
    return 0;
  }
}

int acl_pll_is_valid(acl_pll *pll) {
  if (pll->version_id == PLL_VERSION_ID) {
    if (pll->num_known_settings > 0 && pll->known_settings != NULL &&
        sizeof(*pll->known_settings) ==
            (pll->num_known_settings * sizeof(pll_setting_t)) &&
        acl_bsp_io_is_valid(&pll->io))
      return 1;
    else
      return 0;
  } else if (pll->version_id == PLL_VERSION_ID_20NM) {
    if (acl_bsp_io_is_valid(&pll->io))
      return 1;
    else
      return 0;
  }
  return 0;
}

int acl_pll_setting_is_valid(pll_setting_t ps) {
  if ((ps.freq_khz > 0 && ps.freq_khz < MAX_POSSIBLE_FMAX) && (ps.m != 0) &&
      (ps.cp > 0 && ps.cp < 200) && (ps.c0 == ps.c1 * 2))
    return 1;
  else
    return 0;
}

// Read from pll with pll validation
// Returns 0 on success, < 0 on error
static int acl_pll_read(acl_pll *pll, unsigned int addr, unsigned int *val) {
  if (!acl_pll_is_valid(pll)) {
    pll->io.printf("PLL Error: Invalid pll handle used");
    return -1;
  }

  return _acl_pll_read(pll, addr, val);
}

// Write to pll without pll validation - needed to validate
// Returns 0 on success, < 0 on error
static int acl_pll_write(acl_pll *pll, unsigned int addr, unsigned int val) {
  if (!acl_pll_is_valid(pll)) {
    pll->io.printf("PLL Error: Invalid pll handle used");
    return -1;
  }

  return _acl_pll_write(pll, addr, val);
}

// Read from pll without pll validation - needed to validate
// Returns 0 on success, < 0 on error
static int _acl_pll_read(acl_pll *pll, dev_addr_t addr, unsigned int *val) {
  size_t size = sizeof(unsigned int);
  size_t r = pll->io.read(&pll->io, addr, (char *)val, size);
  if (r < size) {
    pll->io.printf("PLL Error: Read failed, %zu bytes read, %zu expected\n", r,
                   size);
    return -1;
  }
  return 0;
}

// Write to pll without pll validation - needed to validate
// Returns 0 on success, < 0 on error
static int _acl_pll_write(acl_pll *pll, unsigned int addr, unsigned int val) {
  size_t size = sizeof(unsigned int);
  size_t r = pll->io.write(&pll->io, (dev_addr_t)addr, (char *)&val, size);
  if (r < size) {
    pll->io.printf("PLL Error: Write failed, %zu bytes written, %zu expected\n",
                   r, size);
    return -1;
  }
  return 0;
}

// Need PLL ROM until we have ELF package.  Returns 0 if success, -ve on error
int read_pll_settings(acl_pll *pll, unsigned int i, pll_setting_t *P,
                      const std::string &pkg_pll_config) {
  int r = 0;

  if (pll->version_id == PLL_VERSION_ID) {
    dev_addr_t addr = (dev_addr_t)i * sizeof(pll_setting_t);
    P->freq_khz = 0;
    P->m = 0;
    P->n = 0;
    P->k = 0;
    P->c0 = 0;
    P->c1 = 0;
    P->r = 0;
    P->cp = 0;
    P->div = 0;
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->freq_khz);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->m);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->n);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->k);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->c0);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->c1);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->r);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->cp);
    addr += sizeof(int);
    r |= _acl_pll_read(pll, OFFSET_ROM + addr, &P->div);
    addr += sizeof(int);
    return r;
  } else if (pll->version_id == PLL_VERSION_ID_20NM) {
    // read from ELF package
    acl_pll_read_from_elf(pll, pkg_pll_config, P);
    return r;
  } else {
    return 1;
  }
}

// Measures the kernel clock (clocksel==0) or 2x clock (clocksel==1) fmax
static float _acl_pll_get_kernel_freq(acl_pll *pll, unsigned clocksel) {
  time_ns start_time;
  time_ns elapsed_time = 0;
  unsigned int count = 0;

  // Reset counter
  acl_pll_write(pll, OFFSET_COUNTER, clocksel);

  // Wait for timeout ns to pass
  start_time = pll->io.get_time_ns();
  while (elapsed_time < (time_ns)CLK_MEASUREMENT_PERIOD)
    elapsed_time = (pll->io.get_time_ns() - start_time);

  // Read counter and calculate freq
  acl_pll_read(pll, OFFSET_COUNTER, &count);

  return (float)count / (float)elapsed_time * 1000.0f;
}

// Return 1 if lock was achieved, 0 otherwise
static int wait_on_lock(acl_pll *pll) {
  time_ns reset_time;

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL: Waiting on lock signal\n");

  // Wait for pll to reconfig itself by polling ready bit
  reset_time = pll->io.get_time_ns();
  while (!acl_pll_is_locked(pll)) {
    if (pll->io.get_time_ns() - reset_time > (time_ns)(RECONFIG_TIMEOUT)) {
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "  PLL: FAILED to lock\n");
      return 0;
    }
  }
  return 1;
}

/*************************** Public Functions ************************/

float acl_pll_get_kernel_freq(acl_pll *pll) {
  return _acl_pll_get_kernel_freq(pll, 0);
}

float acl_pll_get_kernel2x_freq(acl_pll *pll) {
  return _acl_pll_get_kernel_freq(pll, 1);
}

int acl_pll_init(acl_pll *pll, acl_bsp_io bsp_io,
                 const std::string &pkg_pll_config) {
  unsigned int i = 0;

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "PLL initializing\n");
  pll->num_known_settings = 0;
  pll->known_settings = NULL;
  pll->curr_freq_khz = 0;

  if (!acl_bsp_io_is_valid(&bsp_io))
    return -1;
  pll->io = bsp_io;

  // 1. Make sure hw version matches expected sw version
  assert(get_version_id(pll) == 0);

  // Detected 20nm PLL and didn't supply PLL configuration string
  // Not doing dynamic PLL reconfiguration
  if (pll->version_id == PLL_VERSION_ID_20NM && pkg_pll_config == "") {
    return 0;
  };

  if (pll->version_id == PLL_VERSION_ID) {
    ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "PLL: found 28nm PLL (SV, CV)\n");
    // -------------------------------------------------------------------
    // 28nm PLL (SV, CV)
    // -------------------------------------------------------------------
    // 2. Find out how many known settings are compiled into the SOF
    do {
      pll_setting_t ps;
      ps.freq_khz = 0;
      if (read_pll_settings(pll, i, &ps, "") != 0)
        return -1;
      if (ps.freq_khz == 0 && i > 0)
        break;
      if (!acl_pll_setting_is_valid(ps)) {
        pll->io.printf("PLL Error: Read invalid pll setting for %f MHz.  Make "
                       "sure read access too acl_kernel_clk is functioning and "
                       "the post-quartus-script succeeded\n",
                       (float)ps.freq_khz / 1000.0);
        pll->io.printf("  PLL: Invalid settings f=%d m=%d n=%d k=%d c0=%d "
                       "c1=%d r=%d cp=%d div=%d\n",
                       ps.freq_khz, ps.m, ps.n, ps.k, ps.c0, ps.c1, ps.r, ps.cp,
                       ps.div);
        return -1;
      }

    } while (i++ <= MAX_KNOWN_SETTINGS);

    if (i > MAX_KNOWN_SETTINGS) {
      pll->io.printf(
          "PLL Error: Found %d known pll settings.  Make sure PLL accesses are "
          "functioning and the post-quartus-script succeeded\n",
          i);
      return -1;
    }

    pll->num_known_settings = i;

    // 3. Load all the known settings

    pll->known_settings = (pll_setting_t *)malloc(pll->num_known_settings *
                                                  sizeof(pll_setting_t));
    if (pll->known_settings == NULL) {
      pll->io.printf(" PLL Error: Ran out of memory\n");
      return -1;
    }
    // Read these out of the pll_rom - hardcoded until we package with ELF
    for (i = 0; i < pll->num_known_settings; i++) {
      read_pll_settings(pll, i, &pll->known_settings[i], "");
      ACL_PLL_DEBUG_MSG_VERBOSE(
          pll, 2,
          "Read pll values f=%d m=%d n=%d k=%d c0=%d c1=%d r=%d cp=%d div=%d\n",
          pll->known_settings[i].freq_khz, pll->known_settings[i].m,
          pll->known_settings[i].n, pll->known_settings[i].k,
          pll->known_settings[i].c0, pll->known_settings[i].c1,
          pll->known_settings[i].r, pll->known_settings[i].cp,
          pll->known_settings[i].div);
    }

    // 4. Configure the default settings

    if (!acl_pll_setting_is_valid(pll->known_settings[0])) {
      ACL_PLL_DEBUG_MSG_VERBOSE(
          pll, 0,
          "Read pll values f=%d m=%d n=%d k=%d c0=%d c1=%d r=%d cp=%d div=%d\n",
          pll->known_settings[0].freq_khz, pll->known_settings[0].m,
          pll->known_settings[0].n, pll->known_settings[0].k,
          pll->known_settings[0].c0, pll->known_settings[0].c1,
          pll->known_settings[0].r, pll->known_settings[0].cp,
          pll->known_settings[0].div);
      pll->io.printf(" PLL Error: Default pll settings are invalid\n");
      return -1;
    }
  } else if (pll->version_id == PLL_VERSION_ID_20NM) {
    ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "PLL: found 20nm PLL (A10)\n");
    pll->num_known_settings = 1;
    pll->known_settings = (pll_setting_t *)malloc(pll->num_known_settings *
                                                  sizeof(pll_setting_t));
    if (pll->known_settings == NULL) {
      pll->io.printf(" PLL Error: Ran out of memory\n");
      return -1;
    }
    read_pll_settings(pll, 0, &pll->known_settings[0], pkg_pll_config);
  };

  // Reset the PLL on startup
  acl_pll_reset(pll);
  assert(wait_on_lock(pll));

  ACL_PLL_DEBUG_MSG_VERBOSE(
      pll, 1, "PLL: Before reconfig, kernel clock set to %f MHz %f MHz\n",
      acl_pll_get_kernel_freq(pll), acl_pll_get_kernel2x_freq(pll));

  if (pll->known_settings == NULL) {
    pll->io.printf(" PLL Error: Ran out of memory\n");
    return -1;
  }

  if (acl_pll_reconfigure(pll, pll->known_settings[0]) != 0) {
    pll->io.printf(" PLL Error: Failed to configure default settings\n");
    return -1;
  }

  return 0;
}

void acl_pll_close(acl_pll *pll) {
  if (pll == NULL) {
    return;
  }
  if (pll->known_settings != NULL) {
    free(pll->known_settings);
  }
  pll->curr_freq_khz = 0;
  pll->num_known_settings = 0;
}

int acl_pll_is_locked(acl_pll *pll) {
  unsigned int locked = 0;
  acl_pll_read(pll, OFFSET_LOCK, &locked);
  return (locked == 1) ? 1 : 0;
}

void acl_pll_reset(acl_pll *pll) {
  time_ns reset_time;
  unsigned int sw_resetn = 0; // Any write will cause reset - data is don't care

  assert(pll);

  acl_pll_write(pll, OFFSET_RESET, sw_resetn);

  // This will stall while circuit is being reset.  Executing this read
  // therefore ensures this circuit has come out of reset before proceeding.
  acl_pll_read(pll, OFFSET_RESET, &sw_resetn);
  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL: Reset issued\n");

  // Just in case, verify that value read back shows reset deasserted
  reset_time = pll->io.get_time_ns();
  while (sw_resetn == 0) {
    if (pll->io.get_time_ns() - reset_time > (time_ns)(RECONFIG_TIMEOUT)) {
      pll->io.printf("PLL failed to come out of reset. Read 0x%x\n", sw_resetn);
      assert(sw_resetn != 0);
    }
    acl_pll_read(pll, OFFSET_RESET, &sw_resetn);
  }
}

// Reconfigure the PLL with the given settings
// Returns 0 on success, -ve otherwise
int acl_pll_reconfigure(acl_pll *pll, pll_setting_t pllsettings) {

// If count is zero, bypass this counter (set bit 16 high)
#define DIVUP50DUTY(x)                                                         \
  ((x <= 1) ? (1 << 16) : (((x % 2) << 17) | ((x / 2 + x % 2) << 8) | (x / 2)))

  static int num_reconfigs = 0;
  int num_retries = 0;
  unsigned int r_val;
  unsigned int cp_val;
  time_ns reset_time;
  int status = 0;
  unsigned int x;

  // only request a locked PLL for 28 nm PLL (SV, CV)
  if (pll->version_id == PLL_VERSION_ID) {
    if (!acl_pll_is_locked(pll)) {
      ACL_PLL_DEBUG_MSG_VERBOSE(
          pll, 1, "  PLL: Reconfig requested without lock - resetting\n");
      acl_pll_reset(pll);
      assert(wait_on_lock(pll));
    }
  }

  if (pll->io.debug_verbosity > 2) {
    if (pll->version_id == PLL_VERSION_ID) {
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_M_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  M  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_N_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  N  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_C0_READ_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C0 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_C1_READ_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C1 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_R_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  R  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_CP_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback CP  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_DIV_REG, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback DIV= %08x\n", x);
    } else if (pll->version_id == PLL_VERSION_ID_20NM) {
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_M_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  M  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_N_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  N  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C0_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C0 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C1_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C1 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_CP_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback CP  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_LF_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback LF  = %08x\n", x);
    } else {
      return -1;
    }
  }

  // Retry loop - sometimes the pll doesn't come out of reconfiguration
  do {
    if (pll->version_id == PLL_VERSION_ID) {
      unsigned reconfig_status = 0;

      ACL_PLL_DEBUG_MSG_VERBOSE(
          pll, 1, "Kernel pll reconfiguration parameters being set ... \n");

      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  M = %08x\n",
                                DIVUP50DUTY(pllsettings.m));
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  K = %08x\n", pllsettings.k);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  N = %08x\n",
                                DIVUP50DUTY(pllsettings.n));
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " C0 = %08x\n",
                                DIVUP50DUTY(pllsettings.c0));
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " C1 = %08x\n",
                                DIVUP50DUTY(pllsettings.c1));

      // Set to polling mode
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_MODE_REG, 1);

      // M
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_M_REG,
                    DIVUP50DUTY(pllsettings.m));

      // K
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_K_REG, pllsettings.k);

      // N
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_N_REG,
                    DIVUP50DUTY(pllsettings.n));

      // C0
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_C_REG,
                    (0 << 18) | DIVUP50DUTY(pllsettings.c0));

      // C1
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_C_REG,
                    (1 << 18) | DIVUP50DUTY(pllsettings.c1));

      // R - From the FD
      switch (pllsettings.r) {
      case 18000:
        r_val = 0;
        break;
      case 16000:
        r_val = 1;
        break;
      case 14000:
        r_val = 2;
        break;
      case 12000:
        r_val = 3;
        break;
      case 10000:
        r_val = 4;
        break;
      case 8000:
        r_val = 5;
        break;
      case 6000:
        r_val = 6;
        break;
      case 4000:
        r_val = 7;
        break;
      case 2000:
        r_val = 8;
        break;
      case 1000:
        r_val = 9;
        break;
      case 500:
        r_val = 10;
        break;
      default:
        pll->io.printf("PLL: Read invalid PLL setting - R = %d\n",
                       pllsettings.r);
        return -1;
      }
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  R = %08x\n", r_val);
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_R_REG, r_val);

      // CP
      switch (pllsettings.cp) {
      case 5:
        cp_val = 0;
        break;
      case 10:
        cp_val = 1;
        break;
      case 20:
        cp_val = 2;
        break;
      case 30:
        cp_val = 3;
        break;
      case 40:
        cp_val = 4;
        break;
      default:
        pll->io.printf("PLL: Read invalid PLL setting - CP = %d\n",
                       pllsettings.cp);
        return -1;
      }
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " CP = %08x\n", cp_val);
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_CP_REG, cp_val);

      // DIV
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "DIV = %08x\n",
                                pllsettings.div == 2 ? 0 : 1);
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_DIV_REG,
                    pllsettings.div == 2 ? 0 : 1u);

      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2,
                                "Kernel pll reconfiguration starting ... \n");

      // Start dynamic reconfig
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL + PLL_START_REG, 1);

      // Wait for pll to reconfig itself by polling ready bit
      reset_time = pll->io.get_time_ns();
      while (status != 1) {
        if (reconfig_status == 0)
          acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_STATUS_REG,
                       &reconfig_status);
        status = acl_pll_is_locked(pll);

        // Upon timeout, reset pll and try again.
        if (pll->io.get_time_ns() - reset_time > (time_ns)(RECONFIG_TIMEOUT)) {
          ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1,
                                    "  PLL: FAILED to configure kernel clock "
                                    "after %d successful reconfigs\n",
                                    num_reconfigs);
          ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "       reconfig %d, lock %d\n",
                                    reconfig_status, status);
          // Reset just the PLL to try again
          acl_pll_reset(pll);
          break;
        }
        status &= reconfig_status;
      }

      if (status == 1)
        break;
    } else if (pll->version_id == PLL_VERSION_ID_20NM) {
      ACL_PLL_DEBUG_MSG_VERBOSE(
          pll, 1, "Kernel pll reconfiguration parameters being set ... \n");

      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  M = %08x\n", pllsettings.m);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, "  N = %08x\n", pllsettings.n);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " C0 = %08x\n", pllsettings.c0);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " C1 = %08x\n", pllsettings.c1);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " CP = %08x\n", pllsettings.cp);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 2, " LF = %08x\n", pllsettings.k);

      // M
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_M_REG_20NM,
                    pllsettings.m);

      // N
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_N_REG_20NM,
                    pllsettings.n);

      // C0
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C0_REG_20NM,
                    pllsettings.c0);

      // C1
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C1_REG_20NM,
                    pllsettings.c1);

      // CP
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_CP_REG_20NM,
                    pllsettings.cp);

      // LF
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_LF_REG_20NM,
                    pllsettings.k);

      // submit all reconfiguration settings by writing to the start address
      acl_pll_write(pll, OFFSET_RECONFIG_CTRL_20NM, 1);
      assert(wait_on_lock(pll));

      break;
    }
  } while (num_retries++ < MAX_RECONFIG_RETRIES);

  if (num_retries > MAX_RECONFIG_RETRIES) {
    pll->io.printf("  PLL: FAILED to configure kernel clock after %d reconfigs "
                   "and %d retries\n",
                   num_reconfigs, num_retries - 1);
    if (pll->version_id == PLL_VERSION_ID) {
      pll->io.printf("  PLL:    Settings attempted f=%d m=%d n=%d k=%d c0=%d "
                     "c1=%d r=%d cp=%d div=%d\n",
                     pllsettings.freq_khz, pllsettings.m, pllsettings.n,
                     pllsettings.k, pllsettings.c0, pllsettings.c1,
                     pllsettings.r, pllsettings.cp, pllsettings.div);

      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_M_REG, &x);
      pll->io.printf(" PLL readback M  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_N_REG, &x);
      pll->io.printf(" PLL readback N  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_C0_READ_REG, &x);
      pll->io.printf(" PLL readback C0 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_C1_READ_REG, &x);
      pll->io.printf(" PLL readback C1 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_R_REG, &x);
      pll->io.printf(" PLL readback R  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_CP_REG, &x);
      pll->io.printf(" PLL readback CP  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL + PLL_DIV_REG, &x);
      pll->io.printf(" PLL readback DIV= %08x\n", x);
      return -1;
    } else if (pll->version_id == PLL_VERSION_ID_20NM) {
      pll->io.printf("  PLL:    Settings attempted f=%d m=%d n=%d c0=%d c1=%d "
                     "lf=%d cp=%d\n",
                     pllsettings.freq_khz, pllsettings.m, pllsettings.n,
                     pllsettings.c0, pllsettings.c1, pllsettings.k,
                     pllsettings.cp);

      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_M_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  M  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_N_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback  N  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C0_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C0 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_C1_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback C1 = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_LF_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback LF  = %08x\n", x);
      acl_pll_read(pll, OFFSET_RECONFIG_CTRL_20NM + PLL_CP_REG_20NM, &x);
      ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, " PLL Readback CP  = %08x\n", x);
      return -1;
    } else {
      return -1;
    }
  }

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "Kernel pll ready\n");

  // The output counters can get out of phase, so always reset
  acl_pll_reset(pll);
  assert(wait_on_lock(pll));

  num_reconfigs++;

  ACL_PLL_DEBUG_MSG_VERBOSE(pll, 1, "Kernel clock set to %f MHz %f MHz\n",
                            acl_pll_get_kernel_freq(pll),
                            acl_pll_get_kernel2x_freq(pll));

  // Update internal tracking of currently set clock frequency
  pll->curr_freq_khz = pllsettings.freq_khz;
  return 0;
}

pll_setting_t acl_pll_get_default_settings(acl_pll *pll) {
  assert(pll && pll->num_known_settings > 0);
  return pll->known_settings[0];
}

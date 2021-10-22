// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_PLL_H
#define ACL_PLL_H

#include <string>

#include "acl_bsp_io.h"

typedef struct {
  unsigned int freq_khz;
  unsigned int m;
  unsigned int n;
  unsigned int k;
  unsigned int c0;
  unsigned int c1;
  unsigned int r;
  unsigned int cp;
  unsigned int div;
} pll_setting_t;

typedef struct {
  acl_bsp_io io;
  unsigned int curr_freq_khz;
  pll_setting_t *known_settings;
  unsigned int num_known_settings;
  unsigned int version_id;
} acl_pll;

// **********************************************************
// ********************  User Methods ***********************
// **********************************************************

/*
 * Initialize PLL using the given IO accessors
 * Returns 0 on success, -ve on error
 */
int acl_pll_init(acl_pll *pll, acl_bsp_io bsp_io,
                 const std::string &pkg_pll_config);

void acl_pll_close(acl_pll *pll);

// **********************************************************
// **********************  Advanced *************************
// **********************************************************

/*
 * Reconfigure the PLL with the given settings
 * Returns 0 on success, -ve on error
 */
int acl_pll_reconfigure(acl_pll *pll, pll_setting_t pllsettings);

/*
 * Resets the pll - use this when you lose lock
 */
void acl_pll_reset(acl_pll *pll);

/*
 * Check PLL lock.  Returns 1 if locked, 0 not locked, -ve error
 */
int acl_pll_is_locked(acl_pll *pll);

/*
 * Measure the kernel clock frequencies, returns MHz
 */
float acl_pll_get_kernel_freq(acl_pll *pll);
float acl_pll_get_kernel2x_freq(acl_pll *pll);

/*
 * Return the default pll setting.
 */
pll_setting_t acl_pll_get_default_settings(acl_pll *pll);

int acl_pll_get_num_settings(acl_pll *pll);
pll_setting_t acl_pll_get_setting(acl_pll *pll, int index);

#endif // ACL_PLL

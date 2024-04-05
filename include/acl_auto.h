// Copyright (C) 2011-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#if defined(__cplusplus)
extern "C" {
#endif

/* Get version info from boilerplate infrastructure.
 * This is a .tmpl file so that #include "acl_auto.h" forces
 * the preprocessor is forced to get the file contents from
 * the work dir, not the original source dir.
 * Example, for ACL being in 17.1 OR 17.1std:
 *  ACL_VERSION is "v17.1.0"
 *  ACL_VERSION_PLAIN is "17.1"
 *  ACL_VERSION_PLAIN_FOR_DRIVER_QUERY is "17.1"
 * The last one is for the driver version query.  The OpenCL spec says it
 * has to match \d+.\d+ and nothing else.
 */
#define ACL_VERSION "v2025.0.0"
#define ACL_VERSION_PLAIN "2025.0"
#define ACL_VERSION_PLAIN_FOR_DRIVER_QUERY "2025.0"

/* Check if we are currently compiling for ACDS Pro or Standard.
 * 1 means Pro and 0 means Standard.
 */
#define ACDS_PRO 1

/*
 * This symbol will be unique for a version and build.
 * Link will fail if libraries don't line up.
 */
#define ACL_VERSION_COMPATIBILITY_SYMBOL _ALTERA_SDK_FOR_OPENCL_21_1_0_98_2
extern int ACL_VERSION_COMPATIBILITY_SYMBOL;
#define ACL_USE_VERSION_COMPATIBILITY_SYMBOL                                   \
  { ACL_VERSION_COMPATIBILITY_SYMBOL = 42; }

#if defined(__cplusplus)
}
#endif

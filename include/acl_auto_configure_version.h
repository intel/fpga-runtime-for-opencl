// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_AUTO_CONFIGURE_VERSIONID
// ATTENTION! The autodiscovery string is now forward-compatible; as a result
// the version ID should not be updated except in very special circustances.
#define ACL_AUTO_CONFIGURE_VERSIONID 23

#endif

// This is the oldest version with which the current ACL host runtime is
// compatible. When you update this, make sure to update the backwards
// compatibility unit test in acl_auto_configure_test.cpp. The test is currently
// located in the "simple" test of the auto_configure testgroup.

#ifndef ACL_AUTO_CONFIGURE_BACKWARDS_COMPATIBLE_WITH_VERSIONID
#define ACL_AUTO_CONFIGURE_BACKWARDS_COMPATIBLE_WITH_VERSIONID 23

#endif

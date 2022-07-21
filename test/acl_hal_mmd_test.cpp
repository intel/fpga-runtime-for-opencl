// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <acl_hal.h>
#include <acl_thread.h>

#include "acl_test.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

TEST_GROUP(acl_hal_mmd){void setup(){acl_mutex_wrapper.lock();
acl_hal_test_setup_generic_system();
this->load();
}
void teardown() {
  this->unload();
  acl_hal_test_teardown_generic_system();
  acl_mutex_wrapper.unlock();
  acl_test_run_standard_teardown_checks();
}

void load(void) {}

void unload(void) {}

protected:
}
;

TEST(acl_hal_mmd, load_library) {
  acl_system_def_t sysdef;
  acl_mmd_library_names_t _libraries_to_load;
  // Making a static array to avoid having to malloc in unit test
  const acl_hal_t *test_hal = nullptr;
#ifdef _WIN32
  char library_fullpath[MAX_PATH];
  GetFullPathName("fake_bsp/fakegoodbsp.dll", MAX_PATH, library_fullpath, 0);
  _libraries_to_load.library_name = library_fullpath;
#else
  _libraries_to_load.library_name = "fake_bsp/libfakegoodbsp.so";
#endif
  _libraries_to_load.next = nullptr;
  test_hal = acl_mmd_get_system_definition(&sysdef, &_libraries_to_load);
  CHECK(test_hal != nullptr);
  CHECK_EQUAL(128, sysdef.num_devices);

#ifdef _WIN32
  GetFullPathName("fake_bsp/invalidbsp.dll", MAX_PATH, library_fullpath, 0);
  _libraries_to_load.library_name = library_fullpath;
#else
  _libraries_to_load.library_name = "fake_bsp/libinvalidbsp.so";
#endif
  _libraries_to_load.next = nullptr;
  test_hal = acl_mmd_get_system_definition(&sysdef, &_libraries_to_load);
  CHECK(test_hal == nullptr);

#ifdef _WIN32
  GetFullPathName("fake_bsp/missingfuncbsp.dll", MAX_PATH, library_fullpath, 0);
  _libraries_to_load.library_name = library_fullpath;
#else
  _libraries_to_load.library_name = "fake_bsp/libmissingfuncbsp.so";
#endif
  _libraries_to_load.next = nullptr;
  test_hal = acl_mmd_get_system_definition(&sysdef, &_libraries_to_load);
  CHECK(test_hal == nullptr);
}

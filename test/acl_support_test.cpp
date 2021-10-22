// Copyright (C) 2012-2021 Intel Corporation
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
#include <acl_support.h>
#include <acl_thread.h>

#include "acl_test.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <glob.h>
#endif

TEST_GROUP(support){void setup(){acl_lock();
acl_test_setup_generic_system();
}
void teardown() {
  acl_test_teardown_generic_system();
  acl_unlock();
  acl_test_run_standard_teardown_checks();
}

std::string myrealpath(const std::string &path) {
  auto result = acl_realpath_existing(path);
  acl_print_debug_msg("acl_realpath_existing('%s') -> '%s'\n", path.c_str(),
                      result.c_str());

  if (result != "") {
#ifndef _WIN32
    CHECK_EQUAL('/', result[0]);
#endif
  }
  return result;
}
}
;

TEST(support, make_path_to_dir) {
  CHECK_EQUAL(0, acl_make_path_to_dir(""));
  CHECK_EQUAL(1, acl_make_path_to_dir("/"));
#ifdef _WIN32
  // Console device
  CHECK_EQUAL(0, acl_make_path_to_dir("CON"));
#else
  // This is a special file on Linux
  CHECK_EQUAL(0, acl_make_path_to_dir("/dev/mem"));
  CHECK_EQUAL(0, acl_make_path_to_dir("///not-there"));
#endif
  CHECK_EQUAL(1, acl_make_path_to_dir("."));

  CHECK_EQUAL(0, system("rm -rf ./.not-there"));
  CHECK_EQUAL(1, acl_make_path_to_dir("./.not-there"));
  CHECK_EQUAL(1, acl_make_path_to_dir("./.not-there/or/there")); // deep

  CHECK_EQUAL(1, acl_make_path_to_dir("./.not-there/or/there///"));
  CHECK_EQUAL(1, acl_make_path_to_dir("./.not-there/either////here"));

#ifdef _WIN32
  CHECK_EQUAL(1, acl_make_path_to_dir("c:\\"));
#else
  CHECK_EQUAL(1, acl_make_path_to_dir("///"));
#endif

  // An existing plain file. Should fail to make dir.
  CHECK_EQUAL(0, system("rm -rf .is-here"));
  CHECK_EQUAL(0, system("touch .is-here"));
  CHECK_EQUAL(0, acl_make_path_to_dir(".is-here"));

  // Try an example of a path without a directory component in its name at
  // all.
  CHECK_EQUAL(0, system("rm -rf .not-here"));
  CHECK_EQUAL(1, acl_make_path_to_dir(".not-here"));
}

TEST(support, realpath) {

  CHECK(myrealpath(".") != "");
  CHECK(myrealpath("..") != "");

  CHECK_EQUAL(0, system("touch realpath_test.txt"));
  std::string this_file = "realpath_test.txt";
  auto rp = myrealpath(this_file);
  CHECK(this_file == rp.substr(rp.length() - this_file.length()));

#ifdef __linux__
  // And the something before ends in a pathsep.
  CHECK_EQUAL('/', rp[rp.length() - this_file.length() - 1]);
#endif
}

TEST(support, glob) {
  CHECK_EQUAL(0, system("rm -rf *.globtest"));

  // No hit
  auto result = acl_glob("*.globtest");
  CHECK(result.empty());

  // simple case. One
  CHECK_EQUAL(0, system("touch firstone.globtest"));
  result = acl_glob("*.globtest");
  CHECK_EQUAL(1, result.size());
  CHECK("firstone.globtest" == result[0]);

  // Multiple
  CHECK_EQUAL(0, system("touch another.globtest"));

  result = acl_glob("*.globtest");
  CHECK_EQUAL(2, result.size());
  CHECK((result[0] == "firstone.globtest" && result[1] == "another.globtest") ||
        (result[0] == "another.globtest" && result[1] == "firstone.globtest"));

  CHECK_EQUAL(0, system("rm -rf *.globtest"));
}

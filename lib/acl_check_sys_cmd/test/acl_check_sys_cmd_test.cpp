// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// Unit tests for system_cmd_is_valid() function

#include "acl_check_sys_cmd/acl_check_sys_cmd.h"
#include <cassert>
#include <iostream>

// Unit tests.
#define CHECK(fn_test, good_value)                                             \
  std::cout << #fn_test << " = " << fn_test << "\n";                           \
  assert(fn_test == good_value)

int main() {

  // these are example cmds that are ran via system()/popen() function calls

  // letters digits space > / _
  CHECK(system_cmd_is_valid("ls /sys/class/aclpci_a10_ref 2>/dev/null"), 1);

  // . -
  CHECK(system_cmd_is_valid("quartus_stp -t /scripts/find_jtag_cable.tcl 0x02"),
        1);

  // : '\'
  CHECK(system_cmd_is_valid(
            "quartus_stp -t D:\\scripts\\find_jtag_cable.tcl 0x02"),
        1);

  // " ; @
  CHECK(system_cmd_is_valid("quartus_pgm -c cable -m jtag -o \"P;file@16\""),
        1);

  // & = \n
  CHECK(system_cmd_is_valid("aoc -tidy >build.log 2>&1 -board=arria10 "
                            "-initial-dir=./mydir clfile -options\n"),
        1);

  // ~ \t '
  CHECK(system_cmd_is_valid("test ~ \t '"), 1);

  // invalid charaters
  CHECK(system_cmd_is_valid("test ` "), 0);
  CHECK(system_cmd_is_valid("test ! "), 0);
  CHECK(system_cmd_is_valid("test # "), 0);
  CHECK(system_cmd_is_valid("test $ "), 0);
  CHECK(system_cmd_is_valid("test % "), 0);
  CHECK(system_cmd_is_valid("test ^ "), 0);
  CHECK(system_cmd_is_valid("test * "), 0);
  CHECK(system_cmd_is_valid("test ( "), 0);
  CHECK(system_cmd_is_valid("test ) "), 0);
  CHECK(system_cmd_is_valid("test + "), 0);
  CHECK(system_cmd_is_valid("test { "), 0);
  CHECK(system_cmd_is_valid("test } "), 0);
  CHECK(system_cmd_is_valid("test [ "), 0);
  CHECK(system_cmd_is_valid("test ] "), 0);
  CHECK(system_cmd_is_valid("test | "), 0);
  CHECK(system_cmd_is_valid("test < "), 0);
  CHECK(system_cmd_is_valid("test , "), 0);
  CHECK(system_cmd_is_valid("test ? "), 0);

  std::cout << "All acl_check_sys_cmd tests completed successfully!\n";
  return 0;
}

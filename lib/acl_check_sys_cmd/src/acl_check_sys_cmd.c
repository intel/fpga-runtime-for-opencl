// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "acl_check_sys_cmd/acl_check_sys_cmd.h"

#define NUM_SPECIAL_CHAR 17

static int binary_search(const char *arr, int l, int r, char x) {
  while (l <= r) {
    int m = l + (r - l) / 2;
    if (arr[m] == x) {
      return m;
    }

    if (arr[m] < x) {
      l = m + 1;
    } else {
      r = m - 1;
    }
  }
  return -1;
}

int system_cmd_is_valid(const char *cmd) {
  // Following special charaters are allowed.
  // Important: must store them in (ASCII) order
  const char valid_special_char[NUM_SPECIAL_CHAR] = {
      '\t', '\n', ' ', '"', '&', '\'', '-', '.', '/',
      ':',  ';',  '=', '>', '@', '\\', '_', '~'};

  const char *c = cmd;
  while (*c != '\0') {
    if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') ||
        (*c >= 'a' && *c <= 'z') ||
        binary_search(valid_special_char, 0, NUM_SPECIAL_CHAR - 1, *c) != -1) {
      c++;
    } else {
      return 0;
    }
  }
  return 1;
}

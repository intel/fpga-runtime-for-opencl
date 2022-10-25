#!/bin/bash
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Read changelog entries from standard input and print each
# occurrence of a pull-request reference, e.g., [#123] to
# standard output, along with the link to the pull request.

set -eu -o pipefail

grep -o '\[#[0-9]\+\]' | sed 's,\[#\([0-9]\+\)\],&: https://github.com/intel/fpga-runtime-for-opencl/pull/\1,' | sort -V

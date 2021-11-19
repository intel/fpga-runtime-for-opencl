#!/bin/bash
# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install Intel FPGA SDK for OpenCL Pro Edition
# https://fpgasoftware.intel.com/

set -eu -o pipefail

parent_valgrind_report="$1"
child_valgrind_report="$1"

# remove ==29048== patern from beginning
sed -i -E "s/==.*== //" ${parent_valgrind_report}
sed -i -E "s/==.*== //" ${child_valgrind_report}
# remove memory address
sed -i -E "s/0x.*: //" ${parent_valgrind_report}
sed -i -E "s/0x.*: //" ${child_valgrind_report}
# remove file header
sed -i -e '1,/Parent PID/ d' ${parent_valgrind_report}
sed -i -e '1,/Parent PID/ d' ${child_valgrind_report}
# remove file ending
sed -i -e '/LEAK SUMMARY/Q' ${parent_valgrind_report}
sed -i -e '/LEAK SUMMARY/Q' ${child_valgrind_report}
# check what is added to second file
comm -13 ${parent_valgrind_report} ${child_valgrind_report}

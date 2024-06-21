#!/bin/bash
# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

set -eux -o pipefail

if ! command -v aoc &> /dev/null
then
    echo "Error: aoc could not be found on PATH"
    exit 1
fi

scripthome=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
AOCL_BOARD_PACKAGE_ROOT=$(readlink -f "$scripthome/../../board/a10_ref")
export AOCL_BOARD_PACKAGE_ROOT

# First compile example.cl
kernel='example'
board='a10_ref_small'
compile_arg='-cl-kernel-arg-info'
compute_hash='f3effd06c681436a828f7e11c6c1b037807ddba0'
src=$(readlink -f "$scripthome/../src/$kernel.cl")
dest="$scripthome/$kernel.aocr"

aoc -rtl -tidy -board="$board" -hash="$compute_hash" "$src" "$compile_arg" -o "$dest"

# Then compile foo.cl
kernel='foo'
board='a10_ref_small'
compile_arg=''
compute_hash='7128ac1c937694f5b54a12229f19e4ca3a494c16'
src=$(readlink -f "$scripthome/../src/$kernel.cl")
dest="$scripthome/$kernel.aocr"

aoc -rtl -tidy -board="$board" -hash="$compute_hash" "$src" "$compile_arg" -o "$dest"

# Clean up
rm -rf "$scripthome"/*.temp "$scripthome"/*.bc

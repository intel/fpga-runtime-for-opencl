#!/bin/bash
# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

set -eu -o pipefail

find . -regex '.*\.\(c\|cpp\|h\)' | sort | xargs clang-format -i "$@"

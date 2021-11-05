#!/bin/bash
# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install Intel FPGA SDK for OpenCL Pro Edition
# https://fpgasoftware.intel.com/

set -eu -o pipefail

installdir="$1"
major=21
minor=3
patch=0
build=170
version="$major.$minor.$patch.$build"
installer="AOCLProSetup-$version-linux.run"
installer_url="https://download.altera.com/akdlm/software/acdsinst/$major.$minor/$build/ib_installers/$installer"

curl -L -o "$installer" "$installer_url"
chmod +x "$installer"
./"$installer" --mode unattended --installdir "$installdir" --accept_eula 1

# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install Intel FPGA SDK for OpenCL Pro Edition
# https://fpgasoftware.intel.com/

Param(
  [Parameter(Mandatory)]$installdir,
  $major="21",
  $minor="3",
  $patch="0",
  $build="170",
  $version="$major.$minor.$patch.$build",
  $installer="AOCLProSetup-$version-windows.exe",
  $installer_url="https://download.altera.com/akdlm/software/acdsinst/$major.$minor/$build/ib_installers/$installer",
  $sha256="d7177d248e81ce2f2f228b4c99650a1baecb3de01d6b1842f2fe3d713a24b870"
)

$ErrorActionPreference = "Stop"

Invoke-WebRequest $installer_url -OutFile $installer

$download_sha256 = (Get-FileHash $installer -Algorithm SHA256).Hash
if ($download_sha256 -ne $sha256) {
  throw "SHA256 $download_sha256 of downloaded file does not match expected SHA256 $sha256"
}

Start-Process -Wait -FilePath $installer -ArgumentList --mode,unattended,--installdir,$installdir,--accept_eula,1

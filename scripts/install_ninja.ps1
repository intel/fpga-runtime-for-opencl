# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install Ninja build system
# https://ninja-build.org/

Param(
  [Parameter(Mandatory)]$installdir,
  $version="1.10.2",
  $archive="ninja-win.zip",
  $archive_url="https://github.com/ninja-build/ninja/releases/download/v$version/$archive",
  $sha256="bbde850d247d2737c5764c927d1071cbb1f1957dcabda4a130fa8547c12c695f"
)

$ErrorActionPreference = "Stop"

Invoke-WebRequest $archive_url -OutFile $archive

$download_sha256 = (Get-FileHash $archive -Algorithm SHA256).Hash
if ($download_sha256 -ne $sha256) {
  throw "SHA256 $download_sha256 of downloaded file does not match expected SHA256 $sha256"
}

Expand-Archive $archive $installdir

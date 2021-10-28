# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install zlib compression library
# https://zlib.net/

Param(
  [Parameter(Mandatory)]$installdir,
  $version="1.2.11",
  $builddir="zlib-$version",
  $archive="zlib-$version.tar.gz",
  $archive_url="https://zlib.net/$archive",
  $sha256="c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1"
)

$ErrorActionPreference = "Stop"

Invoke-WebRequest $archive_url -OutFile $archive

$download_sha256 = (Get-FileHash $archive -Algorithm SHA256).Hash
if ($download_sha256 -ne $sha256) {
  throw "SHA256 $download_sha256 of downloaded file does not match expected SHA256 $sha256"
}

tar -xzf $archive

Push-Location $builddir
nmake /F win32\Makefile.msc
Pop-Location

New-Item "$installdir\include","$installdir\lib" -ItemType Directory
Copy-Item "$builddir\zlib.h","$builddir\zconf.h" "$installdir\include"
Copy-Item "$builddir\zdll.lib","$builddir\zlib1.dll" "$installdir\lib"

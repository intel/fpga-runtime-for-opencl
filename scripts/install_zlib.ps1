# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install zlib compression library
# https://zlib.net/

Param(
  [Parameter(Mandatory)]$installdir,
  $version="1.2.13",
  $builddir="zlib-$version",
  $archive="zlib-$version.tar.gz",
  $archive_url="https://zlib.net/fossils/$archive",
  $sha256="b3a24de97a8fdbc835b9833169501030b8977031bcb54b3b3ac13740f846ab30"
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

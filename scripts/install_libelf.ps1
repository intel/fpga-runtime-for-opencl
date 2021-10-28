# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Install Libelf library by Michael Riepe
# https://web.archive.org/web/20190223180146/http://www.mr511.de/software/english.html

Param(
  [Parameter(Mandatory)]$installdir,
  $version="0.8.13",
  $builddir="libelf-$version",
  $archive="libelf-$version.tar.gz",
  $archive_url="https://ftp.netbsd.org/pub/pkgsrc/distfiles/$archive",
  $sha256="591a9b4ec81c1f2042a97aa60564e0cb79d041c52faa7416acb38bc95bd2c76d"
)

$ErrorActionPreference = "Stop"

Invoke-WebRequest $archive_url -OutFile $archive

$download_sha256 = (Get-FileHash $archive -Algorithm SHA256).Hash
if ($download_sha256 -ne $sha256) {
  throw "SHA256 $download_sha256 of downloaded file does not match expected SHA256 $sha256"
}

tar -xzf $archive

# Patch DEF file to ensure the DLL exports the non-deprecated
# versions of the functions, which are used by pkg_editor.
(Get-Content "$builddir\lib\libelf.def") `
  -replace "elf_getphnum","elf_getphdrnum" `
  -replace "elf_getshnum","elf_getshdrnum" `
  -replace "elf_getshstrndx","elf_getshdrstrndx" `
  | Set-Content "$builddir\lib\libelf.def"

Push-Location "$builddir\lib"
.\build.bat
Pop-Location

New-Item "$installdir\include\libelf","$installdir\lib" -ItemType Directory
Copy-Item "$builddir\lib\elf_repl.h","$builddir\lib\libelf.h","$builddir\lib\sys_elf.h" "$installdir\include\libelf"
Copy-Item "$builddir\lib\libelf.lib","$builddir\lib\libelf.dll" "$installdir\lib"

REM Copyright (C) 2021 Intel Corporation
REM SPDX-License-Identifier: BSD-3-Clause

WHERE /q aoc
IF ERRORLEVEL 1 (
    ECHO Error: aoc could not be found on PATH
    EXIT /B
)

SET scripthome=%~dp0
SET OLD_BOARD_PACKAGE_ROOT=%AOCL_BOARD_PACKAGE_ROOT%
SET AOCL_BOARD_PACKAGE_ROOT=%scripthome%/../../board/a10_ref

REM First compile example.cl
SET kernel=example
SET board=a10_ref_small
SET compile_arg=-cl-kernel-arg-info
SET compute_hash=f3effd06c681436a828f7e11c6c1b037807ddba0
SET src=%scripthome%\..\src\%kernel%.cl
SET dest=%scripthome%\%kernel%.aocr

aoc -rtl -tidy -board=%board% -hash=%compute_hash% %src% %compile_arg% -o %dest%

REM Then compile foo.cl
SET kernel=foo
SET board=a10_ref_small
SET compile_arg=
SET compute_hash=7128ac1c937694f5b54a12229f19e4ca3a494c16
SET src=%scripthome%/../src/%kernel%.cl
SET dest=%scripthome%/%kernel%.aocr

aoc -rtl -tidy -board=%board% -hash=%compute_hash% %src% %compile_arg% -o %dest%

REM Clean up
DEL /f %scripthome%\*.temp %scripthome%\*.bc

REM Unset variables
SET AOCL_BOARD_PACKAGE_ROOT=%OLD_BOARD_PACKAGE_ROOT%
SET scripthome=
SET kernel=
SET board=
SET compile_arg=
SET compute_hash=
SET src=
SET dest=

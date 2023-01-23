# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]
### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security

[Unreleased]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2023.1...HEAD

## [2023.1] - 2023-03-23

### Added
- Enable new CSR protocol from the compiler with separated start registers ([#167], [#200]).
- Perform a memory copy for simulation buffer with buffer location ([#214]).
- Implement runtime support for device global with init_mode reprogram ([#161]).
- Enabled `ACL_SUPPORT_DOUBLE` to support `cl_khr_fp64` ([#230]).

### Changed
- Refactor runtime multi-threading/synchronization support ([#152]).
- Skip CSR version check when cra_ring_root doesn't exist ([#195]).

### Fixed
- Fix integer conversion warning in `acl_hash.cpp`, `acl_hash_test.cpp` and `acl_threadsupport.cpp` ([#199], [#201]).
- Fix warnings in unit test BSP ([#204]).
- Fix coverity issue, `AUTO_CAUSES_COPY`, in `acl_usm.cpp` and `acl_context.cpp` ([#218], [#223]).
- Fix wrong printf formatter and uninitialized scalar variable issue in `acl_kernel.cpp` and `acl_pll.cpp` ([#221], [#224]).
- Fix `NEGATIVE_RETURNS` Coverity issue in `acl_device.cpp` ([#225]).
- Fix printf formatter issue in `acl_kernel_if.cpp` ([#220]).
- Cache context offline device setting specified by environment variables ([#235])

### Security
- Resolve multiple memory leaks and stack buffer overflow in the `pkg_editor` library and `pkg_editor_test` ([#190], [#191]).
- Fix undefined behaviour in acl_profiler_test ([#196], [#197]).
- Enable address sanitizer to catch memory safety issues ([#118]).
- Check for null pointer before dereference in `acl_svm.cpp`, `acl_kernel.cpp` and `acl_profiler.cpp` ([#202], [#203], [#206]).
- Unconditionally null-terminate output of `strncpy()` in pkg_editor ([#215])
- Fix various Coverity issues (memory leaks, wrong conditional statetment, dereference after null check ) in `acl_mem.cpp` and `acl_mem_test.cpp` ([#219]).

[2023.1]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2023.0...v2023.1
[#118]: https://github.com/intel/fpga-runtime-for-opencl/pull/118
[#152]: https://github.com/intel/fpga-runtime-for-opencl/pull/152
[#161]: https://github.com/intel/fpga-runtime-for-opencl/pull/161
[#167]: https://github.com/intel/fpga-runtime-for-opencl/pull/167
[#190]: https://github.com/intel/fpga-runtime-for-opencl/pull/190
[#191]: https://github.com/intel/fpga-runtime-for-opencl/pull/191
[#195]: https://github.com/intel/fpga-runtime-for-opencl/pull/195
[#196]: https://github.com/intel/fpga-runtime-for-opencl/pull/196
[#197]: https://github.com/intel/fpga-runtime-for-opencl/pull/197
[#199]: https://github.com/intel/fpga-runtime-for-opencl/pull/199
[#200]: https://github.com/intel/fpga-runtime-for-opencl/pull/200
[#201]: https://github.com/intel/fpga-runtime-for-opencl/pull/201
[#202]: https://github.com/intel/fpga-runtime-for-opencl/pull/202
[#203]: https://github.com/intel/fpga-runtime-for-opencl/pull/203
[#204]: https://github.com/intel/fpga-runtime-for-opencl/pull/204
[#206]: https://github.com/intel/fpga-runtime-for-opencl/pull/206
[#214]: https://github.com/intel/fpga-runtime-for-opencl/pull/214
[#215]: https://github.com/intel/fpga-runtime-for-opencl/pull/215
[#218]: https://github.com/intel/fpga-runtime-for-opencl/pull/218
[#219]: https://github.com/intel/fpga-runtime-for-opencl/pull/219
[#220]: https://github.com/intel/fpga-runtime-for-opencl/pull/220
[#221]: https://github.com/intel/fpga-runtime-for-opencl/pull/221
[#223]: https://github.com/intel/fpga-runtime-for-opencl/pull/223
[#224]: https://github.com/intel/fpga-runtime-for-opencl/pull/224
[#225]: https://github.com/intel/fpga-runtime-for-opencl/pull/225
[#230]: https://github.com/intel/fpga-runtime-for-opencl/pull/230
[#235]: https://github.com/intel/fpga-runtime-for-opencl/pull/235

## [2023.0] - 2022-12-12

### Fixed
- Print kernel hang status only in debug mode ([#157]).
- Correct kernel IO debug messages ([#165]).

### Security
- Resolve multiple memory leaks and stack buffer overflow ([#154]).

[2023.0]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2022.3...v2023.0
[#154]: https://github.com/intel/fpga-runtime-for-opencl/pull/154
[#157]: https://github.com/intel/fpga-runtime-for-opencl/pull/157
[#165]: https://github.com/intel/fpga-runtime-for-opencl/pull/165

## [2022.3] - 2022-10-28

### Added

- Support `CL_MEM_ALLOC_BUFFER_LOCATION_INTEL` property ([#34], [#46], [#88]).
- Support early print buffer flushing in debug mode ([#61]).
- Document how buffers are managed in the runtime ([#81]).
- Add `cl_intel_unified_shared_memory` to supported extensions ([#94]).
- Parse `device_global` memory definitions from auto-discovery string ([#100], [#110]).
- Enable macro `MEM_DEBUG_MSG` in debug mode ([#106]).
- Print warning when kernel argument has multiple `buffer_location` attributes ([#109]).
- Support streaming kernels ([#103], [#108], [#114], [#115], [#142], [#145], [#146], [#153], [#158]).
- Document purpose of FPGA Runtime for OpenCL ([#102]).
- Support Ubuntu 22.04 LTS ([#112], [#113], [#182]).
- Document phase-locked-loop settings ([#131]).
- Support `buffer_location` for shared/host USM allocations ([#141], [#149]).
- Read environment variable `INTELFPGA_SIM_DEVICE_SPEC_DIR` ([#156]).
- Document issue where multiple runtime libraries get linked ([#166]).

### Changed

- Update Khronos OpenCL headers ([#62], [#67], [#68]).
- Support Rocky Linux 8 since CentOS 8 was discontinued ([#72], [#73]).
- Require C++17 compiler support ([#75]).

### Fixed

- Load `libz.so.1` alongside `libz.so` on Linux ([#69]).
- USM device allocations when simulating multiple global memories ([#104]).
- Consistently enable verbose output for all tests ([#121]).
- Inaccessible links to MMD functions in documentation ([#128]).

### Security

- Compile with hardening flags ([#48]).
- Add CMake flag `ACL_TSAN` to build with thread sanitizer ([#63], [#76]).
- Fix data races detected by thread sanitizer ([#74], [#89]).
- Fix compiler warnings ([#77], [#83], [#84], [#86], [#87]).
- Update zlib to 1.2.13 on Windows ([#169]).

[2022.3]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2022.2...v2022.3
[#34]: https://github.com/intel/fpga-runtime-for-opencl/pull/34
[#46]: https://github.com/intel/fpga-runtime-for-opencl/pull/46
[#48]: https://github.com/intel/fpga-runtime-for-opencl/pull/48
[#61]: https://github.com/intel/fpga-runtime-for-opencl/pull/61
[#62]: https://github.com/intel/fpga-runtime-for-opencl/pull/62
[#63]: https://github.com/intel/fpga-runtime-for-opencl/pull/63
[#67]: https://github.com/intel/fpga-runtime-for-opencl/pull/67
[#68]: https://github.com/intel/fpga-runtime-for-opencl/pull/68
[#69]: https://github.com/intel/fpga-runtime-for-opencl/pull/69
[#72]: https://github.com/intel/fpga-runtime-for-opencl/pull/72
[#73]: https://github.com/intel/fpga-runtime-for-opencl/pull/73
[#74]: https://github.com/intel/fpga-runtime-for-opencl/pull/74
[#75]: https://github.com/intel/fpga-runtime-for-opencl/pull/75
[#76]: https://github.com/intel/fpga-runtime-for-opencl/pull/76
[#77]: https://github.com/intel/fpga-runtime-for-opencl/pull/77
[#81]: https://github.com/intel/fpga-runtime-for-opencl/pull/81
[#83]: https://github.com/intel/fpga-runtime-for-opencl/pull/83
[#84]: https://github.com/intel/fpga-runtime-for-opencl/pull/84
[#86]: https://github.com/intel/fpga-runtime-for-opencl/pull/86
[#87]: https://github.com/intel/fpga-runtime-for-opencl/pull/87
[#88]: https://github.com/intel/fpga-runtime-for-opencl/pull/88
[#89]: https://github.com/intel/fpga-runtime-for-opencl/pull/89
[#94]: https://github.com/intel/fpga-runtime-for-opencl/pull/94
[#100]: https://github.com/intel/fpga-runtime-for-opencl/pull/100
[#102]: https://github.com/intel/fpga-runtime-for-opencl/pull/102
[#103]: https://github.com/intel/fpga-runtime-for-opencl/pull/103
[#104]: https://github.com/intel/fpga-runtime-for-opencl/pull/104
[#106]: https://github.com/intel/fpga-runtime-for-opencl/pull/106
[#108]: https://github.com/intel/fpga-runtime-for-opencl/pull/108
[#109]: https://github.com/intel/fpga-runtime-for-opencl/pull/109
[#110]: https://github.com/intel/fpga-runtime-for-opencl/pull/110
[#112]: https://github.com/intel/fpga-runtime-for-opencl/pull/112
[#113]: https://github.com/intel/fpga-runtime-for-opencl/pull/113
[#114]: https://github.com/intel/fpga-runtime-for-opencl/pull/114
[#115]: https://github.com/intel/fpga-runtime-for-opencl/pull/115
[#121]: https://github.com/intel/fpga-runtime-for-opencl/pull/121
[#128]: https://github.com/intel/fpga-runtime-for-opencl/pull/128
[#131]: https://github.com/intel/fpga-runtime-for-opencl/pull/131
[#141]: https://github.com/intel/fpga-runtime-for-opencl/pull/141
[#142]: https://github.com/intel/fpga-runtime-for-opencl/pull/142
[#145]: https://github.com/intel/fpga-runtime-for-opencl/pull/145
[#146]: https://github.com/intel/fpga-runtime-for-opencl/pull/146
[#149]: https://github.com/intel/fpga-runtime-for-opencl/pull/149
[#153]: https://github.com/intel/fpga-runtime-for-opencl/pull/153
[#156]: https://github.com/intel/fpga-runtime-for-opencl/pull/156
[#158]: https://github.com/intel/fpga-runtime-for-opencl/pull/158
[#166]: https://github.com/intel/fpga-runtime-for-opencl/pull/166
[#169]: https://github.com/intel/fpga-runtime-for-opencl/pull/169
[#182]: https://github.com/intel/fpga-runtime-for-opencl/pull/182

## [2022.2] - 2022-04-01

### Added

- Document running unit tests individually ([#35]).
- Document internal use of pre-C++11 standard library ABI ([#31], [#39]).
- Document recommended settings for forked repository ([#44]).
- Implement OpenCL Core 3.0 clCreateBufferWithProperties ([#53]).
- Document running clang-format ([#60]).
- Document git identity setup ([#70]).

### Changed

- Support Rocky Linux 8 instead of CentOS 8 ([#72], [#73]).

### Fixed

- Simulation of systems with multiple global memories ([#29]).
- Explicitly require C++11 compiler support ([#47]).
- Link failure with `-Wl,--no-undefined` ([#49]).

### Security

- Check for null pointer before dereference ([#42], [#43]).
- Update Windows installation script for zlib to 1.2.12 ([#96]).

[2022.2]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2022.1...v2022.2
[#29]: https://github.com/intel/fpga-runtime-for-opencl/pull/29
[#31]: https://github.com/intel/fpga-runtime-for-opencl/pull/31
[#35]: https://github.com/intel/fpga-runtime-for-opencl/pull/35
[#39]: https://github.com/intel/fpga-runtime-for-opencl/pull/39
[#42]: https://github.com/intel/fpga-runtime-for-opencl/pull/42
[#43]: https://github.com/intel/fpga-runtime-for-opencl/pull/43
[#44]: https://github.com/intel/fpga-runtime-for-opencl/pull/44
[#47]: https://github.com/intel/fpga-runtime-for-opencl/pull/47
[#49]: https://github.com/intel/fpga-runtime-for-opencl/pull/49
[#53]: https://github.com/intel/fpga-runtime-for-opencl/pull/53
[#60]: https://github.com/intel/fpga-runtime-for-opencl/pull/60
[#70]: https://github.com/intel/fpga-runtime-for-opencl/pull/70
[#72]: https://github.com/intel/fpga-runtime-for-opencl/pull/72
[#73]: https://github.com/intel/fpga-runtime-for-opencl/pull/73
[#96]: https://github.com/intel/fpga-runtime-for-opencl/pull/96

## [2022.1] - 2021-12-01

- Initial public release.

[2022.1]: https://github.com/intel/fpga-runtime-for-opencl/releases/tag/v2022.1

# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]
### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security

[Unreleased]: https://github.com/intel/fpga-runtime-for-opencl/compare/v2022.2...HEAD

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

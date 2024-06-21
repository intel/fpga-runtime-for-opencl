# Precompiled Binaries for Runtime Unit Tests

This directory contains precompiled binaries that the runtime unit tests could use.

When the environment variable `ACL_TEST_EXAMPLE_BINARY` is set, precompiled binaries
will be used for the unit tests, instead of compiling with aoc in-time. The environment
variable is set to this directory by default by the unit test CMake configuration.

## Unit Tests Using AOC compiler

During setup, the runtime loads an example binary that may be obtained and used by
any runtime unit tests. The loading is done in the function `l_load_example_binary`
of acl_test.cpp, and the loaded binary can be obtained by calling the function
`acl_test_get_example_binary`.

Other than this, the program `from_source` test group contains two tests that either
requires the aoc compiler, or the precompiled binaries to be present:

- `make_prog_dir_and_build_command`
- `online_mode`

## Overriding the Binaries to be Used

To use precompiled binaries contained in another directory, set `ACL_TEST_EXAMPLE_BINARY`
to the root of that directory, and make sure there is a `linux` subdirectory for linux
precompiled binaries and a `windows` subdirectory for windows precompile binaries.

## Updating the Binaries

In case binaries have to be updated, make sure the aoc compiler is available, and
run the compile scripts contained in each of the corresponding OS subdirectories:

```
# On Linux
sh linux/compile_aocr.sh
# On Windows
windows\compile_aocr.sh
```

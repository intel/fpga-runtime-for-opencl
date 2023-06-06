# Fuzz Testing

## Context
"In programming and software development, fuzzing or fuzz testing is an automated software testing technique that involves providing invalid, unexpected, or random data as inputs to a computer program. The program is then monitored for exceptions such as crashes, failing built-in code assertions, or potential memory leaks."

## How to do fuzz testing on Github 
1. Click on the `Actions` tab
2. Click on `Runtime Fuzz Testing` workflow
3. Click on `Run workflow` (Dropdown button)
4. Enter the number of iterations you want a single variable to be mutated
5. Click on `Run workflow` (Green button)
6. Wait until the test finishes
7. Inside the workflow run, click `Peek results` to look at the results
8. Download the artifact from the `Job Summary` to view the full output & result

## How to do fuzz testing locally 
1. Follow [Prerequisites](https://github.com/intel/fpga-runtime-for-opencl#prerequisites) & [Build](https://github.com/intel/fpga-runtime-for-opencl#building-the-runtime) & [Test](https://github.com/intel/fpga-runtime-for-opencl#building-the-runtime) instructions to get a working build. Make sure that the unit tests finishes successfully.
2. Install Radamsa 0.6+, Python 3.10.7+ and Pyyaml/6.0+
3. Run the following at the top level in the runtime repo
```
mkdir -p build/fuzz_testing
cd build/fuzz_testing
CC="${CC:-gcc}" CXX="${CXX:-g++}" cmake -G Ninja ../.. -DCMAKE_BUILD_TYPE=Debug -DACL_CODE_COVERAGE=ON -DACL_TSAN=OFF -DACL_WITH_ASAN=ON -DFUZZ_TESTING=ON "$@"
ninja -v
cd fuzz_testing/script
export AOCL_BOARD_PACKAGE_ROOT="$(git rev-parse --show-toplevel)/test/board/a10_ref"
python3 fuzz_test.py --all
```
4. A results directory and a test_output directory will be created after the fuzz test finishes

## Classifying Failures
- Address Sanitizer Error (ASAN Errors): Any errors caught by address sanitizer such as memory leaks (Not acceptable)
- Aborted Runs: The test aborted/crashed during execution (Not acceptable)
- Assertion Failures: The test failed due to an assertion (Acceptable)
- Failed Runs: The test failed because the CHECK statements failed (Acceptable)
- Hangs: The program did not terminate (Not acceptable)
- Successful runs: The test succeeded (Acceptable)
- Test errors: The test failed due to bugs in the fuzz testing infrastructure (Acceptable)

## Additional Notes
- Currently we are doing most of the fuzz testing on auto discovery strings because these strings are indirectly given by the user. 
- The fuzz tests are initialized based on the current unit tests, however they should be regarded as separate tests. (i.e. You can make fuzz_test/unit_test only changes)

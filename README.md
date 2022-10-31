# Intel® FPGA Runtime for OpenCL™ Software Technology

## Prerequisites

### Linux

-   Red Hat Enterprise Linux (RHEL)\* or Rocky Linux\* 8
-   SUSE Linux Enterprise Server (SLES)\* or openSUSE Leap\* 15
-   Ubuntu\* 18.04, 20.04, or 22.04 LTS
-   GCC 7.4.0 and higher
-   [CMake](https://cmake.org/) 3.10 and higher
-   [Ninja](https://ninja-build.org/) 1.8.2 and higher
-   [Git](https://git-scm.com/)
-   libelf
-   zlib (only needed for simulation flow)

### Windows

-   Windows\* 10 (64 bit)
-   Windows Server\* 2012, 2016, or 2019
-   Microsoft Visual C++ (MSVC)\* 2017 and higher
-   [CMake](https://cmake.org/) 3.10 and higher
-   [Ninja](https://ninja-build.org/) 1.8.2 and higher
-   [Git](https://git-scm.com/)
-   [Cygwin](https://cygwin.com/)
-   libelf (install with [`scripts/install_libelf.ps1`](https://github.com/intel/fpga-runtime-for-opencl/blob/main/scripts/install_libelf.ps1))
-   zlib (only needed for simulation flow, install with [`scripts/install_zlib.ps1`](https://github.com/intel/fpga-runtime-for-opencl/blob/main/scripts/install_zlib.ps1))

## Building the Runtime

### Build Instructions for Linux and Windows

Perform these steps to build the runtime:

1.  Clone and change to the runtime repository.

    ```
    git clone https://github.com/intel/fpga-runtime-for-opencl
    cd fpga-runtime-for-opencl
    ```

2.  Switch to the [latest release](https://github.com/intel/fpga-runtime-for-opencl/releases) version.

    ```
    git checkout v2023.0
    ```

3.  Create and change to the runtime build directory.

    ```
    mkdir build
    cd build
    ```

4.  Generate the Ninja build files using the `cmake` command.

    ```
    cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install
    ```

5.  Build the runtime.

    ```
    ninja
    ```

6.  Install the runtime to the runtime installation directory.

    ```
    ninja install
    ```


### Build Options

-   Build without debugging symbols and with optimization.

    ```
    cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install
    ```

-   Build with debugging symbols and without optimization.

    ```
    cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install
    ```

-   Use GCC as the compiler instead of the system default (for Linux only).

    ```
    CC=gcc CXX=g++ cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install
    ```

-   Specify installation paths to libelf and zlib (for Windows only).

    ```
    cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\libelf;C:\zlib -DCMAKE_INSTALL_PREFIX=../install
    ```

    Alternatively, specify header and library directories explicitly.

    ```
    cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INCLUDE_PATH=C:\libelf\include;C:\zlib\include -DCMAKE_LIBRARY_PATH=C:\libelf\lib;C:\zlib\lib -DCMAKE_INSTALL_PREFIX=../install
    ```

## Running Unit Tests 

To run unit tests, execute the following command in the runtime build directory:

```
ctest -V
```

### Notes

-   To run unit tests, you need the `aoc` executable from the IntelⓇ FPGA SDK
    for OpenCL™.

    To set the path to `aoc`, change to the IntelⓇ FPGA SDK for OpenCL™
    installation directory and source the initialization script.

    -   On Linux:

        ```
        source init_opencl.sh
        ```

    -   On Windows:

        ```
        call init_opencl.bat
        ```

-   On Linux, `aoc` requires the `libtinfo.so.5` library, which you can install
    using one of the following OS-specific commands:

    -   Red Hat Enterprise Linux (RHEL)\* or Rocky Linux\* 8:

        ```
        sudo yum install ncurses-compat-libs-6.1
        ```

    -   SUSE Linux Enterprise Server (SLES)\* or openSUSE Leap\* 15:

        ```
        sudo zypper install libncurses5
        ```

    -   Ubuntu\* 18.04 or 20.04 LTS:

        ```
        sudo apt install libtinfo5
        ```

-   On Windows, you need to set the paths to the libelf and (optionally) zlib libraries.

    ```
    set PATH=C:\libelf\lib;C:\zlib\lib;%PATH%
    ```

-   Running unit tests is not supported for ARM cross compilation.

-   To run unit tests individually, first set the board package directory:

    ```
    export AOCL_BOARD_PACKAGE_ROOT="$(git rev-parse --show-toplevel)/test/board/a10_ref"
    ```

    Switch to the directory containing the test executable:

    ```
    cd build/test
    ```

    Run the tests whose [group and name contain the given substrings](https://github.com/cpputest/cpputest/blob/master/README.md#command-line-switches):

    ```
    ./acl_test -v -g acl_mem -n create_buffer
    ```

## Using the Runtime

### With IntelⓇ FPGA SDK for OpenCL™ or IntelⓇ FPGA RTE for OpenCL™ 

Perform these steps if you have the IntelⓇ FPGA SDK for OpenCL™ or
IntelⓇ FPGA RTE for OpenCL™ installed on your system:

1.  In the IntelⓇ FPGA SDK for OpenCL™ installation directory, source
    the initialization script (see [Running Unit Tests](#running-unit-tests)).

2.  Update the `/etc/OpenCL/vendors/Altera.icd` file with either the filename
    `libalteracl.so` if `LD_LIBRARY_PATH` contains the full path of the runtime
    installation directory; otherwise, the full path of `libalteracl.so` itself.

3.  Compile your OpenCL host program with the OpenCL header files included from
    the runtime installation directory.

    ```
    -Ifpga-runtime-for-opencl/install/include
    ```

4.  Link your OpenCL host program with the `libOpenCL.so` library from the
    IntelⓇ FPGA SDK for OpenCL™ installation directory.

    ```
    -L$INTELFPGAOCLSDKROOT/host/linux64/lib -lOpenCL
    ```

5.  Run your OpenCL host program.

### Without IntelⓇ FPGA SDK for OpenCL™ or IntelⓇ FPGA RTE for OpenCL™

Perform these steps if you do not have the IntelⓇ FPGA SDK for OpenCL™
or IntelⓇ FPGA RTE for OpenCL™ installed on your system:

1.  Download, build, and install the
    [OpenCL ICD Loader](https://github.com/KhronosGroup/OpenCL-ICD-Loader).

2.  Create the `/etc/OpenCL/vendors/Altera.icd` file with either the filename
    `libalteracl.so` if `LD_LIBRARY_PATH` contains the full path of the runtime
    installation directory; otherwise, the full path of `libalteracl.so` itself.

3.  Compile your OpenCL host program with the OpenCL header files included from
    the runtime installation directory.

    ```
    -Ifpga-runtime-for-opencl/install/include
    ```

5.  Link your OpenCL host program with the `libOpenCL.so` library from the
    OpenCL ICD Loader.

    ```
    -lOpenCL
    ```

6.  Run your OpenCL host program.

### Notes

-   When setting the environment variable [`OCL_ICD_FILENAMES`] for debugging,
    ensure it doesn't resolve to any `libalteracl.so` other than the one
    specified in the `/etc/OpenCL/vendors/Altera.icd` file. Mismatches may
    lead to multiple runtime library instances being linked into the program,
    causing undefined behaviour.

[`OCL_ICD_FILENAMES`]: https://github.com/KhronosGroup/OpenCL-ICD-Loader/blob/c5a6e013ad7c8b379fc94e3c849aa3396900a63c/README.md#table-of-debug-environment-variables

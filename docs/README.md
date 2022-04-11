# FPGA Runtime for OpenCL documentation

## OpenCL background

[Khronos® OpenCL™] is a standard for writing programs that run across
heterogeneous platforms consisting of FPGA, CPU, GPU, DSP, etc. The [Intel® FPGA
Runtime for OpenCL™ Software Technology] implements the [OpenCL 1.2] standard
including selected vendor extensions.

## What is the FPGA runtime?

The following components work together to program an [Intel® FPGA]:

1. host program and host compiler
2. OpenCL kernel(s) and offline compiler
3. [Custom Platform]

The runtime is responsible for executing the host program (1) and queueing the
kernel to be executed on the FPGA device. It takes in the hardware programming
image from the compiler (2), builds the contained programs, allocates
host/shared/device memory, programs devices, enqueues and executes the kernels,
and collects kernel execution status signaled from the memory-mapped device
([MMD](../include/MMD/aocl_mmd.h)). Each step is described in the following.

## Why is the FPGA runtime needed?

FPGA hardware design deals with I/O constraints, protocols, monitoring, DDR
calibration, and memory dependencies. The FPGA runtime provides a layer of
abstraction to ease the development flow.

### Memory

Each kernel execution may require accessing device memory. The device memory
needs to be reserved/allocated before kernel execution. The runtime keeps track
of the memory allocation. There are two main types of memory allocations:
[buffers](buffers.md) and unified shared memory (USM).

### Kernels

Kernels are data-parallel functions that extend C99 for parallelism and memory
hierarchy.

### Queues

Queues manage buffer mapping, memory migration, device programming, and kernel
execution. A queue receives asynchronous command completion notifications from
the device. A queue also manages dependencies between commands using events and
barriers.

### Profiling

The runtime keeps track of the status of a kernel execution to record the
wall time of each stage. This provides the user with insights into the
performance of their program.

## User flow

For more information on how users interact with each of the above components,
see the [Intel® FPGA SDK for OpenCL™ Pro Edition: Programming Guide].

## How to contribute

To contribute to the runtime, see [CONTRIBUTING.md](../CONTRIBUTING.md).
To build, test, and use the runtime, see [README.md](../README.md).

[Khronos® OpenCL™]: https://www.khronos.org/opencl/
[OpenCL 1.2]: https://www.khronos.org/registry/OpenCL/sdk/1.2/docs/man/xhtml/
[Intel® FPGA]: https://www.intel.com/content/www/us/en/products/programmable.html
[Intel® FPGA Runtime for OpenCL™ Software Technology]: https://github.com/intel/fpga-runtime-for-opencl
[Intel® FPGA SDK for OpenCL™ Pro Edition: Programming Guide]: https://www.intel.com/content/www/us/en/docs/programmable/683846/
[Custom Platform]: https://www.intel.com/content/www/us/en/docs/programmable/683085/

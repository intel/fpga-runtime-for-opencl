# Buffers

Buffer handles automatic memory migration between the host and the device or between devices when the kernel needs to access the memory. This helps you avoid copying memory from the host to the device manually.

A synopsis of buffer use is as follows:

```cpp
#define GMEM_DATA_VEC 8

cl_platform_id platform;
cl_int err;
// General setups
err = clGetPlatformIDs(1, &platform, NULL);
err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
cl_command_queue queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
// aocx_size is the size of aocx file
// aocx_contents is the content of aocx file
cl_program program = clCreateProgramWithBinary(context, 1, &device, &aocx_size, (const unsigned char **)&aocx_contents, &binary_status, &err);
cl_kernel kernel = clCreateKernel(program, "kernel_name", &err);

// Host side data that contains the information needed for kernel execution
unsigned int *host_data = (unsigned int *)acl_aligned_malloc(global_work_size*sizeof(unsigned int)*GMEM_DATA_VEC);

// Set up the expectation for a buffer, but not actually allocating memory yet
cl_mem mem = clCreateBuffer(context, CL_MEM_READ_WRITE, global_work_size*sizeof(unsigned int)*GMEM_DATA_VEC, NULL, &err);

// Enqueue a memory transfer operation
cl_event event;
err = clEnqueueWriteBuffer(queue, mem, CL_FALSE, 0, global_work_size*sizeof(unsigned int)*GMEM_DATA_VEC, host_data, 0, NULL, &event);

// Informs the kernel that one of its arguments is the buffer
err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &mem);

// Submit kernel execution to queue
err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, &global_work_size_2d[0], &local_work_size_2d[0], 0, NULL, &event);

... // Block on kernel finish and clean up resources
```

## Summary

* Buffers are not allocated on the device until the kernel executes, the `clEnqueueWriteBuffer` function is called, or the `clCreateBuffer` function is called with the `CL_MEM_COPY_HOST_PTR` flag.
* Buffer handles memory migration between the host and device automatically.
* SYCL* runtime typically calls OpenCL functions in a different order in comparison to FPGA Runtime for OpenCL's preferred order (Call orders are expanded in section [SYCL Runtime Flow](#sycl-runtime-flow)). The difference in order of calls sometimes leads to an unexpected result (eg. extra host to device memory copy), but you can avoid it if the `clSetKernelArg` function does not modify `cl_mem` attributes.
* The runtime keeps track of which memory addresses are occupied and decides where the next allocation should be. Specifically, it uses a first-fit allocation algorithm.
* The actual memory allocation is made with MMD calls by passing the device address and size.
* Device address has a special encoding, which is the same between buffer and USM pointers.
* Even if you do not explicitly transfer the memory before launching the kernel, the memory is still migrated right before the kernel executes (i.e., `clEnqueueWriteBuffer` is not necessary).

## SYCL Runtime Flow

SYCL* runtime calls the memory operation in the following order, which are explained in the subsections below:
1. `clCreateBuffer`
2. `clEnqueueWriteBuffer`
3. `clSetKernelArg`
4. `clEnqueueKernel` / `clEnqueueTask`

FPGA Runtime for OpenCL prefered call order is as follows:
1. `clCreateBuffer`
2. `clSetKernelArg`
3. `clEnqueueWriteBuffer`
4. `clEnqueueKernel` / `clEnqueueTask`


**NOTE:** SYCL* runtime calls are not the preferred order from the FPGA Runtime for OpenCL perspective. However, SYCL runtime* cannot change the order of API invocations as it needs to handle other vendors' requirements. Not all vendors can get away with setting kernel argument first before enqueuing write buffer (see [discussion](https://github.com/intel/llvm/discussions/4627)).

### `clCreateBuffer`
When you call the [`clCreateBuffer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L408-L953) function, provide a host pointer if this buffer must move data from the host to other places.

**NOTE:** You can allocate the memory in different global memory and in a different bank within the same global memory.

When you specify the `CL_MEM_COPY_HOST_PTR` flag, the runtime does not know to which device the memory is going, so the runtime will always allocates the memory in the first device in context and submits to `auto_queue`. A context can contain multiple devices, leading to an error if you specify the `CL_MEM_COPY_HOST_PTR` flag. For more information about other available flags, see [OpenCL `clCreateBuffer` Spec](https://www.khronos.org/registry/OpenCL/sdk/1.0/docs/man/xhtml/clCreateBuffer.html).

### `clEnqueueWriteBuffer`
The [`clEnqueueWriteBuffer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L3475-L3510) function allocates memory space for buffer through the [`acl_bind_buffer_to_device`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L357-L406) function and enqueues a memory transfer to copy the memory through the [`l_enqueue_mem_transfer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4726-L5174) function.

#### Allocate Space
The [`acl_bind_buffer_to_device`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L357-L406) function is responsible for finalizing the buffer allocation and it is called only if the allocation is deferred. 

The `acl_bind_buffer_to_device` function first calls the [`acl_do_physical_buffer_allocation`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L256-L316) function to set the target global memory to the [default device global memory](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L277) (as indicated in the `board_spec.xml` file) if not specified. 

**NOTE:** The simulator does not know the memory interfaces of any device until a `.aocx` file is loaded, which usually happens after SYCL* calls the `clEnqueueWriteBuffer` function.

Then, the `acl_bind_buffer_to_device` function reserves the memory through the [`acl_allocate_block`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4310-L4565) function. The `acl_allocate_block` function attempts to allocate memory on the preferred bank. If it fails (i.e., the bank's memory is full), then `acl_allocate_block` function attempts to allocate memory the entire device's global memory. The `acl_allocate_block` function decides on the memory range it can allocate based on the information you provided regarding the device, global memory, and memory bank. It returns a range in the form of `[pointer_to_begin_address, pointer_to_end_address]` (achieved through [`l_get_working_range`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4253-L4308)). The specifics of how memory is reserved are described in [Memory Allocation Algorithm](#memory-allocation-algorithm).

**NOTE:** You can partition a single device's global memory into multiple banks (the partition can be interleaving or separate, with interleaving being the default). Interleaving memory provides more load balancing between memory banks. You can query which specific bank to access through runtime calls. For more information about memory banks, see [Global Memory Accesses Optimization](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-fpga-optimization-guide/top/optimize-your-design/throughput-1/memory-accesses/global-memory-accesses-optimization.html) topic in the *FPGA Optimization Guide for Intel(R) oneAPI Toolkits*.

Once the address range is set, the `acl_allocate_block` function returns the device address. The device address is different from the surface representation in runtime. Specifically, device address is a bitwise OR of the device ID and device pointer as formatted [here](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/include/acl.h#L264-L274).

Once the 2D list is ready and the current block allocation is set, the `acl_bind_buffer_to_device` function [enqueues a memory transfer](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4726-L5174) from the context's `unwrapped_host_mem` to the buffer's `for_enqueue_writes` as described in [Transfer Memory section](#transfer-memory).

##### Memory Allocation Algorithm
The memory allocation algorithm is first-fit allocation. The allocation starts from the beginning of the requested global memory and then searches for the next available space (gaps or ends) that satisfies the size requirement. If you request a specific memory bank, then the memory must be non-interleaving. When you specify a bank ID, the first-fit allocation algorithm starts at the address of `(((bank_id -1) % num_banks) * bank_size + the start of target global_mem)`. The implication of specifying a bank ID is that the consecutive memory allocation may not be adjacent to each other. Conversely, if you never specified a bank ID, the consecutive memory allocation should be adjacent, assuming there was no deallocation.

#### Transfer Memory
The second part of enqueue read/write buffer is to enqueue a memory transfer between the host and device, as implemented in [`l_enqueue_mem_transfer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4726-L5174).

The whole enqueue process is as follows:

1. Upon updating the command queues, memory transfer is submitted to the device operation queue by calling the function [`acl_submit_mem_transfer_device_op`](https://github.com/intel/fpga-runtime-for-opencl/blob/950f21dd079dfd55a473ba4122a4a9dca450e36f/src/acl_command.cpp#L343)
([definition](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L5313-L5392)). When the device operation is executed, the function [`acl_mem_transfer_buffer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L5395-L5409) is called that calls on the [`l_mem_transfer_buffer_explicitly`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L5791-L6246) function. 

2. The `l_mem_transfer_buffer_explicitly` function creates a pointer to pointer mapping between the source and destination buffer, copies the memories, and uses one of the following MMD functions to copy one byte from each pointer:

   * [`copy_hostmem_to_hostmem`](https://github.com/intel/fpga-runtime-for-opencl/blob/fc99b92704a466f7dc4d84bd45d465d64d03dbb0/src/acl_hal_mmd.cpp#L1680-L1694) - Uses `memcpy` system calls.
   * [`copy_hostmem_to_globalmem`](https://github.com/intel/fpga-runtime-for-opencl/blob/fc99b92704a466f7dc4d84bd45d465d64d03dbb0/src/acl_hal_mmd.cpp#L1696-L1716) - Calls the MMD function [`aocl_mmd_write`](https://github.com/intel/fpga-runtime-for-opencl/blob/eed67743e5204ecd425f71418507af65909521b1/include/MMD/aocl_mmd.h#L347-L352).
   * [`copy_globalmem_to_hostmem`](https://github.com/intel/fpga-runtime-for-opencl/blob/fc99b92704a466f7dc4d84bd45d465d64d03dbb0/src/acl_hal_mmd.cpp#L1718-L1739) - Calls the MMD function [`aocl_mmd_read`](https://github.com/intel/fpga-runtime-for-opencl/blob/eed67743e5204ecd425f71418507af65909521b1/include/MMD/aocl_mmd.h#L341-L346).
   * [`copy_globalmem_to_globalmem`](https://github.com/intel/fpga-runtime-for-opencl/blob/fc99b92704a466f7dc4d84bd45d465d64d03dbb0/src/acl_hal_mmd.cpp#L1763-L1873) - If the source and destination are on the same device, then runtime directly calls the MMD function [`aocl_mmd_copy`](https://github.com/intel/fpga-runtime-for-opencl/blob/eed67743e5204ecd425f71418507af65909521b1/include/MMD/aocl_mmd.h#L353-L357). Otherwise, it uses both [`aocl_mmd_read`](https://github.com/intel/fpga-runtime-for-opencl/blob/eed67743e5204ecd425f71418507af65909521b1/include/MMD/aocl_mmd.h#L341-L346) and [`aocl_mmd_write`](https://github.com/intel/fpga-runtime-for-opencl/blob/eed67743e5204ecd425f71418507af65909521b1/include/MMD/aocl_mmd.h#L347-L352) functions to copy from the source device to host and then from the host to the destination device. All operations are blocking. The runtime keeps calling MMD's `yield` (sleep function) function until read and write operations are completed.

### `clSetKernelArg`: What if the `clEnqueueWriteBuffer` function is not called?

When the `clEnqueueWriteBuffer` function is not called, memory transfer automatically happens before launching the kernel that uses the buffer. There is an enqueued memory transfer device operation before every kernel launch device operation. The only difference between calling and not calling the `enqueueWriteBuffer` function is whether the enqueued memory transfer actually copies the memory. 

To ensure the memory is transferred to the right place, the [`clSetKernelArg`](https://github.com/intel/fpga-runtime-for-opencl/blob/3f7a228133f92c63be5b04e222f3fc8ff72310e6/src/acl_kernel.cpp#L314-L725) function plays a crucial role. `clSetKernelArg` is responsible for the following:
* Informing the kernel that one of its arguments is that specific buffer.
* Setting correct buffer attributes (for example, global memory ID) according to the kernel argument's attribute.
* Creating and binding the host channel if the kernel argument is a host pipe.

**NOTE:** Given SYCL* calls the `clSetKernelArg` function after the `clEnqueueWriteBuffer` function, ensure to never change buffer properties inside the `clSetKernelArg` function.

### `clEnqueueKernel` / `clEnqueueTask`

During [kernel enqueue](https://github.com/intel/fpga-runtime-for-opencl/blob/3f7a228133f92c63be5b04e222f3fc8ff72310e6/src/acl_kernel.cpp#L1644-L2313), the [`l_copy_and_adjust_arguments_for_device`](https://github.com/intel/fpga-runtime-for-opencl/blob/3f7a228133f92c63be5b04e222f3fc8ff72310e6/src/acl_kernel.cpp#L2730-L2983) function is called to:
1. Create a temporary buffer object with an aligned copy of the buffer argument value.
2. Obtain the correct kernel's required buffer location.
3. Reserve space at the required device global memory if memory is not already reserved.
4. Copy the reserved address into the kernel invocation image.
5. Prepare the memory migration information containing this temporary buffer as the source and destination device ID as the target. 
   
**NOTES:**
       
   - At this point, the temporary buffer could have already gone through the memory transfer. The different action it has to perform based on whether memory transfer has already happened is being handled when migrating the buffer ( [`acl_mem_migrate_buffer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L5412-L5665)). That is, the `l_copy_and_adjust_arguments_for_device` function knows whether it should be moving from the host to device or device to device (generally, device to device operation is faster).

   - Device local pointer size is 4. Device global pointer size is always the device's address-bit integer divided by 8.

Before [submitting the kernel](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/src/acl_kernel.cpp#L2982-L3093) to the device queue, the `l_copy_and_adjust_arguments_for_device` function  verifies if the device is programmed. If the device is not programmed, runtime queues [reprogram device operation](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/src/acl_kernel.cpp#L3034) to program the device. Then, it [arranges memory migration](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/src/acl_kernel.cpp#L3043) for each kernel memory argument.

Unlike the device operation resulting from the enqueue read/write, the memory migration calls on the [`acl_mem_migrate_buffer`](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L5412-L5665) function, which means that the memory transfer and memory migration operations behave differently. 

#### Memory Migration

The `acl_mem_migrate_buffer` function performs the following actions:

1. Accepts the memory object that was passed into the `clSetKernelArg` object, the destination device ID, and the global memory ID.
2. Verifies if the buffer's 2D list has a memory object at the destination global memory.
3. Checks whether the `block_allocation` (current buffer's allocation) is the same as the one located in the destination global memory of the 2D list. If true, then the memory is already located in the right place, and there will be no copy operation in this case. If false, then it calls the same MMD function as the memory transfer to write from the host memory to the device memory (see the list of MMD functions in section [Transfer Memory](#transfer-memory)).

**NOTE:** The difference between memory migration and memory transfer is that the memory migration's functionality is almost a subset of memory transfer operation because memory transfer also handles the situation where you have offsets and checks on image buffers.

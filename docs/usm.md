# USM Pointers

Unlike buffers, user has to take care of memory migration, transfer manually.

## Summary

1. The memory allocation of usm device pointer are borrowed from buffers.
2. The underlying functionality of usm host and shared allocation are the same (except that they may be in different memory address range).

## Memory accessibility summary
<style type="text/css">
.tg  {border-collapse:collapse;border-spacing:0;}
.tg td{border-color:black;border-style:solid;border-width:1px;font-family:Arial, sans-serif;font-size:14px;
  overflow:hidden;padding:10px 5px;word-break:normal;}
.tg th{border-color:black;border-style:solid;border-width:1px;font-family:Arial, sans-serif;font-size:14px;
  font-weight:normal;overflow:hidden;padding:10px 5px;word-break:normal;}
.tg .tg-c3ow{border-color:inherit;text-align:center;vertical-align:top}
.tg .tg-0pky{border-color:inherit;text-align:left;vertical-align:top}
</style>
<table class="tg">
<thead>
  <tr>
    <th class="tg-c3ow">Name</th>
    <th class="tg-c3ow">Initial Location</th>
    <th class="tg-c3ow">Accessible By</th>
    <th class="tg-c3ow"></th>
    <th class="tg-c3ow">Migratable To</th>
    <th class="tg-c3ow"></th>
  </tr>
</thead>
<tbody>
  <tr>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">N/A</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Any Device</td>
    <td class="tg-0pky">Yes (perhaps over a bus, such as PCIe)</td>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">No</td>
  </tr>
  <tr>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">Specific Device</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">No</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">No</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Specific Device</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">N/A</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Another Device</td>
    <td class="tg-0pky">Optional</td>
    <td class="tg-0pky">Another Device</td>
    <td class="tg-0pky">No</td>
  </tr>
  <tr>
    <td class="tg-0pky">Shared</td>
    <td class="tg-0pky">Host, or Specific Device, Or Unspecified</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Yes</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Specific Device</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">Yes</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Another Device</td>
    <td class="tg-0pky">Optional</td>
    <td class="tg-0pky">Another Device</td>
    <td class="tg-0pky">Optional</td>
  </tr>
  <tr>
    <td class="tg-0pky">Shared System</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Host</td>
    <td class="tg-0pky">Yes</td>
  </tr>
  <tr>
    <td class="tg-0pky"></td>
    <td class="tg-0pky"></td>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">Yes</td>
    <td class="tg-0pky">Device</td>
    <td class="tg-0pky">Yes</td>
  </tr>
</tbody>
</table>

## Memory allocation implementations
### Device USM Allocation

User allocate device memories through calling [clDeviceMemAllocINTEL](https://github.com/intel/fpga-runtime-for-opencl/blob/f79a9c250979cb7818ecb95b5e5778ab4263e3c2/src/acl_usm.cpp#L190-L300). This function borrows majority of the functionality of buffers for reserving and allocating memories. Specifically, it calls `clCreateBufferWithPropertiesINTEL` for creating buffer object, and calls `acl_bind_buffer_to_device` to reserve memory for the buffer (The procedure for `acl_bind_buffer_to_device` is documented in [buffer.md](https://github.com/intel/fpga-runtime-for-opencl/blob/main/docs/buffer.md)).

The function then gives back a wrapper ([acl_usm_allocation_t](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/include/acl.h#L442-L449)) that contains the following information;
1. The buffer object that was used
2. The device this memory is allocated on
3. The address range of this allocation, with same [encoding](https://github.com/intel/fpga-runtime-for-opencl/blob/1264543c0361530f5883e35dc0c9d48ac0fd3653/include/acl.h#L264-L274) as buffer memory's device pointer, queried using [l_get_address_of_writable_copy](https://github.com/intel/fpga-runtime-for-opencl/blob/b08e0af97351718ce0368a9ee507242b35f4929e/src/acl_mem.cpp#L4666-L4722).
4. The alignment, which can only be powers of 2 (In bytes) and no larger than the largest device supported data type `cl_long16`, although MMD's hard limit is 2M (and this was because the max page size is 2M during mmap).

The last step of creating device usm allocation is to let context [track](https://github.com/intel/fpga-runtime-for-opencl/blob/3f7a228133f92c63be5b04e222f3fc8ff72310e6/include/acl_types.h#L1187) this memory in a list.

Note: A good analogy for usm device pointer is malloc on device.

### Device Host and Shared Allocation

[`board_spec.xml`](https://github.com/intel/fpga-runtime-for-opencl/blob/main/test/board/a10_ref/hardware/a10_ref_small/board_spec.xml) may have the following memory section specified, which contains information about the range of memory available for each `allocation_type`: `host`, `shared`, `device`

```
  <global_mem name="host" max_bandwidth="30000" interleaved_bytes="1024" config_addr="0x018" allocation_type="host, shared">
    <interface name="board" port="kernel_mem" type="slave" width="512" maxburst="16" address="0x000000000" size="0x1000000000000" latency="800" waitrequest_allowance="7"/>
  </global_mem>
  
  <!-- DDR4-2400 -->
  <!-- max-bandwidth: (interface freq) * 2 (bits per clock) * (num interfaces) * (data bytes per interface) -->
  <global_mem name="device" max_bandwidth="76800" interleaved_bytes="4096" config_addr="0x018" allocation_type="device" default="1">
    <interface name="board" port="kernel_ddr4a" type="slave" width="512" maxburst="16" address="0x1000000000000" size="0x100000000" latency="1500" waitrequest_allowance="6" bsp_avmm_write_ack="1"/>
    <interface name="board" port="kernel_ddr4b" type="slave" width="512" maxburst="16" address="0x1000100000000" size="0x100000000" latency="1500" waitrequest_allowance="6" bsp_avmm_write_ack="1"/>
    <interface name="board" port="kernel_ddr4c" type="slave" width="512" maxburst="16" address="0x1000200000000" size="0x100000000" latency="1500" waitrequest_allowance="6" bsp_avmm_write_ack="1"/>
    <interface name="board" port="kernel_ddr4d" type="slave" width="512" maxburst="16" address="0x1000300000000" size="0x100000000" latency="1500" waitrequest_allowance="6" bsp_avmm_write_ack="1"/>
  </global_mem>
```

Both [clSharedMemAllocINTEL](https://github.com/intel/fpga-runtime-for-opencl/blob/f79a9c250979cb7818ecb95b5e5778ab4263e3c2/src/acl_usm.cpp#L302-L457) and [clHostMemAllocINTEL](https://github.com/intel/fpga-runtime-for-opencl/blob/f79a9c250979cb7818ecb95b5e5778ab4263e3c2/src/acl_usm.cpp#L45-L188) lead to the same MMD calls ([aocl_mmd_host_alloc](https://gitlab.devtools.intel.com/OPAE/opencl-bsp/-/blob/master/agilex_f_dk/source/host/ccip_mmd.cpp#L930-1049)) under the hood.
1. If the allocation size is greater than 4K, then MMD uses huge page (`MAP_HUGETLB `) when mapping virtual memory through `mmap`.
2. The mapped memory is a private block of hardware memory not accessible by othe rprocesses. (mmap flags: `MAP_ANONYMOUS`, `MAP_PRIVATE`, `MAP_LOCKED`)
3. The allocated memory may be composed of multiple physical pages that is not physcally contiguous. For more details, read OPAE [shim_vtp.h](https://github.com/OPAE/intel-fpga-bbb/blob/b4093957cd1ebb0359f2f67dd541aa5356d43709/BBB_cci_mpf/sw/include/opae/mpf/shim_vtp.h#L84-L116)
4. The difference between host and shared allocation is host allocation may target all device in current context, where as the shared allocation will always target a single device.

## Launching kernel with usm pointer arguments

To bind a specific usm pointer to kernel arguments, user calls [`clSetKernelArgMemPointerINTEL`](https://github.com/intel/fpga-runtime-for-opencl/blob/fc99b92704a466f7dc4d84bd45d465d64d03dbb0/src/acl_kernel.cpp#L842-L1076). This requires the given pointer to satisfy the following requirements:
1. The pointer is [tracked](https://github.com/intel/fpga-runtime-for-opencl/blob/3f7a228133f92c63be5b04e222f3fc8ff72310e6/include/acl_types.h#L1187) in the context in a list.
2. The usm pointer alignment match the expected alignment of the kernel arg
3. The global memory that kernel expect has the same type as the given usm pointer. (eg. whether they are both has `shared` allocation type)
4. Set the kernel argment value as the given pointer, and keep track of the pointer in kernel's `ptr_arg_vector`

Note: OpenCL compiles assumes unlabeled pointers are device global mem (but this is not true for sycl compilers).

Unlike buffer where there were additional migration operation inserted right before kernel launch, there is no more additional processing done on the usm pointers after it is set as kernel arg.


For more information, see [USM spec](https://github.com/KhronosGroup/OpenCL-Docs/blob/master/extensions/cl_intel_unified_shared_memory.asciidoc)
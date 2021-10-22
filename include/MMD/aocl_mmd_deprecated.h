// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef AOCL_MMD_OTHER_H
#define AOCL_MMD_OTHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Support for USM calls on ACL devices*/

#ifndef AOCL_MMD_CALL
#if defined(_WIN32)
#define AOCL_MMD_CALL   __declspec(dllimport)
#else
#define AOCL_MMD_CALL
#endif
#endif

#ifndef WEAK
#if defined(_WIN32)
#define WEAK
#else
#define WEAK  __attribute__((weak))
#endif
#endif

/* DEPRECATED. Use aocl_mmd_program instead
 * This reprogram API is only for mmd version previous than 18.1
*/
AOCL_MMD_CALL int aocl_mmd_reprogram( int handle, void * user_data, size_t size) WEAK;

/**
 * Legacy shared memory allocator. Consider using the new API aocl_mmd_shared_alloc.
 * Allocates memory that is shared between the host and the FPGA.  The
 * host will access this memory using the pointer returned by
 * aocl_mmd_shared_mem_alloc, while the FPGA will access the shared memory
 * using device_ptr_out.  If shared memory is not supported this should return
 * NULL.
 *
 * Shared memory survives FPGA reprogramming if the CPU is not rebooted.
 *
 * Arguments:
 *   size - the size of the shared memory to allocate
 *   device_ptr_out - will receive the pointer value used by the FPGA (the device)
 *                    to access the shared memory.  Cannot be NULL.  The type is
 *                    unsigned long long to handle the case where the host has a
 *                    smaller pointer size than the device.
 *
 * Returns: The pointer value to be used by the host to access the shared
 * memory if successful, otherwise NULL.
 */
AOCL_MMD_CALL void * aocl_mmd_shared_mem_alloc( int handle, size_t size, unsigned long long *device_ptr_out ) WEAK;

/**
 * Frees memory allocated by aocl_mmd_shared_mem_alloc().  If shared memory is not supported,
 * this function should do nothing.
 *
 * Arguments:
 *   host_ptr - the host pointer that points to the shared memory, as returned by
 *              aocl_mmd_shared_mem_alloc
 *   size     - the size of the shared memory to free. Must match the size
 *              originally passed to aocl_mmd_shared_mem_alloc
 */
AOCL_MMD_CALL void aocl_mmd_shared_mem_free ( int handle, void* host_ptr, size_t size ) WEAK;

/**
 *  Make host memory allocation available on specific device
 *  @param name Device name that will now have access to the host allocation
 *  @param size The size of the host allocation
 *  @param host_ptr The pointer to host allocated memory
 */
 AOCL_MMD_CALL void aocl_mmd_shared_mem_prepare_buffer( const char *name, size_t size, void* host_ptr ) WEAK;

/**
 *  Release the buffer that has been prepared by aocl_mmd_shared_mem_prepare_buffer()
 *  Must be called for each device the memory has been prepared for, before that
 *  memory can be released by the host.
 *  @param name Device name that had access to the host allocation
 *  @param host_ptr The pointer to host allocated memory
 */
AOCL_MMD_CALL void aocl_mmd_shared_mem_release_buffer ( const char *name, void* host_ptr ) WEAK;

#ifdef __cplusplus
}
#endif

#endif //AOCL_MMD_OTHER_H

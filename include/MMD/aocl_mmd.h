// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef AOCL_MMD_H
#define AOCL_MMD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Support for memory mapped ACL devices.
 *
 * Typical API lifecycle, from the perspective of the caller.
 *
 *    1. aocl_mmd_open must be called first, to provide a handle for further
 *    operations.
 *
 *    2. The interrupt and status handlers must be set.
 *
 *    3. Read and write operations are performed.
 *
 *    4. aocl_mmd_close may be called to shut down the device. No further
 *    operations are permitted until a subsequent aocl_mmd_open call.
 *
 * aocl_mmd_get_offline_info can be called anytime including before
 * open. aocl_mmd_get_info can be called anytime between open and close.
 */

#ifndef AOCL_MMD_CALL
#if defined(_WIN32)
#define AOCL_MMD_CALL __declspec(dllimport)
#else
#define AOCL_MMD_CALL
#endif
#endif

#ifndef WEAK
#if defined(_WIN32)
#define WEAK
#else
#define WEAK __attribute__((weak))
#endif
#endif

/* The MMD API's version - the runtime expects this string when
 * AOCL_MMD_VERSION is queried. This changes only if the API has changed */
#define AOCL_MMD_VERSION_STRING "20.3"

/* Memory types that can be supported - bitfield. Other than physical memory
 * these types closely align with the OpenCL SVM types.
 *
 * AOCL_MMD_PHYSICAL_MEMORY - The vendor interface includes IP to communicate
 * directly with physical memory such as DDR, QDR, etc.
 *
 * AOCL_MMD_SVM_COARSE_GRAIN_BUFFER - The vendor interface includes support for
 * caching SVM pointer data and requires explicit function calls from the user
 * to synchronize the cache between the host processor and the FPGA. This level
 * of SVM is not currently supported by Altera except as a subset of
 * SVM_FINE_GAIN_SYSTEM support.
 *
 * AOCL_MMD_SVM_FINE_GRAIN_BUFFER - The vendor interface includes support for
 * caching SVM pointer data and requires additional information from the user
 * and/or host runtime that can be collected during pointer allocation in order
 * to synchronize the cache between the host processor and the FPGA. Once this
 * additional data is provided for an SVM pointer, the vendor interface handles
 * cache synchronization between the host processor & the FPGA automatically.
 * This level of SVM is not currently supported by Altera except as a subset
 * of SVM_FINE_GRAIN_SYSTEM support.
 *
 * AOCL_MMD_SVM_FINE_GRAIN_SYSTEM - The vendor interface includes support for
 * caching SVM pointer data and does not require any additional information to
 * synchronize the cache between the host processor and the FPGA. The vendor
 * interface handles cache synchronization between the host processor & the
 * FPGA automatically for all SVM pointers. This level of SVM support is
 * currently under development by Altera and some features may not be fully
 * supported.
 */
#define AOCL_MMD_PHYSICAL_MEMORY (1 << 0)
#define AOCL_MMD_SVM_COARSE_GRAIN_BUFFER (1 << 1)
#define AOCL_MMD_SVM_FINE_GRAIN_BUFFER (1 << 2)
#define AOCL_MMD_SVM_FINE_GRAIN_SYSTEM (1 << 3)

/* program modes - bitfield
 *
 * AOCL_MMD_PROGRAM_PRESERVE_GLOBAL_MEM - preserve contents of global memory
 * when this bit is set to 1. If programming can't occur without preserving
 * global memory contents, the program function must fail, in which case the
 * runtime may re-invoke program with this bit set to 0, allowing programming
 * to occur even if doing so destroys global memory contents.
 *
 * more modes are reserved for stacking on in the future
 */
#define AOCL_MMD_PROGRAM_PRESERVE_GLOBAL_MEM (1 << 0)
typedef int aocl_mmd_program_mode_t;


typedef void* aocl_mmd_op_t;

typedef struct {
   unsigned lo; /* 32 least significant bits of time value. */
   unsigned hi; /* 32 most significant bits of time value. */
} aocl_mmd_timestamp_t;


/* Defines the set of characteristics that can be probed about the board before
 * opening a device. The type of data returned by each is specified in
 * parentheses in the adjacent comment.
 *
 * AOCL_MMD_NUM_BOARDS and AOCL_MMD_BOARD_NAMES
 *   These two fields can be used to implement multi-device support. The MMD
 *   layer may have a list of devices it is capable of interacting with, each
 *   identified with a unique name. The length of the list should be returned
 *   in AOCL_MMD_NUM_BOARDS, and the names of these devices returned in
 *   AOCL_MMD_BOARD_NAMES. The OpenCL runtime will try to call aocl_mmd_open
 *   for each board name returned in AOCL_MMD_BOARD_NAMES.
 */
typedef enum {
   AOCL_MMD_VERSION = 0,       /* Version of MMD (char*)*/
   AOCL_MMD_NUM_BOARDS = 1,    /* Number of candidate boards (int)*/
   AOCL_MMD_BOARD_NAMES = 2,   /* Names of boards available delimiter=; (char*)*/
   AOCL_MMD_VENDOR_NAME = 3,   /* Name of vendor (char*) */
   AOCL_MMD_VENDOR_ID = 4,     /* An integer ID for the vendor (int) */
   AOCL_MMD_USES_YIELD = 5,    /* 1 if yield must be called to poll hw (int) */
   /* The following can be combined in a bit field:
    * AOCL_MMD_PHYSICAL_MEMORY, AOCL_MMD_SVM_COARSE_GRAIN_BUFFER, AOCL_MMD_SVM_FINE_GRAIN_BUFFER, AOCL_MMD_SVM_FINE_GRAIN_SYSTEM.
    * Prior to 14.1, all existing devices supported physical memory and no types of SVM memory, so this
    * is the default when this operation returns '0' for board MMDs with a version prior to 14.1
    */
   AOCL_MMD_MEM_TYPES_SUPPORTED = 6,
} aocl_mmd_offline_info_t;


/** Possible capabilities to return from AOCL_MMD_*_MEM_CAPABILITIES query */
/**
 * If not set allocation function is not supported, even if other capabilities are set.
 */
#define AOCL_MMD_MEM_CAPABILITY_SUPPORTED      (1 << 0)
/**
 *   Supports atomic access to the memory by either the host or device.
 */
#define AOCL_MMD_MEM_CAPABILITY_ATOMIC         (1 << 1)
/**
 * Supports concurrent access to the memory either by host or device if the
 * accesses are not on the same block. Block granularity is defined by
 * AOCL_MMD_*_MEM_CONCURRENT_GRANULARITY., blocks are aligned to this
 * granularity
 */
#define AOCL_MMD_MEM_CAPABILITY_CONCURRENT     (1 << 2)
/**
 * Memory can be accessed by multiple devices at the same time.
 */
#define AOCL_MMD_MEM_CAPABILITY_P2P            (1 << 3)


/* Defines the set of characteristics that can be probed about the board after
 * opening a device. This can involve communication to the device
 *
 * AOCL_MMD_NUM_KERNEL_INTERFACES - The number of kernel interfaces, usually 1
 *
 * AOCL_MMD_KERNEL_INTERFACES - the handle for each kernel interface.
 * param_value will have size AOCL_MMD_NUM_KERNEL_INTERFACES * sizeof int
 *
 * AOCL_MMD_PLL_INTERFACES - the handle for each pll associated with each
 * kernel interface. If a kernel interface is not clocked by acl_kernel_clk
 * then return -1
 *
 * */
typedef enum {
   AOCL_MMD_NUM_KERNEL_INTERFACES = 1,  /* Number of Kernel interfaces (int) */
   AOCL_MMD_KERNEL_INTERFACES = 2,      /* Kernel interface (int*) */
   AOCL_MMD_PLL_INTERFACES = 3,         /* Kernel clk handles (int*) */
   AOCL_MMD_MEMORY_INTERFACE = 4,       /* Global memory handle (int) */
   AOCL_MMD_TEMPERATURE = 5,            /* Temperature measurement (float) */
   AOCL_MMD_PCIE_INFO = 6,              /* PCIe information (char*) */
   AOCL_MMD_BOARD_NAME = 7,             /* Name of board (char*) */
   AOCL_MMD_BOARD_UNIQUE_ID = 8,        /* Unique ID of board (int) */
   AOCL_MMD_CONCURRENT_READS = 9,       /* # of parallel reads; 1 is serial*/
   AOCL_MMD_CONCURRENT_WRITES = 10,     /* # of parallel writes; 1 is serial*/
   AOCL_MMD_CONCURRENT_READS_OR_WRITES = 11, /* total # of concurrent operations read + writes*/
   AOCL_MMD_MIN_HOST_MEMORY_ALIGNMENT = 12,  /* Min alignment that the BSP supports for host allocations (size_t) */
   AOCL_MMD_HOST_MEM_CAPABILITIES = 13,      /* Capabilities of aocl_mmd_host_alloc() (unsigned int)*/
   AOCL_MMD_SHARED_MEM_CAPABILITIES = 14,    /* Capabilities of aocl_mmd_shared_alloc (unsigned int)*/
   AOCL_MMD_DEVICE_MEM_CAPABILITIES = 15,    /* Capabilities of aocl_mmd_device_alloc (unsigned int)*/
   AOCL_MMD_HOST_MEM_CONCURRENT_GRANULARITY = 16,   /*(size_t)*/
   AOCL_MMD_SHARED_MEM_CONCURRENT_GRANULARITY = 17, /*(size_t)*/
   AOCL_MMD_DEVICE_MEM_CONCURRENT_GRANULARITY = 18, /*(size_t)*/
} aocl_mmd_info_t;

typedef struct {
   unsigned long long int exception_type;
   void *user_private_info;
   size_t user_cb;
}aocl_mmd_interrupt_info;

typedef void (*aocl_mmd_interrupt_handler_fn)( int handle, void* user_data );
typedef void (*aocl_mmd_device_interrupt_handler_fn)( int handle, aocl_mmd_interrupt_info* data_in, void* user_data );
typedef void (*aocl_mmd_status_handler_fn)( int handle, void* user_data, aocl_mmd_op_t op, int status );


/* Get information about the board using the enum aocl_mmd_offline_info_t for
 * offline info (called without a handle), and the enum aocl_mmd_info_t for
 * info specific to a certain board.
 * Arguments:
 *
 *   requested_info_id - a value from the aocl_mmd_offline_info_t enum
 *
 *   param_value_size - size of the param_value field in bytes. This should
 *     match the size of the return type expected as indicated in the enum
 *     definition. For example, the AOCL_MMD_TEMPERATURE returns a float, so
 *     the param_value_size should be set to sizeof(float) and you should
 *     expect the same number of bytes returned in param_size_ret.
 *
 *   param_value - pointer to the variable that will receive the returned info
 *
 *   param_size_ret - receives the number of bytes of data actually returned
 *
 * Returns: a negative value to indicate error.
 */
AOCL_MMD_CALL int aocl_mmd_get_offline_info(
    aocl_mmd_offline_info_t requested_info_id,
    size_t param_value_size,
    void* param_value,
    size_t* param_size_ret ) WEAK;

AOCL_MMD_CALL int aocl_mmd_get_info(
    int handle,
    aocl_mmd_info_t requested_info_id,
    size_t param_value_size,
    void* param_value,
    size_t* param_size_ret ) WEAK;

/* Open and initialize the named device.
 *
 * The name is typically one specified by the AOCL_MMD_BOARD_NAMES offline
 * info.
 *
 * Arguments:
 *    name - open the board with this name (provided as a C-style string,
 *           i.e. NUL terminated ASCII.)
 *
 * Returns: the non-negative integer handle for the board, otherwise a
 * negative value to indicate error. Upon receiving the error, the OpenCL
 * runtime will proceed to open other known devices, hence the MMD mustn't
 * exit the application if an open call fails.
 */
AOCL_MMD_CALL int aocl_mmd_open(const char *name) WEAK;

/* Close an opened device, by its handle.
 * Returns: 0 on success, negative values on error.
 */
AOCL_MMD_CALL int aocl_mmd_close(int handle) WEAK;

/* Set the interrupt handler for the opened device.
 * The interrupt handler is called whenever the client needs to be notified
 * of an asynchronous event signaled by the device internals.
 * For example, the kernel has completed or is stalled.
 *
 * Important: Interrupts from the kernel must be ignored until this handler is
 * set
 *
 * Arguments:
 *   fn - the callback function to invoke when a kernel interrupt occurs
 *   user_data - the data that should be passed to fn when it is called.
 *
 * Returns: 0 if successful, negative on error
 */
AOCL_MMD_CALL int aocl_mmd_set_interrupt_handler( int handle, aocl_mmd_interrupt_handler_fn fn, void* user_data ) WEAK;

/* Set the device interrupt handler for the opened device.
 * The device interrupt handler is called whenever the client needs to be notified
 * of a device event signaled by the device internals.
 * For example, an ECC error has been reported.
 *
 * Important: Interrupts from the device must be ignored until this handler is
 * set
 *
 * Arguments:
 *   fn - the callback function to invoke when a device interrupt occurs
 *   user_data - the data that should be passed to fn when it is called.
 *
 * Returns: 0 if successful, negative on error
 */
AOCL_MMD_CALL int aocl_mmd_set_device_interrupt_handler( int handle, aocl_mmd_device_interrupt_handler_fn fn, void* user_data ) WEAK;

/* Set the operation status handler for the opened device.
 * The operation status handler is called with
 *    status 0 when the operation has completed successfully.
 *    status negative when the operation completed with errors.
 *
 * Arguments:
 *   fn - the callback function to invoke when a status update is to be
 *   performed.
 *   user_data - the data that should be passed to fn when it is called.
 *
 * Returns: 0 if successful, negative on error
 */
AOCL_MMD_CALL int aocl_mmd_set_status_handler( int handle, aocl_mmd_status_handler_fn fn, void* user_data ) WEAK;

/* If AOCL_MMD_USES_YIELD is 1, this function is called when the host is idle
 * and hence possibly waiting for events to be processed by the device.
 * If AOCL_MMD_USES_YIELD is 0, this function is never called and the MMD is
 * assumed to provide status/event updates via some other execution thread
 * such as through an interrupt handler.
 *
 * Returns: non-zero if the yield function performed useful work such as
 * processing DMA transactions, 0 if there is no useful work to be performed
 *
 * NOTE: yield may be called continuously as long as it reports that it has useful work
 */
AOCL_MMD_CALL int aocl_mmd_yield(int handle) WEAK;

/* Read, write and copy operations on a single interface.
 * If op is NULL
 *    - Then these calls must block until the operation is complete.
 *    - The status handler is not called for this operation.
 *
 * If op is non-NULL, then:
 *    - These may be non-blocking calls
 *    - The status handler must be called upon completion, with status 0
 *    for success, and a negative value for failure.
 *
 * Arguments:
 *   op - the operation object used to track this operations progress
 *
 *   len - the size in bytes to transfer
 *
 *   src - the host buffer being read from
 *
 *   dst - the host buffer being written to
 *
 *   mmd_interface - the handle to the interface being accessed. E.g. To
 *   access global memory this handle will be whatever is returned by
 *   aocl_mmd_get_info when called with AOCL_MMD_MEMORY_INTERFACE.
 *
 *   offset/src_offset/dst_offset - the byte offset within the interface that
 *   the transfer will begin at.
 *
 * The return value is 0 if the operation launch was successful, and
 * negative otherwise.
 */
AOCL_MMD_CALL int aocl_mmd_read(
      int handle,
      aocl_mmd_op_t op,
      size_t len,
      void* dst,
      int mmd_interface, size_t offset ) WEAK;
AOCL_MMD_CALL int aocl_mmd_write(
      int handle,
      aocl_mmd_op_t op,
      size_t len,
      const void* src,
      int mmd_interface, size_t offset ) WEAK;
AOCL_MMD_CALL int aocl_mmd_copy(
      int handle,
      aocl_mmd_op_t op,
      size_t len,
      int mmd_interface, size_t src_offset, size_t dst_offset ) WEAK;

/* Host Channel create operation
 * Opens channel between host and kernel.
 *
 * Arguments:
 *   channel_name - name of channel to initialize. Same name as used in board_spec.xml
 *
 *   queue_depth - the size in bytes of pinned memory queue in system memory
 *
 *   direction - the direction of the channel
 *
 * The return value is negative if initialization was unsuccessful, and
 * positive otherwise. Positive return value is handle to the channel to be used for
 * subsequent calls for the channel.
 */
AOCL_MMD_CALL int aocl_mmd_hostchannel_create(
      int handle,
      char *channel_name,
      size_t queue_depth,
      int direction) WEAK;

/* Host Channel destroy operation
 * Closes channel between host and kernel.
 *
 * Arguments:
 *   channel - the handle to the channel to close, that was obtained with
 *             create channel
 *
 * The return value is 0 if the destroy was successful, and negative
 * otherwise.
 */
AOCL_MMD_CALL int aocl_mmd_hostchannel_destroy(
      int handle,
      int channel) WEAK;

/* Host Channel get buffer operation
 * Provide host with pointer to buffer they can access to write or
 * read from kernel, along with space or data available in the buffer
 * in bytes.
 *
 * Arguments:
 *   channel - the handle to the channel to get the buffer for
 *
 *   buffer_size - the address that this call will write the amount of
 *                 space or data that's available in the buffer,
 *                 depending on direction of the channel, in bytes
 *
 *   status - the address that this call will write to for result of this
 *            call. Value will be 0 for success, and negative otherwise
 *
 * The return value is the pointer to the buffer that host can write
 * to or read from. NULL if the status is negative.
 */
AOCL_MMD_CALL void *aocl_mmd_hostchannel_get_buffer(
      int handle,
      int channel,
      size_t *buffer_size,
      int *status) WEAK;

/* Host Channel acknowledge buffer operation
 * Acknowledge to the channel that the user has written or read data from
 * it. This will make the data or additional buffer space available to
 * write to or read from kernel.
 *
 * Arguments:
 *   channel - the handle to the channel that user is acknowledging
 *
 *   send_size - the size in bytes that the user is acknowledging
 *
 *   status - the address that this call will write to for result of this
 *            call. Value will be 0 for success, and negative otherwise
 *
 * The return value is equal to send_size if send_size was less than or
 * equal to the buffer_size from get buffer call. If send_size was
 * greater, then return value is the amount that was actually sent.
 */
AOCL_MMD_CALL size_t aocl_mmd_hostchannel_ack_buffer(
      int handle,
      int channel,
      size_t send_size,
      int *status) WEAK;

/* Program the device
 *
 * The host will guarantee that no operations are currently executing on the
 * device. That means the kernels will be idle and no read/write/copy
 * commands are active. Interrupts should be disabled and the FPGA should
 * be reprogrammed with the data from user_data which has size size. The host
 * will then call aocl_mmd_set_status_handler and aocl_mmd_set_interrupt_handler
 * again. At this point interrupts can be enabled.
 *
 * The new handle to the board after reprogram does not have to be the same as
 * the one before.
 *
 * Arguments:
 *   user_data - The binary contents of the fpga.bin file created during
 *   Quartus II compilation.
 *   size - the size in bytes of user_data
 *   program_mode - bit field for programming attributes. See
 *   aocl_mmd_program_mode_t definition
 *
 * Returns: the new non-negative integer handle for the board, otherwise a
 * negative value to indicate error.
 */
AOCL_MMD_CALL int aocl_mmd_program( int handle, void * user_data, size_t size, aocl_mmd_program_mode_t program_mode) WEAK;

/** Error values*/
#define AOCL_MMD_ERROR_SUCCESS                 0
#define AOCL_MMD_ERROR_INVALID_HANDLE         -1
#define AOCL_MMD_ERROR_OUT_OF_MEMORY          -2
#define AOCL_MMD_ERROR_UNSUPPORTED_ALIGNMENT  -3
#define AOCL_MMD_ERROR_UNSUPPORTED_PROPERTY   -4
#define AOCL_MMD_ERROR_INVALID_POINTER        -5
#define AOCL_MMD_ERROR_INVALID_MIGRATION_SIZE -6

/** Memory properties*/
typedef enum {
  /**
   *  Specifies the name of a global memory that can be found in the
   *  board_spec.xml file for the BSP. Allocations will be allocated to this
   *  global memory interface.
   */
  AOCL_MMD_MEM_PROPERTIES_GLOBAL_MEMORY=1,
  /**
   *  Specifies the index of a bank inside the global memory interface that can be found in
   *  the board_spec.xml file for the BSP. Allocations will be allocated to this
   *  memory bank. It is invalid to specify this property without also specifying
   *  AOCL_MMD_GLOBAL_MEMORY_INTERFACE.
   */
  AOCL_MMD_MEM_PROPERTIES_MEMORY_BANK
} aocl_mmd_mem_properties_t;

/**
 *  Host allocations provide memory that is allocated on the host. Host
 *  allocations are accessible by the host and one or more devices.
 *  The same pointer to a host allocation may be used on the host and all
 *  supported devices; they have address equivalence. This memory must be
 *  deallocated with aocl_mmd_free();
 *
 *  Once the device has signaled completion through
 *  aocl_mmd_interrupt_handler_fn() the host can assume it has access to the
 *  latest contents of the memory, allocated by this call.
 *
 *  @param handles Handles for devices that will need access to this memory
 *  @param num_devices Number of devices in the handles
 *  @param size The size of the memory region
 *  @param alignment The alignment in bytes of the allocation
 *  @param properties Specifies additional information about the allocated
 *    memory, described by a property type name and its corresponding value.
 *    Each property type name is immediately followed by the corresponding
 *    desired value. The list is terminated with 0. Supported values are
 *    described above. Example: [<property1>, <value1>, <property2>, <value2>, 0]
 *  @param error The error code defined by AOCL_MMD_ERROR*
 *  @return valid pointer, on error NULL
 */
AOCL_MMD_CALL void* aocl_mmd_host_alloc (int* handles, size_t num_devices, size_t size, size_t alignment, aocl_mmd_mem_properties_t *properties, int* error) WEAK;

/**
 * Frees memory that has been allocated by MMD
 *
 * @param mem The pointer to the memory region. Must be a pointer that is
 *   allocated by the MMD.
 * @return AOCL_MMD_ERROR_SUCCESS if success, else error code
 */
AOCL_MMD_CALL int aocl_mmd_free (void* mem) WEAK;

/**
 *  Allocate memory that is owned by the device. This pointer can only be
 *  accessed by the kernel; can't be accessed by the host. The host is able to
 *  manipulate the pointer (e.g. increment it) just not access the underlying
 *  data. This memory must be deallocated by aocl_mmd_free();
 *
 *  @param  handle Device that will have access to this memory
 *  @param  size The size of the memory region
 *  @param  alignment The alignment in bytes of the memory region
 *  @param  properties Specifies additional information about the allocated
 *    memory, described by a property type name and its corresponding value.
 *    Each property type name is immediately followed by the corresponding
 *    desired value. The list is terminated with 0. Supported values are
 *    described above. Example: [<property1>, <value1>, <property2>, <value2>, 0]
 *  @param error The error code defined by AOCL_MMD_ERROR*
 *  @return Pointer that can be passed into the kernel. NULL on failure.
 */
AOCL_MMD_CALL void * aocl_mmd_device_alloc( int handle, size_t size, size_t alignment, aocl_mmd_mem_properties_t *properties, int* error) WEAK;

/**
 *  Shared allocations may migrate between the host and one or more associated
 *  device. The same pointer to a shared allocation may be used on the host and
 *  the supported device; they have address equivalence.
 *
 *  If the device does not support concurrent access to memory allocated by
 *  aocl_mmd_shared_alloc() then a call must be made to
 *  aocl_mmd_shared_mem_migrate() to indicate that the shared allocation should
 *  be migrated to the device before the device accesses this memory.  For
 *  example, a call to aocl_mmd_shared_mem_migrate() should be made before a
 *  kernel accessing this memory is launched).  Conversely,
 *  aocl_mmd_shared_mem_migrate() should be called again to indicate that the
 *  shared allocation should be migrated to the host before the host accesses
 *  this memory again.  If the device supports concurrent access to memory
 *  allocated with aocl_mmd_shared_alloc(), then the call to
 *  aocl_mmd_shared_mem_migrate() is not necessary, but may still be made.  In
 *  the case of concurrent access, it is the responsibility of the MMD to ensure
 *  both the device and host can access aocl_mmd_shared_alloc() allocations at
 *  all times.
 *
 *  Memory allocated by aocl_mmd_shared_alloc() must be deallocated with
 *  aocl_mmd_free().
 *
 *  @param  handle Device that will have access to this memory
 *  @param  size The size of the memory region
 *  @param alignment The alignment in bytes of the memory region
 *  @param  properties Specifies additional information about the allocated
 *    memory, described by a property type name and its corresponding value.
 *    Each property type name is immediately followed by the corresponding
 *    desired value. The list is terminated with 0. Supported properties are
 *    listed above and have the prefix AOCL_MMD_MEM_PROPERTIES_.
 *    Example: [<property1>, <value1>, <property2>, <value2>, 0]
 *  @param error The error code defined by AOCL_MMD_ERROR*
 *  @return valid pointer, on error NULL
 */
AOCL_MMD_CALL void * aocl_mmd_shared_alloc( int handle, size_t size, size_t alignment, aocl_mmd_mem_properties_t* properties, int* error) WEAK;

typedef enum {
  AOCL_MMD_MIGRATE_TO_HOST = 0,
  AOCL_MMD_MIGRATE_TO_DEVICE = 1
} aocl_mmd_migrate_t;

/**
 *  A call to aocl_mmd_shared_migrate() must be made for non-concurrent shared
 *  allocations any time the accessor of the allocation changes.  For example,
 *  aocl_mmd_shared_migrate() should be called indicating that the allocation
 *  should be migrated to the device before a kernel accessing the allocation
 *  is launched on the device.  Similarly, aocl_mmd_shared_migrate() should be
 *  called indicating that the allocation is migrated to the host before the
 *  host accesses the memory after kernel completion.
 *
 *  For concurrent allocations this call may be used as a performance hint, but
 *  is not strictly required for functionality.
 *
 *  @param  handle Device that will have access to this memory
 *  @param shared_ptr Pointer allocated by aocl_mmd_shared_alloc()
 *  @param size In bytes, the size of the migration. Must be of multiple of a
 *   page boundary that the BSP supports.
 *  @param destination The destination of migration
 *  @return The error code defined by AOCL_MMD_ERROR*
 */
AOCL_MMD_CALL int aocl_mmd_shared_migrate(int handle, void* shared_ptr, size_t size, aocl_mmd_migrate_t destination) WEAK;

#ifdef __cplusplus
}
#endif

#endif

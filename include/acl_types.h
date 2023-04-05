// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_TYPES_H
#define ACL_TYPES_H

// System headers.
#include <cassert>
#include <deque>
#include <list>
#include <set>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include "acl.h"
#include "acl_device_binary.h"
#include "acl_hal.h"
#include "acl_hal_mmd.h"
#include "acl_icd_dispatch.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define FUNCTION_ATTRIBUTES

// --- Object allocation functions ---------------------------------------------

typedef cl_ulong acl_object_magic_t;

// Change to 1 to see debug messages when cl objects are allocated and
// deallocated
#define ACL_DEBUG_ALLOC 0

#ifndef ACL_DEBUG_INVALID_CL_OBJECTS
#ifdef ACL_DEBUG
#define ACL_DEBUG_INVALID_CL_OBJECTS 1
#else
#define ACL_DEBUG_INVALID_CL_OBJECTS 0
#endif
#endif

#define ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_type)                         \
  int acl_get_num_alloc_##cl_type();                                           \
  cl_type acl_alloc_##cl_type();                                               \
  void acl_free_##cl_type(cl_type cl_object);

#define ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_type)                          \
  static int l_num_alloc_##cl_type = 0;                                        \
  int acl_get_num_alloc_##cl_type() { return l_num_alloc_##cl_type; }          \
                                                                               \
  cl_type acl_alloc_##cl_type() {                                              \
    cl_type cl_object = acl_new<_##cl_type>();                                 \
    if (cl_object) {                                                           \
      cl_object->magic = get_magic_val<cl_type>();                             \
      l_num_alloc_##cl_type++;                                                 \
    }                                                                          \
    if (ACL_DEBUG_ALLOC) {                                                     \
      printf("acl_alloc_" #cl_type "() returned %p\n", cl_object);             \
    }                                                                          \
    return cl_object;                                                          \
  }                                                                            \
                                                                               \
  void acl_free_##cl_type(cl_type cl_object) {                                 \
    if (ACL_DEBUG_ALLOC) {                                                     \
      printf("acl_free_" #cl_type "(%p) called\n", cl_object);                 \
    }                                                                          \
    if (cl_object) {                                                           \
      cl_object->magic = 0;                                                    \
      acl_delete(cl_object);                                                   \
      l_num_alloc_##cl_type--;                                                 \
    }                                                                          \
  }

#define ACL_OPEN (-1)

/* The initial number of command queue allocated in each context.
 * There is no hard limit on the total number of command queue in the whole
 * system
 */
#define ACL_INIT_COMMAND_QUEUE_ALLOC (1024)

/* Size of the immediate device operation queue.
 * This is used to size the data structure.
 * We might impose a smaller limit at runtime for test purposes.
 * This should be big enough (at least number of contexts *
 * ACL_MAX_COMMAND_QUEUE) so we could accommodate a kernel in flight for each
 * possible command queue. We should also have room for memory transfers ready
 * to run.
 */
#define ACL_MAX_DEVICE_OPS (2048)

// Min allowable samplers in OpenCL 2.0 is 16
#define ACL_MAX_SAMPLER (32)

#define ACL_PRINTF_BUFFER_TOTAL_SIZE (64 * 1024)

// Always terminated with a 0 entry, so only odd is useful.
#define ACL_MAX_NUM_CONTEXT_PROPERTY_ENTRIES (31)

#define ACL_NUM_PROFILE_TIMESTAMPS (4)

// This is the minimum for the embedded profile.
#define ACL_MAX_KERNEL_ARG_VALUE_BYTES (512)

// Error codes unique to ACL
#define ACL_ERR_CMD_ABORT (-256)
#define ACL_ERR_INVALID_KERNEL_UPDATE (-257)

// Max path length, in bytes.
#define ACL_MAX_PATH (250)

////////////////////
// Internal command enums.
// These must be different from all other cl_command_type values.
// @{

// This is used for the clEnqueueWaitForEvents API call.
// It is the type of the returned event/command.
#define CL_COMMAND_WAIT_FOR_EVENTS_INTELFPGA 0x11ef

// Use this command to program the FPGA, without an associated kernel execution.
// ACL internal use only.
#define CL_COMMAND_PROGRAM_DEVICE_INTELFPGA 0x1210

// @}
////////////////////

/* Compiler modes.
 * This is a context property that is controlled in the following way:
 *
 *    If explicitly set as a context property during clCreateContext*, then
 *    use that value.
 *
 *    If environment variable CL_CONTEXT_COMPILER_MODE_INTELFPGA is set to a
 *    valid value, then use that value.
 *
 *    Otherwise use a default of 0.
 */

/* Context property to define the compiler mode.  */

#ifndef CL_CONTEXT_COMPILER_MODE_INTELFPGA
#error                                                                         \
    "cl_ext_intelfpga.h should have defined CL_CONTEXT_COMPILER_MODE_INTELFPGA"
#endif

/* Possible values for compiler mode:
 *
 *  offline:
 *       Support loading a program from a valid .aocx binary.
 *       Don't support or pretend to support compiling from source.
 *       No magic at all regarding a program library.
 *       This is the standard behaviour, assuming no online compiler.
 *       This should be the default.
 *
 *  offline_capture:
 *       Building the program from source captures the source and compile
 *       options into a program database as a tree on disk.
 *       Use hashing to differentiate.
 *       Create kernel, set kernel arg, enqueue task/ndrange always
 *       succeed.  We need this to fake out early exit in conformance
 *       tests.
 *
 *  offline_oracle:
 *       Assume the program database from an offline_capture run have all been
 *       built offline.
 *       When building the program from source, look it up in the program
 *       database. (Build program is an oracle query.)
 *       Create kernel and set kernel arg behave normally.
 *       Enqueue task/ndrange schedule loading, as necessary, of the SOF
 *       before the kernel is run.
 *
 *  embedded:
 *       Read kernel definitions from autodiscovery on the device.
 *       Never reprogram the device during host program operation.
 *       This is the default. (For now!)
 *
 *  simulation:
 *       Andrew's ModelSim based flow.
 */

/* 0..3 are in CL/cl_ext_intelfpga.h now */
// Mode "CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA" is for SYCL.
// When compiling SYCL program, we will transform one .spv file containing
// multiple kernels
//  into multiple aocx files, each contains one kernel.
// Mode "CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA" will take care of this
// "multiple aocx"
//  scenario during runtime.
#define CL_CONTEXT_COMPILER_MODE_ONLINE_INTELFPGA 4
#define CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA 5
#define CL_CONTEXT_COMPILER_MODE_NUM_MODES_INTELFPGA 6

typedef enum {
  ACL_COMPILER_MODE_OFFLINE = CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA,
  ACL_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY =
      CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA,
  ACL_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY =
      CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY_INTELFPGA,
  ACL_COMPILER_MODE_PRELOADED_BINARY_ONLY =
      CL_CONTEXT_COMPILER_MODE_PRELOADED_BINARY_ONLY_INTELFPGA,
  ACL_COMPILER_MODE_ONLINE = CL_CONTEXT_COMPILER_MODE_ONLINE_INTELFPGA,
  ACL_COMPILER_MODE_SPLIT_KERNEL =
      CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA,
  ACL_COMPILER_MODE_NUM_MODES = CL_CONTEXT_COMPILER_MODE_NUM_MODES_INTELFPGA
} acl_compiler_mode_t;

/* When a feature is still in development, it might need enum values
 * that are distinct from all published enums in the OpenCL token registry.
 * There is a reserved range for values for such "experimental" features.
 *
 * See the following file for how tokens and associated values are
 * registered:
 * https://cvs.khronos.org/svn/repos/registry/trunk/cl/token_registry.txt
 * It also includes all current registrations.
 *
 * This is the base value for the experimental range.
 */
#define ACL_CL_REGISTRY_EXPERIMENTAL_ENUM_BASE 0x10000
#define ACL_EXPERIMENTAL_ENUM(N) (ACL_CL_REGISTRY_EXPERIMENTAL_ENUM_BASE + N)

/* Property to define compile command.
 * The default depends on compiler mode.
 * For example, in create-exe-library mode it's "aoc -rtl"
 */
#define CL_CONTEXT_COMPILE_COMMAND_INTELFPGA ACL_EXPERIMENTAL_ENUM(1)

/* An opaque type for critical section + condition variable.
 * Use indirection here so we don't force every module in the world to pull
 * in windows.h.
 */
typedef struct acl_condvar_s *acl_condvar_t;

typedef enum {
  ACL_INVALID_EXECUTION_TRANSITION = -1,
  ACL_INVALID_EXECUTION_STATUS = -2,
} acl_execution_error_code_t;

// A type used to manage an aligned memory block allocation.
// The memory is allocated if and only if "raw" is non-NULL.
typedef struct {
  void *aligned_ptr = nullptr; // This pointer is aligned as requested.
  void *raw = nullptr; // The malloc base pointer returned by the system.
  size_t alignment = 0;
  size_t size = 0;                    // Actual allocation size.
  unsigned long long device_addr = 0; // Address to pass to the device.
                                      // May be different for shared memory
                                      // (phys addr for device vs virtual
                                      // for the host).
} acl_aligned_ptr_t;

// A callback function type used by various APIs
typedef void(CL_CALLBACK *acl_notify_fn_t)(const char *errinfo,
                                           const void *private_info, size_t cb,
                                           void *user_data);

// An exception callback function type used by cl_device_id APIs
typedef void(CL_CALLBACK *acl_exception_notify_fn_t)(
    CL_EXCEPTION_TYPE_INTEL exception_type, const void *private_info, size_t cb,
    void *user_data);

// A callback function type for clBuildProgram
typedef void(CL_CALLBACK *acl_program_build_notify_fn_t)(cl_program program,
                                                         void *user_data);

// A callback function type for clEnqueueSVMFree
typedef void(CL_CALLBACK *acl_svm_free_notify_fn_t)(cl_command_queue queue,
                                                    cl_uint num_svm_pointers,
                                                    void *svm_pointers[],
                                                    void *user_data);

// A callback function type for clSetEventCallback
typedef void(CL_CALLBACK *acl_event_notify_fn_t)(
    cl_event event, cl_int event_command_exec_status, void *user_data);

typedef void(CL_CALLBACK *acl_mem_destructor_notify_fn_t)(cl_mem memobj,
                                                          void *user_data);

enum host_op_type { MAP, PACKET };

typedef struct host_op_struct {
  enum host_op_type m_op;
  void *m_mmd_buffer;
  void *m_host_buffer;
  size_t m_op_size;
  size_t m_size_sent;
} host_op_t;

typedef struct host_pipe_struct {
  // The handle to the device needed for mmd call
  unsigned int m_physical_device_id;

  // The channel handle returned by mmd create hostch call
  int m_channel_handle;

  // Operations on the host pipe are queued here. map/packet ops
  std::deque<host_op_t> m_host_op_queue;
  // host_op_t * m_host_op_queue;

  // The total size of the operations we're queuing on the pinned mmd buffer
  size_t size_buffered;

  // The kernel that the host pipe is binded to
  cl_kernel m_binded_kernel;

  // Is host pipe binded to kernel device, only done at kernel enqueue time
  bool binded;

  // Channel ID name that the host pipe will use to do the binding
  std::string host_pipe_channel_id;

  // Pipe specific lock. Obtained every time we do an operation on the pipe
  acl_mutex_t m_lock;

  // The following are the new entries introduced by the program scoped
  // hostpipes

  // Whether this program hostpipe is implemented in the CSR
  bool implement_in_csr;

  // The CSR address of this hostpipe. Compiler passes a csr_address = '-' for
  // non-CSR program hostpipe
  std::string csr_address;

} host_pipe_t;

// The device-specific information about a program.
//
// This object is owned by the cl_program that creates it during
// its build process.
class acl_device_program_info_t {
public:
  acl_device_program_info_t();

  ~acl_device_program_info_t();

  cl_program program = nullptr;  // Back pointer.
  cl_device_id device = nullptr; // For what device?  Shortcut only.

  // Build options supplied by the user.
  std::string build_options;

  // Build log emitted by the compiler.
  std::string build_log;

  // The build status for this device.
  cl_build_status build_status = CL_BUILD_NONE;

  // This is the binary that the user passes into clCreateProgramWithBinary
  // or the resulting binary after the user calls clBuildProgram when the
  // enclosing cl_program was created with clCreateProgramWithSource.
  // When the enclosing cl_context is operating with split_kernel == 0
  // this is the binary that contains all the kernels in the cl_program.
  // When split_kernel == 1 this is not used.
  acl_device_binary_t device_binary;

  // A hash of the OpenCL program's source and compile options.
  // This is not the hash contained inside of an aocx file.
  // This hash is only non-empty when the enclosing cl_program was created
  // with clCreateProgramWithSource.
  std::string hash;

  // The command currently using a given accelerator block.
  // This is NULL when the hardware accelerator (kernel) is not being used.
  std::unordered_map<const acl_accel_def_t *, cl_event> current_event;

  // Add a new acl_device_binary_t for the given hashed kernel name and return
  // it. This method should only be used when the enclosing cl_context is
  // operating with split_kernel == 1. The device binary file is not loaded into
  // memory at this point.
  acl_device_binary_t &add_split_binary(const std::string &hashed_name);

  // Return the acl_device_binary_t associated with the given kernel name or
  // nullptr if no acl_device_binary_t in this acl_device_program_info_t
  // contains the specified kernel. If the enclosing cl_context is operating
  // with split_kernel == 1 and preloading device binaries at cl_program
  // creation time is disabled then load the device binary for the given kernel
  // name.
  const acl_device_binary_t *
  get_or_create_device_binary(const std::string &name);

  // Return the acl_accel_def_t for the kernel with the given name or nullptr
  // if none of the contained ACLDeviceBinaries contain a kernel with that name.
  const acl_accel_def_t *get_kernel_accel_def(const std::string &name) const;

  // Return the total number of kernels in this acl_device_program_info_t.
  size_t get_num_kernels() const;

  // Return all the names of the kernels in this device program.
  std::set<std::string> get_all_kernel_names() const;

  // Map logical hostpipe name to the hostpipe struct
  std::unordered_map<std::string, host_pipe_t> program_hostpipe_map;

private:
  // This map is only used when split_kernel == 1 in the enclosing
  // cl_context. This maps a hashed kernel name to the acl_device_binary_t that
  // contains the kernel.
  std::unordered_map<std::string, acl_device_binary_t> m_split_device_binaries;

  // Return the acl_device_binary_t associated with the given kernel name or
  // nullptr if no acl_device_binary_t in this acl_device_program_info_t
  // contains the specified kernel.
  const acl_device_binary_t *get_device_binary(const std::string &name) const;
};

// Official OpenCL data types.

// Must align attributes of the invocation image to 4 bytes because the struct
// is written in 4 byte chunks to the CRA and we don't want padding where we
// don't expect it.
#pragma pack(push, 4)
// These are the bytes written to global memory for a kernel invocation.
typedef struct {
  // The activation_id is the index into the device op queue.
  // The value at acl_platform.device_op_queue[activation_id] will be
  // updated asynchronously by the HAL, so its address must remain stable.
  cl_int activation_id;

  cl_uint accel_id; // The hardware kernel block index.
  // *** Values starting from here are written to kernel configuration
  // registers.
  cl_uint work_dim; // Number of valid dimensions in the work item arrays.
  cl_uint work_group_size; // Number of work items in work group.
                           // Computing in host saves multipliers in HW.
  // Values for work item functions.
  // Always: global_works_size[i] = num_groups[i] * local_work_size[i]
  // Always: Unused dimensions are set to 1.
  cl_uint global_work_size[3]; // Defines global work space.
                               // Force 32-bits for each index
  cl_uint num_groups[3];       // Number of work groups, in each dimension.
                         // We pass in num_groups explicitly to simplify the
                         // hardware.
  cl_uint local_work_size[3]; // Defines the shape of workgroup.
                              // Force 32-bits for each index.  0,0,0 if not
                              // specified by the user.

  cl_uint padding; // Unused 32-bit padding to match the CRA layout.

  // The OpenCL clEnqueueNDRangeKernel specification states that
  // global_work_offset + global_work_size may be any value that can be stored
  // in a variable of size_t on the device. Our devices are 64-bit (the FPGA
  // portion of SoC devices are 64-bit even though the ARM host is 32-bit). We
  // limit global_work_size to 32-bit values but global_work_offset can be a
  // 64-bit value.
  cl_ulong global_work_offset[3];

  // These are the argument values, packed from left to right.
  // We have to cache them because clSetKernelArg can update
  // the kernel object after enqueueing but before invocation.
  // The __global and __constant pointer values are adjusted to the
  // device sized pointers.
  // The __local arguments are the sizes of the buffers to be
  // allocated, rather than a pointer.
  char *arg_value;
  size_t arg_value_size;

} acl_dev_kernel_invocation_image_t;

// Invocation image structure that matches the 18.1 CRA layout.
// This struct only contains the fields that will be written to the CRA.
typedef struct {
  cl_uint work_dim;
  cl_uint work_group_size;
  cl_uint global_work_size[3];
  cl_uint num_groups[3];
  cl_uint local_work_size[3];
  cl_uint global_work_offset[3]; // Unused in kernels compiled with the 18.1
                                 // CRA. All zero elements.
  char arg_value[ACL_MAX_KERNEL_ARG_VALUE_BYTES];
} acl_dev_kernel_invocation_image_181_t;

#pragma pack(pop)

// A wrapper around an invocation image buffer.
typedef struct acl_kernel_invocation_wrapper_t {
  unsigned id;
  cl_uint refcount;
  // Backpointer to the associated event.
  // Easier to shove this in here than change HAL's launch_kernel interface.
  // This is caused by the fact that the Option3 CSR interface doesn't
  // have a "RUNNING" bit.  So the HAL has to set the "running" execution status
  // right when it launches the kernel.
  cl_event event;

  // Used by OPTION3, during transition.
  acl_dev_kernel_invocation_image_t *image;
  acl_dev_kernel_invocation_image_t image_storage; // What image points to.

  std::vector<aocl_mmd_streaming_kernel_arg_info_t> streaming_args;
} acl_kernel_invocation_wrapper_t;

typedef struct {
  cl_mem src_mem;
  unsigned int destination_physical_device_id;
  unsigned int destination_mem_id;
} acl_mem_migrate_wrapper_t;

typedef struct {
  cl_uint num_mem_objects;
  cl_uint num_alloc;
  acl_mem_migrate_wrapper_t *src_mem_list;
} acl_mem_migrate_t;

// Information required to execute a command.
typedef struct {
  cl_command_type type;

  // Only used by map/unmap buffer to choose between
  // the mem_xfer and trivial_mem_mapping cases.
  int trivial;

  union {
    struct {
      // Used when type is one of:
      //     CL_COMMAND_READ_BUFFER,
      //     CL_COMMAND_WRITE_BUFFER,
      //     or CL_COMMAND_COPY_BUFFER.
      // Or when it's a non-trivial map or unmap operation, i.e. that
      // might involve copying data to or from the device.
      // That is, the type is either
      //     CL_COMMAND_MAP_BUFFER and src_mem is the buffer being mapped
      //       into host memory.
      //  or CL_COMMAND_UNMAP_MEM_OBJECT and dst_mem is the buffer being
      //       unmapped.
      //
      // Images can have up to 3 dimensions of offset & byte size
      // (e.g. width, height & depth)
      cl_mem src_mem;
      size_t src_offset[3];
      size_t src_row_pitch;
      size_t src_slice_pitch;

      cl_mem dst_mem;
      size_t dst_offset[3];
      size_t dst_row_pitch;
      size_t dst_slice_pitch;
      size_t cb[3];                // num of bytes to transfer.  "count bytes"
      cl_mem_object_type mem_type; // Memory type (CL_MEM_OBJECT_BUFFER,
                                   // CL_MEM_OBJECT_IMAGE2D, etc.)
      cl_map_flags map_flags; // Valid only if type is CL_COMMAND_MAP_BUFFER
      // Whether this operation is an automatic-map/unmap operation. This is
      // done when there are sub-buffers, and we need to copy live data back to
      // the host before doing an operation with an overlapping sub-buffer or
      // the parent buffer.
      int is_auto_map;
    } mem_xfer;

    struct {
      const void *src_ptr;
      void *dst_ptr;
      size_t src_size;
      size_t dst_size;
    } svm_xfer;

    struct {
      const void *src_ptr;
      void *dst_ptr;
      size_t size;
      bool src_on_host;
      bool dst_on_host;
    } usm_xfer;

    acl_mem_migrate_t memory_migration;

    struct {
      void(CL_CALLBACK *pfn_free_func)(cl_command_queue /* queue */,
                                       cl_uint /* num_svm_pointers */,
                                       void *[] /* svm_pointers[] */,
                                       void * /* user_data */);
      cl_uint num_svm_pointers;
      void *user_data;
      void **svm_pointers;
    } svm_free;

    struct {
      // Used for trivial buffer mappings only, i.e. when we would
      // never transfer memory.
      // When type is one of CL_COMMAND_MAP_BUFFER, CL_UNMAP_MEM_OBJECT
      cl_mem mem;
    } trivial_mem_mapping;

    struct kernel_invocation_t {
      cl_kernel kernel;
      cl_device_id device;
      acl_kernel_invocation_wrapper_t *invocation_wrapper;
      acl_kernel_invocation_wrapper_t *serialization_wrapper;

      const acl_device_binary_t *dev_bin;
      cl_uint accel_id;

      acl_mem_migrate_t memory_migration;

    } ndrange_kernel;

    struct {
      // Used for program scoped hostpipe
      size_t size;
      void *ptr;
      const void *write_ptr;
      bool blocking;
      const char *logical_name; // Use char* instead string here due to a
                                // compilation error from acl_command_info_t
                                // constructor malloc related
    } host_pipe_info;

    // Reprogram the device, without an associated kernel enqueue.
    // This is used to hide the latency of device programming on host
    // program startup.
    // Used with CL_COMMAND_PROGRAM_DEVICE_INTELFPGA
    const acl_device_binary_t *eager_program;

  } info;
} acl_command_info_t;

// A struct to keep the information about each registered callback for events.
// This will be used as a linked list, hence, the next pointer.
typedef struct acl_event_user_callback {

  struct acl_event_user_callback
      *next; // pointer to the next acl_event_user_callback object. The last
             // object points to null.
  void *notify_user_data; // the callback function, provided by the user.
  acl_event_notify_fn_t event_notify_fn;
  cl_int registered_exec_status; // the event_command_exec_statuses that the
                                 // callback function event_notify_fn is
                                 // registered for.
} acl_event_user_callback;

// Events are the fundamental dependency and synchronization primitive in
// OpenCL.
//
// An event
//    Tracks processing state of a command.
//    Can depend on earlier events, i.e. don't launch this command until
//    the earlier ones have completed.
//
// Note:  The execution_status and timestamp[] fields are updated
// asynchronously.  They are updated (in another thread) in response to
// receiving a status message from an accelerator device about a submitted
// command, e.g. the execution of a kernel.
//
// Because of that, the command queue update should treat the _cl_event as
// volatile.
typedef struct _cl_event {
  cl_icd_dispatch *dispatch;
  acl_object_magic_t magic;
  unsigned int id;
  bool not_popped; // If the event is still in the command queue this is true
  cl_uint refcount;

  struct _cl_context *context;
  struct _cl_command_queue *command_queue;
  acl_command_info_t cmd;

  // pointers a kernel event may access
  std::unordered_set<void *> ptr_hashtable;

  // The last of the device operations corresponding to this command, if any.
  // NULL otherwise.
  // This is used to ensure that we commit operations to the device op
  // queue at most once.
  struct acl_device_op_t *last_device_op;

  // The currently running device operation.
  // This is used by HAL interrupt routines to route status updates down
  // to the device operation.  Even though many device ops may be enqueued
  // for a single cl_event, they are always processed serially.
  struct acl_device_op_t *current_device_op;

  // Progress info is recorded in execution_status and timestamp.
  // These are updated asynchronously.
  cl_int execution_status; // CL_QUEUED... down to CL_COMPLETE, or negative for
                           // error.
  // timestamp[ <status> ] is the device time when execution_status
  // changed to <status>
  cl_ulong timestamp[ACL_NUM_PROFILE_TIMESTAMPS];

  // If non-null, then call this exactly once when the event is finished
  // (from the scheduler).
  void (*completion_callback)(cl_event event);

  std::list<cl_event>
      depend_on_me; // events that depend on this event finishing to execute
  std::set<cl_event> depend_on; // events that this event depends on to finish

  acl_event_user_callback *callback_list; // list of callbacks, to be registered
                                          // by user. Initially null.

  int is_on_device_op_queue; // bool whether this event is submitted to DevQ
  bool support_profiling;    // bool whether this event can be profiled
} _cl_event;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_event);

typedef struct _cl_command_queue {
  cl_icd_dispatch *dispatch;
  acl_object_magic_t magic;
  unsigned int id; // id of the pointer in context->command_queue
  cl_uint refcount;
  cl_command_queue_properties properties;
  cl_device_id device;
  cl_context context;

  // All normal user-visible command queues do.
  // The user_event_queue does not, and may be processed out of order.
  int submits_commands;

  int num_commands;           // Number of commands in the queue.
  int num_commands_submitted; // Number of events submitted to device_op_queue

  // Used only for in-order command queues. Owner of all events managed by this
  // queue
  std::deque<cl_event> inorder_commands;

  // Used only for Out-of-order command queues
  std::set<cl_event> commands; // owner of all events managed by this queue
  std::list<cl_event>
      completed_commands; // freshly completed events, ready to be deleted
  std::list<cl_event>
      new_commands; // freshly added events, ready to be submitted
  cl_event
      last_barrier; // if not null all new events to OOO queue must depend on it
} _cl_command_queue;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_command_queue);

// A struct to keep the information about each registered callbacks for memObj
// destructor. This will be used as a linked list.
typedef struct acl_mem_destructor_user_callback {

  struct acl_mem_destructor_user_callback
      *next; // Pointer to the next acl_mem_destructor_user_callback object. The
             // last object points to null.
  void *notify_user_data; // Pointer to user data.
  acl_mem_destructor_notify_fn_t
      mem_destructor_notify_fn; // The callback function, provided by the user.
} acl_mem_destructor_user_callback;

// The bookkeeping required to keep track of a block of allocated memory.
// The storage for these structs is owned by the acl_platform object.
// But these structs are only valid when attached to a valid context.
// When the context is destroyed, all associated _cl_mem blocks are
// invalidated and hence the memory is seen as released.
typedef struct _cl_mem {
  // These fields are used for all types of memory blocks, i.e. both
  // allocated and unallocated blocks.
  cl_icd_dispatch *dispatch;

  // Magic value set to ACL_MEM_MAGIC to identify a valid cl_mem object
  acl_object_magic_t magic;

  // Data structure to keep track of host pipe info
  host_pipe_t *host_pipe_info;

  cl_uint refcount;

  // Current block allocation:
  acl_block_allocation_t *block_allocation;

  // What context does this memory block belong to?
  // This is 0 for unallocated blocks.
  cl_context context;

  // Counter for buffer mappings.
  // If this is non-zero, then the buffer is mapped into host memory.
  // (It is incremented only when the mapping takes effect, i.e. after all
  // enqueue wait time has elapsed and we potentially copy the data into host
  // memory.)
  cl_uint mapping_count;

  // Flag to tell us if this memory has been auto mapped to the host. This is
  // done when there are sub-buffers, and we need to copy live data back to the
  // host before doing an operation with an overlapping sub-buffer or the parent
  // buffer.
  int auto_mapped;

  // In order to enqueue a kernel on a device, we must know at enqueue time if
  // there is enough space for buffer on that device. Any number of kernels can
  // be enqueued on any number of devices. To solve this, we reserve a block on
  // the device at kernel enqueue time. We also can't free a block until we know
  // it is no longer needed. For each device, we keep track of the reserved
  // block allocation as well as the number of kernels in the queue which will
  // run on that device. At kernel enqueue, a block will only be reserved if the
  // count is zero, otherwise the count is incremented. When the kernel
  // completes, the count is decremented. If the buffer is moved to another
  // device, and the count is zero, the block is freed.
  std::vector<acl_block_allocation_t *> reserved_allocations[ACL_MAX_DEVICE];
  std::vector<int> reserved_allocations_count[ACL_MAX_DEVICE];

  // Is the master (i.e. writable) copy of the buffer logically in the host
  // memory space? This is true under exactly the following conditions:
  //    - It is a host-side buffer (user provided or automatically allocated)
  //    - It is a device buffer and:
  //         - The number of fulfilled map requests exceeds the number of
  //           unmap requests.
  //         - At least one of those map requests was a write request.
  //
  //           (Corner case? Should only count the *writable* mappings?
  //           But unmap doesn't say what the map flags were, so we can't track
  //           that. Confirmed: OpenCL 1.2 section 5.4.3 says it's the
  //           programmer's responsibility to ensure a writable buffer is
  //           unmapped before calling a kernel that may use it at all (similar
  //           for copy/read/write buffer) Also, if the buffer is mapped for
  //           read, then you can't run any kernel that may write to the
  //           buffer.)
  int writable_copy_on_host;

  // The flags used to create this buffer.
  // Among other things, the flags determine whether the memory is owned
  // by the host, or whether it's allocated as local memory at kernel runtime.
  //
  // In the CL_MEM_USE_HOST_PTR case, the memory is owned by the host and
  // automatically copied into local memory at kernel invocation time
  // (cached), and copied back when the kernel finishes execution.
  //
  // A __local cl_mem object may be used by more than one kernel
  // simultaneously.  So we can't store the local memory address ranges
  // here.  We have to store it in the kernel arguments instead.
  // We use a separate cl_mem object for each one of those.
  cl_mem_flags flags;
  size_t size; // User-specified size

  // This variable identifies the bank to which the buffer should be allocated
  // on when the user has compiled their design with -no-interleaving flag. Bank
  // ID is specified through either CL_CHANNEL_#_INTELFPGA flag on
  // clCreateBuffer or CL_MEM_CHANNEL_INTEL property on
  // clCreateBufferWithPropertiesINTEL.
  cl_uint bank_id;

  // This variable only has a valid value when _cl_mem.flags &
  // CL_MEM_COPY_HOST_PTR is true. This variable is used to skip error checks in
  // clEnqueueWriteBuffer when initializing a read-only or no-access buffer
  // since there is an internal use of the clEnqueueWriteBuffer API to copy the
  // contents of the host pointer into this buffer which should not error out.
  cl_bool copy_host_ptr_skip_check;

  // The host memory block we have allocated for this buffer.
  //
  // This occurs in two cases:
  //    - When the user requested a buffer vi CL_ALLOC_HOST_PTR
  //    - When device buffers have backing store.
  //
  // We only ever expose the "aligned" pointer member,
  // to ensure faster data transfers.
  acl_aligned_ptr_t host_mem;

  cl_mem_object_type mem_object_type;

  union {
    struct {
      // The host_ptr arg used when this buffer was created.
      void *host_ptr;
      cl_bool is_subbuffer;
      cl_mem parent;
      cl_mem next_sub;
      cl_mem prev_sub;
      size_t sub_origin;
      size_t num_subbuffers;
    } buffer_objs;
    struct {
      // The host_ptr arg used when this buffer was created.
      void *host_ptr;
      // Image format provided by user
      cl_image_format *image_format;
      // Image description provided by user
      cl_image_desc *image_desc;
    } image_objs;
    struct {
      // Size in bytes of pipe packet
      cl_uint pipe_packet_size;
      // Pipe capacity
      cl_uint pipe_max_packets;
      // Properties of pipe (always NULL in 2.0)
      const cl_pipe_properties *properties;
    } pipe_objs;
  } fields;

  // Has the allocation been deferred until we can figure out which device to
  // allocate on?
  int allocation_deferred;
  // The allocation is deferred, and we've stored the host buffer in shadow
  // memory until the allocation happens
  int mem_cpy_host_ptr_pending;

  // If this is a heterogeneous buffer, what is the index of the memory it uses
  unsigned int mem_id;

  // Is this buffer an SVM buffer
  int is_svm;

  // Memory destructor callback list
  acl_mem_destructor_user_callback *destructor_callback_list;
} _cl_mem;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_mem);

typedef struct _cl_kernel {
  cl_icd_dispatch *dispatch;

  // Magic value set to ACL_MEM_MAGIC to identify a valid cl_mem object
  acl_object_magic_t magic;

  cl_uint refcount;
  cl_program program;
  const acl_accel_def_t *accel_def; // provides the name and interface

  // The buffer used to store the profile data after the kernel is complete
  uint64_t *profile_data;

  // Printf support.
  // The buffer used by kernel invocation to dump printf output to.
  // This is NULL if there are no printfs in the kernel.
  cl_mem printf_device_buffer;

  // The printf_device_ptr is ptr from SVM allocation when printf
  // buffer is in SVM buffer.
  void *printf_device_ptr;

  // Argument definitions.

  // Offsets into arg_value are defined by the sizes in the accelerator
  // definition.
  std::vector<int> arg_defined;
  char *arg_value;
  size_t arg_value_size; // Size of the dynamic memory allocated for arg_value
  std::vector<cl_bool> arg_is_svm;
  std::vector<cl_bool> arg_is_ptr;

  // usm allocated pointers used/accessed by kernel
  std::unordered_set<void *> ptr_hashtable;
  std::vector<void *> ptr_arg_vector;

  // The device binary containing the implementation of this kernel.
  // Currently one cl_kernel is only associated with one device
  // which deviates from the OpenCL definition where a cl_kernel
  // should be associated with all devices in the enclosing cl_context.
  // Eventually this should be an array of ACLDeviceBinaries similar
  // to how cl_program contains an array of dev_prog.
  const acl_device_binary_t *dev_bin;

  // In ACL_HAL_DEBUG mode, printf buffer could be dumped before Kernel ends
  // Therefore, we need to keep track of how much data has been processed.
  size_t processed_printf_buffer_size;
} _cl_kernel;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_kernel);

typedef struct kernel_list_s kernel_list_t;
struct kernel_list_s {
  cl_kernel kernel;
  kernel_list_t *next;
};

typedef struct _cl_program {
  cl_icd_dispatch *dispatch;
  acl_object_magic_t magic;
  cl_uint refcount;
  cl_context context;

  // The devices associated with the context the program belongs to
  // Note these are not the devices that the program is on.
  cl_uint num_devices;
  cl_device_id device[ACL_MAX_DEVICE]; // [0..num_devices-1]

  // Device-specific program info.
  // Indices match the device[] array.
  acl_device_program_info_t *dev_prog[ACL_MAX_DEVICE]; // [0..num_devices-1]

  cl_uint num_kernels;
  kernel_list_t *kernel_list;
  kernel_list_t *kernel_free_list;

  // Store the program source, when created with source.
  size_t source_len; // Length of the source tex, including terminating NUL
  unsigned char *source_text; // NULL if the program was created without source.

  // Store whether or not the program was created using builtin kernels
  cl_bool uses_builtin_kernels;
} _cl_program;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_program);

typedef struct acl_svm_entry_s acl_svm_entry_t;
struct acl_svm_entry_s {
  cl_bool read_only;
  cl_bool write_only;
  cl_bool is_mapped;
  void *ptr;
  size_t size;
  acl_svm_entry_t *next;
};

typedef struct _cl_context {
  cl_icd_dispatch *dispatch;
  acl_object_magic_t magic;
  cl_uint refcount;
  acl_compiler_mode_t compiler_mode;

  // Is this context in the middle of being freed?
  // Fix re-entrancy of clReleaseContext.
  int is_being_freed;

  ////////////////////////////
  // Behaviour switches dependent on compiler mode
  //

  // This is set to 1 only in compiler mode
  // "CL_CONTEXT_COMPILER_MODE_SPLIT_KERNEL_INTELFPGA" This mode is for running
  // SYCL programs. During SYCL program compilation, we transform a .spv file
  // containing multiple kernels into
  //  multiple .aocx files, each contains only one kernel. We call this "kernel
  //  spliting".
  // In runtime, we process these multiple aocx files: load an aocx file only
  // when its kernel is
  //  being created.
  int split_kernel;

  // Does this compiler mode creat or use an on-disk library of
  // device program binaries?  This is true for the offline compiler modes.
  int uses_program_library;

  // In this context, do programs build from source?
  // For OpenCL conformance, this is required to be true.
  // In embedded compiler mode, this is false.
  int compiles_programs;
  // Does this mode only compile a program partially?
  // As in, it is not executable after running the build command?
  // If so, then also emit a "build.aocx.cmd" command script.
  int compiles_programs_incompletely;

  // In this context, do we save and restore device side buffers when
  // scheduling a device reprogram?
  // Must be true if programs_devices is true.
  // (For testing and fidelity purposes, this is true in offline-capture
  // mode, though progarms_devices is false.)
  int saves_and_restores_buffers_for_reprogramming;

  // Is the device SOF reprogrammed when switching between different
  // cl_program objects?  Depends only on compiler_mode.
  //
  // For OpenCL conformance, this is required to be true.
  // In embedded compiler mode, this is false.
  int programs_devices;

  // Do we load and use kernel definitions from the program library?
  // In embedded mode we only use the definitions from auto-discovery.
  // But in dynamically reprogrammable modes we load kernel definitions
  // from auto-discovery strings found on disk in the program library.
  int uses_dynamic_sysdef;

  // Should we try to hide device reprogram latency by doing it the first
  // time we load a "built" binary?
  // Should default to true.
  int eagerly_program_device_with_first_binary;

  //
  ////////////////////////////

  ////////////////////////////
  // Data used in various compiler modes.
  //

  // The compile command to use to actually create a binary.
  // Not normally run.  Example:  "aoc"
  // This is used in compile capture and compile online modes.
  // (Compile online mode is just for testing, or you'll be sorry.
  std::string compile_command;

  // In offline compiler modes, we store or retrieve device program
  // binaries from a file tree.  In those modes, the program_library_root
  // is the absolute path of that tree of files.
  // Otherwise it's just a string, and doesn't necessarily exist as a
  // directory.
  std::string program_library_root;

  //
  ////////////////////////////

  // Do device memory buffers have backing store the host?
  // That is, is there a correspondingmemory area in host that can
  // store all the contents of the device buffer.
  // This is used for two purposes:
  // 	- When the user explicitly maps a buffer in to host memory
  // 	- When reprogramming the device, the device global memory
  // 	is wiped out.  So we use the backing store to temporarily store
  // 	the contents of those buffers during programming.
  // This is 0 (false) when the compiler is in embedded mode.
  int device_buffers_have_backing_store;

  // The number of user-supplied properties provided at creation time.
  // This includes the terminating 0.
  // So if the user supplied  { CL_CONTEXT_PLATFORM, blah, 0 } then this
  // is 3.
  int num_property_entries;
  cl_context_properties properties[ACL_MAX_NUM_CONTEXT_PROPERTY_ENTRIES];

  // Error notifcation callback.
  acl_notify_fn_t notify_fn;
  void *notify_user_data;

  // Devices attached to this context.
  cl_uint num_devices;
  cl_device_id device[ACL_MAX_DEVICE];

  // Command queues.
  // Array of pointers to valid command queues with capacity
  // "num_command_queues_allocated"
  //  Initial capacity is ACL_INIT_COMMAND_QUEUE_ALLOC. Will double if capacity
  //  is reached
  //   i.e if (num_command_queues == num_command_queues_allocated) {
  //   num_command_queues_allocated *= 2; }
  //  The valid pointers are always at the start of this array i.e. from 0 to
  //  num_command_queues-1
  cl_command_queue *command_queue;
  int num_command_queues;           // number of valid queue
  int num_command_queues_allocated; // capacity

  // Keeping track of the latest command queue with updates, so the next round
  // of acl_idle_update, starts from there. This is to avoid starvation of
  // events in cqs with higher index, specially if the device_op_queue is almost
  // full.
  int last_command_queue_idx_with_update;

  // This auto-generated command queue is used for internal purposes.
  // The user does not have access to it.
  // For example, this queue is used to move profiling data around.
  cl_command_queue auto_queue;

  // This auto-generated command queue where we process user events.
  // User events are an OpenCL 1.1 feature.
  // A user event is only used as an scheduling dependency; user
  // events never submit a command, so it's safe to be processed
  // out-of-order.
  cl_command_queue user_event_queue;

  // Memory allocator stuff.
  //
  // When using an offline device, there can only be offline devices.
  // Furthermore, there is no device actually attached to the host, and so
  // no device global memory.  In that case we have to emulate the device
  // global memory in host memory, so we can still run most of the API.
  // But the mem region must be private to this context, because a
  // different context might be using a real device.  So we need this
  // region structure here.
  // This memory will not have banking enabled.
  acl_mem_region_t emulated_global_mem;

  // Points to the memory region to be used for this context.
  // In normal operation, this just points to acl_platform.global_mem.
  // But if this context consists just of offline devices, then this
  // points to the emulated_global_mem field.
  acl_mem_region_t *global_mem;

  // Min value of CL_DEVICE_MAX_MEM_ALLOC_SIZE over all devices in this context
  cl_ulong max_mem_alloc_size;

  // List of SVM allocations in this context
  acl_svm_entry_t *svm_list;

  // Events.  These are dynamically allocated.
  std::list<cl_event> free_events; // pool of events that are free to be used
  std::set<cl_event> used_events;  // list of allocated events that are being
                                   // used by the runtime
  // Used to uniquely assign event ids within a context, as events are never
  // deleted just reused
  unsigned int num_events;

  // Host-side buffers to store kernel invocation data.
  // At kernel invocation time, the buffer is copied to the device via DMA.
  acl_kernel_invocation_wrapper_t **invocation_wrapper;
  unsigned num_invocation_wrappers_allocated;

  // The buffer read/write commands are given a pointer to host memory.
  // This cl_mem object is used to wrap those pointers for internal
  // buffer-related methods.
  // This lets us treat read/write/copy in uniform way.
  //
  // This is a "fake" buffer.  We would *like* its base pointer to
  // be 0 (start of memory), and the size to be the max value of size_t.
  // This way we can artificially wrap any memory location by using
  // the original pointer as the offset into this buffer.
  // But the OpenCL API doesn't let us create a USE_HOST_MEM buffer with
  // host pointer set to 0.
  // So we fake it and use a base pointer of ACL_MEM_ALIGN instead.
  // So when we wrap a pointer, we first subtract ACL_MEM_ALIGN from
  // its (integer) value.
  cl_mem unwrapped_host_mem;

  // Each context is automatically associated with a bunch of cl_mem
  // objects, for internal use only.
  // This member counts those references, so we can separate user
  // references from automatic references.  This is required by
  // clReleaseContext to know when to really shut down the context
  // object.
  cl_uint num_automatic_references;

  // Keeps track of pipes binded to this context (both kernel pipes and host
  // pipes)
  std::vector<cl_mem> pipe_vec;

  // USM allocations for this context
  std::list<acl_usm_allocation_t *> usm_allocation;

  // Callbacks to be called while explicitly copying memory between
  // device and host immediately before and after programming.
  // This is used for testing only.
  void(CL_CALLBACK *reprogram_buf_read_callback)(cl_mem, int after_xfer);
  void(CL_CALLBACK *reprogram_buf_write_callback)(cl_mem, int after_xfer);
} _cl_context;

ACL_DECLARE_CL_OBJECT_ALLOC_FUNCTIONS(cl_context);

enum context_mode_lock { BUILT_IN, BINARY };

typedef struct _cl_device_id {
  cl_icd_dispatch *dispatch;
  int id;
  cl_platform_id platform;
  cl_uint refcount;
  cl_device_type type;
  cl_uint vendor_id;
  const char *version;
  const char *driver_version;
  acl_device_def_t def;
  size_t min_local_mem_size; // min size of all local mems for accelerators in
                             // this device.

  // Indicates the number of contexts in which the device is currently opened.
  int opened_count;
  // Indicates if the device has been opened, whether or not it's currently
  // open. If a device has never been opened then the info in the structure is
  // not valid yet.
  int has_been_opened;

  // Creating a context for the device locks all subsequent context creations
  // using this device to be of the same mode (unless all contexts created for
  // this device are released)
  context_mode_lock mode_lock;

  unsigned int address_bits; // cache address bits to avoid GetDeviceInfo calls

  int present; // Is the device present in the host system?

  // Error notification callback.
  CL_EXCEPTION_TYPE_INTEL device_exception_status;
  acl_exception_notify_fn_t exception_notify_fn;
  void *exception_notify_user_data;
  CL_EXCEPTION_TYPE_INTEL listen_mask;

  void *exception_private_info[sizeof(CL_EXCEPTION_TYPE_INTEL) * 8];
  size_t exception_cb[sizeof(CL_EXCEPTION_TYPE_INTEL) * 8];

  // In non-embedded mode, we can reprogram the device during runtime of
  // the host program.
  // A device can only be programmed with one cl_program at a time.
  //
  // These two fields keep track of what is programmed on the device:
  //
  //    loaded_bin:   What acl_device_binary_t is loaded *right now*.
  //
  //    last_bin:     What acl_device_binary_t will be loaded on the device
  //    after
  //                   after the last operation on the device op queue
  //                   has completed?
  //
  const acl_device_binary_t *loaded_bin = nullptr;
  const acl_device_binary_t *last_bin = nullptr;
} _cl_device_id;

typedef struct _cl_sampler {
  cl_icd_dispatch *dispatch;
  int id;
  int link;

  cl_uint refcount;

  cl_context context;

  cl_sampler_properties normalized_coords;
  cl_sampler_properties addressing_mode;
  cl_sampler_properties filter_mode;
} _cl_sampler;

// These are the kinds of device operations that can occur.
typedef enum {

  // The nil value.
  ACL_DEVICE_OP_NONE

  // Run a kernel.
  // Corresponds to acl_command_info_t.info.ndrange_kernel
  ,
  ACL_DEVICE_OP_KERNEL

  // Transfer a single buffer
  // Corresponds to acl_command_info_t.info.mem_xfer
  ,
  ACL_DEVICE_OP_MEM_TRANSFER_READ,
  ACL_DEVICE_OP_MEM_TRANSFER_WRITE,
  ACL_DEVICE_OP_MEM_TRANSFER_COPY

  // Load a new FPGA programming image.
  // Needed when there is no FPGA image loaded, or when we're running a
  // kernel from a cl_program different from the last scheduled kernel.
  // Corresponds to acl_command_info_t.info.ndrange_kernel
  ,
  ACL_DEVICE_OP_REPROGRAM

  // Migrage memory object to a specific device
  ,
  ACL_DEVICE_OP_MEM_MIGRATION

  // USM Memcpy that should call HAL's copy API without any extra work.
  // Corresponds to acl_command_info_t.info.ptr_xfer
  ,
  ACL_DEVICE_OP_USM_MEMCPY,
  // Progrgam based hostpipe read or write
  ACL_DEVICE_OP_HOSTPIPE_READ,
  ACL_DEVICE_OP_HOSTPIPE_WRITE,
  ACL_NUM_DEVICE_OP_TYPES

} acl_device_op_type_t;

// These are device operation conflict types.
// Each device operation falls conflicts with other device operations
// in one of these ways.
//
// The mapping from a device operation to conflict type depends primarily
// on the device operation type, but also on dynamic state of the
// operation.  For example, a kernel that is stalled on a printf buffer
// overflow requires a memory transfer to become unstuck, so it maps to
// the ACL_CONFLICT_MEM conflict type.
typedef enum {
  ACL_CONFLICT_NONE // Acts like nothing. For testing only.
  ,
  ACL_CONFLICT_MEM_READ // Acts like a memory transfer from device to host
  ,
  ACL_CONFLICT_MEM_WRITE // Acts like a memory transfer from host to device
  ,
  ACL_CONFLICT_MEM // Acts like a memory transfer from device to device or host
                   // to host
  ,
  ACL_CONFLICT_KERNEL // Acts like a pure kernel execution
  ,
  ACL_CONFLICT_REPROGRAM // Acts like a device reprogram
  ,
  ACL_CONFLICT_HOSTPIPE_READ // Acts like a hostpipe read from the host channel
  ,
  ACL_CONFLICT_HOSTPIPE_WRITE // Acts like a hostpipe write from the host
                              // channel
  ,
  ACL_NUM_CONFLICT_TYPES
} acl_device_op_conflict_type_t;

// This is command-specific information for one device operation.
typedef struct {
  acl_device_op_type_t type;

  // The user-level command/event that triggered this device operation.
  // Only certain user events trigger the creation of a device operation.
  cl_event event;

  // For the buffer mirroring operations, we also need to know the
  // index into the mirror_buf_info_t.
  unsigned int index;

  // How many bytes are waiting to be picked up from the printf buffer for
  // this kernel?
  // This is updated by the HAL in the middle of an interrupt, and might
  // change any time the kernel is CL_RUNNING.
  // The device op queue scheduler might first see this as non-zero when
  // it sees the CL_COMPLETE status for the first time, but it won't
  // change after that.
  cl_uint num_printf_bytes_pending;

  // Indicate whether this operation is dumping printf buffer before the Kernel
  // for debug purpose
  int debug_dump_printf = 0;
} acl_device_op_info_t;

// An operation to be performed on a device.
// This is the the device-updating component of a user-level command/event on a
// regular command queue on some user context.
//
// One of these objects is created as part of the "submit" action of the
// user-level command.
typedef struct acl_device_op_t {
  int id; // The index of this op in the device op queue array.

  // What kind of conflicts does this operation generate?
  // This is a pure function of type, but is good to cache.
  acl_device_op_conflict_type_t conflict_type;

  // The device op queue maintains two lists: the live operations (still
  // in flight), and the free list.
  // The "link" field is id of the next operation.
  // If this operation is still in flight, then "link" is the id of the
  // next live operation.
  // If this operation is not in flight, then it's on the free list and
  // "link" is the id of the next operation on the free list.
  // In both cases, a link value of ACL_OPEN (-1) indicates that this
  // operation is the last in its list. (ACL_OPEN is like NULL).
  int link;

  // "status" tracks the processing stage for this operation.
#define ACL_PROPOSED (4)
  //    ACL_PROPOSED == 4
  //       This operation is being constructed along with some other
  //       device operations.
  //       It should not be executed while in this state.
  //       It might be cancelled due to some other failure in enqueueing
  //       its peer device operations.
  //
  //    CL_QUEUED == 3
  //       This operation is ready to be executed.
  //       This is the status after all of the device operations for this
  //       user-level command/event have successfully been constructed.
  //
  //    CL_SUBMITTED == 2
  //       This operation has been submitted to the device (HAL actually)
  //       for execution.
  //
  //    CL_RUNNING == 1
  //       This operation is in progress.
  //
  //    CL_COMPLETE == 0
  //       This operation finished successfully.
  //
  // We say that an operation is "in flight" if the device may be doing
  // work to complete the operation.  This translates to:
  //
  //    in_flight == ( CL_RUNNING <= status && status <= CL_SUBMITTED )
  cl_int status;

  // The "execution_status" field tracks the processing by the HAL.
  // It takes on values CL_SUBMITTED, CL_RUNNING, CL_COMPLETE only.
  // The the CL_RUNNING and CL_COMPLETE statuses are set by the HAL itself
  // or asynchronously in response to a device signal.
  //
  // The key difference between "status" and "execution_status" is that
  // "status" is updated by the device operation queueing system, and the
  // execution_status is updated by the HAL.
  // We need to keep those distinct so the queueing system propagates
  // status back to the owning event only once.
  // For example, send the CL_RUNNING timestamp back to the owning event only
  // when execution_status == CL_RUNNING, and status != execution_status.
  cl_int execution_status;

  // The device timestamp for the transition of execution_status to
  // CL_SUBMITTED, CL_RUNNING and CL_COMPLETE states.
  // These are updated by the HAL.
  cl_ulong timestamp[4];

  // Is this the first or last device operation for the owning event?
  int first_in_group;
  int last_in_group;

  // Exclusion group id.
  // Operations in the same group work together as in a transaction.
  // They appear on the queue in sequence, and must not run concurrently
  // with each other.
  // The group id is the id of the last operation in the group.
  int group_id;

  // The command-specific info for this particular operation.
  acl_device_op_info_t info;

} acl_device_op_t;

// Info for profiling the device op queue.
typedef struct {
  cl_uint num_queue_updates;
  cl_uint num_exclusion_checks;
  cl_uint num_conflict_checks;
  cl_uint num_submits;
  cl_uint num_live_op_pending_calcs;
  cl_uint num_queued;
  cl_uint num_submitted;
  cl_uint num_running;
  cl_uint num_complete;
} acl_device_op_stats_t;

// An ordered queue of device operations.
//
// The operations are kept in linked lists, so that we can prune
// operations in the order that they are completed.
// This allows us to support scenarios where a few operations are
// long-running (or infinite), while also processing later short-running
// operations indefinitely.
//
// We maintain two lists: a list of live operations, and a free list.
//
// Multiple committed operations might be in flight, if they are
// all compatible with each other.
//
// For example:
//    Memory transfers conflict with other memory transfers
//          because our PCIe driver does not support concurrent transfers
//    Kernel executions from different programs conflict only if they are
//    from different cl_programs.
//    Device reprogram commands conflict with everything else.
//
// Each user-level command/event that requires device interaction enqueues
// one or more device operations on this queue.
//
// If more than one device operation is required, then they all appear
// contiguously in the queue.
//
// Operations are first enqueued in a "proposed" state, and then converted
// to "committed" state to indicate that it should actually be executed.
//
// The live operation queue consists of zero or more "committed" operations,
// followed by zero or more "proposed" operations.
//
// A proposed operation is one of possibly many operations required
// to be queued to satisfy a user-level event/command.
// The APIs allow the caller to build up a set of proposed operations
// before committing them.  This allows us to fail gracefully and forget
// the proposals, and try again later when there is more free space on the
// queue.
typedef struct acl_device_op_queue_t {
  // Max number of operations permitted.  Normally this is ACL_MAX_DEVICE_OPS.
  int max_ops;

  // Number of committed operations in the circular buffer.
  int num_committed;
  // Number of proposed operations in the circular buffer.
  int num_proposed;

  // List maintenance.  We need to track:
  //    - The head of the live queue
  //    - The tail of the committed queue
  //    - The tail of the live queue.
  //       This might be same as the tail of the committed queue.
  //    - The head of the free queue
  // The following variables contain the index into the op array
  // of the respective nodes.
  // In each case, an ACL_OPEN (-1) index indicates list termination.
  //
  // Note: There are proposed operations if and only if
  //    last_committed != last_live.
  int first_live;
  int last_committed;
  int last_live;
  int first_free;

  acl_device_op_stats_t stats;

  // The operations themselves.
  acl_device_op_t op[ACL_MAX_DEVICE_OPS];

  // Used to cache the devices; indexed by physical_id
  // Used for checking if the device has concurrent read/write support
  acl_device_def_t *devices[ACL_MAX_DEVICE];

  // These function pointers must be set to the actions to be taken when
  // kicking off various device activities.
  void (*launch_kernel)(void *, acl_device_op_t *);
  void (*transfer_buffer)(void *, acl_device_op_t *);
  void (*process_printf)(void *, acl_device_op_t *);
  void (*program_device)(void *, acl_device_op_t *);
  void (*migrate_buffer)(void *, acl_device_op_t *);
  void (*usm_memcpy)(void *, acl_device_op_t *);
  // For test purposes, log transition to CL_RUNNING, CL_COMPLETE
  void (*log_update)(void *, acl_device_op_t *, int new_status);
  void (*hostpipe_read)(void *, acl_device_op_t *);
  void (*hostpipe_write)(void *, acl_device_op_t *);
  void *user_data; // The first argument provided to the callbacks.

} acl_device_op_queue_t;

typedef enum {
  ACL_OBJ_CONTEXT,
  ACL_OBJ_MEM_OBJECT,
  ACL_OBJ_PROGRAM,
  ACL_OBJ_KERNEL,
  ACL_OBJ_COMMAND_QUEUE,
  ACL_OBJ_EVENT
} acl_cl_object_type_t;

typedef struct acl_cl_object_node_t {
  acl_cl_object_type_t type;
  void *object;
  struct acl_cl_object_node_t *next;
} acl_cl_object_node_t;

typedef struct _cl_platform_id
// WARNING: First few fields definitions must be kept in sync with
// initializer in acl_platform.c
{
  cl_icd_dispatch *dispatch;
  int initialized; // Has this been initialized?

  // This counter is used as the quickest check in the acl_idle_update loop
  int device_exception_platform_counter; // indicates number of devices with at
                                         // least one exception

  // The setting of environment variable CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA, if
  // any.
  std::string offline_device;
  // Cache context offline mode specified by environment variables
  // CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA or CL_CONTEXT_MPSIM_DEVICE_INTELFPGA
  int offline_mode;

  // Should we track and automatically release leaked objects?
  // This helps immensely with the OpenCL conformance tests which tend to
  // release objects willy nilly upon any error condition.
  // This is false in normal flows.
  // When finished a test flow, you still have to call
  // acl_release_leaked_objects()
  int track_leaked_objects;
  // The chain of OpenCL objects that have been allocated.
  // This is non-NULL only if auto_release_leaked_objects is true.
  acl_cl_object_node_t *cl_obj_head;

  // The above fields are initialized statically in acl_platform.c

  const char *name;
  const char *vendor;
  const char *suffix;
  cl_uint vendor_id;
  const char *version;
  const char *extensions;
  const char *profile;

  // The HAL
  const acl_hal_t *hal;

  // Everything we need to know about the SOF programmed on the FPGA
  // at the beginning of execution.
  // This includes the interfaces in the compiled program and its
  // device memory configuration.
  //
  // In the "embedded" compiler mode, this is the only program definition
  // that matters.  The FPGA is never reprogrammed in "embedded" mode.
  //
  // In non-embedded mode, we can reprogram the device, and you get
  // interface info about the currently-loaded FPGA image via
  // the cl_device_id
  //
  // In test modes, this may contain multiple device definitions.
  acl_system_def_t *initial_board_def;

  // Number of devices supported in this incarnation
  cl_uint num_devices;
  struct _cl_device_id device[ACL_MAX_DEVICE]; // [0..num_devices-1]

  // The memory allocator.
  // All contexts share the host auto memory, and device global memory.
  // So memory has to be managed at the platform level.
  acl_mem_region_t host_user_mem; // Host allocations with user-provided memory.
  acl_mem_region_t
      host_auto_mem; // Host allocations with memory provided by OpenCL runtime
  acl_mem_region_t global_mem; // Device global memory.
  int num_global_banks; // Number of equal-sized banks that divide up global mem

  struct _cl_sampler
      sampler[ACL_MAX_SAMPLER]; // Storage for the memory block bookkeeping.
  int free_sampler_head;        // ID of the chain of free blocks.

  // The device operation queue.
  // These are the operations that can run immediately on the device.
  acl_device_op_queue_t device_op_queue;

  // Limits. See clGetDeviceInfo for semantics.
  unsigned int max_param_size;
  size_t min_local_mem_size;      // >= 1KB for embedded profile
  unsigned int max_constant_args; // >=4 for embedded profile
  int max_compute_units;
  int max_work_item_dimensions;
  unsigned int max_work_group_size; // total grid size.
  unsigned int max_work_item_sizes; // for each dimension
  unsigned int mem_base_addr_align; // CL_DEVICE_MEM_BASE_ADDR_ALIGN
  int min_data_type_align_size;     // CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE
  unsigned int printf_buffer_size;  // CL_DEVICE_PRINTF_BUFFER_SIZE
  cl_device_fp_config single_fp_config;
  cl_device_fp_config double_fp_config;
  cl_command_queue_properties queue_properties;

  // When capturing program sources, this is the
  // base name of file in which to write the source.
  char capture_base_path[ACL_MAX_PATH];
  // The next capture ID to use when creating a source file.
  // If this is ACL_OPEN, then don't capture any sources.
  int next_capture_id;

  // Pipe parameters
  cl_uint max_pipe_args;
  cl_uint pipe_max_active_reservations;
  cl_uint pipe_max_packet_size;

  // A set of contexts within this platform
  std::set<cl_context> contexts_set;

} _cl_platform_id;

// returns pointer to single global cl_platform_id instance in cl_platform.c
cl_platform_id acl_get_platform();

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDsIntelFPGA(
    cl_platform_id platform, cl_device_type device_type, cl_uint num_entries,
    cl_device_id *devices, cl_uint *num_devices);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfoIntelFPGA(
    cl_device_id device, cl_device_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetPlatformIDsIntelFPGA(
    cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetPlatformInfoIntelFPGA(
    cl_platform_id platform, cl_platform_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL clCreateContextIntelFPGA(
    const cl_context_properties *properties, cl_uint num_devices,
    const cl_device_id *devices, acl_notify_fn_t pfn_notify, void *user_data,
    cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_context CL_API_CALL clCreateContextFromTypeIntelFPGA(
    const cl_context_properties *properties, cl_device_type device_type,
    acl_notify_fn_t pfn_notify, void *user_data, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainContextIntelFPGA(cl_context context);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseContextIntelFPGA(cl_context context);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetContextInfoIntelFPGA(
    cl_context context, cl_context_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueueWithPropertiesIntelFPGA(
    cl_context context, cl_device_id device,
    const cl_queue_properties *properties, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_command_queue CL_API_CALL clCreateCommandQueueIntelFPGA(
    cl_context context, cl_device_id device,
    cl_command_queue_properties properties, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueueIntelFPGA(cl_command_queue command_queue);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueueIntelFPGA(cl_command_queue command_queue);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetCommandQueueInfoIntelFPGA(
    cl_command_queue command_queue, cl_command_queue_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetCommandQueuePropertyIntelFPGA(
    cl_command_queue command_queue, cl_command_queue_properties properties,
    cl_bool enable, cl_command_queue_properties *old_properties);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateBufferIntelFPGA(cl_context context,
                                                        cl_mem_flags flags,
                                                        size_t size,
                                                        void *host_ptr,
                                                        cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    const cl_image_desc *image_desc, void *host_ptr, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImageIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    const cl_image_desc *image_desc, void *host_ptr, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage2DIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    size_t image_width, size_t image_height, size_t image_row_pitch,
    void *host_ptr, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage3DIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    size_t image_width, size_t image_height, size_t image_depth,
    size_t image_row_pitch, size_t image_slice_pitch, void *host_ptr,
    cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainMemObjectIntelFPGA(cl_mem mem);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObjectIntelFPGA(cl_mem mem);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSupportedImageFormatsIntelFPGA(
    cl_context context, cl_mem_flags flags, cl_mem_object_type image_type,
    cl_uint num_entries, cl_image_format *image_formats,
    cl_uint *num_image_formats);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetMemObjectInfoIntelFPGA(
    cl_mem mem, cl_mem_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetImageInfoIntelFPGA(
    cl_mem image, cl_mem_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_sampler CL_API_CALL
clCreateSamplerIntelFPGA(cl_context context, cl_bool normalized_coords,
                         cl_addressing_mode addressing_mode,
                         cl_filter_mode filter_mode, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainSamplerIntelFPGA(cl_sampler sampler);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseSamplerIntelFPGA(cl_sampler sampler);

ACL_EXPORT
CL_API_ENTRY cl_sampler clCreateSamplerWithPropertiesIntelFPGA(
    cl_context context, const cl_sampler_properties *sampler_properties,
    cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSamplerInfoIntelFPGA(
    cl_sampler sampler, cl_sampler_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithSourceIntelFPGA(
    cl_context context, cl_uint count, const char **strings,
    const size_t *lengths, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBinaryIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const size_t *lengths, const unsigned char **binaries,
    cl_int *binary_status, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainProgramIntelFPGA(cl_program program);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseProgramIntelFPGA(cl_program program);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clBuildProgramIntelFPGA(
    cl_program program, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, acl_program_build_notify_fn_t pfn_notify,
    void *user_data);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clUnloadCompilerIntelFPGA(void);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramInfoIntelFPGA(
    cl_program program, cl_program_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetProgramBuildInfoIntelFPGA(
    cl_program program, cl_device_id device, cl_program_build_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_kernel CL_API_CALL clCreateKernelIntelFPGA(
    cl_program program, const char *kernel_name, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clCreateKernelsInProgramIntelFPGA(cl_program program, cl_uint num_kernels,
                                  cl_kernel *kernels, cl_uint *num_kernels_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainKernelIntelFPGA(cl_kernel kernel);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseKernelIntelFPGA(cl_kernel kernel);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgIntelFPGA(cl_kernel kernel,
                                                        cl_uint arg_index,
                                                        size_t arg_size,
                                                        const void *arg_value);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelInfoIntelFPGA(
    cl_kernel kernel, cl_kernel_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelWorkGroupInfoIntelFPGA(
    cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clWaitForEventsIntelFPGA(cl_uint num_events, const cl_event *event_list);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventInfoIntelFPGA(
    cl_event event, cl_event_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainEventIntelFPGA(cl_event event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseEventIntelFPGA(cl_event event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetEventProfilingInfoIntelFPGA(
    cl_event event, cl_profiling_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clFlushIntelFPGA(cl_command_queue command_queue);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clFinishIntelFPGA(cl_command_queue command_queue);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    size_t offset, size_t cb, void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    size_t offset, size_t cb, const void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    size_t src_offset, size_t dst_offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_read,
    const size_t *origin, const size_t *region, size_t row_pitch,
    size_t slice_pitch, void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_write,
    const size_t *origin, const size_t *region, size_t input_row_pitch,
    size_t input_slice_pitch, const void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageIntelFPGA(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_image,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageToBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *region, size_t dst_offset,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferToImageIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_image,
    size_t src_offset, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_map,
    cl_map_flags map_flags, size_t offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_map,
    cl_map_flags map_flags, const size_t *origin, const size_t *region,
    size_t *image_row_pitch, size_t *image_slice_pitch,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueUnmapMemObjectIntelFPGA(
    cl_command_queue command_queue, cl_mem mem, void *mapped_ptr,
    cl_uint num_events, const cl_event *events, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernelIntelFPGA(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueTaskIntelFPGA(cl_command_queue command_queue, cl_kernel kernel,
                       cl_uint num_events_in_wait_list,
                       const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueNativeKernelIntelFPGA(
    cl_command_queue command_queue, void (*user_func)(void *), void *args,
    size_t cb_args, cl_uint num_mem_objects, const cl_mem *mem_list,
    const void **args_mem_loc, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueMarkerIntelFPGA(cl_command_queue command_queue, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWaitForEventsIntelFPGA(
    cl_command_queue command_queue, cl_uint num_event, const cl_event *events);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueBarrierIntelFPGA(cl_command_queue command_queue);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddressIntelFPGA(const char *func_name);

ACL_EXPORT CL_API_ENTRY cl_mem CL_API_CALL clCreatePipeIntelFPGA(
    cl_context context, cl_mem_flags flags, cl_uint pipe_packet_size,
    cl_uint pipe_max_packets, const cl_pipe_properties *properties,
    cl_int *errcode_ret);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clGetPipeInfoIntelFPGA(
    cl_mem pipe, cl_pipe_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret);

ACL_EXPORT
CL_API_ENTRY cl_int clSetEventCallbackIntelFPGA(
    cl_event event, cl_int command_exec_callback_type,
    void(CL_CALLBACK *pfn_event_notify)(cl_event event,
                                        cl_int event_command_exec_status,
                                        void *user_data),
    void *user_data);

ACL_EXPORT
CL_API_ENTRY cl_int clSetMemObjectDestructorCallbackIntelFPGA(
    cl_mem memobj,
    void(CL_CALLBACK *pfn_notify)(cl_mem memobj, void *user_data),
    void *user_data);

ACL_EXPORT
CL_API_ENTRY cl_int clEnqueueFillBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, const void *pattern,
    size_t pattern_size, size_t offset, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, const void *fill_color,
    const size_t *origin, const size_t *region, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateSubBufferIntelFPGA(
    cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type,
    const void *buffer_create_info, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_event CL_API_CALL
clCreateUserEventIntelFPGA(cl_context context, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clSetUserEventStatusIntelFPGA(cl_event event, cl_int execution_status);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    const size_t *buffer_offset, const size_t *host_offset,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    const size_t *buffer_offset, const size_t *host_offset,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, const void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    size_t src_row_pitch, size_t src_slice_pitch, size_t dst_row_pitch,
    size_t dst_slice_pitch, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetKernelArgInfoIntelFPGA(
    cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueMigrateMemObjectsIntelFPGA(
    cl_command_queue command_queue, cl_uint num_mem_objects,
    const cl_mem *mem_objects, cl_mem_migration_flags flags,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clSVMAllocIntelFPGA(cl_context context,
                                                   cl_svm_mem_flags flags,
                                                   size_t size,
                                                   unsigned int alignment);

ACL_EXPORT
CL_API_ENTRY void clSVMFreeIntelFPGA(cl_context context, void *svm_pointer);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMFreeIntelFPGA(
    cl_command_queue command_queue, cl_uint num_svm_pointers,
    void *svm_pointers[],
    void(CL_CALLBACK *pfn_free_func)(cl_command_queue /* queue */,
                                     cl_uint /* num_svm_pointers */,
                                     void *[] /* svm_pointers[] */,
                                     void * /* user_data */),
    void *user_data, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemcpyIntelFPGA(
    cl_command_queue command_queue, cl_bool blocking_copy, void *dst_ptr,
    const void *src_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemFillIntelFPGA(
    cl_command_queue command_queue, void *svm_ptr, const void *pattern,
    size_t pattern_size, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMapIntelFPGA(
    cl_command_queue command_queue, cl_bool blocking_map, cl_map_flags flags,
    void *svm_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL
clEnqueueSVMUnmapIntelFPGA(cl_command_queue command_queue, void *svm_ptr,
                           cl_uint num_events_in_wait_list,
                           const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clSetKernelArgSVMPointerIntelFPGA(
    cl_kernel kernel, cl_uint arg_index, const void *arg_value);

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clCreateSubDevicesIntelFPGA(
    cl_device_id in_device,
    const cl_device_partition_property *partition_properties,
    cl_uint num_entries, cl_device_id *out_devices, cl_uint *num_devices);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainDeviceIntelFPGA(cl_device_id device);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseDeviceIntelFPGA(cl_device_id device);

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBuiltInKernelsIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *kernel_names, cl_int *errcode_ret);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCompileProgramIntelFPGA(
    cl_program program, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_headers,
    const cl_program *input_headers, const char **header_include_names,
    void(CL_CALLBACK *pfn_notify)(cl_program program, void *user_data),
    void *user_data);

ACL_EXPORT
CL_API_ENTRY cl_program CL_API_CALL clLinkProgramIntelFPGA(
    cl_context context, cl_uint num_devices, const cl_device_id *device_list,
    const char *options, cl_uint num_input_programs,
    const cl_program *input_programs,
    void(CL_CALLBACK *pfn_notify)(cl_program program, void *user_data),
    void *user_data, cl_int *errcode_ret);

CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompilerIntelFPGA(cl_platform_id platform);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMarkerWithWaitListIntelFPGA(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueBarrierWithWaitListIntelFPGA(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event);

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clSetKernelExecInfoIntelFPGA(cl_kernel kernel, cl_kernel_exec_info param_name,
                             size_t param_value_size, const void *param_value);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddressForPlatformIntelFPGA(cl_platform_id platform,
                                                  const char *func_name);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif

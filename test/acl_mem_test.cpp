// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <acl.h>
#include <acl_context.h>
#include <acl_hal.h>
#include <acl_mem.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>
#include <unref.h>

#include <cstdio>
#include <string.h>

#include "acl_hal_test.h"
#include "acl_test.h"

static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data);

// Validates the results of clEnqueueFillImage for different image formats. This
// also validates the calls to l_convert_image_format.
cl_int validate_fill_result(cl_context m_context, void *output_ptr,
                            void *fill_color, const size_t *image_size,
                            size_t *origin, size_t *region,
                            const cl_image_format image_from,
                            const cl_image_format image_to);

MT_TEST_GROUP(acl_mem) {
public:
  enum { MEM_SIZE = 1024, SUBBUF_SIZE = 256 };
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    this->load();
  }

  void teardown() {
    // sync threads here because some of this unloading code releases cl
    // objects and then checks the ref count is 0, which might not be true
    // if another thread hasn't started the test yet and it just about to
    // allocate a new cl object
    syncThreads();

    unload_host_and_device_bufs();
    unload_context();
    if (m_context) {
      clReleaseContext(m_context);
      m_context = 0;
    }

    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    m_hmem = m_dmem = 0;
    m_cq = 0;
    m_context = 0;

    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, ACL_MAX_DEVICE,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 3);

    load_normal_context();

    // Just grab devices that are present.
    CHECK(m_device[0]);
    CHECK(m_device[0]->present);
    CHECK(m_device[1]);
    CHECK(m_device[1]->present);
    CHECK(m_device[2]);
    CHECK(m_device[2]->present);
    m_num_devices = 3;

    load_command_queue();
    load_host_and_device_bufs();
  }

  void unload_context() {
    unload_command_queue();
    unload_host_and_device_bufs();
    if (m_context) {
      clReleaseContext(m_context);
      m_context = 0;
    }
    CHECK_EQUAL(0, m_context);
  }
  void load_normal_context(void) {
    unload_context();

    cl_int status = CL_INVALID_DEVICE;
    CHECK_EQUAL(0, m_context);
    m_context = clCreateContext(acl_test_context_prop_preloaded_binary_only(),
                                m_num_devices, &(m_device[0]), notify_me_print,
                                this, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));
  }

  void load_backing_store_context(void) {
    unload_context();
    cl_context_properties props[] = {
        CL_CONTEXT_COMPILER_MODE_INTELFPGA,
        CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY_INTELFPGA, 0};
    cl_int status = CL_INVALID_VALUE;
    CHECK_EQUAL(0, m_context);
    m_context = clCreateContext(props, m_num_devices, &(m_device[0]),
                                notify_me_print, this, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

    load_command_queue();
    load_host_and_device_bufs();
  }

  void unload_command_queue(void) {
    if (m_cq) {
      clReleaseCommandQueue(m_cq);
      m_cq = 0;
    }
  }
  void load_command_queue(void) {
    unload_command_queue();
    CHECK(m_num_devices > 0);
    cl_int status = CL_INVALID_DEVICE;
    m_cq = clCreateCommandQueue(m_context, m_device[0],
                                CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq)));
  }

  void unload_host_and_device_bufs(void) {
    if (m_hmem) {
      clReleaseMemObject(m_hmem);
      m_hmem = 0;
    }
    if (m_dmem) {
      clReleaseMemObject(m_dmem);
      m_dmem = 0;
    }
  }

  void load_host_and_device_bufs(void) {
    unload_host_and_device_bufs();

    cl_mem dmem;
    cl_int status = CL_INVALID_VALUE;
    dmem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 1024, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(dmem != 0);
    m_dmem = dmem;

    cl_mem hmem;
    status = CL_INVALID_VALUE;
    hmem =
        clCreateBuffer(m_context, (CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR),
                       1024, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(hmem != 0);
    m_hmem = hmem;
  }

  void check_event_perfcounters(cl_event event) {
    cl_ulong time[4];
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(time[0]), &time[0], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(time[1]), &time[1], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                        sizeof(time[2]), &time[2], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                        sizeof(time[3]), &time[3], 0));
    CHECK(0 < time[0]);
    CHECK(time[0] < time[1]);
    CHECK(time[1] < time[2]);
    CHECK(time[2] < time[3]);
  }

protected:
  cl_platform_id m_platform;
  cl_context m_context;
  cl_uint m_num_devices;
  cl_device_id m_device[ACL_MAX_DEVICE];
  cl_command_queue m_cq;
  cl_mem m_dmem; // device buffer
  cl_mem m_hmem; // host buffer
public:
  bool yeah;
};

static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data) {
  CppUTestGroupacl_mem *inst = (CppUTestGroupacl_mem *)user_data;
  if (inst) {
    if (inst->yeah)
      printf("Context error: %s\n", errinfo);
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST(acl_mem, create_buffer_align) {
  ACL_LOCKED(acl_print_debug_msg("begin create_buffer_align\n"));
  const int align_req = ACL_MEM_ALIGN;
  this->yeah = true;
  for (unsigned i = 1; i <= 128; i++) {
    cl_int status = CL_INVALID_VALUE;
    cl_mem mem =
        clCreateBuffer(m_context, CL_MEM_ALLOC_HOST_PTR, (size_t)i, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    uintptr_t addr = (uintptr_t)mem->block_allocation->range.begin;
    CHECK_EQUAL(0, addr & (align_req - 1));
    clReleaseMemObject(mem);
  }
  ACL_LOCKED(acl_print_debug_msg("end create_buffer_align\n"));
}

MT_TEST(acl_mem, create_image_align) {
  ACL_LOCKED(acl_print_debug_msg("begin create_image_align\n"));
  const int align_req = ACL_MEM_ALIGN;
  this->yeah = true;
  for (unsigned i = 1; i <= 128; i++) {
    cl_int status = CL_INVALID_VALUE;
    cl_image_format image_format = {CL_R, CL_FLOAT};
    cl_image_desc image_desc = {CL_MEM_OBJECT_IMAGE3D,
                                (size_t)i,
                                (size_t)i,
                                (size_t)i,
                                0,
                                0,
                                0,
                                0,
                                0,
                                {NULL}};

    cl_mem image =
        clCreateImage2D(m_context, CL_MEM_ALLOC_HOST_PTR, &image_format,
                        (size_t)i, (size_t)i, 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(image);
    uintptr_t addr = (uintptr_t)image->block_allocation->range.begin;
    CHECK_EQUAL(0, addr & (align_req - 1));
    clReleaseMemObject(image);

    image = clCreateImage3D(m_context, CL_MEM_ALLOC_HOST_PTR, &image_format,
                            (size_t)i, (size_t)i, (size_t)i, 0, 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(image);
    addr = (uintptr_t)image->block_allocation->range.begin;
    CHECK_EQUAL(0, addr & (align_req - 1));
    clReleaseMemObject(image);

    image = clCreateImage(m_context, CL_MEM_ALLOC_HOST_PTR, &image_format,
                          &image_desc, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    addr = (uintptr_t)image->block_allocation->range.begin;
    CHECK_EQUAL(0, addr & (align_req - 1));
    clReleaseMemObject(image);
  }
  ACL_LOCKED(acl_print_debug_msg("end create_image_align\n"));
  syncThreads();
}

MT_TEST(acl_mem, link) {
  size_t origin[3] = {0, 0, 0};
  size_t region[3] = {0, 0, 0};
  ACL_LOCKED(acl_print_debug_msg("begin link\n"));
  clCreateBuffer(0, 0, 0, 0, 0);
  clEnqueueReadBuffer(0, 0, CL_FALSE, 0, 0, 0, 0, 0, 0);
  clEnqueueReadBufferRect(0, 0, CL_FALSE, origin, origin, region, 0, 0, 0, 0, 0,
                          0, 0, 0);
  clEnqueueWriteBuffer(0, 0, CL_FALSE, 0, 0, 0, 0, 0, 0);
  clEnqueueWriteBufferRect(0, 0, CL_FALSE, origin, origin, region, 0, 0, 0, 0,
                           0, 0, 0, 0);
  clEnqueueCopyBuffer(0, 0, 0, 0, 0, 0, 0, 0, 0);
  clEnqueueCopyBufferRect(0, 0, 0, origin, origin, region, 0, 0, 0, 0, 0, 0, 0);
  clRetainMemObject(0);
  clReleaseMemObject(0);
  clCreateImage(0, 0, 0, 0, 0, 0);
  clCreateImage2D(0, 0, 0, 0, 0, 0, 0, 0);
  clCreateImage3D(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  clCreatePipe(0, 0, 0, 0, 0, 0);
  clGetSupportedImageFormats(0, 0, 0, 0, 0, 0);
  clEnqueueReadImage(0, 0, 0, origin, region, 0, 0, 0, 0, 0, 0);
  clEnqueueWriteImage(0, 0, 0, origin, region, 0, 0, 0, 0, 0, 0);
  clEnqueueCopyImage(0, 0, 0, 0, origin, region, 0, 0, 0);
  clEnqueueCopyImageToBuffer(0, 0, 0, origin, region, 0, 0, 0, 0);
  clEnqueueCopyBufferToImage(0, 0, 0, 0, origin, region, 0, 0, 0);
  clEnqueueMapBuffer(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  clEnqueueMapImage(0, 0, 0, 0, origin, region, 0, 0, 0, 0, 0, 0);
  clEnqueueUnmapMemObject(0, 0, 0, 0, 0, 0);
  clGetMemObjectInfo(0, 0, 0, 0, 0);
  clGetImageInfo(0, 0, 0, 0, 0);
  clGetPipeInfo(0, 0, 0, 0, 0);
  clCreateSubBuffer(0, 0, 0, 0, 0);
  ACL_LOCKED(acl_print_debug_msg("end link\n"));
}

MT_TEST(acl_mem, create_buffer) {
  ACL_LOCKED(acl_print_debug_msg("begin create_buffer\n"));
  cl_int status;
  cl_mem mem;
  char str[1024];

  cl_mem_flags bad_bits =
      (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
       CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR)
      << 1;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Bad context
  status = CL_SUCCESS;
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  mem = clCreateBuffer(0, CL_MEM_READ_WRITE, 1, 0, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  mem = clCreateBuffer(&fake_context, CL_MEM_READ_WRITE, 1, 0, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // bad size
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_BUFFER_SIZE, status);
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 0xffffffff, 0, &status);
  CHECK_EQUAL(CL_INVALID_BUFFER_SIZE, status);
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE,
                       (size_t)m_context->max_mem_alloc_size + 1, 0, &status);
  CHECK_EQUAL(CL_INVALID_BUFFER_SIZE, status);

  // need host ptr
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, 8, 0,
                       &status);
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, 8,
                       0, &status);
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);
  mem = clCreateBuffer(
      m_context, CL_MEM_READ_WRITE, 8, &str[0],
      &status); // need copy_host_ptr or use_host_ptr if spec host_ptr
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);

  // Bad bits
  mem = clCreateBuffer(
      m_context, bad_bits, 8, &str[0],
      &status); // need copy_host_ptr or use_host_ptr if spec host_ptr
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad bits: conflicts around host_ptr use
  mem = clCreateBuffer(m_context, CL_MEM_ALLOC_HOST_PTR | CL_MEM_USE_HOST_PTR,
                       8, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  mem = clCreateBuffer(m_context, CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR, 8,
                       &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad bits: conflicts around r/w specs
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY, 8,
                       &str[0], &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY, 8,
                       &str[0], &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 8,
                       &str[0], &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // If no read/write flags are set, default to CL_MEM_READ_WRITE.  OpenCL 1.1
  // v39. Bug 6643
  mem = clCreateBuffer(m_context, 0, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(mem->flags, CL_MEM_READ_WRITE);
  clReleaseMemObject(mem);
  // Check that we only set the one bit.  Other flags pass through.
  mem = clCreateBuffer(m_context, 0 | CL_MEM_ALLOC_HOST_PTR, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(mem->flags, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR);
  clReleaseMemObject(mem);

  // Valid r/w bits
  cl_mem_flags valid_flags[] = {CL_MEM_READ_WRITE, CL_MEM_READ_ONLY,
                                CL_MEM_WRITE_ONLY};
  for (size_t i = 0; i < sizeof(valid_flags) / sizeof(valid_flags[0]); i++) {
    cl_ulong request_size = 8 * (i + 1);
    mem = clCreateBuffer(m_context, valid_flags[i], (size_t)request_size, 0,
                         &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));

    // Check getinfo
    cl_mem_object_type type;
    cl_mem_flags the_flags;
    size_t size;
    void *host_ptr = (void *)0x12345678;
    cl_uint mapcnt = 0xdeadbeef;
    cl_uint refcnt = 0xdeadbeef;
    cl_context the_context;
    size_t size_ret;

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(mem, CL_MEM_TYPE, sizeof(type),
                                               &type, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_object_type), size_ret);
    CHECK_EQUAL(CL_MEM_OBJECT_BUFFER, type);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_FLAGS, sizeof(the_flags),
                                   &the_flags, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_flags), size_ret);
    CHECK_EQUAL(valid_flags[i], the_flags);

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(mem, CL_MEM_SIZE, sizeof(size),
                                               &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(size, request_size);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_HOST_PTR, sizeof(host_ptr),
                                   &host_ptr, &size_ret));
    CHECK_EQUAL(sizeof(host_ptr), size_ret);
    CHECK_EQUAL(0, host_ptr);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(0, mapcnt);
    CHECK_EQUAL(mem->mapping_count, mapcnt);
    // check mappings count mod and reading
    mem->mapping_count++;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(1, mapcnt);
    CHECK_EQUAL(mem->mapping_count, mapcnt);
    mem->mapping_count--;

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                   &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(refcnt), size_ret);
    CHECK_EQUAL(1, refcnt);
    CHECK_EQUAL(acl_ref_count(mem), refcnt);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(mem, CL_MEM_CONTEXT, sizeof(the_context),
                                   &the_context, &size_ret));
    CHECK_EQUAL(sizeof(the_context), size_ret);
    CHECK_EQUAL(m_context, the_context);

    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  }

  // Check boundary of memory allocation
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE,
                       (size_t)m_context->max_mem_alloc_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));

  // Check invalid getinfo
  cl_context the_context;
  size_t size_ret;
  mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem = {0};
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetMemObjectInfo(0, CL_MEM_CONTEXT, sizeof(the_context),
                                 &the_context, &size_ret));
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetMemObjectInfo(&fake_mem, CL_MEM_CONTEXT, sizeof(the_context),
                                 &the_context, &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();
  CHECK_EQUAL(CL_INVALID_VALUE, clGetMemObjectInfo(mem, 0, sizeof(the_context),
                                                   &the_context, &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetMemObjectInfo(mem, 0, 1, &the_context,
                                 &size_ret)); // param size is too small
  // Can pass null param_value
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(mem, CL_MEM_CONTEXT, 0, 0, &size_ret));
  CHECK_EQUAL(sizeof(cl_context), size_ret);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));

  ACL_LOCKED(acl_print_debug_msg("end create_buffer\n"));
}

MT_TEST(acl_mem, create_image) {
  ACL_LOCKED(acl_print_debug_msg("begin create_image\n"));
  cl_int status;
  cl_mem image;
  char str[1024];
  size_t idevice;
  cl_image_format image_format = {CL_R, CL_FLOAT};
  cl_image_desc image_desc = {
      CL_MEM_OBJECT_IMAGE3D, 5, 6, 7, 0, 0, 0, 0, 0, {NULL}};

  cl_mem_flags bad_bits =
      (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
       CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR)
      << 1;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Bad context
  status = CL_SUCCESS;
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  image = clCreateImage(0, CL_MEM_READ_WRITE, &image_format, &image_desc, 0,
                        &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  image = clCreateImage(&fake_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Bad flags
  image = clCreateImage(
      m_context, bad_bits, &image_format, &image_desc, &str[0],
      &status); // need copy_host_ptr or use_host_ptr if spec host_ptr
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad flags: conflicts around host_ptr use
  image =
      clCreateImage(m_context, CL_MEM_ALLOC_HOST_PTR | CL_MEM_USE_HOST_PTR,
                    &image_format, &image_desc, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  image =
      clCreateImage(m_context, CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR,
                    &image_format, &image_desc, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad flags: conflicts around r/w specs
  image =
      clCreateImage(m_context, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY,
                    &image_format, &image_desc, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  image =
      clCreateImage(m_context, CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY,
                    &image_format, &image_desc, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  image =
      clCreateImage(m_context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY,
                    &image_format, &image_desc, &str[0], &status); // conflict
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // bad image format
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, NULL, &image_desc, 0,
                        &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, status);
  image_format.image_channel_order = 0;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, status);
  image_format.image_channel_order = CL_R;
  image_format.image_channel_data_type = 0;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, status);
  image_format.image_channel_data_type = CL_FLOAT;

  // bad size
  image_desc.image_width = 0;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_width = 5;
  image_desc.image_height = 0;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_height = 6;
  image_desc.image_depth = 0;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_depth = 7;
  image_desc.image_width = 0;
  for (idevice = 0; idevice < m_num_devices; ++idevice) {
    size_t width;
    clGetDeviceInfo(m_device[idevice], CL_DEVICE_IMAGE3D_MAX_WIDTH,
                    sizeof(size_t), &width, NULL);
    if (width + 1 > image_desc.image_width) {
      image_desc.image_width = width + 1;
    }
  }
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_width = 5;
  image_desc.image_height = 0;
  for (idevice = 0; idevice < m_num_devices; ++idevice) {
    size_t height;
    clGetDeviceInfo(m_device[idevice], CL_DEVICE_IMAGE3D_MAX_HEIGHT,
                    sizeof(size_t), &height, NULL);
    if (height + 1 > image_desc.image_height) {
      image_desc.image_height = height + 1;
    }
  }
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_height = 6;
  image_desc.image_depth = 0;
  for (idevice = 0; idevice < m_num_devices; ++idevice) {
    size_t depth;
    clGetDeviceInfo(m_device[idevice], CL_DEVICE_IMAGE3D_MAX_DEPTH,
                    sizeof(size_t), &depth, NULL);
    if (depth + 1 > image_desc.image_depth) {
      image_desc.image_depth = depth + 1;
    }
  }
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_IMAGE_SIZE, status);
  image_desc.image_depth = 7;

  // need host ptr
  image = clCreateImage(m_context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR,
                        &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);
  image = clCreateImage(m_context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                        &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);
  image = clCreateImage(
      m_context, CL_MEM_READ_WRITE, &image_format, &image_desc, &str[0],
      &status); // need copy_host_ptr or use_host_ptr if spec host_ptr
  CHECK_EQUAL(CL_INVALID_HOST_PTR, status);

  // Image format not supported. Are all formats supported?

  // If no read/write flags are set, default to CL_MEM_READ_WRITE.
  image = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(image->flags, CL_MEM_READ_WRITE);
  clReleaseMemObject(image);
  // Check that we only set the one bit.  Other flags pass through.
  image = clCreateImage(m_context, CL_MEM_ALLOC_HOST_PTR, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(image->flags, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR);
  clReleaseMemObject(image);

  // Valid r/w bits
  cl_mem_flags valid_flags[] = {CL_MEM_READ_WRITE, CL_MEM_READ_ONLY,
                                CL_MEM_WRITE_ONLY};
  for (size_t i = 0; i < sizeof(valid_flags) / sizeof(valid_flags[0]); i++) {
    image = clCreateImage(m_context, valid_flags[i], &image_format, &image_desc,
                          0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(image)));

    // Check getinfo
    cl_mem_object_type type;
    cl_mem_flags the_flags;
    size_t size;
    void *host_ptr = (void *)0x12345678;
    cl_uint mapcnt = 0xdeadbeef;
    cl_uint refcnt = 0xdeadbeef;
    cl_context the_context;
    cl_image_format the_image_format;
    size_t size_ret;

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(image, CL_MEM_TYPE, sizeof(type),
                                               &type, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_object_type), size_ret);
    CHECK_EQUAL(CL_MEM_OBJECT_IMAGE3D, type);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_FLAGS, sizeof(the_flags),
                                   &the_flags, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_flags), size_ret);
    CHECK_EQUAL(valid_flags[i], the_flags);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_HOST_PTR, sizeof(host_ptr),
                                   &host_ptr, &size_ret));
    CHECK_EQUAL(sizeof(host_ptr), size_ret);
    CHECK_EQUAL(0, host_ptr);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(0, mapcnt);
    CHECK_EQUAL(image->mapping_count, mapcnt);
    // check mappings count mod and reading
    image->mapping_count++;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(1, mapcnt);
    CHECK_EQUAL(image->mapping_count, mapcnt);
    image->mapping_count--;

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(refcnt), size_ret);
    CHECK_EQUAL(1, refcnt);
    CHECK_EQUAL(acl_ref_count(image), refcnt);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(image, CL_MEM_CONTEXT, sizeof(the_context),
                                   &the_context, &size_ret));
    CHECK_EQUAL(sizeof(the_context), size_ret);
    CHECK_EQUAL(m_context, the_context);

    CHECK_EQUAL(CL_SUCCESS,
                clGetImageInfo(image, CL_IMAGE_FORMAT, sizeof(the_image_format),
                               &the_image_format, &size_ret));
    CHECK_EQUAL(sizeof(the_image_format), size_ret);
    CHECK_EQUAL(CL_R, the_image_format.image_channel_order);
    CHECK_EQUAL(CL_FLOAT, the_image_format.image_channel_data_type);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_ELEMENT_SIZE,
                                           sizeof(size), &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(1 * 4, size);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_ROW_PITCH,
                                           sizeof(size), &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(0, size);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_SLICE_PITCH,
                                           sizeof(size), &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(0, size);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_WIDTH, sizeof(size),
                                           &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(5, size);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_HEIGHT, sizeof(size),
                                           &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(6, size);

    CHECK_EQUAL(CL_SUCCESS, clGetImageInfo(image, CL_IMAGE_DEPTH, sizeof(size),
                                           &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(7, size);

    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image));
  }

  // Check invalid getinfo
  cl_image_format the_image_format;
  size_t size_ret;
  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem = {0};
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetImageInfo(0, CL_IMAGE_FORMAT, sizeof(the_image_format),
                             &the_image_format, &size_ret));
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetImageInfo(&fake_mem, CL_IMAGE_FORMAT,
                             sizeof(the_image_format), &the_image_format,
                             &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetImageInfo(image, 0, sizeof(the_image_format),
                             &the_image_format, &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetImageInfo(image, CL_IMAGE_FORMAT, 1, &the_image_format,
                             &size_ret)); // param size is too small
  // Can pass null param_value
  CHECK_EQUAL(CL_SUCCESS,
              clGetImageInfo(image, CL_IMAGE_FORMAT, 0, 0, &size_ret));
  CHECK_EQUAL(sizeof(cl_context), size_ret);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image));

  ACL_LOCKED(acl_print_debug_msg("end create_image\n"));
}

MT_TEST(acl_mem, create_pipe) {
  ACL_LOCKED(acl_print_debug_msg("begin create_pipe\n"));
  cl_int status;
  cl_mem pipe;
  cl_mem_flags bad_bits =
      (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
       CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR)
      << 1;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Bad context
  status = CL_SUCCESS;
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  pipe = clCreatePipe(0, CL_MEM_READ_WRITE, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  pipe = clCreatePipe(&fake_context, CL_MEM_READ_WRITE, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Bad flags
  pipe = clCreatePipe(m_context, bad_bits, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad flags: conflicts around host_ptr use
  pipe = clCreatePipe(m_context, CL_MEM_ALLOC_HOST_PTR, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  pipe = clCreatePipe(m_context, CL_MEM_USE_HOST_PTR, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  pipe = clCreatePipe(m_context, CL_MEM_COPY_HOST_PTR, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad flags: conflicts around r/w specs
  pipe = clCreatePipe(m_context, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY, 5, 10,
                      NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  pipe = clCreatePipe(m_context, CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY, 5, 10,
                      NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  pipe = clCreatePipe(m_context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 5, 10,
                      NULL, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Invalid pipe size
  pipe = clCreatePipe(m_context, CL_MEM_READ_WRITE, 0, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_PIPE_SIZE, status);
  pipe = clCreatePipe(m_context, CL_MEM_READ_WRITE, 3000, 10, NULL, &status);
  CHECK_EQUAL(CL_INVALID_PIPE_SIZE, status);

  // If no read/write flags are set, default to CL_MEM_READ_WRITE.
  pipe = clCreatePipe(m_context, 0, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(pipe->flags, CL_MEM_READ_WRITE);
  clReleaseMemObject(pipe);

  // Valid r/w bits
  cl_mem_flags valid_flags[] = {CL_MEM_READ_WRITE, CL_MEM_READ_ONLY,
                                CL_MEM_WRITE_ONLY};
  for (size_t i = 0; i < sizeof(valid_flags) / sizeof(valid_flags[0]); i++) {
    pipe = clCreatePipe(m_context, valid_flags[i], 5, 10, NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(pipe)));

    // Check getinfo
    cl_mem_object_type type;
    cl_mem_flags the_flags;
    void *host_ptr = (void *)0x12345678;
    cl_uint mapcnt = 0xdeadbeef;
    cl_uint refcnt = 0xdeadbeef;
    cl_context the_context;
    size_t size_ret;
    cl_uint the_pipe_packet_size;
    cl_uint the_pipe_max_packets;

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(pipe, CL_MEM_TYPE, sizeof(type),
                                               &type, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_object_type), size_ret);
    CHECK_EQUAL(CL_MEM_OBJECT_PIPE, type);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_FLAGS, sizeof(the_flags),
                                   &the_flags, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_flags), size_ret);
    CHECK_EQUAL(valid_flags[i], the_flags);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_HOST_PTR, sizeof(host_ptr),
                                   &host_ptr, &size_ret));
    CHECK_EQUAL(sizeof(host_ptr), size_ret);
    CHECK_EQUAL(0, host_ptr);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(0, mapcnt);
    CHECK_EQUAL(pipe->mapping_count, mapcnt);
    // check mappings count mod and reading
    pipe->mapping_count++;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(1, mapcnt);
    CHECK_EQUAL(pipe->mapping_count, mapcnt);
    pipe->mapping_count--;

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                   &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(refcnt), size_ret);
    CHECK_EQUAL(1, refcnt);
    CHECK_EQUAL(acl_ref_count(pipe), refcnt);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(pipe, CL_MEM_CONTEXT, sizeof(the_context),
                                   &the_context, &size_ret));
    CHECK_EQUAL(sizeof(the_context), size_ret);
    CHECK_EQUAL(m_context, the_context);

    CHECK_EQUAL(CL_SUCCESS, clGetPipeInfo(pipe, CL_PIPE_PACKET_SIZE,
                                          sizeof(the_pipe_packet_size),
                                          &the_pipe_packet_size, &size_ret));
    CHECK_EQUAL(sizeof(the_pipe_packet_size), size_ret);
    CHECK_EQUAL(5, the_pipe_packet_size);

    CHECK_EQUAL(CL_SUCCESS, clGetPipeInfo(pipe, CL_PIPE_MAX_PACKETS,
                                          sizeof(the_pipe_max_packets),
                                          &the_pipe_max_packets, &size_ret));
    CHECK_EQUAL(sizeof(the_pipe_max_packets), size_ret);
    CHECK_EQUAL(10, the_pipe_max_packets);

    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(pipe));
  }

  // Check invalid getinfo
  cl_uint the_pipe_packet_size;
  size_t size_ret;
  pipe = clCreatePipe(m_context, CL_MEM_READ_WRITE, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem = {0};
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetPipeInfo(0, CL_PIPE_PACKET_SIZE,
                            sizeof(the_pipe_packet_size), &the_pipe_packet_size,
                            &size_ret));
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clGetPipeInfo(&fake_mem, CL_PIPE_PACKET_SIZE,
                            sizeof(the_pipe_packet_size), &the_pipe_packet_size,
                            &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPipeInfo(pipe, 0, sizeof(the_pipe_packet_size),
                            &the_pipe_packet_size, &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPipeInfo(pipe, CL_PIPE_PACKET_SIZE, 1, &the_pipe_packet_size,
                            &size_ret)); // param size is too small
  // Can pass null param_value
  CHECK_EQUAL(CL_SUCCESS,
              clGetPipeInfo(pipe, CL_PIPE_PACKET_SIZE, 0, 0, &size_ret));
  CHECK_EQUAL(sizeof(the_pipe_packet_size), size_ret);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(pipe));

  ACL_LOCKED(acl_print_debug_msg("end create_pipe\n"));
}

MT_TEST(acl_mem, create_subbuffer) {
  ACL_LOCKED(acl_print_debug_msg("begin create_subbuffer\n"));
  cl_int status;
  cl_mem parent, subbuffer;
  size_t subbuffer_size = 2;
  cl_buffer_region test_region = {0, subbuffer_size};
  cl_ulong request_size = ACL_MEM_ALIGN * 3;
  cl_uint refcnt = 0xdeadbeef;
  size_t offset = (size_t)-1;
  size_t size_ret;

  cl_mem_flags bad_bits =
      (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
       CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR)
      << 1;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  CHECK_EQUAL(7, acl_ref_count(m_context));
  status = CL_SUCCESS;

  // Bad parent
  parent = clCreateBuffer(m_context, CL_MEM_READ_WRITE, (size_t)request_size, 0,
                          &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(parent != NULL);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem = {0};
  subbuffer =
      clCreateSubBuffer(0, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  subbuffer =
      clCreateSubBuffer(&fake_mem, CL_MEM_READ_WRITE,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();
  CHECK_EQUAL(1, acl_ref_count(parent));
  clReleaseMemObject(parent);
  CHECK_EQUAL(7, acl_ref_count(m_context));

  // Invalid value
  parent = clCreateBuffer(m_context, CL_MEM_WRITE_ONLY, (size_t)request_size, 0,
                          &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(parent != NULL);
  syncThreads(); // make sure threads use different cl_mem objects
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_ONLY, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  CHECK_EQUAL(1, acl_ref_count(parent));
  clReleaseMemObject(parent);

  parent = clCreateBuffer(m_context, CL_MEM_READ_ONLY, (size_t)request_size, 0,
                          &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(parent != NULL);
  syncThreads(); // make sure threads use different cl_mem objects
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_WRITE_ONLY, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  CHECK_EQUAL(1, acl_ref_count(parent));
  clReleaseMemObject(parent);

  parent = clCreateBuffer(m_context, CL_MEM_READ_WRITE, (size_t)request_size, 0,
                          &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(parent != NULL);
  syncThreads(); // make sure threads use different cl_mem objects
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_USE_HOST_PTR,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_ALLOC_HOST_PTR,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_COPY_HOST_PTR,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  CHECK_EQUAL(1, acl_ref_count(parent));
  clReleaseMemObject(parent);

  parent = clCreateBuffer(m_context, CL_MEM_READ_WRITE, (size_t)request_size, 0,
                          &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(parent != NULL);
  syncThreads(); // make sure threads use different cl_mem objects

  // Invalid type
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, 0, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  test_region.origin = 0;
  subbuffer = clCreateSubBuffer(parent, CL_MEM_READ_WRITE,
                                CL_BUFFER_CREATE_TYPE_REGION + 1, &test_region,
                                &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  test_region.origin = 0;

  // Invalid region
  test_region.origin = (size_t)request_size + ACL_MEM_ALIGN;
  test_region.size = 1;
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  test_region.origin = 0;
  test_region.size = (size_t)request_size + 1;
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  test_region.origin = 0;
  test_region.size = 0;
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_INVALID_BUFFER_SIZE, status);

  test_region.origin = 1;
  test_region.size = subbuffer_size;
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_MISALIGNED_SUB_BUFFER_OFFSET, status);

  subbuffer = clCreateSubBuffer(parent, CL_MEM_READ_WRITE,
                                CL_BUFFER_CREATE_TYPE_REGION, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  test_region.origin = 0;
  test_region.size = subbuffer_size;

  // Bad bits
  subbuffer = clCreateSubBuffer(parent, bad_bits, CL_BUFFER_CREATE_TYPE_REGION,
                                &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad bits: Cannot use host pointer flags
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_ALLOC_HOST_PTR | CL_MEM_USE_HOST_PTR,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Bad bits: conflicts around r/w specs
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY,
                        CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // If no read/write flags are set, default to parent flags.
  subbuffer = clCreateSubBuffer(parent, 0, CL_BUFFER_CREATE_TYPE_REGION,
                                &test_region, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(parent->flags, subbuffer->flags);
  clReleaseMemObject(subbuffer);

  // Valid r/w bits
  cl_mem_flags valid_flags[] = {CL_MEM_READ_WRITE, CL_MEM_READ_ONLY,
                                CL_MEM_WRITE_ONLY};
  for (size_t i = 0; i < sizeof(valid_flags) / sizeof(valid_flags[0]); i++) {
    CHECK_EQUAL(1, acl_ref_count(parent));
    subbuffer =
        clCreateSubBuffer(parent, valid_flags[i], CL_BUFFER_CREATE_TYPE_REGION,
                          &test_region, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(subbuffer)));
    // Check that the parent reference count increases by one
    CHECK_EQUAL(2, acl_ref_count(parent));

    // Check getinfo
    cl_mem_object_type type;
    cl_mem_flags the_flags;
    size_t size;
    void *host_ptr = (void *)0x12345678;
    cl_uint mapcnt = 0xdeadbeef;
    cl_context the_context;

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(subbuffer, CL_MEM_TYPE,
                                               sizeof(type), &type, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_object_type), size_ret);
    CHECK_EQUAL(CL_MEM_OBJECT_BUFFER, type);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_FLAGS, sizeof(the_flags),
                                   &the_flags, &size_ret));
    CHECK_EQUAL(sizeof(cl_mem_flags), size_ret);
    CHECK_EQUAL(valid_flags[i], the_flags);

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(subbuffer, CL_MEM_SIZE,
                                               sizeof(size), &size, &size_ret));
    CHECK_EQUAL(sizeof(size), size_ret);
    CHECK_EQUAL(size, subbuffer_size);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_HOST_PTR, sizeof(host_ptr),
                                   &host_ptr, &size_ret));
    CHECK_EQUAL(sizeof(host_ptr), size_ret);
    CHECK_EQUAL(0, host_ptr);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(0, mapcnt);
    CHECK_EQUAL(subbuffer->mapping_count, mapcnt);
    // check mappings count mod and reading
    subbuffer->mapping_count++;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                   &mapcnt, &size_ret));
    CHECK_EQUAL(sizeof(mapcnt), size_ret);
    CHECK_EQUAL(1, mapcnt);
    CHECK_EQUAL(subbuffer->mapping_count, mapcnt);
    subbuffer->mapping_count--;

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(refcnt), size_ret);
    CHECK_EQUAL(1, refcnt);
    CHECK_EQUAL(acl_ref_count(subbuffer), refcnt);
    // The parent buffer should only have one reference count, even though it
    // has internal references from sub buffers
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(parent, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(refcnt), size_ret);
    CHECK_EQUAL(1, refcnt);
    CHECK_EQUAL(acl_ref_count(parent) - 1, refcnt);

    CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(subbuffer, CL_MEM_CONTEXT,
                                               sizeof(the_context),
                                               &the_context, &size_ret));
    CHECK_EQUAL(sizeof(the_context), size_ret);
    CHECK_EQUAL(m_context, the_context);

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(parent, CL_MEM_OFFSET, sizeof(offset),
                                   &offset, &size_ret));
    CHECK_EQUAL(sizeof(offset), size_ret);
    CHECK_EQUAL(0, offset);
    offset = 1111;

    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(subbuffer, CL_MEM_OFFSET, sizeof(offset),
                                   &offset, &size_ret));
    CHECK_EQUAL(sizeof(offset), size_ret);
    CHECK_EQUAL(0, offset);

    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(subbuffer));
  }

  test_region.origin = ACL_MEM_ALIGN;
  subbuffer =
      clCreateSubBuffer(parent, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(parent, CL_MEM_OFFSET, sizeof(offset), &offset,
                                 &size_ret));
  CHECK_EQUAL(sizeof(offset), size_ret);
  CHECK_EQUAL(0, offset);
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(subbuffer, CL_MEM_OFFSET, sizeof(offset),
                                 &offset, &size_ret));
  CHECK_EQUAL(sizeof(offset), size_ret);
  CHECK_EQUAL(ACL_MEM_ALIGN, offset);

  CHECK_EQUAL(9, acl_ref_count(m_context));
  // Make sure I can't completely release a parent buffer while a subbuffer is
  // active
  status = clReleaseMemObject(parent);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(acl_ref_count(parent) != 0);
  status = clReleaseMemObject(parent);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  CHECK(acl_ref_count(parent) != 0);
  status = clReleaseMemObject(parent);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  CHECK(acl_ref_count(parent) != 0);
  status = clReleaseMemObject(parent);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  CHECK(acl_ref_count(parent) != 0);

  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(subbuffer));
  CHECK_EQUAL(7, acl_ref_count(m_context));
  ACL_LOCKED(acl_print_debug_msg("end create_subbuffer\n"));
}

TEST(acl_mem, exhaustion) {
  ACL_LOCKED(acl_print_debug_msg("begin exhaustion\n"));
  // Test for exhausting memory, and for block overlap, and leaks on
  // internal data structures.
  //
  // The amount of memory is limited both by the total heap space in the
  // region (e.g. global memory), as well as the number of cl_mem objects
  // statically allocated in the platform data structure.
  // When request sizes are small, the latter is the limiting factor.

  cl_int status;
  const int max_mems = 1000000;
  const int num_trials =
      3; // run this test a few times times to look for cl_mem leaks.
  int num_mems[num_trials];
  static cl_mem mem[max_mems];

  cl_mem_properties_intel props[] = {CL_MEM_CHANNEL_INTEL, 2, 0};

  // Simple exhaustion across dimms - assumes 2 banks exist
  cl_ulong total = m_context->max_mem_alloc_size;
  ACL_LOCKED(acl_print_debug_msg(" max alloc size %8lx\n", total));
  cl_ulong sofar1 = 0;
  cl_ulong sofar2 = 0;
  mem[0] = clCreateBufferWithPropertiesINTEL(
      m_context, props, 0, (size_t)m_context->max_mem_alloc_size / 4, 0,
      &status);
  sofar2 += m_context->max_mem_alloc_size / 4;
  ACL_LOCKED(acl_print_debug_msg(" sofar1 %8lx sofar2 %8lx\n",
                                 (unsigned long)(sofar1),
                                 (unsigned long)(sofar2)));
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[0]), 1));

  props[1] = 1;
  mem[1] = clCreateBufferWithPropertiesINTEL(
      m_context, props, 0, (size_t)m_context->max_mem_alloc_size / 4, 0,
      &status);
  sofar1 += m_context->max_mem_alloc_size / 4;
  ACL_LOCKED(acl_print_debug_msg(" sofar1 %8lx sofar2 %8lx\n",
                                 (unsigned long)(sofar1),
                                 (unsigned long)(sofar2)));
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[1]), 1));

  mem[2] = clCreateBuffer(
      m_context, 0, (size_t)m_context->max_mem_alloc_size / 2, 0, &status);
  CHECK_EQUAL(
      CL_SUCCESS,
      status); // Buffer allocation should always succeed (allocated deferred)
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[2]),
                         0)); // BUT the physical binding should fail
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[2]));

  props[1] = 2;
  mem[2] = clCreateBufferWithPropertiesINTEL(
      m_context, props, 0, (size_t)m_context->max_mem_alloc_size / 4, 0,
      &status);
  sofar2 += m_context->max_mem_alloc_size / 4;
  ACL_LOCKED(acl_print_debug_msg(" sofar1 %8lx sofar2 %8lx\n",
                                 (unsigned long)(sofar1),
                                 (unsigned long)(sofar2)));
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[2]), 1));
  CHECK((mem[2]->block_allocation->range).begin >
        (mem[1]->block_allocation->range).begin);
  CHECK((mem[2]->block_allocation->range).begin >
        (mem[0]->block_allocation->range).begin);

  props[1] = 2;
  mem[3] = clCreateBufferWithProperties(
      m_context, props, 0, (size_t)m_context->max_mem_alloc_size / 4, 0,
      &status);
  sofar2 += m_context->max_mem_alloc_size / 4;
  ACL_LOCKED(acl_print_debug_msg(" sofar1 %8lx sofar2 %8lx\n",
                                 (unsigned long)(sofar1),
                                 (unsigned long)(sofar2)));
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[3]), 1));

  props[1] = 2;
  // the size of dimm#2 is m_context->max_mem_alloc_size/2
  // mem[1] and mem[3] already exhausted dimm#2
  mem[4] = clCreateBufferWithProperties(m_context, props, 0, 128, 0, &status);
  CHECK_EQUAL(
      CL_SUCCESS,
      status); // Buffer allocation should always succeed (allocation deferred)
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, mem[4]),
                         0)); // BUT the physical bind should fail

  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[0]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[1]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[2]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[3]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[4]));

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  for (size_t request_size = 65536; request_size < (size_t)8e6;
       request_size *= 2) {
    for (int trial = 0; trial < num_trials; trial++) {
      num_mems[trial] = 0;
      // Allocate as many blocks as we can.
      for (int i = 0; i < max_mems; i++) {
        cl_mem block = clCreateBuffer(m_context, CL_MEM_READ_WRITE,
                                      (size_t)request_size, 0, &status);
        int bind_success;
        ACL_LOCKED(bind_success =
                       acl_bind_buffer_to_device(m_cq->device, block));
        if (bind_success) {
          ACL_LOCKED(CHECK(acl_mem_is_valid(block)));
          mem[num_mems[trial]] = block;
          num_mems[trial]++;
        } else {
          if (status == CL_SUCCESS) {
            CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(block));
          }
          break;
        }
      }

      // Check non-overlap using internal data structure.
      for (int i = 0; i < num_mems[trial]; i++) {
        for (int j = 0; j < i; j++) {
          const acl_addr_range_t *left = &(mem[i]->block_allocation->range);
          const acl_addr_range_t *right = &(mem[j]->block_allocation->range);
          CHECK((char *)left->next - (char *)left->begin >
                0); // valid range, intrinsically
          CHECK((char *)right->next - (char *)right->begin >
                0); // valid range, intrinsically
          // Either left comes before right, or vice versa
          CHECK((char *)right->begin - (char *)left->next >= 0 ||
                (char *)left->begin - (char *)right->next >= 0);
        }
      }

      if (trial > 0) {
        // Should have been able to allocate same number of blocks as last
        // time.
        // That's one sign of the absence of leaks.
        CHECK_EQUAL(num_mems[trial - 1], num_mems[trial]);
      }
      // Release them
      for (int i = 0; i < num_mems[trial]; i++) {
        CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem[i]));
      }
    }
  }
  ACL_LOCKED(acl_print_debug_msg("end exhaustion\n"));
}

MT_TEST(acl_mem, read_write_buf) {
  ACL_LOCKED(acl_print_debug_msg("begin read_write_buf\n"));
  char str[100];
  const size_t strsize = sizeof(str) / sizeof(char); // includes NUL (!)
  char resultbuf[strsize];
  cl_int status;

  // The two interesting cases are:
  cl_mem_flags flags[] = {
      CL_MEM_READ_WRITE, // memory is in device global space: requires DMA
      CL_MEM_READ_WRITE |
          CL_MEM_ALLOC_HOST_PTR, // memory is on host: no DMA used
      CL_MEM_READ_WRITE |
          CL_MEM_COPY_HOST_PTR // automatically copy from host side into device.
  };

  // Don't translate device addresses in the test HAL because we already
  // will be passing in "device" memory that are actually in the host
  // address space of the test executable. Ugh.
  acltest_hal_emulate_device_mem = false;

  for (size_t trial = 0; trial < 3; trial++) {
    // On trial 0, we don't request an event to wait on.
    // On trial 1, we request event to wait on, and check performance counters.
    cl_event write_event = 0;
    cl_event copy_event = 0;
    cl_event read_event = 0;

    for (size_t flagstrial1 = 0; flagstrial1 < sizeof(flags) / sizeof(flags[0]);
         flagstrial1++) {
      for (size_t flagstrial2 = 0;
           flagstrial2 < sizeof(flags) / sizeof(flags[0]); flagstrial2++) {
        cl_mem_flags flag1 = flags[flagstrial1];
        cl_mem_flags flag2 = flags[flagstrial2];
        resultbuf[0] = 0;

        sprintf(str, "deadbeef.%d.%d.%d", int(trial), int(flagstrial1),
                int(flagstrial2));

        ACL_LOCKED(
            acl_print_debug_msg("\n\nBEGIN trial %d flagstrial (%d,%d)\n",
                                trial, flagstrial1, flagstrial2));
        ACL_LOCKED(acl_print_debug_msg(" result0 is '%s'\n", resultbuf));

        // Surround clCreateBuffer calls in syncThreads() to ensure each
        // thread is using it's own cl_mem object (otherwise one thread could
        // use an object and then when it's finished the next would take it).
        // The problem with that is that later we release the mem object and
        // we check that after that it's no longer valid, which might not be
        // true if another thread started using the object right away.
        syncThreads();

        cl_mem mem1 = clCreateBuffer(
            m_context, flag1, strsize,
            (void *)((flag1 & CL_MEM_COPY_HOST_PTR) ? str : 0), &status);
        CHECK_EQUAL(CL_SUCCESS, status);
        cl_mem mem2 = clCreateBuffer(
            m_context, flag2, strsize,
            (void *)((flag2 & CL_MEM_COPY_HOST_PTR) ? str : 0), &status);
        CHECK_EQUAL(CL_SUCCESS, status);
        ACL_LOCKED(CHECK(acl_mem_is_valid(mem1)));
        ACL_LOCKED(CHECK(acl_mem_is_valid(mem2)));

        syncThreads();

        ACL_LOCKED(acl_print_debug_msg(" result1 is '%s'\n", resultbuf));

        ACL_LOCKED(acl_print_debug_msg("Mem 1 is: \n"));
        ACL_LOCKED(acl_dump_mem(mem1));
        ACL_LOCKED(acl_print_debug_msg("Mem 2 is: \n"));
        ACL_LOCKED(acl_dump_mem(mem2));

        CHECK_EQUAL(1, acl_ref_count(mem1));
        CHECK_EQUAL(1, acl_ref_count(mem2));

        switch (trial) {
        case 1:
          // Do queued (nonblocking) write, copy, read.
          // And use events to chain them because copy requires it.
          status = clEnqueueWriteBuffer(m_cq, mem1, 0, 0, strsize, str, 0, 0,
                                        &write_event);
          CHECK_EQUAL(CL_SUCCESS, status);
          ACL_LOCKED(acl_dump_mem(mem1));
          status = clEnqueueCopyBuffer(m_cq, mem1, mem2, 0, 0, strsize, 1,
                                       &write_event, &copy_event);
          CHECK_EQUAL(0, mem1->allocation_deferred);
          CHECK_EQUAL(0, mem2->allocation_deferred);
          CHECK_EQUAL(CL_SUCCESS, status);
          ACL_LOCKED(acl_dump_mem(mem1));
          status = clEnqueueReadBuffer(m_cq, mem2, 0, 0, strsize, resultbuf, 1,
                                       &copy_event, &read_event);
          CHECK_EQUAL(CL_SUCCESS, status);
          ACL_LOCKED(acl_dump_mem(mem2));
          break;

        case 0:
          // Just use blocking read and write.  No copy
          status = clEnqueueWriteBuffer(m_cq, mem1, CL_TRUE, 0, strsize, str, 0,
                                        0, 0);
          CHECK_EQUAL(CL_SUCCESS, status);
          ACL_LOCKED(acl_dump_mem(mem1));
          status = clEnqueueReadBuffer(m_cq, mem1, CL_TRUE, 0, strsize,
                                       resultbuf, 0, 0, 0);
          ACL_LOCKED(acl_dump_mem(mem1));
          CHECK_EQUAL(CL_SUCCESS, status);
          break;

        case 2:
          if (!(flag1 & CL_MEM_COPY_HOST_PTR)) {
            // Need to copy the string to mem1 space on the device.
            ACL_LOCKED(acl_print_debug_msg("Enqueue write\n"));
            status = clEnqueueWriteBuffer(m_cq, mem1, CL_TRUE, 0, strsize, str,
                                          0, 0, 0);
            CHECK_EQUAL(CL_SUCCESS, status);
          } else {
            // If COPY_HOST_PTR is specified, then the data is already
            // in mem1, as part of create-buffer operation.
            ACL_LOCKED(acl_print_debug_msg(
                " result7 data '%s'\n", mem1->block_allocation->range.begin));
          }
          ACL_LOCKED(acl_print_debug_msg(" result2 is '%s'\n", resultbuf));
          ACL_LOCKED(acl_print_debug_msg("Enqueue read\n"));
          status = clEnqueueReadBuffer(m_cq, mem1, CL_TRUE, 0, strsize,
                                       resultbuf, 0, 0, 0);
          ACL_LOCKED(acl_print_debug_msg(" result3 is '%s'\n", resultbuf));
          ACL_LOCKED(acl_dump_mem(mem1));
          CHECK_EQUAL(CL_SUCCESS, status);
          break;

          break;
        }

        switch (trial) {
        case 1:
          ACL_LOCKED(CHECK(acl_event_is_valid(write_event)));
          ACL_LOCKED(CHECK(acl_event_is_valid(read_event)));
          ACL_LOCKED(acl_print_debug_msg(
              "trial %d flagstrial (%d,%d) write event %d perf check\n", trial,
              flagstrial1, flagstrial2, write_event->id));
          this->check_event_perfcounters(write_event);
          ACL_LOCKED(acl_print_debug_msg(
              "trial %d flagstrial (%d,%d) copy event %d perf check\n", trial,
              flagstrial1, flagstrial2, copy_event->id));
          this->check_event_perfcounters(copy_event);
          ACL_LOCKED(acl_print_debug_msg(
              "trial %d flagstrial (%d,%d) read event %d perf check\n", trial,
              flagstrial1, flagstrial2, read_event->id));
          this->check_event_perfcounters(read_event);
          CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(write_event));
          CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(copy_event));
          CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(read_event));
          CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
          ACL_LOCKED(CHECK(!acl_event_is_valid(write_event)));
          ACL_LOCKED(CHECK(!acl_event_is_valid(read_event)));

        case 0:
        case 2:
          ACL_LOCKED(acl_print_debug_msg("Skip event checks\n"));
          // No event checks required.
          break;

        default:
          break;
        }
        // Check contents of the moved-around string.
        ACL_LOCKED(acl_print_debug_msg(" result8 is '%s' (should be %s)\n",
                                       resultbuf, str));
        CHECK_EQUAL(0, strncmp(str, resultbuf, strsize - 1));
        ACL_LOCKED(acl_print_debug_msg(" result9 is '%s'\n", resultbuf));

        ACL_LOCKED(acl_bind_buffer_to_device(m_cq->device, mem1));
        ACL_LOCKED(acl_bind_buffer_to_device(m_cq->device, mem2));
        // Poison the data for next round of tests.
        void *dev_addr1 = acltest_translate_device_address(
            mem1->block_allocation->range.begin, 0);
        *((char *)dev_addr1) = 0;
        void *dev_addr2 = acltest_translate_device_address(
            mem2->block_allocation->range.begin, 0);
        *((char *)dev_addr2) = 0;

        CHECK_EQUAL(1, acl_ref_count(mem1));
        CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem1));
        CHECK_EQUAL(1, acl_ref_count(mem2));
        CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem2));
      }
    }
  }

  ACL_LOCKED(acl_print_debug_msg("end read_write_buf\n"));
}

MT_TEST(acl_mem, read_write_buf_rect) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin read_write_buf_rect\n"));
  cl_int status;

  cl_event write_event_1 = 0;
  cl_event write_event_2 = 0;
  cl_event write_event_rect = 0;
  cl_event copy_event_rect = 0;
  cl_event read_event_rect = 0;
  cl_event read_event_1 = 0;
  cl_event read_event_2 = 0;
  int input[8 * 8 * 8];
  int output[8 * 8 * 8];
  int check_val_1[8 * 8 * 8];
  int check_val_2[8 * 8 * 8];
  size_t row_pitch = 8 * sizeof(int);
  size_t slice_pitch = 8 * row_pitch;
  size_t src_offset[3];
  size_t dst_offset[3];
  size_t region[3];

  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) {
      for (int k = 0; k < 8; ++k) {
        input[k * 64 + j * 8 + i] = (k * 8 * 8 + j * 8 + i); // * (i % 2?1:-1);
        check_val_1[k * 64 + j * 8 + i] = input[k * 64 + j * 8 + i];
        check_val_2[k * 64 + j * 8 + i] = input[k * 64 + j * 8 + i];
      }
    }
  }

  cl_mem mem1 =
      clCreateBuffer(m_context, 0, 8 * 8 * 8 * sizeof(int), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem mem2 =
      clCreateBuffer(m_context, 0, 8 * 8 * 8 * sizeof(int), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(mem1)));
  ACL_LOCKED(CHECK(acl_mem_is_valid(mem2)));

  CHECK_EQUAL(1, acl_ref_count(mem1));
  CHECK_EQUAL(1, acl_ref_count(mem2));

  // Invalid command queue
  struct _cl_command_queue fake_cq = {0};
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(0, mem1, 0, dst_offset, src_offset, region,
                                    row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueWriteBufferRect(&fake_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueCopyBufferRect(0, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueCopyBufferRect(&fake_cq, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueReadBufferRect(0, mem2, 0, src_offset, dst_offset, region,
                                   row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueReadBufferRect(&fake_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid mem object
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_cl_mem = {0};
  status = clEnqueueWriteBufferRect(m_cq, 0, 0, dst_offset, src_offset, region,
                                    row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueWriteBufferRect(
      m_cq, &fake_cl_mem, 0, dst_offset, src_offset, region, row_pitch,
      slice_pitch, row_pitch, slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueCopyBufferRect(m_cq, 0, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueCopyBufferRect(m_cq, &fake_cl_mem, mem2, src_offset,
                                   dst_offset, region, row_pitch, slice_pitch,
                                   row_pitch, slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, &fake_cl_mem, src_offset,
                                   dst_offset, region, row_pitch, slice_pitch,
                                   row_pitch, slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueReadBufferRect(m_cq, 0, 0, src_offset, dst_offset, region,
                                   row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  status = clEnqueueReadBufferRect(
      m_cq, &fake_cl_mem, 0, src_offset, dst_offset, region, row_pitch,
      slice_pitch, row_pitch, slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();

  // Out of bounds
  src_offset[0] = 6 * sizeof(int);
  src_offset[1] = 6;
  src_offset[2] = 6;
  dst_offset[0] = 6 * sizeof(int);
  dst_offset[1] = 6;
  dst_offset[2] = 6;
  region[0] = 8 * sizeof(int);
  region[1] = 8;
  region[2] = 8;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // NULL ptr
  src_offset[0] = 0 * sizeof(int);
  src_offset[1] = 0;
  src_offset[2] = 0;
  dst_offset[0] = 0 * sizeof(int);
  dst_offset[1] = 0;
  dst_offset[2] = 0;
  region[0] = 1 * sizeof(int);
  region[1] = 1;
  region[2] = 1;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Region element is zero
  region[0] = 0;
  region[1] = 1;
  region[2] = 1;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  region[0] = 1;
  region[1] = 0;
  region[2] = 1;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  region[0] = 1;
  region[1] = 1;
  region[2] = 0;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueCopyBufferRect(m_cq, mem1, mem2, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Row pitch < region[0]
  region[0] = 4;
  region[1] = 4;
  region[2] = 4;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, region[0] - 1, slice_pitch,
      row_pitch, slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(
      m_cq, mem2, 0, src_offset, dst_offset, region, region[0] - 1, slice_pitch,
      row_pitch, slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, row_pitch, slice_pitch,
      region[0] - 1, slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(
      m_cq, mem2, 0, src_offset, dst_offset, region, row_pitch, slice_pitch,
      region[0] - 1, slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Slice pitch < region[1] * row pitch
  region[0] = 4;
  region[1] = 4;
  region[2] = 4;
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, row_pitch,
      region[1] * row_pitch - 1, row_pitch, slice_pitch, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(
      m_cq, mem2, 0, src_offset, dst_offset, region, row_pitch,
      region[1] * row_pitch - 1, row_pitch, slice_pitch, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, row_pitch, slice_pitch,
      row_pitch, region[1] * row_pitch - 1, input, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueReadBufferRect(
      m_cq, mem2, 0, src_offset, dst_offset, region, row_pitch, slice_pitch,
      row_pitch, region[1] * row_pitch - 1, output, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Invalid event wait list
  status = CL_SUCCESS;
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, row_pitch, slice_pitch,
      row_pitch, slice_pitch, input, 0, &write_event_2, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 0, &read_event_2, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueWriteBufferRect(m_cq, mem1, 0, dst_offset, src_offset,
                                    region, row_pitch, slice_pitch, row_pitch,
                                    slice_pitch, input, 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueReadBufferRect(m_cq, mem2, 0, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, output, 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Overlapping copy
  src_offset[0] = 0 * sizeof(int);
  src_offset[1] = 0;
  src_offset[2] = 0;
  dst_offset[0] = 0 * sizeof(int);
  dst_offset[1] = 0;
  dst_offset[2] = 0;
  region[0] = 1 * sizeof(int);
  region[1] = 1;
  region[2] = 1;
  status = CL_SUCCESS;
  status = clEnqueueCopyBufferRect(m_cq, mem1, mem1, src_offset, dst_offset,
                                   region, row_pitch, slice_pitch, row_pitch,
                                   slice_pitch, 0, NULL, NULL);
  CHECK_EQUAL(CL_MEM_COPY_OVERLAP, status);

  // Write entire input buffer
  status = clEnqueueWriteBuffer(m_cq, mem1, 0, 0, 8 * 8 * 8 * sizeof(int),
                                input, 0, 0, &write_event_1);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clEnqueueWriteBuffer(m_cq, mem2, 0, 0, 8 * 8 * 8 * sizeof(int),
                                input, 1, &write_event_1, &write_event_2);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(acl_print_debug_msg("Mem 1 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem1));
  ACL_LOCKED(acl_print_debug_msg("Mem 2 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem2));

  // Write 4x3x2 section from offset 1,2,3 to offset 2,3,4
  src_offset[0] = 1 * sizeof(int);
  src_offset[1] = 2;
  src_offset[2] = 3;
  dst_offset[0] = 2 * sizeof(int);
  dst_offset[1] = 3;
  dst_offset[2] = 4;
  region[0] = 4 * sizeof(int);
  region[1] = 3;
  region[2] = 2;
  status = clEnqueueWriteBufferRect(
      m_cq, mem1, 0, dst_offset, src_offset, region, row_pitch, slice_pitch,
      row_pitch, slice_pitch, input, 1, &write_event_2, &write_event_rect);
  CHECK_EQUAL(CL_SUCCESS, status);
  for (int i = 2; i < 2 + 4; ++i) {
    for (int j = 3; j < 3 + 3; ++j) {
      for (int k = 4; k < 4 + 2; ++k) {
        check_val_1[k * 64 + j * 8 + i] =
            input[(k - 1) * 64 + (j - 1) * 8 + (i - 1)];
      }
    }
  }
  // Copy 4x3x2 section from offset 3,4,5 to 4,5,6
  src_offset[0] = 3 * sizeof(int);
  src_offset[1] = 4;
  src_offset[2] = 5;
  dst_offset[0] = 4 * sizeof(int);
  dst_offset[1] = 5;
  dst_offset[2] = 6;
  region[0] = 4 * sizeof(int);
  region[1] = 3;
  region[2] = 2;
  status = clEnqueueCopyBufferRect(
      m_cq, mem1, mem2, src_offset, dst_offset, region, row_pitch, slice_pitch,
      row_pitch, slice_pitch, 1, &write_event_rect, &copy_event_rect);
  CHECK_EQUAL(CL_SUCCESS, status);
  for (int i = 4; i < 4 + 4; ++i) {
    for (int j = 5; j < 5 + 3; ++j) {
      for (int k = 6; k < 6 + 2; ++k) {
        check_val_2[k * 64 + j * 8 + i] =
            check_val_1[(k - 1) * 64 + (j - 1) * 8 + (i - 1)];
      }
    }
  }
  // Read all of buffer 1
  status = clEnqueueReadBuffer(m_cq, mem1, 0, 0, 8 * 8 * 8 * sizeof(int),
                               output, 1, &copy_event_rect, &read_event_1);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(acl_print_debug_msg("Mem 1 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem1));
  ACL_LOCKED(acl_print_debug_msg("Mem 2 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem2));

  // Verify that write to buffer 1 was correct
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) {
      for (int k = 0; k < 8; ++k) {
        if (check_val_1[k * 64 + j * 8 + i] != output[k * 64 + j * 8 + i]) {
          printf("Error: Buffer1 location i%d,j%d,k%d does not match expected "
                 "results\n",
                 i, j, k);
          CHECK_EQUAL(check_val_1[k * 64 + j * 8 + i],
                      output[k * 64 + j * 8 + i]);
        }
      }
    }
  }

  // Read all of buffer 2
  status = clEnqueueReadBuffer(m_cq, mem2, 0, 0, 8 * 8 * 8 * sizeof(int),
                               output, 1, &read_event_1, &read_event_2);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(acl_print_debug_msg("Mem 1 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem1));
  ACL_LOCKED(acl_print_debug_msg("Mem 2 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem2));

  // Verify that copy to buffer 2 was correct
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) {
      for (int k = 0; k < 8; ++k) {
        if (check_val_2[k * 64 + j * 8 + i] != output[k * 64 + j * 8 + i]) {
          printf("Error: Buffer2 location i%d,j%d,k%d does not match expected "
                 "results\n",
                 i, j, k);
          CHECK_EQUAL(check_val_2[k * 64 + j * 8 + i],
                      output[k * 64 + j * 8 + i]);
        }
      }
    }
  }

  // Read 4x3x2 section from 4,5,6 to 1,2,3
  src_offset[0] = 4 * sizeof(int);
  src_offset[1] = 5;
  src_offset[2] = 6;
  dst_offset[0] = 1 * sizeof(int);
  dst_offset[1] = 2;
  dst_offset[2] = 3;
  region[0] = 4 * sizeof(int);
  region[1] = 3;
  region[2] = 2;
  status = clEnqueueReadBufferRect(
      m_cq, mem2, 0, src_offset, dst_offset, region, row_pitch, slice_pitch,
      row_pitch, slice_pitch, output, 1, &read_event_2, &read_event_rect);
  CHECK_EQUAL(CL_SUCCESS, status);
  clFinish(m_cq);
  for (int i = 1; i < 1 + 4; ++i) {
    for (int j = 2; j < 2 + 3; ++j) {
      for (int k = 3; k < 3 + 2; ++k) {
        check_val_2[k * 64 + j * 8 + i] =
            check_val_2[(k + 3) * 64 + (j + 3) * 8 + (i + 3)];
      }
    }
  }

  // Verify that read from buffer 2 was correct
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) {
      for (int k = 0; k < 8; ++k) {
        if (check_val_2[k * 64 + j * 8 + i] != output[k * 64 + j * 8 + i]) {
          printf("Error: Output location i%d,j%d,k%d does not match expected "
                 "results\n",
                 i, j, k);
          CHECK_EQUAL(check_val_2[k * 64 + j * 8 + i],
                      output[k * 64 + j * 8 + i]);
        }
      }
    }
  }

  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(write_event_1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(write_event_2));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(write_event_rect));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(copy_event_rect));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(read_event_rect));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(read_event_1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(read_event_2));
  CHECK_EQUAL(1, acl_ref_count(mem1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem1));
  CHECK_EQUAL(1, acl_ref_count(mem2));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem2));

  ACL_LOCKED(acl_print_debug_msg("end read_write_buf_rect\n"));
}

MT_TEST(acl_mem, fill_buf) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin fill_buf\n"));
  char str[64];
  const size_t strsize = sizeof(str) / sizeof(char);
  char resultbuf[strsize + 1]; // includes NUL (!)
  cl_int status;

  // The two interesting cases are:
  cl_mem_flags flags[] = {
      CL_MEM_READ_WRITE, // memory is in device global space: requires DMA
      CL_MEM_READ_WRITE |
          CL_MEM_ALLOC_HOST_PTR, // memory is on host: no DMA used
      CL_MEM_READ_WRITE |
          CL_MEM_COPY_HOST_PTR // automatically copy from host side into device.
  };

  // Don't translate device addresses in the test HAL because we already
  // will be passing in "device" memory that are actually in the host
  // address space of the test executable.
  acltest_hal_emulate_device_mem = false;

  // On trial 0, we don't request an event to wait on.
  // On trial 1, we request event to wait on, and check performance counters.
  cl_event fill_event = 0;
  cl_event read_event = 0;

  for (size_t flagstrial1 = 0; flagstrial1 < sizeof(flags) / sizeof(flags[0]);
       flagstrial1++) {
    cl_mem_flags flag = flags[flagstrial1];
    resultbuf[0] = 0;
    resultbuf[strsize] = 0;

    sprintf(str, "deadbeef.%d.", int(flagstrial1));

    ACL_LOCKED(
        acl_print_debug_msg("\n\nBEGIN  flagstrial (%d)\n", flagstrial1));
    ACL_LOCKED(acl_print_debug_msg(" result0 is '%s'\n", resultbuf));

    // Surround clCreateBuffer calls in syncThreads() to ensure each
    // thread is using it's own cl_mem object (otherwise one thread could
    // use an object and then when it's finished the next would take it).
    // The problem with that is that later we release the mem object and
    // we check that after that it's no longer valid, which might not be
    // true if another thread started using the object right away.
    syncThreads();

    cl_mem mem = clCreateBuffer(
        m_context, flag, strsize,
        (void *)((flag & CL_MEM_COPY_HOST_PTR) ? str : 0), &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));

    syncThreads();

    ACL_LOCKED(acl_print_debug_msg(" result1 is '%s'\n", resultbuf));

    ACL_LOCKED(acl_dump_mem(mem));

    CHECK_EQUAL(1, acl_ref_count(mem));

    status = clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 0, strsize, 0, 0,
                                 &fill_event);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_event_is_valid(fill_event)));
    ACL_LOCKED(acl_dump_mem(mem));
    CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
    ACL_LOCKED(CHECK(acl_event_is_valid(fill_event)));

    status = clEnqueueReadBuffer(m_cq, mem, 0, 0, strsize, resultbuf, 1,
                                 &fill_event, &read_event);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(acl_dump_mem(mem));
    CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
    ACL_LOCKED(acl_print_debug_msg(" result is '%s'\n", resultbuf));
    CHECK_EQUAL(
        0,
        strncmp(
            resultbuf,
            "ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,",
            strsize));

    ACL_LOCKED(CHECK(acl_event_is_valid(fill_event)));
    ACL_LOCKED(CHECK(acl_event_is_valid(read_event)));
    ACL_LOCKED(
        acl_print_debug_msg("flagstrial (%d) write event %d perf check\n",
                            flagstrial1, fill_event->id));
    this->check_event_perfcounters(fill_event);
    ACL_LOCKED(acl_print_debug_msg("flagstrial (%d) read event %d perf check\n",
                                   flagstrial1, read_event->id));
    this->check_event_perfcounters(read_event);
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(fill_event));
    CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(read_event));
    CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
    ACL_LOCKED(CHECK(!acl_event_is_valid(fill_event)));
    ACL_LOCKED(CHECK(!acl_event_is_valid(read_event)));

    ACL_LOCKED(acl_bind_buffer_to_device(m_cq->device, mem));
    // Poison the data for next round of tests.
    void *dev_addr =
        acltest_translate_device_address(mem->block_allocation->range.begin, 0);
    *((char *)dev_addr) = 0;

    CHECK_EQUAL(1, acl_ref_count(mem));
    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  }

  // Bad calls:
  {
    syncThreads();
    cl_mem mem =
        clCreateBuffer(m_context, CL_MEM_READ_WRITE, strsize, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));

    syncThreads();
    CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
                clEnqueueFillBuffer(0, mem, "ABC,", 4, 0, strsize, 0, 0,
                                    NULL)); // bad command queue
    CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
                clEnqueueFillBuffer(m_cq, 0, "ABC,", 4, 0, strsize, 0, 0,
                                    NULL)); // bad mem object
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 4, strsize, 0, 0,
                                    NULL)); // offset+size > buffer size.
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillBuffer(m_cq, mem, NULL, 4, 0, strsize, 0, 0,
                                    NULL)); // NULL pattern
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillBuffer(m_cq, mem, "ABC,", 0, 0, strsize, 0, 0,
                                    NULL)); // pattern size =0.
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillBuffer(m_cq, mem, "ABC,", 3, 0, 6, 0, 0,
                                    NULL)); // pattern size not a power of 2.
    CHECK_EQUAL(
        CL_INVALID_VALUE,
        clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 1, 16, 0, 0,
                            NULL)); // offset not a multiple of pattern_size
    CHECK_EQUAL(
        CL_INVALID_VALUE,
        clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 0, 17, 0, 0,
                            NULL)); // size not a multiple of pattern_size
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillBuffer(
                    m_cq, mem, "ABC,", 4, 1, 17, 0, 0,
                    NULL)); // offset and size not a multiple of pattern_size
    CHECK_EQUAL(
        CL_INVALID_EVENT_WAIT_LIST,
        clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 0, strsize, 1, NULL, NULL));

    CHECK_EQUAL(
        CL_INVALID_EVENT_WAIT_LIST,
        clEnqueueFillBuffer(m_cq, mem, "ABC,", 4, 0, strsize, 1, NULL, NULL));

    CHECK_EQUAL(1, acl_ref_count(mem));
    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  }

  ACL_LOCKED(acl_print_debug_msg("end fill_buf\n"));
}

MT_TEST(acl_mem, read_write_image) {
  ACL_LOCKED(acl_print_debug_msg("begin read_write_image\n"));
  cl_int status;
  cl_mem image1, image2;
  cl_mem buffer1, buffer2;
  float *input_ptr, *output_ptr;
  size_t width = 5;
  size_t height = 6;
  size_t depth = 7;
  size_t origin[3];
  size_t region[3];

  // 2D Image
  cl_image_format image_format = {CL_R, CL_FLOAT};
  cl_image_desc image_desc = {
      CL_MEM_OBJECT_IMAGE2D, width, height, 1, 0, 0, 0, 0, 0, {NULL}};

  origin[0] = 0;
  origin[1] = 0;
  origin[2] = 0;

  region[0] = width;
  region[1] = height;
  region[2] = 1;

  input_ptr = (float *)acl_malloc(sizeof(float) * (height * width));
  CHECK(input_ptr != 0);
  output_ptr = (float *)acl_malloc(sizeof(float) * (height * width));
  CHECK(output_ptr != 0);

  image1 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  image2 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      input_ptr[col + row * width] = (float)col + (float)row + (float)1.5;
      output_ptr[col + row * width] = 0.0;
    }
  }

  // Write from pointer to image
  status = clEnqueueWriteImage(m_cq, image1, CL_TRUE, origin, region, 0, 0,
                               input_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy between 2 images
  status = clEnqueueCopyImage(m_cq, image1, image2, origin, origin, region, 0,
                              NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Read from image to pointer
  status = clEnqueueReadImage(m_cq, image2, CL_TRUE, origin, region, 0, 0,
                              output_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Compare contents of first pointer with second pointer
  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      CHECK(input_ptr[col + row * width] == output_ptr[col + row * width]);
    }
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image2));
  acl_free(input_ptr);
  acl_free(output_ptr);

  input_ptr = (float *)acl_malloc(sizeof(float) * (height * width));
  CHECK(input_ptr != 0);
  output_ptr = (float *)acl_malloc(sizeof(float) * (height * width));
  CHECK(output_ptr != 0);

  image1 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  buffer1 = clCreateBuffer(m_context, 0, sizeof(float) * (height * width), 0,
                           &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  buffer2 = clCreateBuffer(m_context, 0, sizeof(float) * (height * width), 0,
                           &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      input_ptr[col + row * width] = (float)col + (float)row + 2.7f;
      output_ptr[col + row * width] = 0;
    }
  }

  // Write to buffer
  status = clEnqueueWriteBuffer(m_cq, buffer1, CL_TRUE, 0,
                                sizeof(float) * (height * width), input_ptr, 0,
                                NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy from buffer to image
  status = clEnqueueCopyBufferToImage(m_cq, buffer1, image1, 0, origin, region,
                                      0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy from image to buffer
  status = clEnqueueCopyImageToBuffer(m_cq, image1, buffer2, origin, region, 0,
                                      0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Read from buffer
  status = clEnqueueReadBuffer(m_cq, buffer2, CL_TRUE, 0,
                               sizeof(float) * (height * width), output_ptr, 0,
                               NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Compare contents of first buffer with second buffer
  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      CHECK(input_ptr[col + row * width] == output_ptr[col + row * width]);
    }
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(buffer1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(buffer2));
  acl_free(input_ptr);
  acl_free(output_ptr);

  // 3D Image
  image_format.image_channel_order = CL_R;
  image_format.image_channel_data_type = CL_FLOAT;
  image_desc.image_type = CL_MEM_OBJECT_IMAGE3D;
  image_desc.image_width = width;
  image_desc.image_height = height;
  image_desc.image_depth = depth;
  image_desc.image_array_size = 0;
  image_desc.image_row_pitch = 0;
  image_desc.image_slice_pitch = 0;
  image_desc.num_mip_levels = 0;
  image_desc.num_samples = 0;
  image_desc.buffer = NULL;

  origin[0] = 0;
  origin[1] = 0;
  origin[2] = 0;

  region[0] = width;
  region[1] = height;
  region[2] = depth;

  input_ptr = (float *)acl_malloc(sizeof(float) * (height * width * depth));
  CHECK(input_ptr != 0);
  output_ptr = (float *)acl_malloc(sizeof(float) * (height * width * depth));
  CHECK(output_ptr != 0);

  image1 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  image2 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  for (size_t deep = 0; deep < depth; ++deep) {
    for (size_t row = 0; row < height; ++row) {
      for (size_t col = 0; col < width; ++col) {
        input_ptr[col + row * width + deep * width * height] =
            (float)col + (float)row + (float)deep + 1.5f;
        output_ptr[col + row * width + deep * width * height] = 0;
      }
    }
  }

  // Write from pointer to image
  status = clEnqueueWriteImage(m_cq, image1, CL_TRUE, origin, region, 0, 0,
                               input_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy between 2 images
  status = clEnqueueCopyImage(m_cq, image1, image2, origin, origin, region, 0,
                              NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Read from image to pointer
  status = clEnqueueReadImage(m_cq, image2, CL_TRUE, origin, region, 0, 0,
                              output_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Compare contents of first pointer with second pointer
  for (size_t deep = 0; deep < depth; ++deep) {
    for (size_t row = 0; row < height; ++row) {
      for (size_t col = 0; col < width; ++col) {
        CHECK(input_ptr[col + row * width + deep * width * height] ==
              output_ptr[col + row * width + deep * width * height]);
      }
    }
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image2));
  acl_free(input_ptr);
  acl_free(output_ptr);

  input_ptr = (float *)acl_malloc(sizeof(float) * (height * width * depth));
  CHECK(input_ptr != 0);
  output_ptr = (float *)acl_malloc(sizeof(float) * (height * width * depth));
  CHECK(output_ptr != 0);

  image1 = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  buffer1 = clCreateBuffer(
      m_context, 0, sizeof(float) * (height * width * depth), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  buffer2 = clCreateBuffer(
      m_context, 0, sizeof(float) * (height * width * depth), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  for (size_t deep = 0; deep < depth; ++deep) {
    for (size_t row = 0; row < height; ++row) {
      for (size_t col = 0; col < width; ++col) {
        input_ptr[col + row * width + deep * width * height] =
            (float)col + (float)row + (float)deep + 2.7f;
        output_ptr[col + row * width + deep * width * height] = 0;
      }
    }
  }

  // Write to buffer
  status = clEnqueueWriteBuffer(m_cq, buffer1, CL_TRUE, 0,
                                sizeof(float) * (height * width * depth),
                                input_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy from buffer to image
  status = clEnqueueCopyBufferToImage(m_cq, buffer1, image1, 0, origin, region,
                                      0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Copy from image to buffer
  status = clEnqueueCopyImageToBuffer(m_cq, image1, buffer2, origin, region, 0,
                                      0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Read from buffer
  status = clEnqueueReadBuffer(m_cq, buffer2, CL_TRUE, 0,
                               sizeof(float) * (height * width * depth),
                               output_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Compare contents of first buffer with second buffer
  for (size_t deep = 0; deep < depth; ++deep) {
    for (size_t row = 0; row < height; ++row) {
      for (size_t col = 0; col < width; ++col) {
        CHECK(input_ptr[col + row * width + deep * width * height] ==
              output_ptr[col + row * width + deep * width * height]);
      }
    }
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(buffer1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(buffer2));
  acl_free(input_ptr);
  acl_free(output_ptr);

  ACL_LOCKED(acl_print_debug_msg("end read_write_image\n"));
}

MT_TEST(acl_mem, fill_image) {
  ACL_LOCKED(acl_print_debug_msg("begin fill_image\n"));
  cl_int status;
  cl_mem image;
  char *output_ptr;

  float fill_color_float[] = {0.99f, 0.8f, 0.8f, 0.8f};
  cl_int fill_color_int[] = {1, -2, 3, -4};
  cl_uint fill_color_uint[] = {1, 2, 3, 4};

  // Data for different trials
  size_t width[] = {5, 4};
  size_t height[] = {6, 5};
  size_t depth[] = {1, 2};
  size_t origin[3];
  size_t region[3];
  size_t image_size[3];
  cl_mem_object_type object_types[] = {CL_MEM_OBJECT_IMAGE2D,
                                       CL_MEM_OBJECT_IMAGE3D};

  // Bad calls:
  {
    cl_event fill_event;
    cl_image_format test_image_format = {CL_RGBA, CL_SIGNED_INT32};
    cl_image_desc test_image_desc = {
        CL_MEM_OBJECT_IMAGE1D, 5, 1, 1, 0, 0, 0, 0, 0, {NULL}};
    origin[0] = 0;
    origin[1] = 0;
    origin[2] = 0;

    region[0] = 5;
    region[1] = 1;
    region[2] = 1;

    syncThreads();
    image = clCreateImage(m_context, 0, &test_image_format, &test_image_desc, 0,
                          &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    syncThreads();

    CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
                clEnqueueFillImage(NULL, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad command queue
    CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
                clEnqueueFillImage(m_cq, NULL, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad mem object
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, NULL, origin, region, 0, NULL,
                                   &fill_event)); // bad fill color.
    origin[0] = 100;                              // origin = {100,0,0}
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad region/origin
    origin[0] = 0;                                         // origin = {0,0,0}
    region[0] = region[1] = region[2] = 100; // region = {100,100,100}
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad region/origin
    region[0] = region[1] = region[2] = 0;                 // region = {0,0,0}
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad region/origin
    region[0] = region[2] = 1;                             // region = {1,0,1}
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad region/origin
    region[0] = region[1] = region[2] = 1;                 // region = {1,1,1}
    origin[1] = 1;                                         // origin = {0,1,0}
    CHECK_EQUAL(CL_INVALID_VALUE,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, NULL, &fill_event)); // bad region/origin
    origin[1] = 0;                                         // origin = {0,0,0}
    CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   1, NULL, &fill_event)); // invalid wait list
    CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST,
                clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                   0, &fill_event,
                                   &fill_event)); // invalid wait list

    CHECK_EQUAL(1, acl_ref_count(image));
    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image));
  }

  // Good calls
  for (int trial = 0; trial < 2; trial++) {
    cl_image_desc image_desc = {object_types[trial],
                                width[trial],
                                height[trial],
                                depth[trial],
                                0,
                                0,
                                0,
                                0,
                                0,
                                {NULL}};

    image_size[0] = width[trial];
    image_size[1] = height[trial];
    image_size[2] = depth[trial];

    origin[0] = 1;
    origin[1] = 1;
    origin[2] = 0;

    region[0] = width[trial] - 2;
    region[1] = height[trial] - 2;
    region[2] = depth[trial];

    cl_image_format image_formats[100];
    cl_uint num_image_formats = 0;
    size_t read_write_origin[] = {0, 0, 0};
    cl_image_format color_format;
    color_format.image_channel_order = CL_RGBA;

    CHECK_EQUAL(CL_SUCCESS, clGetSupportedImageFormatsIntelFPGA(
                                m_context, 0, object_types[trial], 100,
                                image_formats, &num_image_formats));

    output_ptr = (char *)acl_malloc(
        4 * sizeof(float) * height[trial] * width[trial] *
        depth[trial]); // max size needed for all possible image formats.
    CHECK(output_ptr != 0);

    cl_uint format_inx = 0;
    for (format_inx = 0; format_inx < num_image_formats; format_inx++) {
      cl_event fill_event = NULL;

      memset(output_ptr, 0,
             4 * sizeof(float) * (height[trial] * width[trial] * depth[trial]));

      syncThreads();
      image = clCreateImage(m_context, 0, &image_formats[format_inx],
                            &image_desc, 0, &status);
      CHECK_EQUAL(CL_SUCCESS, status);
      syncThreads();

      // Initialize all image pixels to zero.
      status = clEnqueueWriteImage(m_cq, image, CL_TRUE, read_write_origin,
                                   image_size, 0, 0, output_ptr, 0, NULL, NULL);
      CHECK_EQUAL(CL_SUCCESS, status);

      switch (image_formats[format_inx].image_channel_data_type) {
      case CL_SNORM_INT8:
      case CL_SNORM_INT16:
      case CL_UNORM_INT8:
      case CL_UNORM_INT16:
      case CL_UNORM_SHORT_565:
      case CL_UNORM_SHORT_555:
      case CL_UNORM_INT_101010:
      case CL_HALF_FLOAT:
      case CL_FLOAT:
        status = clEnqueueFillImage(m_cq, image, fill_color_float, origin,
                                    region, 0, NULL, &fill_event);
        CHECK_EQUAL(CL_SUCCESS, status);
        // Read from image to pointer
        status = clEnqueueReadImage(m_cq, image, CL_TRUE, read_write_origin,
                                    image_size, 0, 0, output_ptr, 1,
                                    &fill_event, NULL);
        color_format.image_channel_data_type = CL_FLOAT;
        CHECK_EQUAL(CL_SUCCESS, validate_fill_result(
                                    m_context, output_ptr, fill_color_float,
                                    image_size, origin, region, color_format,
                                    image_formats[format_inx]));
        break;
      case CL_SIGNED_INT8:
      case CL_SIGNED_INT16:
      case CL_SIGNED_INT32:
        status = clEnqueueFillImage(m_cq, image, fill_color_int, origin, region,
                                    0, NULL, &fill_event);
        CHECK_EQUAL(CL_SUCCESS, status);
        // Read from image to pointer
        status = clEnqueueReadImage(m_cq, image, CL_TRUE, read_write_origin,
                                    image_size, 0, 0, output_ptr, 1,
                                    &fill_event, NULL);
        color_format.image_channel_data_type = CL_SIGNED_INT32;
        CHECK_EQUAL(CL_SUCCESS, validate_fill_result(
                                    m_context, output_ptr, fill_color_int,
                                    image_size, origin, region, color_format,
                                    image_formats[format_inx]));
        break;
      case CL_UNSIGNED_INT8:
      case CL_UNSIGNED_INT16:
      case CL_UNSIGNED_INT32:
        status = clEnqueueFillImage(m_cq, image, fill_color_uint, origin,
                                    region, 0, NULL, &fill_event);
        CHECK_EQUAL(CL_SUCCESS, status);
        // Read from image to pointer
        status = clEnqueueReadImage(m_cq, image, CL_TRUE, read_write_origin,
                                    image_size, 0, 0, output_ptr, 1,
                                    &fill_event, NULL);
        color_format.image_channel_data_type = CL_UNSIGNED_INT32;
        CHECK_EQUAL(CL_SUCCESS, validate_fill_result(
                                    m_context, output_ptr, fill_color_uint,
                                    image_size, origin, region, color_format,
                                    image_formats[format_inx]));
        break;
      default:
        FAIL("Unexpected image channel data type.\n");
      }

      CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(image));
      ACL_LOCKED(CHECK(acl_event_is_valid(fill_event)));
      CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(fill_event));
      ACL_LOCKED(CHECK(!acl_event_is_valid(fill_event)));
    }
    acl_free(output_ptr);
  }

  ACL_LOCKED(acl_print_debug_msg("end read_write_image\n"));
}

MT_TEST(acl_mem, map_buf_bad_buf) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin map_buf_bad_buf\n"));
  cl_int status = CL_SUCCESS;

  CHECK_EQUAL(0, clEnqueueMapBuffer(0, 0, 0, 0, 0, 0, 0, 0, 0, &status));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueUnmapMemObject(0, 0, 0, 0, 0, 0));

  status = CL_SUCCESS;
  CHECK_EQUAL(0, clEnqueueMapBuffer(m_cq, 0, 0, 0, 0, 0, 0, 0, 0, &status));
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);

  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clEnqueueUnmapMemObject(m_cq, 0, 0, 0, 0, 0));

  ACL_LOCKED(acl_print_debug_msg("end map_buf_bad_buf\n"));
}

TEST(acl_mem, case_205751_overlapping_alloc) {
  ACL_LOCKED(acl_print_debug_msg("begin case_205751_overlapping_alloc\n"));
  // Test assumes a global memory space with two banks of size bank_size
  // Allocate a small buffer (a), then try to allocate two buffers (b, c) of
  // size bank_size.  Expect the second allocation to fail.
  cl_mem a, b, c;
  cl_int status = CL_SUCCESS;
  size_t total_size = ACL_RANGE_SIZE(
      m_device[0]->def.autodiscovery_def.global_mem_defs[0].range);
  size_t bank_size = total_size / 2;
  size_t small_size = bank_size / 1024;

  // This always passed
  a = clCreateBuffer(m_context, 0, small_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, a), 1));

  b = clCreateBuffer(m_context, 0, bank_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, b), 1));

  c = clCreateBuffer(m_context, 0, bank_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, c), 0));

  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(a));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(b));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(c));

  a = clCreateBuffer(m_context, 0, small_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, a), 1));

  b = clCreateBuffer(m_context, 0, bank_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, b), 1));

  cl_mem_properties_intel props[] = {CL_MEM_CHANNEL_INTEL, 2, 0};
  c = clCreateBufferWithPropertiesINTEL(m_context, props, 0, bank_size, 0,
                                        &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, c), 0));

  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(a));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(b));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(c));
}

TEST(acl_mem, buffer_location_property) {
  ACL_LOCKED(acl_print_debug_msg("begin buffer_location_property\n"));
  // Test assumes more than 1 global memory space
  // Allocate a small buffer (a), then try to allocate two buffers (b, c) of
  // size bank_size.  Expect the second allocation to fail.
  cl_mem a;
  cl_int status = CL_SUCCESS;
  size_t total_size = ACL_RANGE_SIZE(
      m_device[0]->def.autodiscovery_def.global_mem_defs[0].range);
  size_t bank_size = total_size / 2;

  cl_mem_properties_intel props[] = {CL_MEM_ALLOC_BUFFER_LOCATION_INTEL, 0, 0};
  a = clCreateBufferWithPropertiesINTEL(m_context, props, 0, bank_size, 0,
                                        &status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(a)));
  CHECK_EQUAL(CL_SUCCESS, status);
  assert(a);
  CHECK_EQUAL(1, acl_ref_count(a));
  cl_uint read_mem_id = 4;
  size_t size_ret;
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(a, CL_MEM_ALLOC_BUFFER_LOCATION_INTEL,
                                 sizeof(cl_uint), &read_mem_id, &size_ret));
  CHECK_EQUAL(0, read_mem_id);

  cl_buffer_region test_region = {0, 2};
  cl_mem subbuffer =
      clCreateSubBuffer(a, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                        &test_region, &status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(subbuffer)));
  CHECK_EQUAL(CL_SUCCESS, status);
  assert(subbuffer);
  CHECK_EQUAL(2, acl_ref_count(a));
  read_mem_id = 4;
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(subbuffer, CL_MEM_ALLOC_BUFFER_LOCATION_INTEL,
                                 sizeof(cl_uint), &read_mem_id, &size_ret));
  CHECK_EQUAL(0, read_mem_id);

  ACL_LOCKED(CHECK_EQUAL(acl_bind_buffer_to_device(m_cq->device, a), 1));

  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(subbuffer));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(a));
}

TEST(acl_mem, simulation_copy_buffer) {
  // Test mocks a simulation run where a predefined autodiscovery string
  // is loaded at the beginning of the run with default global memory
  // set-up that doesn't match actual. It checks whether the function
  // acl_realloc_buffer_for_simulator moves buffer to the right global
  // memory address range after a fake reprogram updates the global
  // memory configuration.
  cl_mem buffer;
  cl_int status = CL_SUCCESS;
  int input_data = 0xaaaaaaaa;
  int output_data = 0x55555555;
  size_t total_size = ACL_RANGE_SIZE(
      m_device[0]->def.autodiscovery_def.global_mem_defs[0].range);
  size_t global_mem_size = total_size / 2;

  // save original autodiscovery def
  acl_device_def_autodiscovery_t orig_def = m_device[0]->def.autodiscovery_def;
  // create a fake multi global memory system where unit test global
  // memory is split into 2 halves for the 2 global memories
  acl_device_def_autodiscovery_t actual_def =
      m_device[0]->def.autodiscovery_def;
  actual_def.num_global_mem_systems = 2;
  actual_def.global_mem_defs[1].range.next =
      actual_def.global_mem_defs[0].range.next;
  actual_def.global_mem_defs[0].range.next =
      (char *)actual_def.global_mem_defs[0].range.begin + global_mem_size;
  actual_def.global_mem_defs[1].range.begin =
      actual_def.global_mem_defs[0].range.next;

  // simulate loading from a predefined autodiscovery string in
  // acl_shipped_board_cfgs.h
  m_device[0]->def.autodiscovery_def.num_global_mem_systems =
      ACL_MAX_GLOBAL_MEM;
  for (int i = 0; i < ACL_MAX_GLOBAL_MEM; i++) {
    m_device[0]->def.autodiscovery_def.global_mem_defs[i] =
        actual_def.global_mem_defs[0];
  }

  // Create memory with buffer location property
  cl_mem_properties_intel props[] = {CL_MEM_ALLOC_BUFFER_LOCATION_INTEL, 1, 0};
  buffer = clCreateBufferWithPropertiesINTEL(m_context, props, 0, sizeof(int),
                                             0, &status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(buffer)));
  CHECK_EQUAL(CL_SUCCESS, status);
  assert(buffer);
  CHECK_EQUAL(1, acl_ref_count(buffer));

  // Check if the buffer has the right mem id
  cl_uint read_mem_id = 4; // set to a dummy value
  size_t size_ret;
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(buffer, CL_MEM_ALLOC_BUFFER_LOCATION_INTEL,
                                 sizeof(cl_uint), &read_mem_id, &size_ret));
  CHECK_EQUAL(1, read_mem_id);

  // Enqueue write binds buffer to wrong global memory address range
  status = clEnqueueWriteBuffer(m_cq, buffer, CL_TRUE, 0, sizeof(int),
                                &input_data, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(ACL_STRIP_PHYSICAL_ID(buffer->block_allocation->range.begin) >=
        m_device[0]
            ->def.autodiscovery_def.global_mem_defs[1]
            .get_usable_range()
            .begin);
  CHECK(ACL_STRIP_PHYSICAL_ID(buffer->block_allocation->range.next) <
        m_device[0]
            ->def.autodiscovery_def.global_mem_defs[1]
            .get_usable_range()
            .next);

  // Pretend a reprogram happened for simulation, update global memory info
  m_device[0]->def.autodiscovery_def = actual_def;
  CHECK_EQUAL(2, m_device[0]->def.autodiscovery_def.num_global_mem_systems);
  CHECK(m_device[0]
            ->def.autodiscovery_def.global_mem_defs[0]
            .get_usable_range()
            .begin != m_device[0]
                          ->def.autodiscovery_def.global_mem_defs[1]
                          .get_usable_range()
                          .begin);
  CHECK(m_device[0]
            ->def.autodiscovery_def.global_mem_defs[0]
            .get_usable_range()
            .next != m_device[0]
                         ->def.autodiscovery_def.global_mem_defs[1]
                         .get_usable_range()
                         .next);
  CHECK(ACL_STRIP_PHYSICAL_ID(buffer->block_allocation->range.begin) <
        m_device[0]
            ->def.autodiscovery_def.global_mem_defs[1]
            .get_usable_range()
            .begin);

  // Now call the migration function
  ACL_LOCKED(CHECK_EQUAL(acl_realloc_buffer_for_simulator(buffer, 0, 1), 1));
  CHECK(ACL_STRIP_PHYSICAL_ID(buffer->block_allocation->range.begin) >=
        m_device[0]
            ->def.autodiscovery_def.global_mem_defs[1]
            .get_usable_range()
            .begin);
  CHECK(ACL_STRIP_PHYSICAL_ID(buffer->block_allocation->range.next) <
        m_device[0]
            ->def.autodiscovery_def.global_mem_defs[1]
            .get_usable_range()
            .next);

  // Enqueue a blocking read to the right location and check data
  status = clEnqueueReadBuffer(m_cq, buffer, CL_TRUE, 0, sizeof(int),
                               &output_data, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Check data preservation
  CHECK_EQUAL(input_data, output_data);

  // restore and clean up
  m_device[0]->def.autodiscovery_def = orig_def;
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(buffer));
}

MT_TEST(acl_mem, map_buf_bad_flags) {
  ACL_LOCKED(acl_print_debug_msg("begin buf_bad_flags\n"));
  cl_int status = CL_SUCCESS;

  cl_map_flags valid_flags =
      CL_MAP_READ | CL_MAP_WRITE; // 1.2 adds CL_MAP_WRITE_INVALIDATE_REGION
  cl_map_flags invalid_flags = ~(valid_flags);

  // invalid flags
  status = CL_SUCCESS;
  CHECK(m_dmem != 0); // make sure we're testing the flags, not the buf argument
  CHECK_EQUAL(0, clEnqueueMapBuffer(m_cq, m_dmem, 0, invalid_flags, 0, 0, 0, 0,
                                    0, &status));
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  ACL_LOCKED(acl_print_debug_msg("end buf_bad_flags\n"));
}

MT_TEST(acl_mem, map_host_buf) {
  ACL_LOCKED(acl_print_debug_msg("begin map_host_buf\n"));
  cl_int status = CL_SUCCESS;
  size_t size_ret;
  cl_uint refcnt;

  CHECK(m_hmem != 0);

  cl_map_flags valid_flags =
      CL_MAP_READ | CL_MAP_WRITE; // 1.2 adds CL_MAP_WRITE_INVALIDATE_REGION

  // out of bounds cases. Use host mem hmem because that's all we can map with.
  // Subbuffer starts beyond end.
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clEnqueueMapBuffer(m_cq, m_hmem, 0, valid_flags, MEM_SIZE,
                                    SUBBUF_SIZE, 0, 0, 0, &status));
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  // Subbuffer starts before end, but goes beyond end.
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clEnqueueMapBuffer(m_cq, m_hmem, 0, valid_flags,
                                    MEM_SIZE - SUBBUF_SIZE, 2 * SUBBUF_SIZE, 0,
                                    0, 0, &status));
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Map success, because it's on host
  status = CL_MAP_FAILURE;
  cl_event e;
  void *mapped = clEnqueueMapBuffer(m_cq, m_hmem, 0, valid_flags, 0,
                                    SUBBUF_SIZE, 0, 0, &e, &status);
  CHECK(mapped != 0);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Check reference count
  refcnt = 0xdeadbeef;
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(m_hmem, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(m_hmem), refcnt);

  // Now unmap
  cl_event e2;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueUnmapMemObject(m_cq, m_hmem, mapped, 1, &e, &e2));
  clWaitForEvents(1, &e2);

  clReleaseEvent(e);
  clReleaseEvent(e2);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(m_hmem, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(m_hmem), refcnt);

  // Testing map on buffer with USE_HOST_POINTER
  cl_mem_flags flag1 = CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR;
  char str[100];
  char resultbuf[100];
  resultbuf[0] = 0;

  sprintf(str, "deadbeef.%d", int(1));

  // Surround clCreateBuffer calls in syncThreads() to ensure each
  // thread is using it's own cl_mem object (otherwise one thread could
  // use an object and then when it's finished the next would take it).
  // The problem with that is that later we release the mem object and
  // we check that after that it's no longer valid, which might not be
  // true if another thread started using the object right away.
  syncThreads();

  cl_mem mem1 = clCreateBuffer(m_context, flag1, sizeof(char) * sizeof(str),
                               str, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(mem1)));

  syncThreads();

  ACL_LOCKED(acl_print_debug_msg("Mem 1 is: \n"));
  ACL_LOCKED(acl_dump_mem(mem1));
  CHECK_EQUAL(1, acl_ref_count(mem1));

  // Map success, because it's on host
  status = CL_MAP_FAILURE;
  cl_event e3;
  char *mappedout = (char *)clEnqueueMapBuffer(m_cq, mem1, CL_TRUE, valid_flags,
                                               0, sizeof(char) * sizeof(str), 0,
                                               NULL, &e3, &status);
  CHECK(mappedout != 0);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK(mappedout);
  sprintf(mappedout, "deadbeef.%d", int(2));

  // Check reference count
  refcnt = 0xdeadbeef;
  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(mem1, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(mem1), refcnt);

  // Now unmap
  cl_event e4;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueUnmapMemObject(m_cq, mem1, mappedout, 0, NULL, &e4));
  clWaitForEvents(1, &e4);

  clReleaseEvent(e3);
  clReleaseEvent(e4);

  // Reading the result
  ACL_LOCKED(acl_print_debug_msg(" result1 is '%s'\n", resultbuf));
  ACL_LOCKED(acl_print_debug_msg("Enqueue read\n"));
  status = clEnqueueReadBuffer(m_cq, mem1, CL_TRUE, 0,
                               sizeof(char) * sizeof(str), resultbuf, 0, 0, 0);
  ACL_LOCKED(acl_print_debug_msg(" result2 is '%s'\n", resultbuf));
  ACL_LOCKED(acl_dump_mem(mem1));
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(mem1, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(mem1), refcnt);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem1));

  ACL_LOCKED(acl_print_debug_msg("end map_host_buf\n"));
}

MT_TEST(acl_mem, map_dev_buf_with_backing_store) {
  ACL_LOCKED(acl_print_debug_msg("begin map_dev_buf_with_backing_store\n"));
  size_t size_ret;
  cl_uint refcnt;
  cl_int status;
  // 1.2 adds CL_MAP_WRITE_INVALIDATE_REGION
  cl_map_flags flags[] = {CL_MAP_READ | CL_MAP_WRITE, CL_MAP_READ,
                          CL_MAP_WRITE};

  load_backing_store_context();
  ACL_LOCKED(CHECK(m_context->device_buffers_have_backing_store));

  // Don't translate device addresses in the test HAL because we already
  // will be passing in "device" memory that are actually in the host
  // address space of the test executable. Ugh.
  acltest_hal_emulate_device_mem = false;
  // For simplicity, make sure backing store is created over the range of
  // addresses we touch.
  (void)acltest_translate_device_address(m_dmem->block_allocation->range.begin,
                                         16 * 1024 * 1024);

  for (unsigned iflag = 0; iflag < sizeof(flags) / sizeof(flags[0]); iflag++) {

    // Make sure m_dmem is as we think it is.
    // 1. reference count
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK_EQUAL(1, refcnt);
    // 2. map count
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_MAP_COUNT, sizeof(refcnt),
                                   &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK(m_dmem->mapping_count == 0);
    CHECK_EQUAL(0, refcnt);

    // Set initial content.
    CHECK(m_cq);
    ACL_LOCKED(acl_bind_buffer_to_device(m_cq->device, m_dmem));
    void *dev_addr = acltest_translate_device_address(
        m_dmem->block_allocation->range.begin, 0);
    ACL_LOCKED(acl_print_debug_msg(" dev_addr %p\n", dev_addr));
    *((int *)dev_addr) = 0x90210fbf;
    ACL_LOCKED(
        acl_print_debug_msg(" dev_addr %p %x\n", dev_addr, *(int *)dev_addr));

    ACL_LOCKED(acl_print_debug_msg("\n\n about to   map pass %d flags %d\n",
                                   iflag, (int)flags[iflag]));
    ACL_LOCKED(acl_print_debug_msg("\n\n  mem is %p\n", m_dmem));
    ACL_LOCKED(acl_print_debug_msg("\n\n  mem region is %p\n",
                                   m_dmem->block_allocation->region));

    // Now map it with the given flags.
    status = CL_INVALID_VALUE;
    cl_event e;
    void *mapped_dmem = clEnqueueMapBuffer(m_cq, m_dmem, 0, flags[iflag], 0,
                                           SUBBUF_SIZE, 0, 0, &e, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    ACL_LOCKED(acl_print_debug_msg(" done       map pass %d %p \n", iflag,
                                   mapped_dmem));

    // Check the returned pointer
    CHECK(mapped_dmem != 0);
    CHECK_EQUAL(m_dmem->host_mem.aligned_ptr, mapped_dmem);
    CHECK(mapped_dmem);
    ACL_LOCKED(acl_print_debug_msg(" mapped_dmem %p %x\n", mapped_dmem,
                                   *(int *)mapped_dmem));

    // Check the reference count. The mapping increments the reference count
    // only for as long as the event is around.
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    if (e->execution_status == CL_COMPLETE) {
      CHECK_EQUAL(1, refcnt);
    } else {
      CHECK_EQUAL(3, refcnt);
    }
    clReleaseEvent(e);
    clFinish(m_cq); // and flush the event out so it releases the mem object
                    // references

    // Check that the contents were actually copied down from the device.
    // In our test setup the device memory is actually readable.  :-)
    CHECK_EQUAL(0x90210fbf, *((unsigned int *)mapped_dmem));

    *(int *)mapped_dmem = 0x10203457;

    // Check the reference count. The mapping increments the reference count
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK_EQUAL(1, refcnt); // event was deleted, so should be 1 now.

    // Check the map count.
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_MAP_COUNT, sizeof(refcnt),
                                   &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK(m_dmem->mapping_count > 0);
    CHECK_EQUAL(1, m_dmem->mapping_count);
    CHECK_EQUAL(1, refcnt);

    // Check our tracking of the writable master copy.
    if (flags[iflag] & CL_MAP_WRITE) {
      CHECK(m_dmem->writable_copy_on_host);
    } else {
      // If it's not a map request for write, then the writable copy is on
      // the host only if we're in a unified memory setting (like Nios)
      // For external host, then the master copy is not on the host.
      CHECK_EQUAL(!!(m_dmem->block_allocation->region->is_host_accessible),
                  !!(m_dmem->writable_copy_on_host));
    }

    ACL_LOCKED(acl_print_debug_msg(" about to unmap pass %d\n", iflag));
    // Unmap it.
    CHECK_EQUAL(CL_SUCCESS,
                clEnqueueUnmapMemObject(m_cq, m_dmem, mapped_dmem, 0, 0, &e));
    ACL_LOCKED(acl_print_debug_msg("          unmap pass %d\n", iflag));

    // the reference count is elevated as long as the event is still around.
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK_EQUAL(1, refcnt);

    clReleaseEvent(e);

    // Check the reference count.  Now that the event is gone, it's back down
    // to 1.
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_REFERENCE_COUNT,
                                   sizeof(refcnt), &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK_EQUAL(1, refcnt);

    // Check the map count.
    refcnt = 0xdeadbeef;
    size_ret = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clGetMemObjectInfo(m_dmem, CL_MEM_MAP_COUNT, sizeof(refcnt),
                                   &refcnt, &size_ret));
    CHECK_EQUAL(sizeof(cl_uint), size_ret);
    CHECK(m_dmem->mapping_count == 0);
    CHECK_EQUAL(0, m_dmem->mapping_count);
    CHECK_EQUAL(0, refcnt);

    // Check our tracking of the writable master copy.
    // The writable copy should be back on the device.
    CHECK_EQUAL(!!(m_dmem->block_allocation->region->is_host_accessible),
                !!(m_dmem->writable_copy_on_host));

    // Application must ensure that mapped memory must be unmapped before
    // any kernels run that write the mem. Otherwise behaviour is undefined.
    // So we are always copy the buffer back, unless it is mapped with read
    // only.
    //
    // Check that the contents were actually copied back to the device.
    // In our test setup the device memory is actually readable.  :-)
    if (flags[iflag] & ~CL_MAP_READ) {
      CHECK_EQUAL(0x10203457, *((int *)acltest_translate_device_address(
                                  m_dmem->block_allocation->range.begin, 0)));
    }
  }

  ACL_LOCKED(acl_print_debug_msg("end map_dev_buf_with_backing_store\n"));
}

MT_TEST(acl_mem, backing_store) {
  ACL_LOCKED(acl_print_debug_msg("begin backing_store\n"));

  load_backing_store_context();
  size_t size = 1024;
  char foobar[1024];

  cl_mem_flags flags[] = {
      CL_MEM_READ_WRITE,                         // device side
      CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, // host side
      CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR    // host side, user provided
  };

  unsigned flags_length = 3;

  CHECK_EQUAL(1, m_context->device_buffers_have_backing_store);

  for (unsigned pass = 0; pass < flags_length; pass++) {
    cl_int status = CL_INVALID_VALUE;

    // use syncThreads() to ensure each thread uses it's own cl_mem object
    // so that the acl_ref_count(mem) == 0 check below will pass
    syncThreads();
    cl_mem mem =
        clCreateBuffer(m_context, flags[pass], size,
                       (pass == (flags_length - 1) ? foobar : 0), &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(0 != mem->block_allocation->range.begin || mem->allocation_deferred);
    syncThreads();

    switch (pass) {
    case 0: // Device buffer has host-based backing store.
    case 1: // Host alloc'd pointer also uses the host_mem field.
      CHECK_EQUAL(ACL_MEM_ALIGN, mem->host_mem.alignment);
      CHECK(0 != mem->host_mem.raw); // the malloc'd pointer
      // It's always aligned
      CHECK_EQUAL(0, (uintptr_t)mem->block_allocation->range.begin &
                         (ACL_MEM_ALIGN - 1));
      CHECK_EQUAL(0,
                  (uintptr_t)mem->host_mem.aligned_ptr & (ACL_MEM_ALIGN - 1));
      break;

    case 2: // User provided buffer does not use the host_mem field.
      CHECK_EQUAL(foobar, mem->block_allocation->range.begin);
      CHECK_EQUAL(0, mem->host_mem.aligned_ptr);
      CHECK_EQUAL(0, mem->host_mem.raw);
      break;
    }

    // clean up
    CHECK_EQUAL(1, acl_ref_count(mem));
    CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  }

  ACL_LOCKED(acl_print_debug_msg("end begin backing_store\n"));
}

MT_TEST(acl_mem, track_writable_copy) {
  ACL_LOCKED(acl_print_debug_msg("begin track_writable_copy\n"));

  load_backing_store_context();
  CHECK_EQUAL(1, m_context->device_buffers_have_backing_store);

  // This test only means anything on a non-unified memory environment.
  if (m_dmem->block_allocation->region->is_host_accessible)
    return;

  // Initial conditions are as expected.
  CHECK_EQUAL(0, m_dmem->writable_copy_on_host);
  CHECK_EQUAL(0, m_dmem->mapping_count);

  void *mapped_dmem = 0;
  for (unsigned num_pass = 1; num_pass <= 2; num_pass++) {
    for (unsigned pass = 0; pass < num_pass; pass++) {
      // First map as readonly, then second round map again as writable.
      cl_map_flags flags = (cl_map_flags)(pass ? CL_MAP_WRITE : CL_MAP_READ);
      cl_int status = CL_INVALID_VALUE;
      mapped_dmem = clEnqueueMapBuffer(m_cq, m_dmem, 1, flags, 0, SUBBUF_SIZE,
                                       0, 0, 0, &status);
      CHECK(mapped_dmem != 0);
      CHECK_EQUAL(CL_SUCCESS, status);
      CHECK_EQUAL(pass + 1, m_dmem->mapping_count);

      switch (pass) {
      case 0: // have only mapped as READ.
        CHECK_EQUAL(0, m_dmem->writable_copy_on_host);
        break;
      case 1: // mapping a WRITE after READ implies it's writable
        CHECK_EQUAL(1, m_dmem->writable_copy_on_host);
        break;
      }
    }

    // Now unmap it.
    for (unsigned pass = 0; pass < num_pass; pass++) {

      CHECK_EQUAL(num_pass - pass, m_dmem->mapping_count);

      CHECK_EQUAL(CL_SUCCESS,
                  clEnqueueUnmapMemObject(m_cq, m_dmem, mapped_dmem, 0, 0, 0));
      // We've decremented the mappings count.
      CHECK_EQUAL(num_pass - 1 - pass, m_dmem->mapping_count);
      if (m_dmem->mapping_count) {
        // Should still counted as writable on the host, if it ever was writable
        // on the host.
        // If num_pass is 1, then we only ever mapped it as read-only!
        // This is a boolean check!
        CHECK_EQUAL(num_pass > 1, m_dmem->writable_copy_on_host);
      } else {
        CHECK_EQUAL(0, m_dmem->writable_copy_on_host);
      }
    }
  }

  ACL_LOCKED(acl_print_debug_msg("end track_writable_copy\n"));
}

MT_TEST(acl_mem, map_and_refcount) {
  cl_int status;
  load_backing_store_context();

  CHECK(m_hmem);
  CHECK_EQUAL(1, acl_ref_count(m_hmem));

  status = 49;
  cl_event ue = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_event ue2 = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  status = 49;
  char *cp = (char *)clEnqueueMapBuffer(m_cq, m_hmem, CL_FALSE, CL_MAP_WRITE, 0,
                                        1024, 1, &ue, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(cp);
  CHECK_EQUAL(m_hmem->host_mem.aligned_ptr, cp);
  CHECK_EQUAL(2, acl_ref_count(m_hmem));
  CHECK_EQUAL(0, m_hmem->mapping_count);

  status = clEnqueueUnmapMemObject(m_cq, m_hmem, cp, 1, &ue2, 0);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(3, acl_ref_count(m_hmem));
  CHECK_EQUAL(0, m_hmem->mapping_count); // still 0

  status = clSetUserEventStatus(ue, CL_COMPLETE);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(1, m_hmem->mapping_count); // now it's mapped.
  CHECK_EQUAL(2,
              acl_ref_count(m_hmem)); // release the reference from the map cmd

  status = clSetUserEventStatus(ue2, CL_COMPLETE);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(1, acl_ref_count(m_hmem)); // released the reference from map cmd.
  CHECK_EQUAL(0, m_hmem->mapping_count); // all gone

  status = clReleaseEvent(ue);
  CHECK_EQUAL(CL_SUCCESS, status);
}

// Create a cl_mem object from various sources (no pointer, host pointer, SVM
// pointer) and query clGetMemObjectInfo to see if an SVM pointer is used.
MT_TEST(acl_mem, map_svm_pointer) {
  ACL_LOCKED(acl_print_debug_msg("begin map_svm_pointer\n"));
  cl_int status = CL_SUCCESS;
  cl_mem svm_mem;
  int *svm_pointer;
  int *host_pointer;
  cl_bool uses_svm_pointer;
  cl_mem_flags valid_flags;
  cl_svm_mem_flags svm_flags = CL_MEM_READ_WRITE;

  svm_pointer =
      (int *)clSVMAllocIntelFPGA(m_context, svm_flags, sizeof(int), 0);
  host_pointer = (int *)acl_malloc(sizeof(int));

  // Create buffer from no pointer and check mem object info
  status = CL_MAP_FAILURE;
  valid_flags = CL_MEM_READ_WRITE;
  svm_mem = clCreateBuffer(m_context, valid_flags, sizeof(int), NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(svm_mem, CL_MEM_USES_SVM_POINTER,
                                             sizeof(uses_svm_pointer),
                                             &uses_svm_pointer, NULL));
  CHECK_EQUAL(CL_FALSE, uses_svm_pointer);
  clReleaseMemObject(svm_mem);

  // Create buffer from host pointer and check mem object info
  status = CL_MAP_FAILURE;
  valid_flags = CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR;
  svm_mem = clCreateBuffer(m_context, valid_flags, sizeof(int), host_pointer,
                           &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(svm_mem, CL_MEM_USES_SVM_POINTER,
                                             sizeof(uses_svm_pointer),
                                             &uses_svm_pointer, NULL));
  CHECK_EQUAL(CL_FALSE, uses_svm_pointer);
  clReleaseMemObject(svm_mem);

  // Create buffer from SVM pointer and check mem object info
  status = CL_MAP_FAILURE;
  valid_flags = CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR;
  svm_mem =
      clCreateBuffer(m_context, valid_flags, sizeof(int), svm_pointer, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clGetMemObjectInfo(svm_mem, CL_MEM_USES_SVM_POINTER,
                                             sizeof(uses_svm_pointer),
                                             &uses_svm_pointer, NULL));
  CHECK_EQUAL(CL_TRUE, uses_svm_pointer);

  acl_free(host_pointer);
  clReleaseMemObject(svm_mem);
}

MT_TEST(acl_mem, map_unmap_image) {
  ACL_LOCKED(acl_print_debug_msg("begin map_unmap_image\n"));
  cl_int status;
  cl_mem image;
  float *input_ptr;
  float *output_ptr;
  size_t width = 5;
  size_t height = 6;
  size_t origin[3];
  size_t region[3];
  cl_uint mapcnt = 0xdeadbeef;
  cl_uint refcnt = 0xdeadbeef;
  size_t size_ret;
  size_t image_row_pitch;
  size_t image_slice_pitch;

  // 2D Image
  cl_image_format image_format = {CL_R, CL_FLOAT};
  cl_image_desc image_desc = {
      CL_MEM_OBJECT_IMAGE2D, width, height, 1, 0, 0, 0, 0, 0, {NULL}};

  origin[0] = 0;
  origin[1] = 0;
  origin[2] = 0;

  region[0] = width;
  region[1] = height;
  region[2] = 1;

  image = clCreateImage(m_context, 0, &image_format, &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  input_ptr = (float *)acl_malloc(sizeof(float) * width * height);
  CHECK(input_ptr != 0);

  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      CHECK(input_ptr);
      input_ptr[col + row * width] = (float)col + (float)row + 1.5f;
    }
  }

  status = clEnqueueWriteImage(m_cq, image, CL_TRUE, origin, region, 0, 0,
                               input_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                 &mapcnt, &size_ret));
  CHECK_EQUAL(sizeof(mapcnt), size_ret);
  CHECK_EQUAL(0, mapcnt);
  CHECK_EQUAL(image->mapping_count, mapcnt);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(image), refcnt);

  output_ptr = (float *)clEnqueueMapImage(
      m_cq, image, CL_TRUE, 0, origin, region, &image_row_pitch,
      &image_slice_pitch, 0, NULL, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(output_ptr != NULL);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                 &mapcnt, &size_ret));
  CHECK_EQUAL(sizeof(mapcnt), size_ret);
  CHECK_EQUAL(1, mapcnt);
  CHECK_EQUAL(image->mapping_count, mapcnt);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(image), refcnt);

  // Compare contents of first pointer with second pointer
  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      CHECK(output_ptr);
      CHECK(input_ptr[col + row * width] == output_ptr[col + row * width]);
    }
  }

  status = clEnqueueUnmapMemObject(m_cq, image, output_ptr, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_MAP_COUNT, sizeof(mapcnt),
                                 &mapcnt, &size_ret));
  CHECK_EQUAL(sizeof(mapcnt), size_ret);
  CHECK_EQUAL(0, mapcnt);
  CHECK_EQUAL(image->mapping_count, mapcnt);

  CHECK_EQUAL(CL_SUCCESS,
              clGetMemObjectInfo(image, CL_MEM_REFERENCE_COUNT, sizeof(refcnt),
                                 &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(image), refcnt);

  clReleaseMemObject(image);
  acl_free(input_ptr);

  ACL_LOCKED(acl_print_debug_msg("end map_unmap_image\n"));
}

MT_TEST(acl_mem, enqueue_migrate_mem) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  cl_int status;
  cl_mem mem1;
  cl_mem mem2;
  cl_mem mems[2];
  cl_mem_migration_flags flags;

  ACL_LOCKED(acl_print_debug_msg("begin enqueue_migrate_mem\n"));

  mem1 = clCreateBuffer(m_context, 0, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  mem2 = clCreateBuffer(m_context, 0, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  mems[0] = mem1;
  mems[1] = mem2;

  // Invalid command queue
  struct _cl_command_queue fake_cq = {0};
  status = CL_SUCCESS;
  status = clEnqueueMigrateMemObjects(0, 1, &mem1, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);
  status = clEnqueueMigrateMemObjects(&fake_cq, 1, &mem1, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid memory object
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem_storage = {0};
  cl_mem fake_mem = &fake_mem_storage;
  status = clEnqueueMigrateMemObjects(m_cq, 1, &fake_mem, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();

  // Invalid value
  status = CL_SUCCESS;
  status = clEnqueueMigrateMemObjects(m_cq, 1, 0, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMigrateMemObjects(m_cq, 0, &mem1, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Invalid flags
  status = CL_SUCCESS;
  flags =
      (CL_MIGRATE_MEM_OBJECT_HOST | CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED) +
      1;
  status = clEnqueueMigrateMemObjects(m_cq, 1, &mem1, flags, 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Invalid event wait list
  status = CL_SUCCESS;
  status = clEnqueueMigrateMemObjects(m_cq, 1, &mem1, 0, 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueMigrateMemObjects(m_cq, 1, &mem1, 0, 0, (cl_event *)&status,
                                      NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Valid settings don't fail
  status = CL_SUCCESS;
  status = clEnqueueMigrateMemObjects(m_cq, 1, &mem1, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clEnqueueMigrateMemObjects(
      m_cq, 1, &mem1, CL_MIGRATE_MEM_OBJECT_HOST, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clEnqueueMigrateMemObjects(
      m_cq, 1, &mem1, CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clEnqueueMigrateMemObjects(m_cq, 2, mems, 0, 0, NULL,
                                      NULL); // Move two arrays
  CHECK_EQUAL(CL_SUCCESS, status);

  clFlush(m_cq);

  // create a context on another device:
  CHECK(m_num_devices > 1);
  status = CL_INVALID_DEVICE;
  cl_command_queue m_cq2 = clCreateCommandQueue(
      m_context, m_device[1], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq2)));

  status = CL_SUCCESS;
  status = clEnqueueMigrateMemObjects(m_cq2, 1, &mem1, 0, 0, NULL, NULL);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFlush(m_cq2);

  status = clEnqueueMigrateMemObjects(
      m_cq2, 2, mems, 0, 0, NULL,
      NULL); // move two arrays, one should already be at the destination
  CHECK_EQUAL(CL_SUCCESS, status);

  clFlush(m_cq2);

  status = clEnqueueMigrateMemObjects(m_cq, 2, mems, 0, 0, NULL,
                                      NULL); // move them back
  CHECK_EQUAL(CL_SUCCESS, status);

  clFlush(m_cq);

  // move them back and forth repeatedly
  for (int i = 0; i < 10; i++) {
    status = clEnqueueMigrateMemObjects(m_cq2, 2, mems, 0, 0, NULL, NULL);
    CHECK_EQUAL(CL_SUCCESS, status);

    status = clEnqueueMigrateMemObjects(m_cq, 2, mems, 0, 0, NULL, NULL);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  clFlush(m_cq);
  clFlush(m_cq2);

  clReleaseMemObject(mem1);
  clReleaseMemObject(mem2);

  clReleaseCommandQueue(m_cq2);

  ACL_LOCKED(acl_print_debug_msg("end enqueue_migrate_mem\n"));
}

// We have two to check the correct order of calling registered callback fns.
static void CL_CALLBACK my_fn_mem_destructor_notify(cl_mem memobj,
                                                    void *user_data) {
  int *a = (int *)user_data;
  UNREFERENCED_PARAMETER(memobj);
  if (!user_data)
    return;
  a[0]++;   // counts the number of calls.
  a[1] = 1; // indicates this function was called last.
}
static void CL_CALLBACK my_fn_mem_destructor_notify2(cl_mem, void *user_data) {
  int *a = (int *)user_data;
  if (!user_data)
    return;
  a[0]++;   // counts the number of calls.
  a[1] = 2; // indicates this function was called last.
}

MT_TEST(acl_mem, destructor_callback) {
  cl_int status;
  cl_mem mem;
  ACL_LOCKED(acl_print_debug_msg("begin destructor_callback\n"));

  // Bad mem object
  syncThreads();
  {
    acl_set_allow_invalid_type<cl_mem>(1);
    syncThreads();
    struct _cl_mem fake_mem = {0};
    int user_data = 0;
    CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
                clSetMemObjectDestructorCallbackIntelFPGA(
                    &fake_mem, my_fn_mem_destructor_notify, NULL));
    CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
                clSetMemObjectDestructorCallbackIntelFPGA(
                    &fake_mem, my_fn_mem_destructor_notify, &user_data));
    syncThreads();
    acl_set_allow_invalid_type<cl_mem>(0);
  }
  syncThreads();

  // Bad callback function
  {
    mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 10, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));
    CHECK_EQUAL(CL_INVALID_VALUE,
                clSetMemObjectDestructorCallbackIntelFPGA(mem, NULL, NULL));
    clReleaseMemObject(mem);
  }

  // Calling the call_back handler manually
  {
    int user_data[] = {0, 0};
    mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 10, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));
    CHECK_EQUAL(CL_SUCCESS, clSetMemObjectDestructorCallbackIntelFPGA(
                                mem, my_fn_mem_destructor_notify, user_data));
    ACL_LOCKED(acl_mem_destructor_callback(
        mem)); // This should also remove the registered callbacks from the
               // list, so no callback gets called twice.
    CHECK_EQUAL(1, user_data[0]);
    CHECK_EQUAL(1, user_data[1]);
    user_data[0] = 0;
    user_data[1] = 0;
    clReleaseMemObject(mem);
    // Now making sure the callbacks do not get called again.
    CHECK_EQUAL(0, user_data[0]);
    CHECK_EQUAL(0, user_data[1]);
  }

  // Buffer mem objects
  {
    int user_data[] = {0, 0};
    mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 10, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));
    CHECK_EQUAL(CL_SUCCESS, clSetMemObjectDestructorCallbackIntelFPGA(
                                mem, my_fn_mem_destructor_notify, user_data));
    CHECK_EQUAL(CL_SUCCESS, clSetMemObjectDestructorCallbackIntelFPGA(
                                mem, my_fn_mem_destructor_notify2, user_data));

    clRetainMemObject(mem);  // increment
    clReleaseMemObject(mem); // decrement
    // Memory is not destroyed yet, so no callbacks yet.
    CHECK_EQUAL(0, user_data[0]);
    CHECK_EQUAL(0, user_data[1]);

    clReleaseMemObject(mem);      // This should destroy mem object.
    CHECK_EQUAL(2, user_data[0]); // Both callbacks should be called.
    CHECK_EQUAL(
        1, user_data[1]); // The callbacks should be called in reverse order.
  }

  // Pipe mem objects
  {
    int user_data[] = {0, 0};
    mem = clCreatePipe(m_context, CL_MEM_READ_WRITE, 5, 10, NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));
    CHECK_EQUAL(CL_SUCCESS, clSetMemObjectDestructorCallbackIntelFPGA(
                                mem, my_fn_mem_destructor_notify, user_data));
    CHECK_EQUAL(CL_SUCCESS, clSetMemObjectDestructorCallbackIntelFPGA(
                                mem, my_fn_mem_destructor_notify2, user_data));

    clRetainMemObject(mem);  // increment
    clReleaseMemObject(mem); // decrement
    // Memory is not destroyed yet, so no callbacks yet.
    CHECK_EQUAL(0, user_data[0]);
    CHECK_EQUAL(0, user_data[1]);

    clReleaseMemObject(mem);      // This should destroy mem object.
    CHECK_EQUAL(2, user_data[0]); // Both callbacks should be called.
    CHECK_EQUAL(
        1, user_data[1]); // The callbacks should be called in reverse order.
  }

  // Subbuffers
  {
    cl_mem parent, subbuffer;
    int user_data[] = {0, 0};
    cl_buffer_region test_region = {0, 2};

    parent = clCreateBuffer(m_context, CL_MEM_READ_WRITE, ACL_MEM_ALIGN * 3, 0,
                            &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(parent != NULL);

    CHECK_EQUAL(1, acl_ref_count(parent));
    subbuffer =
        clCreateSubBuffer(parent, CL_MEM_READ_WRITE,
                          CL_BUFFER_CREATE_TYPE_REGION, &test_region, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_mem_is_valid(subbuffer)));
    // Check that the parent reference count increases by one
    CHECK_EQUAL(2, acl_ref_count(parent));

    CHECK_EQUAL(CL_SUCCESS,
                clSetMemObjectDestructorCallbackIntelFPGA(
                    parent, my_fn_mem_destructor_notify, user_data));
    CHECK_EQUAL(CL_SUCCESS,
                clSetMemObjectDestructorCallbackIntelFPGA(
                    subbuffer, my_fn_mem_destructor_notify2, user_data));

    clRetainMemObject(subbuffer);  // increment
    clReleaseMemObject(subbuffer); // decrement
    // Memory is not destroyed yet, so no callbacks yet.
    CHECK_EQUAL(0, user_data[0]);
    CHECK_EQUAL(0, user_data[1]);

    clReleaseMemObject(subbuffer);
    CHECK_EQUAL(1, user_data[0]);
    CHECK_EQUAL(2, user_data[1]);

    clReleaseMemObject(parent);
    CHECK_EQUAL(2, user_data[0]);
    CHECK_EQUAL(1, user_data[1]);
  }
}

cl_int validate_fill_result(cl_context m_context, void *output_ptr,
                            void *fill_color, const size_t *image_size,
                            size_t *origin, size_t *region,
                            const cl_image_format image_from,
                            const cl_image_format image_to) {
  cl_int errcode_ret;
  size_t color_size;
  ACL_LOCKED(color_size = acl_get_image_element_size(m_context, &image_to,
                                                     &errcode_ret));
  CHECK_EQUAL(CL_SUCCESS, errcode_ret);

  acl_print_debug_msg("color size: %d\n", (int)color_size);
  acl_print_debug_msg(
      "image_from: type: %x order: %x\t image_to: type: %x order: %x\n",
      image_from.image_channel_data_type, image_from.image_channel_order,
      image_to.image_channel_data_type, image_to.image_channel_order);

  // Grabbing the first pixel.
  cl_uchar
      first_pixel[4 * sizeof(float)]; // Maximum length of a pixel is 4 floats.
  safe_memcpy(first_pixel,
              ((char *)output_ptr) +
                  (origin[0] + origin[1] * image_size[0] +
                   origin[2] * image_size[0] * image_size[1]) *
                      color_size,
              color_size, 4 * sizeof(float), color_size);

  // Validating the format conversion.
  switch (image_to.image_channel_data_type) {
  case CL_UNORM_INT8:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[0] * 255.0),
                  ((cl_uchar *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[0] * 255.0),
                  ((cl_uchar *)first_pixel)[0]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[1] * 255.0),
                  ((cl_uchar *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[0] * 255.0),
                  ((cl_uchar *)first_pixel)[0]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[1] * 255.0),
                  ((cl_uchar *)first_pixel)[1]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[2] * 255.0),
                  ((cl_uchar *)first_pixel)[2]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[3] * 255.0),
                  ((cl_uchar *)first_pixel)[3]);
    } else if (image_to.image_channel_order == CL_BGRA) {
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[0] * 255.0),
                  ((cl_uchar *)first_pixel)[2]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[1] * 255.0),
                  ((cl_uchar *)first_pixel)[1]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[2] * 255.0),
                  ((cl_uchar *)first_pixel)[0]);
      CHECK_EQUAL((cl_uchar)(((cl_float *)fill_color)[3] * 255.0),
                  ((cl_uchar *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_UNORM_INT16:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[0] * 65535.0),
                  ((cl_ushort *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[0] * 65535.0),
                  ((cl_ushort *)first_pixel)[0]);
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[1] * 65535.0),
                  ((cl_ushort *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[0] * 65535.0),
                  ((cl_ushort *)first_pixel)[0]);
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[1] * 65535.0),
                  ((cl_ushort *)first_pixel)[1]);
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[2] * 65535.0),
                  ((cl_ushort *)first_pixel)[2]);
      CHECK_EQUAL((cl_ushort)(((cl_float *)fill_color)[3] * 65535.0),
                  ((cl_ushort *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_SNORM_INT8:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[0] * 127.0),
                  ((cl_char *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[0] * 127.0),
                  ((cl_char *)first_pixel)[0]);
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[1] * 127.0),
                  ((cl_char *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[0] * 127.0),
                  ((cl_char *)first_pixel)[0]);
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[1] * 127.0),
                  ((cl_char *)first_pixel)[1]);
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[2] * 127.0),
                  ((cl_char *)first_pixel)[2]);
      CHECK_EQUAL((cl_char)(((cl_float *)fill_color)[3] * 127.0),
                  ((cl_char *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_SNORM_INT16:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[0] * 32767.0),
                  ((cl_short *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[0] * 32767.0),
                  ((cl_short *)first_pixel)[0]);
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[1] * 32767.0),
                  ((cl_short *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[0] * 32767.0),
                  ((cl_short *)first_pixel)[0]);
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[1] * 32767.0),
                  ((cl_short *)first_pixel)[1]);
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[2] * 32767.0),
                  ((cl_short *)first_pixel)[2]);
      CHECK_EQUAL((cl_short)(((cl_float *)fill_color)[3] * 32767.0),
                  ((cl_short *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_FLOAT:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[0],
                  ((cl_float *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[0],
                  ((cl_float *)first_pixel)[0]);
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[1],
                  ((cl_float *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[0],
                  ((cl_float *)first_pixel)[0]);
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[1],
                  ((cl_float *)first_pixel)[1]);
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[2],
                  ((cl_float *)first_pixel)[2]);
      CHECK_EQUAL((cl_float)((cl_float *)fill_color)[3],
                  ((cl_float *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_SIGNED_INT8:

    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[0],
                  ((cl_char *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[0],
                  ((cl_char *)first_pixel)[0]);
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[1],
                  ((cl_char *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[0],
                  ((cl_char *)first_pixel)[0]);
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[1],
                  ((cl_char *)first_pixel)[1]);
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[2],
                  ((cl_char *)first_pixel)[2]);
      CHECK_EQUAL((cl_char)((cl_int *)fill_color)[3],
                  ((cl_char *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_SIGNED_INT16:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[0],
                  ((cl_short *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[0],
                  ((cl_short *)first_pixel)[0]);
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[1],
                  ((cl_short *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[0],
                  ((cl_short *)first_pixel)[0]);
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[1],
                  ((cl_short *)first_pixel)[1]);
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[2],
                  ((cl_short *)first_pixel)[2]);
      CHECK_EQUAL((cl_short)((cl_int *)fill_color)[3],
                  ((cl_short *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_SIGNED_INT32:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[0],
                  ((cl_int *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[0],
                  ((cl_int *)first_pixel)[0]);
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[1],
                  ((cl_int *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[0],
                  ((cl_int *)first_pixel)[0]);
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[1],
                  ((cl_int *)first_pixel)[1]);
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[2],
                  ((cl_int *)first_pixel)[2]);
      CHECK_EQUAL((cl_int)((cl_int *)fill_color)[3],
                  ((cl_int *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_UNSIGNED_INT8:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[0],
                  ((cl_uchar *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[0],
                  ((cl_uchar *)first_pixel)[0]);
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[1],
                  ((cl_uchar *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[0],
                  ((cl_uchar *)first_pixel)[0]);
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[1],
                  ((cl_uchar *)first_pixel)[1]);
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[2],
                  ((cl_uchar *)first_pixel)[2]);
      CHECK_EQUAL((cl_uchar)((cl_uint *)fill_color)[3],
                  ((cl_uchar *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_UNSIGNED_INT16:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[0],
                  ((cl_ushort *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[0],
                  ((cl_ushort *)first_pixel)[0]);
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[1],
                  ((cl_ushort *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[0],
                  ((cl_ushort *)first_pixel)[0]);
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[1],
                  ((cl_ushort *)first_pixel)[1]);
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[2],
                  ((cl_ushort *)first_pixel)[2]);
      CHECK_EQUAL((cl_ushort)((cl_uint *)fill_color)[3],
                  ((cl_ushort *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  case CL_UNSIGNED_INT32:
    if (image_to.image_channel_order == CL_R) {
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[0],
                  ((cl_uint *)first_pixel)[0]);
    } else if (image_to.image_channel_order == CL_RG) {
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[0],
                  ((cl_uint *)first_pixel)[0]);
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[1],
                  ((cl_uint *)first_pixel)[1]);
    } else if (image_to.image_channel_order == CL_RGBA) {
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[0],
                  ((cl_uint *)first_pixel)[0]);
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[1],
                  ((cl_uint *)first_pixel)[1]);
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[2],
                  ((cl_uint *)first_pixel)[2]);
      CHECK_EQUAL((cl_uint)((cl_uint *)fill_color)[3],
                  ((cl_uint *)first_pixel)[3]);
    } else
      FAIL("Unexpected image channel order.");
    break;

  default:
    FAIL("Unexpected image channel data type.");
  }

  // Validating the fill action. All elements should be equal to the first
  // element.
  for (size_t slice = 0; slice < image_size[2]; ++slice)
    for (size_t row = 0; row < image_size[1]; ++row)
      for (size_t col = 0; col < image_size[0]; ++col)
        for (size_t inx = 0; inx < color_size; inx++) {
          // if in the region, compare to first pixel.
          if (slice >= origin[2] && slice < origin[2] + region[2] &&
              row >= origin[1] && row < origin[1] + region[1] &&
              col >= origin[0] && col < origin[0] + region[0]) {
            cl_char expected = (cl_char)first_pixel[inx];
            cl_char received = ((
                cl_char *)output_ptr)[(col + row * image_size[0] +
                                       slice * image_size[0] * image_size[1]) *
                                          color_size +
                                      inx];
            CHECK_EQUAL(expected, received);
          } else { // the bytes that are not in the region should not be
                   // affected.
            cl_char received = ((
                cl_char *)output_ptr)[(col + row * image_size[0] +
                                       slice * image_size[0] * image_size[1]) *
                                          color_size +
                                      inx];
            CHECK_EQUAL(0, received);
          }
        }
  return CL_SUCCESS;
}

// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// Profiler
// ========
//
// Handle the parsing and printing out of profiler output
//

// System headers.
#include <assert.h>
#include <atomic>
#include <fcntl.h>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else // Linux
#include <unistd.h>
#endif

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_kernel.h>
#include <acl_profiler.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// From stackoverflow: "C Preprocessor: concatenate int to string"
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define ACL_PROFILER_OUTPUT_FILENAME "profile.mon"
#define ACL_PROFILER_TIMER_ENVNAME "ACL_PROFILE_TIMER"
#define ACL_PROFILER_ENABLE_ENVNAME "ACL_RUNTIME_PROFILING_ACTIVE"

// The number of cycles that need to delay for to allow for proper readback of
// tessellated counters At the moment, have 4 segments in the counter, so should
// delay for 4 extra cycles (set to 5 in case the number of counter segments is
// increased to 5 - unlikely to go higher than this)
#define TESS_DELAY 5

static unsigned long profile_timer_on = 0;
static unsigned long profile_enable = 0;
static int pfile_fd = -1;
static int opened_count = 0;
static int failed_to_open_count = 0;
static bool pfile_was_opened = false;
static unsigned long long autorun_start_time = 0;

// Shared Counters Control Value
// -2 => not set
// -1 => shared is off
// 0 - 3 => one of the controls shared can be set to
static int profiler_shared_control_val = -2;

constexpr unsigned int profile_data_buffer_size = 1024 * 1024;    // 1M
constexpr unsigned int profile_data_temp_buffer_size = 10 * 1024; // 10K

int dump_profile_buffer_to_file();
int write_profile_info_to_file(unsigned num_profile_counters,
                               unsigned device_id, unsigned command_queue_id,
                               const std::string &name, uint64_t *profile_data,
                               cl_command_type op_type,
                               unsigned long long start_time,
                               unsigned long long end_time, int shared_counter);

class profile_data_buffer_t {
private:
  char cbuf[profile_data_buffer_size];
  std::atomic<unsigned int> buffer_index;
  std::atomic<unsigned int> file_index;

public:
  profile_data_buffer_t() {
    buffer_index.store(0, std::memory_order_release);
    file_index.store(0, std::memory_order_release);
  }
  const char *get_buffer() { return cbuf; }
  void write(const char *readout, unsigned int str_len) {
    unsigned int cur_buffer_index =
        buffer_index.load(std::memory_order_acquire);
    for (unsigned int i = 0; i < str_len; i++) {
      cbuf[cur_buffer_index] = readout[i];
      cur_buffer_index = (cur_buffer_index + 1) % profile_data_buffer_size;
    }
    buffer_index.store(cur_buffer_index, std::memory_order_release);
  }
  unsigned int size() {
    unsigned int cur_file_index = file_index.load(std::memory_order_acquire);
    unsigned int cur_buffer_index =
        buffer_index.load(std::memory_order_acquire);
    if (cur_file_index <= cur_buffer_index) {
      return cur_buffer_index - cur_file_index;
    } else {
      return cur_buffer_index + profile_data_buffer_size - cur_file_index;
    }
  }
  unsigned int get_buffer_index() {
    return buffer_index.load(std::memory_order_acquire);
  }
  unsigned int get_file_index() {
    return file_index.load(std::memory_order_acquire);
  }
  void update_file_index(unsigned int new_file_index) {
    file_index.store(new_file_index, std::memory_order_release);
  }
};

class profile_temporary_buffer_t {
private:
  char temp_buf[profile_data_temp_buffer_size];
  char *buf_ptr;
  unsigned int total_chars_written;

public:
  profile_temporary_buffer_t() { clear(); }
  void clear() {
    buf_ptr = &temp_buf[0];
    *buf_ptr = '\0';
    total_chars_written = 0;
  }
  unsigned int size() { return total_chars_written; }
  const char *get_buffer() { return temp_buf; }
  void write(const char *fmt, ...) {
    int chars_written = 0;
    va_list args;
    va_start(args, fmt);
    chars_written =
        vsnprintf(buf_ptr, profile_data_temp_buffer_size, fmt, args);
    buf_ptr += chars_written;
    total_chars_written += chars_written;
    va_end(args);
  }
};

// The runtime and signal threads will write to their own buffer to avoid any
// concurrency problem since they will receive and store the data in parallel.
// Store the data from the runtime thread
static profile_data_buffer_t profile_runtime_data_buffer;

// Store the data from the signal handler thread
static profile_data_buffer_t profile_signal_interrupt_data_buffer;
std::mutex lock;

int dump_profile_data_to_buffer(unsigned num_profile_counters,
                                unsigned device_id, unsigned command_queue_id,
                                const char *name, uint64_t *profile_data,
                                cl_command_type op_type,
                                unsigned long long start_time,
                                unsigned long long end_time,
                                int curr_shared_counters_type) {

  if (op_type == ACL_DEVICE_OP_KERNEL) {
    assert(num_profile_counters == 0 || profile_data != 0);
  }

  // temp_buf will construct the output string from the kernel's data
  // temp_buf will ensure a complete output line will be passed to the runtime
  // or signal handler data buffer.
  profile_temporary_buffer_t temp_buf;
  temp_buf.write("%u,%u,%s,%llu,%llu", device_id, command_queue_id, name,
                 start_time, end_time);
  if (op_type == ACL_DEVICE_OP_KERNEL) {
    temp_buf.write(",%d", curr_shared_counters_type);
    temp_buf.write(",%u", num_profile_counters);
    for (unsigned i = 0; i < num_profile_counters; i++) {
      temp_buf.write(",%llu", (long long unsigned)profile_data[i]);
    }
  }
  temp_buf.write("\n");

  if (acl_is_inside_sig()) {
    profile_signal_interrupt_data_buffer.write(temp_buf.get_buffer(),
                                               temp_buf.size());
  } else {
    profile_runtime_data_buffer.write(temp_buf.get_buffer(), temp_buf.size());
  }
  dump_profile_buffer_to_file();

  return 1;
}

void set_env_shared_counter_val() {
  // Get the shared counter value (or set to -1 if its not set/out of expected
  // bounds)
  char *curr_shared_counters_str = getenv(ACL_PROFILER_SHARED_COUNTER);
  // Valid inputs are 0-3, so checks will ensure atoi doesn't allow for inputs
  // such as ""
  if (curr_shared_counters_str != NULL &&
      strnlen(curr_shared_counters_str, 2) == 1 &&
      isdigit(curr_shared_counters_str[0])) {
    profiler_shared_control_val = atoi(curr_shared_counters_str);
    return;
    // Check if the input was "-1" in which case we don't want to print a
    // warning.
  } else if (curr_shared_counters_str != NULL &&
             (strnlen(curr_shared_counters_str, 3) != 2 ||
              strncmp(curr_shared_counters_str, "-1", 2) != 0)) {
    acl_print_debug_msg(
        "WARNING: Unable to set shared to %s. Setting to default (off).\n",
        curr_shared_counters_str);
  }
  profiler_shared_control_val = -1;
}

int get_env_profile_shared_counter_val() {
  // Hasn't been set yet
  if (profiler_shared_control_val <= -2 || profiler_shared_control_val > 3) {
    set_env_shared_counter_val();
  }
  return profiler_shared_control_val;
}

long int write_wrapper(int fd, const void *buf, unsigned int count) {
#ifdef _WIN32
  return _write(fd, buf, count);
#else // Linux
  return write(fd, buf, count);
#endif
}

int write_buffer_data_to_file(profile_data_buffer_t *profile_data_buffer) {
  unsigned int bytes_to_write = profile_data_buffer->size();
  if (bytes_to_write == 0) {
    return 1;
  }

  const char *buffer = profile_data_buffer->get_buffer();
  unsigned int buffer_index = profile_data_buffer->get_buffer_index();
  unsigned int file_index = profile_data_buffer->get_file_index();
  long int bytes_written;
  if (file_index <= buffer_index) {
    bytes_written =
        write_wrapper(pfile_fd, &buffer[file_index], bytes_to_write);
  } else {
    unsigned int bytes_to_arr_end = profile_data_buffer_size - file_index;
    bytes_written =
        write_wrapper(pfile_fd, &buffer[file_index], bytes_to_arr_end);
    bytes_written += write_wrapper(pfile_fd, &buffer[0], buffer_index);
  }
  profile_data_buffer->update_file_index(buffer_index);

  if (bytes_written < 0 || (unsigned long int)bytes_written != bytes_to_write) {
    acl_print_debug_msg("Could not write profile data to file!\n");
    return 0; // failure
  }

  return 1; // success
}

int dump_profile_buffer_to_file() {
  if (opened_count < 1 || !lock.try_lock()) {
    return 1;
  }

  int is_success = 1;
  is_success &=
      write_buffer_data_to_file(&profile_signal_interrupt_data_buffer);
  is_success &= write_buffer_data_to_file(&profile_runtime_data_buffer);

  lock.unlock();

  return is_success;
}

int write_profile_info_to_file(unsigned num_profile_counters,
                               unsigned device_id, unsigned command_queue_id,
                               const std::string &name, uint64_t *profile_data,
                               cl_command_type op_type,
                               unsigned long long start_time,
                               unsigned long long end_time,
                               int shared_counter) {

  if (!profile_enable)
    return 0;

  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }

  acl_open_profiler_file();

  // file open was unsuccessful, fail with a message
  if (opened_count < 1) {
    acl_print_debug_msg("Profiler output file is not opened: " STR(
        ACL_PROFILER_OUTPUT_FILENAME) "\n");
    return 0;
  }

  // temp_buf will construct the output string from the kernel's data
  // temp_buf will ensure a complete output line will be passed to the runtime
  // or signal handler data buffer.
  profile_temporary_buffer_t temp_buf;

  temp_buf.write("%d,", device_id);
  temp_buf.write("%d,", command_queue_id);
  temp_buf.write("%s,", name.c_str());
  temp_buf.write("%llu,", start_time);
  temp_buf.write("%llu,", end_time);
  temp_buf.write("%d", shared_counter);

  if (op_type == ACL_DEVICE_OP_KERNEL) {
    temp_buf.write(",%u", num_profile_counters);
    assert(num_profile_counters == 0 || profile_data != 0);
    for (unsigned i = 0; i < num_profile_counters; i++) {
      temp_buf.write(",");
      temp_buf.write("%llu", (long long unsigned)profile_data[i]);
    }
  }
  temp_buf.write("\n");

  long int bytes_written =
      write_wrapper(pfile_fd, temp_buf.get_buffer(), temp_buf.size());

  acl_close_profiler_file();

  if (bytes_written < 0 ||
      (unsigned long int)bytes_written != temp_buf.size()) {
    acl_print_debug_msg("Could not write profile data to file!\n");
    return 0;
  }
  return 1;
}

int write_profile_info_to_file_from_device(
    cl_device_id device_id, cl_context context, const char *name,
    uint64_t *profile_data, cl_command_type op_type,
    unsigned long long start_time, unsigned long long end_time,
    unsigned num_profile_counters, int shared_counter) {
  acl_assert_locked();
  if (!profile_enable)
    return 0;

  acl_open_profiler_file();

  // file open was unsuccessful, fail with a message
  if (opened_count < 1) {
    acl_context_callback(context, "Profiler output file is not opened: " STR(
                                      ACL_PROFILER_OUTPUT_FILENAME) "\n");
    return 0;
  }

  // temp_buf will construct the output string from the kernel's data
  // temp_buf will ensure a complete output line will be passed to the runtime
  // or signal handler data buffer.
  profile_temporary_buffer_t temp_buf;

  temp_buf.write("%d,", device_id->id);
  temp_buf.write("%d,", 0); // Set command_queue id to just be 0
  temp_buf.write("%s,", name);
  temp_buf.write("%llu,", start_time);
  temp_buf.write("%llu,", end_time);
  temp_buf.write("%d", shared_counter);

  if (op_type == ACL_DEVICE_OP_KERNEL) {
    unsigned i;
    temp_buf.write(",%u", num_profile_counters);
    assert(num_profile_counters == 0 || profile_data != 0);
    for (i = 0; i < num_profile_counters; i++) {
      temp_buf.write(",");
      temp_buf.write("%llu", (long long unsigned)profile_data[i]);
    }
  }
  temp_buf.write("\n");

  long int bytes_written =
      write_wrapper(pfile_fd, temp_buf.get_buffer(), temp_buf.size());

  acl_close_profiler_file();

  if (bytes_written < 0 ||
      (unsigned long int)bytes_written != temp_buf.size()) {
    acl_print_debug_msg("Could not write profile data to file!\n");

    return 0;
  }

  return 1;
}

void acl_enable_profiler_scan_chain(acl_device_op_t *op) {

  cl_event event;
  unsigned int physical_device_id;
  unsigned int accel_id;
  acl_assert_locked();

  event = op->info.event;

  acl_print_debug_msg("acl_enable_profiler_scan_chain\n");

  if (!event) {
    acl_print_debug_msg(
        "acl_enable_profiler_scan_chain is called for NULL event\n");
    return;
  }

  if (event->cmd.info.ndrange_kernel.device == 0) {
    return;
  }

  physical_device_id =
      event->cmd.info.ndrange_kernel.device->def.physical_device_id;
  accel_id = event->cmd.info.ndrange_kernel.accel_id;
  acl_get_hal()->set_profile_shared_control(physical_device_id, accel_id);
  acl_get_hal()->enable_profile_counters(physical_device_id, accel_id);
}

void acl_init_profiler() {
  char *timer_on;
  char *profiler_enable_env_var = getenv(ACL_PROFILER_ENABLE_ENVNAME);
  if (profiler_enable_env_var != NULL &&
      strnlen(profiler_enable_env_var, 2) == 1 &&
      strncmp(profiler_enable_env_var, "1", 1) == 0) {
    profile_enable = 1;
  } else {
    profile_enable = 0;
  }

  // Set the shared control value
  set_env_shared_counter_val();

  timer_on = getenv(ACL_PROFILER_TIMER_ENVNAME);
  acl_assert_locked();
  if (timer_on != NULL && strnlen(timer_on, 2) == 1 &&
      strncmp(timer_on, "1", 1) == 0) {
    profile_timer_on = 1;
  } else {
    profile_timer_on = 0;
  }
  acl_print_debug_msg("profile_timer_on=%lu\n", profile_timer_on);
}

unsigned long is_profile_enabled() { return profile_enable; }

unsigned long is_profile_timer_on() { return profile_timer_on; }

void acl_set_autorun_start_time() {
  std::scoped_lock lock{acl_mutex_wrapper};
  autorun_start_time = acl_get_hal()->get_timestamp();
}

CL_API_ENTRY cl_int CL_API_CALL clGetProfileInfoIntelFPGA(cl_event event) {
  cl_context context;
  cl_kernel kernel;
  unsigned num_profile_counters;
  uint64_t *profile_data;
  unsigned int physical_device_id;
  unsigned int accel_id;
  int i;
  _cl_command_queue *command_queue;
  cl_device_id device_id;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_event_is_valid(event)) {
    acl_print_debug_msg("clGetProfileInfoIntelFPGA is called for NULL event\n");
    return CL_INVALID_EVENT;
  }
  if (event->execution_status != CL_RUNNING) {
    acl_print_debug_msg(
        "clGetProfileInfoIntelFPGA is called for non-running event\n");
    return CL_INVALID_EVENT;
  }

  context = event->context;

  if (!acl_context_is_valid(context)) {
    acl_print_debug_msg(
        "clGetProfileInfoIntelFPGA is called for NULL context\n");
    return CL_INVALID_CONTEXT;
  }

  command_queue = event->command_queue;

  if (!acl_command_queue_is_valid(command_queue)) {
    ERR_RET(CL_INVALID_COMMAND_QUEUE, context,
            "clGetProfileInfoIntelFPGA is called for NULL command_queue");
  }

  device_id = command_queue->device;

  if (!acl_device_is_valid(device_id)) {
    ERR_RET(CL_INVALID_DEVICE, context,
            "clGetProfileInfoIntelFPGA is called for NULL device_id");
  }

  profile_data = 0;
  kernel = event->cmd.info.ndrange_kernel.kernel;
  if (!acl_kernel_is_valid(kernel)) {
    ERR_RET(CL_INVALID_KERNEL, context, "Invalid kernel attached to event");
  }

  // use autodiscovery info to find out how many words will be read from the
  // profiler
  num_profile_counters = kernel->accel_def->profiling_words_to_readback;

  if (num_profile_counters == 0) {
    // there is not profiler data and we are not printing timers
    // nothing to print
    ERR_RET(CL_PROFILING_INFO_NOT_AVAILABLE, context, "No profile information");
  }

  // this kernel has profiling data, get it
  if (num_profile_counters) {
    profile_data = kernel->profile_data;

    // expected to be already allocated at kernel creation
    assert(profile_data);

    physical_device_id =
        event->cmd.info.ndrange_kernel.device->def.physical_device_id;
    accel_id = event->cmd.info.ndrange_kernel.accel_id;

    acl_get_hal()->disable_profile_counters(physical_device_id, accel_id);
    // This for loop is needed to create a delay between counter disabling and
    // the start of shifting to allow tessellated counters to finish their
    // addition It is only needed for S10 and other familes that enable
    // tessellation - can be removed for other families
    for (i = 0; i < TESS_DELAY; i++) {
      acl_get_hal()->disable_profile_counters(physical_device_id, accel_id);
    }
    acl_get_hal()->get_profile_data(physical_device_id, accel_id, profile_data,
                                    num_profile_counters);
    acl_get_hal()->set_profile_shared_control(physical_device_id, accel_id);
    acl_get_hal()->enable_profile_counters(physical_device_id, accel_id);
  }

  // Get the curr shared counter
  int curr_shared_counters = get_env_profile_shared_counter_val();

  if (!write_profile_info_to_file(
          num_profile_counters, (unsigned)device_id->id,
          (unsigned)command_queue->id, kernel->accel_def->iface.name.c_str(),
          profile_data, ACL_DEVICE_OP_KERNEL,
          (unsigned long long)event->timestamp[CL_RUNNING],
          (unsigned long long)0, curr_shared_counters)) {
    ERR_RET(CL_OUT_OF_RESOURCES, context, "Unabled to dump profile data");
  }

  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clGetProfileDataDeviceIntelFPGA(
    cl_device_id device_id, cl_program program, cl_bool read_enqueue_kernels,
    cl_bool read_auto_enqueued, cl_bool clear_counters_after_readback,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret,
    cl_int *errcode_ret) {

  read_enqueue_kernels = read_enqueue_kernels;
  cl_context context;
  unsigned num_profile_counters;
  unsigned int physical_device_id;
  unsigned int accel_id;
  int i;
  cl_int status;
  unsigned long long profiled_time;

  clear_counters_after_readback = clear_counters_after_readback;
  param_value_size = param_value_size;
  param_value = param_value;
  param_value_size_ret = param_value_size_ret;
  errcode_ret = errcode_ret;

  std::scoped_lock lock{acl_mutex_wrapper};

  // Check if valid device_id
  if (!acl_device_is_valid(device_id)) {
    return CL_INVALID_DEVICE;
  }

  // Check if valid program
  if (!acl_program_is_valid(program)) {
    return CL_INVALID_PROGRAM;
  }

  // If program is valid, then context is valid because acl_program_is_valid
  // checks acl_context_is_valid.
  context = program->context;

  // Find the special readback kernel
  if (read_auto_enqueued) {
    const acl_device_binary_t *dev_bin = nullptr;

    const acl_accel_def_t *accel_def =
        acl_find_accel_def(program, ACL_PROFILE_AUTORUN_KERNEL_NAME, dev_bin,
                           &status, program->context, 0);
    // Return error if that readback kernel is not present
    if (status != CL_SUCCESS) {
      // Cannot find the readback kernel for autorun even though the host is
      // asking for it
      const char *message = "Cannot find readback kernel " STR(
          ACL_PROFILE_AUTORUN_KERNEL_NAME) " for autorun profiling. Make sure "
                                           "the .aocx was compiled with "
                                           "autorun kernel profiling enabled";
      ERR_RET(status, program->context, message);
    }
    assert(accel_def != NULL);

    // use autodiscovery info to find out how many words will be read from the
    // profiler
    num_profile_counters = accel_def->profiling_words_to_readback;

    if (num_profile_counters == 0) {
      // Message will say "No profile information for kernel
      // "Intel_Internal_Collect_Autorun_Profiling" for reading back autorun
      // profile data"
      const char *message = "No profile information for kernel " STR(
          ACL_PROFILE_AUTORUN_KERNEL_NAME) " for reading back autorun profile "
                                           "data";
      ERR_RET(CL_PROFILING_INFO_NOT_AVAILABLE, program->context, message);
    } else {
      uint64_t *readback_profile_data;
      readback_profile_data = (uint64_t *)acl_malloc(
          num_profile_counters * sizeof(uint64_t)); // see acl_kernel.c

      // Expected to be already allocated at kernel creation
      assert(readback_profile_data);

      physical_device_id = device_id->def.physical_device_id;
      accel_id = accel_def->id;

      acl_get_hal()->disable_profile_counters(physical_device_id, accel_id);
      // This for loop is needed to create a delay between counter disabling and
      // the start of shifting to allow tessellated counters to finish their
      // addition It is only needed for S10 and other familes that enable
      // tessellation - can be removed for other families
      for (i = 0; i < TESS_DELAY; i++) {
        acl_get_hal()->disable_profile_counters(physical_device_id, accel_id);
      }
      profiled_time = acl_get_hal()->get_timestamp();
      acl_get_hal()->get_profile_data(physical_device_id, accel_id,
                                      readback_profile_data,
                                      num_profile_counters);
      acl_get_hal()->set_profile_shared_control(physical_device_id, accel_id);
      acl_get_hal()->enable_profile_counters(physical_device_id, accel_id);

      // Get the curr shared counter
      int curr_shared_counters = get_env_profile_shared_counter_val();

      if (!write_profile_info_to_file_from_device(
              device_id, context, accel_def->iface.name.c_str(),
              readback_profile_data, ACL_DEVICE_OP_KERNEL, autorun_start_time,
              profiled_time, num_profile_counters, curr_shared_counters)) {
        return CL_OUT_OF_RESOURCES;
      }
    }
  }

  return CL_SUCCESS;
}

int acl_process_autorun_profiler_scan_chain(unsigned int physical_device_id,
                                            unsigned int accel_id) {
  if (!profile_enable)
    return 1;

  unsigned num_profile_counters = 0;
  uint64_t *profile_data = nullptr;
  std::string name = "";

  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }

  const acl_device_binary_t *binary =
      acl_get_platform()->device[physical_device_id].loaded_bin;
  if (binary == nullptr) {
    return 0;
  }
  const acl_accel_def_t *accel_def =
      binary->get_dev_prog()->get_kernel_accel_def(
          ACL_PROFILE_AUTORUN_KERNEL_NAME);
  if (accel_def == nullptr) {
    return 0;
  }

  num_profile_counters = accel_def->profiling_words_to_readback;
  name = accel_def->iface.name;

  // This kernel has profiling data, get it
  if (num_profile_counters) {
    profile_data =
        (uint64_t *)acl_malloc(num_profile_counters * sizeof(uint64_t));

    assert(profile_data);

    acl_get_hal()->get_profile_data(physical_device_id, accel_id, profile_data,
                                    num_profile_counters);
  } else {
    // There is no profiler data - nothing to print
    return 0;
  }

  // Get the curr shared counter
  int curr_shared_counters = get_env_profile_shared_counter_val();

  // Dump data to buffer
  dump_profile_data_to_buffer(
      (unsigned)num_profile_counters, (unsigned)physical_device_id, (unsigned)0,
      name.c_str(), profile_data, ACL_DEVICE_OP_KERNEL,
      (unsigned long long)autorun_start_time,
      (unsigned long long)acl_get_hal()->get_timestamp(), curr_shared_counters);

  if (profile_data) {
    acl_free(profile_data);
  }

  return 1;
}

int acl_process_profiler_scan_chain(acl_device_op_t *op) {

  if (!profile_enable)
    return 1;

  cl_event event;
  cl_kernel kernel;
  unsigned num_profile_counters;
  uint64_t *profile_data;
  unsigned int physical_device_id;
  unsigned int accel_id;
  cl_command_type op_type;
  _cl_command_queue *command_queue;
  cl_device_id device_id;
  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }

  char name[MAX_NAME_SIZE];
  num_profile_counters = 0;
  op_type = op->info.type;
  event = op->info.event;

  acl_print_debug_msg("acl_process_profiler_scan_chain\n");

  if (!acl_is_inside_sig()) {
    // Check event for validity.
    // This also transitively checks the context.
    if (!acl_event_is_valid(event)) {
      acl_print_debug_msg(
          "acl_process_profiler_scan_chain is called for an invalid event\n");
      return 0;
    }
    if (!acl_command_queue_is_valid(event->command_queue)) {
      acl_print_debug_msg("acl_process_profiler_scan_chain is called for an "
                          "event with an invalid command_queue\n");
      return 0;
    }
    if (acl_event_is_done(event)) {
      acl_print_debug_msg(
          "acl_process_profiler_scan_chain is called for a completed event\n");
      return 0;
    }
  }

  command_queue = event->command_queue;
  device_id = command_queue->device;

  if (!device_id) {
    acl_print_debug_msg(
        "acl_process_profiler_scan_chain is called for NULL device_id\n");
    return 0;
  }

  // this is not a kernel event and we are not printing timers
  // so nothing to print
  if (op_type != ACL_DEVICE_OP_KERNEL && profile_timer_on != 1) {
    return 0;
  }

  profile_data = 0;
  if (op_type == ACL_DEVICE_OP_KERNEL) {
    kernel = event->cmd.info.ndrange_kernel.kernel;

    // use autodiscovery info to find out how many words will be read from the
    // profiler
    num_profile_counters = kernel->accel_def->profiling_words_to_readback;

    if (num_profile_counters == 0 && profile_timer_on != 1) {
      // there is not profiler data and we are not printing timers
      // nothing to print
      return 0;
    }

    snprintf(name, MAX_NAME_SIZE, "%s", kernel->accel_def->iface.name.c_str());

    // this kernel has profiling data, get it
    if (num_profile_counters) {
      profile_data = kernel->profile_data;

      // expected to be already allocated at kernel creation
      assert(profile_data);

      physical_device_id =
          event->cmd.info.ndrange_kernel.device->def.physical_device_id;
      accel_id = event->cmd.info.ndrange_kernel.accel_id;

      acl_get_hal()->get_profile_data(physical_device_id, accel_id,
                                      profile_data, num_profile_counters);
    }
  } else if (profile_timer_on != 1) {
    // if ACL_PROFILE_TIMER is not set, do not print info about the rest of
    // the events
    return 0;
  } else if (op_type == ACL_DEVICE_OP_MEM_TRANSFER_COPY) {
    snprintf(name, MAX_NAME_SIZE, ".mem_transfer_copy");
  } else if (op_type == ACL_DEVICE_OP_MEM_TRANSFER_READ) {
    snprintf(name, MAX_NAME_SIZE, ".mem_transfer_read");
  } else if (op_type == ACL_DEVICE_OP_MEM_TRANSFER_WRITE) {
    snprintf(name, MAX_NAME_SIZE, ".mem_transfer_write");
  } else if (op_type == ACL_DEVICE_OP_REPROGRAM) {
    snprintf(name, MAX_NAME_SIZE, ".reprogram");
  } else if (op_type == ACL_DEVICE_OP_MEM_MIGRATION) {
    snprintf(name, MAX_NAME_SIZE, ".mem_migration");
  } else if (op_type == ACL_DEVICE_OP_USM_MEMCPY) {
    snprintf(name, MAX_NAME_SIZE, ".usm_memcpy");
  } else if (op_type == ACL_DEVICE_OP_HOSTPIPE_READ) {
    snprintf(name, MAX_NAME_SIZE, ".hostpipe_read");
  } else if (op_type == ACL_DEVICE_OP_HOSTPIPE_WRITE) {
    snprintf(name, MAX_NAME_SIZE, ".hostpipe_write");
  } else if (op_type == ACL_DEVICE_OP_DEVICE_GLOBAL_READ) {
    snprintf(name, MAX_NAME_SIZE, ".device_global_read");
  } else if (op_type == ACL_DEVICE_OP_DEVICE_GLOBAL_WRITE) {
    snprintf(name, MAX_NAME_SIZE, ".device_global_write");
  } else {
    // Ignore unknown op_type (don't attempt to extract any profiling from it or
    // get timestamps)
    acl_print_debug_msg("Unknown device op type: '%d'\n", int(op_type));
    return 0;
  }

  // For temporal profiling, we want to record the time at which the profiler
  // counters were read back, or the kernel finish time (if available).  For
  // non-temporal profiling we will only have the finish time.
  unsigned long long end_time = op->timestamp[CL_COMPLETE];
  if (end_time == 0)
    end_time = acl_get_hal()->get_timestamp();

  // Get the shared counter value (or set to -1 if its not set/out of expected
  // bounds)
  int curr_shared_counter = get_env_profile_shared_counter_val();

  // Dump data to buffer
  dump_profile_data_to_buffer(
      (unsigned)num_profile_counters, (unsigned)device_id->id,
      (unsigned)command_queue->id, name, profile_data, op_type,
      (unsigned long long)op->timestamp[CL_RUNNING], end_time,
      curr_shared_counter);

  if (op->timestamp[CL_COMPLETE] != 0) {
    // Dump buffer to file since this is the last one.
    dump_profile_buffer_to_file();
  }

  return 1;
}

int acl_open_profiler_file() {
  acl_assert_locked();
  if (!profile_enable)
    return 0;
  if (opened_count > 0) {
    opened_count += 1;
    return 1;
  }
  if (!pfile_was_opened) {
// First time opening the file
#ifdef _WIN32
    pfile_fd = _sopen(ACL_PROFILER_OUTPUT_FILENAME,
                      _O_WRONLY | _O_CREAT | _O_TRUNC, _SH_DENYWR, _S_IWRITE);
#else // Linux
    pfile_fd = open(ACL_PROFILER_OUTPUT_FILENAME, O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR | S_IRGRP);
#endif
  } else {
#ifdef _WIN32
    pfile_fd =
        _sopen(ACL_PROFILER_OUTPUT_FILENAME, _O_WRONLY | O_APPEND, _SH_DENYWR);
#else // Linux
    pfile_fd = open(ACL_PROFILER_OUTPUT_FILENAME, O_WRONLY | O_APPEND);
#endif
  }
  if (pfile_fd < 0) {
    acl_print_debug_msg("Could not open profiler output file: '%s'!\n",
                        ACL_PROFILER_OUTPUT_FILENAME);
    failed_to_open_count += 1;
    return 0;
  }
  opened_count += 1;
  pfile_was_opened = true;
  return 1;
}

int acl_close_profiler_file() {
  acl_assert_locked();
  if (!profile_enable)
    return 1;
  assert(opened_count + failed_to_open_count >= 1);
  if (failed_to_open_count > 0) {
    failed_to_open_count -= 1;
    return 1;
  } else if (opened_count > 1) {
    opened_count -= 1;
    return 1;
  } else {

#ifdef _WIN32
    _close(pfile_fd);
#else // Linux
    close(pfile_fd);
#endif

    opened_count -= 1;
    return 1;
  }
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

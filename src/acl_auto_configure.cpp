// Copyright (C) 2011-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4255)
#endif

// System headers.
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Internal headers.
#include <acl_auto.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_util.h>
#include <acl_version.h>
#include <unref.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <acl_auto_configure.h>
#include <acl_auto_configure_version.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

inline void decrement_section_counters(std::vector<int> &counters) {
  for (auto &x : counters) {
    x--;
  }
}

inline void check_section_counters(std::vector<int> &counters) {
  for (auto &x : counters) {
    assert(x >= 0 && "The number of fields should be positive");
  }
}
// Reads the next word in str starting from start_pos. Stores the word in result
// with leading and trailing whitespace removed. Returns the position
// immediately following the word that was read or std::string::npos if the end
// of string is reached.
static std::string::size_type read_word(const std::string &str,
                                        const std::string::size_type start_pos,
                                        std::string &result) noexcept {
  auto string_start = str.find_first_not_of(' ', start_pos);
  auto string_end = str.find(' ', string_start);
  if (string_start == std::string::npos) {
    result == "";
    return std::string::npos;
  }
  result = str.substr(string_start, string_end - string_start);

  return string_end;
}

// Reads the next word in str and converts it into an unsigned.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
// This is only used for getting the version_id as version_id is not counted
// in any of the forward compatible sections
static bool read_uint(const std::string &str, std::string::size_type &pos,
                      unsigned &val) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  try {
    val = static_cast<unsigned>(std::stoi(result));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into an unsigned.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_uint_counters(const std::string &str,
                               std::string::size_type &pos, unsigned &val,
                               std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    val = static_cast<unsigned>(std::stoi(result));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into a boolean.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_bool_counters(const std::string &str,
                               std::string::size_type &pos, bool &val,
                               std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    val = static_cast<bool>(std::stoi(result));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into an unsigned.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_uint32_counters(const std::string &str,
                                 std::string::size_type &pos, uint32_t &val,
                                 std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    val = static_cast<uint32_t>(std::stoul(result));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into an unsigned 64-bit
// fixed-length integer. Note this read utilizes stoull and fail if
// unsigned long long is not 64-bit long on the platform.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_uint64_counters(const std::string &str, int base,
                                 std::string::size_type &pos, uint64_t &val,
                                 std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    static_assert(sizeof(uint64_t) == sizeof(unsigned long long));
    val = static_cast<uint64_t>(std::stoull(result, nullptr, base));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into an unsigned.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_int_counters(const std::string &str,
                              std::string::size_type &pos, int &val,
                              std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    val = std::stoi(result);
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into a unsigned long long.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_ulonglong_counters(const std::string &str,
                                    std::string::size_type &pos,
                                    unsigned long long &val,
                                    std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    val = std::stoull(result);
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and converts it into a uintptr_t.
// Returns true if a valid integer was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word
// even if an error occurs.
static bool read_uintptr_counters(const std::string &str,
                                  std::string::size_type &pos, uintptr_t &val,
                                  std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);

  size_t end = 0;
  unsigned long long parsed;
  try {
    parsed = std::stoull(result, &end);
  } catch (const std::exception &) {
    return false;
  }
  if (end != result.size()) {
    return false;
  }

  val = static_cast<uintptr_t>(parsed);
  // To make sure the cast work
  // As `unsigned long long` might have difference size comparing to `uintptr_t
  if (val != parsed) {
    return false;
  }

  return true;
}

// Reads the next word in str and converts it into an unsigned or using its
// default value. Returns true if a valid integer was read or false if an error
// occurred. pos is updated to the position immediately following the parsed
// word even if an error occurs.
static bool read_uint_def_counters(const std::string &str,
                                   std::string::size_type &pos, unsigned &val,
                                   unsigned def_val,
                                   std::vector<int> &counters) noexcept {
  std::string result;
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  try {
    if (result == "?")
      val = def_val;
    else
      val = static_cast<unsigned>(std::stoi(result));
  } catch (const std::exception &e) {
    UNREFERENCED_PARAMETER(e);
    return false;
  }
  return true;
}

// Reads the next word in str and stores it in result.
// Returns true if a non-empty substring was read or false if an error occurred.
// pos is updated to the position immediately following the parsed word even if
// an occurred.
static int read_string_counters(const std::string &str,
                                std::string::size_type &pos,
                                std::string &result,
                                std::vector<int> &counters) noexcept {
  pos = read_word(str, pos, result);
  decrement_section_counters(counters);
  return result != "";
}

static bool
read_global_mem_defs(const std::string &config_str,
                     std::string::size_type &curr_pos,
                     unsigned int &num_global_mem_systems,
                     std::array<acl_system_global_mem_def_t, ACL_MAX_GLOBAL_MEM>
                         &global_mem_defs,
                     std::vector<int> &counters) noexcept {
  bool result = read_uint_counters(config_str, curr_pos, num_global_mem_systems,
                                   counters);

  for (auto i = 0U; result && (i < num_global_mem_systems); i++) {
    std::string gmem_name;
    // read total number of fields in global_memories
    int total_fields_global_memories = 0;
    result = read_int_counters(config_str, curr_pos,
                               total_fields_global_memories, counters);
    counters.emplace_back(total_fields_global_memories);

    // read global memory name
    if (result) {
      result = read_string_counters(config_str, curr_pos, gmem_name, counters);
    }

    // read global memory type
    auto gmem_type =
        static_cast<unsigned>(ACL_GLOBAL_MEM_DEVICE_PRIVATE); // Default
    if (result) {
      result = read_uint_counters(config_str, curr_pos, gmem_type, counters);
      if (gmem_type >= static_cast<unsigned>(ACL_GLOBAL_MEM_TYPE_COUNT))
        result = false;
    }

    auto num_dimms = 0U;
    auto configuration_address = 0ULL;
    auto burst_interleaved = 1U;
    std::uintptr_t gmem_start = 0, gmem_end = 0;
    acl_system_global_mem_allocation_type_t allocation_type =
        ACL_GLOBAL_MEM_UNDEFINED_ALLOCATION;
    std::string primary_interface;
    std::vector<std::string> can_access;
    if (result) {
      gmem_start = ~gmem_start;

      // read number of memory interfaces (DIMMS or banks) usable as device
      // global memory
      result = read_uint_counters(config_str, curr_pos, num_dimms, counters);

      if (result && num_dimms > 1) {
        // read memory configuration address
        result = read_ulonglong_counters(config_str, curr_pos,
                                         configuration_address, counters);
        // read whether the memory access is burst-interleaved across memory
        // interfaces
        if (result) {
          result = read_uint_counters(config_str, curr_pos, burst_interleaved,
                                      counters);
        }
      }

      int total_fields_memory_interface = 0;
      if (result) {
        result = read_int_counters(config_str, curr_pos,
                                   total_fields_memory_interface, counters);
      }

      // Find start and end address of global memory.
      // Assume memory range is contiguous, but start/end address pairs for
      // each DIMM can be in any order.
      for (auto j = 0U; result && (j < num_dimms); j++) {
        counters.emplace_back(total_fields_memory_interface);
        auto cur_gmem_start = 0ULL;
        auto cur_gmem_end = 0ULL;
        result = read_ulonglong_counters(config_str, curr_pos, cur_gmem_start,
                                         counters) &&
                 read_ulonglong_counters(config_str, curr_pos, cur_gmem_end,
                                         counters);
        if (gmem_start > cur_gmem_start)
          gmem_start = static_cast<std::uintptr_t>(cur_gmem_start);
        if (gmem_end < cur_gmem_end)
          gmem_end = static_cast<std::uintptr_t>(cur_gmem_end);

        /*****************************************************************
          Since the introduction of autodiscovery forwards-compatibility,
          new entries for the 'global memory interface' section start here
         ****************************************************************/

        // forward compatibility: bypassing remaining fields at the end of
        // memory interface
        while (result && counters.size() > 0 &&
               counters.back() > 0) { // total_fields_memory_interface>0
          std::string tmp;
          result = result &&
                   read_string_counters(config_str, curr_pos, tmp, counters);
          check_section_counters(counters);
        }
        counters.pop_back(); // removing total_fields_memory_interface from
                             // the list
      }

      /*****************************************************************
        Since the introduction of autodiscovery forwards-compatibility,
        new entries for the 'global memory' section start here.
       ****************************************************************/

      // read memory allocation_type
      // These are new since the addition of forward compatibility; it is
      // important that they come after all global memory fields included
      // in version 23, when forward compatibility was added.
      // Only try to read these if there are values left to read in the
      // global memory subsection.
      if (result && counters.back() > 0) {
        auto alloc_type = 0U;
        result = read_uint_counters(config_str, curr_pos, alloc_type, counters);
        allocation_type =
            static_cast<acl_system_global_mem_allocation_type_t>(alloc_type);
      }

      // read memory primary interface
      if (result && counters.back() > 0) {
        result = read_string_counters(config_str, curr_pos, primary_interface,
                                      counters);
        if (result && primary_interface == "-")
          primary_interface = "";
      }

      // read size of memory can access list
      if (result && counters.back() > 0) {
        unsigned can_access_count = 0U;
        result = read_uint_counters(config_str, curr_pos, can_access_count,
                                    counters);
        while (result && can_access_count-- && counters.size() > 0 &&
               counters.back() > 0) {
          std::string temp;
          result = read_string_counters(config_str, curr_pos, temp, counters);
          can_access.push_back(temp);
        }
      }
    }

    if (result) {
      // Global memory definition can't change across reprograms.
      // If global memory definition changed, allocations will get messed up.
      // The check won't be done here though. It will need to be done by the
      // callers.
      //
      // IMPORTANT: If a new field is added here, make sure that field is
      // also copied in acl_program.cpp:l_device_memory_definition_copy().
      // For built-in kernels (and CL_CONTEXT_COMPILER_MODE=3), memory
      // definition is copied from the one loaded from autodiscovery ROM
      // to new program object's device definition.
      global_mem_defs[i].num_global_banks = num_dimms;
      global_mem_defs[i].config_addr =
          static_cast<size_t>(configuration_address);
      global_mem_defs[i].name = gmem_name;
      global_mem_defs[i].range.begin = reinterpret_cast<void *>(gmem_start);
      global_mem_defs[i].range.next = reinterpret_cast<void *>(gmem_end);
      global_mem_defs[i].type =
          static_cast<acl_system_global_mem_type_t>(gmem_type);
      global_mem_defs[i].burst_interleaved = burst_interleaved;
      global_mem_defs[i].allocation_type = allocation_type;
      global_mem_defs[i].primary_interface = primary_interface;
      global_mem_defs[i].can_access_list = can_access;
    }

    // forward compatibility: bypassing remaining fields at the end of global
    // memory
    while (result && counters.size() > 0 &&
           counters.back() > 0) { // total_fields_global_memories>0
      std::string tmp;
      result =
          result && read_string_counters(config_str, curr_pos, tmp, counters);
      check_section_counters(counters);
    }
    counters.pop_back(); // removing total_fields_global_memories
  }

  return result;
}

static bool
read_hostpipe_infos(const std::string &config_str,
                    std::string::size_type &curr_pos,
                    std::vector<acl_hostpipe_info_t> &hostpipe_infos,
                    std::vector<int> &counters) noexcept {
  auto num_hostpipes = 0U;
  bool result =
      read_uint_counters(config_str, curr_pos, num_hostpipes, counters);

  // read total number of fields in hostpipes
  int total_fields_hostpipes = 0;
  if (result) {
    result = read_int_counters(config_str, curr_pos, total_fields_hostpipes,
                               counters);
  }

  for (unsigned i = 0; result && (i < num_hostpipes); i++) {
    counters.emplace_back(total_fields_hostpipes);
    std::string name;

    auto hostpipe_is_host_to_dev = 0U;
    auto hostpipe_is_dev_to_host = 0U;
    auto hostpipe_width = 0U;
    auto hostpipe_max_buffer_depth = 0U;
    result =
        result && read_string_counters(config_str, curr_pos, name, counters) &&
        read_uint_counters(config_str, curr_pos, hostpipe_is_host_to_dev,
                           counters) &&
        read_uint_counters(config_str, curr_pos, hostpipe_is_dev_to_host,
                           counters) &&
        read_uint_counters(config_str, curr_pos, hostpipe_width, counters) &&
        read_uint_counters(config_str, curr_pos, hostpipe_max_buffer_depth,
                           counters);
    // is_host_to_dev and is_dev_to_host are exclusive because of the enum
    // Type
    acl_hostpipe_info_t acl_hostpipe_info;
    acl_hostpipe_info.name = name;
    acl_hostpipe_info.is_host_to_dev = hostpipe_is_host_to_dev;
    acl_hostpipe_info.is_dev_to_host = hostpipe_is_dev_to_host;
    acl_hostpipe_info.data_width = hostpipe_width;
    acl_hostpipe_info.max_buffer_depth = hostpipe_max_buffer_depth;
    hostpipe_infos.push_back(acl_hostpipe_info);

    /*****************************************************************
      Since the introduction of autodiscovery forwards-compatibility,
      new entries for the 'hostpipe' section start here.
     ****************************************************************/

    // forward compatibility: bypassing remaining fields at the end of
    // hostpipes
    while (result && counters.size() > 0 &&
           counters.back() > 0) { // total_fields_hostpipes>0
      std::string tmp;
      result =
          result && read_string_counters(config_str, curr_pos, tmp, counters);
      check_section_counters(counters);
    }
    counters.pop_back(); // removing total_fields_hostpipes
  }

  return result;
}

static bool read_device_global_mem_defs(
    const std::string &config_str, std::string::size_type &curr_pos,
    std::unordered_map<std::string, acl_device_global_mem_def_t>
        &device_global_mem_defs,
    std::vector<int> &counters, std::string &err_str) noexcept {
  unsigned int num_device_global = 0;
  bool result =
      read_uint_counters(config_str, curr_pos, num_device_global, counters);

  // read total number of fields in device global
  unsigned int total_fields_device_global = 0;
  if (result) {
    result = read_uint_counters(config_str, curr_pos,
                                total_fields_device_global, counters);
  }

  // Clean up any residual information first
  device_global_mem_defs.clear();

  for (auto i = 0U; result && (i < num_device_global); i++) {
    counters.emplace_back(total_fields_device_global);

    // read device global name
    std::string device_global_name;
    if (result && counters.back() > 0) {
      result = read_string_counters(config_str, curr_pos, device_global_name,
                                    counters);
    }

    // read device global address
    uint64_t dev_global_addr = 0; // Default
    if (result && counters.back() > 0) {
      // Parse in base 16
      result = read_uint64_counters(config_str, 16, curr_pos, dev_global_addr,
                                    counters);
    }
    // read device global address size
    uint32_t dev_global_size = 0; // Default
    if (result && counters.back() > 0) {
      result =
          read_uint32_counters(config_str, curr_pos, dev_global_size, counters);
    }

    // read device global properties
    auto host_access =
        static_cast<unsigned>(ACL_DEVICE_GLOBAL_HOST_ACCESS_READ_WRITE);
    if (result && counters.back() > 0) {
      result = read_uint_counters(config_str, curr_pos, host_access, counters);
      if (host_access >=
          static_cast<unsigned>(ACL_DEVICE_GLOBAL_HOST_ACCESS_TYPE_COUNT))
        result = false;
    }
    bool can_skip_programming = false;
    if (result && counters.back() > 0) {
      result = read_bool_counters(config_str, curr_pos, can_skip_programming,
                                  counters);
    }
    bool implement_in_csr = false;
    if (result && counters.back() > 0) {
      result =
          read_bool_counters(config_str, curr_pos, implement_in_csr, counters);
    }
    bool reset_on_reuse = false;
    if (result && counters.back() > 0) {
      result =
          read_bool_counters(config_str, curr_pos, reset_on_reuse, counters);
    }

    acl_device_global_mem_def_t dev_global_def = {
        dev_global_addr,
        dev_global_size,
        static_cast<acl_device_global_host_access_t>(host_access),
        can_skip_programming,
        implement_in_csr,
        reset_on_reuse};
    bool ok =
        device_global_mem_defs.insert({device_global_name, dev_global_def})
            .second;
    if (!ok) {
      // Device global name already exist in map, but it should have been
      // unique.
      std::stringstream err_ss;
      err_ss << "Device global name should be unique. " << device_global_name
             << " is repeated.\n";
      err_str = err_ss.str();
      result = false;
    }

    // forward compatibility: bypassing remaining fields at the end of device
    // global memory
    while (result && counters.size() > 0 &&
           counters.back() > 0) { // total_fields_device_global>0
      std::string tmp;
      result =
          result && read_string_counters(config_str, curr_pos, tmp, counters);
      check_section_counters(counters);
    }
    counters.pop_back(); // removing total_fields_device_global
  }

  return result;
}

static bool read_streaming_kernel_arg_info(
    const std::string &config_str, std::string::size_type &curr_pos,
    bool &streaming_arg_info_available,
    acl_streaming_kernel_arg_info &streaming_arg_info,
    std::vector<int> &counters) noexcept {
  unsigned int value = 0;
  bool result = read_uint_counters(config_str, curr_pos, value, counters);
  streaming_arg_info_available = value;

  if (result && streaming_arg_info_available) {
    streaming_arg_info = acl_streaming_kernel_arg_info{};
    result = read_string_counters(config_str, curr_pos,
                                  streaming_arg_info.interface_name, counters);
  }
  return result;
}

static bool read_hostpipe_mappings(
    const std::string &config_str, std::string::size_type &curr_pos,
    std::vector<acl_hostpipe_mapping> &hostpipe_mappings,
    std::vector<int> &counters, std::string &err_str) noexcept {
  unsigned int num_mappings = 0;
  bool result =
      read_uint_counters(config_str, curr_pos, num_mappings, counters);

  unsigned int num_fields_per_mapping = 0;
  if (result) {
    result = read_uint_counters(config_str, curr_pos, num_fields_per_mapping,
                                counters);
  }

  for (unsigned int i = 0; result && (i < num_mappings); i++) {
    counters.emplace_back(num_fields_per_mapping);

    acl_hostpipe_mapping mapping{};
    result =
        read_string_counters(config_str, curr_pos, mapping.logical_name,
                             counters) &&
        read_string_counters(config_str, curr_pos, mapping.physical_name,
                             counters) &&
        read_bool_counters(config_str, curr_pos, mapping.implement_in_csr,
                           counters) &&
        read_string_counters(config_str, curr_pos, mapping.csr_address,
                             counters) &&
        read_bool_counters(config_str, curr_pos, mapping.is_read, counters) &&
        read_bool_counters(config_str, curr_pos, mapping.is_write, counters) &&
        read_uint_counters(config_str, curr_pos, mapping.pipe_width,
                           counters) &&
        read_uint_counters(config_str, curr_pos, mapping.pipe_depth, counters);

    hostpipe_mappings.emplace_back(mapping);

    while (result && counters.back() > 0) {
      std::string tmp;
      result = read_string_counters(config_str, curr_pos, tmp, counters);
    }
    check_section_counters(counters);
    counters.pop_back();
  }

  return result;
}

static bool read_kernel_args(const std::string &config_str,
                             const bool kernel_arg_info_available,
                             std::string::size_type &curr_pos,
                             std::vector<acl_kernel_arg_info_t> &args,
                             std::vector<int> &counters) noexcept {
  // Get the number of parameters
  auto num_args = 0U;
  bool result = read_uint_counters(config_str, curr_pos, num_args, counters);

  if (result) {
    args = std::vector<acl_kernel_arg_info_t>(num_args);
  }

  for (auto j = 0U; result && (j < num_args); j++) {
    auto addr_space_type = 0U;
    auto category = 0U;
    auto size = 0U;
    int total_fields_arguments = 0;
    if (result) {
      result = result && read_int_counters(config_str, curr_pos,
                                           total_fields_arguments, counters);
    }
    counters.emplace_back(total_fields_arguments);
    unsigned alignment = ACL_MEM_ALIGN; // Set default to 1024 bytes
    result =
        result &&
        read_uint_counters(config_str, curr_pos, addr_space_type, counters) &&
        read_uint_counters(config_str, curr_pos, category, counters) &&
        read_uint_counters(config_str, curr_pos, size, counters);
    if (result) {
      result = result &&
               read_uint_counters(config_str, curr_pos, alignment, counters);
    }

    std::string buffer_location = "";
    if (result) {
      unsigned int num_buffer_locations = 0;
      result = result && read_uint_counters(config_str, curr_pos,
                                            num_buffer_locations, counters);
      for (unsigned int k = 0; result && (k < num_buffer_locations); k++) {
        result = result && read_string_counters(config_str, curr_pos,
                                                buffer_location, counters);
      }
      if (result && num_buffer_locations > 1) {
        std::cerr << "WARNING: kernel argument " << j
                  << " has multiple buffer_location attributes which is not "
                     "supported.\nSelecting "
                  << buffer_location << " as buffer location.\n";
      }
    }

    // Only local mem contains the following params
    auto aspace_id = 0U;
    auto lmem_size_bytes = 0U;
    if (result && (addr_space_type == ACL_ARG_ADDR_LOCAL)) {
      result =
          result &&
          read_uint_counters(config_str, curr_pos, aspace_id, counters) &&
          read_uint_counters(config_str, curr_pos, lmem_size_bytes, counters);
    }

    auto type_qualifier = 0U;
    auto host_accessible = 0U;
    std::string pipe_channel_id;
    if (result) {
      result = result && read_uint_counters(config_str, curr_pos,
                                            type_qualifier, counters);
      if (result && (type_qualifier == ACL_ARG_TYPE_PIPE)) {
        result = result && read_uint_counters(config_str, curr_pos,
                                              host_accessible, counters);
        if (result && host_accessible) {
          result = result && read_string_counters(config_str, curr_pos,
                                                  pipe_channel_id, counters);
        }
      }
    }

    std::string name = "";
    std::string type_name = "";
    auto access_qualifier = 0U;
    if (kernel_arg_info_available) {
      if (result) {
        result =
            result &&
            read_string_counters(config_str, curr_pos, name, counters) &&
            read_string_counters(config_str, curr_pos, type_name, counters) &&
            read_uint_counters(config_str, curr_pos, access_qualifier,
                               counters);
      }
      if (type_name == "0")
        type_name = "";
    }

    bool streaming_arg_info_available = false;
    acl_streaming_kernel_arg_info streaming_arg_info;
    if (result && counters.back() > 0) {
      result = read_streaming_kernel_arg_info(config_str, curr_pos,
                                              streaming_arg_info_available,
                                              streaming_arg_info, counters);
    }

    /*****************************************************************
      Since the introduction of autodiscovery forwards-compatibility,
      new entries for each kernel argument section start here.
     ****************************************************************/

    if (result) {
      args[j].name = name;
      args[j].addr_space =
          static_cast<acl_kernel_arg_addr_space_t>(addr_space_type);
      args[j].access_qualifier =
          static_cast<acl_kernel_arg_access_qualifier_t>(access_qualifier);
      args[j].category = static_cast<acl_kernel_arg_category_t>(category);
      args[j].size = size;
      args[j].alignment = alignment;
      args[j].aspace_number = aspace_id;
      args[j].lmem_size_bytes = lmem_size_bytes;
      args[j].type_name = type_name;
      args[j].type_qualifier =
          static_cast<acl_kernel_arg_type_qualifier_t>(type_qualifier);
      args[j].host_accessible = host_accessible;
      args[j].pipe_channel_id = pipe_channel_id;
      args[j].buffer_location = buffer_location;
      args[j].streaming_arg_info_available = streaming_arg_info_available;
      args[j].streaming_arg_info = streaming_arg_info;
    }
    // forward compatibility: bypassing remaining fields at the end of
    // arguments section
    while (result && counters.size() > 0 &&
           counters.back() > 0) { // total_fields_arguments>0
      std::string tmp;
      result =
          result && read_string_counters(config_str, curr_pos, tmp, counters);
      check_section_counters(counters);
    }
    counters.pop_back();
  }

  return result;
}

static bool read_streaming_kernel_control_info(
    const std::string &config_str, std::string::size_type &curr_pos,
    bool &streaming_control_info_available,
    acl_streaming_kernel_control_info &streaming_control_info,
    std::vector<int> &counters) noexcept {
  unsigned int value = 0;
  bool result = read_uint_counters(config_str, curr_pos, value, counters);
  streaming_control_info_available = value;

  if (result && streaming_control_info_available) {
    streaming_control_info = acl_streaming_kernel_control_info{};
    result = read_string_counters(config_str, curr_pos,
                                  streaming_control_info.start, counters) &&
             read_string_counters(config_str, curr_pos,
                                  streaming_control_info.done, counters);
  }

  return result;
}

static bool read_accel_defs(const std::string &config_str,
                            std::string::size_type &curr_pos,
                            const bool kernel_arg_info_available,
                            std::vector<acl_accel_def_t> &accel,
                            std::vector<acl_hal_accel_def_t> &hal_info,
                            std::vector<int> &counters,
                            std::string &err_str) noexcept {
  auto num_accel = 0U;
  bool result = read_uint_counters(config_str, curr_pos, num_accel, counters);
  if (result) {
    accel = std::vector<acl_accel_def_t>(num_accel);
    hal_info = std::vector<acl_hal_accel_def_t>(num_accel);
  }
  // Setup the accelerators
  for (auto i = 0U; result && (i < num_accel); i++) {
    accel[i].id = i;

    accel[i].mem.begin = reinterpret_cast<void *>(0);
    accel[i].mem.next = reinterpret_cast<void *>(0x00020000);

    int total_fields_kernel = 0;
    result = result && read_int_counters(config_str, curr_pos,
                                         total_fields_kernel, counters);
    counters.emplace_back(total_fields_kernel);

    result = result && read_string_counters(config_str, curr_pos,
                                            hal_info[i].name, counters);

    if (!result)
      break;
    accel[i].iface.name = hal_info[i].name;

    // Get kernel CRA address and range.
    // The address is the offset from the CRA address of the first kernel CRA.
    // That first kernel CRA comes after, for example, the PCIE CRA.
    result = result &&
             read_uint_counters(config_str, curr_pos, hal_info[i].csr.address,
                                counters) &&
             read_uint_counters(config_str, curr_pos, hal_info[i].csr.num_bytes,
                                counters);

    result = result && read_uint_counters(config_str, curr_pos,
                                          accel[i].fast_launch_depth, counters);

    // Get the kernel performance monitor address and range - used for
    // profiling.  If the performance monitor is not instantiated, the
    // range field here will be 0.
    result = result &&
             read_uint_counters(config_str, curr_pos,
                                hal_info[i].perf_mon.address, counters) &&
             read_uint_counters(config_str, curr_pos,
                                hal_info[i].perf_mon.num_bytes, counters);

    // Determine whether the kernel is workgroup-invariant.
    result =
        result && read_uint_counters(config_str, curr_pos,
                                     accel[i].is_workgroup_invariant, counters);

    // Determine whether the kernel is workitem-invariant.
    result =
        result && read_uint_counters(config_str, curr_pos,
                                     accel[i].is_workitem_invariant, counters);
    if (!accel[i].is_workgroup_invariant && accel[i].is_workitem_invariant) {
      std::stringstream err_ss;
      err_ss << "FAILED to read auto-discovery string at byte " << curr_pos
             << ": kernel cannot be workitem-invariant while it is "
                "workgroup-variant. "
                "Full auto-discovery string value is "
             << config_str << "\n";
      err_str = err_ss.str();
      result = false;
    }

    // Determine whether the kernel is vectorized.
    result = result && read_uint_counters(config_str, curr_pos,
                                          accel[i].num_vector_lanes, counters);

    // Determine how much profiling data is available in the kernel
    result = result &&
             read_uint_counters(config_str, curr_pos,
                                accel[i].profiling_words_to_readback, counters);

    result =
        result && read_kernel_args(config_str, kernel_arg_info_available,
                                   curr_pos, accel[i].iface.args, counters);

    // Get the number of printf format strings
    auto num_printf_format_strings = 0U;
    result = result && read_uint_counters(config_str, curr_pos,
                                          num_printf_format_strings, counters);
    accel[i].printf_format_info =
        std::vector<acl_printf_info_t>(num_printf_format_strings);

    // Disable fast relaunch when kernel has printf
    if (accel[i].printf_format_info.size() > 0) {
      accel[i].fast_launch_depth = 0;
    }

    // Get the arguments themselves
    int total_fields_printf = 0;
    if (result) {
      result = read_int_counters(config_str, curr_pos, total_fields_printf,
                                 counters);
    }

    for (auto j = 0U; result && (j < accel[i].printf_format_info.size()); j++) {
      counters.emplace_back(total_fields_printf);
      result =
          read_uint_counters(config_str, curr_pos,
                             accel[i].printf_format_info[j].index, counters) &&
          read_string_counters(config_str, curr_pos,
                               accel[i].printf_format_info[j].format_string,
                               counters);

      /*******************************************************************
        Since the introduction of autodiscovery forwards-compatibility,
        new entries for each kernel's 'printf' section start here.
       ******************************************************************/

      // forward compatibility: bypassing remaining fields at the end of
      // printf calls section
      while (result && counters.size() > 0 &&
             counters.back() > 0) { // fields_printf>0
        std::string tmp;
        result =
            result && read_string_counters(config_str, curr_pos, tmp, counters);
        check_section_counters(counters);
      }
      counters.pop_back();
    }

    // Read the number of local mem systems, then aspaceID and static
    // demand for each.
    if (result) {
      auto num_local_aspaces = 0U;
      result =
          read_uint_counters(config_str, curr_pos, num_local_aspaces, counters);

      int total_fields_local_aspaces = 0;
      // Read the number of fields in local mem systems
      if (result) {
        result = read_int_counters(config_str, curr_pos,
                                   total_fields_local_aspaces, counters);
      }
      accel[i].local_aspaces =
          std::vector<acl_local_aspace_info>(num_local_aspaces);

      for (auto it = 0U; it < num_local_aspaces && result; ++it) {
        counters.emplace_back(total_fields_local_aspaces);
        result = read_uint_counters(config_str, curr_pos,
                                    accel[i].local_aspaces[it].aspace_id,
                                    counters) &&
                 read_uint_counters(config_str, curr_pos,
                                    accel[i].local_aspaces[it].static_demand,
                                    counters);

        /****************************************************************
          Since the introduction of autodiscovery forwards-compatibility,
          new entries for each kernel's 'local memory systems' section
          start here.
         ***************************************************************/

        // forward compatibility: bypassing remaining fields at the end of
        // local mem system section
        while (result && counters.size() > 0 &&
               counters.back() > 0) { // fields_local_aspaces>0
          std::string tmp;
          result = result &&
                   read_string_counters(config_str, curr_pos, tmp, counters);
          check_section_counters(counters);
        }
        counters.pop_back();
      }
    }

    // Parse kernel attribute reqd_work_group_size.
    if (result) {
      std::vector<unsigned> wgs = {0U, 0U, 0U};
      result = read_uint_counters(config_str, curr_pos, wgs[0], counters) &&
               read_uint_counters(config_str, curr_pos, wgs[1], counters) &&
               read_uint_counters(config_str, curr_pos, wgs[2], counters);

      accel[i].compile_work_group_size[0] = wgs[0];
      accel[i].compile_work_group_size[1] = wgs[1];
      accel[i].compile_work_group_size[2] = wgs[2];
    }

    accel[i].max_work_group_size_arr[0] = 0;
    accel[i].max_work_group_size_arr[1] = 0;
    accel[i].max_work_group_size_arr[2] = 0;

    // Parse kernel attribute max_work_group_size.
    if (result) {
      auto num_vals = 0U;
      result = read_uint_counters(config_str, curr_pos, num_vals, counters);
      if (result) {
        // OpenCL supports only 3 dimensions in specifying work-group size
        assert(
            num_vals <= 3 &&
            "Unsupported number of Maximum work-group size values specified");
        accel[i].max_work_group_size = 1;
        auto n = 0U;
        while (result && n < num_vals) {
          auto max_work_group_size_val = 0U;
          result =
              result && read_uint_counters(config_str, curr_pos,
                                           max_work_group_size_val, counters);

          accel[i].max_work_group_size_arr[n] = max_work_group_size_val;
          accel[i].max_work_group_size *= max_work_group_size_val;
          n++;
        }
      }
    }

    if (result) {
      result = read_uint_counters(config_str, curr_pos,
                                  accel[i].max_global_work_dim, counters);
    }

    if (result) {
      result = read_uint_counters(config_str, curr_pos,
                                  accel[i].uses_global_work_offset, counters);
    }

    /*******************************************************************
      Since the introduction of autodiscovery forwards-compatibility,
      new entries for each 'kernel description' section start here.
     ******************************************************************/
    accel[i].is_sycl_compile = 0; // Initializing for backward compatability
    if (result && counters.back() > 0) {
      result = read_uint_counters(config_str, curr_pos,
                                  accel[i].is_sycl_compile, counters);
    }

    if (result && counters.back() > 0) {
      result = read_streaming_kernel_control_info(
          config_str, curr_pos, accel[i].streaming_control_info_available,
          accel[i].streaming_control_info, counters);
    }

    // forward compatibility: bypassing remaining fields at the end of kernel
    // description section
    while (result && counters.size() > 0 &&
           counters.back() > 0) { // total_fields_kernel>0
      std::string tmp;
      result =
          result && read_string_counters(config_str, curr_pos, tmp, counters);
      check_section_counters(counters);
    }
    counters.pop_back();
  }

  if (!result) {
    accel.clear();
    hal_info.clear();
  }

  return result;
}

bool acl_load_device_def_from_str(const std::string &config_str,
                                  acl_device_def_autodiscovery_t &devdef,
                                  std::string &err_str) noexcept {
  acl_assert_locked();

  bool result = !config_str.empty();
  err_str.clear();

  std::string::size_type curr_pos = 0;
  int total_fields = 0;
  std::vector<int> counters;

  if (!result) {
    std::stringstream err_ss;
    err_ss << "FAILED to read auto-discovery string at byte " << curr_pos
           << ": Expected non-zero value. Full string value is " << config_str
           << "\n";
    err_str = err_ss.str();
  }

  auto version_id = 0U;
  if (result) {
    result = read_uint(config_str, curr_pos, version_id);
  }

  if (result) {
    if (version_id > static_cast<unsigned>(ACL_AUTO_CONFIGURE_VERSIONID) ||
        version_id <
            static_cast<unsigned>(
                ACL_AUTO_CONFIGURE_BACKWARDS_COMPATIBLE_WITH_VERSIONID)) {
      result = false;
      std::stringstream err_ss;
      err_ss << "Error: The accelerator hardware currently programmed is "
                "incompatible with this\nversion of the runtime (" ACL_VERSION
                " Commit " ACL_GIT_COMMIT ")."
                " Recompile the hardware with\nthe same version of the "
                "compiler and program that onto the board.\n";
      err_str = err_ss.str();
    }
  }

  /*********************************************************************************************************************************************
   *                                                      STOP!!!! *
   *                                                                                                                                           *
   * If some add new fields is added, they will be at the end of the
   *corresponding section or subsection                                       *
   * Section: device and kernel * Subsection: e.g., kernel arguments, global
   *mem, ... * If there is a field that no longer in use, it has ?
   *(question-mark) as its data. * This rule is only applying to the fields that
   *are not string *
   *********************************************************************************************************************************************/
  // read total number of fields in device description
  if (result) {
    result = read_int_counters(config_str, curr_pos, total_fields, counters);
  }
  counters.emplace_back(total_fields);

  // Read rand_hash.
  if (result) {
    result = read_string_counters(config_str, curr_pos, devdef.binary_rand_hash,
                                  counters);
  }

  // Read board name.
  if (result) {
    result = read_string_counters(config_str, curr_pos, devdef.name, counters);
  }

  // Check endianness field
  if (result) {
    result = read_uint_def_counters(config_str, curr_pos, devdef.is_big_endian,
                                    0, counters);
    if (devdef.is_big_endian > 1)
      result = false;
  }

  // Set up device global memories
  if (result) {
    result = read_global_mem_defs(config_str, curr_pos,
                                  devdef.num_global_mem_systems,
                                  devdef.global_mem_defs, counters);
  }

  // Set up hostpipe information
  if (result) {
    result = read_hostpipe_infos(config_str, curr_pos, devdef.acl_hostpipe_info,
                                 counters);
  }

  /*****************************************************************
    Since the introduction of autodiscovery forwards-compatibility,
    new entries for the 'device' section start here.
   ****************************************************************/

  auto kernel_arg_info_available = 0U;
  if (result && counters.back() > 0) {
    result = read_uint_counters(config_str, curr_pos, kernel_arg_info_available,
                                counters);
  }

  // Read device global information.
  if (result && counters.back() > 0) {
    result = read_device_global_mem_defs(
        config_str, curr_pos, devdef.device_global_mem_defs, counters, err_str);
  }

  // Check whether csr_ring_root exist in the IP.
  if (result && counters.back() > 0) {
    result = read_bool_counters(config_str, curr_pos,
                                devdef.cra_ring_root_exist, counters);
  }

  // Read program scoped hostpipes mappings
  if (result && counters.back() > 0) {
    result = read_hostpipe_mappings(
        config_str, curr_pos, devdef.hostpipe_mappings, counters, err_str);
  }

  // forward compatibility: bypassing remaining fields at the end of device
  // description section
  while (result && counters.size() > 0 &&
         counters.back() > 0) { // total_fields>0
    std::string tmp;
    result =
        result && read_string_counters(config_str, curr_pos, tmp, counters);
    check_section_counters(counters);
  }
  counters.pop_back(); // removing total_fields

  // Set up kernel description
  if (result) {
    result = read_accel_defs(config_str, curr_pos, kernel_arg_info_available,
                             devdef.accel, devdef.hal_info, counters, err_str);
  }

  if (!result && err_str.empty()) {
    std::stringstream err_ss;
    err_ss << "FAILED to read auto-discovery string at byte " << curr_pos
           << ". Full auto-discovery string value is " << config_str << "\n";
    err_str = err_ss.str();
  }

  return result;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

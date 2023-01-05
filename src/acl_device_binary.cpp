// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
// To determine endianness of Linux host system
#ifdef __linux__
#include <endian.h>
#endif
#include <fstream>
#include <sstream>

// External library headers.
#include <pkg_editor/pkg_editor.h>

// Internal headers.
#include <acl_auto_configure.h>
#include <acl_device_binary.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_kernel.h>
#include <acl_support.h>
#include <acl_util.h>

// Use endian.h to determine system endianness under Linux.  Assume that
// other platforms (Windows) are always little endian.
static unsigned l_is_host_big_endian(void) {
#ifdef _WIN32 // Not Linux - we don't support big endian hosts on other
              // platforms
  return 0;
#else // We only have endian.h under Linux
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return 0;
#elif __BYTE_ORDER == __BIG_ENDIAN
  return 1;
#endif
#endif
}

// Compares global memory definition of two devices.
// Returns CL_SUCCESS if they are exactly the same.
// Returns CL_INVALID_BINARY if any parameter are different.
static cl_int l_compare_device_global_mem_defs(
    cl_context context, const acl_device_def_autodiscovery_t *orig_device_def,
    const acl_device_def_autodiscovery_t *new_device_def) {
  cl_int status = CL_SUCCESS;
  // The hal printf will only result in output if ACL_DEBUG is set to > 0
#define AND_CHECK_GLOBAL_MEM_DEF(CONDITION, ERRCODE, FAILMSG)                  \
  if (status == CL_SUCCESS && !(CONDITION)) {                                  \
    status = ERRCODE;                                                          \
    acl_print_debug_msg(FAILMSG);                                              \
    acl_context_callback(context, FAILMSG);                                    \
  }
  const char *incompatible_msg = "Device memory layout changes across binaries";
  AND_CHECK_GLOBAL_MEM_DEF(orig_device_def->num_global_mem_systems ==
                               new_device_def->num_global_mem_systems,
                           CL_INVALID_BINARY, incompatible_msg);

  // Loop over all memory systems and check compatibility
  for (unsigned i = 0; i < orig_device_def->num_global_mem_systems; i++) {
    incompatible_msg = "Device memory bank count changes across binaries";
    AND_CHECK_GLOBAL_MEM_DEF(
        orig_device_def->global_mem_defs[i].num_global_banks ==
            new_device_def->global_mem_defs[i].num_global_banks,
        CL_INVALID_BINARY, incompatible_msg);
    incompatible_msg = "Device memory type changes across binaries";
    AND_CHECK_GLOBAL_MEM_DEF(orig_device_def->global_mem_defs[i].type ==
                                 new_device_def->global_mem_defs[i].type,
                             CL_INVALID_BINARY, incompatible_msg);
    incompatible_msg = "Device memory range changes across binaries";
    AND_CHECK_GLOBAL_MEM_DEF(orig_device_def->global_mem_defs[i].range.begin ==
                                 new_device_def->global_mem_defs[i].range.begin,
                             CL_INVALID_BINARY, incompatible_msg);
    AND_CHECK_GLOBAL_MEM_DEF(orig_device_def->global_mem_defs[i].range.next ==
                                 new_device_def->global_mem_defs[i].range.next,
                             CL_INVALID_BINARY, incompatible_msg);

    if (new_device_def->global_mem_defs[i].num_global_banks > 1) {
      incompatible_msg = "Device memory config address changes across binaries";
      AND_CHECK_GLOBAL_MEM_DEF(
          orig_device_def->global_mem_defs[i].config_addr ==
              new_device_def->global_mem_defs[i].config_addr,
          CL_INVALID_BINARY, incompatible_msg);
    }

    if (!orig_device_def->global_mem_defs[i].name.empty() &&
        !new_device_def->global_mem_defs[i].name.empty()) {
      incompatible_msg = "Device memory name changes across binaries";
      AND_CHECK_GLOBAL_MEM_DEF(orig_device_def->global_mem_defs[i].name ==
                                   new_device_def->global_mem_defs[i].name,
                               CL_INVALID_BINARY, incompatible_msg);
    } else {
      incompatible_msg =
          "Device memory is heterogeneous but doesn't have a name";
      // Name can be not set if there's only 1 memory
      AND_CHECK_GLOBAL_MEM_DEF(new_device_def->num_global_mem_systems == 1,
                               CL_INVALID_BINARY, incompatible_msg);
    }
  }

#undef AND_CHECK_GLOBAL_MEM_DEF

  return status;
}

void acl_device_binary_t::load_content(const std::string &filename) {
  unload_content();

  m_filename = acl_realpath_existing(filename);

  reload_content();
  if (m_binary_pkg) {
    m_binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
  } else {
    m_binary_type = CL_PROGRAM_BINARY_TYPE_NONE;
  }
}

void acl_device_binary_t::load_content(const unsigned char *binary_content,
                                       const std::size_t binary_len) {
  unload_content();

  m_binary_storage = acl_shared_aligned_ptr(binary_len);
  safe_memcpy(get_content(), binary_content, binary_len,
              m_binary_storage.get_requested_size(), binary_len);

  m_binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
  m_binary_pkg = acl_pkg_open_file_from_memory(
      reinterpret_cast<char *>(get_content()), binary_len, 0);
  if (m_binary_pkg) {
    m_binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
  } else {
    m_binary_type = CL_PROGRAM_BINARY_TYPE_NONE;
  }
}

void acl_device_binary_t::unload_content() const {
  if (m_binary_pkg) {
    acl_pkg_close_file(m_binary_pkg);
    m_binary_pkg = nullptr;
  }
  m_binary_storage = acl_shared_aligned_ptr();
}

void acl_device_binary_t::reload_content() const {
  if (get_content() || m_filename == "") {
    // Binary is loaded in memory so no need to reload it.
    return;
  }

  std::ifstream binfile{m_filename, std::ios::binary | std::ios::ate};
  auto size = binfile.tellg();
  binfile.seekg(0);
  m_binary_storage = acl_shared_aligned_ptr(static_cast<size_t>(size));
  binfile.read(reinterpret_cast<char *>(m_binary_storage.get_aligned_ptr()),
               static_cast<std::streamsize>(size));

  m_binary_pkg = acl_pkg_open_file_from_memory(
      reinterpret_cast<char *>(m_binary_storage.get_aligned_ptr()),
      m_binary_storage.get_requested_size(), 0);
}

#ifdef _MSC_VER
#pragma warning(push)
// assignment in conditional expression.
#pragma warning(disable : 4706)
#endif

// Load binary_pkg and validate it.
// It must meet certain minimum checks: have the right sections,
// and the content of some of them must match the device program hash, etc.
cl_int acl_device_binary_t::load_binary_pkg(int validate_compile_options,
                                            int validate_memory_layout) {
  cl_int is_simulator;
  auto context = get_dev_prog()->program->context;
#define FAILREAD_MSG "Could not read parts of the program binary."
  size_t data_len = 0;

  acl_assert_locked();

  if (acl_platform.offline_mode == ACL_CONTEXT_MPSIM &&
      !validate_compile_options &&
      context->compiler_mode != CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA &&
      get_binary_len() < 1024) {
    // IF the binary is ridiculously small (arbitrary number) we are going
    // to assume it is just a name string so we are going to fake preloaded
    // for emulator, by picking the first aocx we can find.
    // IF you set mode=3 and have more than one aocx file this might not
    // give you what you expect.

    auto result = acl_glob("*.aocx");
    if (result.empty()) {
      result = acl_glob("../*.aocx");
      if (result.empty()) {
        ERR_RET(CL_INVALID_BINARY, context,
                "Can't find any aocx file in . or ..\n");
      }
    }

    load_content(result[0]);
    if (!get_content()) {
      ERR_RET(CL_INVALID_BINARY, context, "Can't read aocx file\n");
    }
  }

  // Open the package from the memory image.
  // Use 0 for show_mode, i.e. no user messages for anything.
  const auto pkg = get_binary_pkg();
  if (!pkg)
    ERR_RET(CL_INVALID_BINARY, context, "Binary file is malformed");

  auto status = CL_SUCCESS;

  // The hal printf will only result in output if ACL_DEBUG is set to > 0
#define AND_CHECK(CONDITION, ERRCODE, FAILMSG)                                 \
  if (status == CL_SUCCESS && !(CONDITION)) {                                  \
    status = ERRCODE;                                                          \
    acl_print_debug_msg(FAILMSG);                                              \
    acl_context_callback(context, FAILMSG);                                    \
  }

  // When extracting text sections, always allocate one more byte for the
  // terminating NUL.

  // Check board.
  // Must always be present, and match dev_prog->device
  AND_CHECK(acl_pkg_section_exists(pkg, ".acl.board", &data_len),
            CL_INVALID_BINARY,
            "Malformed program binary: missing .acl.board section");
  std::vector<char> pkg_board(data_len + 1);
  AND_CHECK(
      acl_pkg_read_section(pkg, ".acl.board", pkg_board.data(), data_len + 1),
      CL_INVALID_BINARY, FAILREAD_MSG " (board)");
  std::string pkg_board_str{pkg_board.data()};

  // Check board name matches - first create error message, then do check
  std::stringstream errmsg;
  auto *dev_prog = get_dev_prog();
  errmsg
      << "The current binary is not compatible with the one presently loaded "
         "on the FPGA.\n"
      << "This binary targets the BSP variant " << pkg_board_str
      << ", while the FPGA has the BSP\n"
      << "variant " << dev_prog->device->def.autodiscovery_def.name << ".\n"
      << "Use the command 'aocl initialize' to load the matching BSP\n"
      << "variant prior to invoking the host executable.";
  if (!acl_getenv("ACL_PCIE_PROGRAM_SKIP_BOARDNAME_CHECK")) {
    AND_CHECK(pkg_board_str == dev_prog->device->def.autodiscovery_def.name,
              CL_INVALID_BINARY,
              errmsg.str().c_str()); // perform the board name check
  }

  // Check random hash.
  // Must always be present, and if matches - skip first reprogram
  // Note that this step must be done before new autodiscovery is loaded in the
  // runtime.
  if (acl_pkg_section_exists(pkg, ".acl.rand_hash", &data_len) &&
      dev_prog->device->loaded_bin == nullptr &&
      acl_platform.offline_mode != ACL_CONTEXT_MPSIM) {
    std::vector<char> pkg_rand_hash(data_len + 1);
    AND_CHECK(acl_pkg_read_section(pkg, ".acl.rand_hash", pkg_rand_hash.data(),
                                   data_len + 1),
              CL_INVALID_BINARY, FAILREAD_MSG " (rand_hash)");
    // Note that we use dev_prog->device when checking for device global
    // Having the same binary suggest that the aocx on the device currently is
    // the same as the aocx used to create program, so we can peek the device
    // global setup now instead of later after acl_load_device_def_from_str
    if (dev_prog->device->def.autodiscovery_def.binary_rand_hash ==
            std::string(pkg_rand_hash.data()) &&
        (!acl_device_has_reprogram_device_globals(dev_prog->device))) {
      dev_prog->device->last_bin = this;
      dev_prog->device->loaded_bin = this;
    }
  }

  // Check autodiscovery.
  AND_CHECK(acl_pkg_section_exists(pkg, ".acl.autodiscovery", &data_len),
            CL_INVALID_BINARY,
            "Malformed program binary: missing .acl.autodiscovery section");
  std::vector<char> pkg_autodiscovery(data_len + 1);
  AND_CHECK(acl_pkg_read_section(pkg, ".acl.autodiscovery",
                                 pkg_autodiscovery.data(), data_len + 1),
            CL_INVALID_BINARY, FAILREAD_MSG " (ad)");
  std::string pkg_autodiscovery_str{pkg_autodiscovery.data()};
  // A trivial check on autodiscovery.
  // Really, it should parse too, but we don't check that here.
  AND_CHECK(pkg_autodiscovery_str.length() > 4, CL_INVALID_BINARY,
            "Invalid .acl.autodiscovery section in program binary");
  // Load the system configuration, so we get the kernel interfaces.
  if (status == CL_SUCCESS) {
    std::string err;
    auto ok = acl_load_device_def_from_str(pkg_autodiscovery_str,
                                           get_devdef().autodiscovery_def, err);
    if (!ok) {
      acl_context_callback(
          context, "Malformed program interface definition found in binary: ");
      acl_context_callback(context, err.c_str());
      status = CL_INVALID_BINARY;
    }

    // Check that memory layout does not change across reprograms.
    if (status == CL_SUCCESS && ok) {
      // For simulator flow, we treat as if the device has already been
      // programmed and check device global memory layout against
      // dev_prog->device->last_bin
      if (acl_platform.offline_mode == ACL_CONTEXT_MPSIM) {
        if (validate_memory_layout && dev_prog->device->last_bin) {
          AND_CHECK(get_devdef().autodiscovery_def.num_global_mem_systems <=
                            1 ||
                        (context->eagerly_program_device_with_first_binary &&
                         context->uses_dynamic_sysdef),
                    CL_INVALID_BINARY,
                    "Binary's memory defintions could not be verified "
                    "against device");
          status = l_compare_device_global_mem_defs(
              context,
              &(dev_prog->device->last_bin->get_devdef().autodiscovery_def),
              &(get_devdef().autodiscovery_def));
        }
      } else {
        if (get_devdef().autodiscovery_def.num_global_mem_systems == 0) {
          // Trying to create program with pre-19.3 aocx, which is no longer
          // supported The runtime should not get to this point
          acl_context_callback(context, "Program version is incompatible with "
                                        "this version of the runtime");
          status = CL_INVALID_BINARY;
        } else if (dev_prog->device->def.autodiscovery_def
                       .num_global_mem_systems == 0) {
          // The target device is programmed with pre-19.3 aocx, the runtime
          // should also not get to this point
          acl_context_callback(context, "The binary currently loaded on the "
                                        "device has an unsupported version!");
          status = CL_INVALID_BINARY;
        } else {
          // Check that this binary is compatible with the heterogeneous
          // memories that are already on the device.
          // ** IMPORTANT **
          // 1. For now, we will only support the case where swapping between
          // programs preserves the logical memory layout. If this
          //    is NOT the case, then we may have to remap and reallocate
          //    buffers everytime we report. While this is possible, it is
          //    complex and does it offer that much value?
          // 2. ACL is still single threaded. This means that if last_bin is NOT
          // set, then this current dev_bin will be loaded onto the device.
          if (validate_memory_layout) {
            status = l_compare_device_global_mem_defs(
                context, &(dev_prog->device->def.autodiscovery_def),
                &(get_devdef().autodiscovery_def));
          }
        }
      }
    }
  }

  is_simulator = 0;
  if (status == CL_SUCCESS &&
      acl_pkg_section_exists(pkg, ".acl.simulator_object", &data_len)) {
    if (acl_platform.offline_mode != ACL_CONTEXT_MPSIM) {
      acl_context_callback(
          context,
          "aocx contains simulated kernel, but simulation mode not set!");
      status = CL_INVALID_BINARY;
    } else {
      is_simulator = 1;
      // Don't validate the compiler options for simulator.
      // This is equivalent to having a pre-loaded aocx.
      validate_compile_options = 0;
    }
  } else if (status == CL_SUCCESS &&
             acl_pkg_section_exists(pkg, ".acl.emulator_object.linux",
                                    &data_len)) {
    acl_context_callback(
        context,
        "aocx contains unsupported legacy opencl emulated kernel for linux!");
    status = CL_INVALID_BINARY;
  } else if (status == CL_SUCCESS &&
             acl_pkg_section_exists(pkg, ".acl.emulator_object.windows",
                                    &data_len)) {
    acl_context_callback(
        context,
        "aocx contains unsupported legacy opencl emulated kernel for windows!");
  }
  if (status == CL_SUCCESS && acl_platform.offline_mode == ACL_CONTEXT_MPSIM &&
      !is_simulator) {
    acl_context_callback(context,
                         "Simulation mode set but aocx is for hardware!");
    status = CL_INVALID_BINARY;
  }

  if (validate_compile_options) {
    // Check autodiscovery.
    AND_CHECK(acl_pkg_section_exists(pkg, ".acl.compileoptions", &data_len),
              CL_INVALID_BINARY,
              "Malformed program binary: missing .acl.compileoptions section");
    std::vector<char> pkg_compileoptions(data_len + 1);
    AND_CHECK(acl_pkg_read_section(pkg, ".acl.compileoptions",
                                   pkg_compileoptions.data(), data_len + 1),
              CL_INVALID_BINARY, FAILREAD_MSG " (compile options)");
    AND_CHECK(std::string(pkg_compileoptions.data()) == dev_prog->build_options,
              CL_INVALID_BINARY,
              "Program was built with different compile options");

    // Check hash of source+options. Don't do so for binaries loaded by the
    // simulator.
    if (!dev_prog->hash.empty()) {
      // The binary itself doesn't contain the source, because we don't by
      // default keep customer source IP in the binary.
      AND_CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_len),
                CL_INVALID_BINARY,
                "Malformed program binary: missing " ACL_PKG_SECTION_HASH
                " section");
      std::vector<char> pkg_hash(data_len + 1);
      AND_CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, pkg_hash.data(),
                                     data_len + 1),
                CL_INVALID_BINARY, FAILREAD_MSG " (hash)");
      AND_CHECK(std::string(pkg_hash.data()) == dev_prog->hash,
                CL_INVALID_BINARY,
                "Program was built with different source contents (checked via "
                "hashing)");
    }
  }

  // Validate that kernel endianness matches host system
  if (status == CL_SUCCESS) {
    if (l_is_host_big_endian() !=
        get_devdef().autodiscovery_def.is_big_endian) {
      acl_context_callback(
          context, "Endianness mismatch between host system and device binary");
      if (l_is_host_big_endian()) {
        acl_context_callback(context, "Host is big endian");
      } else {
        acl_context_callback(context, "Host is little endian");
      }
      if (get_devdef().autodiscovery_def.is_big_endian) {
        acl_context_callback(context, "Device is big endian");
      } else {
        acl_context_callback(context, "Device is little endian");
      }

      status = CL_INVALID_BINARY; // Fail on the endianness mismatch
    }
  }

#undef AND_CHECK

  return status;
#undef FAILREAD_MSG
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

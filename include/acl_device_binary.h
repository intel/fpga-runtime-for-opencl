// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// System headers.
#include <string>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include "acl.h"
#include "acl_shared_aligned_ptr.h"

/* An opaque type for the ELF-based binary package file.
 * See pkg_editor.h
 */
typedef struct acl_pkg_file *acl_pkg_file_t;

class acl_device_program_info_t;

// acl_device_binary_t encapsulates a device binary (i.e. a single .aocx or
// .aocr file).
class acl_device_binary_t {
public:
  ~acl_device_binary_t() { unload_content(); }

  inline void set_dev_prog(acl_device_program_info_t *dev_prog) {
    m_dev_prog = dev_prog;
  }

  inline acl_device_program_info_t *get_dev_prog() const { return m_dev_prog; }

  inline acl_device_def_t &get_devdef() { return m_devdef; }

  inline const acl_device_def_t &get_devdef() const { return m_devdef; }

  inline size_t get_binary_len() const {
    return m_binary_storage.get_requested_size();
  }

  inline cl_program_binary_type get_binary_type() const {
    return m_binary_type;
  }

  // Return an acl_pkg_file_t created from the device binary (i.e. aocx)
  // loaded in memory in this acl_device_binary_t or nullptr if no device
  // binary is loaded. This will attempt to load the device binary from the
  // filename associated with this acl_device_binary_t if the filename exists
  // and the binary is currently in the unloaded state.
  inline const acl_pkg_file_t get_binary_pkg() const {
    reload_content();
    return m_binary_pkg;
  }

  // Return the path to the file for this device binary.
  // This may be empty if this binary wasn't loaded from a file.
  const std::string &get_filename() const { return m_filename; }

  // Load the contents of the device binary stored inside filename
  // into memory. The file must exist.
  void load_content(const std::string &filename);

  // Copy the contents of binary_content into this acl_device_binary_t.
  // binary_len must be the length of the binary_content in bytes.
  void load_content(const unsigned char *binary_content,
                    std::size_t binary_len);

  // Unload the contents of the device binary file from memory.
  void unload_content() const;

  // Parse the loaded binary for accelerator definitions and populate
  // data structures in this acl_device_binary_t.
  cl_int load_binary_pkg(int validate_compile_options,
                         int validate_memory_layout);

  // Pointer to the contents of the device binary file aligned for use with
  // our kernel mode drivers.
  inline unsigned char *get_content() {
    return reinterpret_cast<unsigned char *>(
        m_binary_storage.get_aligned_ptr());
  }
  inline const unsigned char *get_content() const {
    return reinterpret_cast<unsigned char *>(
        m_binary_storage.get_aligned_ptr());
  }

  // Return a pointer to the acl_accel_def_t with the given kernel name
  // stored in this acl_device_binary_t's devdef or null if no acl_accel_def
  // with the given name exists.
  inline const acl_accel_def_t *get_accel_def(const std::string &name) const {
    for (auto &ad : m_devdef.autodiscovery_def.accel) {
      if (ad.iface.name == name)
        return &ad;
    }

    return nullptr;
  }

private:
  // Back-pointer to parent acl_device_program_info_t that owns this
  // acl_device_binary_t.
  acl_device_program_info_t *m_dev_prog = nullptr;

  // The interface definition for the program.
  // This is parsed from the system description string in the package
  // file.
  acl_device_def_t m_devdef;

  // The program binary type for device. Used to return the status of
  // the binary in clGetProgramBuildInfo.
  cl_program_binary_type m_binary_type = CL_PROGRAM_BINARY_TYPE_NONE;

  // This stores the actual bytes inside the program binary file.
  // We use aligned allocation to ensure programming files are aligned
  // for use with our kernel mode drivers.
  mutable acl_shared_aligned_ptr m_binary_storage;

  // The handle for the unpacked binary.
  // This is loaded if and only if get_contents returns a non-null pointer
  // indicating the binary is loaded (and valid), and
  // we either may be programming the device, or we use dynamic sysdef's
  // to get kernel definitions.
  // (In other words, we fail the binary load if it doesn't validate as a
  // pkg file.)
  mutable acl_pkg_file_t m_binary_pkg = nullptr;

  std::string m_filename;

  void reload_content() const;
};

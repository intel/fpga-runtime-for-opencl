// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <sstream>

// External library headers.
#include <acl_hash/acl_hash.h>

// Internal headers.
#include <acl_support.h>
#include <acl_types.h>

static std::string l_get_hashed_kernel_name(const std::string &kernel_name) {
  acl_hash_context_t ctx;
  acl_hash_init_sha1(&ctx);
  std::string kernel_name_str(kernel_name);
  acl_hash_add(&ctx, kernel_name_str.c_str(), kernel_name_str.length());
  char sha1_kernel_name[ACL_HASH_SHA1_DIGEST_BUFSIZE];
  acl_hash_hexdigest(&ctx, sha1_kernel_name, sizeof(sha1_kernel_name));

  return std::string("kernel_") + std::string(sha1_kernel_name);
}

acl_device_program_info_t::acl_device_program_info_t() {
  device_binary.set_dev_prog(this);
}

acl_device_program_info_t::~acl_device_program_info_t() {
  if (device) {
    if (device->last_bin == &device_binary) {
      device->last_bin = nullptr;
    }
    if (device->loaded_bin == &device_binary) {
      device->loaded_bin = nullptr;
    }

    for (const auto &sdb : m_split_device_binaries) {
      if (device->last_bin == &(sdb.second)) {
        device->last_bin = nullptr;
      }
      if (device->loaded_bin == &(sdb.second)) {
        device->loaded_bin = nullptr;
      }
    }
  }
}

acl_device_binary_t &
acl_device_program_info_t::add_split_binary(const std::string &hashed_name) {
  assert(program->context->split_kernel);

  auto &dev_bin = m_split_device_binaries[hashed_name];
  dev_bin.set_dev_prog(this);
  return dev_bin;
}

const acl_device_binary_t *
acl_device_program_info_t::get_device_binary(const std::string &name) const {
  if (program->context->split_kernel) {
    const auto sdb =
        m_split_device_binaries.find(l_get_hashed_kernel_name(name));
    if (sdb != m_split_device_binaries.end()) {
      return &(sdb->second);
    }
  } else {
    return &device_binary;
  }

  return nullptr;
}

const acl_device_binary_t *
acl_device_program_info_t::get_or_create_device_binary(
    const std::string &name) {
  if (program->context->split_kernel) {
    const auto hashed_name = l_get_hashed_kernel_name(name);
    const auto sdb = m_split_device_binaries.find(hashed_name);

    // When the user has disabled preloading of kernels we need to
    // load it now.
    const char *preload = acl_getenv("CL_PRELOAD_SPLIT_BINARIES_INTELFPGA");
    if (sdb == m_split_device_binaries.end() && preload &&
        std::string(preload) == "0") {
      std::stringstream ss;
      ss << program->context->program_library_root << "/" << hashed_name
         << ".aocx";
      auto filename = ss.str();

      auto &dev_bin = add_split_binary(hashed_name);
      dev_bin.load_content(filename);

      // Too late to handle errors now. User must guarantee all kernel files are
      // present when they disable preloading device binaries. This is not a
      // user-facing flow so it is okay to impose these restrictions.
      auto status = dev_bin.load_binary_pkg(0, 1);
      assert(status == CL_SUCCESS);
      // Need to unload the binary and only load it on an as needed
      // basis due to high memory usage when there are many split binaries.
      dev_bin.unload_content();

      return &dev_bin;
    } else {
      return get_device_binary(name);
    }
  }

  return get_device_binary(name);
}

const acl_accel_def_t *
acl_device_program_info_t::get_kernel_accel_def(const std::string &name) const {
  if (!program->context->uses_dynamic_sysdef) {
    // When using pre-loaded binary mode the accel defs are stored in the
    // device's device def instead of the binary's device def.
    for (const auto &a : device->def.autodiscovery_def.accel) {
      if (a.iface.name == name)
        return &a;
    }
    return nullptr;
  }

  const auto *db = get_device_binary(name);
  if (db) {
    return db->get_accel_def(name);
  } else {
    return nullptr;
  }
}

size_t acl_device_program_info_t::get_num_kernels() const {
  if (program->context->split_kernel) {
    // Each split device binary represents one kernel.
    return m_split_device_binaries.size();
  } else {
    return device_binary.get_devdef().autodiscovery_def.accel.size();
  }
}

std::set<std::string> acl_device_program_info_t::get_all_kernel_names() const {
  std::set<std::string> result;

  if (program->context->split_kernel) {
    for (const auto &db : m_split_device_binaries) {
      for (const auto &a : db.second.get_devdef().autodiscovery_def.accel) {
        result.insert(a.iface.name);
      }
    }
  } else {
    for (const auto &a : device_binary.get_devdef().autodiscovery_def.accel) {
      result.insert(a.iface.name);
    }
  }

  assert(result.size() == get_num_kernels());

  return result;
}

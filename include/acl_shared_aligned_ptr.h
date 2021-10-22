// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// System headers.
#include <cstddef>
#include <memory>

// External library headers.

// Internal headers.
#include "acl.h"

// A type used to manage an aligned memory block allocation.
// The memory is allocated if and only if m_raw is not nullptr.
// The allocated memory is automatically freed when all
// acl_shared_aligned_ptr objects owning it are destroyed.
class acl_shared_aligned_ptr {
public:
  acl_shared_aligned_ptr() = default;
  acl_shared_aligned_ptr(std::size_t size);

  // Return the allocated pointer aligned
  // to the requested alignment.
  inline void *get_aligned_ptr() const { return m_aligned_ptr; }

  // Return the requested alignment.
  inline std::size_t get_alignment() const { return m_alignment; }

  // Return the total allocated size in bytes after
  // adding padding required for  the requested alignment.
  inline std::size_t get_size() const { return m_size; }

  // Return the total size in bytes that was requested
  // to be allocated.
  inline std::size_t get_requested_size() const { return m_requested_size; }

private:
  void *m_aligned_ptr = nullptr; // This pointer is aligned as requested.
  std::shared_ptr<char> m_raw;   // The malloc base pointer returned
                                 // by the system.
  std::size_t m_alignment = ACL_MEM_ALIGN;
  std::size_t m_size = 0;           // Actual allocated size after alignment.
  std::size_t m_requested_size = 0; // Requested allocation size.
};

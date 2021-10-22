// Copyright (C) 2019-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <cstdint>

// Internal headers.
#include <acl_shared_aligned_ptr.h>

acl_shared_aligned_ptr::acl_shared_aligned_ptr(const std::size_t size) {
  const auto alloc_size = size + ACL_MEM_ALIGN;
  if (alloc_size < size)
    return; // watch for wraparound!

  m_raw = std::shared_ptr<char>(new char[alloc_size],
                                std::default_delete<char[]>());
  if (m_raw == nullptr)
    return;

  auto raw_uint = reinterpret_cast<std::uintptr_t>(m_raw.get());
  auto offset = raw_uint & (ACL_MEM_ALIGN - 1);
  if (offset)
    offset = ACL_MEM_ALIGN - offset;
  m_aligned_ptr = reinterpret_cast<void *>(raw_uint + offset);
  m_requested_size = size;
  m_size = alloc_size;
}

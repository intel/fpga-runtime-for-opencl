// Copyright (C) 2017-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External library headers.
#include <CL/cl_ext_intelfpga.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_hostch.h>
#include <acl_mem.h>
#include <acl_platform.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

/* Local Functions */
static cl_int l_push_packet(unsigned int physical_device_id, int channel_handle,
                            const void *host_buffer, size_t write_size) {
  size_t pushed_data;
  int status = 0;

  pushed_data = acl_get_hal()->hostchannel_push(
      physical_device_id, channel_handle, host_buffer, write_size, &status);
  assert(status == 0);
  if (pushed_data == write_size) {
    return CL_SUCCESS;
  }
  // If data type is not byte aligned, such as AC_INT56
  // Pushed data can be smaller than the request write_size due to compiler
  // padding. Runtime needs to check if the trailing bytes are all 0s.
  else if ((pushed_data > 0) && (pushed_data < write_size)) {
    for (size_t i = pushed_data; i < write_size; i++) {
      unsigned char c = ((char *)host_buffer)[i];
      if (c != 0) {
        // This shouldn't happen. Needs to send out a warning to user rather
        // than a silent function failure.
        std::cerr << "Error: Data is not fully written into the Hostpipe. "
                     "None-0 bits have been cut off \n";
        assert(0);
      }
    }
    return CL_SUCCESS;
  } else {
    // Pipe is full in this case. Nothing is pushed in this case.
    assert(pushed_data == 0);
    return CL_PIPE_FULL;
  }
}

static cl_int l_push_sideband_packet(unsigned int physical_device_id,
                                     int channel_handle,
                                     const void *host_buffer, size_t write_size,
                                     host_pipe_t &host_pipe_info) {
  // This pipe has sideband signals, need to break into data section and
  // sideband signal sections
  int status = 0;
  bool final_status = true;
  size_t total_pushed = 0;
  for (auto const &sideband_signal_entry :
       host_pipe_info.side_band_signals_vector) {
    size_t pushed_data;
    if (sideband_signal_entry.port_identifier ==
        static_cast<unsigned>(AOCL_MMD_HOSTCHANNEL_PORT_DATA)) {
      pushed_data = acl_get_hal()->hostchannel_push_no_ack(
          host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
          (const void *)((char *)host_buffer +
                         sideband_signal_entry.port_offset / 8),
          sideband_signal_entry.side_band_size / 8, &status);
    } else {
      // this is sideband signal
      pushed_data = acl_get_hal()->hostchannel_sideband_push_no_ack(
          host_pipe_info.m_physical_device_id,
          sideband_signal_entry.port_identifier,
          host_pipe_info.m_channel_handle,
          (const void *)((char *)host_buffer +
                         sideband_signal_entry.port_offset / 8),
          sideband_signal_entry.side_band_size / 8, &status);
    }
    final_status = final_status && status;
    if (pushed_data == 0) {
      return CL_PIPE_FULL;
    }
    total_pushed += pushed_data;
  }
  // Now need to acknowledge
  size_t acked_size;

  acked_size = acl_get_hal()->hostchannel_ack_buffer(
      host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
      total_pushed, &status);

  if (acked_size == 0) {
    return CL_PIPE_FULL;
  }

  status = final_status && status;
  return status;
}

static size_t l_pull_sideband_packet(unsigned int physical_device_id,
                                     int channel_handle,
                                     const void *host_buffer, size_t write_size,
                                     host_pipe_t &host_pipe_info, int &status) {
  bool final_status = true;
  size_t total_pulled_data = 0;
  for (auto const &sideband_signal_entry :
       host_pipe_info.side_band_signals_vector) {
    size_t pulled_data;
    if (sideband_signal_entry.port_identifier ==
        static_cast<unsigned>(AOCL_MMD_HOSTCHANNEL_PORT_DATA)) {
      pulled_data = acl_get_hal()->hostchannel_pull_no_ack(
          host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
          (void *)((char *)host_buffer + sideband_signal_entry.port_offset / 8),
          sideband_signal_entry.side_band_size / 8, &status);
    } else {
      // this is sideband signal
      pulled_data = acl_get_hal()->hostchannel_sideband_pull_no_ack(
          host_pipe_info.m_physical_device_id,
          sideband_signal_entry.port_identifier,
          host_pipe_info.m_channel_handle,
          (void *)((char *)host_buffer + sideband_signal_entry.port_offset / 8),
          sideband_signal_entry.side_band_size / 8, &status);
    }
    final_status = final_status && status;
    if (pulled_data == 0) {
      return 0;
    }
    total_pulled_data += pulled_data;
  }
  // Now need to acknowledge
  size_t acked_size;

  acked_size = acl_get_hal()->hostchannel_ack_buffer(
      host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
      total_pulled_data, &status);
  if (acked_size == 0) {
    return 0;
  }
  status = final_status && status;
  return total_pulled_data;
}

static void l_clean_up_pending_pipe_ops(cl_mem pipe) {
  size_t acked_size = 0;
  int status = 0;

  while (true) {
    // Flush out any packets at the top of the queue
    for (auto it = pipe->host_pipe_info->m_host_op_queue.begin();
         it != pipe->host_pipe_info->m_host_op_queue.end() &&
         it->m_op == PACKET;) {
      int res = 0;
      size_t available_sz;
      void *mmd_buffer;

      // Make sure the mmd_pointer is where you expect it be
      mmd_buffer = acl_get_hal()->hostchannel_get_buffer(
          pipe->host_pipe_info->m_physical_device_id,
          pipe->host_pipe_info->m_channel_handle, &available_sz, &status);
      assert(status == 0);
      assert(mmd_buffer == it->m_mmd_buffer);

      // We have to block on sending these packets which we claimed we have
      // already sent to the mmd
      while (res == 0) {
        acked_size = acl_get_hal()->hostchannel_ack_buffer(
            pipe->host_pipe_info->m_physical_device_id,
            pipe->host_pipe_info->m_channel_handle,
            pipe->fields.pipe_objs.pipe_packet_size, &status);
        if (acked_size == pipe->fields.pipe_objs.pipe_packet_size) {
          res = 1;
        } else {
          assert(status == 0);
          assert(acked_size == 0);
        }
      }
      pipe->host_pipe_info->size_buffered -= acked_size;

      // Clean up the host pipe operation form the queue
      it = pipe->host_pipe_info->m_host_op_queue.erase(it);
    }

    // If the operation queue is not empty, the next operation should be a MAP
    // operation Process this map operation and see if there's any data to be
    // acked
    if (!pipe->host_pipe_info->m_host_op_queue.empty()) {
      size_t available_sz;
      void *mmd_buffer;

      auto it = pipe->host_pipe_info->m_host_op_queue.begin();
      assert(it->m_op == MAP);

      // Make sure the mmd_pointer is where you expect it be
      mmd_buffer = acl_get_hal()->hostchannel_get_buffer(
          pipe->host_pipe_info->m_physical_device_id,
          pipe->host_pipe_info->m_channel_handle, &available_sz, &status);
      assert(status == 0);
      assert(mmd_buffer == it->m_mmd_buffer);

      // If the user has unmapped any part, send that part to the mmd
      if (it->m_size_sent > 0) {
        acked_size = 0;
        while (acked_size != it->m_size_sent) {
          acked_size += acl_get_hal()->hostchannel_ack_buffer(
              pipe->host_pipe_info->m_physical_device_id,
              pipe->host_pipe_info->m_channel_handle,
              it->m_size_sent - acked_size, &status);
          assert(status == 0);
        }
        pipe->host_pipe_info->size_buffered -= acked_size;
      }

      // If this operation is incomplete, it blocks the beginning of the queue
      // and we stop
      if (it->m_size_sent != it->m_op_size) {
        return;
      }

      // This operation is done so remove it from the queue
      pipe->host_pipe_info->m_host_op_queue.erase(it);
    } else {
      return;
    }
  }
}

static void l_move_ops_from_hostbuf_to_mmdbuf(cl_mem pipe) {
  void *mmd_buffer;
  size_t buffer_size;
  int status = 0;

  auto it = pipe->host_pipe_info->m_host_op_queue.begin();

  // This function should only do work for write pipes since we can't buffer
  // read operations
  assert((pipe->flags & CL_MEM_HOST_WRITE_ONLY) ||
         pipe->host_pipe_info->m_host_op_queue.empty());
  // Make sure we didn't buffer more data than the width of the pipe
  assert(pipe->host_pipe_info->size_buffered <=
         (pipe->fields.pipe_objs.pipe_max_packets *
          pipe->fields.pipe_objs.pipe_packet_size));

  // Find space in the mmd buffer
  mmd_buffer = acl_get_hal()->hostchannel_get_buffer(
      pipe->host_pipe_info->m_physical_device_id,
      pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
  // What if we don't have enough mmd buffer space for some reason? Right now,
  // we will only buffer upto the maximum depth of the channel
  // (max_packets*packet_size). Thus we should always have enough space to
  // transfer the data from m_host_buffer to m_mmd_buffer
  assert(buffer_size >= pipe->host_pipe_info->size_buffered);

  // Copy data from temporary host buffer to mmd buffer for every operation
  while (it != pipe->host_pipe_info->m_host_op_queue.end()) {
    it->m_mmd_buffer = mmd_buffer;
    safe_memcpy(it->m_mmd_buffer, it->m_host_buffer, it->m_op_size, buffer_size,
                it->m_op_size);

    free(it->m_host_buffer);
    it->m_host_buffer = NULL;

    mmd_buffer = ((char *)mmd_buffer) + it->m_op_size;
    ++it;
  }
}

cl_int acl_bind_pipe_to_channel(cl_mem pipe, cl_device_id device,
                                const acl_device_def_autodiscovery_t &devdef) {
  int direction;

  acl_assert_locked();

  for (const auto &hostpipe : devdef.acl_hostpipe_info) {
    if (hostpipe.name == pipe->host_pipe_info->host_pipe_channel_id) {
      // Found the object, we can do checks on it now
      // Check width
      if (pipe->fields.pipe_objs.pipe_packet_size != hostpipe.data_width) {
        return CL_INVALID_VALUE;
      }
      // Check direction
      if ((pipe->flags & CL_MEM_HOST_READ_ONLY) && hostpipe.is_dev_to_host) {
        direction = DEVICE_TO_HOST;
      } else if ((pipe->flags & CL_MEM_HOST_WRITE_ONLY) &&
                 hostpipe.is_host_to_dev) {
        direction = HOST_TO_DEVICE;
      } else {
        return CL_INVALID_VALUE;
      }
      // Check max buffer size
      if (pipe->fields.pipe_objs.pipe_max_packets > hostpipe.max_buffer_depth) {
        return CL_INVALID_VALUE;
      }

      pipe->host_pipe_info->m_physical_device_id =
          device->def.physical_device_id;
      pipe->host_pipe_info->m_channel_handle =
          acl_get_hal()->hostchannel_create(
              pipe->host_pipe_info->m_physical_device_id,
              (char *)pipe->host_pipe_info->host_pipe_channel_id.c_str(),
              pipe->fields.pipe_objs.pipe_max_packets,
              pipe->fields.pipe_objs.pipe_packet_size, direction);
      size_t buffer_size;
      int status = 0;
      acl_get_hal()->hostchannel_get_buffer(
          pipe->host_pipe_info->m_physical_device_id,
          pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
      if (pipe->host_pipe_info->m_channel_handle <= 0) {
        return CL_INVALID_VALUE;
      }
      pipe->host_pipe_info->binded = true;
      return CL_SUCCESS;
    }
  }
  // No matching hostpipe channel id(should not happen)
  return CL_INVALID_VALUE;
}

void acl_process_pipe_transactions(cl_mem pipe) {
  l_move_ops_from_hostbuf_to_mmdbuf(pipe);
  l_clean_up_pending_pipe_ops(pipe);
}

void acl_bind_and_process_all_pipes_transactions(
    cl_context context, cl_device_id device,
    const acl_device_def_autodiscovery_t &devdef) {
  for (const auto &pipe : context->pipe_vec) {
    if (device->loaded_bin && pipe->host_pipe_info &&
        pipe->host_pipe_info->m_binded_kernel &&
        device->loaded_bin->get_dev_prog()->program ==
            pipe->host_pipe_info->m_binded_kernel->program) {
      acl_bind_pipe_to_channel(pipe, device, devdef);
      l_move_ops_from_hostbuf_to_mmdbuf(pipe);
      l_clean_up_pending_pipe_ops(pipe);
    }
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReadPipeIntelFPGA(cl_mem pipe, void *ptr) {
  void *mmd_buffer;
  size_t pulled_data;
  size_t buffer_size;
  cl_int status = 0;

  {
    std::scoped_lock lock{acl_mutex_wrapper};
    acl_idle_update(pipe->context);
  }

  if (pipe->host_pipe_info == NULL) {
    ERR_RET(CL_INVALID_MEM_OBJECT, pipe->context,
            "This pipe is not a host pipe");
  }

  acl_mutex_lock(&(pipe->host_pipe_info->m_lock));

  // Error checking
  if (!(pipe->flags & CL_MEM_HOST_READ_ONLY)) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_MEM_OBJECT, pipe->context,
            "This host pipe is not read-only pipe");
  }
  if (!pipe->host_pipe_info->m_binded_kernel) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_KERNEL, pipe->context,
            "This host pipe has not been bound to a kernel yet");
  }
  if (ptr == NULL) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_VALUE, pipe->context,
            "Invalid pointer was provided to host data");
  }

  // Is the pipe bound to a channel yet? If not then return unsuccessfully
  if (!pipe->host_pipe_info->binded) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return CL_PIPE_EMPTY;
  }

  // Are there any operations queued up on this pipe? Is there any data still
  // available to be read? If yes return that data. Else return 0
  if (!pipe->host_pipe_info->m_host_op_queue.empty()) {
    // Find space in the mmd buffer
    mmd_buffer = acl_get_hal()->hostchannel_get_buffer(
        pipe->host_pipe_info->m_physical_device_id,
        pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
    assert(status == 0);
    // Size of buffered space should never exceed actual available space in the
    // mmd
    assert(buffer_size >= pipe->host_pipe_info->size_buffered);

    // If there's no space left, return unsuccessfully
    if (buffer_size < pipe->host_pipe_info->size_buffered +
                          pipe->fields.pipe_objs.pipe_packet_size) {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      return CL_PIPE_EMPTY;
    }

    mmd_buffer = ((char *)mmd_buffer) + pipe->host_pipe_info->size_buffered;
    pipe->host_pipe_info->size_buffered +=
        pipe->fields.pipe_objs.pipe_packet_size;

    // Create the host operation data structure
    host_op_t host_op;
    host_op.m_op = PACKET;
    host_op.m_mmd_buffer = mmd_buffer;
    host_op.m_host_buffer = NULL;
    host_op.m_op_size = pipe->fields.pipe_objs.pipe_packet_size;
    host_op.m_size_sent = 0;

    safe_memcpy(ptr, host_op.m_mmd_buffer, host_op.m_op_size, buffer_size,
                buffer_size);

    // Save the host operation for later in the operation queue
    pipe->host_pipe_info->m_host_op_queue.push_back(host_op);

    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return CL_SUCCESS;
  }

  pulled_data = acl_get_hal()->hostchannel_pull(
      pipe->host_pipe_info->m_physical_device_id,
      pipe->host_pipe_info->m_channel_handle, ptr,
      pipe->fields.pipe_objs.pipe_packet_size, &status);
  assert(status == 0);

  if (pulled_data == pipe->fields.pipe_objs.pipe_packet_size) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return CL_SUCCESS;
  } else {
    // A packet of data is the smallest size this channel can receive. If it
    // didn't receive a packet, it shouldn't have received anything at all.
    assert(pulled_data == 0);
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return CL_PIPE_EMPTY;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clWritePipeIntelFPGA(cl_mem pipe, void *ptr) {
  size_t buffer_size;
  void *buffer = 0;
  cl_int status = 0;
  cl_int ret;

  {
    std::scoped_lock lock{acl_mutex_wrapper};
    acl_idle_update(pipe->context);
  }

  // Error checking
  if (pipe->host_pipe_info == NULL) {
    ERR_RET(CL_INVALID_MEM_OBJECT, pipe->context,
            "This pipe is not a host pipe");
  }

  acl_mutex_lock(&(pipe->host_pipe_info->m_lock));

  if (!(pipe->flags & CL_MEM_HOST_WRITE_ONLY)) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_MEM_OBJECT, pipe->context,
            "This host pipe is not write-only pipe");
  }
  if (!pipe->host_pipe_info->m_binded_kernel) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_KERNEL, pipe->context,
            "This host pipe has not been bound to a kernel yet");
  }
  if (ptr == NULL) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_VALUE, pipe->context,
            "Invalid pointer was provided to host data");
  }

  // If there is no queued op and the pipe is binded
  // packet should be send right here
  if (pipe->host_pipe_info->m_host_op_queue.empty() &&
      pipe->host_pipe_info->binded) {
    ret = l_push_packet(pipe->host_pipe_info->m_physical_device_id,
                        pipe->host_pipe_info->m_channel_handle, ptr,
                        pipe->fields.pipe_objs.pipe_packet_size);

    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return ret;
  }

  // Queue this operation because we can't do it now

  // Find space in the mmd buffer
  if (pipe->host_pipe_info->binded) {
    buffer = acl_get_hal()->hostchannel_get_buffer(
        pipe->host_pipe_info->m_physical_device_id,
        pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
    assert(status == 0);
  }
  // If the pipe is not bound to a channel, then we need
  // to create a temporary buffer for it in the host
  else {
    buffer_size = pipe->fields.pipe_objs.pipe_packet_size *
                  pipe->fields.pipe_objs.pipe_max_packets;
  }

  // Size of buffered space should never exceed actual available space
  assert(buffer_size >= pipe->host_pipe_info->size_buffered);

  // If there's no space left, return unsuccessfully
  if (buffer_size < pipe->host_pipe_info->size_buffered +
                        pipe->fields.pipe_objs.pipe_packet_size) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    return CL_PIPE_FULL;
  }

  // Create the host operation data structure
  host_op_t host_op;
  host_op.m_op = PACKET;
  host_op.m_op_size = pipe->fields.pipe_objs.pipe_packet_size;
  host_op.m_size_sent = 0;

  if (pipe->host_pipe_info->binded) {
    buffer = ((char *)buffer) + pipe->host_pipe_info->size_buffered;

    host_op.m_mmd_buffer = buffer;
    host_op.m_host_buffer = NULL;

    safe_memcpy(host_op.m_mmd_buffer, ptr, host_op.m_op_size,
                pipe->host_pipe_info->size_buffered, host_op.m_op_size);
  } else {
    buffer = malloc(pipe->fields.pipe_objs.pipe_packet_size);
    if (buffer == NULL) {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      ERR_RET(CL_OUT_OF_HOST_MEMORY, pipe->context,
              "Could not allocate memory for internal data structure");
    }

    host_op.m_mmd_buffer = NULL;
    host_op.m_host_buffer = buffer;

    safe_memcpy(host_op.m_host_buffer, ptr, host_op.m_op_size,
                pipe->fields.pipe_objs.pipe_packet_size, host_op.m_op_size);
  }

  pipe->host_pipe_info->size_buffered +=
      pipe->fields.pipe_objs.pipe_packet_size;

  // Save the host operation for later in the operation queue
  pipe->host_pipe_info->m_host_op_queue.push_back(host_op);

  acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clMapHostPipeIntelFPGA(cl_mem pipe,
                                                      cl_map_flags map_flags,
                                                      size_t requested_size,
                                                      size_t *mapped_size,
                                                      cl_int *errcode_ret) {
  size_t buffer_size;
  void *buffer = 0;
  int status = 0;

  {
    std::scoped_lock lock{acl_mutex_wrapper};
    acl_idle_update(pipe->context);
  }

  if (pipe->host_pipe_info == NULL) {
    BAIL_INFO(CL_INVALID_MEM_OBJECT, pipe->context,
              "This pipe is not a host pipe");
  }

  acl_mutex_lock(&(pipe->host_pipe_info->m_lock));

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  // Error checking
  if (mapped_size == NULL) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    BAIL_INFO(CL_INVALID_VALUE, pipe->context,
              "Invalid pointer was provided for mapped_size argument");
  }
  *mapped_size = 0;

  if (!pipe->host_pipe_info->m_binded_kernel) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    BAIL_INFO(CL_INVALID_KERNEL, pipe->context,
              "This host pipe has not been bound to a kernel yet");
  }
  if (map_flags != 0) {
    acl_context_callback(pipe->context,
                         "map_flags value other than 0 is not supported in the "
                         "Runtime. Ignoring it...");
  }

  // Find space in the mmd
  if (pipe->host_pipe_info->binded) {
    buffer = acl_get_hal()->hostchannel_get_buffer(
        pipe->host_pipe_info->m_physical_device_id,
        pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
    assert(status == 0);
  }
  // If the pipe is not bound to a channel, then we need to
  // create a temporary buffer for it in the host
  else {
    // Obviously can't buffer read operations
    if (pipe->flags & CL_MEM_HOST_READ_ONLY) {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      BAIL_INFO(CL_OUT_OF_RESOURCES, pipe->context,
                "No buffer space for the map operation");
    }

    buffer_size = pipe->fields.pipe_objs.pipe_packet_size *
                  pipe->fields.pipe_objs.pipe_max_packets;
  }

  // Size of buffered space should never exceed actual available space in the
  // mmd
  assert(buffer_size >= pipe->host_pipe_info->size_buffered);

  // If there's no space left, return unsuccessfully
  if (buffer_size == pipe->host_pipe_info->size_buffered) {
    if (pipe->flags & CL_MEM_HOST_READ_ONLY) {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      BAIL_INFO(CL_OUT_OF_RESOURCES, pipe->context,
                "No buffer space for the read map operation");
    } else {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      BAIL_INFO(CL_OUT_OF_RESOURCES, pipe->context,
                "No buffer space for the write map operation");
    }
  }

  // Figure out how much space we can give
  buffer_size -= pipe->host_pipe_info->size_buffered;
  if (requested_size != 0) {
    buffer_size = (buffer_size < requested_size) ? buffer_size : requested_size;
  }
  *mapped_size = buffer_size;

  // Create the host pipe operation data structure
  host_op_t host_op;
  host_op.m_op = MAP;
  host_op.m_size_sent = 0;
  host_op.m_op_size = buffer_size;

  if (pipe->host_pipe_info->binded) {
    buffer = ((char *)buffer) + pipe->host_pipe_info->size_buffered;

    host_op.m_mmd_buffer = buffer;
    host_op.m_host_buffer = NULL;
  } else {
    buffer = malloc(buffer_size);
    if (buffer == NULL) {
      acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, pipe->context,
                "Could not allocate memory for internal data structure");
    }

    host_op.m_mmd_buffer = NULL;
    host_op.m_host_buffer = buffer;
  }

  pipe->host_pipe_info->size_buffered += buffer_size;

  // Save the host pipe operation in the operation queue
  pipe->host_pipe_info->m_host_op_queue.push_back(host_op);

  if (pipe->host_pipe_info->binded) {
    buffer = host_op.m_mmd_buffer;
  } else {
    buffer = host_op.m_host_buffer;
  }

  acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));

  return buffer;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clUnmapHostPipeIntelFPGA(cl_mem pipe, void *mapped_ptr, size_t size_to_unmap,
                         size_t *unmapped_size) {
  void *mmd_buffer;
  size_t acked_size;
  size_t buffer_size;
  int status = 0;
  int first = 1;

  {
    std::scoped_lock lock{acl_mutex_wrapper};
    acl_idle_update(pipe->context);
  }

  if (pipe->host_pipe_info == NULL) {
    ERR_RET(CL_INVALID_MEM_OBJECT, pipe->context,
            "This pipe is not a host pipe");
  }

  acl_mutex_lock(&(pipe->host_pipe_info->m_lock));

  // Error checking
  if (!pipe->host_pipe_info->m_binded_kernel) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_KERNEL, pipe->context,
            "This host pipe has not been bound to a kernel yet");
  }

  assert(pipe->host_pipe_info->m_host_op_queue.size());
  auto it = pipe->host_pipe_info->m_host_op_queue.begin();
  while (it != pipe->host_pipe_info->m_host_op_queue.end()) {
    if (pipe->host_pipe_info->binded) {
      if (it->m_mmd_buffer == mapped_ptr) {
        break;
      }
    } else {
      if (it->m_host_buffer == mapped_ptr) {
        break;
      }
    }
    first = 0;
    ++it;
  }
  if (it == pipe->host_pipe_info->m_host_op_queue.end()) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(CL_INVALID_VALUE, pipe->context,
            "This is not a valid mapped pointer");
  }
  assert(it->m_op == MAP);

  // You shouldn't be trying to send over more data than you have mapped
  if (size_to_unmap > (it->m_op_size - it->m_size_sent)) {
    acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));
    ERR_RET(
        CL_INVALID_VALUE, pipe->context,
        "You are trying to upmap more mapped buffer space than you have left");
  }

  if (first == 0 || !pipe->host_pipe_info->binded) {
    // This is a queued map operation or we haven't binded to the channel yet.
    // Just fake the upmap and leave it for later
    it->m_size_sent += size_to_unmap;
    if (unmapped_size != NULL) {
      *unmapped_size = size_to_unmap;
    }
  } else {
    // Checking to make sure the mmd buffer is where it's supposed to be
    mmd_buffer = acl_get_hal()->hostchannel_get_buffer(
        pipe->host_pipe_info->m_physical_device_id,
        pipe->host_pipe_info->m_channel_handle, &buffer_size, &status);
    assert(status == 0);
    assert(mmd_buffer == (void *)((char *)mapped_ptr + it->m_size_sent));

    // This is the first operation in the queue. Send it to the mmd for transfer
    // to the device
    assert(pipe->host_pipe_info->m_host_op_queue.begin() == it);
    acked_size = acl_get_hal()->hostchannel_ack_buffer(
        pipe->host_pipe_info->m_physical_device_id,
        pipe->host_pipe_info->m_channel_handle, size_to_unmap, &status);
    assert(status == 0);
    if (unmapped_size != NULL) {
      *unmapped_size = acked_size;
    }
    it->m_size_sent += acked_size;
    assert(it->m_size_sent <= it->m_op_size);
    pipe->host_pipe_info->size_buffered -= acked_size;

    if (it->m_size_sent == it->m_op_size) {
      // This map operation is done now. Clean up.
      assert(acked_size == size_to_unmap);
      pipe->host_pipe_info->m_host_op_queue.erase(it);

      // Go through the rest of the queue and flush out any operations
      // that were blocked by this MAP operation
      l_clean_up_pending_pipe_ops(pipe);
    }
  }
  acl_mutex_unlock(&(pipe->host_pipe_info->m_lock));

  return CL_SUCCESS;
}

// Ideally this should be passed from the autodiscovery string.
static constexpr unsigned csr_pipe_address_offet = 8;

void acl_read_program_hostpipe(void *user_data, acl_device_op_t *op) {

  cl_event event = op->info.event;
  cl_int status = 0;
  size_t pulled_data = 0;
  bool blocking = event->cmd.info.host_pipe_dynamic_info.blocking;
  acl_assert_locked();

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  // Difference between event->cmd.info.host_pipe_dynamic_info
  // And host_pipe_info.
  // Event member contains dynamic information like data and size
  // The host_pipe_info stored in the dev_prog->program_hostpipe_map
  // Contains the static information of the pipe, like protocol

  assert(event->command_queue->device->loaded_bin != NULL &&
         "No loaded binary for read hostpipe");
  acl_device_program_info_t *dev_prog =
      event->command_queue->device->loaded_bin->get_dev_prog();
  auto host_pipe_info = dev_prog->program_hostpipe_map.at(
      std::string(event->cmd.info.host_pipe_dynamic_info.logical_name));
  acl_mutex_lock(&(host_pipe_info.m_lock));
  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  if (host_pipe_info.implement_in_csr) {
    // Here is the logic for CSR pipe read
    // Compiler initializes ready register to 1, if ready register exist
    // Non-Blocking uses_ready<true>
    //   1. if ready == 1, fail.
    //   2. Read data.
    //   3. write 1 to ready.

    // Blocking uses_ready<true>
    //   1. wait until ready = 0.
    //   2. read data.
    //   3. write 1 to ready.

    // uses_ready<false>
    // Both Blocking and NonBlocking
    //   1. Read data (always succeeds)

    unsigned long long parsed;
    uintptr_t data_reg, ready_reg;
    // Convert the CSR address to a pointer
    try {
      parsed = std::stoull(host_pipe_info.csr_address, nullptr);
    } catch (const std::exception &) {

      acl_set_device_op_execution_status(op, -1);
      return;
    }

    data_reg = static_cast<uintptr_t>(parsed);
    ready_reg = static_cast<uintptr_t>(
        parsed +
        csr_pipe_address_offet); // ready reg is data reg shift by 8 byte
    unsigned ready = 1;
    unsigned ready_value;
    unsigned *ready_value_pointer = &ready_value;

    if (host_pipe_info.is_stall_free == 0) {
      // If Blocking, wait until the ready register = 0
      // If Non-blocking, just read once and report failure if ready == 1
      do {
        acl_get_hal()->read_csr(host_pipe_info.m_physical_device_id, ready_reg,
                                (void *)ready_value_pointer,
                                (size_t)sizeof(uintptr_t));
      } while (blocking && ready_value != 0);

      // If non-blocking and ready bit is 1, set the op to fail.
      if (!blocking && ready_value == 1) {
        acl_mutex_unlock(&(host_pipe_info.m_lock));
        acl_set_device_op_execution_status(op, -1);
        return;
      }
    }
    // start the CSR read
    auto status =
        acl_get_hal()->read_csr(host_pipe_info.m_physical_device_id, data_reg,
                                event->cmd.info.host_pipe_dynamic_info.ptr,
                                event->cmd.info.host_pipe_dynamic_info.size);
    if (status != 0) {
      acl_mutex_unlock(&(host_pipe_info.m_lock));
      acl_set_device_op_execution_status(op, -1);
      return;
    }
    // Tell CSR it's ready if ready register exist
    if (host_pipe_info.is_stall_free == 0) {
      acl_get_hal()->write_csr(host_pipe_info.m_physical_device_id, ready_reg,
                               (void *)&ready, (size_t)sizeof(uintptr_t));
    }
  } else {
    // Non CSR Case
    if (host_pipe_info.num_side_band_signals == 0) {
      pulled_data = acl_get_hal()->hostchannel_pull(
          host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
          event->cmd.info.host_pipe_dynamic_info.ptr,
          event->cmd.info.host_pipe_dynamic_info.size, &status);
    } else {
      // This pipe has sideband signals, need to break into data section and
      // sideband signal sections
      pulled_data = l_pull_sideband_packet(
          host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
          event->cmd.info.host_pipe_dynamic_info.ptr,
          event->cmd.info.host_pipe_dynamic_info.size, host_pipe_info, status);
    }

    if (!blocking) {
      // If it is non-blocking read, we return with the success code right away
      // TODO: Change to pulled_data != pipe.width when sideband signals pipe
      // are fully implemented. Right now we consider the result is good as long
      // as pulled_data > 0.
      if (status != 0 || pulled_data == 0) {
        acl_mutex_unlock(&(host_pipe_info.m_lock));
        acl_set_device_op_execution_status(op, -1);
        return;
      }
    } else {
      // If it is a blocking read, this call won't return until the kernel
      // writes the data into the pipe.
      // TODO: Change to pulled_data == pipe.width when sideband signals pipe
      // are fully implemented. Right now we consider the result is good as long
      // as pulled_data > 0.
      while (status != 0 || pulled_data == 0) {
        if (host_pipe_info.num_side_band_signals == 0) {
          pulled_data = acl_get_hal()->hostchannel_pull(
              host_pipe_info.m_physical_device_id,
              host_pipe_info.m_channel_handle,
              event->cmd.info.host_pipe_dynamic_info.ptr,
              event->cmd.info.host_pipe_dynamic_info.size, &status);
        } else {
          pulled_data = l_pull_sideband_packet(
              host_pipe_info.m_physical_device_id,
              host_pipe_info.m_channel_handle,
              event->cmd.info.host_pipe_dynamic_info.ptr,
              event->cmd.info.host_pipe_dynamic_info.size, host_pipe_info,
              status);
        }
        acl_update_device_op_queue(&(acl_platform.device_op_queue));
      }
    }
  }

  acl_mutex_unlock(&(host_pipe_info.m_lock));
  acl_set_device_op_execution_status(op, CL_COMPLETE);
}

void acl_write_program_hostpipe(void *user_data, acl_device_op_t *op) {

  cl_int status;
  cl_event event = op->info.event;
  bool blocking = event->cmd.info.host_pipe_dynamic_info.blocking;
  acl_assert_locked();

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  // Difference between event->cmd.info.host_pipe_dynamic_info
  // And host_pipe_info.
  // Event member contains dynamic information like data and size
  // The host_pipe_info stored in the dev_prog->program_hostpipe_map
  // Contains the static information of the pipe, like protocol

  assert(event->command_queue->device->loaded_bin != NULL &&
         "No loaded binary for write hostpipe");
  acl_device_program_info_t *dev_prog =
      event->command_queue->device->loaded_bin->get_dev_prog();
  auto host_pipe_info = dev_prog->program_hostpipe_map.at(
      std::string(event->cmd.info.host_pipe_dynamic_info.logical_name));
  acl_mutex_lock(&(host_pipe_info.m_lock));
  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  if (host_pipe_info.implement_in_csr) {
    // Get CSR address

    // Here is the logic for CSR pipe write
    // Blocking uses_valid<true>:
    //   1. read valid reg, wait until valid is 0
    //   2. write to the pipe.
    //   3. write 1 to the valid.

    // Non-blocking uses_valid<true>
    //   1. read valid reg once ->return failure if valid is 1
    //   2. write to the pipe.
    //   3. write 1 to the valid.

    // uses_valid<false>
    // Both Blocking and NonBlocking
    //   1. Write data (always succeeds)

    unsigned long long parsed;
    uintptr_t data_reg, valid_reg;
    try {
      parsed = std::stoull(host_pipe_info.csr_address, nullptr);
    } catch (const std::exception &) {
      acl_set_device_op_execution_status(op, -1);
      return;
    }
    data_reg = static_cast<uintptr_t>(parsed);
    valid_reg = static_cast<uintptr_t>(
        parsed +
        csr_pipe_address_offet); // valid reg is data reg shift by 8 byte, move
                                 // this to the autodiscovery string maybe

    unsigned valid_value = 1;
    unsigned *valid_value_pointer = &valid_value;

    if (host_pipe_info.is_stall_free == 0) {
      if (blocking) {
        while (valid_value != 0) {
          acl_get_hal()->read_csr(host_pipe_info.m_physical_device_id,
                                  valid_reg, (void *)valid_value_pointer,
                                  (size_t)sizeof(uintptr_t));
        }
      } else {
        // Non-blocking, if valid reg is 1, return failure.
        acl_get_hal()->read_csr(host_pipe_info.m_physical_device_id, valid_reg,
                                (void *)valid_value_pointer,
                                (size_t)sizeof(uintptr_t));

        if (valid_value == 1) {
          acl_mutex_unlock(&(host_pipe_info.m_lock));
          acl_set_device_op_execution_status(op, -1);
          return;
        }
      }
    }

    // start the write
    auto status = acl_get_hal()->write_csr(
        host_pipe_info.m_physical_device_id, data_reg,
        event->cmd.info.host_pipe_dynamic_info.write_ptr,
        event->cmd.info.host_pipe_dynamic_info.size);

    if (status != 0) {
      acl_mutex_unlock(&(host_pipe_info.m_lock));
      acl_set_device_op_execution_status(op, -1);
      return;
    }

    if (host_pipe_info.is_stall_free == 0) {
      const unsigned valid = 1;
      acl_get_hal()->write_csr(host_pipe_info.m_physical_device_id, valid_reg,
                               (void *)&valid, (size_t)sizeof(uintptr_t));
    }

  } else {
    // Regular hostpipe
    // Attempt to write once
    if (host_pipe_info.num_side_band_signals == 0) {
      status = l_push_packet(host_pipe_info.m_physical_device_id,
                             host_pipe_info.m_channel_handle,
                             event->cmd.info.host_pipe_dynamic_info.write_ptr,
                             event->cmd.info.host_pipe_dynamic_info.size);
    } else {
      status = l_push_sideband_packet(
          host_pipe_info.m_physical_device_id, host_pipe_info.m_channel_handle,
          event->cmd.info.host_pipe_dynamic_info.write_ptr,
          event->cmd.info.host_pipe_dynamic_info.size, host_pipe_info);
    }
    if (!blocking) {
      // If it is non-blocking write, we return with the success/failure code
      // right away
      if (status != CL_SUCCESS) {
        acl_mutex_unlock(&(host_pipe_info.m_lock));
        acl_set_device_op_execution_status(op, -1);
        return;
      }
    } else {
      // If it's a blocking write, this function won't return until the write
      // success.
      while (status != CL_SUCCESS) {
        if (host_pipe_info.num_side_band_signals == 0) {
          status =
              l_push_packet(host_pipe_info.m_physical_device_id,
                            host_pipe_info.m_channel_handle,
                            event->cmd.info.host_pipe_dynamic_info.write_ptr,
                            event->cmd.info.host_pipe_dynamic_info.size);
        } else {
          status = l_push_sideband_packet(
              host_pipe_info.m_physical_device_id,
              host_pipe_info.m_channel_handle,
              event->cmd.info.host_pipe_dynamic_info.write_ptr,
              event->cmd.info.host_pipe_dynamic_info.size, host_pipe_info);
        }
        acl_update_device_op_queue(&(acl_platform.device_op_queue));
      }
    }
  }
  acl_mutex_unlock(&(host_pipe_info.m_lock));
  acl_set_device_op_execution_status(op, CL_COMPLETE);
}

// Submit an op to the device op queue to read hostpipe.
// Return 1 if we made forward progress, 0 otherwise.
cl_int acl_submit_read_program_hostpipe_device_op(cl_event event) {
  int result = 0;
  acl_assert_locked();

  // No user-level scheduling blocks this hostpipe read
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  // Already enqueued.
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_HOSTPIPE_READ,
                                  event); // TODO Change this to the READ op

  if (last_op) {
    // We managed to enqueue everything.
    event->last_device_op = last_op;
    acl_commit_proposed_device_ops(doq);
    result = 1;
  } else {
    // Back off, and wait until later when we have more space in the
    // device op queue.
    acl_forget_proposed_device_ops(doq);
  }
  return result;
}

// Submit an op to the device op queue to write hostpipe.
// Return 1 if we made forward progress, 0 otherwise.
cl_int acl_submit_write_program_hostpipe_device_op(cl_event event) {
  int result = 0;
  acl_assert_locked();

  // No user-level scheduling blocks this hostpipe write op
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  // Already enqueued.
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_HOSTPIPE_WRITE, event);

  if (last_op) {
    // We managed to enqueue everything.
    event->last_device_op = last_op;
    acl_commit_proposed_device_ops(doq);
    result = 1;
  } else {
    // Back off, and wait until later when we have more space in the
    // device op queue.
    acl_forget_proposed_device_ops(doq);
  }
  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadHostPipeINTEL(
    cl_command_queue command_queue, cl_program program, const char *pipe_symbol,
    cl_bool blocking_read, void *ptr, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {

  cl_int status = 0;

  // Get context from program, command_queue and event
  cl_context context = program->context;
  cl_device_id device = command_queue->device;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid pointer was provided to host data");
  }

  if (pipe_symbol == NULL) {
    ERR_RET(CL_INVALID_VALUE, context, "Invalid Pipe Symbol");
  }

  assert(device->loaded_bin != NULL &&
         "No loaded binary for enqueue hostpipe read");
  acl_device_program_info_t *dev_prog = device->loaded_bin->get_dev_prog();

  auto search = dev_prog->program_hostpipe_map.find(std::string(pipe_symbol));

  if (search == dev_prog->program_hostpipe_map.end()) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Pipe Symbol is not found in the device");
  }

  if (search == dev_prog->program_hostpipe_map.end()) {

    ERR_RET(CL_INVALID_VALUE, context,
            "Pipe Symbol is not found in the device");
  }

  cl_event local_event = 0; // used for blocking

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_READ_HOST_PIPE_INTEL, &local_event);

  if (status != CL_SUCCESS)
    return status;

  local_event->cmd.info.host_pipe_dynamic_info.size = size;
  local_event->cmd.info.host_pipe_dynamic_info.ptr = ptr;
  local_event->cmd.info.host_pipe_dynamic_info.blocking = blocking_read;
  local_event->cmd.info.host_pipe_dynamic_info.logical_name = pipe_symbol;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (blocking_read) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteHostPipeINTEL(
    cl_command_queue command_queue, cl_program program, const char *pipe_symbol,
    cl_bool blocking_write, const void *ptr, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {

  cl_int status = 0;
  // Get context from program, command_queue and event
  cl_context context = program->context;
  cl_device_id device = command_queue->device;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid pointer was provided to host data");
  }

  if (pipe_symbol == NULL) {
    ERR_RET(CL_INVALID_VALUE, context, "Invalid Pipe Symbol");
  }

  assert(device->loaded_bin != NULL &&
         "No loaded binary for enqueue hostpipe write");
  acl_device_program_info_t *dev_prog = device->loaded_bin->get_dev_prog();

  auto search = dev_prog->program_hostpipe_map.find(std::string(pipe_symbol));

  if (search == dev_prog->program_hostpipe_map.end()) {

    ERR_RET(CL_INVALID_VALUE, context,
            "Pipe Symbol is not found in the device");
  }

  cl_event local_event = 0; // used for blocking

  // Create an event/command to actually move the data at the appropriate time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_WRITE_HOST_PIPE_INTEL, &local_event);

  if (status != CL_SUCCESS)
    return status;

  local_event->cmd.info.host_pipe_dynamic_info.size = size;
  local_event->cmd.info.host_pipe_dynamic_info.write_ptr = ptr;
  local_event->cmd.info.host_pipe_dynamic_info.blocking = blocking_write;
  local_event->cmd.info.host_pipe_dynamic_info.logical_name = pipe_symbol;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (blocking_write) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }

  return CL_SUCCESS;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

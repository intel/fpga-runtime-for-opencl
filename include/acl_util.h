// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_UTIL_H
#define ACL_UTIL_H

#include <cstring>
#include <iostream>

#include <CL/opencl.h>

#include "acl_globals.h"
#include "acl_hal.h"
#include "acl_thread.h"
#include "acl_types.h"
#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

int acl_platform_is_valid(cl_platform_id platform);
int acl_device_is_valid(cl_device_id device);
int acl_device_is_valid_ptr(cl_device_id device);
int acl_context_is_valid(cl_context context);
int acl_command_queue_is_valid(cl_command_queue command_queue);
int acl_mem_is_valid(cl_mem mem);
int acl_program_is_valid(cl_program program);
int acl_kernel_is_valid(cl_kernel kernel);
int acl_kernel_is_valid_ptr(cl_kernel kernel);
void acl_context_callback(struct _cl_context *, const std::string);
int acl_sampler_is_valid(cl_sampler sampler);
int acl_sampler_is_valid_ptr(cl_sampler sampler);
int acl_pipe_is_valid_pointer(cl_mem mem_obj, cl_kernel kernel);

void acl_track_object(acl_cl_object_type_t type, void *object);
void acl_untrack_object(void *object);
void acl_receive_device_exception(unsigned physical_device_id,
                                  CL_EXCEPTION_TYPE_INTEL exception_type,
                                  void *user_private_info, size_t user_cb);
void acl_release_leaked_objects(void);

/////////////////////
// Event object tests.
//
// These are so pervasively used I put them here rather than in event.h

// For events:
//    valid implies valid_ptr
//    live implies valid
//    !done implies live

// Is this event in use by the system?
// (If it's in use by the user, i.e. has refcount > 0, then it is
// considered in use by the system.)
// When the surrounding command queue becomes invalid, this event
// becomes invalid as well.
//
// An event can be "not-live", and still be "valid".
// This occurs when it has completed and notified all its downstream
// dependent events, but it has not yet been cleaned up during
// queue processing.
int acl_event_is_valid(cl_event event);

// Is this event alive, i.e. meet any of the following criteria:
//    a) The user has an outstanding reference to it.
//    b) Another event still depends on this event
//       (Either this event is not yet done,
//       or this event is done and we have not have notified
//       those downstream events that this event is done)
//    c) The underlying command (or barrier/marker) is not yet finished its
//       activity.
//       That is, we are waiting for execution_status to get to
//       CL_COMPLETE or an error.
//       That is, ! acl_event_is_done( event )
int acl_event_is_live(cl_event event);

// Return non-zero if the event processing is finished: complete or error
// Results undefined (including crash) if   ! acl_event_is_valid_ptr(event)
static inline int acl_event_is_done(const cl_event event) {
  return (event->execution_status == CL_COMPLETE ||
          event->execution_status < 0);
}

// given the parameter of memcpy, asserts if the parameters are invalid (i.e.
// overlap or out of bound access) otherwise performs memcpy. This call should
// replace all calls to memcpy
static inline void safe_memcpy(void *dst, const void *src, uintptr_t count,
                               uintptr_t dst_mem_size, uintptr_t src_mem_size) {
  // check that the pointers are not null and memory being accessed does not
  // overlap
  if (dst == NULL || src == NULL) {
    assert(0 && "Program terminated, invalid memory access");
  }
  // check that the access will not go out of bound
  if (count > dst_mem_size || count > src_mem_size) {
    assert(0 && "Program terminated, buffer overflow");
  }
  // check that the pointers are not null and memeory being accessed does not
  // overlap
  if ((dst <= src && (uintptr_t)dst + count > (uintptr_t)src) ||
      (src <= dst && (uintptr_t)src + count > (uintptr_t)dst)) {
    assert(0 && "Program terminated, invalid memory access");
  }
  memcpy(dst, src, count);
}

// For debugging.  These dump text via the HAL's printf API.
#ifdef ACL_DEBUG
void acl_dump_event(cl_event event);
void acl_dump_mem(cl_mem mem);
#else
#define acl_dump_event(x)
#define acl_dump_event_dependency(x)
#define acl_dump_mem(x)
#endif
/////////////////////

// This macro is used to signal failure from a function via "errcode_ret"
// and return 0.
#define BAIL(STATUS)                                                           \
  do {                                                                         \
    if (errcode_ret) {                                                         \
      *errcode_ret = (STATUS);                                                 \
    }                                                                          \
    return 0;                                                                  \
  } while (0)

// This is used to callback for a context error, assuming C is an
// initialized context.
#define BAIL_INFO(STATUS, C, STR)                                              \
  do {                                                                         \
    acl_context_callback(C, STR);                                              \
    BAIL(STATUS);                                                              \
  } while (0)

#define ERR_RET(STATUS, C, STR)                                                \
  do {                                                                         \
    acl_context_callback(C, STR);                                              \
    return STATUS;                                                             \
  } while (0)

// Caller only partly specified the buffer?
// Caller isn't asking for any info at all?
#define VALIDATE_ARRAY_OUT_ARGS(buf_size, buf, answer_size_out, context)       \
  do {                                                                         \
    if (buf && buf_size <= 0) {                                                \
      acl_context_callback(context,                                            \
                           #buf " is specified but " #buf_size " is zero");    \
      return CL_INVALID_VALUE;                                                 \
    }                                                                          \
    if (buf == 0 && buf_size > 0) {                                            \
      acl_context_callback(context, #buf " is not specified but " #buf_size    \
                                         " is positive");                      \
      return CL_INVALID_VALUE;                                                 \
    }                                                                          \
    if (answer_size_out == 0 && buf == 0) {                                    \
      acl_context_callback(context,                                            \
                           #buf " and " #answer_size_out " are both zero");    \
      return CL_INVALID_VALUE;                                                 \
    }                                                                          \
  } while (0)

// On 32-bit ARM, pointers are 32-bit. 2GB value becomes a huge negative number
// unless it's cast to a 64-bit integer. Need to cast to size_t first to avoid
// warning of "casting pointer to integer of different size". Note that using
// size_t and not ptrdiff_t because size_t is unsigned and hence can hold 2GB
// as a positive number.
#define ACL_RANGE_SIZE(R)                                                      \
  (((cl_ulong)(size_t)((R).next)) - ((cl_ulong)(size_t)((R).begin)))

// Result handling.

// How is the result expressed?
typedef enum {
  RESULT_TYPE_CL_BOOL,
  RESULT_TYPE_ENUM, // cheat and store as an int
  RESULT_TYPE_BITFIELD,
  RESULT_TYPE_INT,
  RESULT_TYPE_UINT,
  RESULT_TYPE_ULONG,
  RESULT_TYPE_SIZE_T,
  RESULT_TYPE_SIZE_T3,
  RESULT_TYPE_STR,
  RESULT_TYPE_PTR, // the pointer itself is the value
  RESULT_TYPE_BUF, // the pointed-at data is the value, and we know the size
  RESULT_TYPE_IMAGE_FORMAT
} result_type;
// This is the result value.
typedef union {
  cl_bool bool_value;
  cl_int int_value;
  cl_bitfield bitfield_value;
  cl_uint uint_value;
  cl_ulong ulong_value;
  size_t size_t_value;
  size_t size_t3_value[3];
  const char *charp_value;
  const void *voidp_value;
  cl_image_format image_format_value;
} result_value;

typedef struct {
  result_type type;
  result_value value;
  size_t size; // How much data will or would be written to the param_value?
               // 0 means invalid.
} acl_result_t;

/* Use this macro to initialie the "result" variable.
 * Avoids C4701warnings in MSVC.
 */
#define RESULT_INIT                                                            \
  {                                                                            \
    result.type = RESULT_TYPE_CL_BOOL;                                         \
    result.value.bool_value = 0;                                               \
    result.size = 0;                                                           \
  }

#define RESULT_BOOL(X)                                                         \
  {                                                                            \
    result.type = RESULT_TYPE_CL_BOOL;                                         \
    result.value.bool_value = X;                                               \
    result.size = sizeof(cl_bool);                                             \
  }
#define RESULT_ENUM(X)                                                         \
  {                                                                            \
    result.type = RESULT_TYPE_ENUM;                                            \
    result.value.int_value = (int)(X);                                         \
    result.size = sizeof(cl_int);                                              \
  }
#define RESULT_BITFIELD(X)                                                     \
  {                                                                            \
    result.type = RESULT_TYPE_BITFIELD;                                        \
    result.value.bitfield_value = (cl_bitfield)(X);                            \
    result.size = sizeof(cl_bitfield);                                         \
  }
#define RESULT_INT(X)                                                          \
  {                                                                            \
    result.type = RESULT_TYPE_INT;                                             \
    result.value.int_value = X;                                                \
    result.size = sizeof(cl_int);                                              \
  }
#define RESULT_UINT(X)                                                         \
  {                                                                            \
    result.type = RESULT_TYPE_UINT;                                            \
    result.value.uint_value = X;                                               \
    result.size = sizeof(cl_uint);                                             \
  }
#define RESULT_ULONG(X)                                                        \
  {                                                                            \
    result.type = RESULT_TYPE_ULONG;                                           \
    result.value.ulong_value = X;                                              \
    result.size = sizeof(cl_ulong);                                            \
  }
#define RESULT_SIZE_T(X)                                                       \
  {                                                                            \
    result.type = RESULT_TYPE_SIZE_T;                                          \
    result.value.size_t_value = X;                                             \
    result.size = sizeof(size_t);                                              \
  }
#define RESULT_SIZE_T3(X, Y, Z)                                                \
  {                                                                            \
    result.type = RESULT_TYPE_SIZE_T3;                                         \
    result.value.size_t3_value[0] = X;                                         \
    result.value.size_t3_value[1] = Y;                                         \
    result.value.size_t3_value[2] = Z;                                         \
    result.size = 3 * sizeof(size_t);                                          \
  }
#define RESULT_STR(X)                                                          \
  {                                                                            \
    result.value.charp_value = X;                                              \
    result.type = RESULT_TYPE_STR;                                             \
    result.size = strnlen(result.value.charp_value, MAX_NAME_SIZE) + 1;        \
  }
#define RESULT_PTR(X)                                                          \
  {                                                                            \
    result.value.voidp_value = X;                                              \
    result.type = RESULT_TYPE_PTR;                                             \
    result.size = sizeof(result.value.voidp_value);                            \
  }
#define RESULT_BUF(X, SIZE)                                                    \
  {                                                                            \
    result.value.voidp_value = X;                                              \
    result.type = RESULT_TYPE_BUF;                                             \
    result.size = SIZE;                                                        \
  }
#define RESULT_IMAGE_FORMAT(X)                                                 \
  {                                                                            \
    result.type = RESULT_TYPE_IMAGE_FORMAT;                                    \
    result.value.image_format_value = X;                                       \
    result.size = sizeof(cl_image_format);                                     \
  }

// Probably better as a function.
#define RESULT_COPY(DEST, SIZE)                                                \
  switch (result.type) {                                                       \
  case RESULT_TYPE_CL_BOOL:                                                    \
    safe_memcpy(DEST, &result.value.bool_value, result.size, SIZE,             \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_ENUM: /* same as int */                                     \
  case RESULT_TYPE_INT:                                                        \
    safe_memcpy(DEST, &result.value.int_value, result.size, SIZE,              \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_UINT:                                                       \
    safe_memcpy(DEST, &result.value.uint_value, result.size, SIZE,             \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_BITFIELD:                                                   \
    safe_memcpy(DEST, &result.value.bitfield_value, result.size, SIZE,         \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_ULONG:                                                      \
    safe_memcpy(DEST, &result.value.ulong_value, result.size, SIZE,            \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_SIZE_T:                                                     \
    safe_memcpy(DEST, &result.value.size_t_value, result.size, SIZE,           \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_SIZE_T3:                                                    \
    safe_memcpy(DEST, &(result.value.size_t3_value[0]), result.size, SIZE,     \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_STR:                                                        \
    safe_memcpy(DEST, result.value.charp_value, result.size, SIZE,             \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_BUF:                                                        \
    safe_memcpy(DEST, result.value.voidp_value, result.size, SIZE,             \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_PTR:                                                        \
    safe_memcpy(DEST, &result.value.voidp_value, result.size, SIZE,            \
                result.size);                                                  \
    break;                                                                     \
  case RESULT_TYPE_IMAGE_FORMAT:                                               \
    safe_memcpy(DEST, &result.value.image_format_value, result.size, SIZE,     \
                result.size);                                                  \
    break;                                                                     \
  }

/* Used in Unit Tests: flags if we should check the magic value for the type*/
extern std::unordered_map<std::string, bool> l_allow_invalid_type;

template <typename T> inline void acl_set_allow_invalid_type(bool allow) {
  if (ACL_DEBUG_INVALID_CL_OBJECTS) {
    l_allow_invalid_type[typeid(T).name()] = allow;
  }
}

template <typename T> inline acl_object_magic_t get_magic_val() {
  // You can generate more values like this:
  // python -c 'import random; print("0x%016X" % random.randrange(16**16))'
  if (std::is_same<T, cl_mem>::value) {
    return 0xAC509B16791FEAFD;
  } else if (std::is_same<T, cl_program>::value) {
    return 0x7529D19E1C62D6A5;
  } else if (std::is_same<T, cl_context>::value) {
    return 0xB55CCA727F0E768F;
  } else if (std::is_same<T, cl_event>::value) {
    return 0x969017A8DA3D536D;
  } else if (std::is_same<T, cl_kernel>::value) {
    return 0x66D663F48765AE0B;
  } else if (std::is_same<T, cl_command_queue>::value) {
    return 0xA870E4BDC8516A81;
  } else {
    return 0xDEADBEEFDEADBEEF;
  }
}

template <typename T> inline int acl_is_valid_ptr(const T &cl_object) {
  std::string type_name = typeid(T).name();
  auto it = l_allow_invalid_type.find(type_name);
  bool skip_error_msg = false;
  if (it != l_allow_invalid_type.end()) {
    skip_error_msg = it->second;
  }
  if (ACL_DEBUG_INVALID_CL_OBJECTS && !skip_error_msg && cl_object) {
    if (cl_object->magic != get_magic_val<T>()) {
      std::cout << "Error: invalid \"" << type_name
                << "\" object accessed at address " << cl_object << std::endl;
      return 0;
    }
  }
  return cl_object && (cl_object->magic == get_magic_val<T>());
}

template <typename T> inline static void acl_reset_ref_count(T object) {
  object->refcount = 0;
}

template <typename T> inline static void acl_retain(T object) {
  object->refcount++;
}

template <typename T> inline static void acl_release(T object) {
  object->refcount--;
}

template <typename T> inline static cl_uint acl_ref_count(const T object) {
  return object->refcount;
}

template <typename T> inline static bool acl_is_retained(const T object) {
  return object->refcount > 0;
}

static inline bool is_SOC_device() {
#ifdef ACL_HOST_MEMORY_SHARED
  return true;
#else
  return false;
#endif
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif

// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// Printf
// ========
//
// Handle the parsing and printing out of printf output from the kernel
//
// (see Trasforms/FPGATransforms/TransformPrintf.cpp for description
//  of the printf data format in memory)
//

// System headers.
#include <cassert>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_kernel.h>
#include <acl_printf.h>
#include <acl_program.h>
#include <acl_svm.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

static size_t l_dump_printf_buffer(cl_event event, cl_kernel kernel,
                                   unsigned size);
static void decode_string(std::string &print_data);

// stores to global memory are bound by smallest global memory bandwidth we
// support (i.e 32-bytes) WARNING: this should match the value in
// Trasforms/FPGATransforms/TransformPrintf.cpp
#define SMALLEST_GLOBAL_MEMORY_BANDWIDTH 32

typedef enum {
  LS_NONE = 0,
  LS_HH,
  LS_H,
  LS_HL, // valid only for vector types
  LS_L
} Length_Specifier;

typedef enum {
  PRINTABLE_TYPE_C = 0,
  PRINTABLE_TYPE_D,
  PRINTABLE_TYPE_I,
  PRINTABLE_TYPE_O,
  PRINTABLE_TYPE_U,
  PRINTABLE_TYPE_x,
  PRINTABLE_TYPE_X,
  PRINTABLE_TYPE_s,
  PRINTABLE_TYPE_f,
  PRINTABLE_TYPE_F,
  PRINTABLE_TYPE_e,
  PRINTABLE_TYPE_E,
  PRINTABLE_TYPE_g,
  PRINTABLE_TYPE_G,
  PRINTABLE_TYPE_a,
  PRINTABLE_TYPE_A,
  PRINTABLE_TYPE_P,
  PRINTABLE_TYPE_LAST,
} Printable_Type;

static const char Printable_Type_Identifier[PRINTABLE_TYPE_LAST] = {
    'c', 'd', 'i', 'o', 'u', 'x', 'X', 's', 'f',
    'F', 'e', 'E', 'g', 'G', 'a', 'A', 'p',
};

typedef struct {
  Printable_Type _type;
  std::string _conversion_string;
} Printable_Data_Elem;

static float convert_half_to_float(unsigned short half)
// This method converts a 16-bit half-precision FP number to a
// single-precision floating point number.
// Copy/paste from /ip/test/common/fp_convert_from_half/fpgen#1.cpp
{
  unsigned int sign = (unsigned int)(half & 0x8000) << 16;
  unsigned int exponent = (unsigned int)(half & 0x7C00) >> 10;
  unsigned int mantissa = (unsigned int)(half & 0x03ff) << 13;
  unsigned int result;

  // Handle special case
  if (exponent == 0x01f) // max exponent
  {
    exponent = 255; // Max exponent in the single-precision format.
  } else if (exponent != 0) {
    // exponent -15 removes the bias in half precision, and +127 adds the bias
    // for single-precision.
    exponent = (127 - 15) + exponent;
  }

  exponent = exponent << 23;
  result = sign | exponent | mantissa;
  return (*(float *)&result);
}

// find length specifier strings in the given conversion_string
static Length_Specifier
get_length_specifier(const std::string &conversion_string) {
  for (unsigned i = 0; i < conversion_string.length(); ++i) {
    if (conversion_string[i] == 'h') {
      if (i + 1 == conversion_string.length())
        return LS_H;
      else if (conversion_string[i + 1] == 'l')
        return LS_HL;
      else if (conversion_string[i + 1] == 'h')
        return LS_HH;
      else
        return LS_H;
    } else if (conversion_string[i] == 'l') {
      return LS_L;
    }
  }
  return LS_NONE;
}

static unsigned get_llvm_data_size_char() {
  return 4; // char's are always converted to i32 in IR
}

// these are the size of types in LLVM IR
static unsigned get_llvm_data_size_int_uint(Length_Specifier length,
                                            int vector_type) {

  // inconsistent length specifier, HL valid only for vector types
  if (length == LS_HL && !vector_type)
    return 0;

  if (length == LS_NONE || length == LS_HL)
    return 4; // HL only in vector type
  else if (!vector_type && length == LS_HH)
    return 4; // char's are converted to i32 (if not in vector)
  else if (vector_type && length == LS_HH)
    return 1;
  else if (length == LS_H)
    return 2;
  else if (length == LS_L)
    return 8;
  return 0;
}

static unsigned get_llvm_data_size_float_double(Length_Specifier length,
                                                int vector_type) {

  if (length == LS_HH)
    return 0;

  if (!vector_type && length == LS_H)
    return 0;

  // half float
  if (vector_type && length == LS_H)
    return 2;

  // interpret as double
  if (!vector_type || (vector_type && length == LS_L)) {
    return 8;
  }
  return 4;
}

static unsigned get_llvm_data_size_ptr() {
  return 8; // pointer's are i64
}

static unsigned get_llvm_data_size_string() {
  return 4; // Transform printf pass inserts string index (i32)
}

// verify the size of various host types (used to read from printf buffer)
// against the llvm IR types (used to fill the printf buffer)
static int verify_types() {

  if (
      // char's are not converted in vector types
      sizeof(char) == get_llvm_data_size_int_uint(LS_HH, 1) &&
      sizeof(unsigned char) == get_llvm_data_size_int_uint(LS_HH, 1) &&

      sizeof(int) == get_llvm_data_size_char() &&
      sizeof(int) == get_llvm_data_size_int_uint(LS_HH, 0) &&
      sizeof(short) == get_llvm_data_size_int_uint(LS_H, 0) &&
      sizeof(int) == get_llvm_data_size_int_uint(LS_HL, 1) &&
      sizeof(long long) == get_llvm_data_size_int_uint(LS_L, 0) &&

      sizeof(unsigned int) == get_llvm_data_size_int_uint(LS_HH, 0) &&
      sizeof(unsigned short) == get_llvm_data_size_int_uint(LS_H, 0) &&
      sizeof(unsigned int) == get_llvm_data_size_int_uint(LS_HL, 1) &&
      sizeof(unsigned long long) == get_llvm_data_size_int_uint(LS_L, 0) &&
      // vector floats not are always converted to double
      sizeof(float) == get_llvm_data_size_float_double(LS_NONE, 1) &&
      // vector floats are converted to double if LS_L is given
      sizeof(double) == get_llvm_data_size_float_double(LS_L, 1) &&
      // vector half floats are 2-bytes, will be read as short
      sizeof(unsigned short) == get_llvm_data_size_float_double(LS_H, 1) &&
      // non-vector floats are always converted to double
      sizeof(double) == get_llvm_data_size_float_double(LS_NONE, 0) &&
      sizeof(unsigned long long) == get_llvm_data_size_ptr() &&
      sizeof(unsigned int) == get_llvm_data_size_string()) {
    return 1;
  }
  return 0;
}

static unsigned get_llvm_data_size(Printable_Type type, Length_Specifier length,
                                   int vector_type) {

  if (type == PRINTABLE_TYPE_C) {
    return get_llvm_data_size_char();
  } else if (type == PRINTABLE_TYPE_P) {
    return get_llvm_data_size_ptr();
  } else if (type == PRINTABLE_TYPE_s) {
    return get_llvm_data_size_string();
  } else if (type == PRINTABLE_TYPE_D || type == PRINTABLE_TYPE_I ||
             type == PRINTABLE_TYPE_O || type == PRINTABLE_TYPE_U ||
             type == PRINTABLE_TYPE_x || type == PRINTABLE_TYPE_X) {
    return get_llvm_data_size_int_uint(length, vector_type);
  } else if (type == PRINTABLE_TYPE_f || type == PRINTABLE_TYPE_F ||
             type == PRINTABLE_TYPE_e || type == PRINTABLE_TYPE_E ||
             type == PRINTABLE_TYPE_g || type == PRINTABLE_TYPE_G ||
             type == PRINTABLE_TYPE_a || type == PRINTABLE_TYPE_A) {
    // non-vector floats are always converted to double
    return get_llvm_data_size_float_double(length, vector_type);
  }
  assert(0);
  return 0;
}

// assign the format string from auto-discovery
// return 1 if format string was successfully read
// return 0 if format string was not successfully read
static int
get_format_string(const unsigned format_string_index,
                  std::string &format_string,
                  const std::vector<acl_printf_info_t> &printf_infos) {
  for (const auto &p : printf_infos) {
    if (p.index == format_string_index) {
      format_string = p.format_string;
      return 1;
    }
  }

  return 0;
}

// read a single char at offset and print it
// char's are extended to 4-bytes in kernel
static void print_data_elem_char(char *buffer, unsigned offset,
                                 const std::string &format_string) {

  int data = *((int *)&buffer[offset]);
#ifdef DEBUG
  if (debug_mode > 0) {
    printf(" print_data_elem_char:");
    printf(" format_string=%s", format_string.c_str());
    printf(" data=%u", data);
    printf(" output=");
  }
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
  printf(format_string.c_str(), (char)data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef DEBUG
  acl_print_debug_msg("\n");
#endif
}

// read a single int at offset and print it
static void print_data_elem_int(char *buffer, unsigned offset,
                                const std::string &format_string,
                                Length_Specifier length, int vector_type) {

#ifdef DEBUG
  printf(" print_data_elem_int:");
  printf(" format_string=%s", format_string.c_str());
#endif

  if (length == LS_HH && vector_type) {
    char data = *((char *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%d", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_NONE || length == LS_HH || length == LS_HL) {
    int data = *((int *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%d", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_H) {
    short data = *((short *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%hd", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_L) {
    long long data = *((long long *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%lld", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }

#ifdef DEBUG
  printf("\n");
#endif
}

// read a single uint at offset and print it
static void print_data_elem_uint(char *buffer, unsigned offset,
                                 const std::string &format_string,
                                 Length_Specifier length, int vector_type) {

#ifdef DEBUG
  printf(" print_data_elem_int:");
  printf(" format_string=%s", format_string.c_str());
#endif

  if (length == LS_HH && vector_type) {
    unsigned char data = *((unsigned char *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%d", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_NONE || length == LS_HH || length == LS_HL) {
    unsigned int data = *((unsigned int *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%d", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_H) {
    unsigned short data = *((unsigned short *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%hd", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else if (length == LS_L) {
    unsigned long long data = *((unsigned long long *)&buffer[offset]);
#ifdef DEBUG
    printf(" data=%lld", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }

#ifdef DEBUG
  printf("\n");
#endif
}

// read a single double at offset and print it
// (NOTE: all floats (except float vectors) are converted to double in the IR)
static void print_data_elem_float(char *buffer, unsigned offset,
                                  const std::string &format_string,
                                  Length_Specifier length, int vector_type) {

  // print a double
  if (!vector_type || (vector_type && length == LS_L)) {
    double data = *((double *)&buffer[offset]);
#ifdef DEBUG
    printf(" print_data_elem_double:");
    printf(" format_string=%s", format_string);
    printf(" data=%f", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }
  // print a half float
  else if (vector_type && length == LS_H) {
    unsigned short temp = *((unsigned short *)&buffer[offset]);
    float data = convert_half_to_float(temp);
#ifdef DEBUG
    printf(" print_data_elem_double:");
    printf(" format_string=%s", format_string);
    printf(" data=%f", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  } else {
    float data = *((float *)&buffer[offset]);
#ifdef DEBUG
    printf(" print_data_elem_float:");
    printf(" format_string=%s", format_string);
    printf(" data=%f", data);
    printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
    printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }

#ifdef DEBUG
  printf("\n");
#endif
}

// read a single float at offset and print it
// (NOTE: floats are converted to double in the IR)
static void print_data_elem_ptr(char *buffer, unsigned offset,
                                const std::string &format_string) {

  unsigned long long data = *((unsigned long long *)&buffer[offset]);
#ifdef DEBUG
  printf(" print_data_elem_ptr:");
  printf(" format_string=%s", format_string);
  printf(" data=%lld", data);
  printf(" output=");
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
  printf(format_string.c_str(), data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef DEBUG
  printf("\n");
#endif
}

// read the index of the literal string, get the string from printf infos, print
// it
static void
print_data_elem_string(char *buffer, unsigned offset,
                       std::string &format_string,
                       const std::vector<acl_printf_info_t> &printf_infos) {
  unsigned int literal_string_index = *((unsigned int *)&buffer[offset]);
  std::string literal_string;

  int success =
      get_format_string(literal_string_index, literal_string, printf_infos);

  // corrupt data in memory, fail silently, may be able to print the remaining
  // data
  if (!success)
    return;

#ifdef DEBUG
  printf(" print_data_elem_string:");
  printf(" format_string=%s", format_string);
  printf(" literal_string_index=%d", literal_string_index);
  printf(" literal_string=%s", literal_string);
  printf(" output=");
#endif

  // handle special characters
  // before printing
  decode_string(literal_string);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774) // warning C4774: 'printf' : format string is
                                // not a string literal
#endif
  printf(format_string.c_str(), literal_string.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef DEBUG
  printf("\n");
#endif
}

static void print_data_elem(Printable_Type type, char *buffer, unsigned offset,
                            std::string &format_string, Length_Specifier length,
                            const std::vector<acl_printf_info_t> &printf_infos,
                            int vector_type) {

  if (type == PRINTABLE_TYPE_C) {
    print_data_elem_char(buffer, offset, format_string);
  } else if (type == PRINTABLE_TYPE_P) {
    print_data_elem_ptr(buffer, offset, format_string);
  } else if (type == PRINTABLE_TYPE_s) {
    print_data_elem_string(buffer, offset, format_string, printf_infos);
  } else if (type == PRINTABLE_TYPE_D || type == PRINTABLE_TYPE_I) {
    print_data_elem_int(buffer, offset, format_string, length, vector_type);
  } else if (type == PRINTABLE_TYPE_O || type == PRINTABLE_TYPE_U ||
             type == PRINTABLE_TYPE_x || type == PRINTABLE_TYPE_X) {
    print_data_elem_uint(buffer, offset, format_string, length, vector_type);
  } else if (type == PRINTABLE_TYPE_f || type == PRINTABLE_TYPE_F ||
             type == PRINTABLE_TYPE_e || type == PRINTABLE_TYPE_E ||
             type == PRINTABLE_TYPE_g || type == PRINTABLE_TYPE_G ||
             type == PRINTABLE_TYPE_a || type == PRINTABLE_TYPE_A) {
    // (all floats (except float vectors) are converted to double in the IR)
    print_data_elem_float(buffer, offset, format_string, length, vector_type);
  } else {
    assert(0);
  }
}

// Handle vector types by replicating the conversion string for each
// vector element
static int
modify_conversion_string_for_vectors(std::string &conversion_string) {
  // see if the conversion string contains v*, where * can be 2, 3, 4, 8, 16
  int found_v = -1;
  std::string vector_size_string(2, '\0');
  std::stringstream non_vector_ss;
  for (unsigned i = 0; i < conversion_string.length(); i++) {
    if (conversion_string[i] == 'v') {
      // there is more than one v
      if (found_v != -1)
        return -1;
      found_v = static_cast<int>(i);
    } else if (found_v != -1 && i == static_cast<unsigned>((found_v + 1))) {
      if (conversion_string[i] <= '9' && conversion_string[i] >= '0') {
        vector_size_string[0] = conversion_string[i];
      } else
        return -1; // v followed by no integer
    } else if (found_v != -1 && i == static_cast<unsigned>((found_v + 2))) {
      if (conversion_string[i] <= '9' && conversion_string[i] >= '0') {
        vector_size_string[1] = conversion_string[i];
      } else
        non_vector_ss << conversion_string[i];
    } else {
      non_vector_ss << conversion_string[i];
    }
  }

  // no vector type
  if (found_v == -1)
    return 1;

  int vector_size = std::stoi(vector_size_string);
  std::string non_vector_string = non_vector_ss.str();

#ifdef DEBUG
  printf("vector_size=%d\n", vector_size);
  printf("non_vector_string=%s\n", non_vector_string.c_str());
  printf("non_vector_string_length=%d\n", non_vector_string.length());
#endif

  // copy non_vector_string into conversion_string
  // remove length specifier "hl" from the string, this is special for opencl
  // vector types and standard C printf does not understand it
  std::stringstream conversion_ss;
  for (unsigned i = 0; i < non_vector_string.length(); i++) {
    if (non_vector_string[i] == 'h' && i != (non_vector_string.length() - 1) &&
        non_vector_string[i + 1] == 'l') {
      i++;
      continue;
    }
    conversion_ss << non_vector_string[i];
  }
  conversion_string = conversion_ss.str();

  return vector_size;
}

// Read the format string starting from location ptr, as long as
// end_of_string is not reached.
// Extract string to be printed out and the data that needs be to
// be converted.
// For instance, for "mydata=%4.2f", the non_conversion_string is "mydata="
// the data_elem is PRINTABLE_TYPE_FLOAT, its _conversion_string is "%4.2f"
static std::string::const_iterator get_data_elem_at_offset(
    std::string::const_iterator ptr, std::string::const_iterator end_of_string,
    Printable_Data_Elem &data_elem, std::string &non_conversion_string) {

  std::stringstream ss;

  // each data conversion starts with "%", find the first one
  while (ptr != end_of_string) {

    // %% prints as %
    if ((ptr + 1) != end_of_string && *ptr == '%' && *(ptr + 1) == '%') {
      ss << '%';
      ptr += 2;
    } else {
      if (*ptr == '%')
        break;
      ss << *ptr;
      ptr++;
    }
  }
  non_conversion_string = ss.str();
  if (ptr == end_of_string)
    return end_of_string; // end of parsing
  assert(*ptr == '%');

  // advance until we reach c, u, d, f, etc. or end_of_string
  ss.str("");
  do {
    ss << *ptr;
    assert((int)PRINTABLE_TYPE_C == 0);
    for (int current_type = (int)PRINTABLE_TYPE_C;
         current_type != (int)PRINTABLE_TYPE_LAST; current_type++) {
      if (*ptr == Printable_Type_Identifier[current_type]) {
        data_elem._type = (Printable_Type)current_type;
        data_elem._conversion_string = ss.str();
        return ptr + 1;
      }
    }
    ptr++;
  } while (ptr != end_of_string);
  data_elem._conversion_string = ss.str();

  // we found % but not followed by c, u, d, f
  return end_of_string;
}

static size_t l_dump_printf_buffer(cl_event event, cl_kernel kernel,
                                   unsigned size) {
  unsigned global_offset;        // the location in the printf buffer
  unsigned single_printf_offset; // the offset of a single printf
  void (*hal_dma_fn)(cl_event, const void *, void *, size_t) = 0;
  int src_on_host = 1;
  size_t dumped_buffer_size = 0;
#ifdef _WIN32
  __declspec(align(64)) char
      buffer[ACL_PRINTF_BUFFER_TOTAL_SIZE]; // Aligned to 64, for dma transfers
#else
  char buffer[ACL_PRINTF_BUFFER_TOTAL_SIZE]
      __attribute__((aligned(64))); // Aligned to 64, for dma transfers
#endif
  cl_bool device_supports_any_svm = acl_svm_device_supports_any_svm(
      event->command_queue->device->def.physical_device_id);
  cl_bool device_supports_physical_memory =
      acl_svm_device_supports_physical_memory(
          event->command_queue->device->def.physical_device_id);

  const auto &printf_infos = kernel->accel_def->printf_format_info;

  if (!verify_types()) {
    printf("Host data types are incompatible with ACL compiler, ignoring "
           "printfs...\n");
    return dumped_buffer_size;
  }

  if (printf_infos.empty())
    return dumped_buffer_size;

  // Memory is on the device if all of these are true:
  //   The memory is not SVM or the device does not support SVM.
  //   The device support physical memory
  //   The memory is not host accessible
  // Note that a very similar check is made in l_get_devices_affected_for_op,
  // l_mem_transfer_buffer_explicitly and l_dump_printf_buffer
  if ((!kernel->printf_device_buffer->is_svm || !device_supports_any_svm) &&
      (device_supports_physical_memory)) {
    src_on_host = kernel->printf_device_buffer->block_allocation->region
                      ->is_host_accessible;
  }

  // In shared memory system, device and host memory may be the same.
  // So pick the right copy function here.
  if (src_on_host) {
    hal_dma_fn = acl_get_hal()->copy_hostmem_to_hostmem;
  } else {
    hal_dma_fn = acl_get_hal()->copy_globalmem_to_hostmem;
  }

  // It needs the context from ACL_HAL_DEBUG instead of ACL_DEBUG
  if (acl_get_hal()->get_debug_verbosity &&
      acl_get_hal()->get_debug_verbosity() > 0) {
    printf("Previously processed buffer size is %zu \n",
           kernel->processed_printf_buffer_size);
  }

  // Check if we have already processed all the printf buffer
  if (size > (unsigned int)kernel->processed_printf_buffer_size) {
    void *unprocessed_begin = (void *)((char *)kernel->printf_device_buffer
                                           ->block_allocation->range.begin +
                                       kernel->processed_printf_buffer_size);
    assert(size >= kernel->processed_printf_buffer_size);
    dumped_buffer_size = size - kernel->processed_printf_buffer_size;
    hal_dma_fn(NULL, unprocessed_begin, buffer, dumped_buffer_size);
  } else {
    if (acl_get_hal()->get_debug_verbosity &&
        acl_get_hal()->get_debug_verbosity() > 0) {
      printf("All Printf() buffer has already been dumped \n");
    }
    return dumped_buffer_size;
  }

#ifdef DEBUG
  if (debug_mode > 0) {
    printf("acl_dump_printf_buffer at %p size=%d\n", buffer, size);
    printf("num_prints=%u\n", printf_infos.size());
  }
#endif

#ifdef DEBUG
  // dump the buffer contents
  {
    unsigned i;
    for (i = 0; i < size; i += 4) {
      unsigned format_string_index = *((unsigned *)&buffer[i]);
      acl_print_debug_msg("%u: %x\n", i, format_string_index);
    }
  }
#endif
  // always 32-byte aligned address (this may change if printf chunks can be
  // of different sizes )
  // process all the printfs as long as there is data
  for (global_offset = 0, single_printf_offset = 0;
       global_offset < dumped_buffer_size;
       global_offset += single_printf_offset) {

    // the first 4-bytes is the index of the format string
    unsigned local_offset;
    int success;
    unsigned size_of_data = 0;
    // read the index of the format string from memory (i.e. first 4-bytes of
    // the printf data)
    unsigned format_string_index = *((unsigned *)&buffer[global_offset]);

#ifdef DEBUG
    if (debug_mode) {
      printf("----- new printf data chunk -----\n");
      printf("global_offset=%u\n", global_offset);
      printf("format_string_index=%u\n", format_string_index);
    }
#endif

    // get the format string using the index
    std::string format_string;
    success =
        get_format_string(format_string_index, format_string, printf_infos);
    // silently exit
    if (!success) {
      acl_print_debug_msg(
          "corrupt printf data, ignoring remaining printfs...\n");
      return dumped_buffer_size;
    }

#ifdef DEBUG
    acl_print_debug_msg("format_string=%s\n", format_string);
#endif

    // the address of the data in the buffer of the printf
    local_offset = global_offset + 4;

    // process the output of a single printf inside this loop
    // loop as long as we dont reach the end of the format string

    for (auto ptr = format_string.cbegin(); ptr != format_string.cend();) {

      int vector_size;
      int i;

      // a single data conversion (e.g. %d, %f, etc.)
      Printable_Data_Elem data_elem;
      Length_Specifier length;

      // this is format string without conversion, i.e. just a string to print
      // out
      std::string non_conversion_string;

      // starting from the current location in the format string (i.e. ptr)
      // read from format string, extract string to be printed out as well as
      // the type of the data to be converted.
      // (e.g. for "globalid=%d" non_conversion_string is "globalid=" and
      // data_elem is integer)
      ptr = get_data_elem_at_offset(ptr, format_string.cend(), data_elem,
                                    non_conversion_string);

      //
      // Print non_conversion_string first
      //

      // handle special characters
      // before printing
      decode_string(non_conversion_string);

#ifdef DEBUG
      printf("non conversion string=%s\n", non_conversion_string.c_str());
#else
      printf("%s", non_conversion_string.c_str());
#endif
#ifdef DEBUG
      printf("\n");
#endif

      // no data to be printed, end of printing for this printf
      if (ptr == format_string.cend() && data_elem._conversion_string.empty())
        break;

      // Handle vector types by replicating the conversion string for each
      // vector element
      vector_size =
          modify_conversion_string_for_vectors(data_elem._conversion_string);

      if (vector_size == -1) {
        acl_print_debug_msg("wrong vector specifier in printf call, ignoring "
                            "remaining printfs...\n");
        return dumped_buffer_size;
      }

      // get the length specifier
      length = get_length_specifier(data_elem._conversion_string);

#ifdef DEBUG
      printf("length specifier=%s\n", length == LS_HL   ? "LS_HL"
                                      : length == LS_HH ? "LS_HH"
                                      : length == LS_H  ? "LS_H"
                                      : length == LS_L  ? "LS_L"
                                                        : "NONE");
#endif

      // the size of the data we are converting right now
      size_of_data =
          get_llvm_data_size(data_elem._type, length, (vector_size > 1));

      if (size_of_data == 0) {
        acl_print_debug_msg("wrong length modifier in printf call, ignoring "
                            "remaining printfs...\n");
        return dumped_buffer_size;
      }

      for (i = 0; i < vector_size; i++) {
        //
        // read size_of_data bytes from buffer at an aligned address
        // WARNING: this should match the aligned address calculation in
        // TransformPrintf pass.
        // WARNING: vectors are scalarized before TransformPrintf pass,
        // hence, data is NOT aligned at vector size, but at vector element size
        // e.g. if we are printing float16, data would be aligned at
        // sizeof(float) not sizeof(float16).
        {
          // this is the offset we need to add to read the data at an aligned
          // offset
          unsigned unalignment_size = (local_offset % size_of_data);
          if (unalignment_size != 0) {
            unsigned unalignment_offset = size_of_data - unalignment_size;
            local_offset += unalignment_offset;
          }
        }

        //
        // read and print size_of_data at address local_offset
        //
        print_data_elem(data_elem._type, buffer, local_offset,
                        data_elem._conversion_string, length, printf_infos,
                        (vector_size > 1));

        // printf comma (i.e. ',') between vector elements
        if (i != (vector_size - 1))
          printf(",");

        local_offset += size_of_data;
      }
    }

    // how much data did we process for this printf?
    single_printf_offset = local_offset - global_offset;

    // make it power-of-2, at least SMALLEST_GLOBAL_MEMORY_BANDWIDTH
    // WARNING: this should match the sizes printf data chunks in
    // TransformPrintf pass
    if (single_printf_offset < SMALLEST_GLOBAL_MEMORY_BANDWIDTH)
      single_printf_offset = SMALLEST_GLOBAL_MEMORY_BANDWIDTH;
    else {
      unsigned power_of_2_offset = SMALLEST_GLOBAL_MEMORY_BANDWIDTH;
      while (single_printf_offset > power_of_2_offset)
        power_of_2_offset *= 2;
      single_printf_offset = power_of_2_offset;
    }
  }

#ifdef DEBUG
  printf("exiting acl_dump_buffer...\n");
#endif
  return dumped_buffer_size;
}

//
// Schedule enqueue read buffer to read printf buffer
// The activation ID is the device op ID.
void acl_schedule_printf_buffer_pickup(int activation_id, int size,
                                       int debug_dump_printf) {
  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);

  // This function can potentially be called by a HAL that does not use the
  // ACL global lock, so we need to use acl_lock() instead of
  // acl_assert_locked(). However, the MMD HAL calls this function from a unix
  // signal handler, which can't lock mutexes, so we don't lock in that case.
  // All functions called from this one therefore have to use
  // acl_assert_locked_or_sig() instead of just acl_assert_locked().
  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }

#ifdef DEBUG
  printf("printf pickup %d %d\n", activation_id, size);
  fflush(stdout);
#endif
  if (activation_id >= 0 && activation_id < doq->max_ops) {
    // This address is stable, given a fixed activation_id.
    // So we don't run into race conditions.
    acl_device_op_t *op = doq->op + activation_id;
    op->info.num_printf_bytes_pending = (cl_uint)size;

    // Propagate the operation info
    op->info.debug_dump_printf = debug_dump_printf ? 1 : 0;
  }
  // Signal all waiters.
  acl_signal_device_update();
}

void acl_process_printf_buffer(void *user_data, acl_device_op_t *op) {
  user_data = user_data; // Only used by the test mock

  if (op) {
    cl_event event = op->info.event;
    cl_kernel kernel = event->cmd.info.ndrange_kernel.kernel;

    // Grab the printf data and emit it.
    cl_uint num_bytes = op->info.num_printf_bytes_pending;
    size_t dumped_buffer_size = l_dump_printf_buffer(event, kernel, num_bytes);

    if (op->info.debug_dump_printf) {
      // Update the already processed buffer size
      kernel->processed_printf_buffer_size += dumped_buffer_size;
    } else {
      // Full dump, reset this variable
      kernel->processed_printf_buffer_size = 0;
    }

    // Mark this printf work as done.  Must do this *before* unstalling
    // the kernel, to avoid a race against the kernel filling up the
    // buffer again.
    op->info.num_printf_bytes_pending = 0;

    // Ensure kernel IRQ doesn't race with us to update the
    // num_printf_bytes_pending.
    acl_memory_barrier();

    // Allow the kernel to continue running.
    // We don't need to unstall the kernel during the early flushing during
    // debug.
    if (!op->info.debug_dump_printf) {
      acl_get_hal()->unstall_kernel(
          event->cmd.info.ndrange_kernel.device->def.physical_device_id,
          op->id);
    }
  }
}

static void decode_string(std::string &print_data) {
  auto src = print_data;
  auto len = src.length();

  auto src_i = 0U;
  std::stringstream dst;
  while (src_i < len) {
    // check for special chars
    if ((src_i + 1) < len && src[src_i] == '\\' && src[src_i + 1] == '\\') {
      // unwrap slash char
      dst << '\\';
      src_i += 2;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '2' && src[src_i + 2] == '0') {
      // replace with space
      dst << ' ';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == '9') {
      // replace with horizontal tab
      dst << '\t';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == 'b') {
      // replace with vertical tab
      dst << '\v';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == 'd') {
      // replace with carriage return
      dst << '\r';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == 'a') {
      // replace new line char
      dst << '\n';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == 'c') {
      // replace with feed
      dst << '\f';
      src_i += 3;
    } else if ((src_i + 2) < len && src[src_i] == '\\' &&
               src[src_i + 1] == '0' && src[src_i + 2] == '0') {
      // replace with null char
      dst << '\0';
      src_i += 3;
    } else {
      // if this is a normal char
      dst << src[src_i++];
    }
  }

  print_data = dst.str();
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

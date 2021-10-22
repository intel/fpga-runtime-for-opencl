// clang-format off

// The function `check_copy_overlap` was copied verbatim from the Khronos
// OpenCL Specification Version 2.1 Appendix D - CL_MEM_COPY_OVERLAP.
// https://www.khronos.org/registry/OpenCL/specs/opencl-2.1.pdf

/*
 * Copyright (c) 2011 The Khronos Group Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and /or associated documentation files (the "Materials "), to deal in the Materials
 * without restriction, including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Materials, and to permit persons to
 * whom the Materials are furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Materials.
 * 
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER DEALINGS IN
 * THE MATERIALS.
 */

#include <check_copy_overlap.h>

unsigned int
check_copy_overlap( const size_t src_origin[],
                    const size_t dst_origin[],
                    const size_t region[],
                    const size_t row_pitch,
                    const size_t slice_pitch )
{
      const size_t slice_size = (region[1] - 1) * row_pitch   + region[0];
      const size_t block_size = (region[2] - 1) * slice_pitch + slice_size;

      const size_t src_start  = src_origin[2] * slice_pitch +
                                src_origin[1] * row_pitch +
                                src_origin[0];
      const size_t src_end    = src_start + block_size;

      const size_t dst_start  = dst_origin[2] * slice_pitch +
                                dst_origin[1] * row_pitch +
                                dst_origin[0];
      const size_t dst_end    = dst_start + block_size;

      /* No overlap if dst ends before src starts or if src ends
       * before dst starts.
       */
      if( (dst_end <= src_start) || (src_end <= dst_start) )
      {
            return 0;
      }

      /* No overlap if region[0] for dst or src fits in the gap
       * between region[0] and row_pitch.
       */
      {
            const size_t src_dx = src_origin[0] % row_pitch;
            const size_t dst_dx = dst_origin[0] % row_pitch;

            if( ((dst_dx >= src_dx + region[0]) &&
                 (dst_dx + region[0] <= src_dx + row_pitch)) ||
                ((src_dx >= dst_dx + region[0]) &&
                 (src_dx + region[0] <= dst_dx + row_pitch)) )
            {
                  return 0;
            }
      }

      /* No overlap if region[1] for dst or src fits in the gap
       * between region[1] and slice_pitch.
       */
      {
            const size_t src_dy =
                  (src_origin[1] * row_pitch + src_origin[0]) % slice_pitch;
            const size_t dst_dy =
                   (dst_origin[1] * row_pitch + dst_origin[0]) % slice_pitch;

            if( ((dst_dy >= src_dy + slice_size) &&
                 (dst_dy + slice_size <= src_dy + slice_pitch)) ||
                ((src_dy >= dst_dy + slice_size) &&
                 (src_dy + slice_size <= dst_dy + slice_pitch)) )
            {
                  return 0;
            }
      }

      /* Otherwise src and dst overlap. */
      return 1;
}

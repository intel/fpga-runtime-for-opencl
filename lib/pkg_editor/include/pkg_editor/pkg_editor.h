// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/* Editor for Altera OpenCL package files
 *
 * This provides higher-level functions for ELF work.
 * The idea is to put content into sections, one "piece" of content
 * per section, and use section names to identify the content.
 * The interface enforces unique section names (not true for generic ELFs)
 * and hides all the ugly ELF interface calls and structures.
 */

#ifndef PKG_FILE_EDITOR_H
#define PKG_FILE_EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STRING_LENGTH 100000

/* Modes for open_struct acl_pkg_file() call.
 * Exactly one of ACL_PKG_READ, ACL_PKG_READ_WRITE must be supplied.
 * Other flags may be bitwise OR'd into the mode.
 *
 * You can combine other modes with ACL_PKG_SHOW_* to control messages.
 */
#define ACL_PKG_READ (1 << 0) /* Only reading the package */
#define ACL_PKG_READ_WRITE                                                     \
  (1 << 1) /* Expect to read and write the binary. File must already exist. */
#define ACL_PKG_CREATE                                                         \
  (1 << 2) /* Also creating.  Can only be used with ACL_PKG_READ_WRITE */

#define ACL_PKG_SHOW_ERROR (1 << 8) /*print errors to stderr*/
#define ACL_PKG_SHOW_INFO (1 << 9)  /*print info messages to stdout*/

#define ACL_PKG_SECTION_ACL_VERSION ".acl.version"
#define ACL_PKG_SECTION_ACL_BUILD ".acl.build"
#define ACL_PKG_SECTION_QVERSION ".acl.qversion"
#define ACL_PKG_SECTION_HASH ".acl.hash"
#define ACL_PKG_SECTION_BOARD ".acl.board"
#define ACL_PKG_SECTION_COMPILEOPTIONS ".acl.compileoptions"
#define ACL_PKG_SECTION_SOURCE ".acl.source"
#define ACL_PKG_SECTION_LLVMIR ".acl.llvmir"
#define ACL_PKG_SECTION_VERILOG ".acl.verilog"
#define ACL_PKG_SECTION_PROFILE_BASE ".acl.profile_base"
#define ACL_PKG_SECTION_AUTODISCOVERY ".acl.autodiscovery"
#define ACL_PKG_SECTION_RBF ".acl.rbf"
#define ACL_PKG_SECTION_CORE_RBF ".acl.core.rbf"
#define ACL_PKG_SECTION_PERIPH_RBF ".acl.periph.rbf"
#define ACL_PKG_SECTION_BASE_RBF ".acl.base_revision.rbf"
#define ACL_PKG_SECTION_SOF ".acl.sof"
#define ACL_PKG_SECTION_VFABRIC ".acl.vfabric"
#define ACL_PKG_SECTION_PLL_CONFIG ".acl.pll_config"
#define ACL_PKG_SECTION_FPGA_BIN ".acl.fpga.bin"
#define ACL_PKG_SECTION_EMULATOR_OBJ_LINUX ".acl.emulator_object.linux"
#define ACL_PKG_SECTION_EMULATOR_OBJ_WINDOWS ".acl.emulator_object.windows"
#define ACL_PKG_SECTION_AUTODISCOVERY_XML ".acl.autodiscovery.xml"
#define ACL_PKG_SECTION_BOARDSPEC_XML ".acl.board_spec.xml"
#define ACL_PKG_SECTION_PERIPH_HASH ".acl.periph.hash"
#define ACL_PKG_SECTION_PROFILER_XML ".acl.profiler.xml"
#define ACL_PKG_SECTION_COMPILE_REV ".acl.compile_revision"
#define ACL_PKG_SECTION_PCIE_DEV_ID ".acl.pcie.dev_id"
#define ACL_PKG_SECTION_BASE_PERIPH_HASH ".acl.base_revision.periph.hash"
#define ACL_PKG_SECTION_ADJUST_PLLS_OUTPUT ".acl.quartus_report"
#define ACL_PKG_SECTION_KERNEL_ARG_INFO_XML ".acl.kernel_arg_info.xml"
#define ACL_PKG_SECTION_FAST_COMPILE ".acl.fast_compile"

/* Minimum alignment in memory. */
#define ACL_PKG_MIN_SECTION_ALIGNMENT 128

/* Open and close the pkg file */
struct acl_pkg_file *acl_pkg_open_file(const char *fname, int mode);
/* You can call close on a NULL pointer: it will do nothing.
 * Closing the package file will also free its memory, so you better lose
 * the pointer reference.
 */
int acl_pkg_close_file(struct acl_pkg_file *pkg);

/* Set message output mode: show_mode is some combination of the bits
 * in ACL_PKG_SHOW_INFO and ACL_PKG_SHOW_ERROR
 */
void acl_pkg_set_show_mode(struct acl_pkg_file *pkg, int show_mode);

/* Open memory image of pkg file. Only good for reading!
 * The show_mode argument is an OR combination of zero or more of
 *    ACL_PKG_SHOW_INFO,
 *    ACL_PKG_SHOW_ERROR.
 */
struct acl_pkg_file *acl_pkg_open_file_from_memory(char *pkg_image,
                                                   size_t pkg_image_size,
                                                   int show_mode);

/* Does the given named section exist?
 * Returns 1 for yes, 0 for no.
 * If the section exists, and size_ret is not-NULL, then the size (in bytes) of
 * the section is stored into *size_ret. The size does NOT include NULL
 * terminator, just like strlen().
 */
int acl_pkg_section_exists(const struct acl_pkg_file *pkg,
                           const char *sect_name, size_t *size_ret);

/* Return list of ALL (useful) section names in the package.
 * The buffer must be pre-allocated by the caller upto max_len bytes.
 * Each section name is separated by '\n'
 * Returns 1 on success, 0 on failure.
 */
int acl_pkg_section_names(const struct acl_pkg_file *pkg, char *buf,
                          size_t max_len);

/* Add a new section with specified content.
 * If a section with such name already exists, nothing is done.
 * Returns 0 on failure, non-zero on success.
 */
int acl_pkg_add_data_section(struct acl_pkg_file *pkg, const char *sect_name,
                             const void *content, size_t len);
int acl_pkg_add_data_section_from_file(struct acl_pkg_file *pkg,
                                       const char *sect_name,
                                       const char *in_file);

/* Read content of an existing section.
 * For read_section(), the buffer must be pre-allocated by caller to hold at
 * least len bytes. This function will add '\0' at the end, therefore, the 'len'
 * argument passed to this function must be one larger than the value returned
 * by acl_pkg_section_exists. Returns 0 on failure, non-zero on success.
 */
int acl_pkg_read_section(const struct acl_pkg_file *pkg, const char *sect_name,
                         char *buf, size_t len);
int acl_pkg_read_section_into_file(struct acl_pkg_file *pkg,
                                   const char *sect_name, const char *out_file);

/* Get a transient pointer to a section's data, via buf_ptr.
 * The pointer is transient: It might move if you update the package in any way.
 * This is a "fast" path in comparison to acl_pkg_read_section, so you
 * don't have to allocate space to copy into.
 * Returns 0 on failure, non-zero on success.
 */
int acl_pkg_read_section_transient(const struct acl_pkg_file *pkg,
                                   const char *sect_name, char **buf_ptr);

/* Update content of an existing section.
 * Old content is discarded. The section must already exist.
 * Returns 0 on failure, non-zero on success.
 */
int acl_pkg_update_section(struct acl_pkg_file *pkg, const char *sect_name,
                           const void *new_content, size_t new_len);
int acl_pkg_update_section_from_file(struct acl_pkg_file *pkg,
                                     const char *sect_name,
                                     const char *in_file);

/* List all pkg sections to stdout.
 * Returns 0 on failure, non-zero on success.
 */
int acl_pkg_list_file_sections(struct acl_pkg_file *pkg);

/* Read full content of file into a buffer.
 * The buffer is allocated by this function but must be freed by the caller.
 * File length is returned in the second argument */
void *acl_pkg_read_file_into_buffer(const char *in_file, size_t *file_size_out);

/* support for package/unpackage */

/* Package the input files and directory trees (NULL terminated list in
 * input_files_dirs) and put them into the output file (out_file). Returns 0 on
 * failure, non-zero on success
 */
int acl_pkg_pack(const char *out_file, const char **input_files_dirs);

/* Unpack the input file (or stdin if filename is ACL_PKG_UNPACKAGE_STDIN)
 * created by acl_pkg_pack into directory out_dir.
 * Returns 0 on failure, non-zero on success
 */
#define ACL_PKG_UNPACKAGE_STDIN "-"
int acl_pkg_unpack(const char *in_file, const char *out_dir);

/* Unpack the buffer created by acl_pkg_pack into directory out_dir.
 * Returns 0 on failure, non-zero on success
 */
int acl_pkg_unpack_buffer(const char *buffer, size_t buffer_size,
                          const char *out_dir);

#ifdef __cplusplus
}
#endif

#endif /* PKG_FILE_EDITOR_H */

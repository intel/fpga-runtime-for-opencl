// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4266)
#endif
#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/SimpleString.h"
#include "CppUTest/TestHarness.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Replace this library with <filesystem> once we move to GCC 8 or newer
// versions
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING 1
#include <experimental/filesystem>

#include <climits>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "pkg_editor/pkg_editor.h"
#include <assert.h>
#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#else // Linux
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <fstream>

#ifdef _WIN32
#define NUL "NUL"
#else
#define NUL "/dev/null"
#endif

#define SAMPLE_FILE ".sample_file.elf"
#define PACK_UNPACK_FILE ".pack_unpack"
#define PACK_UNPACK_DIR ".pack_unpack_dir"

using random_bytes_engine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT,
                                 unsigned int>;
namespace fs = std::experimental::filesystem::v1;

int tmpCount = 0;

static void l_remove_file(const char *filename) {
#ifdef _WIN32
  _unlink(filename);
#else
  unlink(filename);
#endif
}

static void l_remove_sample_file(void) { l_remove_file(SAMPLE_FILE); }

static void l_remove_pack_unpack_file(void) { l_remove_file(PACK_UNPACK_FILE); }

int main(int argc, const char **argv) {
  l_remove_sample_file();
  return CommandLineTestRunner::RunAllTests(argc, argv);
}

#ifdef _WIN32
#define snprintf sprintf_s
#endif

SimpleString StringFrom(size_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%zd",
           x); // format string might be platform dependent..?
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}

TEST_GROUP(create){};

TEST(create, bad_flags) {
  char temp[] = "\177ELF";
  CHECK_EQUAL(
      0, acl_pkg_open_file(0, ACL_PKG_CREATE |
                                  ACL_PKG_READ_WRITE)); // require a filename
  CHECK_EQUAL(0, acl_pkg_open_file("blah", 0)); // need one of read, read_write
  CHECK_EQUAL(
      0, acl_pkg_open_file("blah", ACL_PKG_READ |
                                       ACL_PKG_READ_WRITE)); // can't have both
  CHECK_EQUAL(
      0, acl_pkg_open_file(
             "blah", ACL_PKG_READ | ACL_PKG_CREATE)); // CREATE only compatible
                                                      // with ACL_PKG_READ_WRITE

  // Check opening from memory.
  CHECK_EQUAL(0, acl_pkg_open_file_from_memory(0, 12, 0)); // require a pointer
  CHECK_EQUAL(0, acl_pkg_open_file_from_memory(temp, 0, 0)); // require a size

  // Invalid bits.
  int bad_flags = 1 | ((ACL_PKG_SHOW_INFO | ACL_PKG_SHOW_ERROR) << 1);
  CHECK_EQUAL(0, acl_pkg_open_file_from_memory(temp, sizeof(temp), bad_flags));
}

TEST_GROUP(sample_file){
  public : void setup(){} void teardown(){l_remove_sample_file();
}
struct acl_pkg_file *create_file() {
  struct acl_pkg_file *pkg = 0;
  pkg = acl_pkg_open_file(SAMPLE_FILE, ACL_PKG_CREATE | ACL_PKG_READ_WRITE);
  CHECK(pkg);

  const static char name[] = "13.0.0";
  CHECK(acl_pkg_add_data_section(pkg, ACL_PKG_SECTION_ACL_VERSION, name,
                                 sizeof(name)));
  return pkg;
}
struct acl_pkg_file *read_file() {
  struct acl_pkg_file *pkg = 0;
  pkg = acl_pkg_open_file(SAMPLE_FILE, ACL_PKG_READ);
  CHECK(pkg);
  return pkg;
}
struct acl_pkg_file *read_write_file() {
  struct acl_pkg_file *pkg = 0;
  pkg = acl_pkg_open_file(SAMPLE_FILE, ACL_PKG_READ_WRITE);
  CHECK(pkg);
  return pkg;
}
void close_file(struct acl_pkg_file *pkg) {
  if (pkg) {
    acl_pkg_close_file(pkg);
  }
}
}
;

TEST(sample_file, create_close) {
  printf("begin create_close\n");
  struct acl_pkg_file *pkg = create_file();
  CHECK(pkg);
  close_file(pkg);
  printf("end    create_close\n");
}

TEST(sample_file, write_ops_on_readonly) {
  // Check that we can't write to a read-only file.

  printf("begin writeops\n");

  int data = 42;
  close_file(create_file());
  struct acl_pkg_file *pkg = read_file();
  CHECK(pkg);
  CHECK_EQUAL(0, acl_pkg_add_data_section(pkg, ACL_PKG_SECTION_HASH, &data,
                                          sizeof(data)));
  CHECK_EQUAL(0, acl_pkg_add_data_section_from_file(pkg, ACL_PKG_SECTION_HASH,
                                                    SAMPLE_FILE));

  CHECK_EQUAL(0, acl_pkg_update_section(pkg, ACL_PKG_SECTION_HASH, &data,
                                        sizeof(data)));
  CHECK_EQUAL(0, acl_pkg_update_section_from_file(pkg, ACL_PKG_SECTION_HASH,
                                                  SAMPLE_FILE));
  close_file(pkg);
  printf("end   writeops\n");
}

TEST(sample_file, write_ops_on_writable) {
  // Check that we can write to a read-only file.
  printf("begin writeops_write\n");

  size_t data = 42;
  size_t data2 = 84;
  close_file(create_file());
  struct acl_pkg_file *pkg = read_write_file();

  CHECK(pkg);
  CHECK(
      acl_pkg_add_data_section(pkg, ACL_PKG_SECTION_HASH, &data, sizeof(data)));
  // Check that we get the right data back.
  size_t data_size;
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(sizeof(data), data_size);
  char data_result[sizeof(data) + 1] = {0};
  CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, data_result,
                             sizeof(data_result)));
  CHECK_EQUAL(0, memcmp(&data, data_result, sizeof(data)));
  char *data_result_ptr = 0;
  CHECK(acl_pkg_read_section_transient(pkg, ACL_PKG_SECTION_HASH,
                                       &data_result_ptr));
  CHECK_EQUAL(data, (size_t)(*(unsigned int *)data_result_ptr));

  // Test updating.
  CHECK(
      acl_pkg_update_section(pkg, ACL_PKG_SECTION_HASH, &data2, sizeof(data2)));
  // Check that we get the right data back.
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(sizeof(data), data_size);
  char data2_result[sizeof(data) + 1] = {0};
  CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, data2_result,
                             sizeof(data2_result)));
  CHECK_EQUAL(0, memcmp(&data2, data2_result, sizeof(data2)));
  data_result_ptr = 0;
  CHECK(acl_pkg_read_section_transient(pkg, ACL_PKG_SECTION_HASH,
                                       &data_result_ptr));
  CHECK_EQUAL(data2, (size_t)(*(unsigned int *)data_result_ptr));

  // make sure we can list section names
  char buf[2048];
  CHECK(acl_pkg_section_names(pkg, buf, 2048));
  printf("Section names: |%s|\n", buf);
  // check result -- it must contain these two section names
  CHECK(strstr(buf, ACL_PKG_SECTION_HASH));
  CHECK(strstr(buf, ACL_PKG_SECTION_ACL_VERSION));

  close_file(pkg);
  printf("end  writeops_write\n");
}

TEST(sample_file, read_readonly) {
  printf("begin read_readonly\n");

  // First write file.
  struct acl_pkg_file *pkg = create_file();
  char hw[] = "hello world!";
  int system_result = 0;

  CHECK(pkg);
  CHECK(acl_pkg_add_data_section(pkg, ACL_PKG_SECTION_HASH, hw, sizeof(hw)));
  close_file(pkg);

  system_result = system("chmod a-w " SAMPLE_FILE);
  assert(system_result != -1);
  system_result = system("ls -l " SAMPLE_FILE);
  assert(system_result != -1);

  // Now open it readonly.
  // We should be able to do so and read something out.
  pkg = read_file();
  size_t data_size;
  CHECK(pkg);
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(sizeof(hw), data_size);
  char data_result[sizeof(hw) + 1] = {0};
  CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, data_result,
                             sizeof(data_result)));
  CHECK_EQUAL(0, strcmp(data_result, hw));
  char *data_result_ptr = 0;
  CHECK(acl_pkg_read_section_transient(pkg, ACL_PKG_SECTION_HASH,
                                       &data_result_ptr));
  CHECK_EQUAL(0, strcmp(data_result_ptr, hw));

  close_file(pkg);

  system_result = system("chmod a+w " SAMPLE_FILE);
  assert(system_result != -1);
  system_result = system("ls -l " SAMPLE_FILE);
  assert(system_result != -1);

  printf("end   read_readonly\n");
}

TEST(sample_file, add_empty_section) {
  // Adding an empty data section from a file
  printf("Add empty data section from file...");

  // Create an empty file
  const char *file0 = ".content0";
  FILE *fp0 = fopen(file0, "w");
  CHECK(fp0);
  fclose(fp0);

  // Write initial contents
  struct acl_pkg_file *pkg = create_file();
  CHECK(pkg);
  CHECK(acl_pkg_add_data_section_from_file(pkg, ACL_PKG_SECTION_HASH, file0));
  close_file(pkg);

  // Make sure we read an empty section
  pkg = read_file();
  size_t data_size;
  CHECK(pkg);
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(0, data_size);
  close_file(pkg);

  l_remove_file(file0);
  printf("OK\n");
}

TEST(sample_file, update_same_size) {
  // Updating a section from a file, where the file size is the same.
  const char *file0 = ".content0";
  const char *file1 = ".content1";

  FILE *fp0 = fopen(file0, "w");
  FILE *fp1 = fopen(file1, "w");

  printf("Update from file with same size...");

  CHECK(fp0);
  CHECK(fp1);

  // Just write the names to the files.
  fprintf(fp0, "%s", file0); // no newlines, so we don't confuse windows CR/LF
  fprintf(fp1, "%s", file1);
  fclose(fp0);
  fclose(fp1);

  // Write initial contents
  struct acl_pkg_file *pkg = create_file();
  CHECK(pkg);
  CHECK(acl_pkg_add_data_section_from_file(pkg, ACL_PKG_SECTION_HASH, file0));
  close_file(pkg);

  char buf[1000];

  // Make sure we got back what we wrote.
  pkg = read_file();
  size_t data_size;
  CHECK(pkg);
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(strlen(file0), data_size);
  CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, buf, data_size + 1));
  CHECK_EQUAL(0, strncmp(buf, file0, strlen(file0)));
  close_file(pkg);

  // Now use the update-from-file API.
  // It calls the update-from-memory API underneath, so this tests both
  // paths.
  pkg = read_write_file();
  CHECK(pkg);
  CHECK(acl_pkg_update_section_from_file(pkg, ACL_PKG_SECTION_HASH, file1));
  close_file(pkg);

  // Make sure we got back what we wrote.
  memset(buf, 0, sizeof(buf));
  pkg = read_file();
  CHECK(pkg);
  CHECK(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_HASH, &data_size));
  CHECK_EQUAL(strlen(file1), data_size);
  CHECK(acl_pkg_read_section(pkg, ACL_PKG_SECTION_HASH, buf, data_size + 1));
  // printf(" expected '%s' got '%s'\n", file1, buf );
  CHECK_EQUAL(0, strncmp(buf, file1, strlen(file0)));
  close_file(pkg);

  l_remove_file(file0);
  l_remove_file(file1);
  printf("OK\n");
}

TEST_GROUP(package){public :
                        void setup(){} void teardown(){int system_result = 0;
l_remove_pack_unpack_file();
system_result = system("rm -rf " PACK_UNPACK_DIR);
assert(system_result != -1);
}
}
;

#if USE_ZLIB
TEST(package, pack) {
  const char *test_input[] = {"include", "src", "test", NULL};

  // Try with an invalid filename.
  const char *test_input2[] = {"file-not-here", NULL};
  int result = acl_pkg_pack(PACK_UNPACK_FILE, test_input2);
  CHECK_EQUAL(0, result);

  // Try with a non file/dir filename (file check seems weak on Windows)
#ifndef _WIN32
  const char *test_input3[] = {"/dev/null", NULL};
  result = acl_pkg_pack(PACK_UNPACK_FILE, test_input3);
  CHECK_EQUAL(0, result);
#endif

  // File that we can't write (doesn't exist).
  result = acl_pkg_pack("/NOT/FOUND", test_input);
  CHECK_EQUAL(0, result);

  // File that we can't write (not writable).
  result = acl_pkg_pack(".", test_input);
  CHECK_EQUAL(0, result);

  // Try some known good names.
  result = acl_pkg_pack(PACK_UNPACK_FILE, test_input);
  CHECK_EQUAL(1, result);
}

static bool files_same(const fs::path f1, const fs::path f2) {
  std::ifstream file1(f1, std::ifstream::ate | std::ifstream::binary);
  std::ifstream file2(f2, std::ifstream::ate | std::ifstream::binary);
  file1.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  file2.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  const std::ifstream::pos_type fileSize = file1.tellg();

  if (fileSize != file2.tellg()) {
    return false;
  }
  file1.seekg(0);
  file2.seekg(0);

  std::istreambuf_iterator<char> begin1(file1);
  std::istreambuf_iterator<char> begin2(file2);

  return std::equal(begin1, std::istreambuf_iterator<char>(), begin2);
}

static void generate_random_file(const fs::path &name, size_t size) {
  random_bytes_engine rbe;
  std::ofstream file(name);
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  std::generate_n(std::ostreambuf_iterator<char>(file), size,
                  [&] { return (char)rbe(); });
}

// Creates a folder consisting of a certain number of files with random contents
// and random sizes
static std::vector<fs::path> generate_tmp_folder(const fs::path &tmpdir) {
  std::vector<fs::path> files;

  std::mt19937 gen{};
  std::uniform_int_distribution dis{0, 100000};

  int system_result = 0;
  std::string remove_command = "rm -rf " + tmpdir.string();
  const char *command_c_str = remove_command.c_str();
  system_result = system(command_c_str);
  assert(system_result != -1);
  fs::create_directory(tmpdir);

  // Guarantee we always have one large and one empty file
  generate_random_file(files.emplace_back(tmpdir / "empty_file"), 0);
  generate_random_file(files.emplace_back(tmpdir / "large_file"), 10000000);

  const int num_random_files = 8;
  for (int i = 0; i < num_random_files; i++) {
    std::string filename = "file" + std::to_string(i);
    generate_random_file(files.emplace_back(tmpdir / filename), dis(gen));
  }

  return files;
}

static bool is_same_tmpdir(const std::vector<fs::path> &files,
                           const fs::path &unpack_dir) {
  return std::all_of(files.begin(), files.end(), [&](const fs::path &path) {
    fs::path unpacked_file_path = unpack_dir / path;
    return files_same(path, unpacked_file_path);
  });
}

TEST(package, unpack) {
  int result;
  std::string tmpdir_string = "tmp" + std::to_string(tmpCount);
  tmpCount++;
  fs::path tmpdir = tmpdir_string;
  const char *tmpdir_c_str = tmpdir_string.c_str();
  std::vector<fs::path> files = generate_tmp_folder(tmpdir);

  // Create a known good input.
  const char *test_input[] = {tmpdir_c_str, NULL};
  result = acl_pkg_pack(PACK_UNPACK_FILE, test_input);
  CHECK_EQUAL(1, result);

  // Now read it back.
  result = acl_pkg_unpack(PACK_UNPACK_FILE, PACK_UNPACK_DIR);
  CHECK_EQUAL(1, result);

  // Compare some files to be sure that they are the same.
  CHECK(is_same_tmpdir(files, PACK_UNPACK_DIR));
  system(("rm -rf " + tmpdir_string).c_str());
}

TEST(package, unpack_buffer) {
  int result;
  std::string tmpdir_string = "tmp" + std::to_string(tmpCount);
  tmpCount++;
  fs::path tmpdir = tmpdir_string;
  const char *tmpdir_c_str = tmpdir_string.c_str();
  std::vector<fs::path> files = generate_tmp_folder(tmpdir);
  // Create a known good input.
  const char *test_input[] = {"include", "src", "test", tmpdir_c_str, NULL};
  result = acl_pkg_pack(PACK_UNPACK_FILE, test_input);
  CHECK_EQUAL(1, result);

  // Read it into memory.
  std::ifstream file(PACK_UNPACK_FILE,
                     std::ifstream::ate | std::ifstream::binary);
  const std::ifstream::pos_type fileSize = file.tellg();
  file.seekg(0);
  char *buffer = new char[fileSize];
  file.read(buffer, fileSize);
  file.close();

  // Now read it back.
  result = acl_pkg_unpack_buffer(buffer, fileSize, PACK_UNPACK_DIR);
  delete[] buffer;
  CHECK_EQUAL(1, result);

  // Compare some files to be sure that they are the same.
  CHECK_EQUAL(true,
              files_same("include/pkg_editor/pkg_editor.h",
                         PACK_UNPACK_DIR "/include/pkg_editor/pkg_editor.h"));
  CHECK_EQUAL(true, files_same("src/pkg_editor.c",
                               PACK_UNPACK_DIR "/src/pkg_editor.c"));
  CHECK_EQUAL(true, files_same("test/pkg_editor_test.cpp",
                               PACK_UNPACK_DIR "/test/pkg_editor_test.cpp"));
  CHECK(is_same_tmpdir(files, PACK_UNPACK_DIR));
  system(("rm -rf " + tmpdir_string).c_str());
}

TEST(package, unpack_buffer_stdin) {
  int result;
  std::string tmpdir_string = "tmp" + std::to_string(tmpCount);
  tmpCount++;
  fs::path tmpdir = tmpdir_string;
  const char *tmpdir_c_str = tmpdir_string.c_str();
  std::vector<fs::path> files = generate_tmp_folder(tmpdir);

  // Create a known good input.
  const char *test_input[] = {"include", "src", "test", tmpdir_c_str, NULL};
  result = acl_pkg_pack(PACK_UNPACK_FILE, test_input);
  CHECK_EQUAL(1, result);

#ifdef _WIN32
  // Open new input;
  int fd = _open(PACK_UNPACK_FILE, _O_RDONLY | _O_BINARY);
  CHECK(fd > 0);
  // Save stdin.
  int old_stdin = _dup(0);

  // Force stdin to read from the file
  _dup2(fd, 0);
  _close(fd);
  result = acl_pkg_unpack(ACL_PKG_UNPACKAGE_STDIN, PACK_UNPACK_DIR);
  // Put stdin back again.
  _dup2(old_stdin, 0);
  _close(old_stdin);
#else
  // Open new input;
  int fd = open(PACK_UNPACK_FILE, O_RDONLY);
  CHECK(fd > 0);
  // Save stdin.
  int old_stdin = dup(0);

  // Force stdin to read from the file
  dup2(fd, 0);
  close(fd);
  result = acl_pkg_unpack(ACL_PKG_UNPACKAGE_STDIN, PACK_UNPACK_DIR);
  // Put stdin back again.
  dup2(old_stdin, 0);
  close(old_stdin);
#endif
  CHECK_EQUAL(1, result);

  // Compare some files to be sure that they are the same.
  CHECK_EQUAL(true,
              files_same("include/pkg_editor/pkg_editor.h",
                         PACK_UNPACK_DIR "/include/pkg_editor/pkg_editor.h"));
  CHECK_EQUAL(true, files_same("src/pkg_editor.c",
                               PACK_UNPACK_DIR "/src/pkg_editor.c"));
  CHECK_EQUAL(true, files_same("test/pkg_editor_test.cpp",
                               PACK_UNPACK_DIR "/test/pkg_editor_test.cpp"));
  CHECK(is_same_tmpdir(files, PACK_UNPACK_DIR));
  system(("rm -rf " + tmpdir_string).c_str());
}
#endif

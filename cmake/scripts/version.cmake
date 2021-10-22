# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# This script generates a header containing the runtime version based on the
# output of git describe, which generates a human readable name for a commit.

# Use upper case to signal that this is a placeholder value.
set(ACL_GIT_COMMIT "UNKNOWN")

# The extraction of the git commit is strictly optional; if a failure occurs
# due to any reason, the placeholder value for ACL_GIT_COMMIT must be retained.
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --first-parent --long
    RESULT_VARIABLE ACL_GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE ACL_GIT_DESCRIBE_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(ACL_GIT_DESCRIBE_RESULT EQUAL 0 AND ACL_GIT_DESCRIBE_OUTPUT)
    set(ACL_GIT_COMMIT "${ACL_GIT_DESCRIBE_OUTPUT}")
  endif()
endif()

# This only writes the output file when the content has changed, which in turn
# only triggers a rebuild of dependent source files when the commit changes.
configure_file(
  "${ACL_VERSION_INPUT_FILE}"
  "${ACL_VERSION_OUTPUT_FILE}"
  @ONLY
)

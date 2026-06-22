// Copyright (C) 2026 Jamie Cui <jamie.cui@outlook.com>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "test_helpers.h"

TEST(Process, RunCapturesOutputAndFailures) {
  GitOverleafError err = {};
  GitOverleafProcessResult result = {};
  char* ok_argv[] = {const_cast<char*>("sh"), const_cast<char*>("-c"),
                     const_cast<char*>("printf '  hello  \\n'"), nullptr};
  ASSERT_EQ(
      0, git_overleaf_process_run(ok_argv, nullptr, nullptr, 0, &result, &err));
  EXPECT_EQ(0, result.status);
  EXPECT_STREQ("hello", result.output);
  git_overleaf_process_result_free(&result);

  char* fail_argv[] = {const_cast<char*>("sh"), const_cast<char*>("-c"),
                       const_cast<char*>("echo problem; exit 7"), nullptr};
  ASSERT_EQ(0, git_overleaf_process_run(fail_argv, nullptr, nullptr, 1, &result,
                                        &err));
  EXPECT_EQ(7, result.status);
  EXPECT_STREQ("problem", result.output);
  git_overleaf_process_result_free(&result);

  EXPECT_EQ(-1, git_overleaf_process_run(fail_argv, nullptr, nullptr, 0,
                                         &result, &err));
  ExpectContains(err.message, "failed with status 7");
  git_overleaf_process_result_free(&result);
}

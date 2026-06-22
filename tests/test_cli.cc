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

TEST(Cli, HelpAndArgumentErrors) {
  ProcessResultGuard result;

  RunCli({"--help"}, nullptr, &result.value);
  EXPECT_EQ(0, result.value.status);
  ExpectContains(result.value.output, "Usage:");
  ExpectContains(result.value.output, "clone [--project-id ID]");
  git_overleaf_process_result_free(&result.value);

  RunCli({}, nullptr, &result.value);
  EXPECT_EQ(2, result.value.status);
  ExpectContains(result.value.output, "Usage:");
  git_overleaf_process_result_free(&result.value);

  RunCli({"--bogus"}, nullptr, &result.value);
  EXPECT_EQ(2, result.value.status);
  ExpectContains(result.value.output, "unknown global option");
  git_overleaf_process_result_free(&result.value);

  RunCli({"unknown"}, nullptr, &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output, "unknown command");
}

TEST(Cli, AuthWritesCookieAndRejectsConflicts) {
  GitOverleafError err = {};
  TempDir root;
  ASSERT_NE(nullptr, root.path());
  CStr cookie_file(git_overleaf_path_join(root.path(), "cookies"));
  ASSERT_NE(nullptr, cookie_file);

  ProcessResultGuard result;
  RunCli({"auth", "--cookie", "connect.sid=test", "--cookie-file",
          cookie_file.get()},
         nullptr, &result.value);
  EXPECT_EQ(0, result.value.status);
  ExpectContains(result.value.output, "Saved Overleaf cookies");

  char* text_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_read_file(cookie_file.get(), &text_raw, &err));
  CStr text(text_raw);
  EXPECT_STREQ("connect.sid=test\n", text.get());
  struct stat st;
  ASSERT_EQ(0, stat(cookie_file.get(), &st));
  EXPECT_EQ(0600, static_cast<int>(st.st_mode & 0777));
  git_overleaf_process_result_free(&result.value);

  RunCli({"--cookie", "a=b", "auth", "--from-firefox"}, nullptr,
         &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output,
                 "auth accepts either --cookie or --from-firefox");
  git_overleaf_process_result_free(&result.value);

  RunCli({"auth", "--unknown"}, nullptr, &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output, "unknown auth option");
}

TEST(Cli, CloneValidatesBeforeNetwork) {
  TempDir root;
  ASSERT_NE(nullptr, root.path());
  CStr existing(git_overleaf_path_join(root.path(), "my-test-project"));
  ASSERT_NE(nullptr, existing);
  ASSERT_EQ(0, git_overleaf_ensure_directory(existing.get(), nullptr));
  CStr marker(git_overleaf_path_join(existing.get(), "file.txt"));
  ASSERT_NE(nullptr, marker);
  ASSERT_EQ(0, write_text(marker.get(), "already here"));

  ProcessResultGuard result;
  RunCli({"clone"}, root.path(), &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output,
                 "clone requires --project-id ID when not running "
                 "interactively");
  git_overleaf_process_result_free(&result.value);

  RunCli({"clone", "--unknown"}, root.path(), &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output, "unknown clone option");
  git_overleaf_process_result_free(&result.value);

  RunCli({"clone", "--project-id", "p1", "--project-name", "---"},
         root.path(), &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output, "cannot derive clone target");
  git_overleaf_process_result_free(&result.value);

  RunCli({"clone", "--project-id", "p1", "--project-name",
          "My Test Project"},
         root.path(), &result.value);
  EXPECT_EQ(1, result.value.status);
  ExpectContains(result.value.output,
                 "target directory is not empty: my-test-project");
}

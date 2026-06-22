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

TEST(Util, StringsAndProjectDirectoryName) {
  GitOverleafError err = {};
  char sample[] = " \t hello world \n";
  EXPECT_STREQ("hello world", git_overleaf_trim(sample));

  CStr text(git_overleaf_trimmed_dup("  abc  "));
  ASSERT_NE(nullptr, text);
  EXPECT_STREQ("abc", text.get());

  CStr joined(git_overleaf_path_join("left", "right"));
  ASSERT_NE(nullptr, joined);
  EXPECT_STREQ("left/right", joined.get());

  joined.reset(git_overleaf_path_join("left/", "right"));
  ASSERT_NE(nullptr, joined);
  EXPECT_STREQ("left/right", joined.get());

  CStr url(git_overleaf_sanitize_url(" https://example.test/// "));
  ASSERT_NE(nullptr, url);
  EXPECT_STREQ("https://example.test", url.get());

  url.reset(git_overleaf_url_join(" https://example.test/ ", "/project/abc"));
  ASSERT_NE(nullptr, url);
  EXPECT_STREQ("https://example.test/project/abc", url.get());

  {
    EnvGuard home("HOME");
    setenv("HOME", "/tmp/git-overleaf-home", 1);
    CStr expanded(git_overleaf_expand_home("~/cookies"));
    ASSERT_NE(nullptr, expanded);
    EXPECT_STREQ("/tmp/git-overleaf-home/cookies", expanded.get());
  }

  CStr target(
      git_overleaf_project_directory_name("  My Test_Project!!!  ", &err));
  ASSERT_NE(nullptr, target);
  EXPECT_STREQ("my-test-project", target.get());

  target.reset(git_overleaf_project_directory_name("Project 2026: v2", &err));
  ASSERT_NE(nullptr, target);
  EXPECT_STREQ("project-2026-v2", target.get());

  target.reset(git_overleaf_project_directory_name(" --- ", &err));
  EXPECT_EQ(nullptr, target.get());
  ExpectContains(err.message, "cannot derive clone target");
}

TEST(Config, CookieSources) {
  GitOverleafError err = {};
  EnvGuard cookie("GIT_OVERLEAF_COOKIE");
  setenv("GIT_OVERLEAF_COOKIE", " env-cookie=1 ", 1);

  {
    ConfigGuard cfg;
    ASSERT_EQ(0, git_overleaf_config_load_cookie(&cfg.value, &err));
    EXPECT_STREQ("env-cookie=1", cfg.value.cookie);
  }

  unsetenv("GIT_OVERLEAF_COOKIE");
  TempDir root;
  ASSERT_NE(nullptr, root.path());
  CStr cookie_path(git_overleaf_path_join(root.path(), "cookie"));
  ASSERT_NE(nullptr, cookie_path);
  ASSERT_EQ(0, git_overleaf_write_private_file(cookie_path.get(),
                                               "  a=b; c=d  ", &err));
  struct stat st;
  ASSERT_EQ(0, stat(cookie_path.get(), &st));
  EXPECT_EQ(0600, static_cast<int>(st.st_mode & 0777));

  ConfigGuard cfg;
  free(cfg.value.cookie_file);
  cfg.value.cookie_file = git_overleaf_xstrdup(cookie_path.get());
  ASSERT_NE(nullptr, cfg.value.cookie_file);
  ASSERT_EQ(0, git_overleaf_config_load_cookie(&cfg.value, &err));
  EXPECT_STREQ("a=b; c=d", cfg.value.cookie);
}

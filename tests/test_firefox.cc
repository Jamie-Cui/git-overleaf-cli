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

TEST(FirefoxAuth, ImportsCookieHeaderFromExplicitProfile) {
  GitOverleafError err = {};
  TempDir profile;
  ASSERT_NE(nullptr, profile.path());
  CStr db_path(git_overleaf_path_join(profile.path(), "cookies.sqlite"));
  ASSERT_NE(nullptr, db_path);

  time_t future = time(nullptr) + 3600;
  CreateFirefoxCookieDb(db_path.get(),
                        {{"overleaf_session2", "session-value", ".overleaf.com",
                          "/", std::to_string(static_cast<long long>(future))},
                         {"pref", "dark", "www.overleaf.com", "/project",
                          std::to_string(static_cast<long long>(future))}});

  ConfigGuard cfg;
  char* header_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_firefox_cookie_header(&cfg.value, profile.path(),
                                                  &header_raw, &err))
      << err.message;
  CStr header(header_raw);
  ExpectContains(header.get(), "overleaf_session2=session-value");
  ExpectContains(header.get(), "pref=dark");
}

TEST(FirefoxAuth, HandlesSelfHostedHostAndExpiredSessions) {
  GitOverleafError err = {};
  TempDir profile;
  ASSERT_NE(nullptr, profile.path());
  CStr db_path(git_overleaf_path_join(profile.path(), "cookies.sqlite"));
  ASSERT_NE(nullptr, db_path);

  time_t future = time(nullptr) + 3600;
  CreateFirefoxCookieDb(
      db_path.get(), {{"sharelatex_session", "self-hosted", "latex.example.edu",
                       "/", std::to_string(static_cast<long long>(future))}});

  ConfigGuard cfg;
  free(cfg.value.url);
  cfg.value.url = git_overleaf_xstrdup("https://latex.example.edu");
  ASSERT_NE(nullptr, cfg.value.url);
  char* header_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_firefox_cookie_header(&cfg.value, profile.path(),
                                                  &header_raw, &err))
      << err.message;
  CStr header(header_raw);
  ExpectContains(header.get(), "sharelatex_session=self-hosted");

  TempDir expired_profile;
  ASSERT_NE(nullptr, expired_profile.path());
  CStr expired_db(
      git_overleaf_path_join(expired_profile.path(), "cookies.sqlite"));
  ASSERT_NE(nullptr, expired_db);
  CreateFirefoxCookieDb(expired_db.get(), {{"connect.sid", "expired",
                                            ".overleaf.com", "/", "1"}});
  header_raw = nullptr;
  ASSERT_EQ(-1, git_overleaf_firefox_cookie_header(
                    nullptr, expired_profile.path(), &header_raw, &err));
  EXPECT_EQ(nullptr, header_raw);
  ExpectContains(err.message, "expired");

  TempDir no_session_profile;
  ASSERT_NE(nullptr, no_session_profile.path());
  CStr no_session_db(
      git_overleaf_path_join(no_session_profile.path(), "cookies.sqlite"));
  ASSERT_NE(nullptr, no_session_db);
  CreateFirefoxCookieDb(no_session_db.get(),
                        {{"pref", "dark", ".overleaf.com", "/",
                          std::to_string(static_cast<long long>(future))}});
  ASSERT_EQ(-1, git_overleaf_firefox_cookie_header(
                    nullptr, no_session_profile.path(), &header_raw, &err));
  EXPECT_EQ(nullptr, header_raw);
  ExpectContains(err.message, "no authenticated session cookie");
}

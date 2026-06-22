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

void FreeDeleter::operator()(char* ptr) const { free(ptr); }

EnvGuard::EnvGuard(const char* name) : name_(name) {
  const char* value = getenv(name);
  if (value) {
    old_value_ = value;
    had_value_ = true;
  }
}

EnvGuard::~EnvGuard() {
  if (had_value_) {
    setenv(name_.c_str(), old_value_.c_str(), 1);
  } else {
    unsetenv(name_.c_str());
  }
}

TempDir::TempDir() {
  const char* tmp = getenv("TMPDIR");
  if (!tmp || !*tmp) {
    tmp = "/tmp";
  }
  size_t len = strlen(tmp) + strlen("/git-overleaf-test.XXXXXX") + 1;
  char* path = static_cast<char*>(malloc(len));
  if (!path) {
    return;
  }
  snprintf(path, len, "%s/git-overleaf-test.XXXXXX", tmp);
  if (!mkdtemp(path)) {
    free(path);
    return;
  }
  path_.reset(path);
}

TempDir::~TempDir() {
  if (path_) {
    GitOverleafError ignored = {};
    git_overleaf_remove_tree(path_.get(), &ignored);
  }
}

const char* TempDir::path() const { return path_.get(); }

ConfigGuard::ConfigGuard() { git_overleaf_config_init(&value); }

ConfigGuard::~ConfigGuard() { git_overleaf_config_free(&value); }

ProjectListGuard::ProjectListGuard(size_t len) {
  value.items =
      static_cast<GitOverleafProject*>(calloc(len, sizeof(GitOverleafProject)));
  value.len = value.items ? len : 0;
}

ProjectListGuard::~ProjectListGuard() {
  git_overleaf_project_list_free(&value);
}

void ProjectListGuard::Set(size_t index, const char* id, const char* name,
                           const char* owner_email) {
  ASSERT_LT(index, value.len);
  value.items[index].id = git_overleaf_xstrdup(id);
  value.items[index].name = git_overleaf_xstrdup(name);
  value.items[index].owner_email = git_overleaf_xstrdup(owner_email);
  ASSERT_NE(nullptr, value.items[index].id);
  ASSERT_NE(nullptr, value.items[index].name);
  ASSERT_NE(nullptr, value.items[index].owner_email);
}

ProcessResultGuard::~ProcessResultGuard() {
  git_overleaf_process_result_free(&value);
}

int write_text(const char* path, const char* text) {
  FILE* file = fopen(path, "wb");
  if (!file) {
    return -1;
  }
  if (fputs(text, file) == EOF) {
    fclose(file);
    return -1;
  }
  return fclose(file);
}

CStr join3(const char* left, const char* middle, const char* right) {
  CStr prefix(git_overleaf_path_join(left, middle));
  return CStr(prefix ? git_overleaf_path_join(prefix.get(), right) : nullptr);
}

size_t count_substring(const char* text, const char* needle) {
  size_t count = 0;
  size_t needle_len = strlen(needle);
  const char* p = text;
  while ((p = strstr(p, needle))) {
    count++;
    p += needle_len;
  }
  return count;
}

void ExpectContains(const char* text, const char* needle) {
  ASSERT_NE(nullptr, text);
  ASSERT_NE(nullptr, needle);
  EXPECT_NE(nullptr, strstr(text, needle)) << text;
}

void RunCommand(const std::vector<std::string>& args, const char* cwd,
                int allow_failure, GitOverleafProcessResult* result) {
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  GitOverleafError err = {};
  ASSERT_EQ(0, git_overleaf_process_run(argv.data(), cwd, nullptr,
                                        allow_failure, result, &err))
      << err.message;
}

void RunCli(const std::vector<std::string>& args, const char* cwd,
            GitOverleafProcessResult* result) {
  std::vector<std::string> full_args;
  full_args.reserve(args.size() + 1);
  full_args.push_back(GIT_OVERLEAF_CLI_PATH);
  full_args.insert(full_args.end(), args.begin(), args.end());
  RunCommand(full_args, cwd, 1, result);
}

static void ExecSql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db, sql, nullptr, nullptr, &error))
      << (error ? error : "");
  sqlite3_free(error);
}

void CreateFirefoxCookieDb(const char* path,
                           const std::vector<std::vector<std::string>>& rows) {
  sqlite3* db = nullptr;
  ASSERT_EQ(SQLITE_OK, sqlite3_open(path, &db));
  ASSERT_NE(nullptr, db);
  ExecSql(db,
          "create table moz_cookies ("
          "name text, value text, host text, path text, expiry integer)");
  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(db,
                                          "insert into moz_cookies "
                                          "(name, value, host, path, expiry) "
                                          "values (?, ?, ?, ?, ?)",
                                          -1, &stmt, nullptr));
  ASSERT_NE(nullptr, stmt);
  for (const auto& row : rows) {
    ASSERT_EQ(5u, row.size());
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    ASSERT_EQ(SQLITE_OK,
              sqlite3_bind_text(stmt, 1, row[0].c_str(), -1, SQLITE_TRANSIENT));
    ASSERT_EQ(SQLITE_OK,
              sqlite3_bind_text(stmt, 2, row[1].c_str(), -1, SQLITE_TRANSIENT));
    ASSERT_EQ(SQLITE_OK,
              sqlite3_bind_text(stmt, 3, row[2].c_str(), -1, SQLITE_TRANSIENT));
    ASSERT_EQ(SQLITE_OK,
              sqlite3_bind_text(stmt, 4, row[3].c_str(), -1, SQLITE_TRANSIENT));
    ASSERT_EQ(SQLITE_OK, sqlite3_bind_int64(stmt, 5,
                                            static_cast<sqlite3_int64>(strtoll(
                                                row[4].c_str(), nullptr, 10))));
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(stmt));
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

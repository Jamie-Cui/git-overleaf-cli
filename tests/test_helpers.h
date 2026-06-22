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

#pragma once

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include "git-overleaf-cli.h"

struct FreeDeleter {
  void operator()(char* ptr) const;
};

using CStr = std::unique_ptr<char, FreeDeleter>;

class EnvGuard {
 public:
  explicit EnvGuard(const char* name);
  ~EnvGuard();

 private:
  std::string name_;
  std::string old_value_;
  bool had_value_ = false;
};

class TempDir {
 public:
  TempDir();
  ~TempDir();

  const char* path() const;

 private:
  CStr path_;
};

class ConfigGuard {
 public:
  ConfigGuard();
  ~ConfigGuard();

  GitOverleafConfig value;
};

class ProjectListGuard {
 public:
  explicit ProjectListGuard(size_t len);
  ~ProjectListGuard();

  void Set(size_t index, const char* id, const char* name,
           const char* owner_email);

  GitOverleafProjectList value = {};
};

struct ProcessResultGuard {
  GitOverleafProcessResult value = {};

  ~ProcessResultGuard();
};

int write_text(const char* path, const char* text);
CStr join3(const char* left, const char* middle, const char* right);
size_t count_substring(const char* text, const char* needle);
void ExpectContains(const char* text, const char* needle);
void RunCommand(const std::vector<std::string>& args, const char* cwd,
                int allow_failure, GitOverleafProcessResult* result);
void RunCli(const std::vector<std::string>& args, const char* cwd,
            GitOverleafProcessResult* result);
void CreateFirefoxCookieDb(
    const char* path, const std::vector<std::vector<std::string>>& rows);

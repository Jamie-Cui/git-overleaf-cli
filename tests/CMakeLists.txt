# Copyright (C) 2026 Jamie Cui <jamie.cui@outlook.com>

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(git_overleaf_unit_tests
  test_cli.cc
  test_firefox.cc
  test_fs.cc
  test_git.cc
  test_helpers.cc
  test_http.cc
  test_overleaf.cc
  test_process.cc
  test_projects.cc
  test_util.cc
)
target_compile_features(git_overleaf_unit_tests PRIVATE cxx_std_17)
target_compile_definitions(git_overleaf_unit_tests
  PRIVATE
    GIT_OVERLEAF_CLI_PATH="$<TARGET_FILE:git-overleaf-cli>"
)
target_link_libraries(git_overleaf_unit_tests
  PRIVATE
    git_overleaf_core
    GTest::gtest_main
)
add_dependencies(git_overleaf_unit_tests git-overleaf-cli)
git_overleaf_apply_warnings(git_overleaf_unit_tests)
git_overleaf_enable_coverage(git_overleaf_unit_tests)

gtest_discover_tests(git_overleaf_unit_tests DISCOVERY_MODE PRE_TEST)

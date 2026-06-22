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

cmake_minimum_required(VERSION 3.16)

project(git_overleaf_cli VERSION 0.1 LANGUAGES C CXX)

include(CTest)

option(GIT_OVERLEAF_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

function(git_overleaf_apply_warnings target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(git_overleaf_enable_coverage target)
  if(NOT GIT_OVERLEAF_ENABLE_COVERAGE)
    return()
  endif()
  if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "Coverage is only configured for GCC/Clang")
    return()
  endif()
  target_compile_options(${target} PRIVATE -O0 -g --coverage)
  get_target_property(target_type ${target} TYPE)
  if(NOT target_type STREQUAL "STATIC_LIBRARY")
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

add_library(git_overleaf_core STATIC
  src/firefox.c
  src/fs.c
  src/git.c
  src/http.c
  src/overleaf.c
  src/process.c
  src/projects.c
  src/util.c
)
target_compile_features(git_overleaf_core PUBLIC c_std_11)
target_compile_definitions(git_overleaf_core PUBLIC _DARWIN_C_SOURCE)
target_include_directories(git_overleaf_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(git_overleaf_core
  PUBLIC
    PkgConfig::LIBCURL
    PkgConfig::JANSSON
    PkgConfig::SQLITE3
)
git_overleaf_apply_warnings(git_overleaf_core)
git_overleaf_enable_coverage(git_overleaf_core)

add_executable(git-overleaf-cli src/main.c)
target_link_libraries(git-overleaf-cli PRIVATE git_overleaf_core)
git_overleaf_apply_warnings(git-overleaf-cli)
git_overleaf_enable_coverage(git-overleaf-cli)

install(TARGETS git-overleaf-cli RUNTIME DESTINATION bin)

set(GIT_OVERLEAF_COVERAGE_DEPENDS git-overleaf-cli)
if(BUILD_TESTING)
  add_subdirectory(tests)
  list(APPEND GIT_OVERLEAF_COVERAGE_DEPENDS git_overleaf_unit_tests)
endif()

if(GIT_OVERLEAF_ENABLE_COVERAGE)
  find_program(GCOVR_EXECUTABLE gcovr)
  if(GCOVR_EXECUTABLE)
    add_custom_target(coverage
      COMMAND
        ${CMAKE_COMMAND}
        -D COVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/RemoveCoverageData.cmake
      COMMAND
        ${CMAKE_CTEST_COMMAND}
        --test-dir ${CMAKE_BINARY_DIR}
        --output-on-failure
      COMMAND
        ${GCOVR_EXECUTABLE}
        --root ${PROJECT_SOURCE_DIR}
        --object-directory ${CMAKE_BINARY_DIR}
        --filter ${PROJECT_SOURCE_DIR}/src
        --gcov-ignore-parse-errors negative_hits.warn_once_per_file
        --print-summary
        --xml-pretty
        -o ${CMAKE_BINARY_DIR}/coverage.xml
        ${CMAKE_BINARY_DIR}
      DEPENDS ${GIT_OVERLEAF_COVERAGE_DEPENDS}
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      USES_TERMINAL
      COMMENT "Running tests and generating coverage summary")
  else()
    add_custom_target(coverage
      COMMAND
        ${CMAKE_COMMAND} -E echo
        "gcovr is required for the coverage target. Install gcovr and rerun CMake."
      COMMAND ${CMAKE_COMMAND} -E false)
  endif()
else()
  add_custom_target(coverage
    COMMAND
      ${CMAKE_COMMAND} -E echo
      "Coverage is disabled. Reconfigure with -DGIT_OVERLEAF_ENABLE_COVERAGE=ON."
    COMMAND ${CMAKE_COMMAND} -E false)
endif()

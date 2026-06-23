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

include(FetchContent)
include(ExternalProject)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_POLICY_VERSION_MINIMUM
  "3.5"
  CACHE STRING "Minimum CMake policy compatibility for vendored projects")

set(GIT_OVERLEAF_VENDOR_PREFIX
  "${CMAKE_BINARY_DIR}/_vendor"
  CACHE PATH "Install prefix for vendored static dependencies")
set(GIT_OVERLEAF_VENDOR_OPENSSL_PLATFORM
  "linux-x86_64"
  CACHE STRING "OpenSSL Configure target for vendored builds")

set(GIT_OVERLEAF_VENDOR_OPENSSL_VERSION
  "3.3.1"
  CACHE STRING "Vendored OpenSSL version")
set(GIT_OVERLEAF_VENDOR_CURL_VERSION
  "8.18.0"
  CACHE STRING "Vendored curl version")
set(GIT_OVERLEAF_VENDOR_ZLIB_VERSION
  "1.3.1"
  CACHE STRING "Vendored zlib version")
set(GIT_OVERLEAF_VENDOR_JANSSON_VERSION
  "2.14"
  CACHE STRING "Vendored jansson version")
set(GIT_OVERLEAF_VENDOR_SQLITE_YEAR
  "2024"
  CACHE STRING "Vendored SQLite download year path")
set(GIT_OVERLEAF_VENDOR_SQLITE_VERSION_NUMBER
  "3450300"
  CACHE STRING "Vendored SQLite numeric amalgamation version")

set(GIT_OVERLEAF_VENDOR_OPENSSL_URL
  "https://www.openssl.org/source/openssl-${GIT_OVERLEAF_VENDOR_OPENSSL_VERSION}.tar.gz"
  CACHE STRING "Vendored OpenSSL source archive URL")
set(GIT_OVERLEAF_VENDOR_CURL_URL
  "https://curl.se/download/curl-${GIT_OVERLEAF_VENDOR_CURL_VERSION}.tar.xz"
  CACHE STRING "Vendored curl source archive URL")
set(GIT_OVERLEAF_VENDOR_ZLIB_URL
  "https://zlib.net/fossils/zlib-${GIT_OVERLEAF_VENDOR_ZLIB_VERSION}.tar.gz"
  CACHE STRING "Vendored zlib source archive URL")
set(GIT_OVERLEAF_VENDOR_JANSSON_URL
  "https://github.com/akheron/jansson/releases/download/v${GIT_OVERLEAF_VENDOR_JANSSON_VERSION}/jansson-${GIT_OVERLEAF_VENDOR_JANSSON_VERSION}.tar.gz"
  CACHE STRING "Vendored jansson source archive URL")
set(GIT_OVERLEAF_VENDOR_SQLITE_URL
  "https://www.sqlite.org/${GIT_OVERLEAF_VENDOR_SQLITE_YEAR}/sqlite-amalgamation-${GIT_OVERLEAF_VENDOR_SQLITE_VERSION_NUMBER}.zip"
  CACHE STRING "Vendored SQLite amalgamation archive URL")

set(GIT_OVERLEAF_VENDOR_OPENSSL_URL_HASH
  "SHA256=777cd596284c883375a2a7a11bf5d2786fc5413255efab20c50d6ffe6d020b7e"
  CACHE STRING "Optional URL_HASH for vendored OpenSSL, for example SHA256=...")
set(GIT_OVERLEAF_VENDOR_CURL_URL_HASH
  "SHA256=40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946"
  CACHE STRING "Optional URL_HASH for vendored curl, for example SHA256=...")
set(GIT_OVERLEAF_VENDOR_ZLIB_URL_HASH
  "SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
  CACHE STRING "Optional URL_HASH for vendored zlib, for example SHA256=...")
set(GIT_OVERLEAF_VENDOR_JANSSON_URL_HASH
  "SHA256=5798d010e41cf8d76b66236cfb2f2543c8d082181d16bc3085ab49538d4b9929"
  CACHE STRING "Optional URL_HASH for vendored jansson, for example SHA256=...")
set(GIT_OVERLEAF_VENDOR_SQLITE_URL_HASH
  "SHA256=ea170e73e447703e8359308ca2e4366a3ae0c4304a8665896f068c736781c651"
  CACHE STRING "Optional URL_HASH for vendored SQLite, for example SHA256=...")

mark_as_advanced(
  CMAKE_POLICY_VERSION_MINIMUM
  GIT_OVERLEAF_VENDOR_PREFIX
  GIT_OVERLEAF_VENDOR_OPENSSL_PLATFORM
  GIT_OVERLEAF_VENDOR_OPENSSL_VERSION
  GIT_OVERLEAF_VENDOR_CURL_VERSION
  GIT_OVERLEAF_VENDOR_ZLIB_VERSION
  GIT_OVERLEAF_VENDOR_JANSSON_VERSION
  GIT_OVERLEAF_VENDOR_SQLITE_YEAR
  GIT_OVERLEAF_VENDOR_SQLITE_VERSION_NUMBER
  GIT_OVERLEAF_VENDOR_OPENSSL_URL
  GIT_OVERLEAF_VENDOR_CURL_URL
  GIT_OVERLEAF_VENDOR_ZLIB_URL
  GIT_OVERLEAF_VENDOR_JANSSON_URL
  GIT_OVERLEAF_VENDOR_SQLITE_URL
  GIT_OVERLEAF_VENDOR_OPENSSL_URL_HASH
  GIT_OVERLEAF_VENDOR_CURL_URL_HASH
  GIT_OVERLEAF_VENDOR_ZLIB_URL_HASH
  GIT_OVERLEAF_VENDOR_JANSSON_URL_HASH
  GIT_OVERLEAF_VENDOR_SQLITE_URL_HASH
)

function(git_overleaf_fetchcontent_declare_archive name url url_hash)
  if(url_hash)
    FetchContent_Declare(${name}
      URL "${url}"
      URL_HASH "${url_hash}"
    )
  else()
    message(WARNING
      "No URL_HASH is set for ${name}; set the corresponding "
      "GIT_OVERLEAF_VENDOR_*_URL_HASH variable for reproducible builds.")
    FetchContent_Declare(${name}
      URL "${url}"
    )
  endif()
endfunction()

function(git_overleaf_externalproject_url_args out_var url url_hash)
  if(url_hash)
    set(args URL "${url}" URL_HASH "${url_hash}")
  else()
    message(WARNING
      "No URL_HASH is set for ${url}; set the corresponding "
      "GIT_OVERLEAF_VENDOR_*_URL_HASH variable for reproducible builds.")
    set(args URL "${url}")
  endif()
  set(${out_var} "${args}" PARENT_SCOPE)
endfunction()

function(git_overleaf_find_vendored_archive out_var install_dir name)
  set(candidates
    "${install_dir}/lib/lib${name}.a"
    "${install_dir}/lib64/lib${name}.a"
  )
  foreach(candidate IN LISTS candidates)
    if(EXISTS "${candidate}")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "${install_dir}/lib/lib${name}.a" PARENT_SCOPE)
endfunction()

function(git_overleaf_configure_vendored_jansson)
  set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(JANSSON_BUILD_MAN OFF CACHE BOOL "" FORCE)
  set(JANSSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  git_overleaf_fetchcontent_declare_archive(
    git_overleaf_jansson
    "${GIT_OVERLEAF_VENDOR_JANSSON_URL}"
    "${GIT_OVERLEAF_VENDOR_JANSSON_URL_HASH}"
  )
  FetchContent_MakeAvailable(git_overleaf_jansson)

  add_library(git_overleaf_dep_jansson INTERFACE)
  if(TARGET jansson)
    target_link_libraries(git_overleaf_dep_jansson INTERFACE jansson)
  elseif(TARGET jansson_static)
    target_link_libraries(git_overleaf_dep_jansson INTERFACE jansson_static)
  else()
    message(FATAL_ERROR "Vendored jansson did not provide a known CMake target")
  endif()
endfunction()

function(git_overleaf_configure_vendored_sqlite)
  git_overleaf_fetchcontent_declare_archive(
    git_overleaf_sqlite
    "${GIT_OVERLEAF_VENDOR_SQLITE_URL}"
    "${GIT_OVERLEAF_VENDOR_SQLITE_URL_HASH}"
  )
  FetchContent_MakeAvailable(git_overleaf_sqlite)
  FetchContent_GetProperties(git_overleaf_sqlite)

  add_library(git_overleaf_vendor_sqlite STATIC
    "${git_overleaf_sqlite_SOURCE_DIR}/sqlite3.c"
  )
  target_include_directories(git_overleaf_vendor_sqlite PUBLIC
    "${git_overleaf_sqlite_SOURCE_DIR}"
  )
  target_compile_definitions(git_overleaf_vendor_sqlite PRIVATE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
  )

  add_library(git_overleaf_dep_sqlite3 INTERFACE)
  target_link_libraries(git_overleaf_dep_sqlite3 INTERFACE
    git_overleaf_vendor_sqlite
  )
  if(UNIX AND NOT APPLE)
    target_link_libraries(git_overleaf_dep_sqlite3 INTERFACE m)
  endif()
endfunction()

function(git_overleaf_configure_vendored_curl)
  find_program(PERL_EXECUTABLE perl)
  if(NOT PERL_EXECUTABLE)
    message(FATAL_ERROR "Perl is required to build vendored OpenSSL")
  endif()

  if(CMAKE_BUILD_TYPE)
    set(vendored_build_type "${CMAKE_BUILD_TYPE}")
  else()
    set(vendored_build_type "Release")
  endif()

  set(openssl_install "${GIT_OVERLEAF_VENDOR_PREFIX}/openssl")
  set(zlib_install "${GIT_OVERLEAF_VENDOR_PREFIX}/zlib")
  set(curl_install "${GIT_OVERLEAF_VENDOR_PREFIX}/curl")
  set(openssl_include_dir "${openssl_install}/include")
  set(zlib_include_dir "${zlib_install}/include")
  set(curl_include_dir "${curl_install}/include")
  file(MAKE_DIRECTORY
    "${openssl_include_dir}"
    "${zlib_include_dir}"
    "${curl_include_dir}"
  )

  git_overleaf_find_vendored_archive(openssl_ssl_library
    "${openssl_install}" ssl)
  git_overleaf_find_vendored_archive(openssl_crypto_library
    "${openssl_install}" crypto)
  git_overleaf_find_vendored_archive(zlib_library "${zlib_install}" z)
  git_overleaf_find_vendored_archive(curl_library "${curl_install}" curl)

  git_overleaf_externalproject_url_args(openssl_url_args
    "${GIT_OVERLEAF_VENDOR_OPENSSL_URL}"
    "${GIT_OVERLEAF_VENDOR_OPENSSL_URL_HASH}"
  )
  ExternalProject_Add(git_overleaf_vendor_openssl_ep
    ${openssl_url_args}
    PREFIX "${GIT_OVERLEAF_VENDOR_PREFIX}/src/openssl"
    INSTALL_DIR "${openssl_install}"
    CONFIGURE_COMMAND
      "${PERL_EXECUTABLE}" <SOURCE_DIR>/Configure
        ${GIT_OVERLEAF_VENDOR_OPENSSL_PLATFORM}
        no-apps
        no-async
        no-comp
        no-dso
        no-module
        no-shared
        no-tests
        no-zlib
        "--prefix=<INSTALL_DIR>"
        "--openssldir=<INSTALL_DIR>/ssl"
        "--libdir=lib"
    BUILD_COMMAND "${CMAKE_MAKE_PROGRAM}"
    INSTALL_COMMAND "${CMAKE_MAKE_PROGRAM}" install_sw
    BUILD_BYPRODUCTS
      "${openssl_ssl_library}"
      "${openssl_crypto_library}"
  )

  git_overleaf_externalproject_url_args(zlib_url_args
    "${GIT_OVERLEAF_VENDOR_ZLIB_URL}"
    "${GIT_OVERLEAF_VENDOR_ZLIB_URL_HASH}"
  )
  ExternalProject_Add(git_overleaf_vendor_zlib_ep
    ${zlib_url_args}
    PREFIX "${GIT_OVERLEAF_VENDOR_PREFIX}/src/zlib"
    INSTALL_DIR "${zlib_install}"
    CMAKE_ARGS
      "-DCMAKE_BUILD_TYPE=${vendored_build_type}"
      "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
      "-DCMAKE_INSTALL_LIBDIR=lib"
      "-DBUILD_SHARED_LIBS=OFF"
      "-DZLIB_BUILD_EXAMPLES=OFF"
    BUILD_BYPRODUCTS "${zlib_library}"
  )

  git_overleaf_externalproject_url_args(curl_url_args
    "${GIT_OVERLEAF_VENDOR_CURL_URL}"
    "${GIT_OVERLEAF_VENDOR_CURL_URL_HASH}"
  )
  ExternalProject_Add(git_overleaf_vendor_curl_ep
    ${curl_url_args}
    DEPENDS
      git_overleaf_vendor_openssl_ep
      git_overleaf_vendor_zlib_ep
    PREFIX "${GIT_OVERLEAF_VENDOR_PREFIX}/src/curl"
    INSTALL_DIR "${curl_install}"
    CMAKE_ARGS
      "-DCMAKE_BUILD_TYPE=${vendored_build_type}"
      "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
      "-DCMAKE_INSTALL_LIBDIR=lib"
      "-DBUILD_CURL_EXE=OFF"
      "-DBUILD_EXAMPLES=OFF"
      "-DBUILD_LIBCURL_DOCS=OFF"
      "-DBUILD_MISC_DOCS=OFF"
      "-DBUILD_SHARED_LIBS=OFF"
      "-DBUILD_STATIC_LIBS=ON"
      "-DBUILD_TESTING=OFF"
      "-DCURL_BROTLI=OFF"
      "-DCURL_DISABLE_DICT=ON"
      "-DCURL_DISABLE_FILE=ON"
      "-DCURL_DISABLE_FTP=ON"
      "-DCURL_DISABLE_GOPHER=ON"
      "-DCURL_DISABLE_IMAP=ON"
      "-DCURL_DISABLE_LDAP=ON"
      "-DCURL_DISABLE_LDAPS=ON"
      "-DCURL_DISABLE_MQTT=ON"
      "-DCURL_DISABLE_POP3=ON"
      "-DCURL_DISABLE_RTSP=ON"
      "-DCURL_DISABLE_SMB=ON"
      "-DCURL_DISABLE_SMTP=ON"
      "-DCURL_DISABLE_TELNET=ON"
      "-DCURL_DISABLE_TFTP=ON"
      "-DCURL_USE_GSSAPI=OFF"
      "-DCURL_USE_LIBPSL=OFF"
      "-DCURL_USE_LIBSSH=OFF"
      "-DCURL_USE_LIBSSH2=OFF"
      "-DCURL_USE_OPENSSL=ON"
      "-DCURL_ZLIB=OFF"
      "-DCURL_ZSTD=OFF"
      "-DHTTP_ONLY=ON"
      "-DOPENSSL_CRYPTO_LIBRARY=${openssl_crypto_library}"
      "-DOPENSSL_INCLUDE_DIR=${openssl_include_dir}"
      "-DOPENSSL_ROOT_DIR=${openssl_install}"
      "-DOPENSSL_SSL_LIBRARY=${openssl_ssl_library}"
      "-DOPENSSL_USE_STATIC_LIBS=ON"
      "-DUSE_APPLE_IDN=OFF"
      "-DUSE_LIBIDN2=OFF"
      "-DUSE_NGHTTP2=OFF"
      "-DUSE_WIN32_IDN=OFF"
      "-DZLIB_INCLUDE_DIR=${zlib_include_dir}"
      "-DZLIB_LIBRARY=${zlib_library}"
      "-DZLIB_LIBRARY_RELEASE=${zlib_library}"
    BUILD_BYPRODUCTS "${curl_library}"
  )

  add_library(git_overleaf_vendor_ssl STATIC IMPORTED GLOBAL)
  set_target_properties(git_overleaf_vendor_ssl PROPERTIES
    IMPORTED_LOCATION "${openssl_ssl_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${openssl_include_dir}"
  )
  add_dependencies(git_overleaf_vendor_ssl git_overleaf_vendor_openssl_ep)

  add_library(git_overleaf_vendor_crypto STATIC IMPORTED GLOBAL)
  set_target_properties(git_overleaf_vendor_crypto PROPERTIES
    IMPORTED_LOCATION "${openssl_crypto_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${openssl_include_dir}"
  )
  add_dependencies(git_overleaf_vendor_crypto git_overleaf_vendor_openssl_ep)

  add_library(git_overleaf_vendor_zlib STATIC IMPORTED GLOBAL)
  set_target_properties(git_overleaf_vendor_zlib PROPERTIES
    IMPORTED_LOCATION "${zlib_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${zlib_include_dir}"
  )
  add_dependencies(git_overleaf_vendor_zlib git_overleaf_vendor_zlib_ep)

  add_library(git_overleaf_vendor_curl STATIC IMPORTED GLOBAL)
  set_target_properties(git_overleaf_vendor_curl PROPERTIES
    IMPORTED_LOCATION "${curl_library}"
    INTERFACE_COMPILE_DEFINITIONS CURL_STATICLIB
    INTERFACE_INCLUDE_DIRECTORIES "${curl_include_dir}"
  )
  add_dependencies(git_overleaf_vendor_curl git_overleaf_vendor_curl_ep)

  add_library(git_overleaf_dep_libcurl INTERFACE)
  target_link_libraries(git_overleaf_dep_libcurl INTERFACE
    git_overleaf_vendor_curl
    git_overleaf_vendor_ssl
    git_overleaf_vendor_crypto
    git_overleaf_vendor_zlib
  )
  if(UNIX)
    target_link_libraries(git_overleaf_dep_libcurl INTERFACE
      ${CMAKE_DL_LIBS}
      pthread
    )
  endif()
endfunction()

function(git_overleaf_configure_vendored_deps)
  if(NOT UNIX)
    message(FATAL_ERROR "Vendored static dependencies are currently Unix-only")
  endif()
  git_overleaf_configure_vendored_curl()
  git_overleaf_configure_vendored_jansson()
  git_overleaf_configure_vendored_sqlite()
endfunction()

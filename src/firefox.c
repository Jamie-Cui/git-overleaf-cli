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

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "git-overleaf-cli.h"

typedef struct {
  int is_profile;
  int is_install;
  char* path;
  char* isrelative;
  char* default_value;
} GitOverleafFirefoxIniSection;

typedef struct {
  char* install_path;
  char* legacy_path;
  int install_relative;
  int legacy_relative;
} GitOverleafFirefoxProfileChoice;

typedef struct {
  char* name;
  char* value;
  char* host;
  char* path;
  sqlite3_int64 expiry;
} GitOverleafFirefoxCookieRow;

typedef struct {
  GitOverleafFirefoxCookieRow* items;
  size_t len;
  size_t cap;
} GitOverleafFirefoxCookieRows;

static void free_ini_section(GitOverleafFirefoxIniSection* section) {
  if (!section) {
    return;
  }
  free(section->path);
  free(section->isrelative);
  free(section->default_value);
  memset(section, 0, sizeof(*section));
}

static void free_profile_choice(GitOverleafFirefoxProfileChoice* choice) {
  if (!choice) {
    return;
  }
  free(choice->install_path);
  free(choice->legacy_path);
  memset(choice, 0, sizeof(*choice));
}

static int set_string(char** slot, const char* value, GitOverleafError* err) {
  char* copy = git_overleaf_xstrdup(value ? value : "");
  if (!copy) {
    return git_overleaf_error(err, "out of memory");
  }
  free(*slot);
  *slot = copy;
  return 0;
}

static int finish_ini_section(GitOverleafFirefoxIniSection* section,
                              GitOverleafFirefoxProfileChoice* choice,
                              GitOverleafError* err) {
  /* Newer Firefox profiles.ini points Install* sections at the default
     profile; older files mark Profile* sections with Default=1. */
  if (section->is_install && section->default_value && !choice->install_path) {
    choice->install_path = git_overleaf_xstrdup(section->default_value);
    if (!choice->install_path) {
      return git_overleaf_error(err, "out of memory");
    }
    choice->install_relative = 1;
  } else if (section->is_profile && section->default_value &&
             strcmp(section->default_value, "1") == 0 && !choice->legacy_path) {
    choice->legacy_path =
        git_overleaf_xstrdup(section->path ? section->path : "");
    if (!choice->legacy_path) {
      return git_overleaf_error(err, "out of memory");
    }
    choice->legacy_relative =
        section->isrelative && strcmp(section->isrelative, "1") == 0;
  }
  free_ini_section(section);
  return 0;
}

static int starts_with(const char* s, const char* prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char* path_dirname(const char* path) {
  const char* slash = strrchr(path, '/');
  if (!slash) {
    return git_overleaf_xstrdup(".");
  }
  if (slash == path) {
    return git_overleaf_xstrndup(path, 1);
  }
  return git_overleaf_xstrndup(path, (size_t)(slash - path));
}

static int path_is_directory(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int resolve_profile_path(const char* path, int relative,
                                const char* base_directory, char** out,
                                GitOverleafError* err) {
  *out = NULL;
  if (!path || !*path) {
    return git_overleaf_error(
        err, "Firefox profiles.ini default profile has no Path entry");
  }
  if (relative && path[0] != '/') {
    *out = git_overleaf_path_join(base_directory, path);
  } else {
    *out = git_overleaf_expand_home(path);
  }
  return *out ? 0 : git_overleaf_error(err, "out of memory");
}

static int parse_profiles_ini(const char* ini_file, char** profile_out,
                              GitOverleafError* err) {
  *profile_out = NULL;
  char* text = NULL;
  char* base_directory = NULL;
  GitOverleafFirefoxIniSection section = {0};
  GitOverleafFirefoxProfileChoice choice = {0};
  int rc = -1;

  if (git_overleaf_read_file(ini_file, &text, err) != 0) {
    return -1;
  }
  base_directory = path_dirname(ini_file);
  if (!base_directory) {
    git_overleaf_error(err, "out of memory");
    goto done;
  }

  char* line = text;
  while (line) {
    /* Parse profiles.ini in place. Values are copied out before the backing
       buffer is freed at the end of the function. */
    char* next = strchr(line, '\n');
    if (next) {
      *next++ = '\0';
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') {
      line[len - 1] = '\0';
    }

    char* trimmed = git_overleaf_trim(line);
    if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
      line = next;
      continue;
    }
    len = strlen(trimmed);
    if (len >= 2 && trimmed[0] == '[' && trimmed[len - 1] == ']') {
      if (finish_ini_section(&section, &choice, err) != 0) {
        goto done;
      }
      trimmed[len - 1] = '\0';
      const char* name = trimmed + 1;
      section.is_profile = starts_with(name, "Profile");
      section.is_install = starts_with(name, "Install");
      line = next;
      continue;
    }

    char* equals = strchr(trimmed, '=');
    if (equals && (section.is_profile || section.is_install)) {
      *equals = '\0';
      char* key = git_overleaf_trim(trimmed);
      char* value = git_overleaf_trim(equals + 1);
      if (strcasecmp(key, "Path") == 0) {
        if (set_string(&section.path, value, err) != 0) {
          goto done;
        }
      } else if (strcasecmp(key, "IsRelative") == 0) {
        if (set_string(&section.isrelative, value, err) != 0) {
          goto done;
        }
      } else if (strcasecmp(key, "Default") == 0) {
        if (set_string(&section.default_value, value, err) != 0) {
          goto done;
        }
      }
    }
    line = next;
  }
  if (finish_ini_section(&section, &choice, err) != 0) {
    goto done;
  }

  if (choice.install_path) {
    rc = resolve_profile_path(choice.install_path, choice.install_relative,
                              base_directory, profile_out, err);
  } else if (choice.legacy_path) {
    rc = resolve_profile_path(choice.legacy_path, choice.legacy_relative,
                              base_directory, profile_out, err);
  } else {
    rc = git_overleaf_error(
        err,
        "could not determine the default Firefox profile.  Use "
        "--firefox-profile to specify one");
  }

done:
  free_ini_section(&section);
  free_profile_choice(&choice);
  free(base_directory);
  free(text);
  return rc;
}

static int find_profiles_ini(char** out, GitOverleafError* err) {
  *out = NULL;
  char* candidates[3] = {0};
  int count = 0;
  candidates[count++] = git_overleaf_expand_home(
      "~/Library/Application Support/Firefox/"
      "profiles.ini");
  candidates[count++] = git_overleaf_expand_home(
      "~/.mozilla/firefox/"
      "profiles.ini");
  const char* appdata = getenv("APPDATA");
  if (appdata && *appdata) {
    candidates[count++] =
        git_overleaf_path_join(appdata, "Mozilla/Firefox/profiles.ini");
  }

  for (int i = 0; i < count; i++) {
    if (!candidates[i]) {
      for (int j = 0; j < count; j++) {
        free(candidates[j]);
      }
      return git_overleaf_error(err, "out of memory");
    }
    if (access(candidates[i], R_OK) == 0) {
      *out = candidates[i];
      for (int j = 0; j < count; j++) {
        if (j != i) {
          free(candidates[j]);
        }
      }
      return 0;
    }
  }

  for (int i = 0; i < count; i++) {
    free(candidates[i]);
  }
  return git_overleaf_error(
      err,
      "could not find Firefox profiles.ini.  Use --firefox-profile to "
      "specify a Firefox profile directory");
}

static int firefox_profile_directory(const char* explicit_profile, char** out,
                                     GitOverleafError* err) {
  *out = NULL;
  if (explicit_profile && *explicit_profile) {
    *out = git_overleaf_expand_home(explicit_profile);
    if (!*out) {
      return git_overleaf_error(err, "out of memory");
    }
  } else {
    char* ini_file = NULL;
    if (find_profiles_ini(&ini_file, err) != 0) {
      return -1;
    }
    int rc = parse_profiles_ini(ini_file, out, err);
    free(ini_file);
    if (rc != 0) {
      return -1;
    }
  }

  if (!path_is_directory(*out)) {
    int rc = git_overleaf_error(
        err, "Firefox profile directory does not exist: %s", *out);
    free(*out);
    *out = NULL;
    return rc;
  }
  return 0;
}

static char* concat_suffix(const char* text, const char* suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  char* out = malloc(text_len + suffix_len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, text, text_len);
  memcpy(out + text_len, suffix, suffix_len + 1);
  return out;
}

static int copy_regular_file(const char* source, const char* destination,
                             GitOverleafError* err) {
  int in = open(source, O_RDONLY);
  if (in < 0) {
    return git_overleaf_error(err, "could not open %s: %s", source,
                              strerror(errno));
  }
  int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (out < 0) {
    int saved = errno;
    close(in);
    return git_overleaf_error(err, "could not create %s: %s", destination,
                              strerror(saved));
  }

  char buffer[65536];
  for (;;) {
    ssize_t n = read(in, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      int saved = errno;
      close(in);
      close(out);
      return git_overleaf_error(err, "could not read %s: %s", source,
                                strerror(saved));
    }
    if (n == 0) {
      break;
    }
    char* p = buffer;
    ssize_t remaining = n;
    while (remaining > 0) {
      ssize_t written = write(out, p, (size_t)remaining);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        int saved = errno;
        close(in);
        close(out);
        return git_overleaf_error(err, "could not write %s: %s", destination,
                                  strerror(saved));
      }
      p += written;
      remaining -= written;
    }
  }
  int close_in = close(in);
  int close_out = close(out);
  if (close_in != 0 || close_out != 0) {
    return git_overleaf_error(err, "could not finish copying %s", source);
  }
  return 0;
}

static int copy_cookie_store(const char* profile, char** temp_dir_out,
                             char** db_path_out, GitOverleafError* err) {
  *temp_dir_out = NULL;
  *db_path_out = NULL;
  char* source = git_overleaf_path_join(profile, "cookies.sqlite");
  if (!source) {
    return git_overleaf_error(err, "out of memory");
  }
  if (access(source, R_OK) != 0) {
    int rc = git_overleaf_error(
        err,
        "Firefox profile %s does not contain a readable cookies.sqlite file",
        profile);
    free(source);
    return rc;
  }

  char* temp_dir = NULL;
  if (git_overleaf_make_temp_dir(&temp_dir, err) != 0) {
    free(source);
    return -1;
  }
  char* target = git_overleaf_path_join(temp_dir, "cookies.sqlite");
  if (!target) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(source);
    return git_overleaf_error(err, "out of memory");
  }

  if (copy_regular_file(source, target, err) != 0) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(target);
    free(source);
    return -1;
  }

  /* Firefox can keep recent cookie writes in SQLite WAL mode while running,
     so copy the sidecar files with the database to get a consistent view. */
  const char* suffixes[] = {"-wal", "-shm"};
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
    char* sidecar_source = concat_suffix(source, suffixes[i]);
    char* sidecar_target = concat_suffix(target, suffixes[i]);
    if (!sidecar_source || !sidecar_target) {
      free(sidecar_source);
      free(sidecar_target);
      git_overleaf_remove_tree(temp_dir, err);
      free(temp_dir);
      free(target);
      free(source);
      return git_overleaf_error(err, "out of memory");
    }
    if (access(sidecar_source, R_OK) == 0 &&
        copy_regular_file(sidecar_source, sidecar_target, err) != 0) {
      free(sidecar_source);
      free(sidecar_target);
      git_overleaf_remove_tree(temp_dir, err);
      free(temp_dir);
      free(target);
      free(source);
      return -1;
    }
    free(sidecar_source);
    free(sidecar_target);
  }

  free(source);
  *temp_dir_out = temp_dir;
  *db_path_out = target;
  return 0;
}

static int url_host(const char* url, char** out, GitOverleafError* err) {
  *out = NULL;
  char* sanitized =
      git_overleaf_sanitize_url(url ? url : GIT_OVERLEAF_DEFAULT_URL);
  if (!sanitized) {
    return git_overleaf_error(err, "out of memory");
  }

  const char* start = sanitized;
  const char* scheme = strstr(sanitized, "://");
  if (scheme) {
    start = scheme + 3;
  }
  const char* authority_end = start;
  while (*authority_end && *authority_end != '/' && *authority_end != '?' &&
         *authority_end != '#') {
    authority_end++;
  }
  const char* host_start = start;
  for (const char* p = start; p < authority_end; p++) {
    if (*p == '@') {
      host_start = p + 1;
    }
  }

  const char* host_end = authority_end;
  if (host_start < authority_end && *host_start == '[') {
    const char* close =
        memchr(host_start, ']', (size_t)(authority_end - host_start));
    if (close) {
      host_start++;
      host_end = close;
    }
  } else {
    for (const char* p = host_start; p < authority_end; p++) {
      if (*p == ':') {
        host_end = p;
        break;
      }
    }
  }

  if (host_end <= host_start) {
    free(sanitized);
    return git_overleaf_error(err, "invalid Overleaf URL: %s",
                              url ? url : GIT_OVERLEAF_DEFAULT_URL);
  }

  char* host =
      git_overleaf_xstrndup(host_start, (size_t)(host_end - host_start));
  free(sanitized);
  if (!host) {
    return git_overleaf_error(err, "out of memory");
  }
  for (char* p = host; *p; p++) {
    *p = (char)tolower((unsigned char)*p);
  }
  *out = host;
  return 0;
}

static int append_unique(char*** items, size_t* len, size_t* cap,
                         const char* value, GitOverleafError* err) {
  for (size_t i = 0; i < *len; i++) {
    if (strcmp((*items)[i], value) == 0) {
      return 0;
    }
  }
  if (*len == *cap) {
    size_t new_cap = *cap ? *cap * 2 : 4;
    char** new_items = realloc(*items, new_cap * sizeof(*new_items));
    if (!new_items) {
      return git_overleaf_error(err, "out of memory");
    }
    *items = new_items;
    *cap = new_cap;
  }
  (*items)[*len] = git_overleaf_xstrdup(value);
  if (!(*items)[*len]) {
    return git_overleaf_error(err, "out of memory");
  }
  (*len)++;
  return 0;
}

static int append_dot_candidate(char*** items, size_t* len, size_t* cap,
                                const char* value, GitOverleafError* err) {
  char* dotted = concat_suffix(".", value);
  if (!dotted) {
    return git_overleaf_error(err, "out of memory");
  }
  int rc = append_unique(items, len, cap, dotted, err);
  free(dotted);
  return rc;
}

static void free_string_list(char** items, size_t len) {
  for (size_t i = 0; i < len; i++) {
    free(items[i]);
  }
  free(items);
}

static int cookie_host_candidates(const char* url, char*** out_items,
                                  size_t* out_len, GitOverleafError* err) {
  *out_items = NULL;
  *out_len = 0;
  char* host = NULL;
  char** items = NULL;
  size_t len = 0;
  size_t cap = 0;

  if (url_host(url, &host, err) != 0) {
    return -1;
  }
  if (append_unique(&items, &len, &cap, host, err) != 0 ||
      append_dot_candidate(&items, &len, &cap, host, err) != 0) {
    free(host);
    free_string_list(items, len);
    return -1;
  }
  /* Firefox stores domain cookies both as exact hosts and dotted parent
     domains; include parent suffixes for hosted/self-hosted Overleaf domains. */
  for (const char* dot = strchr(host, '.'); dot; dot = strchr(dot + 1, '.')) {
    const char* suffix = dot + 1;
    if (strchr(suffix, '.')) {
      if (append_unique(&items, &len, &cap, suffix, err) != 0 ||
          append_dot_candidate(&items, &len, &cap, suffix, err) != 0) {
        free(host);
        free_string_list(items, len);
        return -1;
      }
    }
  }

  free(host);
  *out_items = items;
  *out_len = len;
  return 0;
}

static char* cookie_query(size_t host_count) {
  const char* prefix =
      "select name, value, host, path, expiry from "
      "moz_cookies where host in (";
  const char* suffix = ")";
  size_t len = strlen(prefix) + strlen(suffix);
  if (host_count > 0) {
    len += 1 + (host_count - 1) * 3;
  }
  char* query = malloc(len + 1);
  if (!query) {
    return NULL;
  }
  char* p = query;
  size_t prefix_len = strlen(prefix);
  memcpy(p, prefix, prefix_len);
  p += prefix_len;
  for (size_t i = 0; i < host_count; i++) {
    if (i > 0) {
      memcpy(p, ", ", 2);
      p += 2;
    }
    *p++ = '?';
  }
  memcpy(p, suffix, strlen(suffix) + 1);
  return query;
}

static void cookie_rows_free(GitOverleafFirefoxCookieRows* rows) {
  if (!rows) {
    return;
  }
  for (size_t i = 0; i < rows->len; i++) {
    free(rows->items[i].name);
    free(rows->items[i].value);
    free(rows->items[i].host);
    free(rows->items[i].path);
  }
  free(rows->items);
  memset(rows, 0, sizeof(*rows));
}

static int cookie_rows_append(GitOverleafFirefoxCookieRows* rows,
                              GitOverleafFirefoxCookieRow* row,
                              GitOverleafError* err) {
  if (rows->len == rows->cap) {
    size_t new_cap = rows->cap ? rows->cap * 2 : 16;
    GitOverleafFirefoxCookieRow* new_items =
        realloc(rows->items, new_cap * sizeof(*new_items));
    if (!new_items) {
      return git_overleaf_error(err, "out of memory");
    }
    rows->items = new_items;
    rows->cap = new_cap;
  }
  rows->items[rows->len++] = *row;
  memset(row, 0, sizeof(*row));
  return 0;
}

static char* sqlite_column_dup(sqlite3_stmt* stmt, int column) {
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? git_overleaf_xstrdup((const char*)text) : NULL;
}

static int load_cookie_rows(const char* db_path, char** hosts,
                            size_t host_count,
                            GitOverleafFirefoxCookieRows* rows,
                            GitOverleafError* err) {
  sqlite3* db = NULL;
  sqlite3_stmt* stmt = NULL;
  char* query = NULL;
  int rc = -1;

  if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    git_overleaf_error(err, "could not open Firefox cookies database: %s",
                       db ? sqlite3_errmsg(db) : "out of memory");
    goto done;
  }

  query = cookie_query(host_count);
  if (!query) {
    git_overleaf_error(err, "out of memory");
    goto done;
  }
  if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
    git_overleaf_error(err, "could not query Firefox cookies database: %s",
                       sqlite3_errmsg(db));
    goto done;
  }
  for (size_t i = 0; i < host_count; i++) {
    if (sqlite3_bind_text(stmt, (int)i + 1, hosts[i], -1, SQLITE_STATIC) !=
        SQLITE_OK) {
      git_overleaf_error(err, "could not bind Firefox cookie host: %s",
                         sqlite3_errmsg(db));
      goto done;
    }
  }

  for (;;) {
    int step = sqlite3_step(stmt);
    if (step == SQLITE_DONE) {
      rc = 0;
      break;
    }
    if (step != SQLITE_ROW) {
      git_overleaf_error(err, "could not read Firefox cookies: %s",
                         sqlite3_errmsg(db));
      goto done;
    }
    GitOverleafFirefoxCookieRow row = {0};
    row.name = sqlite_column_dup(stmt, 0);
    row.value = sqlite_column_dup(stmt, 1);
    row.host = sqlite_column_dup(stmt, 2);
    row.path = sqlite_column_dup(stmt, 3);
    row.expiry = sqlite3_column_type(stmt, 4) == SQLITE_INTEGER
                     ? sqlite3_column_int64(stmt, 4)
                     : 0;
    if ((sqlite3_column_text(stmt, 0) && !row.name) ||
        (sqlite3_column_text(stmt, 1) && !row.value) ||
        (sqlite3_column_text(stmt, 2) && !row.host) ||
        (sqlite3_column_text(stmt, 3) && !row.path)) {
      free(row.name);
      free(row.value);
      free(row.host);
      free(row.path);
      git_overleaf_error(err, "out of memory");
      goto done;
    }
    if (cookie_rows_append(rows, &row, err) != 0) {
      free(row.name);
      free(row.value);
      free(row.host);
      free(row.path);
      goto done;
    }
  }

done:
  if (stmt) {
    sqlite3_finalize(stmt);
  }
  if (db) {
    sqlite3_close(db);
  }
  free(query);
  return rc;
}

static int token_suffix_p(const char* text) {
  for (const char* p = text; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_') {
      return 0;
    }
  }
  return 1;
}

static int session_cookie_name_p(const char* name) {
  if (!name) {
    return 0;
  }
  /* Overleaf and ShareLaTeX deployments have used several session cookie
     names, including token-suffixed variants on some hosts. */
  const char* overleaf = "overleaf_session";
  const char* sharelatex = "sharelatex_session";
  if (starts_with(name, overleaf) && token_suffix_p(name + strlen(overleaf))) {
    return 1;
  }
  if (starts_with(name, sharelatex) &&
      token_suffix_p(name + strlen(sharelatex))) {
    return 1;
  }
  return strcmp(name, "sharelatex.sid") == 0 ||
         strcmp(name, "connect.sid") == 0 || strcmp(name, "sessionid") == 0 ||
         strcmp(name, "session") == 0;
}

static int cookie_expired_p(const GitOverleafFirefoxCookieRow* row,
                            time_t now) {
  return row->expiry > 0 && row->expiry <= (sqlite3_int64)now;
}

static void format_expiry(sqlite3_int64 expiry, char* buffer, size_t size) {
  time_t value = (time_t)expiry;
  struct tm tm_value;
  if (localtime_r(&value, &tm_value) &&
      strftime(buffer, size, "%Y-%m-%d %H:%M:%S %Z", &tm_value) > 0) {
    return;
  }
  snprintf(buffer, size, "%lld", (long long)expiry);
}

static int cookie_header_unsafe_p(const char* text) {
  return text && (strchr(text, '\r') || strchr(text, '\n'));
}

static int build_cookie_header(const GitOverleafFirefoxCookieRows* rows,
                               const char* profile, const char* url, char** out,
                               GitOverleafError* err) {
  *out = NULL;
  time_t now = time(NULL);
  size_t session_count = 0;
  size_t valid_session_count = 0;
  size_t valid_count = 0;
  size_t pair_count = 0;
  size_t header_len = 0;
  sqlite3_int64 session_expiry = 0;

  /* The first pass both validates that authentication cookies exist and
     precomputes the exact Cookie header length for a single allocation. */
  for (size_t i = 0; i < rows->len; i++) {
    const GitOverleafFirefoxCookieRow* row = &rows->items[i];
    int session = session_cookie_name_p(row->name);
    int expired = cookie_expired_p(row, now);
    if (session) {
      session_count++;
      if (row->expiry > 0 &&
          (session_expiry == 0 || row->expiry < session_expiry)) {
        session_expiry = row->expiry;
      }
      if (!expired) {
        valid_session_count++;
      }
    }
    if (!expired) {
      valid_count++;
      if (row->name && *row->name && row->value) {
        if (cookie_header_unsafe_p(row->name) ||
            cookie_header_unsafe_p(row->value)) {
          return git_overleaf_error(err,
                                    "Firefox cookie contains "
                                    "unsupported newline characters");
        }
        header_len += strlen(row->name) + 1 + strlen(row->value);
        if (pair_count > 0) {
          header_len += 2;
        }
        pair_count++;
      }
    }
  }

  if (rows->len == 0) {
    return git_overleaf_error(
        err,
        "no Overleaf cookies found in Firefox profile %s.  Log in to %s in "
        "Firefox, then run auth --from-firefox again",
        profile, url);
  }
  if (session_count == 0) {
    return git_overleaf_error(
        err,
        "found Overleaf cookies in Firefox profile %s, but no authenticated "
        "session cookie.  Log in to %s in Firefox, then run auth "
        "--from-firefox again",
        profile, url);
  }
  if (valid_session_count == 0) {
    char expiry_text[96] = "";
    if (session_expiry > 0) {
      char formatted[64];
      format_expiry(session_expiry, formatted, sizeof(formatted));
      snprintf(expiry_text, sizeof(expiry_text), " since %s", formatted);
    }
    return git_overleaf_error(
        err,
        "Firefox Overleaf session cookies in profile %s are expired%s.  Log "
        "in to %s in Firefox, then run auth --from-firefox again",
        profile, expiry_text, url);
  }
  if (valid_count == 0) {
    return git_overleaf_error(
        err,
        "Firefox Overleaf cookies in profile %s are expired.  Log in to %s "
        "in Firefox, then run auth --from-firefox again",
        profile, url);
  }
  if (pair_count == 0) {
    return git_overleaf_error(err,
                              "no usable Overleaf cookies found in Firefox "
                              "profile %s",
                              profile);
  }

  char* header = malloc(header_len + 1);
  if (!header) {
    return git_overleaf_error(err, "out of memory");
  }
  char* p = header;
  size_t written_pairs = 0;
  for (size_t i = 0; i < rows->len; i++) {
    const GitOverleafFirefoxCookieRow* row = &rows->items[i];
    if (cookie_expired_p(row, now) || !row->name || !*row->name ||
        !row->value) {
      continue;
    }
    if (written_pairs > 0) {
      memcpy(p, "; ", 2);
      p += 2;
    }
    size_t name_len = strlen(row->name);
    size_t value_len = strlen(row->value);
    memcpy(p, row->name, name_len);
    p += name_len;
    *p++ = '=';
    memcpy(p, row->value, value_len);
    p += value_len;
    written_pairs++;
  }
  *p = '\0';
  *out = header;
  return 0;
}

int git_overleaf_firefox_cookie_header(const GitOverleafConfig* cfg,
                                       const char* firefox_profile, char** out,
                                       GitOverleafError* err) {
  *out = NULL;
  char* profile = NULL;
  char* temp_dir = NULL;
  char* db_path = NULL;
  char** hosts = NULL;
  size_t host_count = 0;
  GitOverleafFirefoxCookieRows rows = {0};
  int rc = -1;

  if (firefox_profile_directory(firefox_profile, &profile, err) != 0) {
    goto done;
  }
  if (copy_cookie_store(profile, &temp_dir, &db_path, err) != 0) {
    goto done;
  }
  if (cookie_host_candidates(
          cfg && cfg->url ? cfg->url : GIT_OVERLEAF_DEFAULT_URL, &hosts,
          &host_count, err) != 0) {
    goto done;
  }
  if (load_cookie_rows(db_path, hosts, host_count, &rows, err) != 0) {
    goto done;
  }
  if (build_cookie_header(&rows, profile,
                          cfg && cfg->url ? cfg->url : GIT_OVERLEAF_DEFAULT_URL,
                          out, err) != 0) {
    goto done;
  }
  rc = 0;

done:
  cookie_rows_free(&rows);
  free_string_list(hosts, host_count);
  free(db_path);
  if (temp_dir) {
    GitOverleafError ignored = {{0}};
    git_overleaf_remove_tree(temp_dir, &ignored);
  }
  free(temp_dir);
  free(profile);
  return rc;
}

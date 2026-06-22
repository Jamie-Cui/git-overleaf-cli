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

#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git-overleaf-cli.h"

static size_t write_buffer_cb(char* ptr, size_t size, size_t nmemb,
                              void* userdata) {
  size_t total = size * nmemb;
  GitOverleafBuffer* buffer = userdata;
  char* next = realloc(buffer->data, buffer->len + total + 1);
  if (!next) {
    return 0;
  }
  buffer->data = next;
  memcpy(buffer->data + buffer->len, ptr, total);
  buffer->len += total;
  buffer->data[buffer->len] = '\0';
  return total;
}

static size_t write_file_cb(char* ptr, size_t size, size_t nmemb,
                            void* userdata) {
  return fwrite(ptr, size, nmemb, userdata);
}

static struct curl_slist* build_headers(const GitOverleafConfig* cfg,
                                        const char* referer,
                                        GitOverleafError* err) {
  struct curl_slist* headers = NULL;
  char* origin = git_overleaf_sanitize_url(cfg->url);
  if (!origin) {
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  size_t cookie_len =
      strlen("Cookie: ") + strlen(cfg->cookie ? cfg->cookie : "") + 1;
  size_t origin_len = strlen("Origin: ") + strlen(origin) + 1;
  size_t referer_len =
      strlen("Referer: ") + strlen(referer ? referer : origin) + 1;
  char* cookie_header = malloc(cookie_len);
  char* origin_header = malloc(origin_len);
  char* referer_header = malloc(referer_len);
  if (!cookie_header || !origin_header || !referer_header) {
    free(origin);
    free(cookie_header);
    free(origin_header);
    free(referer_header);
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  snprintf(cookie_header, cookie_len, "Cookie: %s",
           cfg->cookie ? cfg->cookie : "");
  snprintf(origin_header, origin_len, "Origin: %s", origin);
  snprintf(referer_header, referer_len, "Referer: %s",
           referer ? referer : origin);
  /* Overleaf serves some endpoints behind browser-oriented checks, so the
     CLI sends the same cookie, origin, and referer shape as the web app. */
  headers = curl_slist_append(headers, cookie_header);
  headers = curl_slist_append(headers, origin_header);
  headers = curl_slist_append(headers, referer_header);
  headers =
      curl_slist_append(headers, "Accept: text/html,application/json,*/*");
  free(origin);
  free(cookie_header);
  free(origin_header);
  free(referer_header);
  return headers;
}

static int configure_common(CURL* curl, const char* url,
                            struct curl_slist* headers, char* error_buffer) {
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "git-overleaf-cli/0.1");
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
  return 0;
}

int git_overleaf_http_get(const GitOverleafConfig* cfg, const char* url,
                          const char* referer, GitOverleafBuffer* out,
                          GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  CURL* curl = curl_easy_init();
  if (!curl) {
    return git_overleaf_error(err, "could not initialize curl");
  }
  struct curl_slist* headers = build_headers(cfg, referer, err);
  if (!headers) {
    curl_easy_cleanup(curl);
    return -1;
  }
  char error_buffer[CURL_ERROR_SIZE] = {0};
  configure_common(curl, url, headers, error_buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_buffer_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (code != CURLE_OK) {
    git_overleaf_buffer_free(out);
    return git_overleaf_error(
        err, "GET %s failed: HTTP %ld: %s", url, status,
        error_buffer[0] ? error_buffer : curl_easy_strerror(code));
  }
  if (!out->data) {
    out->data = git_overleaf_xstrdup("");
  }
  return out->data ? 0 : git_overleaf_error(err, "out of memory");
}

int git_overleaf_http_download(const GitOverleafConfig* cfg, const char* url,
                               const char* referer, const char* output_file,
                               GitOverleafError* err) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return git_overleaf_error(err, "could not initialize curl");
  }
  FILE* file = fopen(output_file, "wb");
  if (!file) {
    curl_easy_cleanup(curl);
    return git_overleaf_error(err, "could not open %s: %s", output_file,
                              strerror(errno));
  }
  struct curl_slist* headers = build_headers(cfg, referer, err);
  if (!headers) {
    fclose(file);
    curl_easy_cleanup(curl);
    return -1;
  }
  char error_buffer[CURL_ERROR_SIZE] = {0};
  configure_common(curl, url, headers, error_buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
  /* Large project snapshots should not fail because of the normal request
     timeout; LOW_SPEED_* still catches stalled transfers. */
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
  CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  int close_status = fclose(file);
  if (code != CURLE_OK || close_status != 0) {
    return git_overleaf_error(
        err, "download %s failed: HTTP %ld: %s", url, status,
        error_buffer[0] ? error_buffer : curl_easy_strerror(code));
  }
  return 0;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  }
  return -1;
}

static char* html_decode(const char* input) {
  size_t len = strlen(input);
  char* out = malloc(len + 1);
  if (!out) {
    return NULL;
  }
  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (input[i] == '&') {
      if (strncmp(input + i, "&amp;", 5) == 0) {
        out[j++] = '&';
        i += 4;
      } else if (strncmp(input + i, "&quot;", 6) == 0) {
        out[j++] = '"';
        i += 5;
      } else if (strncmp(input + i, "&#39;", 5) == 0) {
        out[j++] = '\'';
        i += 4;
      } else if (strncmp(input + i, "&#34;", 5) == 0) {
        out[j++] = '"';
        i += 4;
      } else {
        out[j++] = input[i];
      }
    } else {
      out[j++] = input[i];
    }
  }
  out[j] = '\0';
  return out;
}

static char* percent_decode(const char* input) {
  size_t len = strlen(input);
  char* out = malloc(len + 1);
  if (!out) {
    return NULL;
  }
  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (input[i] == '%' && i + 2 < len) {
      int hi = hex_value(input[i + 1]);
      int lo = hex_value(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out[j++] = (char)((hi << 4) | lo);
        i += 2;
      } else {
        out[j++] = input[i];
      }
    } else {
      out[j++] = input[i];
    }
  }
  out[j] = '\0';
  return out;
}

static char* extract_attr(const char* tag_start, const char* tag_end,
                          const char* name) {
  size_t name_len = strlen(name);
  const char* p = tag_start;
  while (p && p < tag_end) {
    /* This is intentionally a tiny quoted-attribute scanner for one trusted
       meta tag, not a general HTML parser. */
    p = strstr(p, name);
    if (!p || p >= tag_end) {
      break;
    }
    const char* q = p + name_len;
    while (q < tag_end &&
           (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
      q++;
    }
    if (q >= tag_end || *q != '=') {
      p = q;
      continue;
    }
    q++;
    while (q < tag_end &&
           (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
      q++;
    }
    if (q >= tag_end || (*q != '"' && *q != '\'')) {
      p = q;
      continue;
    }
    char quote = *q++;
    const char* value_start = q;
    while (q < tag_end && *q != quote) {
      q++;
    }
    if (q < tag_end) {
      return git_overleaf_xstrndup(value_start, (size_t)(q - value_start));
    }
    p = q;
  }
  return NULL;
}

static int looks_like_login_page(const char* html) {
  return strstr(html, "Log in to Overleaf") || strstr(html, "/login") ||
         strstr(html, "name=\"email\"") || strstr(html, "name=\"password\"");
}

static char* extract_projects_blob(const char* html, GitOverleafError* err) {
  const char* marker = strstr(html, "ol-prefetchedProjectsBlob");
  if (!marker) {
    if (looks_like_login_page(html)) {
      git_overleaf_error(
          err,
          "Overleaf returned the login page instead of the project list; "
          "your cookies are missing or expired. Run `git-overleaf-cli auth "
          "--from-firefox' after logging in to the same Overleaf host in "
          "Firefox");
      return NULL;
    }
    git_overleaf_error(err,
                       "could not find project list in Overleaf project page; "
                       "the page layout may have changed or authentication "
                       "failed");
    return NULL;
  }
  const char* tag_start = marker;
  while (tag_start > html && *tag_start != '<') {
    tag_start--;
  }
  const char* tag_end = strchr(marker, '>');
  if (!tag_end) {
    git_overleaf_error(err, "could not parse project list meta tag");
    return NULL;
  }
  char* content = extract_attr(tag_start, tag_end, "content");
  if (!content) {
    git_overleaf_error(err, "could not find project list content attribute");
    return NULL;
  }
  /* The prefetched projects JSON is stored in an HTML attribute and then
     percent-encoded, so both layers must be removed before JSON parsing. */
  char* html_decoded = html_decode(content);
  char* decoded = html_decoded ? percent_decode(html_decoded) : NULL;
  free(content);
  free(html_decoded);
  if (!decoded) {
    git_overleaf_error(err, "out of memory");
  }
  return decoded;
}

static int parse_projects_json(const char* json_text,
                               GitOverleafProjectList* out,
                               GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  json_error_t json_err;
  json_t* root = json_loads(json_text, 0, &json_err);
  if (!root) {
    return git_overleaf_error(err, "could not parse project list JSON: %s",
                              json_err.text);
  }
  json_t* projects = json_object_get(root, "projects");
  if (!json_is_array(projects)) {
    json_decref(root);
    return git_overleaf_error(
        err, "project list JSON does not contain a projects array");
  }
  size_t count = json_array_size(projects);
  out->items = calloc(count, sizeof(GitOverleafProject));
  if (!out->items && count > 0) {
    json_decref(root);
    return git_overleaf_error(err, "out of memory");
  }
  out->len = count;
  for (size_t i = 0; i < count; i++) {
    json_t* project = json_array_get(projects, i);
    json_t* id = json_object_get(project, "id");
    json_t* name = json_object_get(project, "name");
    json_t* owner = json_object_get(project, "owner");
    json_t* email = owner ? json_object_get(owner, "email") : NULL;
    out->items[i].id =
        git_overleaf_xstrdup(json_is_string(id) ? json_string_value(id) : "");
    out->items[i].name = git_overleaf_xstrdup(
        json_is_string(name) ? json_string_value(name) : "");
    out->items[i].owner_email = git_overleaf_xstrdup(
        json_is_string(email) ? json_string_value(email) : "");
    if (!out->items[i].id || !out->items[i].name ||
        !out->items[i].owner_email) {
      json_decref(root);
      git_overleaf_project_list_free(out);
      return git_overleaf_error(err, "out of memory");
    }
  }
  json_decref(root);
  return 0;
}

int git_overleaf_overleaf_parse_projects_page(const char* html,
                                              GitOverleafProjectList* out,
                                              GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  if (!html) {
    return git_overleaf_error(err, "empty Overleaf project page");
  }
  char* json_text = extract_projects_blob(html, err);
  if (!json_text) {
    return -1;
  }
  int rc = parse_projects_json(json_text, out, err);
  free(json_text);
  return rc;
}

int git_overleaf_overleaf_list_projects(const GitOverleafConfig* cfg, GitOverleafProjectList* out,
                                        GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  char* url = git_overleaf_url_join(cfg->url, "project");
  if (!url) {
    return git_overleaf_error(err, "out of memory");
  }
  GitOverleafBuffer page = {0};
  if (git_overleaf_http_get(cfg, url, cfg->url, &page, err) != 0) {
    free(url);
    return -1;
  }
  free(url);
  int rc = git_overleaf_overleaf_parse_projects_page(page.data, out, err);
  git_overleaf_buffer_free(&page);
  return rc;
}

void git_overleaf_project_list_free(GitOverleafProjectList* list) {
  if (!list) {
    return;
  }
  for (size_t i = 0; i < list->len; i++) {
    free(list->items[i].id);
    free(list->items[i].name);
    free(list->items[i].owner_email);
  }
  free(list->items);
  list->items = NULL;
  list->len = 0;
}

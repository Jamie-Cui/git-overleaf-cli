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
#include <sys/select.h>
#include <time.h>

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

static struct curl_slist* append_extra_headers(struct curl_slist* headers,
                                               struct curl_slist* extra) {
  for (struct curl_slist* item = extra; item; item = item->next) {
    headers = curl_slist_append(headers, item->data);
  }
  return headers;
}

static struct curl_slist* build_headers(const GitOverleafConfig* cfg,
                                        const char* referer,
                                        struct curl_slist* extra_headers,
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
  headers = append_extra_headers(headers, extra_headers);
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
  return git_overleaf_http_request(cfg, "GET", url, referer, NULL, NULL, out,
                                   err);
}

int git_overleaf_http_request(const GitOverleafConfig* cfg, const char* method,
                              const char* url, const char* referer,
                              struct curl_slist* extra_headers,
                              const char* body, GitOverleafBuffer* out,
                              GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  CURL* curl = curl_easy_init();
  if (!curl) {
    return git_overleaf_error(err, "could not initialize curl");
  }
  struct curl_slist* headers = build_headers(cfg, referer, extra_headers, err);
  if (!headers) {
    curl_easy_cleanup(curl);
    return -1;
  }
  char error_buffer[CURL_ERROR_SIZE] = {0};
  configure_common(curl, url, headers, error_buffer);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method ? method : "GET");
  if (body) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
  }
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
  struct curl_slist* headers = build_headers(cfg, referer, NULL, err);
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

static char* project_page_url(const GitOverleafConfig* cfg,
                              const char* project_id) {
  size_t len = strlen("project//") + strlen(project_id) + 1;
  char* path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "project/%s", project_id);
  char* url = git_overleaf_url_join(cfg->url, path);
  free(path);
  return url;
}

static char* project_api_url(const GitOverleafConfig* cfg,
                             const char* project_id, const char* suffix) {
  size_t len = strlen("project//") + strlen(project_id) + strlen(suffix) + 1;
  char* path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "project/%s%s", project_id, suffix);
  char* url = git_overleaf_url_join(cfg->url, path);
  free(path);
  return url;
}

static char* csrf_cache_key(const GitOverleafConfig* cfg,
                            const char* project_id) {
  const char* cookie = cfg->cookie ? cfg->cookie : "";
  size_t len = strlen(cfg->url) + strlen(project_id) + strlen(cookie) + 3;
  char* key = malloc(len);
  if (!key) {
    return NULL;
  }
  snprintf(key, len, "%s|%s|%s", cfg->url, project_id, cookie);
  return key;
}

static int overleaf_csrf_token(const GitOverleafConfig* cfg,
                               const char* project_id, char** out,
                               GitOverleafError* err) {
  static char* cached_key = NULL;
  static char* cached_token = NULL;
  *out = NULL;
  char* key = csrf_cache_key(cfg, project_id);
  if (!key) {
    return git_overleaf_error(err, "out of memory");
  }
  if (cached_key && cached_token && strcmp(cached_key, key) == 0) {
    *out = git_overleaf_xstrdup(cached_token);
    free(key);
    return *out ? 0 : git_overleaf_error(err, "out of memory");
  }
  char* url = project_page_url(cfg, project_id);
  if (!url) {
    free(key);
    return git_overleaf_error(err, "out of memory");
  }
  GitOverleafBuffer page = {0};
  if (git_overleaf_http_get(cfg, url, url, &page, err) != 0) {
    free(key);
    free(url);
    return -1;
  }
  const char* marker = strstr(page.data ? page.data : "", "ol-csrfToken");
  if (!marker) {
    git_overleaf_buffer_free(&page);
    free(key);
    free(url);
    return git_overleaf_error(
        err, "could not extract csrf token for project %s", project_id);
  }
  const char* tag_start = marker;
  while (tag_start > page.data && *tag_start != '<') {
    tag_start--;
  }
  const char* tag_end = strchr(marker, '>');
  if (!tag_end) {
    git_overleaf_buffer_free(&page);
    free(key);
    free(url);
    return git_overleaf_error(err, "could not parse csrf token meta tag");
  }
  char* content = extract_attr(tag_start, tag_end, "content");
  char* decoded = content ? html_decode(content) : NULL;
  free(content);
  git_overleaf_buffer_free(&page);
  free(url);
  if (!decoded || !*decoded) {
    free(decoded);
    free(key);
    return git_overleaf_error(err, "could not find csrf token content");
  }
  free(cached_key);
  free(cached_token);
  cached_key = git_overleaf_xstrdup(key);
  cached_token = git_overleaf_xstrdup(decoded);
  free(key);
  if (!cached_key || !cached_token) {
    free(decoded);
    return git_overleaf_error(err, "out of memory");
  }
  *out = decoded;
  return 0;
}

static struct curl_slist* mutation_headers(const GitOverleafConfig* cfg,
                                           const char* project_id,
                                           const char* content_type,
                                           GitOverleafError* err) {
  char* token = NULL;
  if (overleaf_csrf_token(cfg, project_id, &token, err) != 0) {
    return NULL;
  }
  struct curl_slist* headers = NULL;
  size_t token_len = strlen("x-csrf-token: ") + strlen(token) + 1;
  char* token_header = malloc(token_len);
  if (!token_header) {
    free(token);
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  snprintf(token_header, token_len, "x-csrf-token: %s", token);
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Cache-Control: no-cache");
  headers = curl_slist_append(headers, token_header);
  if (content_type) {
    size_t type_len = strlen("Content-Type: ") + strlen(content_type) + 1;
    char* type_header = malloc(type_len);
    if (!type_header) {
      curl_slist_free_all(headers);
      free(token_header);
      free(token);
      git_overleaf_error(err, "out of memory");
      return NULL;
    }
    snprintf(type_header, type_len, "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, type_header);
    free(type_header);
  }
  free(token_header);
  free(token);
  return headers;
}

static int parse_json_response(const GitOverleafBuffer* buffer, json_t** out,
                               GitOverleafError* err) {
  *out = NULL;
  json_error_t json_err;
  json_t* root = json_loads(buffer->data ? buffer->data : "", 0, &json_err);
  if (!root) {
    return git_overleaf_error(err, "could not parse Overleaf JSON response: %s",
                              json_err.text);
  }
  *out = root;
  return 0;
}

int git_overleaf_overleaf_create_folder(const GitOverleafConfig* cfg,
                                        const char* project_id,
                                        const char* parent_id, const char* name,
                                        char** id_out, GitOverleafError* err) {
  *id_out = NULL;
  char* url = project_api_url(cfg, project_id, "/folder");
  char* referer = project_page_url(cfg, project_id);
  struct curl_slist* headers =
      mutation_headers(cfg, project_id, "application/json", err);
  if (!url || !referer || !headers) {
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return url && referer ? -1 : git_overleaf_error(err, "out of memory");
  }
  json_t* body_json =
      json_pack("{s:s,s:s}", "parent_folder_id", parent_id ? parent_id : "",
                "name", name ? name : "");
  char* body = body_json ? json_dumps(body_json, JSON_COMPACT) : NULL;
  json_decref(body_json);
  if (!body) {
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return git_overleaf_error(err, "out of memory");
  }
  GitOverleafBuffer response = {0};
  int rc = git_overleaf_http_request(cfg, "POST", url, referer, headers, body,
                                     &response, err);
  free(body);
  free(url);
  free(referer);
  curl_slist_free_all(headers);
  if (rc != 0) {
    return -1;
  }
  json_t* parsed = NULL;
  if (parse_json_response(&response, &parsed, err) != 0) {
    git_overleaf_buffer_free(&response);
    return -1;
  }
  json_t* id = json_object_get(parsed, "_id");
  if (!json_is_string(id)) {
    json_decref(parsed);
    git_overleaf_buffer_free(&response);
    return git_overleaf_error(err,
                              "Overleaf create-folder response missing _id");
  }
  *id_out = git_overleaf_xstrdup(json_string_value(id));
  json_decref(parsed);
  git_overleaf_buffer_free(&response);
  return *id_out ? 0 : git_overleaf_error(err, "out of memory");
}

int git_overleaf_overleaf_delete_entity(const GitOverleafConfig* cfg,
                                        const char* project_id,
                                        const GitOverleafRemoteEntity* entity,
                                        GitOverleafError* err) {
  const char* type = NULL;
  switch (entity->type) {
    case GIT_OVERLEAF_REMOTE_FOLDER:
      type = "folder";
      break;
    case GIT_OVERLEAF_REMOTE_DOC:
      type = "doc";
      break;
    case GIT_OVERLEAF_REMOTE_FILE:
      type = "file";
      break;
  }
  size_t suffix_len =
      strlen("/") + strlen(type) + strlen("/") + strlen(entity->id) + 1;
  char* suffix = malloc(suffix_len);
  if (!suffix) {
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(suffix, suffix_len, "/%s/%s", type, entity->id);
  char* url = project_api_url(cfg, project_id, suffix);
  char* referer = project_page_url(cfg, project_id);
  struct curl_slist* headers =
      mutation_headers(cfg, project_id, "application/json", err);
  free(suffix);
  if (!url || !referer || !headers) {
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return url && referer ? -1 : git_overleaf_error(err, "out of memory");
  }
  GitOverleafBuffer response = {0};
  int rc = git_overleaf_http_request(cfg, "DELETE", url, referer, headers, "{}",
                                     &response, err);
  git_overleaf_buffer_free(&response);
  free(url);
  free(referer);
  curl_slist_free_all(headers);
  return rc;
}

int git_overleaf_overleaf_upload_file(
    const GitOverleafConfig* cfg, const char* project_id, const char* folder_id,
    const char* file_name, const char* file_path, char** id_out,
    GitOverleafRemoteEntityType* type_out, GitOverleafError* err) {
  *id_out = NULL;
  *type_out = GIT_OVERLEAF_REMOTE_FILE;
  size_t suffix_len =
      strlen("/upload?folder_id=") + strlen(folder_id ? folder_id : "") + 1;
  char* suffix = malloc(suffix_len);
  if (!suffix) {
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(suffix, suffix_len, "/upload?folder_id=%s",
           folder_id ? folder_id : "");
  char* url = project_api_url(cfg, project_id, suffix);
  char* referer = project_page_url(cfg, project_id);
  struct curl_slist* headers = mutation_headers(cfg, project_id, NULL, err);
  free(suffix);
  if (!url || !referer || !headers) {
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return url && referer ? -1 : git_overleaf_error(err, "out of memory");
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return git_overleaf_error(err, "could not initialize curl");
  }
  struct curl_slist* base_headers = build_headers(cfg, referer, headers, err);
  if (!base_headers) {
    curl_easy_cleanup(curl);
    free(url);
    free(referer);
    curl_slist_free_all(headers);
    return -1;
  }

  curl_mime* mime = curl_mime_init(curl);
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "relativePath");
  curl_mime_data(part, "null", CURL_ZERO_TERMINATED);
  part = curl_mime_addpart(mime);
  curl_mime_name(part, "name");
  curl_mime_data(part, file_name, CURL_ZERO_TERMINATED);
  part = curl_mime_addpart(mime);
  curl_mime_name(part, "type");
  curl_mime_data(part, "application/octet-stream", CURL_ZERO_TERMINATED);
  part = curl_mime_addpart(mime);
  curl_mime_name(part, "qqfile");
  curl_mime_filedata(part, file_path);
  curl_mime_type(part, "application/octet-stream");

  GitOverleafBuffer response = {0};
  char error_buffer[CURL_ERROR_SIZE] = {0};
  configure_common(curl, url, base_headers, error_buffer);
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_buffer_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_mime_free(mime);
  curl_slist_free_all(base_headers);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  free(url);
  free(referer);
  if (code != CURLE_OK) {
    git_overleaf_buffer_free(&response);
    return git_overleaf_error(
        err, "upload %s failed: HTTP %ld: %s", file_path, status,
        error_buffer[0] ? error_buffer : curl_easy_strerror(code));
  }

  json_t* parsed = NULL;
  if (parse_json_response(&response, &parsed, err) != 0) {
    git_overleaf_buffer_free(&response);
    return -1;
  }
  json_t* id = json_object_get(parsed, "entity_id");
  json_t* entity_type = json_object_get(parsed, "entity_type");
  if (!json_is_string(id)) {
    json_decref(parsed);
    git_overleaf_buffer_free(&response);
    return git_overleaf_error(err,
                              "Overleaf upload response missing entity_id");
  }
  *id_out = git_overleaf_xstrdup(json_string_value(id));
  if (json_is_string(entity_type) &&
      strcmp(json_string_value(entity_type), "doc") == 0) {
    *type_out = GIT_OVERLEAF_REMOTE_DOC;
  }
  json_decref(parsed);
  git_overleaf_buffer_free(&response);
  return *id_out ? 0 : git_overleaf_error(err, "out of memory");
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

int git_overleaf_overleaf_list_projects(const GitOverleafConfig* cfg,
                                        GitOverleafProjectList* out,
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

static void remote_entity_free(GitOverleafRemoteEntity* entity) {
  if (!entity) {
    return;
  }
  free(entity->path);
  free(entity->name);
  free(entity->id);
  free(entity->parent_id);
  memset(entity, 0, sizeof(*entity));
}

void git_overleaf_remote_table_free(GitOverleafRemoteTable* table) {
  if (!table) {
    return;
  }
  for (size_t i = 0; i < table->len; i++) {
    remote_entity_free(&table->items[i]);
  }
  free(table->items);
  memset(table, 0, sizeof(*table));
}

GitOverleafRemoteEntity* git_overleaf_remote_table_find(
    GitOverleafRemoteTable* table, const char* path) {
  if (!table || !path) {
    return NULL;
  }
  for (size_t i = 0; i < table->len; i++) {
    if (strcmp(table->items[i].path, path) == 0) {
      return &table->items[i];
    }
  }
  return NULL;
}

int git_overleaf_remote_table_upsert(GitOverleafRemoteTable* table,
                                     const char* path, const char* name,
                                     const char* id,
                                     GitOverleafRemoteEntityType type,
                                     const char* parent_id,
                                     GitOverleafError* err) {
  GitOverleafRemoteEntity* existing =
      git_overleaf_remote_table_find(table, path ? path : "");
  if (existing) {
    remote_entity_free(existing);
  } else {
    if (table->len == table->cap) {
      size_t next_cap = table->cap ? table->cap * 2 : 16;
      GitOverleafRemoteEntity* next =
          realloc(table->items, next_cap * sizeof(GitOverleafRemoteEntity));
      if (!next) {
        return git_overleaf_error(err, "out of memory");
      }
      table->items = next;
      memset(table->items + table->cap, 0,
             (next_cap - table->cap) * sizeof(GitOverleafRemoteEntity));
      table->cap = next_cap;
    }
    existing = &table->items[table->len++];
  }
  existing->path = git_overleaf_xstrdup(path ? path : "");
  existing->name = git_overleaf_xstrdup(name ? name : "");
  existing->id = git_overleaf_xstrdup(id ? id : "");
  existing->parent_id = git_overleaf_xstrdup(parent_id ? parent_id : "");
  existing->type = type;
  if (!existing->path || !existing->name || !existing->id ||
      !existing->parent_id) {
    return git_overleaf_error(err, "out of memory");
  }
  return 0;
}

void git_overleaf_remote_table_remove(GitOverleafRemoteTable* table,
                                      const char* path) {
  if (!table || !path) {
    return;
  }
  for (size_t i = 0; i < table->len; i++) {
    if (strcmp(table->items[i].path, path) == 0) {
      remote_entity_free(&table->items[i]);
      if (i + 1 < table->len) {
        memmove(&table->items[i], &table->items[i + 1],
                (table->len - i - 1) * sizeof(GitOverleafRemoteEntity));
      }
      table->len--;
      memset(&table->items[table->len], 0, sizeof(GitOverleafRemoteEntity));
      return;
    }
  }
}

void git_overleaf_remote_table_forget_prefix(GitOverleafRemoteTable* table,
                                             const char* path) {
  if (!table || !path) {
    return;
  }
  size_t path_len = strlen(path);
  for (size_t i = 0; i < table->len;) {
    int match =
        strcmp(table->items[i].path, path) == 0 ||
        (path_len > 0 && strncmp(table->items[i].path, path, path_len) == 0 &&
         table->items[i].path[path_len] == '/');
    if (match) {
      git_overleaf_remote_table_remove(table, table->items[i].path);
    } else {
      i++;
    }
  }
}

static const char* json_string_value_or_empty(json_t* object, const char* key) {
  json_t* value = json_object_get(object, key);
  return json_is_string(value) ? json_string_value(value) : "";
}

static char* remote_child_path(const char* parent, const char* name) {
  if (!parent || !*parent) {
    return git_overleaf_xstrdup(name ? name : "");
  }
  size_t len = strlen(parent) + 1 + strlen(name ? name : "") + 1;
  char* path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "%s/%s", parent, name ? name : "");
  return path;
}

static int parse_remote_folder(json_t* folder, const char* parent_path,
                               const char* parent_id,
                               GitOverleafRemoteTable* table,
                               GitOverleafError* err) {
  const char* name = json_string_value_or_empty(folder, "name");
  const char* id = json_string_value_or_empty(folder, "_id");
  char* path = NULL;
  if (strcmp(name, "rootFolder") == 0) {
    path = git_overleaf_xstrdup("");
  } else {
    path = remote_child_path(parent_path, name);
  }
  if (!path) {
    return git_overleaf_error(err, "out of memory");
  }
  if (git_overleaf_remote_table_upsert(table, path, name, id,
                                       GIT_OVERLEAF_REMOTE_FOLDER, parent_id,
                                       err) != 0) {
    free(path);
    return -1;
  }

  json_t* docs = json_object_get(folder, "docs");
  if (json_is_array(docs)) {
    size_t index;
    json_t* doc;
    json_array_foreach(docs, index, doc) {
      const char* doc_name = json_string_value_or_empty(doc, "name");
      const char* doc_id = json_string_value_or_empty(doc, "_id");
      char* doc_path = remote_child_path(path, doc_name);
      if (!doc_path) {
        free(path);
        return git_overleaf_error(err, "out of memory");
      }
      int rc = git_overleaf_remote_table_upsert(
          table, doc_path, doc_name, doc_id, GIT_OVERLEAF_REMOTE_DOC, id, err);
      free(doc_path);
      if (rc != 0) {
        free(path);
        return -1;
      }
    }
  }

  json_t* files = json_object_get(folder, "fileRefs");
  if (json_is_array(files)) {
    size_t index;
    json_t* file;
    json_array_foreach(files, index, file) {
      const char* file_name = json_string_value_or_empty(file, "name");
      const char* file_id = json_string_value_or_empty(file, "_id");
      char* file_path = remote_child_path(path, file_name);
      if (!file_path) {
        free(path);
        return git_overleaf_error(err, "out of memory");
      }
      int rc =
          git_overleaf_remote_table_upsert(table, file_path, file_name, file_id,
                                           GIT_OVERLEAF_REMOTE_FILE, id, err);
      free(file_path);
      if (rc != 0) {
        free(path);
        return -1;
      }
    }
  }

  json_t* folders = json_object_get(folder, "folders");
  if (json_is_array(folders)) {
    size_t index;
    json_t* child;
    json_array_foreach(folders, index, child) {
      if (parse_remote_folder(child, path, id, table, err) != 0) {
        free(path);
        return -1;
      }
    }
  }
  free(path);
  return 0;
}

int git_overleaf_overleaf_parse_remote_table(json_t* root_folder,
                                             GitOverleafRemoteTable* out,
                                             GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  if (!json_is_object(root_folder)) {
    return git_overleaf_error(err, "remote project tree root is not an object");
  }
  if (parse_remote_folder(root_folder, "", "", out, err) != 0) {
    git_overleaf_remote_table_free(out);
    return -1;
  }
  if (!git_overleaf_remote_table_find(out, "")) {
    git_overleaf_remote_table_free(out);
    return git_overleaf_error(err, "remote project tree has no root folder");
  }
  return 0;
}

typedef struct {
  CURL* curl;
  struct curl_slist* headers;
  json_t** events;
  size_t events_len;
  size_t events_cap;
  int next_ack_id;
  char failure[512];
} GitOverleafSocketClient;

static double monotonic_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int ws_wait_socket(CURL* curl, int writeable, double deadline,
                          GitOverleafError* err) {
  curl_socket_t socket_fd = CURL_SOCKET_BAD;
  curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket_fd);
  if (socket_fd == CURL_SOCKET_BAD) {
    return git_overleaf_error(err, "websocket has no active socket");
  }
  while (monotonic_seconds() < deadline) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket_fd, &fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    int rc = select((int)socket_fd + 1, writeable ? NULL : &fds,
                    writeable ? &fds : NULL, NULL, &tv);
    if (rc > 0) {
      return 0;
    }
    if (rc < 0 && errno != EINTR) {
      return git_overleaf_error(err, "websocket select failed: %s",
                                strerror(errno));
    }
  }
  return git_overleaf_error(err, "timed out waiting for websocket");
}

static int socket_send_text(GitOverleafSocketClient* client, const char* text,
                            double deadline, GitOverleafError* err) {
  size_t len = strlen(text);
  size_t offset = 0;
  while (offset < len) {
    size_t sent = 0;
    CURLcode code =
        curl_ws_send(client->curl, text + offset, len - offset, &sent,
                     (curl_off_t)(len - offset), CURLWS_TEXT);
    if (code == CURLE_AGAIN) {
      if (ws_wait_socket(client->curl, 1, deadline, err) != 0) {
        return -1;
      }
      continue;
    }
    if (code != CURLE_OK) {
      return git_overleaf_error(err, "websocket send failed: %s",
                                curl_easy_strerror(code));
    }
    offset += sent;
  }
  return 0;
}

static int socket_recv_text(GitOverleafSocketClient* client, char** out,
                            double deadline, GitOverleafError* err) {
  *out = NULL;
  GitOverleafBuffer buffer = {0};
  for (;;) {
    char chunk[4096];
    size_t received = 0;
    const struct curl_ws_frame* meta = NULL;
    CURLcode code =
        curl_ws_recv(client->curl, chunk, sizeof(chunk), &received, &meta);
    if (code == CURLE_AGAIN) {
      if (ws_wait_socket(client->curl, 0, deadline, err) != 0) {
        git_overleaf_buffer_free(&buffer);
        return -1;
      }
      continue;
    }
    if (code != CURLE_OK) {
      git_overleaf_buffer_free(&buffer);
      return git_overleaf_error(err, "websocket receive failed: %s",
                                curl_easy_strerror(code));
    }
    if (meta && (meta->flags & CURLWS_CLOSE)) {
      git_overleaf_buffer_free(&buffer);
      return git_overleaf_error(err, "websocket closed by Overleaf");
    }
    if (received > 0) {
      char* next = realloc(buffer.data, buffer.len + received + 1);
      if (!next) {
        git_overleaf_buffer_free(&buffer);
        return git_overleaf_error(err, "out of memory");
      }
      buffer.data = next;
      memcpy(buffer.data + buffer.len, chunk, received);
      buffer.len += received;
      buffer.data[buffer.len] = '\0';
    }
    if (!meta || meta->bytesleft == 0) {
      break;
    }
  }
  if (!buffer.data) {
    buffer.data = git_overleaf_xstrdup("");
    if (!buffer.data) {
      return git_overleaf_error(err, "out of memory");
    }
  }
  *out = buffer.data;
  return 0;
}

static const char* socketio_payload_after_third_colon(const char* text) {
  const char* p = text;
  for (int i = 0; i < 3; i++) {
    p = strchr(p, ':');
    if (!p) {
      return NULL;
    }
    p++;
  }
  return p;
}

static int socket_queue_event(GitOverleafSocketClient* client, json_t* event,
                              GitOverleafError* err) {
  if (client->events_len == client->events_cap) {
    size_t next_cap = client->events_cap ? client->events_cap * 2 : 8;
    json_t** next = realloc(client->events, next_cap * sizeof(json_t*));
    if (!next) {
      return git_overleaf_error(err, "out of memory");
    }
    client->events = next;
    client->events_cap = next_cap;
  }
  client->events[client->events_len++] = event;
  return 0;
}

static int socket_event_name_matches(json_t* event, const char* name) {
  json_t* event_name = json_object_get(event, "name");
  return json_is_string(event_name) &&
         strcmp(json_string_value(event_name), name) == 0;
}

static int socket_doc_update_event_matches(json_t* event, const char* doc_id) {
  json_t* event_name = json_object_get(event, "name");
  if (!json_is_string(event_name)) {
    return 0;
  }
  const char* name = json_string_value(event_name);
  json_t* args = json_object_get(event, "args");
  json_t* doc = NULL;
  if (strcmp(name, "otUpdateApplied") == 0) {
    json_t* update = json_is_array(args) ? json_array_get(args, 0) : NULL;
    doc = json_is_object(update) ? json_object_get(update, "doc") : NULL;
  } else if (strcmp(name, "otUpdateError") == 0) {
    json_t* message = json_is_array(args) ? json_array_get(args, 1) : NULL;
    doc = json_is_object(message) ? json_object_get(message, "doc_id") : NULL;
  } else {
    return 0;
  }
  return json_is_string(doc) && strcmp(json_string_value(doc), doc_id) == 0;
}

static json_t* socket_take_event(GitOverleafSocketClient* client,
                                 const char* name, const char* doc_id,
                                 int update_event) {
  for (size_t i = 0; i < client->events_len; i++) {
    json_t* event = client->events[i];
    int match = name ? socket_event_name_matches(event, name) : 1;
    if (match && update_event && doc_id) {
      match = socket_doc_update_event_matches(event, doc_id);
    }
    if (match) {
      if (i + 1 < client->events_len) {
        memmove(&client->events[i], &client->events[i + 1],
                (client->events_len - i - 1) * sizeof(json_t*));
      }
      client->events_len--;
      return event;
    }
  }
  return NULL;
}

static int socket_handle_event_text(GitOverleafSocketClient* client,
                                    const char* text, json_t** event_out,
                                    GitOverleafError* err) {
  *event_out = NULL;
  const char* payload = socketio_payload_after_third_colon(text);
  if (!payload) {
    return git_overleaf_error(err, "unsupported websocket event frame: %s",
                              text);
  }
  json_error_t json_err;
  json_t* event = json_loads(payload, 0, &json_err);
  if (!event) {
    return git_overleaf_error(err, "could not parse websocket event: %s",
                              json_err.text);
  }
  if (socket_event_name_matches(event, "connectionRejected")) {
    snprintf(client->failure, sizeof(client->failure),
             "Overleaf rejected websocket connection");
  }
  *event_out = event;
  return 0;
}

static int socket_wait_next_event(GitOverleafSocketClient* client,
                                  const char* name, const char* doc_id,
                                  int update_event, json_t** out,
                                  GitOverleafError* err);

static int socket_handle_non_ack_frame(GitOverleafSocketClient* client,
                                       const char* text, double deadline,
                                       GitOverleafError* err) {
  if (strncmp(text, "2::", 3) == 0) {
    return socket_send_text(client, "2::", deadline, err);
  }
  if (strncmp(text, "7:", 2) == 0) {
    snprintf(client->failure, sizeof(client->failure),
             "Unauthorized websocket response");
    return git_overleaf_error(err, "%s", client->failure);
  }
  if (strncmp(text, "5:", 2) == 0) {
    json_t* event = NULL;
    if (socket_handle_event_text(client, text, &event, err) != 0) {
      return -1;
    }
    if (client->failure[0]) {
      json_decref(event);
      return git_overleaf_error(err, "%s", client->failure);
    }
    return socket_queue_event(client, event, err);
  }
  return 0;
}

static int socket_ack_id_matches(const char* text, const char* ack_id,
                                 const char** payload_out) {
  *payload_out = NULL;
  const char* p = socketio_payload_after_third_colon(text);
  if (!p) {
    return 0;
  }
  const char* id_start = p;
  while (*p >= '0' && *p <= '9') {
    p++;
  }
  if (p == id_start) {
    return 0;
  }
  size_t id_len = (size_t)(p - id_start);
  if (strlen(ack_id) != id_len || strncmp(id_start, ack_id, id_len) != 0) {
    return 0;
  }
  if (*p == '+') {
    *payload_out = p + 1;
  }
  return 1;
}

static int socket_emit(GitOverleafSocketClient* client, const char* name,
                       json_t* args, json_t** ack_out, GitOverleafError* err) {
  *ack_out = NULL;
  char ack_id[32];
  snprintf(ack_id, sizeof(ack_id), "%d", ++client->next_ack_id);
  json_t* payload = json_object();
  if (!payload) {
    return git_overleaf_error(err, "out of memory");
  }
  json_object_set_new(payload, "name", json_string(name));
  json_object_set(payload, "args", args);
  char* payload_text = json_dumps(payload, JSON_COMPACT);
  json_decref(payload);
  if (!payload_text) {
    return git_overleaf_error(err, "out of memory");
  }
  size_t frame_len =
      strlen("5:+::") + strlen(ack_id) + strlen(payload_text) + 1;
  char* frame = malloc(frame_len);
  if (!frame) {
    free(payload_text);
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(frame, frame_len, "5:%s+::%s", ack_id, payload_text);
  free(payload_text);
  double deadline = monotonic_seconds() + 15.0;
  if (socket_send_text(client, frame, deadline, err) != 0) {
    free(frame);
    return -1;
  }
  free(frame);

  while (monotonic_seconds() < deadline) {
    char* text = NULL;
    if (socket_recv_text(client, &text, deadline, err) != 0) {
      return -1;
    }
    if (strncmp(text, "6:", 2) == 0) {
      const char* payload = NULL;
      int matches = socket_ack_id_matches(text, ack_id, &payload);
      if (matches) {
        if (payload && *payload) {
          json_error_t json_err;
          *ack_out = json_loads(payload, 0, &json_err);
          if (!*ack_out) {
            free(text);
            return git_overleaf_error(err, "could not parse websocket ack: %s",
                                      json_err.text);
          }
        } else {
          *ack_out = json_array();
          if (!*ack_out) {
            free(text);
            return git_overleaf_error(err, "out of memory");
          }
        }
        free(text);
        return 0;
      }
    } else if (socket_handle_non_ack_frame(client, text, deadline, err) != 0) {
      free(text);
      return -1;
    }
    free(text);
  }
  return git_overleaf_error(err, "timed out waiting for websocket ack `%s'",
                            name);
}

static int socket_connect(const GitOverleafConfig* cfg, const char* project_id,
                          GitOverleafSocketClient* client,
                          GitOverleafError* err) {
  memset(client, 0, sizeof(*client));
  size_t handshake_path_len =
      strlen("socket.io/1/?projectId=&esh=1&ssp=1") + strlen(project_id) + 1;
  char* handshake_path = malloc(handshake_path_len);
  if (!handshake_path) {
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(handshake_path, handshake_path_len,
           "socket.io/1/?projectId=%s&esh=1&ssp=1", project_id);
  char* handshake_url = git_overleaf_url_join(cfg->url, handshake_path);
  free(handshake_path);
  if (!handshake_url) {
    return git_overleaf_error(err, "out of memory");
  }
  GitOverleafBuffer body = {0};
  if (git_overleaf_http_get(cfg, handshake_url, cfg->url, &body, err) != 0) {
    free(handshake_url);
    return -1;
  }
  char* colon = strchr(body.data ? body.data : "", ':');
  if (colon) {
    *colon = '\0';
  }
  if (!body.data || !*body.data) {
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    return git_overleaf_error(err,
                              "Overleaf Socket.IO handshake returned no id");
  }

  char* sanitized = git_overleaf_sanitize_url(cfg->url);
  if (!sanitized) {
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    return git_overleaf_error(err, "out of memory");
  }
  const char* scheme_suffix = NULL;
  const char* ws_scheme = NULL;
  if (strncmp(sanitized, "https://", 8) == 0) {
    ws_scheme = "wss://";
    scheme_suffix = sanitized + 8;
  } else if (strncmp(sanitized, "http://", 7) == 0) {
    ws_scheme = "ws://";
    scheme_suffix = sanitized + 7;
  } else {
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    free(sanitized);
    return git_overleaf_error(err, "unsupported Overleaf URL for websocket: %s",
                              cfg->url);
  }
  size_t ws_len = strlen(ws_scheme) + strlen(scheme_suffix) +
                  strlen("/socket.io/1/websocket/?projectId=&esh=1&ssp=1") +
                  strlen(body.data) + strlen(project_id) + 1;
  char* ws_url = malloc(ws_len);
  if (!ws_url) {
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    free(sanitized);
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(ws_url, ws_len,
           "%s%s/socket.io/1/websocket/%s?projectId=%s&esh=1&ssp=1", ws_scheme,
           scheme_suffix, body.data, project_id);

  client->curl = curl_easy_init();
  if (!client->curl) {
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    free(sanitized);
    free(ws_url);
    return git_overleaf_error(err, "could not initialize curl");
  }
  client->headers = build_headers(cfg, cfg->url, NULL, err);
  if (!client->headers) {
    curl_easy_cleanup(client->curl);
    client->curl = NULL;
    git_overleaf_buffer_free(&body);
    free(handshake_url);
    free(sanitized);
    free(ws_url);
    return -1;
  }
  char error_buffer[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(client->curl, CURLOPT_URL, ws_url);
  curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, client->headers);
  curl_easy_setopt(client->curl, CURLOPT_CONNECT_ONLY, 2L);
  curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(client->curl, CURLOPT_USERAGENT, "git-overleaf-cli/0.1");
  curl_easy_setopt(client->curl, CURLOPT_ERRORBUFFER, error_buffer);
  CURLcode code = curl_easy_perform(client->curl);
  git_overleaf_buffer_free(&body);
  free(handshake_url);
  free(sanitized);
  free(ws_url);
  if (code != CURLE_OK) {
    return git_overleaf_error(
        err, "websocket connect failed: %s",
        error_buffer[0] ? error_buffer : curl_easy_strerror(code));
  }
  return 0;
}

static void socket_close(GitOverleafSocketClient* client) {
  if (!client) {
    return;
  }
  for (size_t i = 0; i < client->events_len; i++) {
    json_decref(client->events[i]);
  }
  free(client->events);
  if (client->curl) {
    curl_easy_cleanup(client->curl);
  }
  if (client->headers) {
    curl_slist_free_all(client->headers);
  }
  memset(client, 0, sizeof(*client));
}

static int socket_wait_next_event(GitOverleafSocketClient* client,
                                  const char* name, const char* doc_id,
                                  int update_event, json_t** out,
                                  GitOverleafError* err) {
  *out = socket_take_event(client, name, doc_id, update_event);
  if (*out) {
    return 0;
  }
  double deadline = monotonic_seconds() + 15.0;
  while (monotonic_seconds() < deadline) {
    char* text = NULL;
    if (socket_recv_text(client, &text, deadline, err) != 0) {
      return -1;
    }
    if (strncmp(text, "2::", 3) == 0) {
      int rc = socket_send_text(client, "2::", deadline, err);
      free(text);
      if (rc != 0) {
        return -1;
      }
      continue;
    }
    if (strncmp(text, "7:", 2) == 0) {
      free(text);
      return git_overleaf_error(err, "Unauthorized websocket response");
    }
    if (strncmp(text, "5:", 2) == 0) {
      json_t* event = NULL;
      if (socket_handle_event_text(client, text, &event, err) != 0) {
        free(text);
        return -1;
      }
      free(text);
      if (client->failure[0]) {
        json_decref(event);
        return git_overleaf_error(err, "%s", client->failure);
      }
      if (!name || socket_event_name_matches(event, name)) {
        if (update_event && doc_id) {
          if (!socket_doc_update_event_matches(event, doc_id)) {
            if (socket_queue_event(client, event, err) != 0) {
              json_decref(event);
              return -1;
            }
            continue;
          }
        }
        *out = event;
        return 0;
      }
      if (socket_queue_event(client, event, err) != 0) {
        json_decref(event);
        return -1;
      }
    } else {
      free(text);
    }
  }
  return git_overleaf_error(err, "timed out waiting for websocket event `%s'",
                            name);
}

int git_overleaf_overleaf_fetch_remote_table(const GitOverleafConfig* cfg,
                                             const char* project_id,
                                             GitOverleafRemoteTable* out,
                                             GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  GitOverleafSocketClient client;
  if (socket_connect(cfg, project_id, &client, err) != 0) {
    return -1;
  }
  json_t* join_event = NULL;
  int rc = socket_wait_next_event(&client, "joinProjectResponse", NULL, 0,
                                  &join_event, err);
  if (rc == 0) {
    json_t* args = json_object_get(join_event, "args");
    json_t* first = json_is_array(args) ? json_array_get(args, 0) : NULL;
    json_t* project =
        json_is_object(first) ? json_object_get(first, "project") : NULL;
    json_t* root_folders =
        json_is_object(project) ? json_object_get(project, "rootFolder") : NULL;
    json_t* root_folder =
        json_is_array(root_folders) ? json_array_get(root_folders, 0) : NULL;
    if (!root_folder) {
      rc = git_overleaf_error(err, "could not fetch remote project tree");
    } else {
      rc = git_overleaf_overleaf_parse_remote_table(root_folder, out, err);
    }
  }
  json_decref(join_event);
  socket_close(&client);
  return rc;
}

static int utf8_codepoint(const unsigned char* text, size_t len, size_t* offset,
                          unsigned int* codepoint) {
  unsigned char c = text[*offset];
  if (c < 0x80) {
    *codepoint = c;
    (*offset)++;
    return 0;
  }
  size_t need = 0;
  unsigned int value = 0;
  if ((c & 0xE0) == 0xC0) {
    need = 2;
    value = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    need = 3;
    value = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    need = 4;
    value = c & 0x07;
  } else {
    return -1;
  }
  if (*offset + need > len) {
    return -1;
  }
  for (size_t i = 1; i < need; i++) {
    unsigned char d = text[*offset + i];
    if ((d & 0xC0) != 0x80) {
      return -1;
    }
    value = (value << 6) | (d & 0x3F);
  }
  *offset += need;
  *codepoint = value;
  return 0;
}

static int utf16_units_for_prefix(const char* text, size_t len, size_t* out,
                                  GitOverleafError* err) {
  *out = 0;
  size_t offset = 0;
  while (offset < len) {
    unsigned int cp = 0;
    if (utf8_codepoint((const unsigned char*)text, len, &offset, &cp) != 0) {
      return git_overleaf_error(err,
                                "ShareJS text update requires valid UTF-8");
    }
    *out += cp > 0xFFFF ? 2 : 1;
  }
  return 0;
}

static int utf8_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

int git_overleaf_sharejs_text_op(const char* before, const char* after,
                                 json_t** out, GitOverleafError* err) {
  *out = NULL;
  if (!before || !after) {
    return git_overleaf_error(err, "ShareJS text op requires text inputs");
  }
  size_t before_len = strlen(before);
  size_t after_len = strlen(after);
  if (before_len == after_len && memcmp(before, after, before_len) == 0) {
    return 0;
  }
  size_t prefix = 0;
  while (prefix < before_len && prefix < after_len &&
         before[prefix] == after[prefix]) {
    prefix++;
  }
  while (prefix > 0 && ((prefix < before_len &&
                         utf8_continuation((unsigned char)before[prefix])) ||
                        (prefix < after_len &&
                         utf8_continuation((unsigned char)after[prefix])))) {
    prefix--;
  }
  size_t suffix = 0;
  while (suffix < before_len - prefix && suffix < after_len - prefix &&
         before[before_len - suffix - 1] == after[after_len - suffix - 1]) {
    suffix++;
  }
  while (suffix > 0 &&
         (((before_len - suffix) < before_len &&
           utf8_continuation((unsigned char)before[before_len - suffix])) ||
          ((after_len - suffix) < after_len &&
           utf8_continuation((unsigned char)after[after_len - suffix])))) {
    suffix--;
  }

  size_t position = 0;
  if (utf16_units_for_prefix(before, prefix, &position, err) != 0) {
    return -1;
  }
  size_t deleted_len = before_len - prefix - suffix;
  size_t inserted_len = after_len - prefix - suffix;
  char* deleted = git_overleaf_xstrndup(before + prefix, deleted_len);
  char* inserted = git_overleaf_xstrndup(after + prefix, inserted_len);
  if (!deleted || !inserted) {
    free(deleted);
    free(inserted);
    return git_overleaf_error(err, "out of memory");
  }
  json_t* ops = json_array();
  if (!ops) {
    free(deleted);
    free(inserted);
    return git_overleaf_error(err, "out of memory");
  }
  if (deleted_len > 0) {
    json_t* item = json_object();
    json_object_set_new(item, "d", json_string(deleted));
    json_object_set_new(item, "p", json_integer((json_int_t)position));
    json_array_append_new(ops, item);
  }
  if (inserted_len > 0) {
    json_t* item = json_object();
    json_object_set_new(item, "i", json_string(inserted));
    json_object_set_new(item, "p", json_integer((json_int_t)position));
    json_array_append_new(ops, item);
  }
  free(deleted);
  free(inserted);
  *out = ops;
  return 0;
}

static char* ack_doc_text(json_t* lines, const char* doc_id,
                          GitOverleafError* err) {
  size_t total = 1;
  size_t index;
  json_t* line;
  if (!json_is_array(lines)) {
    git_overleaf_error(err, "could not read Overleaf doc text for %s", doc_id);
    return NULL;
  }
  json_array_foreach(lines, index, line) {
    if (!json_is_string(line)) {
      git_overleaf_error(err, "could not read Overleaf doc text for %s",
                         doc_id);
      return NULL;
    }
    total += strlen(json_string_value(line));
    if (index + 1 < json_array_size(lines)) {
      total++;
    }
  }
  char* text = malloc(total);
  if (!text) {
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  text[0] = '\0';
  json_array_foreach(lines, index, line) {
    strcat(text, json_string_value(line));
    if (index + 1 < json_array_size(lines)) {
      strcat(text, "\n");
    }
  }
  return text;
}

static int remote_doc_state(GitOverleafSocketClient* client, const char* doc_id,
                            char** text_out, json_int_t* version_out,
                            GitOverleafError* err) {
  *text_out = NULL;
  *version_out = 0;
  json_t* options = json_object();
  json_object_set_new(options, "encodeRanges", json_true());
  json_object_set_new(options, "supportsHistoryOT", json_true());
  json_t* args = json_array();
  json_array_append_new(args, json_string(doc_id));
  json_array_append_new(args, json_integer(-1));
  json_array_append_new(args, options);
  json_t* ack = NULL;
  int rc = socket_emit(client, "joinDoc", args, &ack, err);
  json_decref(args);
  if (rc != 0) {
    return -1;
  }
  json_t* error_object = json_array_get(ack, 0);
  if (error_object && !json_is_null(error_object)) {
    json_decref(ack);
    return git_overleaf_error(err, "could not join Overleaf doc %s", doc_id);
  }
  json_t* lines = json_array_get(ack, 1);
  json_t* version = json_array_get(ack, 2);
  json_t* type = json_array_get(ack, 5);
  if (!json_is_integer(version)) {
    json_decref(ack);
    return git_overleaf_error(
        err, "could not determine Overleaf doc version for %s", doc_id);
  }
  if (!json_is_string(type) ||
      strcmp(json_string_value(type), "sharejs-text-ot") != 0) {
    json_decref(ack);
    return git_overleaf_error(err, "Overleaf doc %s uses unsupported OT type",
                              doc_id);
  }
  char* text = ack_doc_text(lines, doc_id, err);
  if (!text) {
    json_decref(ack);
    return -1;
  }
  *text_out = text;
  *version_out = json_integer_value(version);
  json_decref(ack);
  return 0;
}

static int wait_doc_update_applied(GitOverleafSocketClient* client,
                                   const char* doc_id, GitOverleafError* err) {
  for (;;) {
    json_t* event = NULL;
    if (socket_wait_next_event(client, NULL, doc_id, 1, &event, err) != 0) {
      return -1;
    }
    if (socket_event_name_matches(event, "otUpdateError")) {
      json_decref(event);
      return git_overleaf_error(
          err, "could not update Overleaf doc %s through OT", doc_id);
    }
    json_t* args = json_object_get(event, "args");
    json_t* update = json_is_array(args) ? json_array_get(args, 0) : NULL;
    int source_applied =
        json_is_object(update) && !json_object_get(update, "op");
    json_decref(event);
    if (source_applied) {
      return 0;
    }
  }
}

int git_overleaf_overleaf_update_doc_text_content(
    const GitOverleafConfig* cfg, const char* project_id, const char* doc_id,
    const char* before, const char* after, GitOverleafError* err) {
  json_t* op = NULL;
  if (git_overleaf_sharejs_text_op(before, after, &op, err) != 0) {
    return -1;
  }
  if (!op) {
    return 0;
  }
  GitOverleafSocketClient client;
  if (socket_connect(cfg, project_id, &client, err) != 0) {
    json_decref(op);
    return -1;
  }
  json_t* join_event = NULL;
  int rc = socket_wait_next_event(&client, "joinProjectResponse", NULL, 0,
                                  &join_event, err);
  json_decref(join_event);
  if (rc != 0) {
    socket_close(&client);
    json_decref(op);
    return -1;
  }

  char* remote_text = NULL;
  json_int_t version = 0;
  if (remote_doc_state(&client, doc_id, &remote_text, &version, err) != 0) {
    socket_close(&client);
    json_decref(op);
    return -1;
  }
  if (strcmp(before, remote_text) != 0) {
    free(remote_text);
    socket_close(&client);
    json_decref(op);
    return git_overleaf_error(err,
                              "remote Overleaf doc %s changed after snapshot "
                              "download; pull and retry",
                              doc_id);
  }
  free(remote_text);

  json_t* update = json_object();
  json_object_set_new(update, "doc", json_string(doc_id));
  json_object_set_new(update, "v", json_integer(version));
  json_object_set(update, "op", op);
  json_t* meta = json_object();
  json_object_set_new(meta, "source", json_string("git-overleaf-cli"));
  json_object_set_new(update, "meta", meta);
  json_t* args = json_array();
  json_array_append_new(args, json_string(doc_id));
  json_array_append_new(args, update);
  json_t* ack = NULL;
  rc = socket_emit(&client, "applyOtUpdate", args, &ack, err);
  json_decref(args);
  json_decref(op);
  if (rc != 0) {
    socket_close(&client);
    return -1;
  }
  json_t* error_object = json_array_get(ack, 0);
  if (error_object && !json_is_null(error_object)) {
    json_decref(ack);
    socket_close(&client);
    return git_overleaf_error(
        err, "could not update Overleaf doc %s through OT", doc_id);
  }
  json_decref(ack);
  if (wait_doc_update_applied(&client, doc_id, err) != 0) {
    socket_close(&client);
    return -1;
  }
  char* after_remote = NULL;
  json_int_t ignored_version = 0;
  if (remote_doc_state(&client, doc_id, &after_remote, &ignored_version, err) !=
      0) {
    socket_close(&client);
    return -1;
  }
  int matches = strcmp(after, after_remote) == 0;
  free(after_remote);
  socket_close(&client);
  if (!matches) {
    return git_overleaf_error(err,
                              "Overleaf doc %s did not match expected text "
                              "after OT update; pull and retry",
                              doc_id);
  }
  return 0;
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

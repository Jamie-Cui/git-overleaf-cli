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
#include <stdlib.h>
#include <string.h>

#include "git-overleaf-cli.h"

static int ci_char(int c) {
  return tolower((unsigned char)c);
}

static int ci_starts_with(const char* text, const char* prefix) {
  if (!text || !prefix) {
    return 0;
  }
  while (*prefix) {
    if (!*text || ci_char(*text) != ci_char(*prefix)) {
      return 0;
    }
    text++;
    prefix++;
  }
  return 1;
}

static int ci_contains(const char* text, const char* needle) {
  if (!text || !needle) {
    return 0;
  }
  if (!*needle) {
    return 1;
  }
  for (const char* p = text; *p; p++) {
    const char* h = p;
    const char* n = needle;
    while (*h && *n && ci_char(*h) == ci_char(*n)) {
      h++;
      n++;
    }
    if (!*n) {
      return 1;
    }
  }
  return 0;
}

static size_t tokenize_query(char* query, char** tokens, size_t max_tokens) {
  size_t count = 0;
  char* p = query;
  while (*p && count < max_tokens) {
    while (*p && isspace((unsigned char)*p)) {
      p++;
    }
    if (!*p) {
      break;
    }
    tokens[count++] = p;
    while (*p && !isspace((unsigned char)*p)) {
      p++;
    }
    if (*p) {
      *p++ = '\0';
    }
  }
  return count;
}

static int project_field_has_all_tokens(const char* field, char** tokens,
                                        size_t token_count) {
  for (size_t i = 0; i < token_count; i++) {
    if (!ci_contains(field, tokens[i])) {
      return 0;
    }
  }
  return 1;
}

static int project_matches_all_tokens(const GitOverleafProject* project,
                                      char** tokens, size_t token_count) {
  for (size_t i = 0; i < token_count; i++) {
    if (!ci_contains(project->name, tokens[i]) &&
        !ci_contains(project->owner_email, tokens[i]) &&
        !ci_contains(project->id, tokens[i])) {
      return 0;
    }
  }
  return 1;
}

static int project_match_score(const GitOverleafProject* project,
                               const char* query, char** tokens,
                               size_t token_count) {
  if (token_count == 0) {
    return 100;
  }
  if (strcmp(project->id, query) == 0) {
    return 0;
  }
  if (!project_matches_all_tokens(project, tokens, token_count)) {
    return -1;
  }
  /* Lower scores sort first. Prefer name-prefix hits, then all-token matches
     in a single stable field, and finally cross-field fuzzy matches. */
  if (ci_starts_with(project->name, tokens[0])) {
    return 10;
  }
  if (project_field_has_all_tokens(project->name, tokens, token_count)) {
    return 20;
  }
  if (project_field_has_all_tokens(project->id, tokens, token_count)) {
    return 25;
  }
  if (project_field_has_all_tokens(project->owner_email, tokens,
                                   token_count)) {
    return 30;
  }
  return 40;
}

static int compare_project_matches(const void* a, const void* b) {
  const GitOverleafProjectMatch* left = a;
  const GitOverleafProjectMatch* right = b;
  if (left->score != right->score) {
    return left->score - right->score;
  }
  if (left->index < right->index) {
    return -1;
  }
  if (left->index > right->index) {
    return 1;
  }
  return 0;
}

int git_overleaf_project_find_exact_id(const GitOverleafProjectList* projects,
                                       const char* text, size_t* selected) {
  if (!projects || !text || !*text) {
    return 0;
  }
  for (size_t i = 0; i < projects->len; i++) {
    if (strcmp(projects->items[i].id, text) == 0) {
      if (selected) {
        *selected = i;
      }
      return 1;
    }
  }
  return 0;
}

int git_overleaf_project_match_query(const GitOverleafProjectList* projects,
                                     const char* query,
                                     GitOverleafProjectMatch** out,
                                     size_t* out_len,
                                     GitOverleafError* err) {
  *out = NULL;
  *out_len = 0;
  if (!projects) {
    return 0;
  }

  char* trimmed = git_overleaf_trimmed_dup(query ? query : "");
  if (!trimmed) {
    return git_overleaf_error(err, "out of memory");
  }
  char* tokens[16];
  size_t token_count =
      tokenize_query(trimmed, tokens, sizeof(tokens) / sizeof(tokens[0]));

  GitOverleafProjectMatch* matches =
      calloc(projects->len, sizeof(*matches));
  if (!matches && projects->len > 0) {
    free(trimmed);
    return git_overleaf_error(err, "out of memory");
  }

  size_t match_count = 0;
  for (size_t i = 0; i < projects->len; i++) {
    int score = project_match_score(&projects->items[i], trimmed, tokens,
                                    token_count);
    if (score >= 0) {
      matches[match_count].index = i;
      matches[match_count].score = score;
      match_count++;
    }
  }
  qsort(matches, match_count, sizeof(*matches), compare_project_matches);

  free(trimmed);
  *out = matches;
  *out_len = match_count;
  return 0;
}

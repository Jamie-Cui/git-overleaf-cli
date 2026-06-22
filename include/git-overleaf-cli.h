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

#ifndef GIT_OVERLEAF_CLI_H
#define GIT_OVERLEAF_CLI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIT_OVERLEAF_DEFAULT_URL "https://www.overleaf.com"
#define GIT_OVERLEAF_DEFAULT_COOKIE_FILE "~/.git-overleaf-cookies"
#define GIT_OVERLEAF_BASE_REF "refs/git-overleaf/base"
#define GIT_OVERLEAF_SYNC_METADATA_FILE ".git-overleaf-sync.json"

typedef struct {
  char message[2048];
} GitOverleafError;

typedef struct {
  char* data;
  size_t len;
} GitOverleafBuffer;

typedef struct {
  char* url;
  char* cookie;
  char* cookie_file;
  char* git;
  char* unzip;
  int url_explicit;
  int json;
  int verbose;
} GitOverleafConfig;

typedef struct {
  int status;
  char* output;
} GitOverleafProcessResult;

typedef struct {
  char* id;
  char* name;
  char* owner_email;
} GitOverleafProject;

typedef struct {
  GitOverleafProject* items;
  size_t len;
} GitOverleafProjectList;

typedef struct {
  size_t index;
  int score;
} GitOverleafProjectMatch;

typedef struct {
  char* temp_dir;
  char* root;
} GitOverleafSnapshot;

typedef enum {
  GIT_OVERLEAF_SYNC_IN_SYNC,
  GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE,
  GIT_OVERLEAF_SYNC_REMOTE_MATCHES_BASE,
  GIT_OVERLEAF_SYNC_HEAD_MATCHES_BASE,
  GIT_OVERLEAF_SYNC_DIVERGED
} GitOverleafSyncState;

int git_overleaf_error(GitOverleafError* err, const char* fmt, ...);
char* git_overleaf_xstrdup(const char* s);
char* git_overleaf_xstrndup(const char* s, size_t n);
char* git_overleaf_trim(char* s);
char* git_overleaf_trimmed_dup(const char* s);
char* git_overleaf_expand_home(const char* path);
char* git_overleaf_path_join(const char* left, const char* right);
char* git_overleaf_url_join(const char* base, const char* path);
char* git_overleaf_sanitize_url(const char* url);
char* git_overleaf_project_directory_name(const char* name,
                                          GitOverleafError* err);
int git_overleaf_write_private_file(const char* path, const char* text,
                                    GitOverleafError* err);
int git_overleaf_read_file(const char* path, char** out, GitOverleafError* err);
void git_overleaf_buffer_free(GitOverleafBuffer* buffer);

void git_overleaf_config_init(GitOverleafConfig* cfg);
void git_overleaf_config_free(GitOverleafConfig* cfg);
int git_overleaf_config_load_cookie(GitOverleafConfig* cfg,
                                    GitOverleafError* err);
int git_overleaf_firefox_cookie_header(const GitOverleafConfig* cfg,
                                       const char* firefox_profile, char** out,
                                       GitOverleafError* err);

int git_overleaf_process_run(char* const argv[], const char* cwd,
                             char* const env[], int allow_failure,
                             GitOverleafProcessResult* out,
                             GitOverleafError* err);
void git_overleaf_process_result_free(GitOverleafProcessResult* result);

int git_overleaf_git_capture(const GitOverleafConfig* cfg, const char* repo,
                             const char* const args[], size_t argc,
                             char* const env[], int allow_failure,
                             GitOverleafProcessResult* out,
                             GitOverleafError* err);
int git_overleaf_git_output(const GitOverleafConfig* cfg, const char* repo,
                            const char* const args[], size_t argc,
                            char* const env[], char** out,
                            GitOverleafError* err);
int git_overleaf_git_ok(const GitOverleafConfig* cfg, const char* repo,
                        const char* const args[], size_t argc,
                        char* const env[], GitOverleafError* err);
int git_overleaf_git_config_get(const GitOverleafConfig* cfg, const char* repo,
                                const char* key, char** out,
                                GitOverleafError* err);
int git_overleaf_git_config_set(const GitOverleafConfig* cfg, const char* repo,
                                const char* key, const char* value,
                                GitOverleafError* err);
int git_overleaf_git_root(const GitOverleafConfig* cfg, const char* directory,
                          char** out, GitOverleafError* err);
int git_overleaf_git_current_branch(const GitOverleafConfig* cfg,
                                    const char* repo, char** out,
                                    GitOverleafError* err);
int git_overleaf_git_rev_parse(const GitOverleafConfig* cfg, const char* repo,
                               const char* revision, char** out,
                               GitOverleafError* err);
int git_overleaf_git_rev_parse_verify(const GitOverleafConfig* cfg,
                                      const char* repo, const char* revision,
                                      char** out, GitOverleafError* err);
int git_overleaf_git_tree_id(const GitOverleafConfig* cfg, const char* repo,
                             const char* revision, char** out,
                             GitOverleafError* err);
int git_overleaf_git_is_clean(const GitOverleafConfig* cfg, const char* repo,
                              GitOverleafError* err);
int git_overleaf_git_is_ancestor(const GitOverleafConfig* cfg, const char* repo,
                                 const char* ancestor, const char* descendant,
                                 int* is_ancestor, GitOverleafError* err);
int git_overleaf_git_commit_directory(const GitOverleafConfig* cfg,
                                      const char* repo, const char* directory,
                                      const char* parent, const char* message,
                                      char** commit_out, GitOverleafError* err);
int git_overleaf_git_write_metadata(const GitOverleafConfig* cfg,
                                    const char* repo, const char* project_id,
                                    const char* project_name,
                                    GitOverleafError* err);
int git_overleaf_git_set_base_ref(const GitOverleafConfig* cfg,
                                  const char* repo, const char* revision,
                                  GitOverleafError* err);
int git_overleaf_git_prepare_sync_metadata_repo(const char* repo,
                                                GitOverleafError* err);

int git_overleaf_ensure_directory(const char* path, GitOverleafError* err);
int git_overleaf_directory_empty_or_missing(const char* path, int* empty,
                                            GitOverleafError* err);
int git_overleaf_copy_tree(const char* source, const char* destination,
                           GitOverleafError* err);
int git_overleaf_remove_tree(const char* path, GitOverleafError* err);
int git_overleaf_make_temp_dir(char** out, GitOverleafError* err);
int git_overleaf_make_temp_file(char** out, GitOverleafError* err);
int git_overleaf_normalize_extracted_root(const char* directory, char** out,
                                          GitOverleafError* err);
int git_overleaf_delete_sync_metadata(const char* root, char** metadata_text,
                                      GitOverleafError* err);

int git_overleaf_http_get(const GitOverleafConfig* cfg, const char* url,
                          const char* referer, GitOverleafBuffer* out,
                          GitOverleafError* err);
int git_overleaf_http_download(const GitOverleafConfig* cfg, const char* url,
                               const char* referer, const char* output_file,
                               GitOverleafError* err);

int git_overleaf_overleaf_parse_projects_page(const char* html,
                                              GitOverleafProjectList* out,
                                              GitOverleafError* err);
int git_overleaf_overleaf_list_projects(const GitOverleafConfig* cfg,
                                        GitOverleafProjectList* out,
                                        GitOverleafError* err);
void git_overleaf_project_list_free(GitOverleafProjectList* list);
int git_overleaf_project_find_exact_id(const GitOverleafProjectList* projects,
                                       const char* text, size_t* selected);
int git_overleaf_project_match_query(const GitOverleafProjectList* projects,
                                     const char* query,
                                     GitOverleafProjectMatch** out,
                                     size_t* out_len,
                                     GitOverleafError* err);
GitOverleafSyncState git_overleaf_overleaf_sync_state(
    const char* base_tree, const char* head_tree, const char* remote_tree);
int git_overleaf_overleaf_download_snapshot(const GitOverleafConfig* cfg,
                                            const char* project_id,
                                            GitOverleafSnapshot* out,
                                            GitOverleafError* err);
void git_overleaf_snapshot_free(GitOverleafSnapshot* snapshot);
int git_overleaf_overleaf_clone(const GitOverleafConfig* cfg,
                                const char* project_id,
                                const char* project_name, const char* target,
                                GitOverleafError* err);
int git_overleaf_overleaf_init(const GitOverleafConfig* cfg, const char* repo,
                               const char* project_id, const char* project_name,
                               GitOverleafError* err);
int git_overleaf_overleaf_pull(const GitOverleafConfig* cfg,
                               const char* repo_arg, GitOverleafError* err);

#ifdef __cplusplus
}
#endif

#endif

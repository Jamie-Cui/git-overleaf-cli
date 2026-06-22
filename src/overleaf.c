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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "git-overleaf-cli.h"

static char* project_path(const char* project_id) {
  size_t len = strlen("project//") + strlen(project_id) + 1;
  char* path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "project/%s", project_id);
  return path;
}

static char* project_download_path(const char* project_id) {
  size_t len = strlen("project//download/zip") + strlen(project_id) + 1;
  char* path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "project/%s/download/zip", project_id);
  return path;
}

static int git_unset_config(const GitOverleafConfig* cfg, const char* repo,
                            const char* key, GitOverleafError* err) {
  const char* args[] = {"config", "--local", "--unset-all", key};
  GitOverleafProcessResult result;
  int rc = git_overleaf_git_capture(cfg, repo, args, 4, NULL, 1, &result, err);
  git_overleaf_process_result_free(&result);
  return rc;
}

static int clear_pending(const GitOverleafConfig* cfg, const char* repo,
                         GitOverleafError* err) {
  if (git_unset_config(cfg, repo, "git-overleaf.pendingRemoteCommit", err) !=
      0) {
    return -1;
  }
  if (git_unset_config(cfg, repo, "git-overleaf.pendingAction", err) != 0) {
    return -1;
  }
  return 0;
}

static int repo_project_id(const GitOverleafConfig* cfg, const char* repo,
                           char** out, GitOverleafError* err) {
  if (git_overleaf_git_config_get(cfg, repo, "git-overleaf.projectId", out,
                                  err) != 0) {
    return -1;
  }
  if (!*out || !**out) {
    free(*out);
    *out = NULL;
    return git_overleaf_error(
        err, "repository is not configured as an Overleaf project");
  }
  return 0;
}

static int apply_repo_url(const GitOverleafConfig* cfg, const char* repo,
                          GitOverleafConfig* local, char** repo_url_owner,
                          GitOverleafError* err) {
  *local = *cfg;
  *repo_url_owner = NULL;
  if (cfg->url_explicit) {
    return 0;
  }
  /* A repo keeps the Overleaf host it was initialized with; a command-line
     --url is treated as an explicit override for this run. */
  if (git_overleaf_git_config_get(cfg, repo, "git-overleaf.url", repo_url_owner,
                                  err) != 0) {
    return -1;
  }
  if (*repo_url_owner && **repo_url_owner) {
    local->url = *repo_url_owner;
  }
  return 0;
}

int git_overleaf_overleaf_download_snapshot(const GitOverleafConfig* cfg,
                                            const char* project_id,
                                            GitOverleafSnapshot* out,
                                            GitOverleafError* err) {
  memset(out, 0, sizeof(*out));
  char* temp_dir = NULL;
  char* zip_file = NULL;
  char* download_path = NULL;
  char* download_url = NULL;
  char* referer_path = NULL;
  char* referer_url = NULL;
  char* root = NULL;
  char* metadata_text = NULL;

  if (git_overleaf_make_temp_dir(&temp_dir, err) != 0 ||
      git_overleaf_make_temp_file(&zip_file, err) != 0) {
    free(temp_dir);
    free(zip_file);
    return -1;
  }
  download_path = project_download_path(project_id);
  referer_path = project_path(project_id);
  download_url =
      download_path ? git_overleaf_url_join(cfg->url, download_path) : NULL;
  referer_url =
      referer_path ? git_overleaf_url_join(cfg->url, referer_path) : NULL;
  if (!download_path || !referer_path || !download_url || !referer_url) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(zip_file);
    free(download_path);
    free(download_url);
    free(referer_path);
    free(referer_url);
    return git_overleaf_error(err, "out of memory");
  }

  if (git_overleaf_http_download(cfg, download_url, referer_url, zip_file,
                                 err) != 0) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(zip_file);
    free(download_path);
    free(download_url);
    free(referer_path);
    free(referer_url);
    return -1;
  }

  char* argv[] = {
      cfg->unzip ? cfg->unzip : "unzip", "-q", zip_file, "-d", temp_dir, NULL};
  GitOverleafProcessResult unzip_result;
  if (git_overleaf_process_run(argv, NULL, NULL, 0, &unzip_result, err) != 0) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(zip_file);
    free(download_path);
    free(download_url);
    free(referer_path);
    free(referer_url);
    return -1;
  }
  git_overleaf_process_result_free(&unzip_result);
  unlink(zip_file);

  /* Overleaf zips may contain a single top-level directory, and Emacs
     git-overleaf metadata must not participate in content comparisons. */
  if (git_overleaf_normalize_extracted_root(temp_dir, &root, err) != 0 ||
      git_overleaf_delete_sync_metadata(root, &metadata_text, err) != 0) {
    git_overleaf_remove_tree(temp_dir, err);
    free(temp_dir);
    free(zip_file);
    free(download_path);
    free(download_url);
    free(referer_path);
    free(referer_url);
    free(root);
    free(metadata_text);
    return -1;
  }

  out->temp_dir = temp_dir;
  out->root = root;
  out->metadata_text = metadata_text;
  free(zip_file);
  free(download_path);
  free(download_url);
  free(referer_path);
  free(referer_url);
  return 0;
}

void git_overleaf_snapshot_free(GitOverleafSnapshot* snapshot) {
  if (!snapshot) {
    return;
  }
  if (snapshot->temp_dir) {
    GitOverleafError ignored = {{0}};
    git_overleaf_remove_tree(snapshot->temp_dir, &ignored);
  }
  free(snapshot->temp_dir);
  free(snapshot->root);
  free(snapshot->metadata_text);
  snapshot->temp_dir = NULL;
  snapshot->root = NULL;
  snapshot->metadata_text = NULL;
}

int git_overleaf_overleaf_clone(const GitOverleafConfig* cfg,
                                const char* project_id,
                                const char* project_name, const char* target,
                                GitOverleafError* err) {
  int empty = 0;
  if (git_overleaf_directory_empty_or_missing(target, &empty, err) != 0) {
    return -1;
  }
  if (!empty) {
    return git_overleaf_error(err, "target directory is not empty: %s", target);
  }

  GitOverleafSnapshot snapshot;
  if (git_overleaf_overleaf_download_snapshot(cfg, project_id, &snapshot,
                                              err) != 0) {
    return -1;
  }

  int rc = 0;
  if (git_overleaf_ensure_directory(target, err) != 0 ||
      git_overleaf_copy_tree(snapshot.root, target, err) != 0) {
    rc = -1;
    goto done;
  }

  const char* init_args[] = {"init"};
  const char* add_args[] = {"add", "--all", "."};
  const char* commit_args[] = {"-c",
                               "user.name=Overleaf Project",
                               "-c",
                               "user.email=git-overleaf@local",
                               "commit",
                               "-m",
                               "chore: import project from Overleaf"};
  if (git_overleaf_git_ok(cfg, target, init_args, 1, NULL, err) != 0 ||
      git_overleaf_git_write_metadata(cfg, target, project_id,
                                      project_name ? project_name : project_id,
                                      err) != 0 ||
      git_overleaf_git_prepare_sync_metadata_repo(target, err) != 0 ||
      git_overleaf_git_ok(cfg, target, add_args, 3, NULL, err) != 0 ||
      git_overleaf_git_ok(cfg, target, commit_args, 7, NULL, err) != 0 ||
      git_overleaf_git_set_base_ref(cfg, target, "HEAD", err) != 0) {
    rc = -1;
    goto done;
  }

done:
  git_overleaf_snapshot_free(&snapshot);
  return rc;
}

int git_overleaf_overleaf_init(const GitOverleafConfig* cfg,
                               const char* repo_arg, const char* project_id,
                               const char* project_name,
                               GitOverleafError* err) {
  char* repo = NULL;
  char* parent = NULL;
  char* commit = NULL;
  GitOverleafSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));

  if (git_overleaf_git_root(cfg, repo_arg ? repo_arg : ".", &repo, err) != 0) {
    return -1;
  }
  if (git_overleaf_overleaf_download_snapshot(cfg, project_id, &snapshot,
                                              err) != 0) {
    free(repo);
    return -1;
  }

  if (git_overleaf_git_rev_parse_verify(cfg, repo, GIT_OVERLEAF_BASE_REF,
                                        &parent, err) != 0) {
    git_overleaf_snapshot_free(&snapshot);
    free(repo);
    return -1;
  }
  /* init records the downloaded snapshot as the new sync base. If a previous
     base ref exists, the new base commit is chained after it for auditability.
   */
  char message[128];
  time_t now = time(NULL);
  struct tm tm_value;
  localtime_r(&now, &tm_value);
  strftime(message, sizeof(message),
           "overleaf: configured base snapshot %Y-%m-%d %H:%M:%S", &tm_value);
  if (git_overleaf_git_commit_directory(cfg, repo, snapshot.root, parent,
                                        message, &commit, err) != 0 ||
      git_overleaf_git_write_metadata(cfg, repo, project_id,
                                      project_name ? project_name : project_id,
                                      err) != 0 ||
      clear_pending(cfg, repo, err) != 0 ||
      git_overleaf_git_prepare_sync_metadata_repo(repo, err) != 0 ||
      git_overleaf_git_set_base_ref(cfg, repo, commit, err) != 0) {
    git_overleaf_snapshot_free(&snapshot);
    free(repo);
    free(parent);
    free(commit);
    return -1;
  }

  git_overleaf_snapshot_free(&snapshot);
  free(repo);
  free(parent);
  free(commit);
  return 0;
}

GitOverleafSyncState git_overleaf_overleaf_sync_state(const char* base_tree,
                                                      const char* head_tree,
                                                      const char* remote_tree) {
  /* Compare tree IDs instead of commit IDs: these synthetic commits only
     represent snapshot content, and their messages/timestamps are irrelevant.
   */
  if (strcmp(head_tree, base_tree) == 0 &&
      strcmp(remote_tree, base_tree) == 0) {
    return GIT_OVERLEAF_SYNC_IN_SYNC;
  }
  if (strcmp(head_tree, remote_tree) == 0) {
    return GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE;
  }
  if (strcmp(remote_tree, base_tree) == 0) {
    return GIT_OVERLEAF_SYNC_REMOTE_MATCHES_BASE;
  }
  if (strcmp(head_tree, base_tree) == 0) {
    return GIT_OVERLEAF_SYNC_HEAD_MATCHES_BASE;
  }
  return GIT_OVERLEAF_SYNC_DIVERGED;
}

typedef struct {
  char* path;
  char* full_path;
  int is_dir;
} GitOverleafLocalEntry;

typedef struct {
  GitOverleafLocalEntry* items;
  size_t len;
  size_t cap;
} GitOverleafLocalTable;

typedef struct {
  char* path;
  int is_folder;
  int depth;
} GitOverleafDeleteEntry;

static void local_table_free(GitOverleafLocalTable* table) {
  if (!table) {
    return;
  }
  for (size_t i = 0; i < table->len; i++) {
    free(table->items[i].path);
    free(table->items[i].full_path);
  }
  free(table->items);
  memset(table, 0, sizeof(*table));
}

static int local_table_append(GitOverleafLocalTable* table, const char* path,
                              const char* full_path, int is_dir,
                              GitOverleafError* err) {
  if (table->len == table->cap) {
    size_t next_cap = table->cap ? table->cap * 2 : 32;
    GitOverleafLocalEntry* next =
        realloc(table->items, next_cap * sizeof(GitOverleafLocalEntry));
    if (!next) {
      return git_overleaf_error(err, "out of memory");
    }
    table->items = next;
    memset(table->items + table->cap, 0,
           (next_cap - table->cap) * sizeof(GitOverleafLocalEntry));
    table->cap = next_cap;
  }
  table->items[table->len].path = git_overleaf_xstrdup(path ? path : "");
  table->items[table->len].full_path = git_overleaf_xstrdup(full_path);
  table->items[table->len].is_dir = is_dir;
  if (!table->items[table->len].path || !table->items[table->len].full_path) {
    return git_overleaf_error(err, "out of memory");
  }
  table->len++;
  return 0;
}

static GitOverleafLocalEntry* local_table_find(GitOverleafLocalTable* table,
                                               const char* path, int is_dir) {
  for (size_t i = 0; i < table->len; i++) {
    if (table->items[i].is_dir == is_dir &&
        strcmp(table->items[i].path, path) == 0) {
      return &table->items[i];
    }
  }
  return NULL;
}

static int path_depth(const char* path) {
  if (!path || !*path) {
    return 0;
  }
  int depth = 1;
  for (const char* p = path; *p; p++) {
    if (*p == '/') {
      depth++;
    }
  }
  return depth;
}

static char* parent_path(const char* path) {
  const char* slash = strrchr(path ? path : "", '/');
  if (!slash) {
    return git_overleaf_xstrdup("");
  }
  return git_overleaf_xstrndup(path, (size_t)(slash - path));
}

static const char* path_basename(const char* path) {
  const char* slash = strrchr(path ? path : "", '/');
  return slash ? slash + 1 : path;
}

static int scan_local_tree_walk(const char* root, const char* relative,
                                GitOverleafLocalTable* table,
                                GitOverleafError* err) {
  char* directory = relative && *relative
                        ? git_overleaf_path_join(root, relative)
                        : git_overleaf_xstrdup(root);
  if (!directory) {
    return git_overleaf_error(err, "out of memory");
  }
  DIR* dir = opendir(directory);
  if (!dir) {
    int saved = errno;
    free(directory);
    return git_overleaf_error(err, "could not open %s: %s", root,
                              strerror(saved));
  }
  struct dirent* entry;
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        strcmp(entry->d_name, ".git") == 0) {
      continue;
    }
    char* child_rel = relative && *relative
                          ? git_overleaf_path_join(relative, entry->d_name)
                          : git_overleaf_xstrdup(entry->d_name);
    char* child_full =
        child_rel ? git_overleaf_path_join(root, child_rel) : NULL;
    if (!child_rel || !child_full) {
      free(child_rel);
      free(child_full);
      closedir(dir);
      free(directory);
      return git_overleaf_error(err, "out of memory");
    }
    if (strcmp(child_rel, GIT_OVERLEAF_SYNC_METADATA_FILE) == 0) {
      free(child_rel);
      free(child_full);
      continue;
    }
    struct stat st;
    if (lstat(child_full, &st) != 0) {
      int saved = errno;
      free(child_rel);
      free(child_full);
      closedir(dir);
      free(directory);
      return git_overleaf_error(err, "could not inspect local path: %s",
                                strerror(saved));
    }
    if (S_ISDIR(st.st_mode)) {
      if (local_table_append(table, child_rel, child_full, 1, err) != 0 ||
          scan_local_tree_walk(root, child_rel, table, err) != 0) {
        free(child_rel);
        free(child_full);
        closedir(dir);
        free(directory);
        return -1;
      }
    } else if (S_ISREG(st.st_mode)) {
      if (local_table_append(table, child_rel, child_full, 0, err) != 0) {
        free(child_rel);
        free(child_full);
        closedir(dir);
        free(directory);
        return -1;
      }
    }
    free(child_rel);
    free(child_full);
  }
  closedir(dir);
  free(directory);
  return 0;
}

static int scan_local_tree(const char* root, GitOverleafLocalTable* table,
                           GitOverleafError* err) {
  memset(table, 0, sizeof(*table));
  char* root_copy = git_overleaf_xstrdup(root);
  if (!root_copy) {
    return git_overleaf_error(err, "out of memory");
  }
  if (local_table_append(table, "", root_copy, 1, err) != 0) {
    free(root_copy);
    return -1;
  }
  free(root_copy);
  if (scan_local_tree_walk(root, "", table, err) != 0) {
    local_table_free(table);
    return -1;
  }
  return 0;
}

static int local_max_depth(const GitOverleafLocalTable* table) {
  int max_depth = 0;
  for (size_t i = 0; i < table->len; i++) {
    int depth = path_depth(table->items[i].path);
    if (depth > max_depth) {
      max_depth = depth;
    }
  }
  return max_depth;
}

static int compare_file_entries(const void* a, const void* b) {
  const GitOverleafLocalEntry* left = *(const GitOverleafLocalEntry* const*)a;
  const GitOverleafLocalEntry* right = *(const GitOverleafLocalEntry* const*)b;
  return strcmp(left->path, right->path);
}

static int compare_delete_entries(const void* a, const void* b) {
  const GitOverleafDeleteEntry* left = a;
  const GitOverleafDeleteEntry* right = b;
  if (left->is_folder != right->is_folder) {
    return left->is_folder - right->is_folder;
  }
  if (left->depth != right->depth) {
    return right->depth - left->depth;
  }
  return strcmp(left->path, right->path);
}

static int git_config_has_value(const GitOverleafConfig* cfg, const char* repo,
                                const char* key, GitOverleafError* err) {
  const char* args[] = {"config", "--get", key};
  GitOverleafProcessResult result;
  if (git_overleaf_git_capture(cfg, repo, args, 3, NULL, 1, &result, err) !=
      0) {
    return 0;
  }
  int present = result.status == 0 && result.output && *result.output;
  git_overleaf_process_result_free(&result);
  return present;
}

static int commit_staged_changes(const GitOverleafConfig* cfg, const char* repo,
                                 GitOverleafError* err) {
  int merge_in_progress = 0;
  if (git_overleaf_git_merge_in_progress(cfg, repo, &merge_in_progress, err) !=
      0) {
    return -1;
  }
  int have_name = git_config_has_value(cfg, repo, "user.name", err);
  int have_email = git_config_has_value(cfg, repo, "user.email", err);
  const char* fallback_merge_args[] = {
      "-c",     "user.name=Overleaf Project",
      "-c",     "user.email=git-overleaf@local",
      "commit", "--no-edit"};
  const char* merge_args[] = {"commit", "--no-edit"};
  const char* fallback_commit_args[] = {"-c",
                                        "user.name=Overleaf Project",
                                        "-c",
                                        "user.email=git-overleaf@local",
                                        "commit",
                                        "-m",
                                        "chore: sync local changes before "
                                        "Overleaf push"};
  const char* commit_args[] = {"commit", "-m",
                               "chore: sync local changes before Overleaf "
                               "push"};
  if (merge_in_progress) {
    return have_name && have_email
               ? git_overleaf_git_ok(cfg, repo, merge_args, 2, NULL, err)
               : git_overleaf_git_ok(cfg, repo, fallback_merge_args, 6, NULL,
                                     err);
  }
  return have_name && have_email
             ? git_overleaf_git_ok(cfg, repo, commit_args, 3, NULL, err)
             : git_overleaf_git_ok(cfg, repo, fallback_commit_args, 7, NULL,
                                   err);
}

static int prepare_working_tree_for_push(const GitOverleafConfig* cfg,
                                         const char* repo, int stage_all,
                                         GitOverleafError* err) {
  if (stage_all) {
    const char* add_args[] = {"add", "--all", "."};
    if (git_overleaf_git_ok(cfg, repo, add_args, 3, NULL, err) != 0) {
      return -1;
    }
  }
  int staged = 0;
  int unstaged = 0;
  int unmerged = 0;
  if (git_overleaf_git_status_flags(cfg, repo, &staged, &unstaged, &unmerged,
                                    err) != 0) {
    return -1;
  }
  if (unmerged) {
    return git_overleaf_error(
        err, "repository has unresolved merge conflicts; resolve them first");
  }
  if (unstaged) {
    return git_overleaf_error(err,
                              "Overleaf push requires a clean working tree; "
                              "stage all changes or use --stage");
  }
  if (staged && commit_staged_changes(cfg, repo, err) != 0) {
    return -1;
  }
  return 0;
}

static int append_delete_entry(GitOverleafDeleteEntry** entries, size_t* len,
                               size_t* cap, const char* path, int is_folder,
                               GitOverleafError* err) {
  if (*len == *cap) {
    size_t next_cap = *cap ? *cap * 2 : 16;
    GitOverleafDeleteEntry* next =
        realloc(*entries, next_cap * sizeof(GitOverleafDeleteEntry));
    if (!next) {
      return git_overleaf_error(err, "out of memory");
    }
    *entries = next;
    *cap = next_cap;
  }
  (*entries)[*len].path = git_overleaf_xstrdup(path);
  (*entries)[*len].is_folder = is_folder;
  (*entries)[*len].depth = path_depth(path);
  if (!(*entries)[*len].path) {
    return git_overleaf_error(err, "out of memory");
  }
  (*len)++;
  return 0;
}

static void delete_entries_free(GitOverleafDeleteEntry* entries, size_t len) {
  for (size_t i = 0; i < len; i++) {
    free(entries[i].path);
  }
  free(entries);
}

static int sync_create_directories(const GitOverleafConfig* cfg,
                                   const char* project_id,
                                   GitOverleafLocalTable* local,
                                   GitOverleafRemoteTable* remote,
                                   GitOverleafError* err) {
  int max_depth = local_max_depth(local);
  for (int depth = 1; depth <= max_depth; depth++) {
    for (size_t i = 0; i < local->len; i++) {
      GitOverleafLocalEntry* entry = &local->items[i];
      if (!entry->is_dir || !*entry->path || path_depth(entry->path) != depth) {
        continue;
      }
      GitOverleafRemoteEntity* remote_entry =
          git_overleaf_remote_table_find(remote, entry->path);
      if (remote_entry && remote_entry->type != GIT_OVERLEAF_REMOTE_FOLDER) {
        if (git_overleaf_overleaf_delete_entity(cfg, project_id, remote_entry,
                                                err) != 0) {
          return -1;
        }
        git_overleaf_remote_table_remove(remote, entry->path);
        remote_entry = NULL;
      }
      if (!remote_entry) {
        char* parent = parent_path(entry->path);
        if (!parent) {
          return git_overleaf_error(err, "out of memory");
        }
        GitOverleafRemoteEntity* parent_entry =
            git_overleaf_remote_table_find(remote, parent);
        if (!parent_entry) {
          free(parent);
          return git_overleaf_error(
              err, "could not find remote parent folder for %s", entry->path);
        }
        char* id = NULL;
        if (git_overleaf_overleaf_create_folder(
                cfg, project_id, parent_entry->id, path_basename(entry->path),
                &id, err) != 0) {
          free(parent);
          return -1;
        }
        int rc = git_overleaf_remote_table_upsert(
            remote, entry->path, path_basename(entry->path), id,
            GIT_OVERLEAF_REMOTE_FOLDER, parent_entry->id, err);
        free(id);
        free(parent);
        if (rc != 0) {
          return -1;
        }
      }
    }
  }
  return 0;
}

static int sync_files(const GitOverleafConfig* cfg, const char* project_id,
                      GitOverleafLocalTable* local, const char* remote_root,
                      GitOverleafRemoteTable* remote, GitOverleafError* err) {
  size_t file_count = 0;
  for (size_t i = 0; i < local->len; i++) {
    if (!local->items[i].is_dir) {
      file_count++;
    }
  }
  GitOverleafLocalEntry** files =
      file_count ? calloc(file_count, sizeof(GitOverleafLocalEntry*)) : NULL;
  if (file_count && !files) {
    return git_overleaf_error(err, "out of memory");
  }
  size_t file_index = 0;
  for (size_t i = 0; i < local->len; i++) {
    if (!local->items[i].is_dir) {
      files[file_index++] = &local->items[i];
    }
  }
  qsort(files, file_count, sizeof(GitOverleafLocalEntry*),
        compare_file_entries);

  for (size_t i = 0; i < file_count; i++) {
    GitOverleafLocalEntry* entry = files[i];
    GitOverleafRemoteEntity* remote_entry =
        git_overleaf_remote_table_find(remote, entry->path);
    char* remote_file = git_overleaf_path_join(remote_root, entry->path);
    if (!remote_file) {
      free(files);
      return git_overleaf_error(err, "out of memory");
    }
    int same_content = 0;
    if (remote_entry && remote_entry->type != GIT_OVERLEAF_REMOTE_FOLDER &&
        git_overleaf_files_equal(entry->full_path, remote_file, &same_content,
                                 err) != 0) {
      free(remote_file);
      free(files);
      return -1;
    }
    if (!same_content) {
      if (remote_entry && remote_entry->type == GIT_OVERLEAF_REMOTE_DOC) {
        char* before = NULL;
        char* after = NULL;
        if (git_overleaf_read_file(remote_file, &before, err) != 0 ||
            git_overleaf_read_file(entry->full_path, &after, err) != 0 ||
            git_overleaf_overleaf_update_doc_text_content(
                cfg, project_id, remote_entry->id, before, after, err) != 0) {
          free(before);
          free(after);
          free(remote_file);
          free(files);
          return -1;
        }
        free(before);
        free(after);
      } else {
        if (remote_entry) {
          if (git_overleaf_overleaf_delete_entity(cfg, project_id, remote_entry,
                                                  err) != 0) {
            free(remote_file);
            free(files);
            return -1;
          }
          if (remote_entry->type == GIT_OVERLEAF_REMOTE_FOLDER) {
            git_overleaf_remote_table_forget_prefix(remote, entry->path);
          } else {
            git_overleaf_remote_table_remove(remote, entry->path);
          }
        }
        char* parent = parent_path(entry->path);
        if (!parent) {
          free(remote_file);
          free(files);
          return git_overleaf_error(err, "out of memory");
        }
        GitOverleafRemoteEntity* parent_entry =
            git_overleaf_remote_table_find(remote, parent);
        if (!parent_entry) {
          free(parent);
          free(remote_file);
          free(files);
          return git_overleaf_error(
              err, "could not find remote parent folder for %s", entry->path);
        }
        char* id = NULL;
        GitOverleafRemoteEntityType type = GIT_OVERLEAF_REMOTE_FILE;
        if (git_overleaf_overleaf_upload_file(
                cfg, project_id, parent_entry->id, path_basename(entry->path),
                entry->full_path, &id, &type, err) != 0) {
          free(parent);
          free(remote_file);
          free(files);
          return -1;
        }
        int rc = git_overleaf_remote_table_upsert(
            remote, entry->path, path_basename(entry->path), id, type,
            parent_entry->id, err);
        free(id);
        free(parent);
        if (rc != 0) {
          free(remote_file);
          free(files);
          return -1;
        }
      }
    }
    free(remote_file);
  }
  free(files);
  return 0;
}

static int sync_delete_remote_only(const GitOverleafConfig* cfg,
                                   const char* project_id,
                                   GitOverleafLocalTable* local,
                                   GitOverleafRemoteTable* remote,
                                   GitOverleafError* err) {
  GitOverleafDeleteEntry* deletes = NULL;
  size_t delete_len = 0;
  size_t delete_cap = 0;
  for (size_t i = 0; i < remote->len; i++) {
    GitOverleafRemoteEntity* entity = &remote->items[i];
    if (!*entity->path ||
        strcmp(entity->path, GIT_OVERLEAF_SYNC_METADATA_FILE) == 0) {
      continue;
    }
    int local_present =
        local_table_find(local, entity->path,
                         entity->type == GIT_OVERLEAF_REMOTE_FOLDER) != NULL;
    if (!local_present) {
      if (append_delete_entry(&deletes, &delete_len, &delete_cap, entity->path,
                              entity->type == GIT_OVERLEAF_REMOTE_FOLDER,
                              err) != 0) {
        delete_entries_free(deletes, delete_len);
        return -1;
      }
    }
  }
  qsort(deletes, delete_len, sizeof(GitOverleafDeleteEntry),
        compare_delete_entries);
  for (size_t i = 0; i < delete_len; i++) {
    GitOverleafRemoteEntity* entity =
        git_overleaf_remote_table_find(remote, deletes[i].path);
    if (!entity) {
      continue;
    }
    if (git_overleaf_overleaf_delete_entity(cfg, project_id, entity, err) !=
        0) {
      delete_entries_free(deletes, delete_len);
      return -1;
    }
    if (entity->type == GIT_OVERLEAF_REMOTE_FOLDER) {
      git_overleaf_remote_table_forget_prefix(remote, deletes[i].path);
    } else {
      git_overleaf_remote_table_remove(remote, deletes[i].path);
    }
  }
  delete_entries_free(deletes, delete_len);
  return 0;
}

static int sync_local_tree_to_overleaf(const GitOverleafConfig* cfg,
                                       const char* project_id,
                                       const char* local_root,
                                       const char* remote_root,
                                       GitOverleafRemoteTable* remote,
                                       GitOverleafError* err) {
  GitOverleafLocalTable local;
  if (scan_local_tree(local_root, &local, err) != 0) {
    return -1;
  }
  int rc = 0;
  if (sync_create_directories(cfg, project_id, &local, remote, err) != 0 ||
      sync_files(cfg, project_id, &local, remote_root, remote, err) != 0 ||
      sync_delete_remote_only(cfg, project_id, &local, remote, err) != 0) {
    rc = -1;
  }
  local_table_free(&local);
  return rc;
}

static char* sync_metadata_json(const GitOverleafConfig* cfg, const char* repo,
                                const char* revision, const char* project_id,
                                GitOverleafError* err) {
  char* commit = NULL;
  char* tree = NULL;
  char time_text[64];
  time_t now = time(NULL);
  struct tm tm_value;
  gmtime_r(&now, &tm_value);
  strftime(time_text, sizeof(time_text), "%Y-%m-%dT%H:%M:%SZ", &tm_value);
  if (git_overleaf_git_rev_parse(cfg, repo, revision, &commit, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, commit, &tree, err) != 0) {
    free(commit);
    free(tree);
    return NULL;
  }
  json_t* root = json_pack("{s:i,s:s,s:s,s:s,s:s,s:s,s:s}", "schema", 1, "tool",
                           "git-overleaf-cli", "projectId", project_id,
                           "overleafUrl", cfg->url, "localCommit", commit,
                           "localTree", tree, "syncedAt", time_text);
  free(commit);
  free(tree);
  if (!root) {
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  char* compact = json_dumps(root, JSON_COMPACT);
  json_decref(root);
  if (!compact) {
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  size_t len = strlen(compact);
  char* text = malloc(len + 2);
  if (!text) {
    free(compact);
    git_overleaf_error(err, "out of memory");
    return NULL;
  }
  memcpy(text, compact, len);
  text[len] = '\n';
  text[len + 1] = '\0';
  free(compact);
  return text;
}

static int upload_sync_metadata(const GitOverleafConfig* cfg, const char* repo,
                                const char* revision, const char* project_id,
                                GitOverleafRemoteTable* remote,
                                const char* remote_metadata_text,
                                GitOverleafError* err) {
  char* metadata = sync_metadata_json(cfg, repo, revision, project_id, err);
  if (!metadata) {
    return -1;
  }
  GitOverleafRemoteEntity* root = git_overleaf_remote_table_find(remote, "");
  if (!root) {
    free(metadata);
    return git_overleaf_error(err,
                              "could not find remote Overleaf root folder");
  }
  GitOverleafRemoteEntity* existing =
      git_overleaf_remote_table_find(remote, GIT_OVERLEAF_SYNC_METADATA_FILE);
  if (existing && existing->type == GIT_OVERLEAF_REMOTE_DOC &&
      remote_metadata_text) {
    int rc = git_overleaf_overleaf_update_doc_text_content(
        cfg, project_id, existing->id, remote_metadata_text, metadata, err);
    free(metadata);
    return rc;
  }

  char* temp_file = NULL;
  if (git_overleaf_make_temp_file(&temp_file, err) != 0) {
    free(metadata);
    return -1;
  }
  FILE* file = fopen(temp_file, "wb");
  if (!file) {
    int saved = errno;
    free(metadata);
    free(temp_file);
    return git_overleaf_error(err, "could not create sync metadata: %s",
                              strerror(saved));
  }
  fputs(metadata, file);
  fclose(file);
  free(metadata);

  if (existing) {
    if (git_overleaf_overleaf_delete_entity(cfg, project_id, existing, err) !=
        0) {
      unlink(temp_file);
      free(temp_file);
      return -1;
    }
    git_overleaf_remote_table_remove(remote, GIT_OVERLEAF_SYNC_METADATA_FILE);
  }
  char* id = NULL;
  GitOverleafRemoteEntityType type = GIT_OVERLEAF_REMOTE_FILE;
  if (git_overleaf_overleaf_upload_file(cfg, project_id, root->id,
                                        GIT_OVERLEAF_SYNC_METADATA_FILE,
                                        temp_file, &id, &type, err) != 0) {
    unlink(temp_file);
    free(temp_file);
    return -1;
  }
  unlink(temp_file);
  free(temp_file);
  int rc = git_overleaf_remote_table_upsert(
      remote, GIT_OVERLEAF_SYNC_METADATA_FILE, GIT_OVERLEAF_SYNC_METADATA_FILE,
      id, type, root->id, err);
  free(id);
  return rc;
}

static int upload_head_and_set_base(const GitOverleafConfig* cfg,
                                    const char* repo, const char* head,
                                    const char* project_id,
                                    const char* remote_root,
                                    GitOverleafRemoteTable* remote,
                                    const char* remote_metadata_text,
                                    GitOverleafError* err) {
  char* local_root = NULL;
  if (git_overleaf_git_materialize_commit(cfg, repo, head, &local_root, err) !=
      0) {
    return -1;
  }
  int rc = 0;
  if (sync_local_tree_to_overleaf(cfg, project_id, local_root, remote_root,
                                  remote, err) != 0 ||
      upload_sync_metadata(cfg, repo, head, project_id, remote,
                           remote_metadata_text, err) != 0 ||
      git_overleaf_git_set_base_ref(cfg, repo, head, err) != 0) {
    rc = -1;
  }
  GitOverleafError ignored = {{0}};
  git_overleaf_remove_tree(local_root, &ignored);
  free(local_root);
  return rc;
}

int git_overleaf_overleaf_pull(const GitOverleafConfig* cfg,
                               const char* repo_arg, GitOverleafError* err) {
  char* repo = NULL;
  char* repo_url = NULL;
  char* project_id = NULL;
  char* project_name = NULL;
  char* branch = NULL;
  char* pending = NULL;
  char* base = NULL;
  char* head = NULL;
  char* remote_commit = NULL;
  char* base_tree = NULL;
  char* head_tree = NULL;
  char* remote_tree = NULL;
  GitOverleafSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));

  if (git_overleaf_git_root(cfg, repo_arg ? repo_arg : ".", &repo, err) != 0 ||
      repo_project_id(cfg, repo, &project_id, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.projectName",
                                  &project_name, err) != 0 ||
      git_overleaf_git_current_branch(cfg, repo, &branch, err) != 0 ||
      git_overleaf_git_is_clean(cfg, repo, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.pendingAction",
                                  &pending, err) != 0) {
    goto fail;
  }
  if (pending && *pending) {
    git_overleaf_error(
        err, "unresolved pending Overleaf %s exists; finish it first", pending);
    goto fail;
  }

  GitOverleafConfig local_cfg;
  if (apply_repo_url(cfg, repo, &local_cfg, &repo_url, err) != 0) {
    goto fail;
  }
  if (git_overleaf_overleaf_download_snapshot(&local_cfg, project_id, &snapshot,
                                              err) != 0 ||
      git_overleaf_git_rev_parse(cfg, repo, GIT_OVERLEAF_BASE_REF, &base,
                                 err) != 0 ||
      git_overleaf_git_rev_parse(cfg, repo, "HEAD", &head, err) != 0) {
    goto fail;
  }

  char message[128];
  time_t now = time(NULL);
  struct tm tm_value;
  localtime_r(&now, &tm_value);
  strftime(message, sizeof(message),
           "overleaf: remote snapshot %Y-%m-%d %H:%M:%S", &tm_value);
  /* The remote snapshot is materialized as a commit whose parent is the last
     known base, making a normal Git merge sufficient for pull semantics. */
  if (git_overleaf_git_commit_directory(cfg, repo, snapshot.root, base, message,
                                        &remote_commit, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, base, &base_tree, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, head, &head_tree, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, remote_commit, &remote_tree, err) !=
          0) {
    goto fail;
  }

  switch (git_overleaf_overleaf_sync_state(base_tree, head_tree, remote_tree)) {
    case GIT_OVERLEAF_SYNC_IN_SYNC:
      printf("Project `%s' is already in sync\n",
             project_name && *project_name ? project_name : project_id);
      break;
    case GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE:
      if (git_overleaf_git_set_base_ref(cfg, repo, head, err) != 0) {
        goto fail;
      }
      printf("Local and remote content match; base ref updated\n");
      break;
    case GIT_OVERLEAF_SYNC_REMOTE_MATCHES_BASE:
      printf("No remote Overleaf changes to pull into `%s'\n", branch);
      break;
    case GIT_OVERLEAF_SYNC_HEAD_MATCHES_BASE: {
      const char* merge_args[] = {"merge", "--ff-only", remote_commit};
      if (git_overleaf_git_ok(cfg, repo, merge_args, 3, NULL, err) != 0 ||
          git_overleaf_git_set_base_ref(cfg, repo, "HEAD", err) != 0) {
        goto fail;
      }
      printf("Pulled remote Overleaf changes into `%s'\n", branch);
      break;
    }
    case GIT_OVERLEAF_SYNC_DIVERGED: {
      const char* merge_args[] = {"merge", "--no-ff", "--no-edit",
                                  remote_commit};
      GitOverleafProcessResult merge_result;
      if (git_overleaf_git_capture(cfg, repo, merge_args, 4, NULL, 1,
                                   &merge_result, err) != 0) {
        goto fail;
      }
      if (merge_result.status == 0) {
        git_overleaf_process_result_free(&merge_result);
        if (git_overleaf_git_set_base_ref(cfg, repo, remote_commit, err) != 0) {
          goto fail;
        }
        printf("Pulled Overleaf changes into `%s'\n", branch);
      } else {
        git_overleaf_process_result_free(&merge_result);
        /* Keep enough local config to resume or inspect the interrupted sync
           after the user resolves the Git merge conflict. */
        if (git_overleaf_git_config_set(cfg, repo,
                                        "git-overleaf.pendingRemoteCommit",
                                        remote_commit, err) != 0 ||
            git_overleaf_git_config_set(cfg, repo, "git-overleaf.pendingAction",
                                        "pull", err) != 0) {
          goto fail;
        }
        printf(
            "Merge conflict on `%s'. Resolve conflicts, commit, then push "
            "with git-overleaf-cli push.\n",
            branch);
      }
      break;
    }
  }

  git_overleaf_snapshot_free(&snapshot);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(branch);
  free(pending);
  free(base);
  free(head);
  free(remote_commit);
  free(base_tree);
  free(head_tree);
  free(remote_tree);
  return 0;

fail:
  git_overleaf_snapshot_free(&snapshot);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(branch);
  free(pending);
  free(base);
  free(head);
  free(remote_commit);
  free(base_tree);
  free(head_tree);
  free(remote_tree);
  return -1;
}

static int load_remote_sync_context(
    const GitOverleafConfig* cfg, const char* repo, const char* project_id,
    GitOverleafSnapshot* snapshot, GitOverleafRemoteTable* remote_table,
    char** base, char** head, char** remote_commit, char** base_tree,
    char** head_tree, char** remote_tree, GitOverleafError* err) {
  memset(snapshot, 0, sizeof(*snapshot));
  memset(remote_table, 0, sizeof(*remote_table));
  if (git_overleaf_overleaf_download_snapshot(cfg, project_id, snapshot, err) !=
          0 ||
      git_overleaf_overleaf_fetch_remote_table(cfg, project_id, remote_table,
                                               err) != 0 ||
      git_overleaf_git_rev_parse(cfg, repo, GIT_OVERLEAF_BASE_REF, base, err) !=
          0 ||
      git_overleaf_git_rev_parse(cfg, repo, "HEAD", head, err) != 0) {
    return -1;
  }

  char message[128];
  time_t now = time(NULL);
  struct tm tm_value;
  localtime_r(&now, &tm_value);
  strftime(message, sizeof(message),
           "overleaf: remote snapshot %Y-%m-%d %H:%M:%S", &tm_value);
  if (git_overleaf_git_commit_directory(cfg, repo, snapshot->root, *base,
                                        message, remote_commit, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, *base, base_tree, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, *head, head_tree, err) != 0 ||
      git_overleaf_git_tree_id(cfg, repo, *remote_commit, remote_tree, err) !=
          0) {
    return -1;
  }
  return 0;
}

static void free_remote_sync_context(GitOverleafSnapshot* snapshot,
                                     GitOverleafRemoteTable* remote_table,
                                     char* base, char* head,
                                     char* remote_commit, char* base_tree,
                                     char* head_tree, char* remote_tree) {
  git_overleaf_snapshot_free(snapshot);
  git_overleaf_remote_table_free(remote_table);
  free(base);
  free(head);
  free(remote_commit);
  free(base_tree);
  free(head_tree);
  free(remote_tree);
}

int git_overleaf_overleaf_push(const GitOverleafConfig* cfg,
                               const char* repo_arg, int stage_all,
                               GitOverleafError* err) {
  char* repo = NULL;
  char* repo_url = NULL;
  char* project_id = NULL;
  char* project_name = NULL;
  char* branch = NULL;
  char* pending = NULL;
  char* pending_remote_commit = NULL;
  char* base = NULL;
  char* head = NULL;
  char* remote_commit = NULL;
  char* base_tree = NULL;
  char* head_tree = NULL;
  char* remote_tree = NULL;
  GitOverleafSnapshot snapshot;
  GitOverleafRemoteTable remote_table;
  memset(&snapshot, 0, sizeof(snapshot));
  memset(&remote_table, 0, sizeof(remote_table));

  if (git_overleaf_git_root(cfg, repo_arg ? repo_arg : ".", &repo, err) != 0 ||
      repo_project_id(cfg, repo, &project_id, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.projectName",
                                  &project_name, err) != 0 ||
      git_overleaf_git_current_branch(cfg, repo, &branch, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.pendingAction",
                                  &pending, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.pendingRemoteCommit",
                                  &pending_remote_commit, err) != 0) {
    goto fail;
  }

  GitOverleafConfig local_cfg;
  if (apply_repo_url(cfg, repo, &local_cfg, &repo_url, err) != 0) {
    goto fail;
  }

  if (pending && *pending) {
    if (strcmp(pending, "pull") != 0) {
      git_overleaf_error(err, "unsupported pending Overleaf action: %s",
                         pending);
      goto fail;
    }
    if (git_overleaf_git_is_clean(cfg, repo, err) != 0) {
      goto fail;
    }
  } else if (prepare_working_tree_for_push(cfg, repo, stage_all, err) != 0) {
    goto fail;
  }

  if (load_remote_sync_context(
          &local_cfg, repo, project_id, &snapshot, &remote_table, &base, &head,
          &remote_commit, &base_tree, &head_tree, &remote_tree, err) != 0) {
    goto fail;
  }

  if (pending && *pending) {
    if (!pending_remote_commit || !*pending_remote_commit) {
      git_overleaf_error(err, "pending pull metadata is incomplete");
      goto fail;
    }
    char* pending_tree = NULL;
    int ancestor = 0;
    if (git_overleaf_git_tree_id(cfg, repo, pending_remote_commit,
                                 &pending_tree, err) != 0 ||
        git_overleaf_git_is_ancestor(cfg, repo, pending_remote_commit, head,
                                     &ancestor, err) != 0) {
      free(pending_tree);
      goto fail;
    }
    if (strcmp(pending_tree, remote_tree) != 0) {
      free(pending_tree);
      git_overleaf_error(
          err,
          "remote project changed again while pending pull was unresolved; "
          "start a new pull");
      goto fail;
    }
    free(pending_tree);
    if (!ancestor) {
      git_overleaf_error(
          err, "merge is not complete; resolve conflicts and commit first");
      goto fail;
    }
    if (upload_head_and_set_base(&local_cfg, repo, head, project_id,
                                 snapshot.root, &remote_table,
                                 snapshot.metadata_text, err) != 0 ||
        clear_pending(cfg, repo, err) != 0) {
      goto fail;
    }
    printf("Pushed merged Overleaf pull for `%s'\n",
           project_name && *project_name ? project_name : project_id);
  } else {
    switch (
        git_overleaf_overleaf_sync_state(base_tree, head_tree, remote_tree)) {
      case GIT_OVERLEAF_SYNC_IN_SYNC:
        if (upload_sync_metadata(&local_cfg, repo, head, project_id,
                                 &remote_table, snapshot.metadata_text,
                                 err) != 0) {
          goto fail;
        }
        printf("Project `%s' is already in sync\n",
               project_name && *project_name ? project_name : project_id);
        break;
      case GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE:
        if (upload_sync_metadata(&local_cfg, repo, head, project_id,
                                 &remote_table, snapshot.metadata_text,
                                 err) != 0 ||
            git_overleaf_git_set_base_ref(cfg, repo, head, err) != 0) {
          goto fail;
        }
        printf("Local and remote content match; base ref updated\n");
        break;
      case GIT_OVERLEAF_SYNC_REMOTE_MATCHES_BASE:
        if (upload_head_and_set_base(&local_cfg, repo, head, project_id,
                                     snapshot.root, &remote_table,
                                     snapshot.metadata_text, err) != 0) {
          goto fail;
        }
        printf("Pushed `%s' to Overleaf\n",
               project_name && *project_name ? project_name : project_id);
        break;
      case GIT_OVERLEAF_SYNC_HEAD_MATCHES_BASE:
      case GIT_OVERLEAF_SYNC_DIVERGED:
        git_overleaf_error(
            err, "remote Overleaf changes exist for `%s'; run pull first",
            branch);
        goto fail;
    }
  }

  free_remote_sync_context(&snapshot, &remote_table, base, head, remote_commit,
                           base_tree, head_tree, remote_tree);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(branch);
  free(pending);
  free(pending_remote_commit);
  return 0;

fail:
  free_remote_sync_context(&snapshot, &remote_table, base, head, remote_commit,
                           base_tree, head_tree, remote_tree);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(branch);
  free(pending);
  free(pending_remote_commit);
  return -1;
}

int git_overleaf_overleaf_overwrite(const GitOverleafConfig* cfg,
                                    const char* repo_arg, int stage_all,
                                    GitOverleafError* err) {
  char* repo = NULL;
  char* repo_url = NULL;
  char* project_id = NULL;
  char* project_name = NULL;
  char* pending = NULL;
  char* base = NULL;
  char* head = NULL;
  char* remote_commit = NULL;
  char* base_tree = NULL;
  char* head_tree = NULL;
  char* remote_tree = NULL;
  GitOverleafSnapshot snapshot;
  GitOverleafRemoteTable remote_table;
  memset(&snapshot, 0, sizeof(snapshot));
  memset(&remote_table, 0, sizeof(remote_table));

  if (git_overleaf_git_root(cfg, repo_arg ? repo_arg : ".", &repo, err) != 0 ||
      repo_project_id(cfg, repo, &project_id, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.projectName",
                                  &project_name, err) != 0 ||
      git_overleaf_git_config_get(cfg, repo, "git-overleaf.pendingAction",
                                  &pending, err) != 0) {
    goto fail;
  }
  if (pending && *pending) {
    git_overleaf_error(
        err, "pending Overleaf %s exists; finish it before overwrite", pending);
    goto fail;
  }

  GitOverleafConfig local_cfg;
  if (apply_repo_url(cfg, repo, &local_cfg, &repo_url, err) != 0 ||
      prepare_working_tree_for_push(cfg, repo, stage_all, err) != 0 ||
      load_remote_sync_context(
          &local_cfg, repo, project_id, &snapshot, &remote_table, &base, &head,
          &remote_commit, &base_tree, &head_tree, &remote_tree, err) != 0) {
    goto fail;
  }

  GitOverleafSyncState state =
      git_overleaf_overleaf_sync_state(base_tree, head_tree, remote_tree);
  if (state == GIT_OVERLEAF_SYNC_IN_SYNC ||
      state == GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE) {
    if (upload_sync_metadata(&local_cfg, repo, head, project_id, &remote_table,
                             snapshot.metadata_text, err) != 0 ||
        git_overleaf_git_set_base_ref(cfg, repo, head, err) != 0) {
      goto fail;
    }
    printf("Local and remote content match; base ref updated\n");
  } else {
    if (upload_head_and_set_base(&local_cfg, repo, head, project_id,
                                 snapshot.root, &remote_table,
                                 snapshot.metadata_text, err) != 0) {
      goto fail;
    }
    printf("Overwrote Overleaf project `%s' with local HEAD\n",
           project_name && *project_name ? project_name : project_id);
  }

  free_remote_sync_context(&snapshot, &remote_table, base, head, remote_commit,
                           base_tree, head_tree, remote_tree);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(pending);
  return 0;

fail:
  free_remote_sync_context(&snapshot, &remote_table, base, head, remote_commit,
                           base_tree, head_tree, remote_tree);
  free(repo);
  free(repo_url);
  free(project_id);
  free(project_name);
  free(pending);
  return -1;
}

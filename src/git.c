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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "git-overleaf-cli.h"

static char** build_git_argv(const GitOverleafConfig* cfg, const char* repo,
                             const char* const args[], size_t argc) {
  size_t extra = repo ? 3 : 1;
  char** argv = calloc(extra + argc + 1, sizeof(char*));
  if (!argv) {
    return NULL;
  }
  size_t index = 0;
  argv[index++] = cfg->git ? cfg->git : "git";
  if (repo) {
    argv[index++] = "-C";
    argv[index++] = (char*)repo;
  }
  for (size_t i = 0; i < argc; i++) {
    argv[index++] = (char*)args[i];
  }
  argv[index] = NULL;
  return argv;
}

int git_overleaf_git_capture(const GitOverleafConfig* cfg, const char* repo,
                             const char* const args[], size_t argc,
                             char* const env[], int allow_failure,
                             GitOverleafProcessResult* out, GitOverleafError* err) {
  char** argv = build_git_argv(cfg, repo, args, argc);
  if (!argv) {
    return git_overleaf_error(err, "out of memory");
  }
  int rc = git_overleaf_process_run(argv, NULL, env, allow_failure, out, err);
  free(argv);
  return rc;
}

int git_overleaf_git_output(const GitOverleafConfig* cfg, const char* repo,
                            const char* const args[], size_t argc,
                            char* const env[], char** out,
                            GitOverleafError* err) {
  *out = NULL;
  GitOverleafProcessResult result;
  if (git_overleaf_git_capture(cfg, repo, args, argc, env, 0, &result, err) !=
      0) {
    return -1;
  }
  *out = result.output;
  result.output = NULL;
  git_overleaf_process_result_free(&result);
  return 0;
}

int git_overleaf_git_ok(const GitOverleafConfig* cfg, const char* repo,
                        const char* const args[], size_t argc,
                        char* const env[], GitOverleafError* err) {
  GitOverleafProcessResult result;
  int rc =
      git_overleaf_git_capture(cfg, repo, args, argc, env, 0, &result, err);
  git_overleaf_process_result_free(&result);
  return rc;
}

int git_overleaf_git_config_get(const GitOverleafConfig* cfg, const char* repo,
                                const char* key, char** out,
                                GitOverleafError* err) {
  const char* args[] = {"config", "--local", "--get", key};
  GitOverleafProcessResult result;
  /* A missing local config key is represented as NULL, not as a command
     failure, because several callers use absence as default-state input. */
  if (git_overleaf_git_capture(cfg, repo, args, 4, NULL, 1, &result, err) !=
      0) {
    return -1;
  }
  if (result.status == 0 && result.output && *result.output) {
    *out = result.output;
    result.output = NULL;
  } else {
    *out = NULL;
  }
  git_overleaf_process_result_free(&result);
  return 0;
}

int git_overleaf_git_config_set(const GitOverleafConfig* cfg, const char* repo,
                                const char* key, const char* value,
                                GitOverleafError* err) {
  const char* args[] = {"config", "--local", key, value};
  return git_overleaf_git_ok(cfg, repo, args, 4, NULL, err);
}

int git_overleaf_git_root(const GitOverleafConfig* cfg, const char* directory,
                          char** out, GitOverleafError* err) {
  const char* args[] = {"rev-parse", "--show-toplevel"};
  GitOverleafProcessResult result;
  if (git_overleaf_git_capture(cfg, directory ? directory : ".", args, 2, NULL,
                               1, &result, err) != 0) {
    return -1;
  }
  if (result.status != 0 || !result.output || !*result.output) {
    git_overleaf_process_result_free(&result);
    return git_overleaf_error(err, "not inside a Git repository");
  }
  *out = result.output;
  result.output = NULL;
  git_overleaf_process_result_free(&result);
  return 0;
}

int git_overleaf_git_current_branch(const GitOverleafConfig* cfg, const char* repo,
                                    char** out, GitOverleafError* err) {
  const char* args[] = {"branch", "--show-current"};
  if (git_overleaf_git_output(cfg, repo, args, 2, NULL, out, err) != 0) {
    return -1;
  }
  if (!*out || !**out) {
    free(*out);
    *out = NULL;
    return git_overleaf_error(err, "detached HEAD is not supported");
  }
  return 0;
}

int git_overleaf_git_rev_parse(const GitOverleafConfig* cfg, const char* repo,
                               const char* revision, char** out,
                               GitOverleafError* err) {
  const char* args[] = {"rev-parse", revision};
  return git_overleaf_git_output(cfg, repo, args, 2, NULL, out, err);
}

int git_overleaf_git_rev_parse_verify(const GitOverleafConfig* cfg, const char* repo,
                                      const char* revision, char** out,
                                      GitOverleafError* err) {
  const char* args[] = {"rev-parse", "--verify", revision};
  GitOverleafProcessResult result;
  /* The base ref is optional during first-time init, so verification failure
     becomes a NULL revision rather than a hard error. */
  if (git_overleaf_git_capture(cfg, repo, args, 3, NULL, 1, &result, err) !=
      0) {
    return -1;
  }
  if (result.status == 0 && result.output && *result.output) {
    *out = result.output;
    result.output = NULL;
  } else {
    *out = NULL;
  }
  git_overleaf_process_result_free(&result);
  return 0;
}

int git_overleaf_git_tree_id(const GitOverleafConfig* cfg, const char* repo,
                             const char* revision, char** out,
                             GitOverleafError* err) {
  size_t len = strlen(revision) + strlen("^{tree}") + 1;
  char* spec = malloc(len);
  if (!spec) {
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(spec, len, "%s^{tree}", revision);
  const char* args[] = {"rev-parse", spec};
  int rc = git_overleaf_git_output(cfg, repo, args, 2, NULL, out, err);
  free(spec);
  return rc;
}

int git_overleaf_git_is_clean(const GitOverleafConfig* cfg, const char* repo,
                              GitOverleafError* err) {
  const char* args[] = {"status", "--porcelain"};
  char* out = NULL;
  if (git_overleaf_git_output(cfg, repo, args, 2, NULL, &out, err) != 0) {
    return -1;
  }
  int clean = !out || !*out;
  free(out);
  if (!clean) {
    return git_overleaf_error(
        err, "repository has local changes; commit or stash them first");
  }
  return 0;
}

int git_overleaf_git_is_ancestor(const GitOverleafConfig* cfg, const char* repo,
                                 const char* ancestor, const char* descendant,
                                 int* is_ancestor, GitOverleafError* err) {
  const char* args[] = {"merge-base", "--is-ancestor", ancestor, descendant};
  GitOverleafProcessResult result;
  if (git_overleaf_git_capture(cfg, repo, args, 4, NULL, 1, &result, err) !=
      0) {
    return -1;
  }
  *is_ancestor = result.status == 0;
  git_overleaf_process_result_free(&result);
  return 0;
}

static int git_identity_args(const GitOverleafConfig* cfg, const char* repo,
                             const char*** args_out, size_t* argc_out,
                             GitOverleafError* err) {
  static const char* placeholder[] = {"-c", "user.name=Overleaf Project", "-c",
                                      "user.email=git-overleaf@local"};
  const char* name_args[] = {"config", "--get", "user.name"};
  const char* email_args[] = {"config", "--get", "user.email"};
  GitOverleafProcessResult name;
  GitOverleafProcessResult email;
  if (git_overleaf_git_capture(cfg, repo, name_args, 3, NULL, 1, &name, err) !=
      0) {
    return -1;
  }
  if (git_overleaf_git_capture(cfg, repo, email_args, 3, NULL, 1, &email,
                               err) != 0) {
    git_overleaf_process_result_free(&name);
    return -1;
  }
  int configured = name.status == 0 && email.status == 0 && name.output &&
                   *name.output && email.output && *email.output;
  git_overleaf_process_result_free(&name);
  git_overleaf_process_result_free(&email);
  if (configured) {
    *args_out = NULL;
    *argc_out = 0;
  } else {
    *args_out = placeholder;
    *argc_out = 4;
  }
  return 0;
}

int git_overleaf_git_commit_directory(const GitOverleafConfig* cfg, const char* repo,
                                      const char* directory, const char* parent,
                                      const char* message, char** commit_out,
                                      GitOverleafError* err) {
  *commit_out = NULL;
  char* index_file = NULL;
  if (git_overleaf_make_temp_file(&index_file, err) != 0) {
    return -1;
  }
  unlink(index_file);
  /* Build the snapshot commit with an isolated index. This avoids touching the
     user's staged changes while still writing objects into the target repo. */
  size_t env_len = strlen("GIT_INDEX_FILE=") + strlen(index_file) + 1;
  char* env_index = malloc(env_len);
  if (!env_index) {
    free(index_file);
    return git_overleaf_error(err, "out of memory");
  }
  snprintf(env_index, env_len, "GIT_INDEX_FILE=%s", index_file);
  char* env[] = {env_index, NULL};

  char* git_dir = git_overleaf_path_join(repo, ".git");
  if (!git_dir) {
    free(env_index);
    free(index_file);
    return git_overleaf_error(err, "out of memory");
  }
  const char* add_args[] = {
      "--git-dir", git_dir, "--work-tree", directory, "add", "--all", "."};
  if (git_overleaf_git_ok(cfg, NULL, add_args, 7, env, err) != 0) {
    free(git_dir);
    free(env_index);
    free(index_file);
    return -1;
  }

  const char* tree_args[] = {"write-tree"};
  char* tree = NULL;
  if (git_overleaf_git_output(cfg, repo, tree_args, 1, env, &tree, err) != 0) {
    free(git_dir);
    free(env_index);
    free(index_file);
    return -1;
  }

  const char** identity = NULL;
  size_t identity_argc = 0;
  if (git_identity_args(cfg, repo, &identity, &identity_argc, err) != 0) {
    free(tree);
    free(git_dir);
    free(env_index);
    free(index_file);
    return -1;
  }

  size_t argc = identity_argc + 2 + (parent ? 2 : 0) + 2;
  const char** commit_args = calloc(argc, sizeof(char*));
  if (!commit_args) {
    free(tree);
    free(git_dir);
    free(env_index);
    free(index_file);
    return git_overleaf_error(err, "out of memory");
  }
  size_t i = 0;
  for (size_t j = 0; j < identity_argc; j++) {
    commit_args[i++] = identity[j];
  }
  commit_args[i++] = "commit-tree";
  commit_args[i++] = tree;
  if (parent) {
    commit_args[i++] = "-p";
    commit_args[i++] = parent;
  }
  commit_args[i++] = "-m";
  commit_args[i++] = message;

  /* commit-tree returns the new commit ID without updating any refs; callers
     decide whether this synthetic snapshot becomes the base or is merged. */
  int rc = git_overleaf_git_output(cfg, repo, commit_args, argc, env,
                                   commit_out, err);
  free(commit_args);
  free(tree);
  free(git_dir);
  unlink(index_file);
  free(env_index);
  free(index_file);
  return rc;
}

int git_overleaf_git_write_metadata(const GitOverleafConfig* cfg, const char* repo,
                                    const char* project_id,
                                    const char* project_name,
                                    GitOverleafError* err) {
  char* url = git_overleaf_sanitize_url(cfg->url);
  if (!url) {
    return git_overleaf_error(err, "out of memory");
  }
  int rc = 0;
  rc = rc || git_overleaf_git_config_set(cfg, repo, "git-overleaf.projectId",
                                         project_id, err);
  rc = rc || git_overleaf_git_config_set(
                 cfg, repo, "git-overleaf.projectName",
                 project_name ? project_name : project_id, err);
  rc = rc ||
       git_overleaf_git_config_set(cfg, repo, "git-overleaf.url", url, err);
  rc = rc || git_overleaf_git_config_set(cfg, repo, "git-overleaf.baseRef",
                                         GIT_OVERLEAF_BASE_REF, err);
  free(url);
  return rc ? -1 : 0;
}

int git_overleaf_git_set_base_ref(const GitOverleafConfig* cfg, const char* repo,
                                  const char* revision, GitOverleafError* err) {
  char* base_ref = NULL;
  if (git_overleaf_git_config_get(cfg, repo, "git-overleaf.baseRef", &base_ref,
                                  err) != 0) {
    return -1;
  }
  const char* ref = base_ref && *base_ref ? base_ref : GIT_OVERLEAF_BASE_REF;
  const char* args[] = {"update-ref", ref, revision};
  int rc = git_overleaf_git_ok(cfg, repo, args, 3, NULL, err);
  free(base_ref);
  return rc;
}

int git_overleaf_git_prepare_sync_metadata_repo(const char* repo,
                                                GitOverleafError* err) {
  char* git_dir = git_overleaf_path_join(repo, ".git");
  char* info_dir = git_dir ? git_overleaf_path_join(git_dir, "info") : NULL;
  char* exclude = info_dir ? git_overleaf_path_join(info_dir, "exclude") : NULL;
  if (!git_dir || !info_dir || !exclude) {
    free(git_dir);
    free(info_dir);
    free(exclude);
    return git_overleaf_error(err, "out of memory");
  }
  if (git_overleaf_ensure_directory(info_dir, err) != 0) {
    free(git_dir);
    free(info_dir);
    free(exclude);
    return -1;
  }
  /* The sync metadata file is tool-local state. Store the ignore rule in
     .git/info/exclude so the repository's tracked .gitignore is untouched. */
  char* text = NULL;
  GitOverleafError ignored = {{0}};
  if (git_overleaf_read_file(exclude, &text, &ignored) != 0) {
    text = git_overleaf_xstrdup("");
  }
  int present = 0;
  if (text) {
    char* copy = git_overleaf_xstrdup(text);
    if (!copy) {
      free(text);
      free(git_dir);
      free(info_dir);
      free(exclude);
      return git_overleaf_error(err, "out of memory");
    }
    for (char* line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
      if (strcmp(git_overleaf_trim(line), GIT_OVERLEAF_SYNC_METADATA_FILE) == 0) {
        present = 1;
        break;
      }
    }
    free(copy);
  }
  if (!present) {
    FILE* file = fopen(exclude, "ab");
    if (!file) {
      int rc = git_overleaf_error(err, "could not open %s", exclude);
      free(text);
      free(git_dir);
      free(info_dir);
      free(exclude);
      return rc;
    }
    if (text && *text && text[strlen(text) - 1] != '\n') {
      fputc('\n', file);
    }
    fputs(GIT_OVERLEAF_SYNC_METADATA_FILE "\n", file);
    fclose(file);
  }
  free(text);
  free(git_dir);
  free(info_dir);
  free(exclude);
  return 0;
}

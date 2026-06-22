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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "git-overleaf-cli.h"

static void usage(FILE* stream) {
  fprintf(
      stream,
      "git-overleaf-cli 0.1 MVP\n"
      "\n"
      "Usage:\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] auth --cookie COOKIE [--cookie-file "
      "FILE]\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] auth --from-firefox "
      "[--firefox-profile DIR] [--cookie-file FILE]\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] list\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] clone [--project-id ID] "
      "[--project-name NAME] [TARGET]\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] init --project-id ID "
      "[--project-name NAME] [--repo DIR]\n"
      "  git-overleaf-cli [GLOBAL-OPTIONS] pull [--repo DIR]\n"
      "\n"
      "Subcommands:\n"
      "  auth                Save or import Overleaf Cookie headers locally\n"
      "  list                List projects visible to the current cookie\n"
      "  clone               Download a project snapshot into a new Git repo\n"
      "  init                Bind an existing Git repo to an Overleaf project\n"
      "  pull                Merge the latest Overleaf snapshot into a repo\n"
      "\n"
      "Global options:\n"
      "  --url URL            Overleaf URL (default: "
      "https://www.overleaf.com)\n"
      "  --cookie COOKIE      Raw Cookie header for this run\n"
      "  --cookie-file FILE   Cookie file (default: ~/.git-overleaf-cookies)\n"
      "  --git PATH           Git executable (default: git)\n"
      "  --unzip PATH         Unzip executable (default: unzip)\n"
      "  --help               Show this help\n"
      "\n"
      "Auth options:\n"
      "  --from-firefox       Import cookies from the default Firefox profile\n"
      "  --firefox-profile DIR\n"
      "                       Import cookies from a specific Firefox profile\n"
      "\n"
      "No webdriver authentication is implemented in this MVP.\n");
}

static int need_value(int argc, char** argv, int index, GitOverleafError* err) {
  if (index + 1 >= argc) {
    return git_overleaf_error(err, "%s requires a value", argv[index]);
  }
  return 0;
}

static int parse_global(GitOverleafConfig* cfg, int argc, char** argv,
                        int* index, GitOverleafError* err) {
  while (*index < argc) {
    const char* arg = argv[*index];
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      usage(stdout);
      exit(0);
    } else if (strcmp(arg, "--url") == 0) {
      if (need_value(argc, argv, *index, err) != 0) {
        return -1;
      }
      free(cfg->url);
      cfg->url = git_overleaf_sanitize_url(argv[++(*index)]);
      cfg->url_explicit = 1;
      if (!cfg->url) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (strcmp(arg, "--cookie") == 0) {
      if (need_value(argc, argv, *index, err) != 0) {
        return -1;
      }
      free(cfg->cookie);
      cfg->cookie = git_overleaf_trimmed_dup(argv[++(*index)]);
      if (!cfg->cookie) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (strcmp(arg, "--cookie-file") == 0) {
      if (need_value(argc, argv, *index, err) != 0) {
        return -1;
      }
      free(cfg->cookie_file);
      cfg->cookie_file = git_overleaf_xstrdup(argv[++(*index)]);
      if (!cfg->cookie_file) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (strcmp(arg, "--git") == 0) {
      if (need_value(argc, argv, *index, err) != 0) {
        return -1;
      }
      free(cfg->git);
      cfg->git = git_overleaf_xstrdup(argv[++(*index)]);
      if (!cfg->git) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (strcmp(arg, "--unzip") == 0) {
      if (need_value(argc, argv, *index, err) != 0) {
        return -1;
      }
      free(cfg->unzip);
      cfg->unzip = git_overleaf_xstrdup(argv[++(*index)]);
      if (!cfg->unzip) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (arg[0] == '-') {
      return git_overleaf_error(err, "unknown global option: %s", arg);
    } else {
      break;
    }
    (*index)++;
  }
  return 0;
}

static int command_auth(GitOverleafConfig* cfg, int argc, char** argv,
                        GitOverleafError* err) {
  const char* cookie = cfg->cookie;
  char* imported_cookie = NULL;
  const char* firefox_profile = NULL;
  int from_firefox = 0;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--cookie") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      cookie = argv[++i];
    } else if (strcmp(argv[i], "--cookie-file") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      free(cfg->cookie_file);
      cfg->cookie_file = git_overleaf_xstrdup(argv[++i]);
      if (!cfg->cookie_file) {
        return git_overleaf_error(err, "out of memory");
      }
    } else if (strcmp(argv[i], "--from-firefox") == 0) {
      from_firefox = 1;
    } else if (strcmp(argv[i], "--firefox-profile") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      firefox_profile = argv[++i];
      from_firefox = 1;
    } else {
      return git_overleaf_error(err, "unknown auth option: %s", argv[i]);
    }
  }
  if (from_firefox && cookie && *cookie) {
    return git_overleaf_error(
        err, "auth accepts either --cookie or --from-firefox, not both");
  }
  if (from_firefox) {
    if (git_overleaf_firefox_cookie_header(cfg, firefox_profile,
                                           &imported_cookie, err) != 0) {
      return -1;
    }
    cookie = imported_cookie;
  }
  if (!cookie || !*cookie) {
    free(imported_cookie);
    return git_overleaf_error(err,
                              "auth requires --cookie COOKIE or "
                              "--from-firefox");
  }
  if (git_overleaf_write_private_file(
          cfg->cookie_file ? cfg->cookie_file : GIT_OVERLEAF_DEFAULT_COOKIE_FILE, cookie,
          err) != 0) {
    free(imported_cookie);
    return -1;
  }
  printf("%s Overleaf cookies to %s\n",
         from_firefox ? "Imported Firefox" : "Saved",
         cfg->cookie_file ? cfg->cookie_file : GIT_OVERLEAF_DEFAULT_COOKIE_FILE);
  free(imported_cookie);
  return 0;
}

static int command_list(GitOverleafConfig* cfg, GitOverleafError* err) {
  if (git_overleaf_config_load_cookie(cfg, err) != 0) {
    return -1;
  }
  GitOverleafProjectList projects;
  if (git_overleaf_overleaf_list_projects(cfg, &projects, err) != 0) {
    return -1;
  }
  printf("%-28s  %-40s  %s\n", "PROJECT ID", "NAME", "OWNER");
  for (size_t i = 0; i < projects.len; i++) {
    printf("%-28s  %-40s  %s\n", projects.items[i].id, projects.items[i].name,
           projects.items[i].owner_email);
  }
  git_overleaf_project_list_free(&projects);
  return 0;
}

static int clone_interactive_available(void) {
  return isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
}

static int read_prompt_line(const char* prompt, char* buffer, size_t size,
                            GitOverleafError* err) {
  fprintf(stderr, "%s", prompt);
  fflush(stderr);
  if (!fgets(buffer, (int)size, stdin)) {
    return git_overleaf_error(err, "project selection cancelled");
  }
  char* newline = strchr(buffer, '\n');
  if (newline) {
    *newline = '\0';
  } else {
    int c = 0;
    while ((c = getchar()) != '\n' && c != EOF) {
    }
  }
  return 0;
}

static int parse_display_selection(const char* text, size_t shown,
                                   size_t* selected_display) {
  if (!text || !*text) {
    return 0;
  }
  char* end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  if (*end || value == 0 || value > shown) {
    return 0;
  }
  *selected_display = (size_t)value - 1;
  return 1;
}

static int validate_clone_target(const char* target, GitOverleafError* err) {
  int target_empty = 0;
  if (git_overleaf_directory_empty_or_missing(target, &target_empty, err) !=
      0) {
    return -1;
  }
  if (!target_empty) {
    return git_overleaf_error(err, "target directory is not empty: %s", target);
  }
  return 0;
}

static int select_project_interactive(const GitOverleafProjectList* projects,
                                      size_t* selected,
                                      GitOverleafError* err) {
  const size_t display_limit = 20;
  char input[512] = "";
  char query[512] = "";
  int have_query = 0;

  if (projects->len == 0) {
    return git_overleaf_error(err, "no Overleaf projects are visible");
  }

  fprintf(stderr,
          "Search Overleaf projects by name, owner email, or project id.\n");
  for (;;) {
    /* A non-numeric response after the result list becomes the next search,
       so users can narrow results without returning to the initial prompt. */
    if (!have_query &&
        read_prompt_line("> ", query, sizeof(query), err) != 0) {
      return -1;
    }
    have_query = 0;
    char* trimmed_query = git_overleaf_trim(query);
    if (strcmp(trimmed_query, "q") == 0 || strcmp(trimmed_query, "quit") == 0) {
      return git_overleaf_error(err, "project selection cancelled");
    }
    if (git_overleaf_project_find_exact_id(projects, trimmed_query,
                                           selected)) {
      return 0;
    }

    GitOverleafProjectMatch* matches = NULL;
    size_t match_count = 0;
    if (git_overleaf_project_match_query(projects, trimmed_query, &matches,
                                         &match_count, err) != 0) {
      return -1;
    }

    if (match_count == 0) {
      fprintf(stderr, "No matching projects. Try another search or q.\n");
      free(matches);
      continue;
    }

    size_t shown = match_count < display_limit ? match_count : display_limit;
    fprintf(stderr, "\n%zu match%s, showing 1-%zu", match_count,
            match_count == 1 ? "" : "es", shown);
    if (match_count > shown) {
      fprintf(stderr, ". Enter another search to narrow results");
    }
    fprintf(stderr, ":\n");
    for (size_t i = 0; i < shown; i++) {
      const GitOverleafProject* project = &projects->items[matches[i].index];
      fprintf(stderr, "%3zu  %-44.44s  %-28.28s  %.28s\n", i + 1,
              project->name, project->owner_email, project->id);
    }
    fprintf(stderr, "\n");

    if (read_prompt_line(
            "Select number, enter another search, paste project id, or q: ",
            input, sizeof(input), err) != 0) {
      free(matches);
      return -1;
    }
    char* response = git_overleaf_trim(input);
    if (strcmp(response, "q") == 0 || strcmp(response, "quit") == 0) {
      free(matches);
      return git_overleaf_error(err, "project selection cancelled");
    }
    size_t selected_display = 0;
    if (parse_display_selection(response, shown, &selected_display)) {
      *selected = matches[selected_display].index;
      free(matches);
      return 0;
    }
    if (git_overleaf_project_find_exact_id(projects, response, selected)) {
      free(matches);
      return 0;
    }
    snprintf(query, sizeof(query), "%s", response);
    have_query = 1;
    free(matches);
  }
}

static int command_clone(GitOverleafConfig* cfg, int argc, char** argv,
                         GitOverleafError* err) {
  const char* project_id = NULL;
  const char* project_name = NULL;
  const char* target = NULL;
  char* target_owner = NULL;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--project-id") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      project_id = argv[++i];
    } else if (strcmp(argv[i], "--project-name") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      project_name = argv[++i];
    } else if (argv[i][0] == '-') {
      return git_overleaf_error(err, "unknown clone option: %s", argv[i]);
    } else if (!target) {
      target = argv[i];
    } else {
      return git_overleaf_error(err, "unexpected clone argument: %s", argv[i]);
    }
  }
  if (!target && project_name && *project_name) {
    target_owner = git_overleaf_project_directory_name(project_name, err);
    if (!target_owner) {
      return -1;
    }
    target = target_owner;
  }
  if (target && validate_clone_target(target, err) != 0) {
    free(target_owner);
    return -1;
  }
  if (!project_id && !clone_interactive_available()) {
    free(target_owner);
    return git_overleaf_error(
        err, "clone requires --project-id ID when not running interactively");
  }
  if (git_overleaf_config_load_cookie(cfg, err) != 0) {
    free(target_owner);
    return -1;
  }
  GitOverleafProjectList projects = {0};
  /* Fetching the full project list is only necessary for interactive
     selection or when the target name must be derived from a project id. */
  int need_project_list = !project_id || (!target && !project_name);
  if (need_project_list) {
    fprintf(stderr, "Fetching Overleaf project list...\n");
    if (git_overleaf_overleaf_list_projects(cfg, &projects, err) != 0) {
      free(target_owner);
      return -1;
    }
  }
  if (!project_id) {
    size_t selected = 0;
    if (select_project_interactive(&projects, &selected, err) != 0) {
      free(target_owner);
      git_overleaf_project_list_free(&projects);
      return -1;
    }
    project_id = projects.items[selected].id;
    if (!project_name || !*project_name) {
      project_name = projects.items[selected].name;
    }
  } else if (!target && (!project_name || !*project_name)) {
    size_t selected = 0;
    if (!git_overleaf_project_find_exact_id(&projects, project_id, &selected)) {
      free(target_owner);
      git_overleaf_project_list_free(&projects);
      return git_overleaf_error(
          err,
          "clone requires TARGET or --project-name NAME when project id %s is "
          "not present in the visible project list",
          project_id);
    }
    project_name = projects.items[selected].name;
  }

  if (!target) {
    target_owner = git_overleaf_project_directory_name(project_name, err);
    if (!target_owner) {
      git_overleaf_project_list_free(&projects);
      return -1;
    }
    target = target_owner;
    if (validate_clone_target(target, err) != 0) {
      free(target_owner);
      git_overleaf_project_list_free(&projects);
      return -1;
    }
  }
  if (git_overleaf_overleaf_clone(cfg, project_id, project_name, target, err) !=
      0) {
    free(target_owner);
    git_overleaf_project_list_free(&projects);
    return -1;
  }
  printf("Cloned `%s' into %s\n", project_name ? project_name : project_id,
         target);
  free(target_owner);
  git_overleaf_project_list_free(&projects);
  return 0;
}

static int command_init(GitOverleafConfig* cfg, int argc, char** argv,
                        GitOverleafError* err) {
  const char* project_id = NULL;
  const char* project_name = NULL;
  const char* repo = ".";
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--project-id") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      project_id = argv[++i];
    } else if (strcmp(argv[i], "--project-name") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      project_name = argv[++i];
    } else if (strcmp(argv[i], "--repo") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      repo = argv[++i];
    } else {
      return git_overleaf_error(err, "unknown init option: %s", argv[i]);
    }
  }
  if (!project_id) {
    return git_overleaf_error(err, "init requires --project-id ID");
  }
  if (git_overleaf_config_load_cookie(cfg, err) != 0) {
    return -1;
  }
  if (git_overleaf_overleaf_init(cfg, repo, project_id, project_name, err) !=
      0) {
    return -1;
  }
  printf("Configured repository to track Overleaf project `%s'\n",
         project_name ? project_name : project_id);
  return 0;
}

static int command_pull(GitOverleafConfig* cfg, int argc, char** argv,
                        GitOverleafError* err) {
  const char* repo = ".";
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--repo") == 0) {
      if (need_value(argc, argv, i, err) != 0) {
        return -1;
      }
      repo = argv[++i];
    } else {
      return git_overleaf_error(err, "unknown pull option: %s", argv[i]);
    }
  }
  if (git_overleaf_config_load_cookie(cfg, err) != 0) {
    return -1;
  }
  return git_overleaf_overleaf_pull(cfg, repo, err);
}

int main(int argc, char** argv) {
  GitOverleafConfig cfg;
  GitOverleafError err = {{0}};
  git_overleaf_config_init(&cfg);

  int index = 1;
  int rc = 1;
  if (argc == 1) {
    usage(stderr);
    git_overleaf_config_free(&cfg);
    return 2;
  }
  if (parse_global(&cfg, argc, argv, &index, &err) != 0) {
    fprintf(stderr, "git-overleaf-cli: %s\n", err.message);
    git_overleaf_config_free(&cfg);
    return 2;
  }
  if (index >= argc) {
    usage(stderr);
    git_overleaf_config_free(&cfg);
    return 2;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  const char* command = argv[index++];
  if (strcmp(command, "auth") == 0) {
    rc = command_auth(&cfg, argc - index, argv + index, &err);
  } else if (strcmp(command, "list") == 0) {
    rc = command_list(&cfg, &err);
  } else if (strcmp(command, "clone") == 0) {
    rc = command_clone(&cfg, argc - index, argv + index, &err);
  } else if (strcmp(command, "init") == 0) {
    rc = command_init(&cfg, argc - index, argv + index, &err);
  } else if (strcmp(command, "pull") == 0) {
    rc = command_pull(&cfg, argc - index, argv + index, &err);
  } else if (strcmp(command, "push") == 0 ||
             strcmp(command, "overwrite") == 0) {
    rc = git_overleaf_error(
        &err,
        "%s is not implemented in the MVP; use Emacs git-overleaf for now",
        command);
  } else {
    rc = git_overleaf_error(&err, "unknown command: %s", command);
  }
  curl_global_cleanup();

  if (rc != 0) {
    fprintf(stderr, "git-overleaf-cli: %s\n", err.message);
  }
  git_overleaf_config_free(&cfg);
  return rc == 0 ? 0 : 1;
}

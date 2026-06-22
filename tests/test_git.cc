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

#include "test_helpers.h"

TEST(GitMetadata, ExcludeIsCreatedAndDeduplicated) {
  GitOverleafError err = {};
  TempDir root;
  ASSERT_NE(nullptr, root.path());
  CStr git_dir(git_overleaf_path_join(root.path(), ".git"));
  CStr info_dir(git_overleaf_path_join(git_dir.get(), "info"));
  CStr exclude(git_overleaf_path_join(info_dir.get(), "exclude"));
  ASSERT_NE(nullptr, git_dir);
  ASSERT_NE(nullptr, info_dir);
  ASSERT_NE(nullptr, exclude);
  ASSERT_EQ(0, git_overleaf_ensure_directory(info_dir.get(), &err));
  ASSERT_EQ(0, write_text(exclude.get(), "existing"));

  ASSERT_EQ(0, git_overleaf_git_prepare_sync_metadata_repo(root.path(), &err));
  ASSERT_EQ(0, git_overleaf_git_prepare_sync_metadata_repo(root.path(), &err));

  char* text_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_read_file(exclude.get(), &text_raw, &err));
  CStr text(text_raw);
  ExpectContains(text.get(), "existing\n" GIT_OVERLEAF_SYNC_METADATA_FILE "\n");
  EXPECT_EQ(1u, count_substring(text.get(), GIT_OVERLEAF_SYNC_METADATA_FILE));
}

TEST(Git, RepositoryHelpersAndSyntheticCommit) {
  GitOverleafError err = {};
  TempDir repo;
  ASSERT_NE(nullptr, repo.path());
  ConfigGuard cfg;

  const char* init_args[] = {"init"};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), init_args, 1,
                                   nullptr, &err))
      << err.message;

  ASSERT_EQ(0, git_overleaf_git_config_set(&cfg.value, repo.path(),
                                           "git-overleaf.testKey", "value",
                                           &err));
  char* value_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_config_get(&cfg.value, repo.path(),
                                           "git-overleaf.testKey", &value_raw,
                                           &err));
  CStr value(value_raw);
  EXPECT_STREQ("value", value.get());

  char* repo_root_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_root(&cfg.value, repo.path(), &repo_root_raw,
                                     &err));
  CStr repo_root(repo_root_raw);
  EXPECT_STREQ(repo.path(), repo_root.get());

  CStr file(git_overleaf_path_join(repo.path(), "main.tex"));
  ASSERT_NE(nullptr, file);
  ASSERT_EQ(0, write_text(file.get(), "hello"));
  const char* add_args[] = {"add", "--all", "."};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), add_args, 3,
                                   nullptr, &err))
      << err.message;
  const char* commit_args[] = {"-c",
                               "user.name=Test User",
                               "-c",
                               "user.email=test@example.invalid",
                               "commit",
                               "-m",
                               "initial"};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), commit_args, 7,
                                   nullptr, &err))
      << err.message;

  char* branch_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_current_branch(&cfg.value, repo.path(),
                                               &branch_raw, &err))
      << err.message;
  CStr branch(branch_raw);
  ASSERT_NE(nullptr, branch);
  EXPECT_STRNE("", branch.get());

  char* head_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_rev_parse(&cfg.value, repo.path(), "HEAD",
                                          &head_raw, &err))
      << err.message;
  CStr head(head_raw);
  ASSERT_NE(nullptr, head);
  EXPECT_EQ(40u, strlen(head.get()));

  char* missing_raw = reinterpret_cast<char*>(0x1);
  ASSERT_EQ(0, git_overleaf_git_rev_parse_verify(
                   &cfg.value, repo.path(), "refs/missing", &missing_raw,
                   &err));
  EXPECT_EQ(nullptr, missing_raw);

  char* tree_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_tree_id(&cfg.value, repo.path(), "HEAD",
                                        &tree_raw, &err))
      << err.message;
  CStr tree(tree_raw);
  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(40u, strlen(tree.get()));

  int is_ancestor = 0;
  ASSERT_EQ(0, git_overleaf_git_is_ancestor(&cfg.value, repo.path(), "HEAD",
                                            "HEAD", &is_ancestor, &err))
      << err.message;
  EXPECT_EQ(1, is_ancestor);
  ASSERT_EQ(0, git_overleaf_git_is_clean(&cfg.value, repo.path(), &err))
      << err.message;

  ASSERT_EQ(0, git_overleaf_git_write_metadata(&cfg.value, repo.path(), "p1",
                                               "Project One", &err))
      << err.message;
  char* project_id_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_config_get(&cfg.value, repo.path(),
                                           "git-overleaf.projectId",
                                           &project_id_raw, &err));
  CStr project_id(project_id_raw);
  EXPECT_STREQ("p1", project_id.get());

  TempDir snapshot;
  ASSERT_NE(nullptr, snapshot.path());
  CStr remote_file(git_overleaf_path_join(snapshot.path(), "remote.tex"));
  ASSERT_NE(nullptr, remote_file);
  ASSERT_EQ(0, write_text(remote_file.get(), "remote"));
  char* synthetic_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_commit_directory(
                   &cfg.value, repo.path(), snapshot.path(), "HEAD",
                   "synthetic remote", &synthetic_raw, &err))
      << err.message;
  CStr synthetic(synthetic_raw);
  ASSERT_NE(nullptr, synthetic);
  EXPECT_EQ(40u, strlen(synthetic.get()));

  ASSERT_EQ(0, git_overleaf_git_set_base_ref(&cfg.value, repo.path(),
                                             synthetic.get(), &err))
      << err.message;
  char* base_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_git_rev_parse_verify(
                   &cfg.value, repo.path(), GIT_OVERLEAF_BASE_REF, &base_raw,
                   &err))
      << err.message;
  CStr base(base_raw);
  EXPECT_STREQ(synthetic.get(), base.get());

  CStr dirty(git_overleaf_path_join(repo.path(), "dirty.txt"));
  ASSERT_NE(nullptr, dirty);
  ASSERT_EQ(0, write_text(dirty.get(), "dirty"));
  EXPECT_EQ(-1, git_overleaf_git_is_clean(&cfg.value, repo.path(), &err));
  ExpectContains(err.message, "repository has local changes");
}

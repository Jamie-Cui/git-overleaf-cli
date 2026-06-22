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

TEST(Overleaf, SyncStateClassifier) {
  EXPECT_EQ(GIT_OVERLEAF_SYNC_IN_SYNC,
            git_overleaf_overleaf_sync_state("a", "a", "a"));
  EXPECT_EQ(GIT_OVERLEAF_SYNC_HEAD_MATCHES_REMOTE,
            git_overleaf_overleaf_sync_state("a", "b", "b"));
  EXPECT_EQ(GIT_OVERLEAF_SYNC_REMOTE_MATCHES_BASE,
            git_overleaf_overleaf_sync_state("a", "b", "a"));
  EXPECT_EQ(GIT_OVERLEAF_SYNC_HEAD_MATCHES_BASE,
            git_overleaf_overleaf_sync_state("a", "a", "b"));
  EXPECT_EQ(GIT_OVERLEAF_SYNC_DIVERGED,
            git_overleaf_overleaf_sync_state("a", "b", "c"));
}

TEST(Overleaf, SnapshotFreeRemovesTemporaryDirectory) {
  GitOverleafError err = {};
  char* temp_dir = nullptr;
  ASSERT_EQ(0, git_overleaf_make_temp_dir(&temp_dir, &err));
  ASSERT_NE(nullptr, temp_dir);
  CStr temp_dir_owner(git_overleaf_xstrdup(temp_dir));
  ASSERT_NE(nullptr, temp_dir_owner);
  CStr file(git_overleaf_path_join(temp_dir, "snapshot.tex"));
  ASSERT_NE(nullptr, file);
  ASSERT_EQ(0, write_text(file.get(), "snapshot"));

  GitOverleafSnapshot snapshot = {};
  snapshot.temp_dir = temp_dir;
  snapshot.root = git_overleaf_xstrdup(temp_dir);
  ASSERT_NE(nullptr, snapshot.root);
  git_overleaf_snapshot_free(&snapshot);

  EXPECT_EQ(nullptr, snapshot.temp_dir);
  EXPECT_EQ(nullptr, snapshot.root);
  EXPECT_NE(0, access(temp_dir_owner.get(), F_OK));

  git_overleaf_snapshot_free(nullptr);
}

TEST(Overleaf, EarlyValidationPathsAvoidNetwork) {
  GitOverleafError err = {};
  TempDir root;
  ASSERT_NE(nullptr, root.path());
  ConfigGuard cfg;

  CStr target(git_overleaf_path_join(root.path(), "target"));
  ASSERT_NE(nullptr, target);
  ASSERT_EQ(0, git_overleaf_ensure_directory(target.get(), &err));
  CStr file(git_overleaf_path_join(target.get(), "existing.tex"));
  ASSERT_NE(nullptr, file);
  ASSERT_EQ(0, write_text(file.get(), "existing"));

  ASSERT_EQ(-1, git_overleaf_overleaf_clone(&cfg.value, "p1", "Project",
                                            target.get(), &err));
  ExpectContains(err.message, "target directory is not empty");

  ASSERT_EQ(-1, git_overleaf_overleaf_init(&cfg.value, root.path(), "p1",
                                           "Project", &err));
  ExpectContains(err.message, "not inside a Git repository");

  ASSERT_EQ(-1, git_overleaf_overleaf_pull(&cfg.value, root.path(), &err));
  ExpectContains(err.message, "not inside a Git repository");
}

TEST(Overleaf, PullPreflightFailuresBeforeDownload) {
  GitOverleafError err = {};
  TempDir repo;
  ASSERT_NE(nullptr, repo.path());
  ConfigGuard cfg;
  const char* init_args[] = {"init"};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), init_args, 1,
                                   nullptr, &err))
      << err.message;
  CStr file(git_overleaf_path_join(repo.path(), "main.tex"));
  ASSERT_NE(nullptr, file);
  ASSERT_EQ(0, write_text(file.get(), "hello"));
  const char* add_args[] = {"add", "--all", "."};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), add_args, 3,
                                   nullptr, &err))
      << err.message;
  const char* commit_args[] = {"-c",     "user.name=Test User",
                               "-c",     "user.email=test@example.invalid",
                               "commit", "-m",
                               "initial"};
  ASSERT_EQ(0, git_overleaf_git_ok(&cfg.value, repo.path(), commit_args, 7,
                                   nullptr, &err))
      << err.message;

  ASSERT_EQ(-1, git_overleaf_overleaf_pull(&cfg.value, repo.path(), &err));
  ExpectContains(err.message,
                 "repository is not configured as an Overleaf project");

  ASSERT_EQ(0, git_overleaf_git_write_metadata(&cfg.value, repo.path(), "p1",
                                               "Project One", &err))
      << err.message;
  CStr dirty(git_overleaf_path_join(repo.path(), "dirty.tex"));
  ASSERT_NE(nullptr, dirty);
  ASSERT_EQ(0, write_text(dirty.get(), "dirty"));
  ASSERT_EQ(-1, git_overleaf_overleaf_pull(&cfg.value, repo.path(), &err));
  ExpectContains(err.message, "repository has local changes");

  ASSERT_EQ(0, unlink(dirty.get()));
  ASSERT_EQ(0, git_overleaf_git_config_set(&cfg.value, repo.path(),
                                           "git-overleaf.pendingAction", "pull",
                                           &err));
  ASSERT_EQ(-1, git_overleaf_overleaf_pull(&cfg.value, repo.path(), &err));
  ExpectContains(err.message, "unresolved pending Overleaf pull");
}

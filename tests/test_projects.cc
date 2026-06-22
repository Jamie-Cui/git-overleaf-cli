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

TEST(Projects, FindsExactIdsAndRanksMatches) {
  GitOverleafError err = {};
  ProjectListGuard projects(7);
  ASSERT_NE(nullptr, projects.value.items);
  projects.Set(0, "alpha", "Paper Draft", "owner@example.com");
  projects.Set(1, "beta", "Deep Paper Draft", "author@example.com");
  projects.Set(2, "paper-id", "Unrelated", "nobody@example.com");
  projects.Set(3, "gamma", "Thesis", "paper.owner@example.com");
  projects.Set(4, "spread", "Draft", "paper@example.com");
  projects.Set(5, "Exact-ID", "Other", "other@example.com");
  projects.Set(6, "nomatch", "Other", "none@example.com");

  size_t selected = 999;
  EXPECT_EQ(1, git_overleaf_project_find_exact_id(&projects.value, "Exact-ID",
                                                  &selected));
  EXPECT_EQ(5u, selected);
  EXPECT_EQ(0, git_overleaf_project_find_exact_id(&projects.value, "exact-id",
                                                  &selected));

  GitOverleafProjectMatch* matches = nullptr;
  size_t match_count = 0;
  ASSERT_EQ(0, git_overleaf_project_match_query(&projects.value, "paper",
                                                &matches, &match_count, &err));
  ASSERT_NE(nullptr, matches);
  EXPECT_EQ(5u, match_count);
  EXPECT_EQ(0u, matches[0].index);
  EXPECT_EQ(10, matches[0].score);
  EXPECT_EQ(1u, matches[1].index);
  EXPECT_EQ(20, matches[1].score);
  EXPECT_EQ(2u, matches[2].index);
  EXPECT_EQ(25, matches[2].score);
  EXPECT_EQ(3u, matches[3].index);
  EXPECT_EQ(30, matches[3].score);
  free(matches);

  matches = nullptr;
  ASSERT_EQ(0, git_overleaf_project_match_query(
                   &projects.value, "draft paper@example.com", &matches,
                   &match_count, &err));
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(1u, match_count);
  EXPECT_EQ(4u, matches[0].index);
  EXPECT_EQ(10, matches[0].score);
  free(matches);

  matches = nullptr;
  ASSERT_EQ(0, git_overleaf_project_match_query(&projects.value, "Exact-ID",
                                                &matches, &match_count, &err));
  ASSERT_NE(nullptr, matches);
  ASSERT_GE(match_count, 1u);
  EXPECT_EQ(5u, matches[0].index);
  EXPECT_EQ(0, matches[0].score);
  free(matches);

  matches = nullptr;
  ASSERT_EQ(0, git_overleaf_project_match_query(&projects.value, "   ",
                                                &matches, &match_count, &err));
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(projects.value.len, match_count);
  for (size_t i = 0; i < match_count; i++) {
    EXPECT_EQ(i, matches[i].index);
    EXPECT_EQ(100, matches[i].score);
  }
  free(matches);
}

TEST(Projects, MatchesLargeListsWithoutFzf) {
  GitOverleafError err = {};
  ProjectListGuard projects(1000);
  ASSERT_NE(nullptr, projects.value.items);
  char id[64];
  char name[96];
  char owner[96];
  for (size_t i = 0; i < projects.value.len; i++) {
    snprintf(id, sizeof(id), "project-%04zu", i);
    snprintf(name, sizeof(name), "Generated Project %04zu", i);
    snprintf(owner, sizeof(owner), "owner%04zu@example.com", i);
    projects.Set(i, id, name, owner);
  }
  free(projects.value.items[42].name);
  projects.value.items[42].name =
      git_overleaf_xstrdup("Compiler Quantum Plan");
  ASSERT_NE(nullptr, projects.value.items[42].name);
  free(projects.value.items[777].name);
  projects.value.items[777].name =
      git_overleaf_xstrdup("Quantum Compiler Notes");
  ASSERT_NE(nullptr, projects.value.items[777].name);
  free(projects.value.items[779].id);
  projects.value.items[779].id = git_overleaf_xstrdup("quantum-compiler-id");
  ASSERT_NE(nullptr, projects.value.items[779].id);

  GitOverleafProjectMatch* matches = nullptr;
  size_t match_count = 0;
  ASSERT_EQ(0, git_overleaf_project_match_query(&projects.value,
                                                "quantum compiler", &matches,
                                                &match_count, &err));
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(3u, match_count);
  EXPECT_EQ(777u, matches[0].index);
  EXPECT_EQ(10, matches[0].score);
  EXPECT_EQ(42u, matches[1].index);
  EXPECT_EQ(20, matches[1].score);
  EXPECT_EQ(779u, matches[2].index);
  EXPECT_EQ(25, matches[2].score);
  free(matches);
}

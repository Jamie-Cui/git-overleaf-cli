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

TEST(OverleafProjects, ParsesProjectPageAndErrors) {
  GitOverleafError err = {};
  GitOverleafProjectList projects = {};
  const char* html =
      "<html><head><meta name=\"ol-prefetchedProjectsBlob\" content=\""
      "{&quot;projects&quot;:["
      "{&quot;id&quot;:&quot;abc123&quot;,"
      "&quot;name&quot;:&quot;Paper%20Draft&quot;,"
      "&quot;owner&quot;:{&quot;email&quot;:&quot;owner%40example.com&quot;}},"
      "{&quot;id&quot;:&quot;def456&quot;,"
      "&quot;name&quot;:&quot;No%20Owner&quot;}"
      "]}\"></head></html>";
  ASSERT_EQ(0,
            git_overleaf_overleaf_parse_projects_page(html, &projects, &err));
  ASSERT_EQ(2u, projects.len);
  EXPECT_STREQ("abc123", projects.items[0].id);
  EXPECT_STREQ("Paper Draft", projects.items[0].name);
  EXPECT_STREQ("owner@example.com", projects.items[0].owner_email);
  EXPECT_STREQ("def456", projects.items[1].id);
  EXPECT_STREQ("No Owner", projects.items[1].name);
  EXPECT_STREQ("", projects.items[1].owner_email);
  git_overleaf_project_list_free(&projects);

  EXPECT_EQ(-1, git_overleaf_overleaf_parse_projects_page(
                    "<html><form action=\"/login\"><input "
                    "name=\"email\"><input name=\"password\"></form>",
                    &projects, &err));
  ExpectContains(err.message, "login page");
  git_overleaf_project_list_free(&projects);

  const char* invalid_schema =
      "<meta name=\"ol-prefetchedProjectsBlob\" "
      "content=\"{&quot;notProjects&quot;:[]}\">";
  EXPECT_EQ(-1, git_overleaf_overleaf_parse_projects_page(invalid_schema,
                                                          &projects, &err));
  ExpectContains(err.message, "projects array");
  git_overleaf_project_list_free(&projects);
}

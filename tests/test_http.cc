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

TEST(OverleafRemote, ParsesRemoteTreeAndTextOps) {
  GitOverleafError err = {};
  const char* tree_json =
      "{"
      "\"name\":\"rootFolder\",\"_id\":\"root\","
      "\"docs\":[{\"name\":\"main.tex\",\"_id\":\"doc1\"}],"
      "\"fileRefs\":[{\"name\":\"fig.png\",\"_id\":\"file1\"}],"
      "\"folders\":[{\"name\":\"chapters\",\"_id\":\"folder1\","
      "\"docs\":[{\"name\":\"intro.tex\",\"_id\":\"doc2\"}],"
      "\"fileRefs\":[],\"folders\":[]}]"
      "}";
  json_error_t json_err;
  json_t* root = json_loads(tree_json, 0, &json_err);
  ASSERT_NE(nullptr, root) << json_err.text;
  GitOverleafRemoteTable table = {};
  ASSERT_EQ(0, git_overleaf_overleaf_parse_remote_table(root, &table, &err))
      << err.message;
  json_decref(root);

  GitOverleafRemoteEntity* main =
      git_overleaf_remote_table_find(&table, "main.tex");
  ASSERT_NE(nullptr, main);
  EXPECT_EQ(GIT_OVERLEAF_REMOTE_DOC, main->type);
  EXPECT_STREQ("doc1", main->id);
  GitOverleafRemoteEntity* chapter =
      git_overleaf_remote_table_find(&table, "chapters/intro.tex");
  ASSERT_NE(nullptr, chapter);
  EXPECT_EQ(GIT_OVERLEAF_REMOTE_DOC, chapter->type);
  EXPECT_STREQ("folder1", chapter->parent_id);
  git_overleaf_remote_table_free(&table);

  json_t* op = nullptr;
  ASSERT_EQ(0, git_overleaf_sharejs_text_op("hello", "hello world", &op, &err))
      << err.message;
  ASSERT_NE(nullptr, op);
  ASSERT_EQ(1u, json_array_size(op));
  EXPECT_STREQ(" world",
               json_string_value(json_object_get(json_array_get(op, 0), "i")));
  EXPECT_EQ(5, json_integer_value(json_object_get(json_array_get(op, 0), "p")));
  json_decref(op);

  ASSERT_EQ(0, git_overleaf_sharejs_text_op("same", "same", &op, &err))
      << err.message;
  EXPECT_EQ(nullptr, op);

  std::string emoji = "\xF0\x9F\x98\x80";
  std::string before = "a" + emoji + "b";
  std::string after = "a" + emoji + "Xb";
  ASSERT_EQ(
      0, git_overleaf_sharejs_text_op(before.c_str(), after.c_str(), &op, &err))
      << err.message;
  ASSERT_NE(nullptr, op);
  ASSERT_EQ(1u, json_array_size(op));
  EXPECT_STREQ("X",
               json_string_value(json_object_get(json_array_get(op, 0), "i")));
  EXPECT_EQ(3, json_integer_value(json_object_get(json_array_get(op, 0), "p")));
  json_decref(op);
}

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

TEST(FileSystem, TreeOperations) {
  GitOverleafError err = {};
  TempDir root;
  ASSERT_NE(nullptr, root.path());

  int empty = 0;
  CStr missing(git_overleaf_path_join(root.path(), "missing"));
  ASSERT_NE(nullptr, missing);
  ASSERT_EQ(
      0, git_overleaf_directory_empty_or_missing(missing.get(), &empty, &err));
  EXPECT_EQ(1, empty);

  CStr source(git_overleaf_path_join(root.path(), "source"));
  CStr source_sub(git_overleaf_path_join(source.get(), "sub"));
  CStr source_file(git_overleaf_path_join(source_sub.get(), "file.txt"));
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, source_sub);
  ASSERT_NE(nullptr, source_file);
  ASSERT_EQ(0, git_overleaf_ensure_directory(source_sub.get(), &err));
  ASSERT_EQ(0, write_text(source_file.get(), "hello"));
  ASSERT_EQ(
      0, git_overleaf_directory_empty_or_missing(source.get(), &empty, &err));
  EXPECT_EQ(0, empty);

  CStr dest(git_overleaf_path_join(root.path(), "dest"));
  ASSERT_NE(nullptr, dest);
  ASSERT_EQ(0, git_overleaf_copy_tree(source.get(), dest.get(), &err));
  CStr copied_file = join3(dest.get(), "sub", "file.txt");
  ASSERT_NE(nullptr, copied_file);
  char* copied_text_raw = nullptr;
  ASSERT_EQ(0,
            git_overleaf_read_file(copied_file.get(), &copied_text_raw, &err));
  CStr copied_text(copied_text_raw);
  EXPECT_STREQ("hello", copied_text.get());

  CStr extract(git_overleaf_path_join(root.path(), "extract"));
  CStr only(git_overleaf_path_join(extract.get(), "only"));
  ASSERT_NE(nullptr, extract);
  ASSERT_NE(nullptr, only);
  ASSERT_EQ(0, git_overleaf_ensure_directory(only.get(), &err));
  char* normalized_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_normalize_extracted_root(extract.get(),
                                                     &normalized_raw, &err));
  CStr normalized(normalized_raw);
  EXPECT_STREQ(only.get(), normalized.get());

  CStr extra(git_overleaf_path_join(extract.get(), "extra.txt"));
  ASSERT_NE(nullptr, extra);
  ASSERT_EQ(0, write_text(extra.get(), "extra"));
  normalized_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_normalize_extracted_root(extract.get(),
                                                     &normalized_raw, &err));
  normalized.reset(normalized_raw);
  EXPECT_STREQ(extract.get(), normalized.get());

  CStr metadata(
      git_overleaf_path_join(dest.get(), GIT_OVERLEAF_SYNC_METADATA_FILE));
  ASSERT_NE(nullptr, metadata);
  ASSERT_EQ(0, write_text(metadata.get(), "metadata"));
  char* metadata_text_raw = nullptr;
  ASSERT_EQ(0, git_overleaf_delete_sync_metadata(dest.get(), &metadata_text_raw,
                                                 &err));
  CStr metadata_text(metadata_text_raw);
  EXPECT_STREQ("metadata", metadata_text.get());
  EXPECT_NE(0, access(metadata.get(), F_OK));
}

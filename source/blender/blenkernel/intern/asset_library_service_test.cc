/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2020 Blender Foundation
 * All rights reserved.
 */

#include "asset_library_service.hh"

#include "CLG_log.h"

#include "testing/testing.h"

namespace blender::bke::tests {

class AssetLibraryServiceTest : public testing::Test {
 public:
  CatalogFilePath asset_library_root_;

  static void SetUpTestSuite()
  {
    CLG_init();
  }
  static void TearDownTestSuite()
  {
    CLG_exit();
  }

  void SetUp() override
  {
    const std::string test_files_dir = blender::tests::flags_test_asset_dir();
    if (test_files_dir.empty()) {
      FAIL();
    }
    asset_library_root_ = test_files_dir + "/" + "asset_library";
  }

  void TearDown() override
  {
    AssetLibraryService::destroy();
  }
};

TEST_F(AssetLibraryServiceTest, get_destroy)
{
  AssetLibraryService *const service = AssetLibraryService::get();
  EXPECT_EQ(service, AssetLibraryService::get())
      << "Calling twice without destroying in between should return the same instance.";

  AssetLibraryService::destroy();

  /* On GCC in release mode (and maybe also debug mode without ASAN enabled), allocating an
   * #AssetLibraryService will reuse the space that should have just been freed in the above
   * destroy() call. To see that the get() call below really allocates a new object, allocate a
   * dummy block of memory first. */
  AssetLibraryService *dummy_pointer = new AssetLibraryService();

  EXPECT_NE(service, AssetLibraryService::get())
      << "Calling twice with destroying in between should return a new instance.";

  /* This should not crash. */
  AssetLibraryService::destroy();
  AssetLibraryService::destroy();

  delete dummy_pointer;
}

TEST_F(AssetLibraryServiceTest, library_pointers)
{
  AssetLibraryService *service = AssetLibraryService::get();
  AssetLibrary *const lib = service->get_asset_library_on_disk(asset_library_root_);
  AssetLibrary *const curfile_lib = service->get_asset_library_current_file();

  EXPECT_EQ(lib, service->get_asset_library_on_disk(asset_library_root_))
      << "Calling twice without destroying in between should return the same instance.";
  EXPECT_EQ(curfile_lib, service->get_asset_library_current_file())
      << "Calling twice without destroying in between should return the same instance.";

  AssetLibraryService::destroy();

  /* On GCC in release mode (and maybe also debug mode without ASAN enabled), allocating an
   * #AssetLibraryService will reuse the space that should have just been freed in the above
   * destroy() call. To see that the get() call below really allocates a new object, allocate a
   * dummy block of memory first. */
  AssetLibrary *dummy_pointer = new AssetLibrary();

  service = AssetLibraryService::get();
  EXPECT_NE(lib, service->get_asset_library_on_disk(asset_library_root_))
      << "Calling twice with destroying in between should return a new instance.";
  EXPECT_NE(curfile_lib, service->get_asset_library_current_file())
      << "Calling twice with destroying in between should return a new instance.";

  delete dummy_pointer;
}

TEST_F(AssetLibraryServiceTest, catalogs_loaded)
{
  AssetLibraryService *const service = AssetLibraryService::get();
  AssetLibrary *const lib = service->get_asset_library_on_disk(asset_library_root_);
  AssetCatalogService *const cat_service = lib->catalog_service.get();

  const bUUID UUID_POSES_ELLIE("df60e1f6-2259-475b-93d9-69a1b4a8db78");
  EXPECT_NE(nullptr, cat_service->find_catalog(UUID_POSES_ELLIE))
      << "Catalogs should be loaded after getting an asset library from disk.";
}

}  // namespace blender::bke::tests
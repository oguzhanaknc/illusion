#pragma once

// A writable copy of the glTF test data mounted as "game", plus a "cache" mount, for asset tests.

#include "TestSupport.hpp"

#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>

namespace illusion::test
{

struct AssetProject
{
    TempDirectory directory;
    VirtualFileSystem files;

    AssetProject()
    {
        const std::filesystem::path source = utf8Path(ILLUSION_TEST_DATA_DIR) / "gltf";
        std::filesystem::copy(source, directory.path / "game", std::filesystem::copy_options::recursive);
        std::filesystem::create_directories(directory.path / "cache");
        REQUIRE(files.mount("game", directory.path / "game", MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);
    }
};

} // namespace illusion::test

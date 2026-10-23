#pragma once

/**
@file
@brief File access for the Xbox shell that works outside the app package.

A UWP app can only reach folders outside its own package (a USB drive on `E:\`, any folder the user picked) through
the brokered `...FromApp` variants of the Win32 file API, even with the `broadFileSystemAccess` capability. The
plain functions behind `std::filesystem` and `std::ifstream` fail there with access denied. These helpers go through
the brokered API.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace platform::uwp::shell::fs {

/// @brief An entry of a directory listing.
struct DirectoryEntry {
    std::filesystem::path path;
    bool isDirectory = false;
    std::uint64_t size = 0;      // in bytes; 0 for directories
    std::uint64_t lastWrite = 0; // FILETIME ticks (100 ns since 1601), only meaningful for comparisons
};

/// @brief Lists a directory, without recursing into subdirectories. `.` and `..` are skipped.
/// @param[in] directory the directory to list
/// @param[out] out receives the entries; it is cleared first
/// @return `true` on success, `false` if the directory could not be opened
bool ListDirectory(const std::filesystem::path &directory, std::vector<DirectoryEntry> &out);

/// @brief Reads a whole file into memory.
/// @param[in] path the file to read
/// @param[in] maxBytes the largest file accepted; bigger files are rejected
/// @return the file contents, or nothing if the file could not be read or is too big
std::optional<std::string> ReadWholeFile(const std::filesystem::path &path, std::uint64_t maxBytes);

/// @brief Creates a directory and its parents if they do not exist.
/// @return `true` if the directory exists afterwards
bool CreateDirectories(const std::filesystem::path &directory);

} // namespace platform::uwp::shell::fs

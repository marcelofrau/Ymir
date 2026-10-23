#include "file_system.hpp"

#include <windows.h>

#include <fileapifromapp.h>

#include <algorithm>
#include <system_error>

namespace platform::uwp::shell::fs {

namespace {

    std::uint64_t ToTicks(const FILETIME &time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }

    /// @brief Closes a Win32 handle when it goes out of scope.
    struct HandleCloser {
        HANDLE handle;
        ~HandleCloser() {
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
        }
    };

} // namespace

bool ListDirectory(const std::filesystem::path &directory, std::vector<DirectoryEntry> &out) {
    out.clear();
    const std::wstring pattern = (directory / L"*").wstring();

    WIN32_FIND_DATAW data{};
    const HANDLE find =
        FindFirstFileExFromAppW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }

    do {
        const std::wstring_view name{data.cFileName};
        if (name == L"." || name == L"..") {
            continue;
        }
        DirectoryEntry entry{};
        entry.path = directory / data.cFileName;
        entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.size = entry.isDirectory ? 0 : (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        entry.lastWrite = ToTicks(data.ftLastWriteTime);
        out.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return true;
}

std::optional<std::string> ReadWholeFile(const std::filesystem::path &path, std::uint64_t maxBytes) {
    CREATEFILE2_EXTENDED_PARAMETERS params{};
    params.dwSize = sizeof(params);
    params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    params.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    const HandleCloser file{
        CreateFile2FromAppW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params)};
    if (file.handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > maxBytes) {
        return std::nullopt;
    }

    std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min)(data.size() - offset, std::size_t{1} << 24));
        DWORD read = 0;
        if (!ReadFile(file.handle, data.data() + offset, chunk, &read, nullptr) || read == 0) {
            return std::nullopt;
        }
        offset += read;
    }
    return data;
}

bool CreateDirectories(const std::filesystem::path &directory) {
    std::error_code error{};
    if (std::filesystem::is_directory(directory, error)) {
        return true;
    }
    // Create the parents first; CreateDirectoryFromAppW only creates the last component
    if (directory.has_parent_path() && directory.parent_path() != directory) {
        CreateDirectories(directory.parent_path());
    }
    return CreateDirectoryFromAppW(directory.wstring().c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace platform::uwp::shell::fs

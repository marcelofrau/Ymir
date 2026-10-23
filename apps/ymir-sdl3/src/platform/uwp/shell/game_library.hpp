#pragma once

/**
@file
@brief The game library of the Xbox shell: the disc images found in the folders the user picked.

Nothing is scanned on its own. The user adds a folder with the folder picker (or asks for a rescan) and the library
walks it in the background, reading the Saturn header of every disc image (title, product number, region). The
result is cached in `<profile>/state/shell_library.toml`, keyed by path, size and modification time, so the next
launch shows the grid straight away and a rescan only opens the images that changed.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform::uwp::shell {

/// @brief A game found in the library.
struct GameEntry {
    std::filesystem::path path;  // the disc image to load (.cue, .chd, .iso, .ccd or .mds)
    std::string title;           // from the disc header, translated to UTF-8; the file name if the header has none
    std::string productCode;     // e.g. "MK-81009"; also the cover file name
    std::string region;          // compatible area codes, e.g. "JTUE"
    std::string releaseDate;     // YYYYMMDD
    std::uint64_t size = 0;      // file size, to detect changes
    std::uint64_t lastWrite = 0; // modification time, to detect changes
};

/// @brief Finds the disc images in the library folders and keeps their headers.
class GameLibrary {
public:
    /// @brief Creates the library.
    /// @param[in] cachePath where the scan results are cached
    explicit GameLibrary(std::filesystem::path cachePath);
    ~GameLibrary();

    GameLibrary(const GameLibrary &) = delete;
    GameLibrary &operator=(const GameLibrary &) = delete;

    /// @brief Loads the cached results of the last scan. Does not scan anything.
    void LoadCache();

    /// @brief Scans the given folders again in the background, replacing the game list when done.
    /// Does nothing if a scan is already running.
    /// @param[in] folders the library folders, searched recursively a few levels deep
    void StartScan(std::vector<std::filesystem::path> folders);

    /// @brief Determines whether a scan is running.
    [[nodiscard]] bool Scanning() const {
        return m_scanning;
    }

    /// @brief Retrieves how many disc images the running scan has looked at so far.
    [[nodiscard]] std::size_t ScanProgress() const {
        return m_scanProgress;
    }

    /// @brief Retrieves a counter that changes every time the game list changes, to know when to refresh a copy.
    [[nodiscard]] std::uint64_t Version() const {
        return m_version;
    }

    /// @brief Copies the game list, sorted by title.
    [[nodiscard]] std::vector<GameEntry> Games() const;

private:
    void ScanThread(std::stop_token stopToken, std::vector<std::filesystem::path> folders);
    void SaveCache(const std::vector<GameEntry> &games) const;

    std::filesystem::path m_cachePath;

    mutable std::mutex m_mutex;
    std::vector<GameEntry> m_games;

    std::atomic_bool m_scanning{false};
    std::atomic<std::size_t> m_scanProgress{0};
    std::atomic<std::uint64_t> m_version{0};

    std::jthread m_scanThread;
};

} // namespace platform::uwp::shell

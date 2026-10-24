#pragma once

/**
@file
@brief Cover art and on-demand game metadata for the game grid of the Xbox shell.

Covers are read from `<profile>/covers/`, named after the product number on the disc (for example `MK-81009.png`) or
after the disc image file (`Sonic Jam.jpg`). When a cover or game synopsis is missing locally, a background worker
fetches it on demand without any login or API key (Libretro Thumbnails CDN + Wikipedia MediaWiki API) and caches the
image in `<profile>/covers/` and the synopsis in `<profile>/state/shell_metadata.toml`.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <platform/uwp/shell/game_library.hpp>
#include <platform/uwp/shell/theme.hpp>

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app::services {

class GraphicsService;

} // namespace app::services

namespace platform::uwp::shell {

/// @brief Loads and keeps the cover textures and on-demand descriptions of the game grid.
class CoverCache {
public:
    /// @brief Largest cover edge kept in memory, in pixels; bigger images are scaled down.
    static constexpr std::uint32_t kMaxEdge = 256;

    /// @brief Creates the cache.
    /// @param[in] folder the folder with the cover images (`<profile>/covers`)
    /// @param[in] metadataPath where fetched descriptions are cached (`<profile>/state/shell_metadata.toml`)
    explicit CoverCache(std::filesystem::path folder, std::filesystem::path metadataPath = {});
    ~CoverCache();

    CoverCache(const CoverCache &) = delete;
    CoverCache &operator=(const CoverCache &) = delete;

    /// @brief Allows a few more covers to be loaded this frame. Call once per frame before drawing the grid.
    void BeginFrame() {
        m_loadBudget = 2;
    }

    /// @brief Retrieves the cover of a game, loading it if the frame budget allows or queueing an on-demand download.
    /// @param[in] graphics the service that owns the GUI textures
    /// @param[in] game the game
    /// @return the cover, or `nullptr` if the game has none or it is not loaded yet
    const ThemeAsset *Find(app::services::GraphicsService &graphics, const GameEntry &game);

    /// @brief Retrieves the cached or downloaded synopsis/description of a game, queueing an on-demand fetch if needed.
    /// @param[in] game the game
    /// @return the description text, or empty if still fetching / unavailable
    std::string FindDescription(const GameEntry &game);

    /// @brief Clears all in-memory and queued state (used when resetting saved data).
    void Clear();

private:
    struct Cover {
        ThemeAsset asset;
        std::vector<std::uint8_t> rgba; // kept for texture re-creation when the graphics backend changes
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool valid = false;
    };

    std::unique_ptr<Cover> Load(app::services::GraphicsService &graphics, const GameEntry &game) const;
    void EnqueueFetch(const GameEntry &game, bool needCover, bool needDescription);
    void WorkerLoop(std::stop_token stopToken);
    void LoadMetadataCache();
    void SaveMetadataCache() const;

    std::filesystem::path m_folder;
    std::filesystem::path m_metadataPath;
    std::unordered_map<std::string, std::unique_ptr<Cover>> m_covers; // by game path; null entries are misses
    int m_loadBudget = 0;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_cv;
    struct FetchTask {
        GameEntry game;
        bool needCover = false;
        bool needDescription = false;
    };
    std::vector<FetchTask> m_queue;
    std::unordered_set<std::string> m_attemptedCovers;
    std::unordered_set<std::string> m_attemptedDescriptions;
    std::unordered_set<std::string> m_readyCovers; // keys whose downloaded cover is ready on disk
    std::unordered_map<std::string, std::string> m_descriptions; // keyed by MetadataKey(game)
    std::jthread m_worker;
};

} // namespace platform::uwp::shell

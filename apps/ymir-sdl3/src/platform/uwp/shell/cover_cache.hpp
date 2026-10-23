#pragma once

/**
@file
@brief Cover art for the game grid of the Xbox shell.

Covers are the user's own images in `<profile>/covers/`, named after the product number on the disc (for example
`MK-81009.png`) or after the disc image file (`Sonic Jam.jpg`). They are loaded on demand, a couple per frame so the
grid never stalls, and scaled down to tile size so a big library does not fill the memory.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <platform/uwp/shell/game_library.hpp>
#include <platform/uwp/shell/theme.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace app::services {

class GraphicsService;

} // namespace app::services

namespace platform::uwp::shell {

/// @brief Loads and keeps the cover textures of the game grid.
class CoverCache {
public:
    /// @brief Largest cover edge kept in memory, in pixels; bigger images are scaled down.
    static constexpr std::uint32_t kMaxEdge = 256;

    /// @brief Creates the cache.
    /// @param[in] folder the folder with the cover images
    explicit CoverCache(std::filesystem::path folder);

    /// @brief Allows a few more covers to be loaded this frame. Call once per frame before drawing the grid.
    void BeginFrame() {
        m_loadBudget = 2;
    }

    /// @brief Retrieves the cover of a game, loading it if the frame budget allows.
    /// @param[in] graphics the service that owns the GUI textures
    /// @param[in] game the game
    /// @return the cover, or `nullptr` if the game has none or it is not loaded yet
    const ThemeAsset *Find(app::services::GraphicsService &graphics, const GameEntry &game);

private:
    struct Cover {
        ThemeAsset asset;
        std::vector<std::uint8_t> rgba; // kept for texture re-creation when the graphics backend changes
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool valid = false;
    };

    std::unique_ptr<Cover> Load(app::services::GraphicsService &graphics, const GameEntry &game) const;

    std::filesystem::path m_folder;
    std::unordered_map<std::string, std::unique_ptr<Cover>> m_covers; // by game path; null entries are misses
    int m_loadBudget = 0;
};

} // namespace platform::uwp::shell

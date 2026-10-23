#pragma once

/**
@file
@brief Art assets of the Xbox shell.

The shell is themed after the Sega Saturn BIOS: a starfield, a spaceship console panel and spheres as menu options.
The art is described by a `theme.toml` manifest that maps asset ids to PNG files, optionally as sprite sheets or as
9-slice panels. See `docs/uwp/shell/ASSETS.md` for the asset contract.

Every asset is looked up in this order:
1. `<profile>/shell-theme/`, so the artwork can be iterated on by uploading files through the Device Portal without
   rebuilding the app;
2. the copy embedded in the executable;
3. nothing: `Find()` returns `nullptr` and the caller draws a procedural placeholder, so the shell works before the
   artwork exists.
*/

#include <platform/uwp/shell/theme_layout.hpp>

#include <app/services/gfx/gfx_gui_types.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace app::services {

class GraphicsService;

} // namespace app::services

namespace platform::uwp::shell {

/// @brief A loaded art asset: a texture, optionally split into sprite sheet frames or 9-slice margins.
struct ThemeAsset {
    app::gfx::GUITextureHandle texture = app::gfx::kInvalidGUITextureHandle;

    float textureWidth = 0.0f;  // full texture width in pixels
    float textureHeight = 0.0f; // full texture height in pixels

    int frames = 1;   // number of sprite sheet frames
    int columns = 1;  // number of frames per row in the sprite sheet
    float fps = 0.0f; // animation speed; 0 means the asset is static

    float slice[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // 9-slice margins in pixels: left, top, right, bottom

    /// @brief Retrieves the width of a single frame, in pixels.
    [[nodiscard]] float FrameWidth() const;

    /// @brief Retrieves the height of a single frame, in pixels.
    [[nodiscard]] float FrameHeight() const;

    /// @brief Retrieves the frame to show at a point in time.
    /// @param[in] time the animation time in seconds
    /// @return the frame index, in [0, frames)
    [[nodiscard]] int FrameAt(double time) const;

    /// @brief Computes the texture coordinates of a frame.
    /// @param[in] frame the frame index
    /// @param[out] uv0 the top-left texture coordinate (u, v)
    /// @param[out] uv1 the bottom-right texture coordinate (u, v)
    void FrameUV(int frame, float uv0[2], float uv1[2]) const;

    /// @brief Determines whether the asset is a 9-slice panel.
    [[nodiscard]] bool IsNineSlice() const {
        return slice[0] > 0.0f || slice[1] > 0.0f || slice[2] > 0.0f || slice[3] > 0.0f;
    }
};

/// @brief Loads and owns the art assets of the shell.
class Theme {
public:
    /// @brief Loads the manifest and every asset it lists, creating one GUI texture per asset.
    /// Assets that cannot be read or decoded are skipped with a log line; the shell draws placeholders for them.
    /// @param[in] graphicsService the service that owns the GUI textures
    /// @param[in] overrideDir the directory whose files take precedence over the embedded ones
    void Load(app::services::GraphicsService &graphicsService, const std::filesystem::path &overrideDir);

    /// @brief Looks up an asset by id.
    /// @param[in] id the asset id from the manifest
    /// @return the asset, or `nullptr` if it is not part of the theme
    [[nodiscard]] const ThemeAsset *Find(std::string_view id) const;

    /// @brief Retrieves the positions of the shell elements, from the `[layout]` section of the manifest.
    [[nodiscard]] const ThemeLayout &GetLayout() const {
        return m_layout;
    }

private:
    /// @brief Decoded RGBA pixels of a texture. They are kept alive because the graphics service re-uploads them
    /// whenever the graphics backend is recreated.
    struct PixelData {
        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    std::unordered_map<std::string, ThemeAsset> m_assets;
    ThemeLayout m_layout;
    std::vector<std::unique_ptr<PixelData>> m_pixels;
};

} // namespace platform::uwp::shell

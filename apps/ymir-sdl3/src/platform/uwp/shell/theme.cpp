#include "theme.hpp"

#include "shell_log.hpp"

#include <app/services/graphics_service.hpp>

#include <cmrc/cmrc.hpp>

#include <imgui.h>

#include <stb_image.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

CMRC_DECLARE(Ymir_sdl3_shell_rc);

namespace platform::uwp::shell {

namespace {

    constexpr const char *kManifestName = "theme.toml";
    constexpr const char *kDefaultFontName = "3270NerdFont-Regular.ttf";
    constexpr const char *kIconFontName = "fa-solid-900.ttf";

    /// @brief Reads a theme file, preferring the override directory over the embedded copy.
    /// @param[in] overrideDir the override directory; may not exist
    /// @param[in] name the file name relative to the theme root
    /// @return the file contents, or nothing if the file exists in neither place
    std::optional<std::string> ReadThemeFile(const std::filesystem::path &overrideDir, const std::string &name) {
        std::error_code error{};
        const std::filesystem::path overridePath =
            overrideDir / std::filesystem::path{std::u8string{name.begin(), name.end()}};
        if (!overrideDir.empty() && std::filesystem::is_regular_file(overridePath, error)) {
            std::ifstream in{overridePath, std::ios::binary};
            if (in) {
                return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
            }
            devlog::warn<grp::shell>("Could not read theme override {}", overridePath);
        }

        auto embedfs = cmrc::Ymir_sdl3_shell_rc::get_filesystem();
        if (embedfs.exists(name)) {
            const cmrc::file file = embedfs.open(name);
            return std::string{file.begin(), file.end()};
        }
        return std::nullopt;
    }

    /// @brief Reads a fixed-size array of numbers from the manifest. Integers and floats are both accepted.
    /// @param[in] node the manifest node
    /// @param[out] out receives the values; left untouched unless the node is an array of exactly `N` numbers
    /// @return `true` if the values were read
    template <std::size_t N>
    bool ReadNumbers(toml::node_view<const toml::node> node, float (&out)[N]) {
        const toml::array *array = node.as_array();
        if (array == nullptr || array->size() != N) {
            return false;
        }
        float values[N];
        for (std::size_t i = 0; i < N; ++i) {
            const std::optional<double> value = (*array)[i].value<double>();
            if (!value) {
                return false;
            }
            values[i] = static_cast<float>(*value);
        }
        std::copy(std::begin(values), std::end(values), std::begin(out));
        return true;
    }

    /// @brief Reads the `[layout]` section. Missing or malformed entries keep their defaults.
    void ReadLayout(const toml::table &manifest, ThemeLayout &layout) {
        const auto section = manifest["layout"];
        float point[2];
        float rect[4];
        float pod[3];

        if (ReadNumbers(section["dash_top"], point)) {
            layout.dashTop = {point[0], point[1]};
        }
        if (ReadNumbers(section["dash_bottom"], point)) {
            layout.dashBottom = {point[0], point[1]};
        }
        if (ReadNumbers(section["lcd"], rect)) {
            layout.lcd = {{rect[0], rect[1]}, {rect[2], rect[3]}};
        }
        if (const std::optional<double> pitch = section["lcd_pitch"].value<double>(); pitch && *pitch >= 1.0) {
            layout.lcdPitch = static_cast<float>(*pitch);
        }
        if (ReadNumbers(section["pod_left"], pod)) {
            layout.podLeft = {{pod[0], pod[1]}, pod[2]};
        }
        if (ReadNumbers(section["pod_right"], pod)) {
            layout.podRight = {{pod[0], pod[1]}, pod[2]};
        }
        if (ReadNumbers(section["grid"], rect)) {
            layout.grid = {{rect[0], rect[1]}, {rect[2], rect[3]}};
        }
        if (const std::optional<std::int64_t> columns = section["grid_columns"].value<std::int64_t>();
            columns && *columns >= 1 && *columns <= 12) {
            layout.gridColumns = static_cast<int>(*columns);
        }
        if (ReadNumbers(section["spheres"], rect)) {
            layout.spheres = {rect[0], rect[1], rect[2], rect[3]};
        }
        if (ReadNumbers(section["focus_label"], point)) {
            layout.focusLabel = {point[0], point[1]};
        }
        if (ReadNumbers(section["focus_description"], point)) {
            layout.focusDescription = {point[0], point[1]};
        }
        if (ReadNumbers(section["message_panel"], rect)) {
            layout.messagePanel = {{rect[0], rect[1]}, {rect[2], rect[3]}};
        }
        if (ReadNumbers(section["legend"], point)) {
            layout.legend = {point[0], point[1]};
        }
        if (ReadNumbers(section["version"], point)) {
            layout.versionPos = {point[0], point[1]};
        }
    }

} // namespace

float ThemeAsset::FrameWidth() const {
    return textureWidth / static_cast<float>(std::max(columns, 1));
}

float ThemeAsset::FrameHeight() const {
    const int rows = (std::max(frames, 1) + std::max(columns, 1) - 1) / std::max(columns, 1);
    return textureHeight / static_cast<float>(std::max(rows, 1));
}

int ThemeAsset::FrameAt(double time) const {
    if (frames <= 1 || fps <= 0.0f) {
        return 0;
    }
    const auto frame = static_cast<long long>(std::floor(time * fps));
    return static_cast<int>(((frame % frames) + frames) % frames);
}

void ThemeAsset::FrameUV(int frame, float uv0[2], float uv1[2]) const {
    const int cols = std::max(columns, 1);
    const int rows = (std::max(frames, 1) + cols - 1) / cols;
    frame = std::clamp(frame, 0, std::max(frames, 1) - 1);
    const float du = 1.0f / static_cast<float>(cols);
    const float dv = 1.0f / static_cast<float>(std::max(rows, 1));
    uv0[0] = static_cast<float>(frame % cols) * du;
    uv0[1] = static_cast<float>(frame / cols) * dv;
    uv1[0] = uv0[0] + du;
    uv1[1] = uv0[1] + dv;
}

void Theme::Load(app::services::GraphicsService &graphicsService, const std::filesystem::path &overrideDir) {
    const std::optional<std::string> manifestText = ReadThemeFile(overrideDir, kManifestName);
    if (!manifestText) {
        devlog::info<grp::shell>("No theme manifest found; using procedural placeholders");
        return;
    }

    auto parseResult = toml::parse(std::string_view{*manifestText}, std::string_view{kManifestName});
    if (parseResult.failed()) {
        devlog::warn<grp::shell>("Invalid theme manifest: {}", parseResult.error().description());
        return;
    }
    const toml::table &manifest = parseResult.table();

    ReadLayout(manifest, m_layout);

    const std::string fontName = manifest["font"].value_or(std::string{kDefaultFontName});
    if (std::optional<std::string> fontBytes = ReadThemeFile(overrideDir, fontName)) {
        m_fontData = std::move(*fontBytes);
        ImFontConfig fontConfig{};
        fontConfig.FontDataOwnedByAtlas = false;
        std::snprintf(fontConfig.Name, sizeof(fontConfig.Name), "3270 Nerd Font");
        m_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(m_fontData.data(), static_cast<int>(m_fontData.size()),
                                                            16.0f, &fontConfig);
    } else {
        devlog::warn<grp::shell>("Theme font {} not found; using default font", fontName);
    }

    if (std::optional<std::string> iconBytes = ReadThemeFile(overrideDir, kIconFontName)) {
        m_iconFontData = std::move(*iconBytes);
        ImFontConfig iconConfig{};
        iconConfig.FontDataOwnedByAtlas = false;
        std::snprintf(iconConfig.Name, sizeof(iconConfig.Name), "Font Awesome 6 Solid");
        m_iconFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            m_iconFontData.data(), static_cast<int>(m_iconFontData.size()), 32.0f, &iconConfig);
    } else {
        devlog::warn<grp::shell>("Icon font {} not found", kIconFontName);
    }

    const toml::table *assets = manifest["assets"].as_table();
    if (assets == nullptr) {
        devlog::info<grp::shell>("Theme manifest lists no assets; using procedural placeholders");
        return;
    }

    for (const auto &[key, node] : *assets) {
        const toml::table *entry = node.as_table();
        if (entry == nullptr) {
            continue;
        }
        const std::string id{key.str()};
        const std::optional<std::string> fileName = (*entry)["file"].value<std::string>();
        if (!fileName) {
            devlog::warn<grp::shell>("Theme asset {} has no file", id);
            continue;
        }

        const std::optional<std::string> fileData = ReadThemeFile(overrideDir, *fileName);
        if (!fileData) {
            devlog::warn<grp::shell>("Theme asset {}: file {} not found", id, *fileName);
            continue;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(fileData->data()),
                                                 static_cast<int>(fileData->size()), &width, &height, &channels, 4);
        if (decoded == nullptr) {
            devlog::warn<grp::shell>("Theme asset {}: could not decode {}: {}", id, *fileName, stbi_failure_reason());
            continue;
        }

        auto pixels = std::make_unique<PixelData>();
        pixels->width = static_cast<std::uint32_t>(width);
        pixels->height = static_cast<std::uint32_t>(height);
        pixels->rgba.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4u);
        stbi_image_free(decoded);

        // The setup function runs on creation and again whenever the graphics backend is recreated, so it reads
        // from the pixel buffer owned by the theme instead of a temporary one
        const PixelData *source = pixels.get();
        auto textureResult = graphicsService.CreateTexture(
            {
                .width = source->width,
                .height = source->height,
                .format = app::gfx::PixelFormat::R8G8B8A8_UNORM,
                .access = app::gfx::TextureAccess::Static,
                .name = "[Ymir] Shell " + id,
            },
            [source](app::gfx::GUITextureHandle, bool, void *data, size_t pitch) {
                auto *bytes = static_cast<std::uint8_t *>(data);
                const std::size_t rowSize = static_cast<std::size_t>(source->width) * 4u;
                for (std::uint32_t y = 0; y < source->height; ++y) {
                    std::memcpy(bytes + y * pitch, source->rgba.data() + y * rowSize, rowSize);
                }
            });
        if (!textureResult) {
            devlog::warn<grp::shell>("Theme asset {}: could not create texture: {}", id, textureResult.Error().message);
            continue;
        }

        ThemeAsset asset{};
        asset.texture = textureResult.Value();
        asset.textureWidth = static_cast<float>(width);
        asset.textureHeight = static_cast<float>(height);
        asset.frames = std::max((*entry)["frames"].value_or(1), 1);
        asset.columns = std::max((*entry)["columns"].value_or(asset.frames), 1);
        asset.fps = static_cast<float>((*entry)["fps"].value_or(0.0));
        if (const toml::array *slice = (*entry)["slice"].as_array(); slice != nullptr && slice->size() == 4) {
            for (std::size_t i = 0; i < 4; ++i) {
                asset.slice[i] = static_cast<float>((*slice)[i].value_or(0.0));
            }
        }

        m_pixels.push_back(std::move(pixels));
        m_assets.insert_or_assign(id, asset);
        devlog::debug<grp::shell>("Loaded theme asset {} ({}x{}, {} frames)", id, width, height, asset.frames);
    }

    devlog::info<grp::shell>("Loaded {} theme assets", m_assets.size());
}

const ThemeAsset *Theme::Find(std::string_view id) const {
    auto it = m_assets.find(std::string{id});
    return it != m_assets.end() ? &it->second : nullptr;
}

} // namespace platform::uwp::shell

#include "cover_cache.hpp"

#include "file_system.hpp"
#include "shell_log.hpp"

#include <app/services/graphics_service.hpp>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace platform::uwp::shell {

namespace {

    constexpr std::uint64_t kMaxCoverBytes = 16ull * 1024 * 1024;

    std::string ToUtf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    /// @brief Scales an RGBA image down by averaging boxes of source pixels.
    std::vector<std::uint8_t> BoxDownscale(const std::uint8_t *src, int srcW, int srcH, int dstW, int dstH) {
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(dstW) * dstH * 4);
        for (int y = 0; y < dstH; ++y) {
            const int y0 = y * srcH / dstH;
            const int y1 = std::max(y0 + 1, (y + 1) * srcH / dstH);
            for (int x = 0; x < dstW; ++x) {
                const int x0 = x * srcW / dstW;
                const int x1 = std::max(x0 + 1, (x + 1) * srcW / dstW);
                std::array<std::uint32_t, 4> sum{};
                for (int sy = y0; sy < y1; ++sy) {
                    for (int sx = x0; sx < x1; ++sx) {
                        const std::uint8_t *p = src + (static_cast<std::size_t>(sy) * srcW + sx) * 4;
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += p[c];
                        }
                    }
                }
                const auto count = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
                for (int c = 0; c < 4; ++c) {
                    dst[(static_cast<std::size_t>(y) * dstW + x) * 4 + c] = static_cast<std::uint8_t>(sum[c] / count);
                }
            }
        }
        return dst;
    }

} // namespace

CoverCache::CoverCache(std::filesystem::path folder)
    : m_folder(std::move(folder)) {}

const ThemeAsset *CoverCache::Find(app::services::GraphicsService &graphics, const GameEntry &game) {
    const std::string key = ToUtf8(game.path);
    if (auto it = m_covers.find(key); it != m_covers.end()) {
        return it->second != nullptr && it->second->valid ? &it->second->asset : nullptr;
    }
    if (m_loadBudget <= 0) {
        return nullptr;
    }
    --m_loadBudget;
    auto cover = Load(graphics, game);
    const ThemeAsset *asset = cover != nullptr ? &cover->asset : nullptr;
    m_covers.emplace(key, std::move(cover));
    return asset;
}

std::unique_ptr<CoverCache::Cover> CoverCache::Load(app::services::GraphicsService &graphics,
                                                    const GameEntry &game) const {
    // The product number first, then the disc image name; PNG or JPEG
    std::vector<std::filesystem::path> candidates;
    for (const char *ext : {".png", ".jpg", ".jpeg"}) {
        if (!game.productCode.empty()) {
            candidates.push_back(
                m_folder /
                std::filesystem::path{std::u8string{game.productCode.begin(), game.productCode.end()}}.concat(ext));
        }
        candidates.push_back(m_folder / std::filesystem::path{game.path.stem()}.concat(ext));
    }

    for (const std::filesystem::path &candidate : candidates) {
        const std::optional<std::string> data = fs::ReadWholeFile(candidate, kMaxCoverBytes);
        if (!data) {
            continue;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data->data()),
                                                 static_cast<int>(data->size()), &width, &height, &channels, 4);
        if (decoded == nullptr) {
            devlog::warn<grp::shell>("Could not decode the cover {}", candidate);
            continue;
        }

        auto cover = std::make_unique<Cover>();
        const float scale = std::min(1.0f, static_cast<float>(kMaxEdge) / static_cast<float>(std::max(width, height)));
        const int dstW = std::max(1, static_cast<int>(width * scale));
        const int dstH = std::max(1, static_cast<int>(height * scale));
        if (dstW != width || dstH != height) {
            cover->rgba = BoxDownscale(decoded, width, height, dstW, dstH);
        } else {
            cover->rgba.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
        }
        stbi_image_free(decoded);
        cover->width = static_cast<std::uint32_t>(dstW);
        cover->height = static_cast<std::uint32_t>(dstH);

        const Cover *source = cover.get();
        auto texture = graphics.CreateTexture(
            {
                .width = cover->width,
                .height = cover->height,
                .format = app::gfx::PixelFormat::R8G8B8A8_UNORM,
                .access = app::gfx::TextureAccess::Static,
                .name = "[Ymir] Shell cover",
            },
            [source](app::gfx::GUITextureHandle, bool, void *pixels, size_t pitch) {
                auto *bytes = static_cast<std::uint8_t *>(pixels);
                const std::size_t row = static_cast<std::size_t>(source->width) * 4u;
                for (std::uint32_t y = 0; y < source->height; ++y) {
                    std::memcpy(bytes + y * pitch, source->rgba.data() + y * row, row);
                }
            });
        if (!texture) {
            devlog::warn<grp::shell>("Could not create the cover texture for {}", candidate);
            return nullptr;
        }
        cover->asset.texture = texture.Value();
        cover->asset.textureWidth = static_cast<float>(cover->width);
        cover->asset.textureHeight = static_cast<float>(cover->height);
        cover->valid = true;
        return cover;
    }
    return nullptr;
}

} // namespace platform::uwp::shell

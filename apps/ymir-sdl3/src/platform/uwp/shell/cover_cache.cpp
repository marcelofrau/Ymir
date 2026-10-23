#include "cover_cache.hpp"

#include "file_system.hpp"
#include "shell_log.hpp"

#include <app/services/graphics_service.hpp>

#ifdef GetCurrentTime
    #undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.h>

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>

namespace platform::uwp::shell {

namespace {

    constexpr std::uint64_t kMaxCoverBytes = 16ull * 1024 * 1024;

    std::string ToUtf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    std::string MetadataKey(const GameEntry &game) {
        if (!game.productCode.empty()) {
            return game.productCode;
        }
        if (!game.title.empty()) {
            return game.title;
        }
        return ToUtf8(game.path.stem());
    }

    std::string SanitizeFilename(std::string_view name) {
        std::string out;
        out.reserve(name.size());
        for (const char ch : name) {
            if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' ||
                ch == '?' || ch == '*') {
                out.push_back('_');
            } else {
                out.push_back(ch);
            }
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
            out.pop_back();
        }
        return out.empty() ? std::string{"cover"} : out;
    }

    /// @brief Strips parenthesized tags like `(USA)`, `(Disc 1)`, `[!]` and normalizes whitespace/case.
    std::string CleanGameTitle(std::string_view raw) {
        std::string stripped;
        stripped.reserve(raw.size());
        int parenDepth = 0;
        int bracketDepth = 0;
        for (const char ch : raw) {
            if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')' && parenDepth > 0) {
                --parenDepth;
            } else if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']' && bracketDepth > 0) {
                --bracketDepth;
            } else if (parenDepth == 0 && bracketDepth == 0) {
                stripped.push_back(ch == '_' ? ' ' : ch);
            }
        }

        // Collapse multiple spaces and trim
        std::string collapsed;
        bool prevSpace = true;
        for (const char ch : stripped) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!prevSpace) {
                    collapsed.push_back(' ');
                    prevSpace = true;
                }
            } else {
                collapsed.push_back(ch);
                prevSpace = false;
            }
        }
        if (!collapsed.empty() && collapsed.back() == ' ') {
            collapsed.pop_back();
        }

        // If the header title is ALL CAPS, convert it to Title Case for Libretro / Wikipedia matching
        bool hasLower = false;
        bool hasUpper = false;
        for (const char ch : collapsed) {
            if (std::islower(static_cast<unsigned char>(ch))) {
                hasLower = true;
            } else if (std::isupper(static_cast<unsigned char>(ch))) {
                hasUpper = true;
            }
        }
        if (hasUpper && !hasLower) {
            bool wordStart = true;
            for (std::size_t i = 0; i < collapsed.size(); ++i) {
                const auto uc = static_cast<unsigned char>(collapsed[i]);
                if (std::isalpha(uc)) {
                    // Keep short Roman numerals (II, III, IV, VI) uppercase
                    collapsed[i] = wordStart ? static_cast<char>(std::toupper(uc))
                                             : static_cast<char>(std::tolower(uc));
                    wordStart = false;
                } else {
                    wordStart = (collapsed[i] == ' ' || collapsed[i] == '-' || collapsed[i] == ':');
                }
            }
        }
        return collapsed;
    }

    /// @brief Sanitizes characters according to Libretro Thumbnail naming rules (`&*/:`"`<>?\|` -> `_`).
    std::string ToLibretroName(std::string_view name) {
        std::string out;
        out.reserve(name.size());
        for (const char ch : name) {
            if (ch == '&' || ch == '*' || ch == '/' || ch == ':' || ch == '`' || ch == '<' || ch == '>' ||
                ch == '?' || ch == '\\' || ch == '|' || ch == '"') {
                out.push_back('_');
            } else {
                out.push_back(ch);
            }
        }
        return out;
    }

    std::string UrlEncode(std::string_view text) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(text.size() * 2);
        for (const unsigned char ch : text) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                out.push_back(static_cast<char>(ch));
            } else {
                out.push_back('%');
                out.push_back(kHex[(ch >> 4) & 0x0F]);
                out.push_back(kHex[ch & 0x0F]);
            }
        }
        return out;
    }

    std::optional<std::string> HttpGetBytes(winrt::Windows::Web::Http::HttpClient &client, std::string_view url) {
        try {
            const winrt::Windows::Foundation::Uri uri{winrt::to_hstring(url)};
            auto response = client.GetAsync(uri).get();
            if (!response.IsSuccessStatusCode()) {
                return std::nullopt;
            }
            auto buffer = response.Content().ReadAsBufferAsync().get();
            const std::uint32_t len = buffer.Length();
            if (len == 0 || len > kMaxCoverBytes) {
                return std::nullopt;
            }
            const auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
            std::string bytes(len, '\0');
            reader.ReadBytes(
                winrt::array_view<std::uint8_t>(reinterpret_cast<std::uint8_t *>(bytes.data()),
                                                reinterpret_cast<std::uint8_t *>(bytes.data() + len)));
            return bytes;
        } catch (...) {
            return std::nullopt;
        }
    }

    struct WikiInfo {
        std::string articleTitle;
        std::string extract;
        std::string thumbnailUrl;
    };

    WikiInfo QueryWikipedia(winrt::Windows::Web::Http::HttpClient &client, std::string_view gameTitle) {
        WikiInfo info{};
        if (gameTitle.empty()) {
            return info;
        }
        const std::string query = std::string{gameTitle} + " Sega Saturn video game";
        const std::string url =
            "https://en.wikipedia.org/w/api.php?action=query&format=json&generator=search&gsrsearch=" +
            UrlEncode(query) +
            "&gsrlimit=1&prop=extracts|pageimages&exintro=1&explaintext=1&exchars=520&piprop=thumbnail&pithumbsize=512";

        const std::optional<std::string> body = HttpGetBytes(client, url);
        if (!body) {
            return info;
        }

        try {
            const auto doc = nlohmann::json::parse(*body, nullptr, false);
            if (doc.is_discarded() || !doc.contains("query") || !doc["query"].contains("pages")) {
                return info;
            }
            for (const auto &[pageId, page] : doc["query"]["pages"].items()) {
                (void)pageId;
                if (page.contains("title") && page["title"].is_string()) {
                    info.articleTitle = page["title"].get<std::string>();
                    // Strip trailing " (video game)" or " (Sega Saturn)" from article title
                    if (const std::size_t paren = info.articleTitle.find(" ("); paren != std::string::npos) {
                        info.articleTitle.erase(paren);
                    }
                }
                if (page.contains("extract") && page["extract"].is_string()) {
                    info.extract = page["extract"].get<std::string>();
                    for (char &ch : info.extract) {
                        if (ch == '\n' || ch == '\r') {
                            ch = ' ';
                        }
                    }
                }
                if (page.contains("thumbnail") && page["thumbnail"].contains("source") &&
                    page["thumbnail"]["source"].is_string()) {
                    info.thumbnailUrl = page["thumbnail"]["source"].get<std::string>();
                }
                break;
            }
        } catch (...) {
        }
        return info;
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

CoverCache::CoverCache(std::filesystem::path folder, std::filesystem::path metadataPath)
    : m_folder(std::move(folder))
    , m_metadataPath(std::move(metadataPath)) {
    LoadMetadataCache();
    m_worker = std::jthread{[this](std::stop_token stopToken) { WorkerLoop(stopToken); }};
}

CoverCache::~CoverCache() {
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_cv.notify_all();
    }
}

void CoverCache::Clear() {
    {
        std::scoped_lock lock{m_mutex};
        m_queue.clear();
        m_attemptedCovers.clear();
        m_attemptedDescriptions.clear();
        m_readyCovers.clear();
        m_descriptions.clear();
    }
    m_covers.clear();
}

const ThemeAsset *CoverCache::Find(app::services::GraphicsService &graphics, const GameEntry &game) {
    const std::string key = ToUtf8(game.path);
    {
        std::scoped_lock lock{m_mutex};
        if (m_readyCovers.erase(key) > 0) {
            m_covers.erase(key);
        }
    }
    if (auto it = m_covers.find(key); it != m_covers.end()) {
        return it->second != nullptr && it->second->valid ? &it->second->asset : nullptr;
    }
    if (m_loadBudget <= 0) {
        return nullptr;
    }
    --m_loadBudget;
    auto cover = Load(graphics, game);
    const ThemeAsset *asset = cover != nullptr ? &cover->asset : nullptr;
    const bool missingCover = (cover == nullptr);
    m_covers.emplace(key, std::move(cover));
    if (missingCover) {
        EnqueueFetch(game, true, false);
    }
    return asset;
}

std::string CoverCache::FindDescription(const GameEntry &game) {
    const std::string metaKey = MetadataKey(game);
    {
        std::scoped_lock lock{m_mutex};
        if (auto it = m_descriptions.find(metaKey); it != m_descriptions.end()) {
            return it->second;
        }
    }
    EnqueueFetch(game, false, true);
    return {};
}

void CoverCache::EnqueueFetch(const GameEntry &game, bool needCover, bool needDescription) {
    const std::string coverKey = ToUtf8(game.path);
    const std::string metaKey = MetadataKey(game);

    std::scoped_lock lock{m_mutex};
    if (needCover && !m_attemptedCovers.insert(coverKey).second) {
        needCover = false;
    }
    if (needDescription && !m_attemptedDescriptions.insert(metaKey).second) {
        needDescription = false;
    }
    if (!needCover && !needDescription) {
        return;
    }

    m_queue.push_back(FetchTask{.game = game, .needCover = needCover, .needDescription = needDescription});
    m_cv.notify_one();
}

void CoverCache::WorkerLoop(std::stop_token stopToken) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }

    winrt::Windows::Web::Http::HttpClient client{nullptr};
    try {
        client = winrt::Windows::Web::Http::HttpClient{};
        client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"YmirSaturnEmulator/0.4.2 (Xbox UWP)");
    } catch (...) {
        return;
    }

    while (!stopToken.stop_requested()) {
        FetchTask task{};
        {
            std::unique_lock lock{m_mutex};
            m_cv.wait(lock, stopToken, [this] { return !m_queue.empty(); });
            if (stopToken.stop_requested() || m_queue.empty()) {
                break;
            }
            // Take the newest request first so the currently focused game in the grid gets priority
            task = std::move(m_queue.back());
            m_queue.pop_back();
        }

        const std::string stem = ToUtf8(task.game.path.stem());
        const std::string cleanStem = CleanGameTitle(stem);
        const std::string cleanTitle = CleanGameTitle(task.game.title);
        const std::string searchTitle = !cleanStem.empty() ? cleanStem : cleanTitle;

        const WikiInfo wiki = QueryWikipedia(client, searchTitle);
        if (!wiki.extract.empty()) {
            const std::string metaKey = MetadataKey(task.game);
            {
                std::scoped_lock lock{m_mutex};
                m_descriptions[metaKey] = wiki.extract;
            }
            SaveMetadataCache();
        }

        if (task.needCover) {
            std::vector<std::string> regions;
            if (task.game.region.find('U') != std::string::npos) {
                regions.emplace_back("USA");
            }
            if (task.game.region.find('E') != std::string::npos) {
                regions.emplace_back("Europe");
            }
            if (task.game.region.find('J') != std::string::npos) {
                regions.emplace_back("Japan");
            }
            for (const char *fallbackRegion : {"USA", "Japan", "Europe", "USA, Europe"}) {
                if (std::find(regions.begin(), regions.end(), fallbackRegion) == regions.end()) {
                    regions.emplace_back(fallbackRegion);
                }
            }

            std::vector<std::string> candidateNames;
            if (!stem.empty()) {
                candidateNames.push_back(ToLibretroName(stem));
            }
            for (const std::string &base : {wiki.articleTitle, cleanStem, cleanTitle}) {
                if (base.empty()) {
                    continue;
                }
                const std::string safeBase = ToLibretroName(base);
                for (const std::string &reg : regions) {
                    candidateNames.push_back(safeBase + " (" + reg + ")");
                }
            }

            std::optional<std::string> imageBytes;
            for (const std::string &candidate : candidateNames) {
                if (stopToken.stop_requested()) {
                    return;
                }
                const std::string url =
                    "https://thumbnails.libretro.com/Sega%20-%20Saturn/Named_Boxarts/" + UrlEncode(candidate) + ".png";
                imageBytes = HttpGetBytes(client, url);
                if (imageBytes) {
                    devlog::info<grp::shell>("Downloaded cover art from Libretro CDN: {}", candidate);
                    break;
                }
            }

            if (!imageBytes && !wiki.thumbnailUrl.empty()) {
                imageBytes = HttpGetBytes(client, wiki.thumbnailUrl);
                if (imageBytes) {
                    devlog::info<grp::shell>("Downloaded cover art from Wikipedia for {}", searchTitle);
                }
            }

            if (imageBytes) {
                // Verify that stb_image can decode it before caching to disk
                int w = 0;
                int h = 0;
                int c = 0;
                if (stbi_info_from_memory(reinterpret_cast<const stbi_uc *>(imageBytes->data()),
                                          static_cast<int>(imageBytes->size()), &w, &h, &c) != 0 &&
                    w > 0 && h > 0) {
                    const std::string fileBase =
                        SanitizeFilename(!task.game.productCode.empty() ? task.game.productCode : stem);
                    const std::filesystem::path savePath =
                        m_folder / std::filesystem::path{std::u8string{fileBase.begin(), fileBase.end()}}.concat(".png");
                    if (fs::WriteWholeFile(savePath, *imageBytes)) {
                        std::scoped_lock lock{m_mutex};
                        m_readyCovers.insert(ToUtf8(task.game.path));
                    }
                }
            }
        }
    }
}

void CoverCache::LoadMetadataCache() {
    if (m_metadataPath.empty()) {
        return;
    }
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(m_metadataPath, ec)) {
        return;
    }
    auto parseResult = toml::parse_file(m_metadataPath.native());
    if (parseResult.failed()) {
        return;
    }
    std::scoped_lock lock{m_mutex};
    if (const toml::array *list = parseResult.table()["entry"].as_array()) {
        for (const toml::node &node : *list) {
            if (const toml::table *tbl = node.as_table()) {
                const std::string key = (*tbl)["key"].value_or(std::string{});
                const std::string desc = (*tbl)["description"].value_or(std::string{});
                if (!key.empty() && !desc.empty()) {
                    m_descriptions[key] = desc;
                }
            }
        }
    }
}

void CoverCache::SaveMetadataCache() const {
    if (m_metadataPath.empty()) {
        return;
    }
    toml::array list;
    {
        std::scoped_lock lock{m_mutex};
        for (const auto &[key, desc] : m_descriptions) {
            if (!desc.empty()) {
                list.push_back(toml::table{{"key", key}, {"description", desc}});
            }
        }
    }
    toml::table tbl{{"entry", std::move(list)}};
    std::error_code ec{};
    std::filesystem::create_directories(m_metadataPath.parent_path(), ec);
    std::ofstream out{m_metadataPath, std::ios::binary | std::ios::trunc};
    if (out) {
        out << tbl;
    }
}

std::unique_ptr<CoverCache::Cover> CoverCache::Load(app::services::GraphicsService &graphics,
                                                    const GameEntry &game) const {
    // The product number first, then the disc image name; PNG or JPEG
    std::vector<std::filesystem::path> candidates;
    for (const char *ext : {".png", ".jpg", ".jpeg"}) {
        if (!game.productCode.empty()) {
            const std::string safeCode = SanitizeFilename(game.productCode);
            candidates.push_back(
                m_folder / std::filesystem::path{std::u8string{safeCode.begin(), safeCode.end()}}.concat(ext));
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

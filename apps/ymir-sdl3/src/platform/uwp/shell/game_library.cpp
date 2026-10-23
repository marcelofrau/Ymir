#include "game_library.hpp"

#include "file_system.hpp"
#include "shell_log.hpp"

#include <ymir/media/disc.hpp>
#include <ymir/media/loader/loader.hpp>
#include <ymir/util/string.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace platform::uwp::shell {

namespace {

    /// @brief How deep below each library folder the scan looks.
    constexpr int kMaxDepth = 4;

    /// @brief Determines whether a file is a disc image the emulator loads directly. Track files (.bin, .img) are
    /// skipped: they are reached through their .cue or .ccd sheet.
    bool IsDiscImage(const std::filesystem::path &path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".cue" || ext == ".chd" || ext == ".iso" || ext == ".ccd" || ext == ".mds";
    }

    std::string ToUtf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    std::filesystem::path FromUtf8(const std::string &text) {
        return std::filesystem::path{std::u8string{text.begin(), text.end()}};
    }

    std::string Trim(std::string text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.pop_back();
        }
        std::size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
            ++start;
        }
        return text.substr(start);
    }

    void Collect(const std::filesystem::path &folder, int depth, std::vector<fs::DirectoryEntry> &out,
                 const std::stop_token &stopToken) {
        std::vector<fs::DirectoryEntry> entries;
        if (stopToken.stop_requested() || !fs::ListDirectory(folder, entries)) {
            return;
        }
        for (fs::DirectoryEntry &entry : entries) {
            if (entry.isDirectory) {
                if (depth < kMaxDepth) {
                    Collect(entry.path, depth + 1, out, stopToken);
                }
            } else if (IsDiscImage(entry.path)) {
                out.push_back(std::move(entry));
            }
        }
    }

} // namespace

GameLibrary::GameLibrary(std::filesystem::path cachePath)
    : m_cachePath(std::move(cachePath)) {}

GameLibrary::~GameLibrary() {
    m_scanThread = {}; // requests the stop and joins
}

void GameLibrary::LoadCache() {
    std::error_code error{};
    if (!std::filesystem::is_regular_file(m_cachePath, error)) {
        return;
    }
    auto parseResult = toml::parse_file(m_cachePath.native());
    if (parseResult.failed()) {
        devlog::warn<grp::shell>("Ignoring an invalid game library cache: {}", parseResult.error().description());
        return;
    }

    std::vector<GameEntry> games;
    if (const toml::array *list = parseResult.table()["game"].as_array()) {
        for (const toml::node &node : *list) {
            const toml::table *tbl = node.as_table();
            if (tbl == nullptr) {
                continue;
            }
            GameEntry game{};
            game.path = FromUtf8((*tbl)["path"].value_or(std::string{}));
            if (game.path.empty()) {
                continue;
            }
            game.title = (*tbl)["title"].value_or(std::string{});
            game.productCode = (*tbl)["product"].value_or(std::string{});
            game.makerId = (*tbl)["maker"].value_or(std::string{});
            game.version = (*tbl)["version"].value_or(std::string{});
            game.deviceInfo = (*tbl)["device"].value_or(std::string{});
            game.region = (*tbl)["region"].value_or(std::string{});
            game.peripherals = (*tbl)["peripherals"].value_or(std::string{});
            game.releaseDate = (*tbl)["date"].value_or(std::string{});
            game.size = static_cast<std::uint64_t>((*tbl)["size"].value_or(std::int64_t{0}));
            game.lastWrite = static_cast<std::uint64_t>((*tbl)["mtime"].value_or(std::int64_t{0}));
            games.push_back(std::move(game));
        }
    }

    std::scoped_lock lock{m_mutex};
    m_games = std::move(games);
    ++m_version;
    devlog::info<grp::shell>("Game library: {} games from the cache", m_games.size());
}

void GameLibrary::StartScan(std::vector<std::filesystem::path> folders) {
    if (m_scanning.exchange(true)) {
        return;
    }
    m_scanProgress = 0;
    m_scanThread = std::jthread{[this, folders = std::move(folders)](std::stop_token stopToken) mutable {
        ScanThread(stopToken, std::move(folders));
    }};
}

std::vector<GameEntry> GameLibrary::Games() const {
    std::scoped_lock lock{m_mutex};
    return m_games;
}

void GameLibrary::ScanThread(std::stop_token stopToken, std::vector<std::filesystem::path> folders) {
    // Known images are reused when their size and modification time did not change
    std::unordered_map<std::string, GameEntry> known;
    for (GameEntry &game : Games()) {
        known.emplace(ToUtf8(game.path), std::move(game));
    }

    std::vector<fs::DirectoryEntry> images;
    for (const std::filesystem::path &folder : folders) {
        Collect(folder, 0, images, stopToken);
    }
    devlog::info<grp::shell>("Game library: scanning {} disc images in {} folders", images.size(), folders.size());

    std::vector<GameEntry> games;
    for (const fs::DirectoryEntry &image : images) {
        if (stopToken.stop_requested()) {
            m_scanning = false;
            return;
        }
        ++m_scanProgress;

        // Reuse known entry only if it already has the extended header fields populated
        const std::string key = ToUtf8(image.path);
        if (auto it = known.find(key);
            it != known.end() && it->second.size == image.size && it->second.lastWrite == image.lastWrite &&
            !it->second.makerId.empty()) {
            games.push_back(it->second);
            continue;
        }

        // Only the header is needed; the image is not preloaded and is closed again right away
        ymir::media::Disc disc{};
        if (!ymir::media::LoadDisc(image.path, disc, false, [](ymir::media::MessageType, std::string) {})) {
            devlog::debug<grp::shell>("Game library: skipping {}, not a valid disc image", image.path);
            continue;
        }
        const ymir::media::SaturnHeader &header = disc.header;

        GameEntry game{};
        game.path = image.path;
        game.title = Trim(::util::TranslateSaturnString(header.gameTitle));
        if (game.title.empty()) {
            game.title = ToUtf8(image.path.stem());
        }
        game.productCode = Trim(header.productNumber);
        game.makerId = Trim(header.makerID);
        game.version = Trim(header.version);
        game.deviceInfo = Trim(header.deviceInfo);
        game.region = Trim(header.rawCompatAreaCode);
        game.peripherals = Trim(header.rawCompatPeripherals);
        game.releaseDate = Trim(header.releaseDate);
        game.size = image.size;
        game.lastWrite = image.lastWrite;
        games.push_back(std::move(game));
    }

    std::sort(games.begin(), games.end(), [](const GameEntry &a, const GameEntry &b) { return a.title < b.title; });
    SaveCache(games);
    {
        std::scoped_lock lock{m_mutex};
        m_games = std::move(games);
        ++m_version;
        devlog::info<grp::shell>("Game library: {} games", m_games.size());
    }
    m_scanning = false;
}

void GameLibrary::SaveCache(const std::vector<GameEntry> &games) const {
    toml::array list;
    for (const GameEntry &game : games) {
        list.push_back(toml::table{
            {"path", ToUtf8(game.path)},
            {"title", game.title},
            {"product", game.productCode},
            {"maker", game.makerId},
            {"version", game.version},
            {"device", game.deviceInfo},
            {"region", game.region},
            {"peripherals", game.peripherals},
            {"date", game.releaseDate},
            {"size", static_cast<std::int64_t>(game.size)},
            {"mtime", static_cast<std::int64_t>(game.lastWrite)},
        });
    }
    toml::table tbl{{"game", std::move(list)}};

    std::error_code error{};
    std::filesystem::create_directories(m_cachePath.parent_path(), error);
    std::ofstream out{m_cachePath, std::ios::binary | std::ios::trunc};
    if (!out) {
        devlog::warn<grp::shell>("Could not save the game library cache to {}", m_cachePath);
        return;
    }
    out << tbl;
}

} // namespace platform::uwp::shell

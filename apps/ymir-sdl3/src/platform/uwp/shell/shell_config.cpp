#include "shell_config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>

namespace platform::uwp::shell {

namespace {

    constexpr std::string_view kModeShell = "Shell";
    constexpr std::string_view kModeDesktop = "Desktop";

} // namespace

bool ShellConfig::Load(const std::filesystem::path &path) {
    *this = ShellConfig{};

    std::error_code error{};
    if (!std::filesystem::is_regular_file(path, error)) {
        return false;
    }

    auto parseResult = toml::parse_file(path.native());
    if (parseResult.failed()) {
        return false;
    }
    auto &data = parseResult.table();

    if (auto opt = data["Shell"]["Mode"].value<std::string>()) {
        mode = *opt == kModeDesktop ? UIMode::Desktop : UIMode::Shell;
    }
    if (auto opt = data["Shell"]["SafeArea"].value<double>()) {
        safeArea = std::clamp(static_cast<float>(*opt), 0.0f, 0.25f);
    }
    if (auto opt = data["Music"]["Enabled"].value<bool>()) {
        musicEnabled = *opt;
    }
    if (auto opt = data["Music"]["Volume"].value<double>()) {
        musicVolume = std::clamp(static_cast<float>(*opt), 0.0f, 1.0f);
    }
    if (auto opt = data["Music"]["ShuffleTracks"].value<bool>()) {
        musicShuffle = *opt;
    }
    if (auto opt = data["Shell"]["Scanlines"].value<bool>()) {
        scanlines = *opt;
    }
    if (auto opt = data["Shell"]["WizardCompleted"].value<bool>()) {
        wizardCompleted = *opt;
    }
    if (const toml::array *folders = data["Library"]["Folders"].as_array()) {
        for (const toml::node &node : *folders) {
            if (auto folder = node.value<std::string>(); folder && !folder->empty()) {
                libraryFolders.emplace_back(std::u8string{folder->begin(), folder->end()});
            }
        }
    }
    if (!data["Shell"]["WizardCompleted"].is_boolean() && !libraryFolders.empty()) {
        wizardCompleted = true;
    }
    return true;
}

bool ShellConfig::Save(const std::filesystem::path &path) const {
    toml::table shellTbl{
        {"Mode", std::string{mode == UIMode::Desktop ? kModeDesktop : kModeShell}},
        {"SafeArea", static_cast<double>(safeArea)},
        {"Scanlines", scanlines},
        {"WizardCompleted", wizardCompleted},
    };
    toml::table musicTbl{
        {"Enabled", musicEnabled},
        {"Volume", static_cast<double>(musicVolume)},
        {"ShuffleTracks", musicShuffle},
    };
    toml::array folders;
    for (const std::filesystem::path &folder : libraryFolders) {
        const std::u8string text = folder.u8string();
        folders.push_back(std::string{text.begin(), text.end()});
    }
    toml::table libraryTbl{{"Folders", std::move(folders)}};
    toml::table tbl{{"Shell", std::move(shellTbl)}, {"Music", std::move(musicTbl)}, {"Library", std::move(libraryTbl)}};

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        return false;
    }
    out << tbl;
    return static_cast<bool>(out);
}

} // namespace platform::uwp::shell

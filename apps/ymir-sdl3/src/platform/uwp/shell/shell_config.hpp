#pragma once

/**
@file
@brief Persistent configuration of the Xbox shell.

The shell keeps its own small `shell.toml` in the profile root instead of adding fields to `app::Settings`. The
desktop settings code is one of the busiest areas upstream, and keeping the shell out of it keeps the fork easy to
rebase.
*/

#include <filesystem>
#include <vector>

namespace platform::uwp::shell {

/// @brief Which user interface the app shows on the console.
enum class UIMode {
    Shell,   // the gamepad-first shell
    Desktop, // the original desktop ImGui interface, kept as an advanced mode
};

/// @brief Settings of the shell, persisted in `<profile>/shell.toml`.
struct ShellConfig {
    /// @brief The interface to show.
    UIMode mode = UIMode::Shell;

    /// @brief Fraction of the screen kept free on each side for TV overscan.
    float safeArea = 0.05f;

    /// @brief Whether the background music plays on the shell screens.
    bool musicEnabled = true;

    /// @brief Background music volume, in [0, 1].
    float musicVolume = 0.5f;

    /// @brief Whether the background music tracks play in random order. Defaults to sequential playback.
    bool musicShuffle = false;

    /// @brief The folders the game library is scanned from, added by the user with the folder picker.
    std::vector<std::filesystem::path> libraryFolders;

    /// @brief Whether the faint CRT scanlines are drawn over the shell.
    bool scanlines = true;

    /// @brief Whether the initial Welcome / Setup Wizard has been completed.
    bool wizardCompleted = false;

    /// @brief Loads the configuration. Missing or invalid files and fields fall back to the defaults.
    /// @param[in] path the path to `shell.toml`
    /// @return `true` if the file existed and was parsed, `false` if the defaults were used
    bool Load(const std::filesystem::path &path);

    /// @brief Saves the configuration.
    /// @param[in] path the path to `shell.toml`
    /// @return `true` on success, `false` if the file could not be written
    bool Save(const std::filesystem::path &path) const;
};

} // namespace platform::uwp::shell

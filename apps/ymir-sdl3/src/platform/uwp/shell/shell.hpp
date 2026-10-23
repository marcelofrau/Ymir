#pragma once

/**
@file
@brief The Xbox shell: a gamepad-first user interface themed after the Sega Saturn BIOS.

The shell replaces the desktop ImGui interface on the console. It is a self-contained module owned by `app::App`
and wired in through a handful of guarded one-line hooks, so the desktop interface and the upstream code around it
stay untouched. The desktop interface remains reachable as an advanced mode.

Screens:
- **Home**: the game library as a hologram grid floating over the starfield, the action spheres on the cockpit
  console, the audio-reactive cubes on the pods, the dot-matrix LCD and the controller legend. The emulation is
  paused and the background music plays.
- **In game**: the shell is hidden and the gamepad drives the emulated Saturn. View opens the pause menu.
- **Paused**: the pause menu as an overlay over the frozen game (save states, reset, change disc, settings...).

Settings and every other tool are overlay windows (see `Overlay`) opened from the home screen or the pause menu.

In desktop mode the shell draws nothing and only watches for View being held for one second, which switches back.

See `docs/uwp/shell/README.md` for the architecture and the decisions behind it.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <platform/uwp/shell/audio_cube.hpp>
#include <platform/uwp/shell/cover_cache.hpp>
#include <platform/uwp/shell/game_library.hpp>
#include <platform/uwp/shell/layout.hpp>
#include <platform/uwp/shell/marquee.hpp>
#include <platform/uwp/shell/music_player.hpp>
#include <platform/uwp/shell/overlay.hpp>
#include <platform/uwp/shell/screens/home_screen.hpp>
#include <platform/uwp/shell/shell_config.hpp>
#include <platform/uwp/shell/starfield.hpp>
#include <platform/uwp/shell/theme.hpp>
#include <platform/uwp/shell/widgets.hpp>

#include <SDL3/SDL_gamepad.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace app {

struct SharedContext;

} // namespace app

namespace platform::uwp::shell {

/// @brief The gamepad-first user interface of the Xbox build.
class Shell {
public:
    /// @brief Creates the shell. Nothing is loaded until `LoadConfig()` and `Init()` are called.
    /// @param[in] context the shared application context, used to reach the services and the emulator
    explicit Shell(app::SharedContext &context);

    Shell(const Shell &) = delete;
    Shell &operator=(const Shell &) = delete;

    /// @brief Loads `shell.toml` from the profile. Call once the profile path is known, before `IsActive()` is used.
    void LoadConfig();

    /// @brief Loads the theme, the library cache and the music. Call once the graphics context and the ImGui fonts
    /// are ready.
    void Init();

    /// @brief Determines whether the shell is the active interface, as opposed to the desktop interface.
    [[nodiscard]] bool IsActive() const {
        return m_config.mode == UIMode::Shell;
    }

    /// @brief Determines whether the gamepad drives the shell rather than the emulated Saturn.
    [[nodiscard]] bool OwnsGamepad() const {
        return IsActive() && m_screen != Screen::InGame;
    }

    /// @brief Handles a gamepad button event. Called by the app for every button event, before routing it.
    /// @param[in] button the button
    /// @param[in] down `true` if the button was pressed, `false` if it was released
    /// @param[in] timestampNs the event timestamp in nanoseconds
    void OnGamepadButton(SDL_GamepadButton button, bool down, std::uint64_t timestampNs);

    /// @brief Draws the shell. Call every frame between `ImGui::NewFrame()` and the desktop windows, so the desktop
    /// windows and modals opened from the shell stay on top of it.
    void Draw();

    /// @brief Notifies the shell that a disc was loaded, so it can switch to the game.
    /// Safe to call from the emulator thread.
    void OnDiscLoaded();

private:
    enum class Screen {
        Home,   // the library and the action spheres; the emulation is paused
        InGame, // the shell is hidden and the gamepad drives the Saturn
        Paused, // the pause menu over the frozen game
    };

    // Screens and transitions
    void ShowHome();
    void EnterGame();
    void OpenPause();
    void PauseEmulation();
    void SetMode(UIMode mode);
    void SaveConfig() const;

    // Home actions
    void HandleHomeAction(const HomeResult &result);
    void LaunchGame(const std::filesystem::path &path);
    void OpenLoadDisc();
    void OpenBiosPicker();
    void ProcessPickedBios();
    void RescanBios();
    void OpenFolderPicker();
    void ProcessPickedFolder();
    void StartLibraryScan();
    void SwitchToDesktop();
    void Quit();

    // Overlays (settings_pages.cpp)
    [[nodiscard]] OverlayScreen BuildSettingsScreen();
    [[nodiscard]] OverlayScreen BuildPauseScreen();

    // Per-frame
    [[nodiscard]] HomeModel BuildHomeModel(bool inputEnabled);
    void RefreshLibrary();
    void DrawHome();
    void DrawPaused();
    void DrawInGameHint();
    void DrawCubes(const DrawContext &ctx);
    void UpdateMarquee(const DrawContext &ctx, const HomeModel &model);
    [[nodiscard]] DrawContext MakeContext(ImDrawList *drawList);

    app::SharedContext &m_context;

    ShellConfig m_config;
    std::filesystem::path m_configPath;
    bool m_initialized = false;

    Layout m_layout;
    Starfield m_starfield;
    std::vector<ProjectedStar> m_starScratch;
    Theme m_theme;
    ShapeCache m_shapes;
    HomeScreen m_home;
    Overlay m_overlay;

    MusicPlayer m_music;
    AudioCube m_cubes[2]; // left and right pod, following the left and right music channels

    std::unique_ptr<GameLibrary> m_library;
    std::unique_ptr<CoverCache> m_covers;
    std::vector<GameEntry> m_games;  // copy of the library, refreshed when it changes
    std::vector<GameEntry> m_recent; // recently loaded discs, newest first
    std::uint64_t m_libraryVersion = ~std::uint64_t{0};
    bool m_wasScanning = false;

    Marquee m_marquee;
    std::chrono::steady_clock::time_point m_lastMessageTime{}; // newest app message already sent to the marquee
    std::string m_idleTray;                                    // tray status the idle marquee texts were built for

    Screen m_screen = Screen::Home;
    bool m_sessionStarted = false; // the Saturn has been started at least once since launch
    bool m_pausedByShell = false;  // the shell paused the emulation and must resume it when leaving
    bool m_focusWindow = true;     // give the shell window the ImGui focus on the next frame
    bool m_overlayWasOpen = false;
    double m_time = 0.0; // animation clock, in seconds
    double m_gameEnteredTime = 0.0;
    std::size_t m_pauseSlot = 0; // save state slot selected in the pause menu

    std::atomic_bool m_discLoaded{false};

    std::optional<std::uint64_t> m_viewDownTimestamp; // desktop mode: when View was pressed

    // Written by the file and folder picker callbacks, which may run on another thread
    std::mutex m_pickMutex;
    std::optional<std::filesystem::path> m_pickedBios;
    std::optional<std::filesystem::path> m_pickedFolder;
};

} // namespace platform::uwp::shell

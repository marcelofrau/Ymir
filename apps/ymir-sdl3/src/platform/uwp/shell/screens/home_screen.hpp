#pragma once

/**
@file
@brief Home screen of the Xbox shell.

The sky between the two dashboards holds either:
- the first-run Welcome / Setup Wizard (when no IPL ROM is loaded or initial setup has not been completed),
- the Saturn BIOS 3x2 sphere menu floating over the stars and overlapping the bottom dashboard, or
- the game library as a grid of hologram tiles with two tabs (all games, recent discs) switched with LB/RB and a
  detailed game info card over the bottom dashboard.
*/

#include <platform/uwp/shell/cover_cache.hpp>
#include <platform/uwp/shell/game_library.hpp>
#include <platform/uwp/shell/widgets.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace platform::uwp::shell {

/// @brief What the home screen shows. Filled in by the shell every frame.
struct HomeModel {
    bool biosMissing = false;   // no IPL ROM is loaded, so nothing can run yet
    bool showWizard = false;    // show the Welcome / First-Run Setup Wizard
    bool sessionActive = false; // the emulated Saturn has been started and is paused behind the shell
    std::string biosName;       // file name of the configured IPL ROM, empty when missing
    std::string discTitle;      // title of the loaded disc, empty if there is none
    std::string iplFolder;      // where IPL ROM files are picked up from, shown in the wizard
    std::size_t folderCount = 0; // number of configured game library folders

    const std::vector<GameEntry> *games = nullptr;  // the whole library, sorted by title
    const std::vector<GameEntry> *recent = nullptr; // recently loaded discs, newest first
    bool scanning = false;                          // a library scan is running
    std::size_t scanProgress = 0;                   // disc images looked at by the running scan
    bool inputEnabled = false;                      // the shell window has the focus, so shortcuts apply
    CoverCache *covers = nullptr;
};

/// @brief An option chosen on the home screen, carried out by the shell.
enum class HomeAction {
    None,
    BrowseGames,    // switch between the Saturn BIOS sphere menu and the game library grid
    Start,          // start or resume the emulated Saturn
    LoadDisc,       // pick a disc image with the file picker
    LaunchGame,     // load the game of `HomeResult::game`
    AddFolder,      // pick a folder to add to the library
    Rescan,         // scan the library folders again
    SelectBios,     // pick an IPL ROM file
    RescanBios,     // scan the IPL ROM folder again
    CompleteWizard, // mark the initial setup wizard as completed and open the main menu
    Settings,       // open the settings
    Desktop,        // switch to the desktop interface
    Quit,           // quit the app
};

/// @brief The outcome of a frame of the home screen.
struct HomeResult {
    HomeAction action = HomeAction::None;
    std::filesystem::path game; // for HomeAction::LaunchGame
};

/// @brief Draws the home screen and reports what the user chose.
class HomeScreen {
public:
    /// @brief Moves the focus to the first option on the next frame. Call when the screen is shown.
    void RequestFocus() {
        m_focusRequested = true;
    }

    /// @brief Returns true when the library grid sub-view is open inside the home screen.
    [[nodiscard]] bool InLibraryView() const {
        return m_view == View::Library;
    }

    /// @brief Switches the home screen to the initial Saturn BIOS sphere menu.
    void ShowSpheres() {
        m_view = View::Spheres;
        m_focusRequested = true;
    }

    /// @brief Switches the home screen to the game library grid.
    void ShowLibrary() {
        m_view = View::Library;
        m_focusRequested = true;
    }

    /// @brief Draws the screen. Must be called inside the shell window.
    /// @param[in] ctx the draw context
    /// @param[in] model what to show
    /// @return what the user chose this frame
    HomeResult Draw(const DrawContext &ctx, const HomeModel &model);

    /// @brief Retrieves the controller legend for the current focus.
    [[nodiscard]] std::vector<ButtonHint> Legend(const HomeModel &model) const;

private:
    enum class View { Spheres, Library };
    enum class Tab { All, Recent };
    enum class FocusKind { Sphere, Game, AddFolder };

    void DrawWizard(const DrawContext &ctx, const HomeModel &model, HomeResult &result);
    void DrawGrid(const DrawContext &ctx, const HomeModel &model, HomeResult &result);
    void DrawTabs(const DrawContext &ctx, const HomeModel &model);
    void DrawGameDetails(const DrawContext &ctx, const HomeModel &model);
    void DrawSpheres(const DrawContext &ctx, const HomeModel &model, HomeResult &result);
    void DrawSphereHeader(const DrawContext &ctx, const HomeModel &model);

    static constexpr std::size_t kMaxSpheres = 6;

    std::array<SphereState, kMaxSpheres> m_spheres{};
    std::vector<TileState> m_tiles;
    TileState m_addFolderTile{};

    View m_view = View::Spheres;
    Tab m_tab = Tab::All;
    bool m_focusRequested = true;
    bool m_lastShowWizard = false;
    bool m_lastBiosMissing = false;

    FocusKind m_focusKind = FocusKind::Sphere;
    std::size_t m_focusIndex = 0;
};

} // namespace platform::uwp::shell

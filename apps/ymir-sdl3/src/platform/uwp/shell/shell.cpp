#include "shell.hpp"

#include "shell_log.hpp"

#include <platform/uwp/shell/dot_matrix.hpp>
#include <platform/uwp/shell/file_system.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/imgui_data.hpp>
#include <app/profile.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <app/services/disc_service.hpp>
#include <app/services/file_dialog_service.hpp>
#include <app/services/graphics_service.hpp>
#include <app/services/rom_service.hpp>
#include <app/services/window_manager_service.hpp>

#include <util/rom_loader.hpp>

#include <ymir/media/cd_interface.hpp>
#include <ymir/media/saturn_header.hpp>
#include <ymir/util/string.hpp>

#include <SDL3/SDL_events.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <thread>

namespace platform::uwp::shell {

namespace {

    /// @brief How long View must be held in desktop mode to switch back to the shell.
    constexpr std::uint64_t kHoldToReturnNs = 1'000'000'000ull;

    /// @brief How long the "View: menu" hint stays on screen after a game starts, in seconds.
    constexpr double kInGameHintSeconds = 5.0;

    /// @brief The version shown on the LCD of the top dashboard, next to the title.
    constexpr std::string_view kVersionText = "v" Ymir_VERSION;

    std::string ToUtf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    /// @brief Stores the first path of a file dialog selection into a slot guarded by a mutex.
    void StorePick(std::mutex &mutex, std::optional<std::filesystem::path> &slot, const char *const *filelist) {
        if (filelist == nullptr || *filelist == nullptr) {
            return; // cancelled or failed
        }
        const std::string file = *filelist;
        std::scoped_lock lock{mutex};
        slot = std::filesystem::path{std::u8string{file.begin(), file.end()}};
    }

} // namespace

Shell::Shell(app::SharedContext &context)
    : m_context(context) {}

void Shell::LoadConfig() {
    m_configPath = m_context.profile.GetPath(app::ProfilePath::Root) / "shell.toml";
    if (!m_config.Load(m_configPath)) {
        // First launch on this console: write the defaults so the file can be inspected through the Device Portal
        SaveConfig();
    }
    devlog::info<grp::shell>("Interface mode: {}", IsActive() ? "shell" : "desktop");
}

void Shell::Init() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    const std::filesystem::path root = m_context.profile.GetPath(app::ProfilePath::Root);
    auto &graphics = m_context.serviceLocator.GetRequired<app::services::GraphicsService>();
    m_theme.Load(graphics, root / "shell-theme");

    // The marquee shows what happens from now on; the startup chatter before the shell existed is skipped
    m_lastMessageTime = std::chrono::steady_clock::now();

    // The library shows the results of the last scan right away; nothing is scanned until the user asks for it
    m_library = std::make_unique<GameLibrary>(m_context.profile.GetPath(app::ProfilePath::PersistentState) /
                                              "shell_library.toml");
    m_library->LoadCache();

    // Covers and background music are the user's own files in the profile. The folders are created here so they
    // show up in the Device Portal file explorer
    const std::filesystem::path coversFolder = root / "covers";
    const std::filesystem::path musicFolder = root / "music";
    fs::CreateDirectories(coversFolder);
    fs::CreateDirectories(musicFolder);
    m_covers = std::make_unique<CoverCache>(coversFolder, m_context.profile.GetPath(app::ProfilePath::PersistentState) /
                                                              "shell_metadata.toml");
    m_music.Start(musicFolder, m_config.musicShuffle);
    m_music.SetVolume(m_config.musicVolume);

    if (IsActive()) {
        ShowHome();
        m_music.PlaySfx(Sfx::Boot);
    }
}

// -----------------------------------------------------------------------------
// Input

void Shell::OnGamepadButton(SDL_GamepadButton button, bool down, std::uint64_t timestampNs) {
    (void)timestampNs;
    if (!IsActive()) {
        // Desktop mode (Original GUI): pressing View returns to the Xbox shell
        if (button == SDL_GAMEPAD_BUTTON_BACK && down) {
            m_viewDownTimestamp.reset();
            m_music.PlaySfx(Sfx::Scroll);
            m_lastScrollSfxTime = m_time;
            SetMode(UIMode::Shell);
            ShowHome();
        }
        return;
    }

    if (!down) {
        return;
    }

    // Any button pressed while the shell owns the gamepad plays the scroll/button sound
    if (m_screen != Screen::InGame) {
        m_music.PlaySfx(Sfx::Scroll);
        m_lastScrollSfxTime = m_time;
    }

    const bool isMenuButton = button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_LEFT_STICK ||
                              button == SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (!isMenuButton) {
        return;
    }

    // View, L3 or R3 opens the pause menu in game and closes it again; on the home screen View goes back to a started
    // game. None of these buttons exist on the 8-button Saturn control pad, so no game input is lost
    switch (m_screen) {
    case Screen::InGame:
        m_music.PlaySfx(Sfx::Scroll);
        m_lastScrollSfxTime = m_time;
        OpenPause();
        break;
    case Screen::Paused: EnterGame(); break;
    case Screen::Home:
        if (button == SDL_GAMEPAD_BUTTON_BACK && m_sessionStarted && !m_overlay.IsOpen()) {
            EnterGame();
        }
        break;
    }
}

void Shell::OnDiscLoaded() {
    // Runs on the emulator thread; the GUI thread picks it up on the next frame
    m_discLoaded = true;
}

// -----------------------------------------------------------------------------
// Screens

void Shell::PauseEmulation() {
    // The shell owns the gamepad from now on: release whatever the Saturn was holding
    m_context.inputContext.ResetAllGamepadInputs();
    if (!m_context.paused) {
        m_context.EnqueueEvent(app::events::emu::SetPaused(true));
        m_pausedByShell = true;
    }
}

void Shell::ShowHome() {
    m_screen = Screen::Home;
    m_overlay.CloseAll();
    m_focusWindow = true;
    m_home.RequestFocus();
    m_starfield.Warp();
    m_music.SetPlaying(m_config.musicEnabled);
    PauseEmulation();
}

void Shell::ShowMainMenu() {
    m_home.ShowSpheres();
    ShowHome();
}

void Shell::ShowGameLibrary() {
    m_home.ShowLibrary();
    ShowHome();
}

void Shell::OpenPause() {
    m_screen = Screen::Paused;
    PauseEmulation();
    m_overlay.CloseAll();
    m_overlay.Push(BuildPauseScreen());
}

void Shell::EnterGame() {
    m_screen = Screen::InGame;
    m_sessionStarted = true;
    m_gameEnteredTime = m_time;
    m_overlay.CloseAll();
    m_music.SetPlaying(false);

    // The button that confirmed the choice is still down; make sure the Saturn does not see a stale press
    m_context.inputContext.ResetAllGamepadInputs();

    if (m_pausedByShell) {
        m_context.EnqueueEvent(app::events::emu::SetPaused(false));
        m_pausedByShell = false;
    }
}

void Shell::SetMode(UIMode mode) {
    if (m_config.mode == mode) {
        return;
    }
    m_config.mode = mode;
    SaveConfig();
    devlog::info<grp::shell>("Switched to the {} interface", mode == UIMode::Shell ? "shell" : "desktop");
}

void Shell::SaveConfig() const {
    if (m_configPath.empty()) {
        return;
    }
    if (!m_config.Save(m_configPath)) {
        devlog::warn<grp::shell>("Could not save {}", m_configPath);
    }
}

void Shell::ResetAllSavedData() {
    // Stop any background library scan before deleting files in the profile
    m_library.reset();

    const auto clearDirectoryContents = [](const std::filesystem::path &dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            return;
        }
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
            std::error_code removeEc;
            std::filesystem::remove_all(entry.path(), removeEc);
        }
    };

    clearDirectoryContents(m_context.profile.GetPath(app::ProfilePath::PersistentState));
    clearDirectoryContents(m_context.profile.GetPath(app::ProfilePath::SaveStates));
    clearDirectoryContents(m_context.profile.GetPath(app::ProfilePath::BackupMemory));

    std::error_code folderEc;
    m_context.profile.CreateFolders(folderEc);

    auto &settings = m_context.serviceLocator.GetRequired<app::Settings>();
    settings.ResetToDefaults();
    settings.Save();

    m_config = ShellConfig{};
    SaveConfig();
    m_music.SetVolume(m_config.musicVolume);

    {
        std::unique_lock lock{m_context.locks.disc};
        m_context.state.recentDiscs.clear();
    }

    m_library = std::make_unique<GameLibrary>(m_context.profile.GetPath(app::ProfilePath::PersistentState) /
                                              "shell_library.toml");
    m_library->LoadCache();
    m_libraryVersion = 0;
    m_games.clear();
    m_recent.clear();
    if (m_covers != nullptr) {
        m_covers->Clear();
    }

    m_sessionStarted = false;
    auto &romService = m_context.serviceLocator.GetRequired<app::services::ROMService>();
    romService.ScanIPLROMs();
    if (!romService.LoadIPLROM().succeeded) {
        m_context.iplRomPath.clear();
    }
    m_context.EnqueueEvent(app::events::emu::EjectDisc());
    m_context.EnqueueEvent(app::events::emu::FactoryReset());

    ShowMainMenu();
    m_marquee.Post("All saved data reset to initial state");
}

// -----------------------------------------------------------------------------
// Actions

void Shell::HandleHomeAction(const HomeResult &result) {
    switch (result.action) {
    case HomeAction::None: break;
    case HomeAction::BrowseGames: m_starfield.Warp(); break;
    case HomeAction::Start:
        m_music.PlaySfx(Sfx::Select);
        EnterGame();
        break;
    case HomeAction::LoadDisc: OpenLoadDisc(); break;
    case HomeAction::LaunchGame:
        m_music.PlaySfx(Sfx::Select);
        LaunchGame(result.game);
        break;
    case HomeAction::AddFolder: OpenFolderPicker(); break;
    case HomeAction::Rescan: StartLibraryScan(); break;
    case HomeAction::SelectBios: OpenBiosPicker(); break;
    case HomeAction::RescanBios: RescanBios(); break;
    case HomeAction::CompleteWizard:
        m_config.wizardCompleted = true;
        SaveConfig();
        m_starfield.Warp();
        ShowMainMenu();
        break;
    case HomeAction::Settings: m_overlay.Push(BuildSettingsScreen()); break;
    case HomeAction::Desktop: SwitchToDesktop(); break;
    case HomeAction::Quit: Quit(); break;
    }
}

void Shell::LaunchGame(const std::filesystem::path &path) {
    // The shell switches to the game once the emulator thread reports the disc as loaded
    m_marquee.Post("Loading " + ToUtf8(path.stem()));
    m_context.EnqueueEvent(app::events::emu::LoadDisc(path));
}

void Shell::OpenFilePicker(FilePickerMode mode, std::filesystem::path initialDir) {
    m_pickerMode = mode;
    m_pickerDir = std::move(initialDir);
    StartPickerLoad(m_pickerDir);
    m_overlay.Push(BuildFilePickerScreen());
}

void Shell::StartPickerLoad(std::filesystem::path directory) {
    auto state = std::make_shared<PickerLoadState>();
    m_pickerLoad = state;
    m_pickerEntries.clear();
    m_pickerLoading = true;
    m_pickerReadFailed = false;

    // The worker owns its shared result, so closing the picker never makes the UI thread wait for slow removable media.
    std::thread([state = std::move(state), directory = std::move(directory)] {
        std::vector<fs::DirectoryEntry> entries;
        bool succeeded = true;
        if (directory.empty()) {
            for (const std::filesystem::path &drive : fs::ListDrives()) {
                entries.push_back({.path = drive, .isDirectory = true});
            }
        } else {
            succeeded = fs::ListDirectory(directory, entries);
            std::sort(entries.begin(), entries.end(), [](const fs::DirectoryEntry &a, const fs::DirectoryEntry &b) {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory;
                }
                return a.path.filename().native() < b.path.filename().native();
            });
        }

        std::scoped_lock lock{state->mutex};
        state->entries = std::move(entries);
        state->succeeded = succeeded;
        state->complete = true;
    }).detach();
}

void Shell::ProcessPickerLoad() {
    const std::shared_ptr<PickerLoadState> state = m_pickerLoad;
    if (!state) {
        return;
    }

    {
        std::scoped_lock lock{state->mutex};
        if (!state->complete) {
            return;
        }
        m_pickerEntries = std::move(state->entries);
        m_pickerReadFailed = !state->succeeded;
    }
    m_pickerLoad.reset();
    m_pickerLoading = false;

    const std::string_view expectedTitle = m_pickerMode == FilePickerMode::Disc   ? "LOAD DISC IMAGE"
                                           : m_pickerMode == FilePickerMode::Bios ? "SELECT SATURN BIOS (IPL ROM)"
                                                                                  : "SELECT GAMES LIBRARY FOLDER";
    if (m_overlay.TopScreenTitle() == expectedTitle) {
        m_overlay.Replace(BuildFilePickerScreen(), true);
    }
}

void Shell::OpenLoadDisc() {
    // Open the custom shell file picker starting in the first library folder (or the drives root)
    const std::filesystem::path startDir =
        !m_config.libraryFolders.empty() ? m_config.libraryFolders.front() : std::filesystem::path{};
    OpenFilePicker(FilePickerMode::Disc, startDir);
}

void Shell::OpenBiosPicker() {
    // Open the custom shell file picker at the drives & shortcuts view so the user can browse USB or LocalState
    OpenFilePicker(FilePickerMode::Bios, {});
}

void Shell::ProcessPickedBios() {
    std::optional<std::filesystem::path> picked;
    {
        std::scoped_lock lock{m_pickMutex};
        picked.swap(m_pickedBios);
    }
    if (!picked) {
        return;
    }

    // Copy the picked BIOS into <profile>/roms/ipl/ when chosen from an external drive so it stays in LocalState
    std::filesystem::path targetPath = *picked;
    const std::filesystem::path iplDir = m_context.profile.GetPath(app::ProfilePath::IPLROMImages);
    if (!iplDir.empty() && !picked->filename().empty()) {
        const std::filesystem::path localCopy = iplDir / picked->filename();
        std::error_code ec{};
        if (!std::filesystem::equivalent(*picked, localCopy, ec)) {
            if (fs::CopyFileTo(*picked, localCopy, 4ull * 1024 * 1024)) {
                targetPath = localCopy;
            }
        }
    }

    // Validate the file, make it the configured IPL ROM, load it and restart the Saturn with it
    const ::util::ROMLoadResult result = ::util::LoadIPLROM(targetPath, *m_context.saturn.instance);
    if (!result.succeeded) {
        m_marquee.Post("Invalid BIOS file: " + ToUtf8(targetPath.filename()));
        m_context.serviceLocator.GetRequired<app::services::WindowManagerService>().OpenSimpleErrorModal(
            fmt::format("The file {} is not a valid IPL ROM: {}", targetPath, result.errorMessage));
        return;
    }

    auto &settings = m_context.serviceLocator.GetRequired<app::Settings>();
    settings.system.ipl.overrideImage = true;
    settings.system.ipl.path = targetPath;
    settings.MakeDirty();
    m_context.serviceLocator.GetRequired<app::services::ROMService>().ScanIPLROMs();
    m_context.serviceLocator.GetRequired<app::services::ROMService>().LoadIPLROM();
    m_context.EnqueueEvent(app::events::emu::HardReset());
    m_marquee.Post("BIOS configured: " + ToUtf8(targetPath.filename()));
    m_home.RequestFocus();
}

void Shell::RescanBios() {
    auto &romService = m_context.serviceLocator.GetRequired<app::services::ROMService>();
    romService.ScanIPLROMs();
    if (romService.LoadIPLROM().succeeded) {
        m_context.EnqueueEvent(app::events::emu::HardReset());
        m_marquee.Post("BIOS found: " + ToUtf8(m_context.iplRomPath.filename()));
        m_home.RequestFocus();
    } else {
        m_marquee.Post("No IPL ROM found in ROMs folder");
        m_context.DisplayMessage("No IPL ROM found");
    }
}

void Shell::OpenFolderPicker() {
    OpenFilePicker(FilePickerMode::Folder, {});
}

void Shell::ProcessPickedFolder() {
    std::optional<std::filesystem::path> picked;
    {
        std::scoped_lock lock{m_pickMutex};
        picked.swap(m_pickedFolder);
    }
    if (!picked) {
        return;
    }
    if (std::find(m_config.libraryFolders.begin(), m_config.libraryFolders.end(), *picked) ==
        m_config.libraryFolders.end()) {
        m_config.libraryFolders.push_back(*picked);
        SaveConfig();
    }
    m_marquee.Post("Library folder added: " + ToUtf8(*picked));
    StartLibraryScan();
}

void Shell::StartLibraryScan() {
    if (m_config.libraryFolders.empty()) {
        // Nothing to scan yet: ask for a folder first
        OpenFolderPicker();
        return;
    }
    if (m_library != nullptr && !m_library->Scanning()) {
        m_marquee.Post("Scanning the game library...");
        m_library->StartScan(m_config.libraryFolders);
    }
}

void Shell::SwitchToDesktop() {
    // The desktop interface shows the Saturn screen right away, so let it run
    m_overlay.CloseAll();
    if (m_pausedByShell) {
        m_context.EnqueueEvent(app::events::emu::SetPaused(false));
        m_pausedByShell = false;
    }
    m_context.inputContext.ResetAllGamepadInputs();
    m_music.SetPlaying(false);
    m_screen = Screen::Home;
    SetMode(UIMode::Desktop);
}

void Shell::Quit() {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

// -----------------------------------------------------------------------------
// Per-frame

DrawContext Shell::MakeContext(ImDrawList *drawList) {
    auto &graphics = m_context.serviceLocator.GetRequired<app::services::GraphicsService>();
    return DrawContext{drawList, m_layout, m_theme, graphics, m_shapes, m_time, ImGui::GetIO().DeltaTime};
}

void Shell::RefreshLibrary() {
    if (m_library == nullptr) {
        return;
    }
    if (m_library->Version() != m_libraryVersion) {
        m_libraryVersion = m_library->Version();
        m_games = m_library->Games();
    }
    const bool scanning = m_library->Scanning();
    if (m_wasScanning && !scanning) {
        m_marquee.Post(fmt::format("Library ready: {} games", m_games.size()));
    }
    m_wasScanning = scanning;

    // Recent discs, newest first, with the library's title when the disc is known. The list is updated by the
    // emulator thread when a disc loads, hence the lock
    m_recent.clear();
    std::unique_lock lock{m_context.locks.disc};
    for (const std::filesystem::path &path : m_context.state.recentDiscs) {
        auto it =
            std::find_if(m_games.begin(), m_games.end(), [&](const GameEntry &game) { return game.path == path; });
        if (it != m_games.end()) {
            m_recent.push_back(*it);
        } else {
            GameEntry entry{};
            entry.path = path;
            entry.title = ToUtf8(path.stem());
            m_recent.push_back(std::move(entry));
        }
    }
}

HomeModel Shell::BuildHomeModel(bool inputEnabled) {
    HomeModel model{};
    model.biosMissing = m_context.iplRomPath.empty();
    model.showWizard = model.biosMissing || !m_config.wizardCompleted;
    model.sessionActive = m_sessionStarted;
    model.biosName = model.biosMissing ? std::string{} : ToUtf8(m_context.iplRomPath.filename());
    model.iplFolder = fmt::format("{}", m_context.profile.GetPath(app::ProfilePath::IPLROMImages));
    model.folderCount = m_config.libraryFolders.size();
    model.games = &m_games;
    model.recent = &m_recent;
    model.scanning = m_library != nullptr && m_library->Scanning();
    model.scanProgress = m_library != nullptr ? m_library->ScanProgress() : 0;
    model.inputEnabled = inputEnabled;
    model.covers = m_covers.get();

    std::unique_lock lock{m_context.locks.disc};
    const ymir::media::CDInterface &cdif = m_context.saturn.GetCDInterface();
    if (cdif.HasDisc()) {
        const ymir::media::SaturnHeader &header = cdif.GetDiscHeader();
        model.discTitle =
            header.gameTitle.empty() ? std::string{"Unnamed game"} : ::util::TranslateSaturnString(header.gameTitle);
    }
    return model;
}

void Shell::Draw() {
    // Runs in every mode so a fade out that started before switching to the desktop interface still completes
    m_music.Update();

    if (!IsActive() || !m_initialized) {
        // A disc loaded from the desktop interface must not throw the shell into the game the next time it is
        // shown
        m_discLoaded = false;
        return;
    }

    const ImGuiIO &io = ImGui::GetIO();
    m_time += io.DeltaTime;
    m_layout.Update(io.DisplaySize.x, io.DisplaySize.y, m_config.safeArea);

    if (m_discLoaded.exchange(false)) {
        EnterGame();
    }
    ProcessPickedBios();
    ProcessPickedFolder();
    ProcessPickerLoad();
    RefreshLibrary();

    switch (m_screen) {
    case Screen::InGame:
        m_lastFocusId = 0;
        DrawInGameHint();
        break;
    case Screen::Paused: DrawPaused(); break;
    case Screen::Home:
        m_starfield.Update(io.DeltaTime);
        DrawHome();
        break;
    }

    if (m_screen != Screen::InGame) {
        const ImGuiID focusId = ImGui::GetFocusID();
        if (focusId != 0 && m_lastFocusId != 0 && focusId != m_lastFocusId && m_time - m_lastScrollSfxTime > 0.08) {
            m_music.PlaySfx(Sfx::Scroll);
            m_lastScrollSfxTime = m_time;
        }
        m_lastFocusId = focusId;
    }
}

void Shell::DrawHome() {
    // Closing an overlay gives the focus back to the shell window
    const bool overlayOpen = m_overlay.IsOpen();
    if (m_overlayWasOpen && !overlayOpen) {
        m_focusWindow = true;
    }
    m_overlayWasOpen = overlayOpen;

    // If nothing has the focus (for instance after a desktop window opened from the shell was closed), take it back
    // so the gamepad navigation keeps working
    if (!overlayOpen && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
        m_focusWindow = true;
    }

    const Rect display = m_layout.Display();
    ImGui::SetNextWindowPos(ToImVec2(display.pos));
    ImGui::SetNextWindowSize(ToImVec2(display.size));
    if (m_focusWindow && !overlayOpen) {
        ImGui::SetNextWindowFocus();
        m_home.RequestFocus();
        m_focusWindow = false;
    }

    // The widgets draw their own focus effect, so ImGui's rectangular navigation cursor is hidden
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoDocking;

    HomeResult result{};
    bool windowFocused = false;
    std::vector<ButtonHint> legend;
    if (ImGui::Begin("##ymir_shell", nullptr, kFlags)) {
        windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const DrawContext ctx = MakeContext(ImGui::GetWindowDrawList());
        const HomeModel model = BuildHomeModel(windowFocused && !overlayOpen);
        UpdateMarquee(ctx, model);
        if (m_covers != nullptr) {
            m_covers->BeginFrame();
        }

        // Back to front: the sky, the bottom and top dashboards, the audio-reactive cubes on the pods, the home
        // screen contents (spheres, wizard or library + game details card) in front of the bottom dashboard, and the
        // legend/version/scanline finish over everything
        DrawSpaceBackground(ctx, m_starfield, m_starScratch);
        DrawBottomDashboard(ctx);
        DrawCubes(ctx);
        DrawTopDashboard(ctx, m_marquee);
        result = m_home.Draw(ctx, model);
        DrawVersionLabel(ctx, kVersionText);
        if (!overlayOpen) {
            legend = m_home.Legend(model);
            DrawLegend(ctx, legend);
        }
        if (m_config.scanlines) {
            DrawScanlines(ctx);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Overlays (settings...) over the home screen, with their own legend on top of the dimmed screen
    if (overlayOpen) {
        m_overlay.Draw(MakeContext(nullptr), true);
        const std::vector<ButtonHint> overlayLegend = m_overlay.Legend();
        DrawLegend(MakeContext(ImGui::GetForegroundDrawList()), overlayLegend);
        return;
    }

    // B goes back to the game from the sphere menu. Only while the shell window has the focus, so B still closes a
    // desktop window or dialog opened on top of the shell
    if (result.action == HomeAction::None && windowFocused && m_sessionStarted && !m_home.InLibraryView() &&
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        result.action = HomeAction::Start;
    }

    HandleHomeAction(result);
}

void Shell::DrawPaused() {
    // The pause menu floats over the frozen game; closing it with B resumes the game
    if (!m_overlay.IsOpen()) {
        EnterGame();
        return;
    }
    m_overlay.Draw(MakeContext(nullptr), true);
    if (!m_overlay.IsOpen() && m_screen == Screen::Paused) {
        EnterGame();
        return;
    }
    const std::vector<ButtonHint> legend = m_overlay.Legend();
    DrawLegend(MakeContext(ImGui::GetForegroundDrawList()), legend);
}

void Shell::DrawInGameHint() {
    const double elapsed = m_time - m_gameEnteredTime;
    if (elapsed >= kInGameHintSeconds) {
        return;
    }

    // Fade out over the last second
    const float alpha = static_cast<float>(std::min(1.0, kInGameHintSeconds - elapsed));
    const DrawContext ctx = MakeContext(ImGui::GetForegroundDrawList());

    const Rect safe = m_layout.SafeArea();
    const Vec2 size{m_layout.Scale(330.0f), m_layout.Scale(64.0f)};
    const Vec2 pos{safe.pos.x + safe.size.x - size.x, safe.pos.y + safe.size.y - size.y};
    ctx.drawList->AddRectFilled(ToImVec2(pos), ImVec2(pos.x + size.x, pos.y + size.y),
                                IM_COL32(10, 16, 36, static_cast<int>(200 * alpha)), m_layout.Scale(32.0f));
    DrawGlyph(ctx, Glyph::View, {pos.x + m_layout.Scale(62.0f), pos.y + size.y * 0.5f}, 18.0f);
    const Vec2 textSize = MeasureDotText(ctx, 3.4f, "Ymir menu");
    DrawDotText(ctx, {pos.x + m_layout.Scale(118.0f), pos.y + (size.y - textSize.y) * 0.5f}, 3.4f, "Ymir menu",
                IM_COL32(220, 230, 255, static_cast<int>(255 * alpha)));
}

void Shell::DrawCubes(const DrawContext &ctx) {
    // Pastel green on the left, pastel red on the right, each following its own channel of the music
    constexpr ImU32 kLeftColor = IM_COL32(168, 230, 176, 255);
    constexpr ImU32 kRightColor = IM_COL32(244, 167, 167, 255);
    const ThemeLayout &anchors = m_theme.GetLayout();
    m_cubes[0].Update(ctx.deltaTime, m_music.Levels(0));
    m_cubes[1].Update(ctx.deltaTime, m_music.Levels(1));
    DrawAudioCube(ctx, anchors.podLeft, m_cubes[0], kLeftColor, 0.0f);
    DrawAudioCube(ctx, anchors.podRight, m_cubes[1], kRightColor, 1.7f);
}

void Shell::UpdateMarquee(const DrawContext &ctx, const HomeModel &model) {
    // A new music track is announced like any other event
    if (std::optional<std::string> track = m_music.TakeTrackChange()) {
        m_marquee.Post("\xE2\x99\xAA " + *track);
    }

    // Messages the app reported since the last frame (resets, disc and BIOS loads...) become marquee events. They
    // are read newest first from the ring buffer and posted oldest first
    {
        std::vector<std::string> fresh;
        std::unique_lock lock{m_context.locks.messages};
        const auto &messages = m_context.messages;
        auto newest = m_lastMessageTime;
        for (std::size_t i = messages.Count(); i > 0; --i) {
            const app::Message *message = messages.Get(i - 1);
            if (message == nullptr || message->timestamp <= m_lastMessageTime) {
                break;
            }
            newest = std::max(newest, message->timestamp);
            fresh.push_back(message->message);
        }
        m_lastMessageTime = newest;
        lock.unlock();
        for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
            m_marquee.Post(std::move(*it));
        }
    }

    const std::string tray = model.biosMissing         ? std::string{"No BIOS: choose one below to get started"}
                             : model.discTitle.empty() ? std::string{"No disc in the tray"}
                                                       : "Disc: " + model.discTitle;
    // The idle texts are rebuilt when what they describe changes: the tray, the library size or the music
    const std::string idleKey = fmt::format("{}|{}|{}", tray, m_games.size(), m_music.TrackCount());
    if (idleKey != m_idleTray) {
        m_idleTray = idleKey;
        std::vector<std::string> idle{tray};
        if (m_games.empty()) {
            idle.emplace_back("Press X to add the folder with your disc images");
        } else {
            idle.push_back(fmt::format("{} games in your library", m_games.size()));
        }
        if (m_music.TrackCount() == 0) {
            idle.emplace_back("Copy music files to the music folder of your profile for background music");
        }
        idle.emplace_back("Press View in game to open the menu");
        m_marquee.SetIdleTexts(std::move(idle));
    }

    // The marquee is measured in LCD dot columns and moves a whole column at a time
    constexpr float kColumnsPerSecond = 22.0f;
    m_marquee.Update(ctx.deltaTime, static_cast<float>(MarqueeColumns(ctx)), kColumnsPerSecond,
                     [](std::string_view text) { return static_cast<float>(dotmatrix::TextColumns(text)); });
}

} // namespace platform::uwp::shell

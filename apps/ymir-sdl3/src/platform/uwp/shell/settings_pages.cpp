// The overlay screens of the Xbox shell: the settings and the pause menu.
//
// Every row reads and writes the same Settings fields and sends the same events as the corresponding desktop widget
// (see app/ui/widgets/settings_widgets.cpp and system_widgets.cpp), so changes made here behave exactly like changes
// made in the desktop settings window. Anything the shell has no page for is reachable through the "Advanced" page,
// which opens the desktop windows on top of the shell; they are navigable with the gamepad too.

#include "shell.hpp"

#include "shell_log.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>
#include <app/profile.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <app/services/disc_service.hpp>
#include <app/services/save_state_service.hpp>
#include <app/services/window_manager_service.hpp>

#include <util/regions.hpp>

#include <ymir/hw/smpc/peripheral/peripheral_defs.hpp>
#include <ymir/hw/smpc/smpc.hpp>
#include <ymir/media/cd_interface.hpp>

#include <fmt/format.h>

#include <ctime>

namespace platform::uwp::shell {

namespace {

    std::string ToUtf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    std::string Percent(float value) {
        return fmt::format("{}%", static_cast<int>(value * 100.0f + 0.5f));
    }

    /// @brief Formats a save state timestamp in local time.
    std::string FormatTimestamp(std::chrono::system_clock::time_point time) {
        const std::time_t t = std::chrono::system_clock::to_time_t(time);
        std::tm local{};
        localtime_s(&local, &t);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
        return buffer;
    }

    constexpr std::array kPeripherals{
        ymir::peripheral::PeripheralType::None,         ymir::peripheral::PeripheralType::ControlPad,
        ymir::peripheral::PeripheralType::AnalogPad,    ymir::peripheral::PeripheralType::ArcadeRacer,
        ymir::peripheral::PeripheralType::MissionStick, ymir::peripheral::PeripheralType::VirtuaGun,
        ymir::peripheral::PeripheralType::ShuttleMouse,
    };

    constexpr std::array kRegions{
        ymir::core::config::sys::Region::Japan,
        ymir::core::config::sys::Region::NorthAmerica,
        ymir::core::config::sys::Region::EuropePAL,
        ymir::core::config::sys::Region::AsiaNTSC,
    };

} // namespace

OverlayScreen Shell::BuildSettingsScreen() {
    app::Settings &settings = m_context.serviceLocator.GetRequired<app::Settings>();
    app::SharedContext &ctx = m_context;
    OverlayScreen screen{.title = "SETTINGS", .pages = {}};

    // ---------------------------------------------------------------- System
    {
        OverlayPage page{.title = "SYSTEM", .rows = {}};
        page.rows.push_back(OverlayRow::Choice(
            "Video standard", "NTSC (60 Hz) for American and Japanese games, PAL (50 Hz) for European games.",
            {"NTSC", "PAL"},
            [&settings] {
                return *settings.system.videoStandard == ymir::core::config::sys::VideoStandard::PAL ? 1 : 0;
            },
            [&settings, &ctx](int choice) {
                const auto standard = choice == 1 ? ymir::core::config::sys::VideoStandard::PAL
                                                  : ymir::core::config::sys::VideoStandard::NTSC;
                ctx.EnqueueEvent(app::events::emu::SetVideoStandard(standard));
                settings.system.videoStandard = standard;
                settings.MakeDirty();
            }));
        std::vector<std::string> regionNames;
        for (const auto region : kRegions) {
            regionNames.push_back(::util::RegionToString(region));
        }
        page.rows.push_back(OverlayRow::Choice(
            "Console region", "The region of the emulated console. Changing it restarts the Saturn.",
            std::move(regionNames),
            [&ctx] {
                const auto current = static_cast<ymir::core::config::sys::Region>(ctx.saturn.GetSMPC().GetAreaCode());
                for (std::size_t i = 0; i < kRegions.size(); ++i) {
                    if (kRegions[i] == current) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            },
            [&ctx](int choice) {
                ctx.EnqueueEvent(app::events::emu::SetAreaCode(static_cast<uint8>(kRegions[choice])));
                ctx.EnqueueEvent(app::events::emu::HardReset());
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Detect region from disc", "Switch the console region to match the disc when one is loaded.",
            [&settings] { return *settings.system.autodetectRegion; },
            [&settings](bool value) {
                settings.system.autodetectRegion = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Action(
            "BIOS (IPL ROM)", "Choose the Saturn BIOS file with the file picker.", [this] { OpenBiosPicker(); },
            [&ctx] { return ctx.iplRomPath.empty() ? std::string{"not found"} : ToUtf8(ctx.iplRomPath.filename()); }));
        page.rows.push_back(OverlayRow::Action("Scan the BIOS folder again",
                                               "Look for IPL ROM files copied to the roms/ipl folder of the profile.",
                                               [this] { RescanBios(); }));
        page.rows.push_back(OverlayRow::Toggle(
            "Remember the last disc", "Load the last disc again when Ymir starts.",
            [&settings] { return settings.general.rememberLastLoadedDisc; },
            [&settings](bool value) {
                settings.general.rememberLastLoadedDisc = value;
                settings.MakeDirty();
            }));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Video
    {
        OverlayPage page{.title = "VIDEO", .rows = {}};
        page.rows.push_back(OverlayRow::Toggle(
            "Hardware rendering",
            "Render VDP1 and VDP2 on the GPU with Direct3D 12. Faster, with extra enhancements (experimental).",
            [&settings] { return *settings.video.useHardwareAcceleration; },
            [&settings, &ctx](bool value) {
                settings.video.useHardwareAcceleration = value;
                settings.MakeDirty();
                ctx.EnqueueEvent(app::events::emu::SwitchVDPRenderer());
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Deinterlace", "Render interlaced high-resolution modes as progressive video. Costs performance.",
            [&settings] { return settings.video.enhancements.deinterlace.Get(); },
            [&settings](bool value) {
                settings.video.enhancements.deinterlace = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Transparent meshes", "Draw the Saturn's checkerboard \"mesh\" effects as real transparency.",
            [&settings] { return settings.video.enhancements.transparentMeshes.Get(); },
            [&settings](bool value) {
                settings.video.enhancements.transparentMeshes = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Integer scaling", "Scale the picture by whole numbers only, for sharp pixels.",
            [&settings] { return settings.video.forceIntegerScaling; },
            [&settings](bool value) {
                settings.video.forceIntegerScaling = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Force aspect ratio", "Keep the picture at the configured aspect ratio instead of the native one.",
            [&settings] { return settings.video.forceAspectRatio; },
            [&settings](bool value) {
                settings.video.forceAspectRatio = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Show frame rate", "Show the frame rate counter in a corner of the screen.",
            [&settings] { return settings.gui.showFrameRateOSD; },
            [&settings](bool value) {
                settings.gui.showFrameRateOSD = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Threaded VDP1 (software)", "Run the software VDP1 renderer on its own thread.",
            [&settings] { return settings.video.swRenderer.threadedVDP1.Get(); },
            [&settings, &ctx](bool value) {
                settings.MakeDirty();
                ctx.EnqueueEvent(app::events::emu::EnableThreadedVDP1(value));
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Threaded VDP2 (software)", "Run the software VDP2 renderer on its own thread. Recommended.",
            [&settings] { return settings.video.swRenderer.threadedVDP2.Get(); },
            [&settings, &ctx](bool value) {
                settings.MakeDirty();
                ctx.EnqueueEvent(app::events::emu::EnableThreadedVDP2(value));
            }));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Audio
    {
        OverlayPage page{.title = "AUDIO", .rows = {}};
        page.rows.push_back(OverlayRow::Slider(
            "Game volume", "Volume of the emulated Saturn.", 0.0f, 1.0f, 0.05f,
            [&settings] { return *settings.audio.volume; },
            [&settings, &ctx](float value) {
                settings.audio.volume = value;
                ctx.lastVolumeChangeTime = std::chrono::steady_clock::now();
                settings.MakeDirty();
            },
            Percent));
        page.rows.push_back(OverlayRow::Toggle(
            "Mute game audio", "Silence the emulated Saturn.", [&settings] { return *settings.audio.mute; },
            [&settings](bool value) {
                settings.audio.mute = value;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Choice(
            "Sample interpolation", "Linear matches the hardware; nearest neighbor sounds grittier.",
            {"Nearest neighbor", "Linear"},
            [&settings] {
                return *settings.audio.interpolation ==
                               ymir::core::config::audio::SampleInterpolationMode::NearestNeighbor
                           ? 0
                           : 1;
            },
            [&settings](int choice) {
                settings.audio.interpolation = choice == 0
                                                   ? ymir::core::config::audio::SampleInterpolationMode::NearestNeighbor
                                                   : ymir::core::config::audio::SampleInterpolationMode::Linear;
                settings.MakeDirty();
            }));
        page.rows.push_back(OverlayRow::Toggle(
            "Background music", "Play the music files of the profile's music folder on the shell screens.",
            [this] { return m_config.musicEnabled; },
            [this](bool value) {
                m_config.musicEnabled = value;
                SaveConfig();
                m_music.SetPlaying(value && m_screen == Screen::Home);
            }));
        page.rows.push_back(OverlayRow::Slider(
            "Music volume", "Volume of the background music.", 0.0f, 1.0f, 0.05f,
            [this] { return m_config.musicVolume; },
            [this](float value) {
                m_config.musicVolume = value;
                m_music.SetVolume(value);
                SaveConfig();
            },
            Percent));
        page.rows.push_back(OverlayRow::Toggle(
            "Shuffle music", "Play the music tracks in random order. Applies the next time Ymir starts.",
            [this] { return m_config.musicShuffle; },
            [this](bool value) {
                m_config.musicShuffle = value;
                SaveConfig();
            }));
        page.rows.push_back(OverlayRow::Info(
            "Music tracks", [this] { return std::to_string(m_music.TrackCount()); },
            "OGG, MP3, FLAC or WAV files in the music folder of the profile. Restart Ymir after adding files."));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Input
    {
        OverlayPage page{.title = "CONTROLS", .rows = {}};
        std::vector<std::string> peripheralNames;
        for (const auto type : kPeripherals) {
            peripheralNames.emplace_back(ymir::peripheral::GetPeripheralName(type));
        }
        for (std::size_t port = 0; port < 2; ++port) {
            page.rows.push_back(OverlayRow::Choice(
                fmt::format("Port {} device", port + 1), "The peripheral plugged into this controller port.",
                peripheralNames,
                [&settings, port] {
                    const auto current = *settings.input.ports[port].type;
                    for (std::size_t i = 0; i < kPeripherals.size(); ++i) {
                        if (kPeripherals[i] == current) {
                            return static_cast<int>(i);
                        }
                    }
                    return 0;
                },
                [&settings, port](int choice) {
                    settings.input.ports[port].type = kPeripherals[choice];
                    settings.MakeDirty();
                }));
            page.rows.push_back(OverlayRow::Action(
                fmt::format("Port {} button mapping", port + 1),
                "Opens the binds editor. It is a desktop window: navigate it with the d-pad, close it with B.",
                [&ctx, port] {
                    ctx.EnqueueEvent(app::events::gui::OpenPeripheralBindsEditor(static_cast<uint32>(port), 0));
                }));
        }
        page.rows.push_back(OverlayRow::Slider(
            "Left stick dead zone", "How far the left stick must move before the Saturn sees it.", 0.0f, 0.9f, 0.05f,
            [&settings] { return *settings.input.gamepad.lsDeadzone; },
            [&settings](float value) {
                settings.input.gamepad.lsDeadzone = value;
                settings.MakeDirty();
            },
            Percent));
        page.rows.push_back(OverlayRow::Slider(
            "Analog to digital threshold", "How far an analog input must move to count as a d-pad press.", 0.05f, 0.95f,
            0.05f, [&settings] { return *settings.input.gamepad.analogToDigitalSensitivity; },
            [&settings](float value) {
                settings.input.gamepad.analogToDigitalSensitivity = value;
                settings.MakeDirty();
            },
            Percent));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Library
    {
        OverlayPage page{.title = "LIBRARY", .rows = {}};
        page.rows.push_back(OverlayRow::Action("Add a folder",
                                               "Choose a folder with disc images; its subfolders are searched too.",
                                               [this] { OpenFolderPicker(); }));
        page.rows.push_back(OverlayRow::Action(
            "Scan the library again", "Look for new or changed disc images in the library folders.",
            [this] { StartLibraryScan(); }, [this] { return fmt::format("{} games", m_games.size()); }));
        for (std::size_t i = 0; i < m_config.libraryFolders.size(); ++i) {
            const std::filesystem::path folder = m_config.libraryFolders[i];
            page.rows.push_back(OverlayRow::Action(
                ToUtf8(folder), "Press A to remove this folder from the library (the files are not touched).",
                [this, folder] {
                    std::erase(m_config.libraryFolders, folder);
                    SaveConfig();
                    m_marquee.Post("Library folder removed: " + ToUtf8(folder));
                    // Rebuild the screen so the removed folder disappears from the list
                    m_overlay.Replace(BuildSettingsScreen());
                },
                [] { return std::string{"Remove"}; }));
        }
        page.rows.push_back(OverlayRow::Info(
            "Covers folder", [this] { return ToUtf8(m_context.profile.GetPath(app::ProfilePath::Root) / "covers"); },
            "Put cover images here named after the product number (MK-81009.png) or the disc file name."));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Interface
    {
        OverlayPage page{.title = "INTERFACE", .rows = {}};
        page.rows.push_back(OverlayRow::Slider(
            "TV safe area", "Space kept free at the edges for TVs that cut off the picture.", 0.0f, 0.1f, 0.01f,
            [this] { return m_config.safeArea; },
            [this](float value) {
                m_config.safeArea = value;
                SaveConfig();
            },
            Percent));
        page.rows.push_back(OverlayRow::Toggle(
            "CRT scanlines", "Draw faint scanlines over the shell screens.", [this] { return m_config.scanlines; },
            [this](bool value) {
                m_config.scanlines = value;
                SaveConfig();
            }));
        page.rows.push_back(OverlayRow::Action(
            "Desktop interface",
            "Switch to the full desktop interface. Hold View for one second there to come back to the shell.",
            [this] { SwitchToDesktop(); }));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- Advanced
    {
        OverlayPage page{.title = "ADVANCED", .rows = {}};
        const auto openTab = [&ctx](app::ui::SettingsTab tab) {
            return [&ctx, tab] { ctx.EnqueueEvent(app::events::gui::OpenSettings(tab)); };
        };
        page.rows.push_back(
            OverlayRow::Action("Backup memory manager",
                               "Copy, import and delete the save files of the internal memory and "
                               "of the backup cartridge. Opens the desktop window.",
                               [&ctx] { ctx.EnqueueEvent(app::events::gui::OpenBackupMemoryManager()); }));
        page.rows.push_back(OverlayRow::Action("Cartridge", "Choose the cartridge in the expansion slot.",
                                               openTab(app::ui::SettingsTab::Cartridge)));
        page.rows.push_back(OverlayRow::Action("CD block", "CD drive read speed and low-level emulation.",
                                               openTab(app::ui::SettingsTab::CDBlock)));
        page.rows.push_back(OverlayRow::Action("Hotkeys", "Keyboard and controller shortcuts.",
                                               openTab(app::ui::SettingsTab::Hotkeys)));
        page.rows.push_back(
            OverlayRow::Action("Tweaks", "Accuracy and performance options.", openTab(app::ui::SettingsTab::Tweaks)));
        page.rows.push_back(OverlayRow::Action("All desktop settings", "Every setting, in the desktop window.",
                                               openTab(app::ui::SettingsTab::General)));
        screen.pages.push_back(std::move(page));
    }

    // ---------------------------------------------------------------- About
    {
        OverlayPage page{.title = "ABOUT", .rows = {}};
        page.rows.push_back(OverlayRow::Info(
            "Ymir", [] { return std::string{"v" Ymir_VERSION}; }, "Sega Saturn emulator. Xbox shell by the UWP port."));
        page.rows.push_back(OverlayRow::Info("BIOS", [&ctx] {
            return ctx.iplRomPath.empty() ? std::string{"not found"} : ToUtf8(ctx.iplRomPath.filename());
        }));
        page.rows.push_back(
            OverlayRow::Info("Games in the library", [this] { return std::to_string(m_games.size()); }));
        page.rows.push_back(OverlayRow::Info(
            "Profile folder", [&ctx] { return ToUtf8(ctx.profile.GetPath(app::ProfilePath::Root)); },
            "Reach it from the Device Portal file explorer to copy BIOS files, covers and music."));
        screen.pages.push_back(std::move(page));
    }

    return screen;
}

OverlayScreen Shell::BuildPauseScreen() {
    app::SharedContext &ctx = m_context;
    auto &saveStates = m_context.serviceLocator.GetRequired<app::services::SaveStateService>();
    OverlayScreen screen{.title = "PAUSED", .pages = {}};
    OverlayPage page{.title = "GAME", .rows = {}};

    const auto slotInfo = [&saveStates](std::size_t slot) {
        const app::savestates::Slot *state = saveStates.Peek(slot);
        return state != nullptr && state->IsValid() ? FormatTimestamp(state->primary.timestamp) : std::string{"empty"};
    };

    page.rows.push_back(OverlayRow::Action("Resume", "Back to the game.", [this] { EnterGame(); }));

    std::vector<std::string> slots;
    for (std::size_t i = 0; i < saveStates.GetSlotCount(); ++i) {
        slots.push_back(fmt::format("Slot {}", i + 1));
    }
    page.rows.push_back(OverlayRow::Choice(
        "Save state slot", "Choose the slot with left/right; save or load it below.", std::move(slots),
        [this] { return static_cast<int>(m_pauseSlot); },
        [this](int choice) { m_pauseSlot = static_cast<std::size_t>(choice); }));
    page.rows.push_back(OverlayRow::Action(
        "Save state", "Save the game in the selected slot. The previous state of the slot can be undone.",
        [this, &saveStates] {
            saveStates.SaveSaveStateSlot(m_pauseSlot);
            m_marquee.Post(fmt::format("Saved to slot {}", m_pauseSlot + 1));
        },
        [this, slotInfo] { return slotInfo(m_pauseSlot); }));
    OverlayRow load = OverlayRow::Action(
        "Load state", "Load the selected slot and go back to the game.",
        [this, &saveStates] {
            saveStates.LoadSaveStateSlot(m_pauseSlot);
            EnterGame();
        },
        [this, slotInfo] { return slotInfo(m_pauseSlot); });
    load.enabled = [this, &saveStates] {
        const app::savestates::Slot *state = saveStates.Peek(m_pauseSlot);
        return state != nullptr && state->IsValid();
    };
    page.rows.push_back(std::move(load));

    page.rows.push_back(OverlayRow::Action("Reset", "Press the Saturn's reset button.", [this, &ctx] {
        ctx.EnqueueEvent(app::events::emu::SoftReset());
        EnterGame();
    }));
    page.rows.push_back(OverlayRow::Action("Hard reset", "Power cycle the Saturn.", [this, &ctx] {
        ctx.EnqueueEvent(app::events::emu::HardReset());
        EnterGame();
    }));
    page.rows.push_back(
        OverlayRow::Action("Change disc", "Load another disc image with the file picker.", [this] { OpenLoadDisc(); }));
    page.rows.push_back(OverlayRow::Action("Settings", "System, video, audio, controls and more.",
                                           [this] { m_overlay.Push(BuildSettingsScreen()); }));
    page.rows.push_back(
        OverlayRow::Action("Game library", "Back to the home screen; the game stays paused.", [this] { ShowHome(); }));
    page.rows.push_back(OverlayRow::Action("Desktop interface", "Switch to the full desktop interface.",
                                           [this] { SwitchToDesktop(); }));
    screen.pages.push_back(std::move(page));
    return screen;
}

} // namespace platform::uwp::shell

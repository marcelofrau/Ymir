// The overlay screens of the Xbox shell: the settings and the pause menu.
//
// Every row reads and writes the same Settings fields and sends the same events as the corresponding desktop widget
// (see app/ui/widgets/settings_widgets.cpp and system_widgets.cpp), so changes made here behave exactly like changes
// made in the desktop settings window. Anything the shell has no page for is reachable through the "Advanced" page,
// which opens the desktop windows on top of the shell; they are navigable with the gamepad too.

#include "shell.hpp"

#include "file_system.hpp"
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

#include <algorithm>
#include <cctype>
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
        page.rows.push_back(OverlayRow::Action(
            "Open Welcome / Setup Wizard",
            "Re-open the initial setup screen to configure your BIOS and games library folder.", [this] {
                m_config.wizardCompleted = false;
                SaveConfig();
                ShowMainMenu();
            }));
        page.rows.push_back(OverlayRow::Action(
            "Reset all saved data...",
            "Restore all settings, library folders, recent games, save states and backup memory to the initial state.",
            [this] { m_overlay.Push(BuildResetConfirmScreen()); }));
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
            "Original GUI", "Switch to the original desktop interface. Press View there to come back to the shell.",
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
        page.customDraw = [this, &ctx](const DrawContext &dctx, Rect rect) {
            ImFont *nerdFont = dctx.theme.GetFont() != nullptr ? dctx.theme.GetFont() : ImGui::GetFont();
            ImFont *iconFont = dctx.theme.GetIconFont();
            ImDrawList *dl = dctx.drawList;

            const float pad = dctx.layout.Scale(22.0f);
            float y = rect.pos.y + dctx.layout.Scale(8.0f);

            // Top hero banner with the 3D Ymir planet icon + 3D YMIR wordmark logo
            const Rect banner{{rect.pos.x, y}, {rect.size.x, dctx.layout.Scale(136.0f)}};
            dl->AddRectFilled(ToImVec2(banner.pos), ImVec2(banner.pos.x + banner.size.x, banner.pos.y + banner.size.y),
                              IM_COL32(8, 24, 52, 210), dctx.layout.Scale(12.0f));
            dl->AddRect(ToImVec2(banner.pos), ImVec2(banner.pos.x + banner.size.x, banner.pos.y + banner.size.y),
                        IM_COL32(90, 210, 255, 190), dctx.layout.Scale(12.0f), 0,
                        std::max(1.0f, dctx.layout.Scale(1.8f)));

            float textLeft = banner.pos.x + pad;
            if (const ThemeAsset *icon = dctx.theme.Find("ymir_icon")) {
                const float iconSz = dctx.layout.Scale(106.0f);
                DrawAsset(dctx, *icon, {{textLeft, banner.pos.y + (banner.size.y - iconSz) * 0.5f}, {iconSz, iconSz}});
                textLeft += iconSz + dctx.layout.Scale(22.0f);
            }
            if (const ThemeAsset *logo = dctx.theme.Find("ymir_logo")) {
                const float logoH = dctx.layout.Scale(62.0f);
                const float aspect = logo->FrameHeight() > 0.0f ? logo->FrameWidth() / logo->FrameHeight() : 3.8f;
                const float logoW = logoH * aspect;
                DrawAsset(dctx, *logo, {{textLeft, banner.pos.y + dctx.layout.Scale(16.0f)}, {logoW, logoH}});
            } else {
                DrawDotText(dctx, {textLeft, banner.pos.y + dctx.layout.Scale(22.0f)}, 5.2f, "YMIR",
                            IM_COL32(150, 235, 255, 255));
            }
            DrawLabel(dctx, nerdFont, 24.0f, {textLeft, banner.pos.y + dctx.layout.Scale(88.0f)},
                      IM_COL32(165, 225, 255, 255),
                      "Sega Saturn Emulator  •  v" Ymir_VERSION "  •  Xbox / UWP Edition");

            y += banner.size.y + dctx.layout.Scale(18.0f);

            // Two-column credit cards: Left = Authors, Right = Special Thanks
            const float colGap = dctx.layout.Scale(20.0f);
            const float colW = (rect.size.x - colGap) * 0.5f;
            const float cardH = dctx.layout.Scale(210.0f);

            const auto drawCardFrame = [&](Rect card, std::string_view title, const char *faGlyph, ImU32 accent) {
                dl->AddRectFilled(ToImVec2(card.pos), ImVec2(card.pos.x + card.size.x, card.pos.y + card.size.y),
                                  IM_COL32(6, 18, 40, 205), dctx.layout.Scale(10.0f));
                dl->AddRect(ToImVec2(card.pos), ImVec2(card.pos.x + card.size.x, card.pos.y + card.size.y), accent,
                            dctx.layout.Scale(10.0f), 0, std::max(1.0f, dctx.layout.Scale(1.6f)));
                float titleX = card.pos.x + dctx.layout.Scale(20.0f);
                if (iconFont != nullptr && faGlyph != nullptr) {
                    DrawLabel(dctx, iconFont, 22.0f, {titleX, card.pos.y + dctx.layout.Scale(16.0f)}, accent, faGlyph);
                    titleX += dctx.layout.Scale(32.0f);
                }
                DrawDotText(dctx, {titleX, card.pos.y + dctx.layout.Scale(18.0f)}, 2.8f, title, accent);
            };

            // Left Card: Credits & Port Author
            const Rect leftCard{{rect.pos.x, y}, {colW, cardH}};
            drawCardFrame(leftCard, "AUTHORS & ARCHITECTURE", "\xEF\x84\x9B", IM_COL32(90, 215, 255, 230));
            const float lx = leftCard.pos.x + dctx.layout.Scale(20.0f);
            DrawLabel(dctx, nerdFont, 24.0f, {lx, leftCard.pos.y + dctx.layout.Scale(56.0f)},
                      IM_COL32(255, 215, 110, 255), "Xbox / UWP Port Author: Marcelo (marcelofrau)");
            DrawLabel(dctx, nerdFont, 20.0f, {lx, leftCard.pos.y + dctx.layout.Scale(86.0f)},
                      IM_COL32(205, 228, 248, 255),
                      "Saturn BIOS 3D shell, D3D12 compute/graphics port, XAudio2 backend & Xbox integration.",
                      colW - dctx.layout.Scale(40.0f));
            DrawLabel(dctx, nerdFont, 23.0f, {lx, leftCard.pos.y + dctx.layout.Scale(142.0f)},
                      IM_COL32(130, 235, 255, 255), "Ymir Core Emulator: StrikerX3 & Contributors");
            DrawLabel(dctx, nerdFont, 19.0f, {lx, leftCard.pos.y + dctx.layout.Scale(170.0f)},
                      IM_COL32(175, 205, 235, 240), "High-accuracy Sega Saturn emulation core (GPLv3).");

            // Right Card: Special Thanks
            const Rect rightCard{{rect.pos.x + colW + colGap, y}, {colW, cardH}};
            drawCardFrame(rightCard, "SPECIAL THANKS", "\xEF\x80\x84", IM_COL32(255, 185, 75, 235));
            const float rx = rightCard.pos.x + dctx.layout.Scale(20.0f);
            DrawLabel(dctx, nerdFont, 25.0f, {rx, rightCard.pos.y + dctx.layout.Scale(56.0f)},
                      IM_COL32(255, 230, 140, 255), "MewLew   •   aloisious   •   DanP142");
            DrawLabel(dctx, nerdFont, 21.0f, {rx, rightCard.pos.y + dctx.layout.Scale(94.0f)},
                      IM_COL32(220, 238, 255, 255),
                      "Thank you for all the support, great ideas, testing feedback, and inspiration to join and "
                      "contribute to the Sega Saturn & Xbox emulation community!",
                      colW - dctx.layout.Scale(40.0f));

            y += cardH + dctx.layout.Scale(18.0f);

            // Bottom System Summary Bar
            const Rect sysCard{{rect.pos.x, y}, {rect.size.x, dctx.layout.Scale(126.0f)}};
            drawCardFrame(sysCard, "SYSTEM & PROFILE STATUS", "\xEF\x8B\x9B", IM_COL32(80, 220, 165, 225));
            const float sx = sysCard.pos.x + dctx.layout.Scale(20.0f);
            const std::string biosStr =
                ctx.iplRomPath.empty() ? std::string{"Not configured"} : ToUtf8(ctx.iplRomPath.filename());
            const std::string line1 =
                fmt::format("Active BIOS: {}   |   Games in Library: {}   |   Library Folders: {}", biosStr,
                            m_games.size(), m_config.libraryFolders.size());
            const std::string line2 =
                fmt::format("Profile Path: {}", ToUtf8(ctx.profile.GetPath(app::ProfilePath::Root)));
            DrawLabel(dctx, nerdFont, 22.0f, {sx, sysCard.pos.y + dctx.layout.Scale(54.0f)},
                      IM_COL32(235, 245, 255, 255), line1, sysCard.size.x - dctx.layout.Scale(40.0f));
            DrawLabel(dctx, nerdFont, 19.0f, {sx, sysCard.pos.y + dctx.layout.Scale(86.0f)},
                      IM_COL32(165, 205, 235, 240), line2, sysCard.size.x - dctx.layout.Scale(40.0f));
        };
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
    page.rows.push_back(
        OverlayRow::Action("Main menu", "Return to the main menu; the game stays paused.", [this] { ShowMainMenu(); }));
    page.rows.push_back(OverlayRow::Action("Game library", "Open the game library grid; the game stays paused.",
                                           [this] { ShowGameLibrary(); }));

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
    page.rows.push_back(OverlayRow::Action("Original GUI",
                                           "Switch to the original desktop interface. Press View to return.",
                                           [this] { SwitchToDesktop(); }));
    screen.pages.push_back(std::move(page));
    return screen;
}

OverlayScreen Shell::BuildResetConfirmScreen() {
    OverlayScreen screen{.title = "RESET SAVED DATA", .pages = {}};
    OverlayPage page{.title = "CONFIRM", .rows = {}};
    page.rows.push_back(OverlayRow::Action(
        "Cancel", "Keep your current settings, library, save states and backup memory.", [this] { m_overlay.Pop(); }));
    page.rows.push_back(OverlayRow::Action(
        "Reset all saved data",
        "Delete saved settings, library cache, save states and backup memory, returning Ymir to its initial state.",
        [this] { ResetAllSavedData(); }));
    screen.pages.push_back(std::move(page));
    return screen;
}

OverlayScreen Shell::BuildFilePickerScreen() {
    const char *screenTitle = m_pickerMode == FilePickerMode::Disc   ? "LOAD DISC IMAGE"
                              : m_pickerMode == FilePickerMode::Bios ? "SELECT SATURN BIOS (IPL ROM)"
                                                                     : "SELECT GAMES LIBRARY FOLDER";
    OverlayScreen screen{.title = screenTitle, .pages = {}};

    const std::string pageTitle = m_pickerDir.empty() ? std::string{"DRIVES & SHORTCUTS"} : ToUtf8(m_pickerDir);
    OverlayPage page{.title = pageTitle, .rows = {}};

    const auto navigateTo = [this](std::filesystem::path dir) {
        m_pickerDir = std::move(dir);
        StartPickerLoad(m_pickerDir);
        m_overlay.Replace(BuildFilePickerScreen(), true);
    };

    const auto formatSize = [](std::uint64_t bytes) -> std::string {
        if (bytes >= 1024ull * 1024 * 1024) {
            return fmt::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        }
        if (bytes >= 1024ull * 1024) {
            return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }
        if (bytes >= 1024ull) {
            return fmt::format("{} KB", bytes / 1024ull);
        }
        return fmt::format("{} B", bytes);
    };

    const auto matchesFileFilter = [mode = m_pickerMode](const std::filesystem::path &path) {
        if (mode == FilePickerMode::Folder) {
            return false;
        }
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (mode == FilePickerMode::Disc) {
            return ext == ".cue" || ext == ".chd" || ext == ".iso" || ext == ".ccd" || ext == ".mds";
        }
        // Bios mode: accept common Saturn BIOS extensions (.bin, .rom, .ic17, .bios)
        return ext == ".bin" || ext == ".rom" || ext == ".ic17" || ext == ".bios" || ext.empty();
    };

    if (m_pickerDir.empty()) {
        // Root view: list all accessible drives + quick shortcuts into the app profile
        const std::filesystem::path profileRoot = m_context.profile.GetPath(app::ProfilePath::Root);
        const std::filesystem::path iplFolder = m_context.profile.GetPath(app::ProfilePath::IPLROMImages);

        if (!profileRoot.empty()) {
            OverlayRow row = OverlayRow::Action(
                "App Data (LocalState)", "Browse Ymir's LocalState profile directory (" + ToUtf8(profileRoot) + ").",
                [navigateTo, profileRoot] { navigateTo(profileRoot); }, [] { return std::string{"PROFILE"}; });
            row.icon = "\xEF\x80\x95"; // fa-house (U+F015)
            row.iconColor = IM_COL32(90, 225, 255, 255);
            row.useNerdFont = true;
            page.rows.push_back(std::move(row));
        }
        if (!iplFolder.empty()) {
            OverlayRow row = OverlayRow::Action(
                "IPL ROMs Folder (roms/ipl)", "Browse the default Sega Saturn BIOS folder (" + ToUtf8(iplFolder) + ").",
                [navigateTo, iplFolder] { navigateTo(iplFolder); }, [] { return std::string{"BIOS DIR"}; });
            row.icon = "\xEF\x8B\x9B"; // fa-microchip (U+F2DB)
            row.iconColor = IM_COL32(255, 180, 65, 255);
            row.useNerdFont = true;
            page.rows.push_back(std::move(row));
        }

        for (const fs::DirectoryEntry &drive : m_pickerEntries) {
            const std::string driveStr = ToUtf8(drive.path);
            OverlayRow row = OverlayRow::Action(
                "Drive " + driveStr, "Browse storage volume " + driveStr + " (USB / Dev Drive / Local storage).",
                [navigateTo, drivePath = drive.path] { navigateTo(drivePath); }, [] { return std::string{"DRIVE"}; });
            row.icon = "\xEF\x82\xA0"; // fa-hard-drive (U+F0A0)
            row.iconColor = IM_COL32(130, 230, 175, 255);
            row.useNerdFont = true;
            page.rows.push_back(std::move(row));
        }

        if (m_pickerLoading) {
            page.rows.push_back(OverlayRow::Info(
                "Loading available drives", [] { return std::string{"PLEASE WAIT"}; },
                "Checking accessible storage volumes in the background."));
        } else if (m_pickerReadFailed) {
            page.rows.push_back(OverlayRow::Info(
                "Drives could not be read", [] { return std::string{"UNAVAILABLE"}; },
                "Browse App Data or the IPL ROM folder, or try again later."));
        }

        screen.pages.push_back(std::move(page));
        return screen;
    }

    // Inside a directory:
    // 1. In Folder mode, top action confirms the current directory
    if (m_pickerMode == FilePickerMode::Folder) {
        const std::filesystem::path currentDir = m_pickerDir;
        OverlayRow selectRow = OverlayRow::Action(
            "[ Select this folder ]", "Use " + ToUtf8(currentDir) + " as a games library directory.",
            [this, currentDir] {
                {
                    std::scoped_lock lock{m_pickMutex};
                    m_pickedFolder = currentDir;
                }
                m_overlay.Pop();
            },
            [] { return std::string{"CONFIRM"}; });
        selectRow.icon = "\xEF\x81\x98"; // fa-circle-check (U+F058)
        selectRow.iconColor = IM_COL32(60, 235, 145, 255);
        selectRow.useNerdFont = true;
        page.rows.push_back(std::move(selectRow));
    }

    // 2. Up one folder (`..`) and back to Drives root
    {
        const std::filesystem::path parent = m_pickerDir.parent_path();
        const bool hasDistinctParent = !parent.empty() && parent != m_pickerDir;
        const std::filesystem::path target = hasDistinctParent ? parent : std::filesystem::path{};
        OverlayRow upRow = OverlayRow::Action(
            "[ .. ]  Parent folder",
            hasDistinctParent ? ("Go up to " + ToUtf8(parent)) : "Return to the Drives & Shortcuts list.",
            [navigateTo, target] { navigateTo(target); }, [] { return std::string{"UP"}; });
        upRow.icon = "\xEF\x81\xA2"; // fa-arrow-up (U+F062)
        upRow.iconColor = IM_COL32(120, 215, 255, 255);
        upRow.useNerdFont = true;
        page.rows.push_back(std::move(upRow));
    }
    {
        OverlayRow drivesRow = OverlayRow::Action(
            "[ Drives & Shortcuts ]", "Jump back to the list of storage drives (USB E:\\, D:\\, Q:\\, LocalState).",
            [navigateTo] { navigateTo({}); }, [] { return std::string{"DRIVES"}; });
        drivesRow.icon = "\xEF\x82\xA0"; // fa-hard-drive (U+F0A0)
        drivesRow.iconColor = IM_COL32(130, 230, 175, 255);
        drivesRow.useNerdFont = true;
        page.rows.push_back(std::move(drivesRow));
    }

    if (m_pickerLoading) {
        page.rows.push_back(OverlayRow::Info(
            "Loading folder contents", [] { return std::string{"PLEASE WAIT"}; },
            "Reading this folder in the background; the Shell remains responsive."));
        screen.pages.push_back(std::move(page));
        return screen;
    }
    if (m_pickerReadFailed) {
        page.rows.push_back(OverlayRow::Info(
            "Folder could not be read", [] { return std::string{"ACCESS DENIED"}; },
            "Choose another drive or folder, or use Parent folder to go back."));
    }

    // 3. List subdirectories and matching files
    std::vector<fs::DirectoryEntry> dirs;
    std::vector<fs::DirectoryEntry> files;
    for (const fs::DirectoryEntry &entry : m_pickerEntries) {
        if (entry.isDirectory) {
            dirs.push_back(entry);
        } else if (matchesFileFilter(entry.path)) {
            files.push_back(entry);
        }
    }

    for (const fs::DirectoryEntry &dir : dirs) {
        const std::filesystem::path dirPath = dir.path;
        const std::string name = ToUtf8(dirPath.filename());
        OverlayRow row = OverlayRow::Action(
            name, "Open folder " + ToUtf8(dirPath), [navigateTo, dirPath] { navigateTo(dirPath); },
            [] { return std::string{"DIR"}; });
        row.icon = "\xEF\x81\xBB"; // fa-folder (U+F07B)
        row.iconColor = IM_COL32(255, 205, 85, 255);
        row.useNerdFont = true;
        page.rows.push_back(std::move(row));
    }

    for (const fs::DirectoryEntry &file : files) {
        const std::filesystem::path filePath = file.path;
        const std::string name = ToUtf8(filePath.filename());
        const std::string sizeStr = formatSize(file.size);
        const bool isDisc = m_pickerMode == FilePickerMode::Disc;
        OverlayRow row = OverlayRow::Action(
            name, ToUtf8(filePath),
            [this, filePath, isDisc] {
                m_overlay.Pop();
                if (isDisc) {
                    m_music.PlaySfx(Sfx::Select);
                    LaunchGame(filePath);
                } else {
                    std::scoped_lock lock{m_pickMutex};
                    m_pickedBios = filePath;
                }
            },
            [sizeStr] { return sizeStr; });
        row.icon = isDisc ? "\xEF\x94\x9F" : "\xEF\x8B\x9B"; // fa-compact-disc / fa-microchip
        row.iconColor = isDisc ? IM_COL32(90, 210, 255, 255) : IM_COL32(255, 175, 70, 255);
        row.useNerdFont = true;
        page.rows.push_back(std::move(row));
    }

    screen.pages.push_back(std::move(page));
    return screen;
}

} // namespace platform::uwp::shell

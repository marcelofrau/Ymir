#include "home_screen.hpp"

#include <app/imgui_data.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <span>

namespace platform::uwp::shell {

namespace {

    /// @brief A sphere on the cockpit console.
    struct Option {
        const char *id;
        const char *assetKey;
        const char *iconGlyph;
        const char *label;
        const char *description;
        ImU32 color;
        HomeAction action;
    };

    // Primary-colored spheres inspired by the Sega Saturn BIOS, with Font Awesome 6 Solid emblem glyphs
    constexpr Option kBrowseGames{"##browse",
                                  "browse",
                                  "\xEF\x84\x9B", // fa-gamepad (U+F11B)
                                  "Browse Games",
                                  "Open the game library grid and launch a Sega Saturn disc.",
                                  IM_COL32(255, 175, 40, 255),
                                  HomeAction::BrowseGames};
    constexpr Option kResume{"##resume",
                             "start",
                             "\xEF\x81\x8B", // fa-play (U+F04B)
                             "Resume Game",
                             "Return to the running game.",
                             IM_COL32(50, 215, 195, 255),
                             HomeAction::Start};
    constexpr Option kLoadDisc{"##disc",
                               "disc",
                               "\xEF\x94\x9F", // fa-compact-disc (U+F51F)
                               "Load Disc",
                               "Boot directly from a disc image file (.cue, .chd, .iso, .ccd, .mds).",
                               IM_COL32(60, 150, 255, 255),
                               HomeAction::LoadDisc};
    constexpr Option kBootBios{"##bios_boot",
                               "start",
                               "\xEF\x8B\x9B", // fa-microchip (U+F2DB)
                               "Boot BIOS",
                               "Start the Sega Saturn BIOS and CD player.",
                               IM_COL32(50, 215, 195, 255),
                               HomeAction::Start};
    constexpr Option kSettings{"##settings",
                               "settings",
                               "\xEF\x80\x93", // fa-gear (U+F013)
                               "Settings",
                               "System, video, audio, controls and library settings.",
                               IM_COL32(70, 200, 110, 255),
                               HomeAction::Settings};
    constexpr Option kSelectBios{"##bios",
                                 "bios",
                                 "\xEF\x8B\x9B", // fa-microchip (U+F2DB)
                                 "Select BIOS",
                                 "Browse your drives and choose a Sega Saturn IPL ROM file (.bin, .rom).",
                                 IM_COL32(255, 150, 40, 255),
                                 HomeAction::SelectBios};
    constexpr Option kAddFolder{"##wizard_folder",
                                "folder",
                                "\xEF\x81\xBC", // fa-folder-open (U+F07C)
                                "Add Folder",
                                "Choose a folder with your Sega Saturn disc images to populate the library.",
                                IM_COL32(60, 150, 255, 255),
                                HomeAction::AddFolder};
    constexpr Option kRescanBios{"##rescan",
                                 "rescan",
                                 "\xEF\x80\xA1", // fa-rotate (U+F021)
                                 "Scan BIOS",
                                 "Look for IPL ROM files in the profile ROM folder again.",
                                 IM_COL32(90, 185, 255, 255),
                                 HomeAction::RescanBios};
    constexpr Option kFinishWizard{"##continue",
                                   "start",
                                   "\xEF\x81\x8B", // fa-play (U+F04B)
                                   "Continue",
                                   "Finish initial setup and enter the Ymir main menu.",
                                   IM_COL32(50, 220, 160, 255),
                                   HomeAction::CompleteWizard};
    constexpr Option kDesktop{"##desktop",
                              "desktop",
                              "\xEF\x84\x88", // fa-desktop (U+F108)
                              "Original GUI",
                              "Switch to the original desktop interface. Press View to come back.",
                              IM_COL32(170, 90, 230, 255),
                              HomeAction::Desktop};
    constexpr Option kQuit{"##quit",       "quit",
                           "\xEF\x80\x91", // fa-power-off (U+F011)
                           "Quit",         "Close Ymir.", IM_COL32(225, 50, 60, 255), HomeAction::Quit};

    constexpr std::array kRunOptions{kBrowseGames, kLoadDisc, kBootBios, kSettings, kDesktop, kQuit};
    constexpr std::array kResumeOptions{kBrowseGames, kResume, kLoadDisc, kSettings, kDesktop, kQuit};
    constexpr std::array kWizardNeedBiosOptions{kSelectBios, kAddFolder, kRescanBios, kSettings, kDesktop, kQuit};
    constexpr std::array kWizardWithBiosOptions{kFinishWizard, kSelectBios, kAddFolder, kSettings, kDesktop, kQuit};

    std::span<const Option> OptionsFor(const HomeModel &model) {
        if (model.showWizard || model.biosMissing) {
            return model.biosMissing ? std::span<const Option>{kWizardNeedBiosOptions}
                                     : std::span<const Option>{kWizardWithBiosOptions};
        }
        return model.sessionActive ? std::span<const Option>{kResumeOptions} : std::span<const Option>{kRunOptions};
    }

    Rect ToDisplay(const Layout &layout, Rect rect) {
        return {layout.CanvasPoint(rect.pos), {layout.Scale(rect.size.x), layout.Scale(rect.size.y)}};
    }

    std::string FormatRegion(std::string_view code) {
        std::vector<std::string_view> names;
        if (code.find('J') != std::string_view::npos) {
            names.push_back("Japan");
        }
        if (code.find('U') != std::string_view::npos) {
            names.push_back("North America");
        }
        if (code.find('E') != std::string_view::npos) {
            names.push_back("Europe");
        }
        if (code.find('T') != std::string_view::npos || code.find('A') != std::string_view::npos) {
            names.push_back("Asia");
        }
        if (code.find('B') != std::string_view::npos || code.find('L') != std::string_view::npos) {
            names.push_back("Brazil/LatAm");
        }
        if (code.find('K') != std::string_view::npos) {
            names.push_back("Korea");
        }
        if (names.empty()) {
            return code.empty() ? std::string{"Region Free"} : std::string{code};
        }
        std::string out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                out += " / ";
            }
            out += names[i];
        }
        return out;
    }

    std::string FormatReleaseDate(std::string_view raw) {
        if (raw.size() == 8 && std::all_of(raw.begin(), raw.end(), [](unsigned char c) { return std::isdigit(c); })) {
            return fmt::format("{}-{}-{}", raw.substr(0, 4), raw.substr(4, 2), raw.substr(6, 2));
        }
        return std::string{raw};
    }

    std::string FormatMaker(std::string_view raw) {
        if (raw.empty()) {
            return "Sega";
        }
        if (raw.find("SEGA ENTERPRISES") != std::string_view::npos) {
            return "Sega";
        }
        // Common third-party licensee codes ("SEGA TP T-xxx")
        struct Licensee {
            std::string_view code;
            std::string_view name;
        };
        static constexpr std::array<Licensee, 24> kLicensees{{
            {"T-12", "Capcom"},          {"T-95", "Konami"},
            {"T-143", "Hudson Soft"},    {"T-144", "Atlus"},
            {"T-45", "Game Arts"},       {"T-127", "Working Designs"},
            {"T-36", "Electronic Arts"}, {"T-50", "EA Sports"},
            {"T-70", "Virgin"},          {"T-76", "Koei"},
            {"T-81", "Acclaim"},         {"T-11", "Taito"},
            {"T-15", "Sunsoft"},         {"T-18", "Tecmo"},
            {"T-24", "Data East"},       {"T-31", "SNK"},
            {"T-66", "Compile"},         {"T-91", "Victor"},
            {"T-133", "Bandai"},         {"T-197", "Marvelous"},
            {"T-254", "Psychgnosis"},    {"T-261", "Eidos"},
            {"T-279", "ESP / Treasure"}, {"T-325", "Quintet"},
        }};
        for (const auto &lic : kLicensees) {
            if (raw.find(lic.code) != std::string_view::npos) {
                return std::string{lic.name};
            }
        }
        if (raw.starts_with("SEGA TP ")) {
            return std::string{raw.substr(8)};
        }
        return std::string{raw};
    }

    std::string FormatPeripherals(std::string_view raw) {
        std::vector<std::string_view> list;
        if (raw.find('J') != std::string_view::npos) {
            list.push_back("Control Pad");
        }
        if (raw.find('E') != std::string_view::npos || raw.find('A') != std::string_view::npos) {
            list.push_back("3D Analog Pad");
        }
        if (raw.find('W') != std::string_view::npos) {
            list.push_back("RAM Cart");
        }
        if (raw.find('M') != std::string_view::npos) {
            list.push_back("Mouse");
        }
        if (raw.find('G') != std::string_view::npos) {
            list.push_back("Virtua Gun");
        }
        if (raw.find('S') != std::string_view::npos) {
            list.push_back("Arcade Racer");
        }
        if (raw.find('K') != std::string_view::npos) {
            list.push_back("Keyboard");
        }
        if (list.empty()) {
            return "Standard Pad";
        }
        std::string out;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += list[i];
        }
        return out;
    }

} // namespace

HomeResult HomeScreen::Draw(const DrawContext &ctx, const HomeModel &model) {
    HomeResult result{};

    const bool wizardActive = model.showWizard || model.biosMissing;
    if (wizardActive != m_lastShowWizard || model.biosMissing != m_lastBiosMissing) {
        m_lastShowWizard = wizardActive;
        m_lastBiosMissing = model.biosMissing;
        m_view = View::Spheres;
        m_focusIndex = 0;
        m_focusRequested = true;
    }

    if (wizardActive) {
        DrawWizard(ctx, model, result);
        return result;
    }

    if (m_view == View::Library) {
        // LB/RB switch between all games and the recent discs; B returns to the Saturn BIOS sphere menu
        if (model.inputEnabled) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
                m_tab = m_tab == Tab::All ? Tab::Recent : Tab::All;
                m_focusRequested = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) {
                result.action = HomeAction::AddFolder;
            } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) {
                result.action = HomeAction::Rescan;
            } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
                m_view = View::Spheres;
                m_focusKind = FocusKind::Sphere;
                m_focusIndex = 0;
                m_focusRequested = true;
                result.action = HomeAction::BrowseGames;
                return result;
            }
        }
        DrawGrid(ctx, model, result);
        DrawTabs(ctx, model);
        DrawGameDetails(ctx, model);
    } else {
        DrawSpheres(ctx, model, result);
        DrawSphereHeader(ctx, model);
        if (result.action == HomeAction::BrowseGames) {
            m_view = View::Library;
            m_focusRequested = true;
        }
    }

    return result;
}

void HomeScreen::DrawWizard(const DrawContext &ctx, const HomeModel &model, HomeResult &result) {
    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    ImFont *nerdFont = ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : imguiData->fonts.sansSerif.regular;
    ImFont *iconFont = ctx.theme.GetIconFont();

    const Rect panel = ToDisplay(ctx.layout, {{330.0f, 155.0f}, {1260.0f, 455.0f}});
    DrawOverlayPanel(ctx, panel, "WELCOME TO YMIR - INITIAL SETUP");

    // Draw Ymir planet icon in the top-right corner of the welcome panel
    if (const ThemeAsset *icon = ctx.theme.Find("ymir_icon")) {
        const float iconSize = ctx.layout.Scale(82.0f);
        DrawAsset(
            ctx, *icon,
            {{panel.pos.x + panel.size.x - iconSize - ctx.layout.Scale(28.0f), panel.pos.y + ctx.layout.Scale(18.0f)},
             {iconSize, iconSize}});
    }

    const float padX = ctx.layout.Scale(36.0f);
    DrawLabel(ctx, nerdFont, 24.0f, {panel.pos.x + padX, panel.pos.y + ctx.layout.Scale(78.0f)},
              IM_COL32(215, 235, 255, 255), "Choose your Saturn BIOS and game library using the options below.",
              panel.size.x - padX * 2.0f - ctx.layout.Scale(90.0f));

    const auto drawStepBox = [&](float refY, bool ready, std::string_view stepTitle, std::string_view statusLine,
                                 std::string_view detailLine) {
        const Rect box = ToDisplay(ctx.layout, {{366.0f, refY}, {1188.0f, 108.0f}});
        const ImU32 accent = ready ? IM_COL32(55, 225, 145, 255) : IM_COL32(255, 175, 60, 255);
        ctx.drawList->AddRectFilled(ToImVec2(box.pos), ImVec2(box.pos.x + box.size.x, box.pos.y + box.size.y),
                                    IM_COL32(6, 22, 46, 210), ctx.layout.Scale(10.0f));
        ctx.drawList->AddRect(ToImVec2(box.pos), ImVec2(box.pos.x + box.size.x, box.pos.y + box.size.y), accent,
                              ctx.layout.Scale(10.0f), 0, std::max(1.0f, ctx.layout.Scale(1.8f)));

        if (iconFont != nullptr) {
            const char *glyph = ready ? "\xEF\x81\x98" : "\xEF\x81\xAA"; // fa-circle-check / fa-circle-exclamation
            DrawLabelCentered(ctx, iconFont, 34.0f,
                              {box.pos.x + ctx.layout.Scale(40.0f), box.pos.y + box.size.y * 0.5f}, accent, glyph);
        }

        const float textX = box.pos.x + ctx.layout.Scale(76.0f);
        DrawDotText(ctx, {textX, box.pos.y + ctx.layout.Scale(14.0f)}, 2.8f, stepTitle, accent);
        const float textRight = box.pos.x + box.size.x - ctx.layout.Scale(18.0f);
        ctx.drawList->PushClipRect(ImVec2(textX, box.pos.y + ctx.layout.Scale(38.0f)),
                                   ImVec2(textRight, box.pos.y + box.size.y - ctx.layout.Scale(8.0f)), true);
        DrawLabel(ctx, nerdFont, 21.0f, {textX, box.pos.y + ctx.layout.Scale(42.0f)}, IM_COL32(240, 248, 255, 255),
                  statusLine);
        DrawLabel(ctx, nerdFont, 18.0f, {textX, box.pos.y + ctx.layout.Scale(72.0f)}, IM_COL32(160, 205, 235, 235),
                  detailLine);
        ctx.drawList->PopClipRect();
    };

    const bool biosReady = !model.biosMissing;
    const std::string biosStatus = biosReady ? fmt::format("READY: {}", model.biosName)
                                             : std::string{"REQUIRED: No Sega Saturn IPL ROM selected yet"};
    const std::string biosDetail = "Select BIOS to browse drives and choose a .bin or .rom file.";
    drawStepBox(280.0f, biosReady, "STEP 1 - SEGA SATURN BIOS (IPL ROM)", biosStatus, biosDetail);

    const std::size_t gameCount = model.games != nullptr ? model.games->size() : 0;
    const bool libraryReady = model.folderCount > 0;
    const std::string libStatus =
        libraryReady ? fmt::format("READY: {} folder(s) configured ({} games found)", model.folderCount, gameCount)
                     : std::string{"RECOMMENDED: Choose a folder containing your Saturn disc images"};
    const std::string libDetail = "Select Add Folder to choose where your Saturn games are stored.";
    drawStepBox(402.0f, libraryReady, "STEP 2 - GAMES LIBRARY DIRECTORY", libStatus, libDetail);

    // Focused sphere description at the bottom of the wizard panel
    const std::span<const Option> options = OptionsFor(model);
    if (!options.empty()) {
        const Option &opt = options[std::min(m_focusIndex, options.size() - 1)];
        DrawDotTextCentered(ctx, ctx.layout.CanvasPoint({960.0f, 548.0f}), 2.9f, opt.label,
                            IM_COL32(130, 230, 255, 255));
        DrawLabelCentered(ctx, nerdFont, 19.0f, ctx.layout.CanvasPoint({960.0f, 582.0f}), IM_COL32(215, 235, 255, 255),
                          opt.description);
    }

    DrawSpheres(ctx, model, result);
}

void HomeScreen::DrawTabs(const DrawContext &ctx, const HomeModel &model) {
    const ThemeLayout &anchors = ctx.theme.GetLayout();
    const Vec2 origin = ctx.layout.CanvasPoint({960.0f, anchors.grid.pos.y + anchors.grid.size.y + 18.0f});
    constexpr float kPitch = 2.8f;

    const std::size_t allCount = model.games != nullptr ? model.games->size() : 0;
    const std::size_t recentCount = model.recent != nullptr ? model.recent->size() : 0;
    const std::string all = fmt::format("ALL GAMES {}", allCount);
    const std::string recent = fmt::format("RECENT {}", recentCount);

    // The selected tab is lit and boxed, the other one dimmed, with the LB/RB glyphs on both ends
    constexpr float kBumperRadius = 14.0f;
    const float bumperHalfW = ctx.layout.Scale(kBumperRadius * 1.55f);
    const float bumperGap = ctx.layout.Scale(26.0f);
    const float tabGap = ctx.layout.Scale(36.0f);
    const float padX = ctx.layout.Scale(14.0f);
    const float padY = ctx.layout.Scale(7.0f);
    const float textHeight = MeasureDotText(ctx, kPitch, all).y;
    const float centerY = origin.y + textHeight * 0.5f;

    const float tabsWidth =
        MeasureDotText(ctx, kPitch, all).x + MeasureDotText(ctx, kPitch, recent).x + padX * 4.0f + tabGap;
    const float groupWidth = bumperHalfW * 4.0f + bumperGap * 2.0f + tabsWidth;
    float x = origin.x - groupWidth * 0.5f;
    DrawGlyph(ctx, Glyph::LB, {x + bumperHalfW, centerY}, kBumperRadius);
    x += bumperHalfW * 2.0f + bumperGap;

    const auto tab = [&](const std::string &text, bool selected) {
        const Vec2 size = MeasureDotText(ctx, kPitch, text);
        if (selected) {
            ctx.drawList->AddRect(
                ImVec2(x, origin.y - padY), ImVec2(x + size.x + padX * 2.0f, origin.y + size.y + padY),
                IM_COL32(90, 220, 255, 200), ctx.layout.Scale(4.0f), 0, std::max(1.0f, ctx.layout.Scale(1.5f)));
        }
        DrawDotText(ctx, {x + padX, origin.y}, kPitch, text,
                    selected ? IM_COL32(200, 245, 255, 255) : IM_COL32(110, 160, 190, 200));
        x += size.x + padX * 2.0f;
    };
    tab(all, m_tab == Tab::All);
    x += tabGap;
    tab(recent, m_tab == Tab::Recent);
    x += bumperGap;
    DrawGlyph(ctx, Glyph::RB, {x + bumperHalfW, centerY}, kBumperRadius);

    // Scan progress on the right
    if (model.scanning) {
        const std::string text = fmt::format("SCANNING {}", model.scanProgress);
        const Vec2 size = MeasureDotText(ctx, kPitch, text);
        const Vec2 right = ctx.layout.CanvasPoint({anchors.grid.pos.x + anchors.grid.size.x, 0.0f});
        const float blink = 0.6f + 0.4f * std::sin(static_cast<float>(ctx.time) * 6.0f);
        DrawDotText(ctx, {right.x - size.x, origin.y}, kPitch, text,
                    IM_COL32(255, 200, 90, static_cast<int>(255 * blink)));
    }
}

void HomeScreen::DrawGrid(const DrawContext &ctx, const HomeModel &model, HomeResult &result) {
    const ThemeLayout &anchors = ctx.theme.GetLayout();
    const std::vector<GameEntry> empty;
    const std::vector<GameEntry> &games = m_tab == Tab::All ? (model.games != nullptr ? *model.games : empty)
                                                            : (model.recent != nullptr ? *model.recent : empty);

    const Rect grid = ToDisplay(ctx.layout, anchors.grid);
    const float margin = ctx.layout.Scale(16.0f);
    ImGui::SetCursorScreenPos(ImVec2(grid.pos.x - margin, grid.pos.y - margin));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin, margin));
    const bool open = ImGui::BeginChild("##library", ImVec2(grid.size.x + margin * 2.0f, grid.size.y + margin * 2.0f),
                                        ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding,
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (open) {
        const DrawContext gridCtx{
            ImGui::GetWindowDrawList(), ctx.layout, ctx.theme, ctx.graphics, ctx.shapes, ctx.time, ctx.deltaTime};
        const int columns = std::max(anchors.gridColumns, 1);
        const float gap = ctx.layout.Scale(24.0f);
        const float tileW = (grid.size.x - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        const float tileH = tileW * 1.12f;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool showAddFolder = m_tab == Tab::All && !model.scanning;
        const std::size_t count = games.size() + (showAddFolder ? 1 : 0);
        m_tiles.resize(games.size());

        for (std::size_t i = 0; i < count; ++i) {
            const auto column = static_cast<float>(i % columns);
            const auto row = static_cast<float>(i / columns);
            const Rect tile{{origin.x + column * (tileW + gap), origin.y + row * (tileH + gap)}, {tileW, tileH}};

            if (i == 0 && m_focusRequested) {
                ImGui::SetKeyboardFocusHere();
                m_focusRequested = false;
            }

            if (i < games.size()) {
                const GameEntry &game = games[i];
                const ThemeAsset *cover = model.covers != nullptr ? model.covers->Find(ctx.graphics, game) : nullptr;
                ImGui::PushID(static_cast<int>(i));
                if (HologramTile(gridCtx, "##game", tile, game.title, game.productCode, cover, static_cast<int>(i),
                                 m_tiles[i])) {
                    result.action = HomeAction::LaunchGame;
                    result.game = game.path;
                }
                if (ImGui::IsItemFocused()) {
                    m_focusKind = FocusKind::Game;
                    m_focusIndex = i;
                }
                ImGui::PopID();
            } else {
                if (HologramActionTile(gridCtx, "##addfolder", tile, "+", "ADD FOLDER", m_addFolderTile)) {
                    result.action = HomeAction::AddFolder;
                }
                if (ImGui::IsItemFocused()) {
                    m_focusKind = FocusKind::AddFolder;
                }
            }
        }

        if (count == 0) {
            DrawDotTextCentered(gridCtx, {grid.pos.x + grid.size.x * 0.5f, grid.pos.y + grid.size.y * 0.45f}, 3.4f,
                                m_tab == Tab::Recent ? "No recent discs yet" : "Scanning your library...",
                                IM_COL32(150, 215, 255, 220));
        }
    }
    ImGui::EndChild();
}

void HomeScreen::DrawGameDetails(const DrawContext &ctx, const HomeModel &model) {
    const std::vector<GameEntry> empty;
    const std::vector<GameEntry> &games = m_tab == Tab::All ? (model.games != nullptr ? *model.games : empty)
                                                            : (model.recent != nullptr ? *model.recent : empty);

    const Rect card = ToDisplay(ctx.layout, {{350.0f, 660.0f}, {1220.0f, 300.0f}});
    const float rounding = ctx.layout.Scale(14.0f);
    const ImVec2 p0 = ToImVec2(card.pos);
    const ImVec2 p1(card.pos.x + card.size.x, card.pos.y + card.size.y);
    ctx.drawList->AddRectFilled(p0, p1, IM_COL32(6, 16, 36, 232), rounding);
    ctx.drawList->AddRect(p0, p1, IM_COL32(80, 205, 255, 200), rounding, 0, std::max(1.0f, ctx.layout.Scale(2.0f)));

    // Corner brackets
    const float bracket = ctx.layout.Scale(22.0f);
    const float line = std::max(1.0f, ctx.layout.Scale(2.5f));
    const ImU32 brkCol = IM_COL32(110, 235, 255, 235);
    ctx.drawList->AddLine(p0, ImVec2(p0.x + bracket, p0.y), brkCol, line);
    ctx.drawList->AddLine(p0, ImVec2(p0.x, p0.y + bracket), brkCol, line);
    ctx.drawList->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x - bracket, p0.y), brkCol, line);
    ctx.drawList->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p0.y + bracket), brkCol, line);
    ctx.drawList->AddLine(p1, ImVec2(p1.x - bracket, p1.y), brkCol, line);
    ctx.drawList->AddLine(p1, ImVec2(p1.x, p1.y - bracket), brkCol, line);
    ctx.drawList->AddLine(ImVec2(p0.x, p1.y), ImVec2(p0.x + bracket, p1.y), brkCol, line);
    ctx.drawList->AddLine(ImVec2(p0.x, p1.y), ImVec2(p0.x, p1.y - bracket), brkCol, line);

    ImFont *nerdFont = ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : ImGui::GetFont();

    if (m_focusKind == FocusKind::AddFolder || games.empty() || m_focusIndex >= games.size()) {
        DrawDotText(ctx, {card.pos.x + ctx.layout.Scale(32.0f), card.pos.y + ctx.layout.Scale(28.0f)}, 3.4f,
                    "ADD GAMES FOLDER", IM_COL32(120, 225, 255, 255));
        DrawLabel(ctx, nerdFont, 24.0f, {card.pos.x + ctx.layout.Scale(32.0f), card.pos.y + ctx.layout.Scale(82.0f)},
                  IM_COL32(220, 238, 255, 255),
                  "Press A (or X) to open the Ymir Folder Browser and add a directory with your Sega Saturn disc "
                  "images (.chd, .cue, .iso, .ccd, .mds). Cover art and game synopses are downloaded automatically on "
                  "demand and cached in your profile.",
                  card.size.x - ctx.layout.Scale(64.0f));
        return;
    }

    const GameEntry &game = games[m_focusIndex];
    const ThemeAsset *cover = model.covers != nullptr ? model.covers->Find(ctx.graphics, game) : nullptr;
    const std::string description = model.covers != nullptr ? model.covers->FindDescription(game) : std::string{};

    // Left cover thumbnail box
    const float pad = ctx.layout.Scale(22.0f);
    const Rect thumb{{card.pos.x + pad, card.pos.y + pad}, {ctx.layout.Scale(210.0f), card.size.y - pad * 2.0f}};
    ctx.drawList->AddRectFilled(ToImVec2(thumb.pos), ImVec2(thumb.pos.x + thumb.size.x, thumb.pos.y + thumb.size.y),
                                IM_COL32(4, 10, 24, 220), ctx.layout.Scale(8.0f));
    ctx.drawList->AddRect(ToImVec2(thumb.pos), ImVec2(thumb.pos.x + thumb.size.x, thumb.pos.y + thumb.size.y),
                          IM_COL32(70, 175, 235, 160), ctx.layout.Scale(8.0f));
    if (cover != nullptr) {
        const float scale = std::min((thumb.size.x - ctx.layout.Scale(10.0f)) / cover->FrameWidth(),
                                     (thumb.size.y - ctx.layout.Scale(10.0f)) / cover->FrameHeight());
        const Vec2 sz{cover->FrameWidth() * scale, cover->FrameHeight() * scale};
        DrawAsset(ctx, *cover,
                  {{thumb.pos.x + (thumb.size.x - sz.x) * 0.5f, thumb.pos.y + (thumb.size.y - sz.y) * 0.5f}, sz});
    } else if (ctx.theme.GetIconFont() != nullptr) {
        DrawLabelCentered(ctx, ctx.theme.GetIconFont(), 68.0f,
                          {thumb.pos.x + thumb.size.x * 0.5f, thumb.pos.y + thumb.size.y * 0.45f},
                          IM_COL32(90, 200, 255, 190), "\xEF\x94\x9F");
        DrawDotTextCentered(ctx, {thumb.pos.x + thumb.size.x * 0.5f, thumb.pos.y + thumb.size.y * 0.80f}, 2.2f,
                            game.productCode.empty() ? "SATURN" : game.productCode, IM_COL32(150, 220, 255, 220));
    }

    // Right column: title, metadata badges, and synopsis
    const float infoX = thumb.pos.x + thumb.size.x + ctx.layout.Scale(26.0f);
    const float infoW = card.pos.x + card.size.x - pad - infoX;
    float y = card.pos.y + pad;

    ctx.drawList->PushClipRect(ImVec2(infoX, y), ImVec2(infoX + infoW, y + ctx.layout.Scale(36.0f)), true);
    DrawLabel(ctx, nerdFont, 28.0f, {infoX, y}, IM_COL32(240, 250, 255, 255), game.title);
    ctx.drawList->PopClipRect();
    y += ctx.layout.Scale(38.0f);

    const std::string metaLine1 =
        fmt::format("Product: {}   |   Region: {}   |   Release: {}   |   Disc: {}",
                    game.productCode.empty() ? "N/A" : game.productCode, FormatRegion(game.region),
                    game.releaseDate.empty() ? "Unknown" : FormatReleaseDate(game.releaseDate),
                    game.deviceInfo.empty() ? "CD-1/1" : game.deviceInfo);
    ctx.drawList->PushClipRect(ImVec2(infoX, y), ImVec2(infoX + infoW, y + ctx.layout.Scale(25.0f)), true);
    DrawLabel(ctx, nerdFont, 18.0f, {infoX, y}, IM_COL32(120, 225, 255, 255), metaLine1);
    ctx.drawList->PopClipRect();
    y += ctx.layout.Scale(28.0f);

    const std::string metaLine2 =
        fmt::format("Publisher: {}   |   Input: {}   |   Version: {}", FormatMaker(game.makerId),
                    FormatPeripherals(game.peripherals), game.version.empty() ? "1.000" : game.version);
    ctx.drawList->PushClipRect(ImVec2(infoX, y), ImVec2(infoX + infoW, y + ctx.layout.Scale(25.0f)), true);
    DrawLabel(ctx, nerdFont, 18.0f, {infoX, y}, IM_COL32(255, 205, 110, 245), metaLine2);
    ctx.drawList->PopClipRect();
    y += ctx.layout.Scale(32.0f);

    // Separator line
    ctx.drawList->AddLine(ImVec2(infoX, y), ImVec2(infoX + infoW, y), IM_COL32(80, 190, 240, 110), 1.0f);
    y += ctx.layout.Scale(10.0f);

    const std::string shownSynopsis =
        !description.empty()
            ? description
            : fmt::format("{} ({}) for the Sega Saturn. Published by {} ({}). Compatible with {}. Press A to launch "
                          "this disc.",
                          game.title, FormatRegion(game.region), FormatMaker(game.makerId),
                          game.productCode.empty() ? "Saturn CD-ROM" : game.productCode,
                          FormatPeripherals(game.peripherals));

    ctx.drawList->PushClipRect(ImVec2(infoX, y), ImVec2(infoX + infoW, card.pos.y + card.size.y - pad), true);
    DrawLabel(ctx, nerdFont, 21.0f, {infoX, y}, IM_COL32(205, 228, 248, 255), shownSynopsis, infoW);
    ctx.drawList->PopClipRect();
}

void HomeScreen::DrawSpheres(const DrawContext &ctx, const HomeModel &model, HomeResult &result) {
    const std::span<const Option> options = OptionsFor(model);

    if (model.showWizard || model.biosMissing) {
        // Single row of spheres overlapping the top of the bottom dashboard (y = 735, dashboard starts at y = 675)
        for (std::size_t i = 0; i < options.size(); ++i) {
            const Option &option = options[i];
            const float t = options.size() > 1 ? static_cast<float>(i) / static_cast<float>(options.size() - 1) : 0.5f;
            const Vec2 refPos{510.0f + (1410.0f - 510.0f) * t, 760.0f};
            const Vec2 center = ctx.layout.CanvasPoint(refPos);

            if (i == 0 && m_focusRequested) {
                ImGui::SetKeyboardFocusHere();
                m_focusRequested = false;
            }
            if (SphereButton(ctx, option.id, option.assetKey, option.iconGlyph, center, 40.0f, option.color,
                             m_spheres[i])) {
                result.action = option.action;
            }
            const bool focused = ImGui::IsItemFocused();
            if (focused) {
                m_focusKind = FocusKind::Sphere;
                m_focusIndex = i;
            }
            DrawDotTextCentered(ctx, ctx.layout.CanvasPoint({refPos.x, refPos.y + 62.0f}), 2.4f, option.label,
                                focused ? IM_COL32(255, 255, 255, 255) : IM_COL32(165, 210, 240, 210));
        }
        return;
    }

    // Saturn BIOS 3x2 sphere matrix lowered so the bottom row overlaps the bottom dashboard (which starts at y = 675)
    constexpr int kColumns = 3;
    constexpr std::array<float, kColumns> kColX{765.0f, 960.0f, 1155.0f};
    constexpr std::array<float, 2> kRowY{565.0f, 720.0f};
    constexpr float kSphereRadius = 46.0f;

    for (std::size_t i = 0; i < options.size(); ++i) {
        const Option &option = options[i];
        const std::size_t col = i % kColumns;
        const std::size_t row = std::min<std::size_t>(i / kColumns, 1);
        const Vec2 refPos{kColX[col], kRowY[row]};
        const Vec2 center = ctx.layout.CanvasPoint(refPos);

        if (i == 0 && m_focusRequested) {
            ImGui::SetKeyboardFocusHere();
            m_focusRequested = false;
        }
        if (SphereButton(ctx, option.id, option.assetKey, option.iconGlyph, center, kSphereRadius, option.color,
                         m_spheres[i])) {
            result.action = option.action;
        }
        const bool focused = ImGui::IsItemFocused();
        if (focused) {
            m_focusKind = FocusKind::Sphere;
            m_focusIndex = i;
        }
        DrawDotTextCentered(ctx, ctx.layout.CanvasPoint({refPos.x, refPos.y + kSphereRadius + 22.0f}), 2.4f,
                            option.label, focused ? IM_COL32(255, 255, 255, 255) : IM_COL32(165, 210, 240, 210));
    }
}

void HomeScreen::DrawSphereHeader(const DrawContext &ctx, const HomeModel &model) {
    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    const std::span<const Option> options = OptionsFor(model);
    if (options.empty()) {
        return;
    }
    const std::size_t idx = std::min(m_focusIndex, options.size() - 1);
    const Option &option = options[idx];

    DrawHoloLabelCentered(ctx, imguiData->fonts.display, 42.0f, ctx.layout.CanvasPoint({960.0f, 345.0f}), option.label);
    DrawDotTextCentered(ctx, ctx.layout.CanvasPoint({960.0f, 392.0f}), 3.0f, option.description,
                        IM_COL32(150, 215, 255, 255));
}

std::vector<ButtonHint> HomeScreen::Legend(const HomeModel &model) const {
    if (model.showWizard || model.biosMissing) {
        const std::span<const Option> options = OptionsFor(model);
        const char *selectLabel =
            !options.empty() ? options[std::min(m_focusIndex, options.size() - 1)].label : "Select";
        return {
            {Glyph::A, selectLabel},
        };
    }

    if (m_view == View::Spheres) {
        std::vector<ButtonHint> hints{
            {Glyph::A, "Select"},
        };
        if (model.sessionActive) {
            hints.push_back({Glyph::View, "Back to Game"});
        }
        return hints;
    }

    std::vector<ButtonHint> hints;
    switch (m_focusKind) {
    case FocusKind::Game: hints.push_back({Glyph::A, "Play"}); break;
    case FocusKind::AddFolder: hints.push_back({Glyph::A, "Add Folder"}); break;
    case FocusKind::Sphere: hints.push_back({Glyph::A, "Select"}); break;
    }
    hints.push_back({Glyph::B, "Main Menu"});
    hints.push_back({Glyph::X, "Add Folder"});
    hints.push_back({Glyph::Y, "Rescan"});
    hints.push_back({Glyph::LB, "Tab"});
    return hints;
}

} // namespace platform::uwp::shell

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
        const char *label;
        const char *description;
        ImU32 color;
        HomeAction action;
    };

    // The option sets for the states of the home screen. Colors follow the primary-colored spheres of the Saturn
    // BIOS
    constexpr Option kStart{
        "##start",        "start", "Start", "Run the Saturn with the disc in the tray.", IM_COL32(255, 150, 40, 255),
        HomeAction::Start};
    constexpr Option kResume{"##start",        "start", "Resume", "Return to the game.", IM_COL32(255, 150, 40, 255),
                             HomeAction::Start};
    constexpr Option kLoadDisc{"##disc",
                               "disc",
                               "Load Disc",
                               "Choose a disc image file (.cue, .chd, .iso, .ccd, .mds).",
                               IM_COL32(60, 140, 255, 255),
                               HomeAction::LoadDisc};
    constexpr Option kSettings{"##settings",
                               "settings",
                               "Settings",
                               "System, video, audio and input settings.",
                               IM_COL32(70, 200, 110, 255),
                               HomeAction::Settings};
    constexpr Option kSelectBios{"##bios",
                                 "bios",
                                 "Select BIOS",
                                 "Choose a Saturn IPL ROM file (.bin, .rom).",
                                 IM_COL32(255, 150, 40, 255),
                                 HomeAction::SelectBios};
    constexpr Option kRescanBios{"##rescan",
                                 "rescan",
                                 "Scan Again",
                                 "Look for IPL ROM files in the ROM folder again.",
                                 IM_COL32(60, 140, 255, 255),
                                 HomeAction::RescanBios};
    constexpr Option kDesktop{"##desktop",
                              "desktop",
                              "Desktop UI",
                              "Switch to the full desktop interface. Hold View for one second to come back.",
                              IM_COL32(170, 90, 230, 255),
                              HomeAction::Desktop};
    constexpr Option kQuit{"##quit", "quit", "Quit", "Close Ymir.", IM_COL32(225, 50, 60, 255), HomeAction::Quit};

    constexpr std::array kRunOptions{kStart, kLoadDisc, kSettings, kDesktop, kQuit};
    constexpr std::array kResumeOptions{kResume, kLoadDisc, kSettings, kDesktop, kQuit};
    constexpr std::array kBiosOptions{kSelectBios, kRescanBios, kSettings, kDesktop, kQuit};

    std::span<const Option> OptionsFor(const HomeModel &model) {
        return model.biosMissing     ? std::span<const Option>{kBiosOptions}
               : model.sessionActive ? std::span<const Option>{kResumeOptions}
                                     : std::span<const Option>{kRunOptions};
    }

    Rect ToDisplay(const Layout &layout, Rect rect) {
        return {layout.CanvasPoint(rect.pos), {layout.Scale(rect.size.x), layout.Scale(rect.size.y)}};
    }

    /// @brief Formats a header release date (YYYYMMDD) as YYYY-MM-DD.
    std::string FormatDate(const std::string &date) {
        if (date.size() != 8) {
            return date;
        }
        return date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2);
    }

} // namespace

HomeResult HomeScreen::Draw(const DrawContext &ctx, const HomeModel &model) {
    HomeResult result{};

    // A different option set means the old focus target no longer exists; start again from the beginning
    if (model.biosMissing != m_lastBiosMissing) {
        m_lastBiosMissing = model.biosMissing;
        m_focusRequested = true;
    }

    // LB/RB switch between all games and the recent discs
    if (model.inputEnabled && !model.biosMissing) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
            m_tab = m_tab == Tab::All ? Tab::Recent : Tab::All;
            m_focusRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) {
            result.action = HomeAction::AddFolder;
        } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) {
            result.action = HomeAction::Rescan;
        }
    }

    if (model.biosMissing) {
        const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
        const Rect panel = ToDisplay(ctx.layout, ctx.theme.GetLayout().messagePanel);
        DrawOverlayPanel(ctx, panel, "BIOS NOT FOUND");
        const std::string text =
            fmt::format("Ymir needs a Sega Saturn IPL ROM (BIOS) to run. Choose one with Select BIOS, or copy it to "
                        "the folder below with the Device Portal and choose Scan Again:\n{}",
                        model.iplFolder);
        DrawLabel(ctx, imguiData->fonts.sansSerif.regular, 26.0f,
                  {panel.pos.x + ctx.layout.Scale(36.0f), panel.pos.y + ctx.layout.Scale(82.0f)},
                  IM_COL32(220, 230, 250, 255), text, panel.size.x - ctx.layout.Scale(72.0f));
    } else {
        DrawTabs(ctx, model);
        DrawGrid(ctx, model, result);
    }

    DrawSpheres(ctx, model, result);
    DrawFocusInfo(ctx, model);
    return result;
}

void HomeScreen::DrawTabs(const DrawContext &ctx, const HomeModel &model) {
    const ThemeLayout &anchors = ctx.theme.GetLayout();
    const Vec2 origin = ctx.layout.CanvasPoint({anchors.grid.pos.x, anchors.grid.pos.y - 30.0f});
    constexpr float kPitch = 2.8f;

    const std::size_t allCount = model.games != nullptr ? model.games->size() : 0;
    const std::size_t recentCount = model.recent != nullptr ? model.recent->size() : 0;
    const std::string all = fmt::format("ALL GAMES {}", allCount);
    const std::string recent = fmt::format("RECENT {}", recentCount);

    // The selected tab is lit and boxed, the other one dimmed, with the LB/RB glyphs on both ends
    float x = origin.x;
    DrawGlyph(ctx, Glyph::LB, {x + ctx.layout.Scale(26.0f), origin.y + ctx.layout.Scale(9.0f)}, 14.0f);
    x += ctx.layout.Scale(60.0f);
    const auto tab = [&](const std::string &text, bool selected) {
        const Vec2 size = MeasureDotText(ctx, kPitch, text);
        const float pad = ctx.layout.Scale(10.0f);
        if (selected) {
            ctx.drawList->AddRect(ImVec2(x - pad, origin.y - pad * 0.6f),
                                  ImVec2(x + size.x + pad, origin.y + size.y + pad * 0.6f), IM_COL32(90, 220, 255, 200),
                                  ctx.layout.Scale(4.0f), 0, std::max(1.0f, ctx.layout.Scale(1.5f)));
        }
        DrawDotText(ctx, {x, origin.y}, kPitch, text,
                    selected ? IM_COL32(200, 245, 255, 255) : IM_COL32(110, 160, 190, 200));
        x += size.x + pad * 3.0f;
    };
    tab(all, m_tab == Tab::All);
    tab(recent, m_tab == Tab::Recent);
    DrawGlyph(ctx, Glyph::RB, {x + ctx.layout.Scale(10.0f), origin.y + ctx.layout.Scale(9.0f)}, 14.0f);

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

    // The grid lives in a child window so it scrolls on its own; NavFlattened lets the d-pad move between its tiles
    // and the spheres as if they were in the same window. The window is padded so the glow of a lifted tile at the
    // edge is not clipped
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

        // The tiles scroll with the child window; positions are relative to its content origin
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

void HomeScreen::DrawSpheres(const DrawContext &ctx, const HomeModel &model, HomeResult &result) {
    const SphereRow &row = ctx.theme.GetLayout().spheres;
    const std::span<const Option> options = OptionsFor(model);

    // If the grid had nothing to focus (or is hidden), the first sphere takes the focus
    for (std::size_t i = 0; i < options.size(); ++i) {
        const Option &option = options[i];
        const float t = options.size() > 1 ? static_cast<float>(i) / static_cast<float>(options.size() - 1) : 0.5f;
        const Vec2 center = ctx.layout.CanvasPoint({row.firstX + (row.lastX - row.firstX) * t, row.y});

        if (i == 0 && m_focusRequested) {
            ImGui::SetKeyboardFocusHere();
            m_focusRequested = false;
        }
        if (SphereButton(ctx, option.id, option.assetKey, center, row.radius, option.color, m_spheres[i])) {
            result.action = option.action;
        }
        if (ImGui::IsItemFocused()) {
            m_focusKind = FocusKind::Sphere;
            m_focusIndex = i;
        }
    }
}

void HomeScreen::DrawFocusInfo(const DrawContext &ctx, const HomeModel &model) {
    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    const ThemeLayout &anchors = ctx.theme.GetLayout();

    std::string label;
    std::string description;
    const std::vector<GameEntry> *games = m_tab == Tab::All ? model.games : model.recent;
    if (m_focusKind == FocusKind::Game && games != nullptr && m_focusIndex < games->size() && !model.biosMissing) {
        const GameEntry &game = (*games)[m_focusIndex];
        label = game.title;
        std::string info = game.productCode;
        if (!game.region.empty()) {
            info += (info.empty() ? "" : "  |  ") + std::string{"Region "} + game.region;
        }
        if (!game.releaseDate.empty()) {
            info += (info.empty() ? "" : "  |  ") + FormatDate(game.releaseDate);
        }
        description = info;
    } else if (m_focusKind == FocusKind::AddFolder && !model.biosMissing) {
        label = "Add Folder";
        description = "Choose a folder with your disc images; Ymir looks inside it and its subfolders.";
    } else {
        const std::span<const Option> options = OptionsFor(model);
        if (m_focusIndex < options.size()) {
            label = options[m_focusIndex].label;
            description = options[m_focusIndex].description;
        }
    }

    if (!label.empty()) {
        // Long game titles are shortened to the width of the grid
        const float maxWidth = ctx.layout.Scale(anchors.grid.size.x);
        Vec2 size = MeasureLabel(ctx, imguiData->fonts.display, 40.0f, label);
        while (size.x > maxWidth && label.size() > 4) {
            label = label.substr(0, label.size() - 4) + "...";
            size = MeasureLabel(ctx, imguiData->fonts.display, 40.0f, label);
        }
        DrawHoloLabelCentered(ctx, imguiData->fonts.display, 40.0f, ctx.layout.CanvasPoint(anchors.focusLabel), label);
    }
    if (!description.empty()) {
        const std::string fitted =
            FitDotText(description, ctx.layout.Scale(anchors.grid.size.x), ctx.layout.Scale(3.0f));
        DrawDotTextCentered(ctx, ctx.layout.CanvasPoint(anchors.focusDescription), 3.0f, fitted,
                            IM_COL32(150, 215, 255, 255));
    }
}

std::vector<ButtonHint> HomeScreen::Legend(const HomeModel &model) const {
    std::vector<ButtonHint> hints{{Glyph::DPad, "Navigate"}};
    if (m_focusKind == FocusKind::Game && !model.biosMissing) {
        hints.push_back({Glyph::A, "Play"});
    } else {
        hints.push_back({Glyph::A, "Select"});
    }
    if (!model.biosMissing) {
        hints.push_back({Glyph::X, "Add folder"});
        hints.push_back({Glyph::Y, "Rescan"});
    }
    if (model.sessionActive) {
        hints.push_back({Glyph::B, "Resume"});
    }
    return hints;
}

} // namespace platform::uwp::shell

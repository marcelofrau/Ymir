#include "overlay.hpp"

#include <app/imgui_data.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace platform::uwp::shell {

namespace {

    /// @brief Panel area on the reference canvas: the sky between the dashboards, leaving the legend visible.
    constexpr Rect kPanel{{300.0f, 178.0f}, {1320.0f, 670.0f}};

    constexpr float kRowHeight = 56.0f;
    constexpr float kRowGap = 6.0f;
    constexpr float kLabelPitch = 3.2f;

    constexpr ImU32 kHolo = IM_COL32(90, 220, 255, 255);

    ImU32 Alpha(ImU32 color, float alpha) {
        const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }

    Rect ToDisplay(const Layout &layout, Rect rect) {
        return {layout.CanvasPoint(rect.pos), {layout.Scale(rect.size.x), layout.Scale(rect.size.y)}};
    }

    bool IsEnabled(const OverlayRow &row) {
        return !row.enabled || row.enabled();
    }

    /// @brief Steps a choice or a slider, or flips a toggle. Returns whether the row reacts to left/right.
    bool Adjust(const OverlayRow &row, int direction) {
        switch (row.kind) {
        case OverlayRow::Kind::Toggle:
            if (row.getToggle && row.setToggle) {
                row.setToggle(!row.getToggle());
            }
            return true;
        case OverlayRow::Kind::Choice:
            if (row.getChoice && row.setChoice && !row.choices.empty()) {
                const int count = static_cast<int>(row.choices.size());
                row.setChoice(((row.getChoice() + direction) % count + count) % count);
            }
            return true;
        case OverlayRow::Kind::Slider:
            if (row.getValue && row.setValue) {
                const float value =
                    std::clamp(row.getValue() + row.step * static_cast<float>(direction), row.minValue, row.maxValue);
                row.setValue(value);
            }
            return true;
        default: return false;
        }
    }

} // namespace

// -----------------------------------------------------------------------------
// Rows

OverlayRow OverlayRow::Action(std::string label, std::string help, std::function<void()> activate,
                              std::function<std::string()> valueText) {
    OverlayRow row{};
    row.kind = Kind::Action;
    row.label = std::move(label);
    row.help = std::move(help);
    row.activate = std::move(activate);
    row.valueText = std::move(valueText);
    return row;
}

OverlayRow OverlayRow::Toggle(std::string label, std::string help, std::function<bool()> get,
                              std::function<void(bool)> set) {
    OverlayRow row{};
    row.kind = Kind::Toggle;
    row.label = std::move(label);
    row.help = std::move(help);
    row.getToggle = std::move(get);
    row.setToggle = std::move(set);
    return row;
}

OverlayRow OverlayRow::Choice(std::string label, std::string help, std::vector<std::string> choices,
                              std::function<int()> get, std::function<void(int)> set) {
    OverlayRow row{};
    row.kind = Kind::Choice;
    row.label = std::move(label);
    row.help = std::move(help);
    row.choices = std::move(choices);
    row.getChoice = std::move(get);
    row.setChoice = std::move(set);
    return row;
}

OverlayRow OverlayRow::Slider(std::string label, std::string help, float minValue, float maxValue, float step,
                              std::function<float()> get, std::function<void(float)> set,
                              std::function<std::string(float)> format) {
    OverlayRow row{};
    row.kind = Kind::Slider;
    row.label = std::move(label);
    row.help = std::move(help);
    row.minValue = minValue;
    row.maxValue = maxValue;
    row.step = step;
    row.getValue = std::move(get);
    row.setValue = std::move(set);
    row.formatValue = std::move(format);
    return row;
}

OverlayRow OverlayRow::Info(std::string label, std::function<std::string()> valueText, std::string help) {
    OverlayRow row{};
    row.kind = Kind::Info;
    row.label = std::move(label);
    row.help = std::move(help);
    row.valueText = std::move(valueText);
    return row;
}

// -----------------------------------------------------------------------------
// Stack

void Overlay::Push(OverlayScreen screen) {
    // While drawing, the stack must not grow under the state being drawn; the screen is opened after the frame
    if (m_drawing) {
        m_pendingPushes.push_back(std::move(screen));
        return;
    }
    State state{};
    state.screen = std::move(screen);
    m_stack.push_back(std::move(state));
    m_focusWindow = true;
}

void Overlay::Pop() {
    ++m_closeRequests;
}

void Overlay::Replace(OverlayScreen screen, bool resetFocus) {
    m_pendingReplace = std::move(screen);
    m_pendingReplaceResetFocus = resetFocus;
}

void Overlay::CloseAll() {
    if (m_drawing) {
        m_closeRequests = static_cast<int>(m_stack.size());
        m_pendingPushes.clear();
        return;
    }
    m_stack.clear();
    m_closeRequests = 0;
    m_hasFocus = false;
}

void Overlay::Draw(const DrawContext &ctx, bool dimBackground) {
    // Pops requested by row callbacks or by B on the previous frame
    while (m_closeRequests > 0 && !m_stack.empty()) {
        m_stack.pop_back();
        --m_closeRequests;
        m_focusWindow = true;
        if (!m_stack.empty()) {
            m_stack.back().focusRequested = true;
        }
    }
    m_closeRequests = 0;

    m_hasFocus = false;
    if (m_stack.empty()) {
        return;
    }

    // Callbacks may push a new screen while this one is drawn; the reference is taken by index so it survives
    const std::size_t top = m_stack.size() - 1;

    const Rect display = ctx.layout.Display();
    ImGui::SetNextWindowPos(ToImVec2(display.pos));
    ImGui::SetNextWindowSize(ToImVec2(display.size));
    if (m_focusWindow) {
        ImGui::SetNextWindowFocus();
        m_focusWindow = false;
        m_stack[top].focusRequested = true;
    }

    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

    m_drawing = true;
    if (ImGui::Begin("##ymir_overlay", nullptr, kFlags)) {
        const DrawContext octx{
            ImGui::GetWindowDrawList(), ctx.layout, ctx.theme, ctx.graphics, ctx.shapes, ctx.time, ctx.deltaTime};
        const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
        ImDrawList *dl = octx.drawList;

        if (dimBackground) {
            dl->AddRectFilled(ToImVec2(display.pos),
                              ImVec2(display.pos.x + display.size.x, display.pos.y + display.size.y),
                              IM_COL32(0, 4, 12, 150));
        }

        // The hologram panel
        const Rect panel = ToDisplay(ctx.layout, kPanel);
        const ImVec2 p0 = ToImVec2(panel.pos);
        const ImVec2 p1(panel.pos.x + panel.size.x, panel.pos.y + panel.size.y);
        const float line = std::max(1.0f, ctx.layout.Scale(1.5f));
        dl->AddRectFilled(p0, p1, IM_COL32(6, 18, 38, 222), ctx.layout.Scale(10.0f));
        dl->AddRect(p0, p1, Alpha(kHolo, 0.7f), ctx.layout.Scale(10.0f), 0, line);
        const float bracket = ctx.layout.Scale(34.0f);
        const std::array<ImVec2, 4> corners{p0, ImVec2(p1.x, p0.y), p1, ImVec2(p0.x, p1.y)};
        const std::array<ImVec2, 4> dirs{ImVec2(1, 1), ImVec2(-1, 1), ImVec2(-1, -1), ImVec2(1, -1)};
        for (int i = 0; i < 4; ++i) {
            dl->AddLine(corners[i], ImVec2(corners[i].x + dirs[i].x * bracket, corners[i].y), kHolo, line * 2.5f);
            dl->AddLine(corners[i], ImVec2(corners[i].x, corners[i].y + dirs[i].y * bracket), kHolo, line * 2.5f);
        }

        State &state = m_stack[top];
        state.page = std::min(state.page, state.screen.pages.empty() ? std::size_t{0} : state.screen.pages.size() - 1);

        // Header: screen title, then the page tabs with the LB/RB glyphs
        const float padX = ctx.layout.Scale(36.0f);
        DrawDotText(octx, {panel.pos.x + padX, panel.pos.y + ctx.layout.Scale(24.0f)}, 4.4f, state.screen.title,
                    IM_COL32(150, 230, 255, 255));
        if (state.screen.pages.size() > 1) {
            float x = panel.pos.x + padX;
            const float y = panel.pos.y + ctx.layout.Scale(70.0f);
            DrawGlyph(octx, Glyph::LB, {x + ctx.layout.Scale(24.0f), y + ctx.layout.Scale(10.0f)}, 14.0f);
            x += ctx.layout.Scale(58.0f);
            for (std::size_t i = 0; i < state.screen.pages.size(); ++i) {
                const std::string &title = state.screen.pages[i].title;
                const Vec2 size = MeasureDotText(octx, 2.8f, title);
                const float pad = ctx.layout.Scale(9.0f);
                const bool selected = i == state.page;
                if (selected) {
                    dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.6f),
                                      ImVec2(x + size.x + pad, y + size.y + pad * 0.6f), Alpha(kHolo, 0.22f),
                                      ctx.layout.Scale(4.0f));
                    dl->AddRect(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + size.x + pad, y + size.y + pad * 0.6f),
                                Alpha(kHolo, 0.8f), ctx.layout.Scale(4.0f), 0, line);
                }
                DrawDotText(octx, {x, y}, 2.8f, title,
                            selected ? IM_COL32(220, 250, 255, 255) : IM_COL32(110, 160, 190, 210));
                x += size.x + pad * 3.0f;
            }
            DrawGlyph(octx, Glyph::RB, {x + ctx.layout.Scale(8.0f), y + ctx.layout.Scale(10.0f)}, 14.0f);
        }

        // Rows, in a scrolling child window (or custom page renderer when provided)
        const float rowsTop = panel.pos.y + ctx.layout.Scale(state.screen.pages.size() > 1 ? 118.0f : 82.0f);
        const float helpHeight = ctx.layout.Scale(70.0f);
        const Rect rowsRect{{panel.pos.x + padX, rowsTop},
                            {panel.size.x - padX * 2.0f, panel.pos.y + panel.size.y - helpHeight - rowsTop}};
        if (!state.screen.pages.empty() && state.screen.pages[state.page].customDraw) {
            const Rect fullContent{
                {panel.pos.x + padX, rowsTop},
                {panel.size.x - padX * 2.0f, panel.pos.y + panel.size.y - ctx.layout.Scale(28.0f) - rowsTop}};
            state.screen.pages[state.page].customDraw(octx, fullContent);
            // Dummy nav item so the window keeps focus for LB/RB/B navigation
            ImGui::SetCursorScreenPos(ToImVec2(fullContent.pos));
            if (state.focusRequested) {
                ImGui::SetKeyboardFocusHere();
                state.focusRequested = false;
            }
            ImGui::InvisibleButton("##custom_page", ImVec2(1.0f, 1.0f), ImGuiButtonFlags_EnableNav);
        } else {
            ImGui::SetCursorScreenPos(ToImVec2(rowsRect.pos));
            if (ImGui::BeginChild("##rows", ToImVec2(rowsRect.size), ImGuiChildFlags_NavFlattened,
                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse)) {
                const DrawContext rctx{ImGui::GetWindowDrawList(),
                                       ctx.layout,
                                       ctx.theme,
                                       ctx.graphics,
                                       ctx.shapes,
                                       ctx.time,
                                       ctx.deltaTime};
                if (!state.screen.pages.empty()) {
                    // Copy the rows: a callback may replace the screen's pages while they are being drawn
                    const std::vector<OverlayRow> rows = state.screen.pages[state.page].rows;
                    if (state.rowFocus.size() != rows.size()) {
                        state.rowFocus.assign(rows.size(), 0.0f);
                    }
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    const float rowH = ctx.layout.Scale(kRowHeight);
                    const float gap = ctx.layout.Scale(kRowGap);
                    for (std::size_t i = 0; i < rows.size(); ++i) {
                        const Rect rect{{origin.x, origin.y + static_cast<float>(i) * (rowH + gap)},
                                        {rowsRect.size.x, rowH}};
                        ImGui::SetCursorScreenPos(ToImVec2(rect.pos));
                        if (state.focusRequested && i == std::min(state.focusedRow, rows.size() - 1)) {
                            ImGui::SetKeyboardFocusHere();
                            ImGui::SetScrollHereY(0.25f);
                            state.focusRequested = false;
                        }
                        ImGui::PushID(static_cast<int>(i));
                        const bool pressed =
                            ImGui::InvisibleButton("##row", ToImVec2(rect.size), ImGuiButtonFlags_EnableNav);
                        const bool focused = ImGui::IsItemFocused();
                        ImGui::PopID();
                        if (focused) {
                            if (state.focusedRow != i) {
                                state.focusedRow = i;
                                ImGui::SetScrollHereY();
                            }
                        }

                        DrawRow(rctx, state, i, rect, focused);

                        const OverlayRow &row = rows[i];
                        if (pressed && IsEnabled(row)) {
                            if (row.kind == OverlayRow::Kind::Action && row.activate) {
                                row.activate();
                            } else {
                                Adjust(row, +1);
                            }
                        }
                    }
                }
            }
            ImGui::EndChild();
        }

        // Help text of the focused row
        if (!m_stack.empty() && top < m_stack.size()) {
            State &live = m_stack[top];
            if (!live.screen.pages.empty() && live.focusedRow < live.screen.pages[live.page].rows.size()) {
                const std::string &help = live.screen.pages[live.page].rows[live.focusedRow].help;
                if (!help.empty()) {
                    ImFont *helpFont =
                        ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : imguiData->fonts.sansSerif.regular;
                    DrawLabel(octx, helpFont, 24.0f,
                              {panel.pos.x + padX, panel.pos.y + panel.size.y - helpHeight + ctx.layout.Scale(8.0f)},
                              IM_COL32(170, 205, 235, 255), help, panel.size.x - padX * 2.0f);
                }
            }
        }

        m_hasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (m_hasFocus && top < m_stack.size()) {
        HandleInput(m_stack[top]);
    }
    m_drawing = false;

    // A screen rebuilt by a row callback replaces the top one, on the same page and row when they still exist
    if (m_pendingReplace && !m_stack.empty()) {
        State &state = m_stack.back();
        state.screen = std::move(*m_pendingReplace);
        state.page = std::min(state.page, state.screen.pages.empty() ? std::size_t{0} : state.screen.pages.size() - 1);
        if (m_pendingReplaceResetFocus) {
            state.focusedRow = 0;
        }
        state.rowFocus.clear();
        state.focusRequested = true;
    }
    m_pendingReplace.reset();
    m_pendingReplaceResetFocus = false;

    // Screens opened by row callbacks during the frame
    for (OverlayScreen &screen : m_pendingPushes) {
        Push(std::move(screen));
    }
    m_pendingPushes.clear();
}

void Overlay::HandleInput(State &state) {
    // LB/RB switch pages
    if (state.screen.pages.size() > 1) {
        const int count = static_cast<int>(state.screen.pages.size());
        int page = static_cast<int>(state.page);
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
            page = (page - 1 + count) % count;
        } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
            page = (page + 1) % count;
        }
        if (page != static_cast<int>(state.page)) {
            state.page = static_cast<std::size_t>(page);
            state.focusedRow = 0;
            state.focusRequested = true;
            state.rowFocus.clear();
        }
    }

    // Left/right adjust the focused row
    if (!state.screen.pages.empty() && state.focusedRow < state.screen.pages[state.page].rows.size()) {
        const OverlayRow &row = state.screen.pages[state.page].rows[state.focusedRow];
        if (IsEnabled(row)) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, true)) {
                Adjust(row, -1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, true)) {
                Adjust(row, +1);
            }
        }
    }

    // B goes back
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        Pop();
    }
}

void Overlay::DrawRow(const DrawContext &ctx, State &state, std::size_t index, Rect rect, bool focused) {
    if (state.screen.pages.empty() || index >= state.screen.pages[state.page].rows.size()) {
        return;
    }
    const OverlayRow &row = state.screen.pages[state.page].rows[index];
    const bool enabled = IsEnabled(row);

    if (state.rowFocus.size() <= index) {
        state.rowFocus.resize(index + 1, 0.0f);
    }
    float &focus = state.rowFocus[index];
    focus += ((focused ? 1.0f : 0.0f) - focus) * (1.0f - std::exp(-14.0f * ctx.deltaTime));

    ImDrawList *dl = ctx.drawList;
    const ImVec2 p0 = ToImVec2(rect.pos);
    const ImVec2 p1(rect.pos.x + rect.size.x, rect.pos.y + rect.size.y);
    const float line = std::max(1.0f, ctx.layout.Scale(1.5f));

    // Highlight: a filled band with an accent bar on the left
    dl->AddRectFilled(p0, p1, Alpha(kHolo, 0.04f + 0.16f * focus), ctx.layout.Scale(6.0f));
    if (focus > 0.01f) {
        dl->AddRect(p0, p1, Alpha(kHolo, 0.7f * focus), ctx.layout.Scale(6.0f), 0, line);
        dl->AddRectFilled(p0, ImVec2(p0.x + ctx.layout.Scale(6.0f), p1.y), Alpha(kHolo, focus), ctx.layout.Scale(6.0f),
                          ImDrawFlags_RoundCornersLeft);
    }

    const float alpha = enabled ? 1.0f : 0.4f;
    const float midY = rect.pos.y + rect.size.y * 0.5f;
    float labelX = rect.pos.x + ctx.layout.Scale(24.0f);
    if (!row.icon.empty() && ctx.theme.GetIconFont() != nullptr) {
        DrawLabelCentered(ctx, ctx.theme.GetIconFont(), 22.0f, {rect.pos.x + ctx.layout.Scale(34.0f), midY},
                          Alpha(row.iconColor, alpha), row.icon);
        labelX = rect.pos.x + ctx.layout.Scale(64.0f);
    }
    if (row.useNerdFont && ctx.theme.GetFont() != nullptr) {
        const Vec2 labelSize = MeasureLabel(ctx, ctx.theme.GetFont(), 24.0f, row.label);
        DrawLabel(ctx, ctx.theme.GetFont(), 24.0f, {labelX, midY - labelSize.y * 0.5f},
                  Alpha(IM_COL32(225, 240, 255, 255), alpha), row.label);
    } else {
        const Vec2 labelSize = MeasureDotText(ctx, kLabelPitch, row.label);
        DrawDotText(ctx, {labelX, midY - labelSize.y * 0.5f}, kLabelPitch, row.label,
                    Alpha(IM_COL32(225, 240, 255, 255), alpha));
    }

    // Value on the right, by kind
    const float right = rect.pos.x + rect.size.x - ctx.layout.Scale(24.0f);
    const ImU32 valueColor = Alpha(IM_COL32(150, 230, 255, 255), alpha);
    const auto rightText = [&](const std::string &text, ImU32 color) {
        const Vec2 size = MeasureDotText(ctx, 2.8f, text);
        DrawDotText(ctx, {right - size.x, midY - size.y * 0.5f}, 2.8f, text, color);
        return size.x;
    };

    switch (row.kind) {
    case OverlayRow::Kind::Toggle: {
        const bool on = row.getToggle && row.getToggle();
        const float w = ctx.layout.Scale(76.0f);
        const float h = ctx.layout.Scale(32.0f);
        const ImVec2 t0(right - w, midY - h * 0.5f);
        const ImVec2 t1(right, midY + h * 0.5f);
        dl->AddRectFilled(
            t0, t1, on ? Alpha(IM_COL32(40, 200, 120, 255), alpha) : Alpha(IM_COL32(50, 60, 80, 255), alpha), h * 0.5f);
        const float knobX = on ? t1.x - h * 0.5f : t0.x + h * 0.5f;
        dl->AddCircleFilled(ImVec2(knobX, midY), h * 0.38f, Alpha(IM_COL32_WHITE, alpha), 20);
        const char *text = on ? "ON" : "OFF";
        const Vec2 size = MeasureDotText(ctx, 2.6f, text);
        DrawDotText(ctx, {t0.x - size.x - ctx.layout.Scale(14.0f), midY - size.y * 0.5f}, 2.6f, text, valueColor);
        break;
    }
    case OverlayRow::Kind::Choice: {
        std::string text = "-";
        if (row.getChoice && !row.choices.empty()) {
            const int choice = std::clamp(row.getChoice(), 0, static_cast<int>(row.choices.size()) - 1);
            text = row.choices[choice];
        }
        const float width = rightText(text, valueColor);
        // Arrows on both sides while focused, the cue that left/right changes the value
        if (focus > 0.01f) {
            const float arrow = ctx.layout.Scale(8.0f);
            const float gapX = ctx.layout.Scale(14.0f);
            const ImU32 color = Alpha(kHolo, focus * alpha);
            const float lx = right - width - gapX;
            dl->AddTriangleFilled(ImVec2(lx - arrow, midY), ImVec2(lx, midY - arrow), ImVec2(lx, midY + arrow), color);
            const float rx = right + gapX * 0.5f;
            dl->AddTriangleFilled(ImVec2(rx + arrow, midY), ImVec2(rx, midY - arrow), ImVec2(rx, midY + arrow), color);
        }
        break;
    }
    case OverlayRow::Kind::Slider: {
        const float value = row.getValue ? row.getValue() : row.minValue;
        const float t = row.maxValue > row.minValue ? (value - row.minValue) / (row.maxValue - row.minValue) : 0.0f;
        const float w = ctx.layout.Scale(300.0f);
        const float h = ctx.layout.Scale(10.0f);
        const ImVec2 b0(right - w, midY - h * 0.5f);
        const ImVec2 b1(right, midY + h * 0.5f);
        dl->AddRectFilled(b0, b1, Alpha(IM_COL32(40, 55, 80, 255), alpha), h * 0.5f);
        dl->AddRectFilled(b0, ImVec2(b0.x + w * std::clamp(t, 0.0f, 1.0f), b1.y), Alpha(kHolo, alpha), h * 0.5f);
        dl->AddCircleFilled(ImVec2(b0.x + w * std::clamp(t, 0.0f, 1.0f), midY), h * 1.1f, Alpha(IM_COL32_WHITE, alpha),
                            16);
        const std::string text = row.formatValue ? row.formatValue(value) : std::to_string(value);
        const Vec2 size = MeasureDotText(ctx, 2.8f, text);
        DrawDotText(ctx, {b0.x - size.x - ctx.layout.Scale(18.0f), midY - size.y * 0.5f}, 2.8f, text, valueColor);
        break;
    }
    case OverlayRow::Kind::Action: {
        // A chevron on the right, with the current value before it when there is one
        const float chevron = ctx.layout.Scale(9.0f);
        dl->AddTriangleFilled(ImVec2(right, midY), ImVec2(right - chevron, midY - chevron),
                              ImVec2(right - chevron, midY + chevron), Alpha(kHolo, (0.5f + 0.5f * focus) * alpha));
        if (row.valueText) {
            const std::string text = FitDotText(row.valueText(), rect.size.x * 0.55f, ctx.layout.Scale(2.8f));
            const Vec2 size = MeasureDotText(ctx, 2.8f, text);
            DrawDotText(ctx, {right - chevron - ctx.layout.Scale(16.0f) - size.x, midY - size.y * 0.5f}, 2.8f, text,
                        valueColor);
        }
        break;
    }
    case OverlayRow::Kind::Info: {
        if (row.valueText) {
            rightText(FitDotText(row.valueText(), rect.size.x * 0.6f, ctx.layout.Scale(2.8f)),
                      Alpha(IM_COL32(170, 200, 225, 255), alpha));
        }
        break;
    }
    }
}

std::vector<ButtonHint> Overlay::Legend() const {
    std::vector<ButtonHint> hints;
    if (!m_stack.empty()) {
        const State &state = m_stack.back();
        if (!state.screen.pages.empty() && state.focusedRow < state.screen.pages[state.page].rows.size()) {
            const OverlayRow &row = state.screen.pages[state.page].rows[state.focusedRow];
            switch (row.kind) {
            case OverlayRow::Kind::Action: hints.push_back({Glyph::A, "Select"}); break;
            case OverlayRow::Kind::Toggle: hints.push_back({Glyph::A, "Toggle"}); break;
            case OverlayRow::Kind::Choice:
            case OverlayRow::Kind::Slider: hints.push_back({Glyph::A, "Change"}); break;
            default: break;
            }
        }
        if (state.screen.pages.size() > 1) {
            hints.push_back({Glyph::RB, "Pages"});
        }
    }
    hints.push_back({Glyph::B, m_stack.size() > 1 ? "Back" : "Close"});
    return hints;
}

} // namespace platform::uwp::shell

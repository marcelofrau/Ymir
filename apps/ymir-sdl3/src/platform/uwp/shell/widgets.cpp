#include "widgets.hpp"

#include "dot_matrix.hpp"

#include <app/imgui_data.hpp>
#include <app/services/graphics_service.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <string>

namespace platform::uwp::shell {

namespace {

    /// @brief Linearly interpolates between two packed colors, including alpha.
    ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
        const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
        const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
        t = std::clamp(t, 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                                     ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
    }

    /// @brief Replaces the alpha of a packed color.
    ImU32 WithAlpha(ImU32 color, float alpha) {
        const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }

    /// @brief Converts a rectangle on the reference canvas to display coordinates.
    Rect CanvasRect(const DrawContext &ctx, Rect rect) {
        return {ctx.layout.CanvasPoint(rect.pos), {ctx.layout.Scale(rect.size.x), ctx.layout.Scale(rect.size.y)}};
    }

    /// @brief Dark teal that reads on the cyan bar of the top dashboard, like the lit segments of an LCD.
    constexpr ImU32 kLcdLitColor = IM_COL32(4, 58, 66, 255);

    /// @brief The unlit dots of the LCD, barely visible, so the bar reads as a dot-matrix display.
    constexpr ImU32 kLcdGhostColor = IM_COL32(0, 80, 90, 34);

    /// @brief Fraction of the pitch covered by a dot; the rest is the gap that makes the grid visible.
    constexpr float kDotFill = 0.78f;

    /// @brief Font size of the legend labels and glyph radius of the legend, in reference units.
    constexpr float kLegendFontSize = 26.0f;
    constexpr float kLegendGlyphRadius = 15.5f;

    /// @brief Font size of the subtle version text in the bottom-right corner, in reference units.
    constexpr float kVersionFontSize = 20.0f;

    /// @brief The dot-matrix grid of the top dashboard LCD, in display coordinates.
    struct LcdGrid {
        Vec2 origin;      // top-left corner of the first dot
        float pitch;      // distance between dots, in display pixels
        int columns;      // dots per row
        int rows;         // dots per column
        int textRow;      // vertical center row of the single-scale text
        int marqueeStart; // first column of the marquee
    };

    LcdGrid ComputeLcd(const DrawContext &ctx) {
        const ThemeLayout &anchors = ctx.theme.GetLayout();
        const float refPitch = std::max(anchors.lcdPitch, 1.0f);
        LcdGrid grid{};
        grid.columns = std::max(static_cast<int>(anchors.lcd.size.x / refPitch), 1);
        grid.rows = std::max(static_cast<int>(anchors.lcd.size.y / refPitch), 1);

        // Center the grid in the LCD rectangle, then snap it to whole display pixels so every dot is crisp
        const Vec2 refOrigin{anchors.lcd.pos.x + (anchors.lcd.size.x - grid.columns * refPitch) * 0.5f,
                             anchors.lcd.pos.y + (anchors.lcd.size.y - grid.rows * refPitch) * 0.5f};
        const Vec2 origin = ctx.layout.CanvasPoint(refOrigin);
        grid.origin = {std::round(origin.x), std::round(origin.y)};
        grid.pitch = ctx.layout.Scale(refPitch);

        grid.textRow = (grid.rows - dotmatrix::kGlyphHeight) / 2;
        const int titleColumns = dotmatrix::TextColumns("YMIR");
        grid.marqueeStart = 2 + titleColumns + 6;
        return grid;
    }

    /// @brief Lights one dot of the LCD grid.
    void PlotLcdDot(const DrawContext &ctx, const LcdGrid &grid, int column, int row, ImU32 color) {
        if (column < 0 || column >= grid.columns || row < 0 || row >= grid.rows) {
            return;
        }
        const float x = grid.origin.x + column * grid.pitch;
        const float y = grid.origin.y + row * grid.pitch;
        const float size = grid.pitch * kDotFill;
        ctx.drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size), color, size * 0.2f);
    }

    /// @brief Writes text on the LCD grid.
    /// @param[in] scale 1 for normal glyphs, 2 for double size
    /// @param[in] minColumn,maxColumn the columns the text may use; dots outside are not lit (the marquee clipping)
    void PlotLcdText(const DrawContext &ctx, const LcdGrid &grid, std::string_view text, int column, int row, int scale,
                     ImU32 color, int minColumn, int maxColumn) {
        for (const char32_t codePoint : dotmatrix::Decode(text)) {
            const dotmatrix::GlyphRows &glyph = dotmatrix::GlyphFor(codePoint);
            for (int gy = 0; gy < dotmatrix::kGlyphHeight; ++gy) {
                for (int gx = 0; gx < dotmatrix::kGlyphWidth; ++gx) {
                    if (!dotmatrix::IsLit(glyph, gx, gy)) {
                        continue;
                    }
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            const int c = column + gx * scale + sx;
                            if (c >= minColumn && c <= maxColumn) {
                                PlotLcdDot(ctx, grid, c, row + gy * scale + sy, color);
                            }
                        }
                    }
                }
            }
            column += dotmatrix::kAdvance * scale;
            if (column > maxColumn) {
                break;
            }
        }
    }

    /// @brief Draws a 9-slice panel: the corners keep their size, the edges stretch in one direction and the center
    /// stretches in both.
    void DrawNineSlice(const DrawContext &ctx, const ThemeAsset &asset, Rect rect, ImU32 tint) {
        const ImTextureID textureId = ctx.graphics.GetImGuiTextureID(asset.texture);

        // Texture-space margins, in pixels, and their size on screen. The margins are authored for the 1080p
        // reference, so they scale with the layout like everything else
        const float texW = asset.textureWidth;
        const float texH = asset.textureHeight;
        const std::array<float, 4> uvX{0.0f, asset.slice[0] / texW, 1.0f - asset.slice[2] / texW, 1.0f};
        const std::array<float, 4> uvY{0.0f, asset.slice[1] / texH, 1.0f - asset.slice[3] / texH, 1.0f};
        const std::array<float, 4> posX{rect.pos.x, rect.pos.x + ctx.layout.Scale(asset.slice[0]),
                                        rect.pos.x + rect.size.x - ctx.layout.Scale(asset.slice[2]),
                                        rect.pos.x + rect.size.x};
        const std::array<float, 4> posY{rect.pos.y, rect.pos.y + ctx.layout.Scale(asset.slice[1]),
                                        rect.pos.y + rect.size.y - ctx.layout.Scale(asset.slice[3]),
                                        rect.pos.y + rect.size.y};

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (posX[col + 1] <= posX[col] || posY[row + 1] <= posY[row]) {
                    continue;
                }
                ctx.drawList->AddImage(textureId, ImVec2(posX[col], posY[row]), ImVec2(posX[col + 1], posY[row + 1]),
                                       ImVec2(uvX[col], uvY[row]), ImVec2(uvX[col + 1], uvY[row + 1]), tint);
            }
        }
    }

    /// @brief Draws the focus halo of a procedural sphere: a ring in the Saturn's "mesh" transparency (a checkerboard
    /// of dots instead of real alpha blending) with pixel blocks orbiting around it.
    void DrawMeshHalo(const DrawContext &ctx, Vec2 center, float radius, ImU32 color, float focus, float spin) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ctx.time) * 4.0f);
        const ImU32 haloColor = LerpColor(color, IM_COL32_WHITE, 0.45f);

        const float cell = std::max(2.0f, std::round(ctx.layout.Scale(5.0f)));
        const float inner = radius * 1.06f;
        const float outer = radius * 1.32f;
        const int cells = static_cast<int>(std::ceil(outer / cell));
        const float dot = cell * 0.7f;
        for (int iy = -cells; iy <= cells; ++iy) {
            for (int ix = -cells; ix <= cells; ++ix) {
                if (((ix + iy) & 1) != 0) {
                    continue;
                }
                const float dx = ix * cell;
                const float dy = iy * cell;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance < inner || distance > outer) {
                    continue;
                }
                const float fade = 1.0f - (distance - inner) / (outer - inner);
                const float alpha = focus * (0.35f + 0.3f * pulse) * fade;
                const float x = std::round(center.x + dx);
                const float y = std::round(center.y + dy);
                ctx.drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + dot, y + dot), WithAlpha(haloColor, alpha));
            }
        }

        constexpr int kBlocks = 12;
        const float orbit = radius * 1.45f;
        const float block = std::round(ctx.layout.Scale(8.0f));
        for (int i = 0; i < kBlocks; ++i) {
            const float angle =
                -spin * 0.6f + static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / static_cast<float>(kBlocks);
            const float x = std::round(center.x + std::cos(angle) * orbit - block * 0.5f);
            const float y = std::round(center.y + std::sin(angle) * orbit - block * 0.5f);
            const float alpha = focus * (i % 3 == 0 ? 0.95f : 0.45f);
            ctx.drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + block, y + block), WithAlpha(haloColor, alpha));
        }
    }

} // namespace

// -----------------------------------------------------------------------------
// Backdrop and dashboards

void DrawSpaceBackground(const DrawContext &ctx, const Starfield &starfield, std::vector<ProjectedStar> &scratch) {
    const Rect display = ctx.layout.Display();
    const ImVec2 p0 = ToImVec2(display.pos);
    const ImVec2 p1(display.pos.x + display.size.x, display.pos.y + display.size.y);

    // The artwork already has stars of its own; the procedural ones stay faint while cruising so they do not clutter
    // it, and come out in full during a warp jump
    float starIntensity = 1.0f;
    if (const ThemeAsset *space = ctx.theme.Find("bg_space")) {
        DrawAsset(ctx, *space, display);
        const float warp = std::clamp((starfield.Speed() - Starfield::kCruiseSpeed) /
                                          (Starfield::kWarpSpeed - Starfield::kCruiseSpeed),
                                      0.0f, 1.0f);
        starIntensity = 0.3f + 0.7f * warp;
    } else {
        // Deep-space gradient: a hint of blue at the top fading to black, like the Saturn BIOS sky
        const ImU32 top = IM_COL32(10, 14, 38, 255);
        const ImU32 bottom = IM_COL32(0, 0, 6, 255);
        ctx.drawList->AddRectFilledMultiColor(p0, p1, top, top, bottom, bottom);
    }

    const float centerX = display.pos.x + display.size.x * 0.5f;
    const float centerY = display.pos.y + display.size.y * 0.42f;
    starfield.Project(centerX, centerY, display.size.x * 0.5f, scratch);

    const float scale = std::max(ctx.layout.ScaleFactor(), 0.5f);
    for (const ProjectedStar &star : scratch) {
        if (star.x < p0.x || star.x > p1.x || star.y < p0.y || star.y > p1.y) {
            continue;
        }
        const ImU32 color = IM_COL32(210, 225, 255, static_cast<int>(star.brightness * starIntensity * 255.0f));
        const float dx = star.x - star.prevX;
        const float dy = star.y - star.prevY;
        if (dx * dx + dy * dy > 2.25f) {
            // Long enough to read as a warp streak
            ctx.drawList->AddLine(ImVec2(star.prevX, star.prevY), ImVec2(star.x, star.y), color, star.radius * scale);
        } else {
            // At rest the stars are square pixels, in keeping with the rest of the shell
            const float half = star.radius * scale;
            ctx.drawList->AddRectFilled(ImVec2(std::round(star.x - half), std::round(star.y - half)),
                                        ImVec2(std::round(star.x + half), std::round(star.y + half)), color);
        }
    }
}

void DrawBottomDashboard(const DrawContext &ctx) {
    const ThemeLayout &anchors = ctx.theme.GetLayout();
    if (const ThemeAsset *dash = ctx.theme.Find("dash_bottom")) {
        // The artwork is authored at reference size, so its pixel size is its size on the reference canvas
        DrawAsset(ctx, *dash, CanvasRect(ctx, {anchors.dashBottom, {dash->FrameWidth(), dash->FrameHeight()}}));
        return;
    }

    // Placeholder: a trapezoid console seen from the pilot seat, with a light strip and indicator lamps
    const Rect rect = CanvasRect(ctx, {anchors.dashBottom, {Layout::kReferenceWidth, 405.0f}});
    const float x = rect.pos.x;
    const float y = rect.pos.y;
    const float w = rect.size.x;
    const float h = rect.size.y;
    const std::array<ImVec2, 4> shape{ImVec2(x + w * 0.06f, y), ImVec2(x + w * 0.94f, y), ImVec2(x + w, y + h),
                                      ImVec2(x, y + h)};
    ctx.drawList->AddConvexPolyFilled(shape.data(), static_cast<int>(shape.size()), IM_COL32(16, 24, 48, 215));
    ctx.drawList->AddPolyline(shape.data(), static_cast<int>(shape.size()), IM_COL32(90, 170, 255, 170),
                              ImDrawFlags_Closed, std::max(1.0f, ctx.layout.Scale(2.0f)));

    const float stripY = y + h * 0.12f;
    ctx.drawList->AddLine(ImVec2(x + w * 0.10f, stripY), ImVec2(x + w * 0.90f, stripY), IM_COL32(120, 200, 255, 120),
                          std::max(1.0f, ctx.layout.Scale(3.0f)));

    // Indicator lamps blink slowly, out of phase with each other
    constexpr int kLamps = 9;
    for (int i = 0; i < kLamps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kLamps);
        const float blink = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(static_cast<float>(ctx.time) * 1.7f + i * 1.3f));
        ctx.drawList->AddCircleFilled(ImVec2(x + w * (0.08f + t * 0.84f), y + h * 0.9f), ctx.layout.Scale(4.0f),
                                      WithAlpha(IM_COL32(255, 170, 60, 255), blink), 12);
    }
}

void DrawTopDashboard(const DrawContext &ctx, const Marquee &marquee) {
    const ThemeLayout &anchors = ctx.theme.GetLayout();

    if (const ThemeAsset *dash = ctx.theme.Find("dash_top")) {
        DrawAsset(ctx, *dash, CanvasRect(ctx, {anchors.dashTop, {dash->FrameWidth(), dash->FrameHeight()}}));
    } else {
        // Placeholder: a silver frame around a cyan bar, roughly where the artwork has them
        const Rect frame = CanvasRect(ctx, {anchors.dashTop, {1331.0f, 158.0f}});
        const Rect bar = CanvasRect(ctx, {{anchors.dashTop.x + 102.0f, anchors.dashTop.y + 28.0f}, {1124.0f, 99.0f}});
        ctx.drawList->AddRectFilled(ToImVec2(frame.pos), ImVec2(frame.pos.x + frame.size.x, frame.pos.y + frame.size.y),
                                    IM_COL32(170, 180, 200, 255), frame.size.y * 0.5f);
        ctx.drawList->AddRectFilled(ToImVec2(bar.pos), ImVec2(bar.pos.x + bar.size.x, bar.pos.y + bar.size.y),
                                    IM_COL32(40, 205, 200, 255), bar.size.y * 0.5f);
    }

    const LcdGrid grid = ComputeLcd(ctx);

    // Every dot of the display, unlit, so the bar reads as a dot-matrix LCD even where there is no text
    const float ghostSize = grid.pitch * kDotFill;
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            const float x = grid.origin.x + column * grid.pitch;
            const float y = grid.origin.y + row * grid.pitch;
            ctx.drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + ghostSize, y + ghostSize), kLcdGhostColor);
        }
    }

    const int lastColumn = grid.columns - 1;
    PlotLcdText(ctx, grid, "YMIR", 2, grid.textRow, 1, kLcdLitColor, 0, lastColumn);

    // A divider between the fixed title and the marquee
    const int dividerColumn = grid.marqueeStart - 3;
    for (int row = 2; row < grid.rows - 2; ++row) {
        PlotLcdDot(ctx, grid, dividerColumn, row, WithAlpha(kLcdLitColor, 0.55f));
    }

    // The marquee moves one whole dot column at a time, like a real LCD
    const std::string_view text = marquee.Current();
    if (!text.empty()) {
        const int column = grid.marqueeStart + static_cast<int>(std::floor(marquee.Offset()));
        PlotLcdText(ctx, grid, text, column, grid.textRow, 1, kLcdLitColor, grid.marqueeStart, lastColumn);
    }
}

int MarqueeColumns(const DrawContext &ctx) {
    const LcdGrid grid = ComputeLcd(ctx);
    return std::max(grid.columns - grid.marqueeStart, 1);
}

void DrawVersionLabel(const DrawContext &ctx, std::string_view version) {
    if (version.empty()) {
        return;
    }
    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    ImFont *font = ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : imguiData->fonts.monospace.regular;
    const Vec2 anchor = ctx.layout.CanvasPoint(ctx.theme.GetLayout().versionPos);
    const Vec2 size = MeasureLabel(ctx, font, kVersionFontSize, version);
    const Vec2 pos{anchor.x - size.x, anchor.y - size.y};
    const float shadow = std::max(1.0f, ctx.layout.Scale(1.0f));
    DrawLabel(ctx, font, kVersionFontSize, {pos.x + shadow, pos.y + shadow}, IM_COL32(0, 0, 0, 120), version);
    DrawLabel(ctx, font, kVersionFontSize, pos, IM_COL32(190, 210, 235, 135), version);
}

void DrawScanlines(const DrawContext &ctx) {
    const Rect display = ctx.layout.Display();
    const float step = std::max(2.0f, std::round(ctx.layout.Scale(3.0f)));
    const float thickness = std::max(1.0f, std::round(step / 3.0f));
    const float right = display.pos.x + display.size.x;
    const float bottom = display.pos.y + display.size.y;
    for (float y = display.pos.y; y < bottom; y += step) {
        ctx.drawList->AddRectFilled(ImVec2(display.pos.x, y), ImVec2(right, y + thickness), IM_COL32(0, 0, 0, 26));
    }
}

// -----------------------------------------------------------------------------
// Panels and text

void DrawAsset(const DrawContext &ctx, const ThemeAsset &asset, Rect rect, ImU32 tint) {
    if (asset.IsNineSlice()) {
        DrawNineSlice(ctx, asset, rect, tint);
        return;
    }
    float uv0[2];
    float uv1[2];
    asset.FrameUV(asset.FrameAt(ctx.time), uv0, uv1);
    ctx.drawList->AddImage(ctx.graphics.GetImGuiTextureID(asset.texture), ToImVec2(rect.pos),
                           ImVec2(rect.pos.x + rect.size.x, rect.pos.y + rect.size.y), ImVec2(uv0[0], uv0[1]),
                           ImVec2(uv1[0], uv1[1]), tint);
}

void DrawOverlayPanel(const DrawContext &ctx, Rect rect, std::string_view title) {
    const float rounding = ctx.layout.Scale(18.0f);
    const ImVec2 p0 = ToImVec2(rect.pos);
    const ImVec2 p1(rect.pos.x + rect.size.x, rect.pos.y + rect.size.y);
    ctx.drawList->AddRectFilled(p0, p1, IM_COL32(10, 16, 36, 225), rounding);
    ctx.drawList->AddRect(p0, p1, IM_COL32(90, 170, 255, 190), rounding, 0, std::max(1.0f, ctx.layout.Scale(2.0f)));

    if (!title.empty()) {
        DrawDotText(ctx, {rect.pos.x + ctx.layout.Scale(36.0f), rect.pos.y + ctx.layout.Scale(28.0f)}, 4.5f, title,
                    IM_COL32(120, 220, 255, 255));
    }
}

void DrawLabel(const DrawContext &ctx, ImFont *font, float refSize, Vec2 pos, ImU32 color, std::string_view text,
               float wrapWidth) {
    ctx.drawList->AddText(font, ctx.layout.Scale(refSize), ToImVec2(pos), color, text.data(), text.data() + text.size(),
                          wrapWidth);
}

void DrawLabelCentered(const DrawContext &ctx, ImFont *font, float refSize, Vec2 center, ImU32 color,
                       std::string_view text) {
    const Vec2 size = MeasureLabel(ctx, font, refSize, text);
    DrawLabel(ctx, font, refSize, {center.x - size.x * 0.5f, center.y - size.y * 0.5f}, color, text);
}

Vec2 MeasureLabel(const DrawContext &ctx, ImFont *font, float refSize, std::string_view text) {
    const ImVec2 size =
        font->CalcTextSizeA(ctx.layout.Scale(refSize), FLT_MAX, 0.0f, text.data(), text.data() + text.size());
    return {size.x, size.y};
}

void DrawHoloLabelCentered(const DrawContext &ctx, ImFont *font, float refSize, Vec2 center, std::string_view text) {
    // The split drifts a little, like the convergence of a CRT that is not quite aligned
    const float split = ctx.layout.Scale(2.4f) * (0.7f + 0.3f * std::sin(static_cast<float>(ctx.time) * 2.7f));
    DrawLabelCentered(ctx, font, refSize, {center.x - split, center.y}, IM_COL32(0, 230, 255, 120), text);
    DrawLabelCentered(ctx, font, refSize, {center.x + split, center.y}, IM_COL32(255, 60, 200, 105), text);
    DrawLabelCentered(ctx, font, refSize, center, IM_COL32(245, 250, 255, 255), text);
}

void DrawDotText(const DrawContext &ctx, Vec2 pos, float refPitch, std::string_view text, ImU32 color) {
    const float pitch = ctx.layout.Scale(refPitch);
    const float size = std::max(1.0f, pitch * kDotFill);
    float x = std::round(pos.x);
    const float y = std::round(pos.y);
    for (const char32_t codePoint : dotmatrix::Decode(text)) {
        const dotmatrix::GlyphRows &glyph = dotmatrix::GlyphFor(codePoint);
        for (int gy = 0; gy < dotmatrix::kGlyphHeight; ++gy) {
            for (int gx = 0; gx < dotmatrix::kGlyphWidth; ++gx) {
                if (dotmatrix::IsLit(glyph, gx, gy)) {
                    const float dx = x + gx * pitch;
                    const float dy = y + gy * pitch;
                    ctx.drawList->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + size, dy + size), color);
                }
            }
        }
        x += dotmatrix::kAdvance * pitch;
    }
}

void DrawDotTextCentered(const DrawContext &ctx, Vec2 center, float refPitch, std::string_view text, ImU32 color) {
    const Vec2 size = MeasureDotText(ctx, refPitch, text);
    DrawDotText(ctx, {center.x - size.x * 0.5f, center.y - size.y * 0.5f}, refPitch, text, color);
}

Vec2 MeasureDotText(const DrawContext &ctx, float refPitch, std::string_view text) {
    const float pitch = ctx.layout.Scale(refPitch);
    return {static_cast<float>(dotmatrix::TextColumns(text)) * pitch,
            static_cast<float>(dotmatrix::kGlyphHeight) * pitch};
}

// -----------------------------------------------------------------------------
// Controller legend

namespace {

    /// @brief Half the width of a glyph, in display pixels. Bumpers are wider than the round buttons.
    float GlyphHalfWidth(const DrawContext &ctx, Glyph glyph, float refRadius) {
        const float radius = ctx.layout.Scale(refRadius);
        return glyph == Glyph::LB || glyph == Glyph::RB ? radius * 1.55f : radius;
    }

    /// @brief Draws the round body every controller glyph shares: a disc with a darker rim and a glossy highlight.
    void DrawButtonBody(const DrawContext &ctx, Vec2 center, float radius, ImU32 color) {
        ctx.drawList->AddCircleFilled(ToImVec2(center), radius, LerpColor(color, IM_COL32_BLACK, 0.35f), 40);
        ctx.drawList->AddCircleFilled(ToImVec2(center), radius * 0.86f, color, 40);
        ctx.drawList->AddCircleFilled(ImVec2(center.x - radius * 0.28f, center.y - radius * 0.34f), radius * 0.3f,
                                      IM_COL32(255, 255, 255, 60), 20);
    }

} // namespace

void DrawGlyph(const DrawContext &ctx, Glyph glyph, Vec2 center, float refRadius) {
    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    ImFont *letterFont = imguiData->fonts.sansSerif.bold;
    const float radius = ctx.layout.Scale(refRadius);
    const float line = std::max(1.0f, ctx.layout.Scale(refRadius * 0.11f));
    constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 kDark = IM_COL32(52, 58, 72, 255);

    // Face buttons: the Xbox colors with the letter on top
    const auto faceButton = [&](ImU32 color, const char *letter) {
        DrawButtonBody(ctx, center, radius, color);
        DrawLabelCentered(ctx, letterFont, refRadius * 1.25f, {center.x, center.y + radius * 0.02f}, kWhite, letter);
    };

    switch (glyph) {
    case Glyph::A: faceButton(IM_COL32(16, 160, 64, 255), "A"); break;
    case Glyph::B: faceButton(IM_COL32(222, 36, 48, 255), "B"); break;
    case Glyph::X: faceButton(IM_COL32(16, 112, 220, 255), "X"); break;
    case Glyph::Y: faceButton(IM_COL32(240, 176, 0, 255), "Y"); break;

    case Glyph::LB:
    case Glyph::RB: {
        // A bumper: a wide, flat shape with a rounder outer top corner, like the shoulder buttons seen from above
        const bool left = glyph == Glyph::LB;
        const float halfW = GlyphHalfWidth(ctx, glyph, refRadius);
        const float halfH = radius * 0.72f;
        const ImVec2 p0(center.x - halfW, center.y - halfH);
        const ImVec2 p1(center.x + halfW, center.y + halfH);
        const ImDrawFlags corners = left ? ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight |
                                               ImDrawFlags_RoundCornersBottomLeft
                                         : ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomLeft |
                                               ImDrawFlags_RoundCornersBottomRight;
        ctx.drawList->AddRectFilled(p0, p1, LerpColor(kDark, IM_COL32_BLACK, 0.3f), halfH, corners);
        ctx.drawList->AddRectFilled(ImVec2(p0.x + line, p0.y + line), ImVec2(p1.x - line, p1.y - line), kDark,
                                    halfH - line, corners);
        DrawLabelCentered(ctx, letterFont, refRadius * 0.95f, center, kWhite, left ? "LB" : "RB");
        break;
    }

    case Glyph::View: {
        // Two overlapping windows, the icon printed on the View button
        DrawButtonBody(ctx, center, radius, kDark);
        const float w = radius * 0.62f;
        const float h = radius * 0.44f;
        const float off = radius * 0.16f;
        ctx.drawList->AddRect(ImVec2(center.x - w * 0.5f - off, center.y - h * 0.5f - off),
                              ImVec2(center.x + w * 0.5f - off, center.y + h * 0.5f - off), kWhite, line, 0, line);
        ctx.drawList->AddRectFilled(ImVec2(center.x - w * 0.5f + off, center.y - h * 0.5f + off),
                                    ImVec2(center.x + w * 0.5f + off, center.y + h * 0.5f + off), kWhite, line);
        break;
    }

    case Glyph::Menu: {
        // Three lines, the icon printed on the Menu button
        DrawButtonBody(ctx, center, radius, kDark);
        const float w = radius * 0.5f;
        for (int i = -1; i <= 1; ++i) {
            const float y = center.y + i * radius * 0.26f;
            ctx.drawList->AddLine(ImVec2(center.x - w, y), ImVec2(center.x + w, y), kWhite, line * 1.2f);
        }
        break;
    }

    case Glyph::DPad: {
        // A plus-shaped pad with a darker hub and a highlight on the upper arm
        const float arm = radius * 0.98f;
        const float half = radius * 0.36f;
        ctx.drawList->AddRectFilled(ImVec2(center.x - arm, center.y - half), ImVec2(center.x + arm, center.y + half),
                                    kDark, half * 0.35f);
        ctx.drawList->AddRectFilled(ImVec2(center.x - half, center.y - arm), ImVec2(center.x + half, center.y + arm),
                                    kDark, half * 0.35f);
        ctx.drawList->AddCircleFilled(ToImVec2(center), half * 0.55f, LerpColor(kDark, IM_COL32_BLACK, 0.45f), 16);
        ctx.drawList->AddTriangleFilled(
            ImVec2(center.x, center.y - arm * 0.82f), ImVec2(center.x - half * 0.5f, center.y - arm * 0.45f),
            ImVec2(center.x + half * 0.5f, center.y - arm * 0.45f), IM_COL32(255, 255, 255, 150));
        break;
    }
    }
}

void DrawLegend(const DrawContext &ctx, std::span<const ButtonHint> hints) {
    if (hints.empty()) {
        return;
    }

    const app::YmirImGuiData *imguiData = app::GetYmirImGuiData();
    ImFont *legendFont = ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : imguiData->fonts.monospace.regular;

    const float glyphLabelGap = ctx.layout.Scale(10.0f);
    const float hintGap = ctx.layout.Scale(32.0f);

    // Measure everything first so the legend can be centered on its anchor
    float totalWidth = 0.0f;
    float labelHeight = 0.0f;
    for (const ButtonHint &hint : hints) {
        const Vec2 labelSize = MeasureLabel(ctx, legendFont, kLegendFontSize, hint.label);
        totalWidth += GlyphHalfWidth(ctx, hint.glyph, kLegendGlyphRadius) * 2.0f + glyphLabelGap + labelSize.x;
        labelHeight = std::max(labelHeight, labelSize.y);
    }
    totalWidth += hintGap * static_cast<float>(hints.size() - 1);

    // A dark pill behind the legend keeps it readable over the bright cockpit floor
    const Vec2 center = ctx.layout.CanvasPoint(ctx.theme.GetLayout().legend);
    const float glyphRadius = ctx.layout.Scale(kLegendGlyphRadius);
    const float padX = ctx.layout.Scale(24.0f);
    const float halfH = std::max(glyphRadius, labelHeight * 0.5f) + ctx.layout.Scale(8.0f);
    const ImVec2 p0(center.x - totalWidth * 0.5f - padX, center.y - halfH);
    const ImVec2 p1(center.x + totalWidth * 0.5f + padX, center.y + halfH);
    ctx.drawList->AddRectFilled(p0, p1, IM_COL32(8, 12, 28, 190), halfH);
    ctx.drawList->AddRect(p0, p1, IM_COL32(90, 200, 230, 110), halfH, 0, std::max(1.0f, ctx.layout.Scale(1.5f)));

    float x = center.x - totalWidth * 0.5f;
    for (const ButtonHint &hint : hints) {
        const float halfWidth = GlyphHalfWidth(ctx, hint.glyph, kLegendGlyphRadius);
        DrawGlyph(ctx, hint.glyph, {x + halfWidth, center.y}, kLegendGlyphRadius);
        x += halfWidth * 2.0f + glyphLabelGap;

        const Vec2 labelSize = MeasureLabel(ctx, legendFont, kLegendFontSize, hint.label);
        DrawLabel(ctx, legendFont, kLegendFontSize, {x, center.y - labelSize.y * 0.5f}, IM_COL32(225, 238, 255, 255),
                  hint.label);
        x += labelSize.x + hintGap;
    }
}

// -----------------------------------------------------------------------------
// 3D shapes

void DrawFlatShaded(const DrawContext &ctx, std::span<const ProjectedQuad> quads, ImU32 color, bool drawEdges) {
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(color);
    const float edgeThickness = std::max(1.0f, ctx.layout.Scale(1.0f));
    for (const ProjectedQuad &quad : quads) {
        // One flat color per quad. The brightest bands bleach towards white, the specular highlight of 90s
        // flat-shaded polygons
        const float shade = 0.16f + 0.92f * quad.light;
        const float bleach = std::max(0.0f, quad.light - 0.82f) * 2.4f;
        const ImVec4 fill(std::min(base.x * shade + bleach, 1.0f), std::min(base.y * shade + bleach, 1.0f),
                          std::min(base.z * shade + bleach, 1.0f), base.w);
        const ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(fill);
        const std::array<ImVec2, 4> points{ImVec2(quad.x[0], quad.y[0]), ImVec2(quad.x[1], quad.y[1]),
                                           ImVec2(quad.x[2], quad.y[2]), ImVec2(quad.x[3], quad.y[3])};
        ctx.drawList->AddConvexPolyFilled(points.data(), 4, fillColor);

        if (drawEdges) {
            // Thin darker edges show the polygon structure
            const ImVec4 edge(fill.x * 0.55f, fill.y * 0.55f, fill.z * 0.55f, 0.5f * base.w);
            ctx.drawList->AddPolyline(points.data(), 4, ImGui::ColorConvertFloat4ToU32(edge), ImDrawFlags_Closed,
                                      edgeThickness);
        } else {
            // Seal anti-aliased seams between adjacent quads of the same face without visible stroke lines
            ctx.drawList->AddPolyline(points.data(), 4, fillColor, ImDrawFlags_Closed, 1.0f);
        }
    }
}

namespace {

    /// @brief Hologram cyan.
    constexpr ImU32 kHoloColor = IM_COL32(90, 220, 255, 255);

    /// @brief Shared behavior of the hologram tiles: the ImGui item, the focus easing, the lift and the panel.
    /// @param[out] pressed receives whether the tile was activated
    /// @return the drawn rectangle, lifted while focused
    Rect HologramFrame(const DrawContext &ctx, const char *id, Rect rect, int seed, TileState &state, bool &pressed) {
        ImGui::SetCursorScreenPos(ToImVec2(rect.pos));
        pressed = ImGui::InvisibleButton(id, ToImVec2(rect.size), ImGuiButtonFlags_EnableNav);
        const bool focused = ImGui::IsItemFocused();

        constexpr float kFocusRate = 12.0f;
        state.focus += ((focused ? 1.0f : 0.0f) - state.focus) * (1.0f - std::exp(-kFocusRate * ctx.deltaTime));

        // The focused tile lifts towards the viewer
        const float grow = state.focus * 0.06f;
        const Rect drawn{{rect.pos.x - rect.size.x * grow * 0.5f, rect.pos.y - rect.size.y * grow * 0.5f},
                         {rect.size.x * (1.0f + grow), rect.size.y * (1.0f + grow)}};

        // A hologram is never perfectly stable: a faint fast flicker, desynchronized between tiles
        const float time = static_cast<float>(ctx.time);
        const float flicker = 0.9f + 0.1f * std::sin(time * 23.0f + static_cast<float>(seed) * 1.7f);
        const float alpha = (0.55f + 0.45f * state.focus) * flicker;

        const ImVec2 p0 = ToImVec2(drawn.pos);
        const ImVec2 p1(drawn.pos.x + drawn.size.x, drawn.pos.y + drawn.size.y);
        const float rounding = ctx.layout.Scale(6.0f);
        const float line = std::max(1.0f, ctx.layout.Scale(1.5f));

        // Glow around the focused tile
        for (int i = 3; i >= 1; --i) {
            const float pad = ctx.layout.Scale(4.0f * static_cast<float>(i));
            ctx.drawList->AddRect(ImVec2(p0.x - pad, p0.y - pad), ImVec2(p1.x + pad, p1.y + pad),
                                  WithAlpha(kHoloColor, state.focus * 0.12f * static_cast<float>(4 - i)),
                                  rounding + pad, 0, line * 2.0f);
        }
        ctx.drawList->AddRectFilled(p0, p1, WithAlpha(IM_COL32(20, 90, 130, 255), 0.22f + 0.18f * state.focus),
                                    rounding);
        ctx.drawList->AddRect(p0, p1, WithAlpha(kHoloColor, alpha * 0.8f), rounding, 0, line);

        // Corner brackets, the high-tech touch
        const float bracket = std::min(drawn.size.x, drawn.size.y) * 0.16f;
        const ImU32 bracketColor = WithAlpha(LerpColor(kHoloColor, IM_COL32_WHITE, state.focus * 0.6f), alpha);
        const float thick = line * 2.0f;
        const std::array<ImVec2, 4> corners{p0, ImVec2(p1.x, p0.y), p1, ImVec2(p0.x, p1.y)};
        const std::array<ImVec2, 4> dirs{ImVec2(1, 1), ImVec2(-1, 1), ImVec2(-1, -1), ImVec2(1, -1)};
        for (int i = 0; i < 4; ++i) {
            const ImVec2 c = corners[i];
            ctx.drawList->AddLine(c, ImVec2(c.x + dirs[i].x * bracket, c.y), bracketColor, thick);
            ctx.drawList->AddLine(c, ImVec2(c.x, c.y + dirs[i].y * bracket), bracketColor, thick);
        }
        return drawn;
    }

    /// @brief Holographic scanlines over a rectangle, drifting slowly downwards.
    void DrawHoloScanlines(const DrawContext &ctx, Rect rect, float strength) {
        const float step = std::max(2.0f, std::round(ctx.layout.Scale(4.0f)));
        const float drift = std::fmod(static_cast<float>(ctx.time) * ctx.layout.Scale(18.0f), step);
        const float bottom = rect.pos.y + rect.size.y;
        for (float y = rect.pos.y + drift; y < bottom; y += step) {
            ctx.drawList->AddLine(ImVec2(rect.pos.x, y), ImVec2(rect.pos.x + rect.size.x, y),
                                  WithAlpha(kHoloColor, 0.07f * strength), 1.0f);
        }
    }

    /// @brief Appends a code point to a UTF-8 string.
    void AppendUtf8(std::string &out, char32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

} // namespace

std::string FitDotText(std::string_view text, float maxWidth, float pitch) {
    const int maxChars = std::max(1, static_cast<int>((maxWidth / std::max(pitch, 0.1f) + 1.0f) / dotmatrix::kAdvance));
    const std::u32string decoded = dotmatrix::Decode(text);
    if (static_cast<int>(decoded.size()) <= maxChars) {
        return std::string{text};
    }
    std::string out;
    for (int i = 0; i < std::max(maxChars - 3, 1); ++i) {
        AppendUtf8(out, decoded[i]);
    }
    out += "...";
    return out;
}

bool HologramTile(const DrawContext &ctx, const char *id, Rect rect, std::string_view title,
                  std::string_view productCode, const ThemeAsset *cover, int seed, TileState &state) {
    bool pressed = false;
    const Rect drawn = HologramFrame(ctx, id, rect, seed, state, pressed);

    // Cover area on top, title strip below it
    const float pad = ctx.layout.Scale(8.0f);
    const float titleHeight = ctx.layout.Scale(30.0f);
    const Rect art{{drawn.pos.x + pad, drawn.pos.y + pad},
                   {drawn.size.x - pad * 2.0f, drawn.size.y - pad * 2.0f - titleHeight}};

    if (cover != nullptr) {
        // Fit the cover in the art area, keeping its aspect ratio
        const float scale = std::min(art.size.x / cover->FrameWidth(), art.size.y / cover->FrameHeight());
        const Vec2 size{cover->FrameWidth() * scale, cover->FrameHeight() * scale};
        const Rect fitted{{art.pos.x + (art.size.x - size.x) * 0.5f, art.pos.y + (art.size.y - size.y) * 0.5f}, size};
        const ImU32 tint = LerpColor(IM_COL32(150, 225, 255, 175), IM_COL32_WHITE, state.focus);
        DrawAsset(ctx, *cover, fitted, tint);
    } else {
        // No cover: the product number inside a disc outline, like a label read by the drive
        const std::string code = productCode.empty() ? std::string{"SEGA SATURN"} : std::string{productCode};
        const Vec2 center{art.pos.x + art.size.x * 0.5f, art.pos.y + art.size.y * 0.45f};
        const std::string fitted = FitDotText(code, art.size.x - pad * 2.0f, ctx.layout.Scale(3.0f));
        DrawDotTextCentered(ctx, center, 3.0f, fitted, WithAlpha(kHoloColor, 0.85f + 0.15f * state.focus));
        const float disc = std::min(art.size.x, art.size.y) * 0.44f;
        ctx.drawList->AddCircle(ToImVec2(center), disc, WithAlpha(kHoloColor, 0.35f), 48,
                                std::max(1.0f, ctx.layout.Scale(1.5f)));
        ctx.drawList->AddCircle(ToImVec2(center), disc * 0.18f, WithAlpha(kHoloColor, 0.35f), 24,
                                std::max(1.0f, ctx.layout.Scale(1.5f)));
    }
    DrawHoloScanlines(ctx, art, 1.0f - state.focus * 0.6f);

    // Title in 3270 Nerd Font (size 16), with horizontal marquee when the tile is focused and the title overflows
    if (state.focus > 0.5f) {
        state.marqueeTime += ctx.deltaTime;
    } else {
        state.marqueeTime = 0.0f;
    }
    ImFont *titleFont = ctx.theme.GetFont() != nullptr ? ctx.theme.GetFont() : ImGui::GetFont();
    constexpr float kTitleFontSize = 16.0f;
    const float availWidth = std::max(1.0f, drawn.size.x - pad * 2.0f);
    const Vec2 textSize = MeasureLabel(ctx, titleFont, kTitleFontSize, title);
    const float titleCenterY = drawn.pos.y + drawn.size.y - pad * 0.7f - titleHeight * 0.5f;
    const float textY = titleCenterY - textSize.y * 0.5f;
    const ImU32 textColor = LerpColor(IM_COL32(170, 225, 255, 230), IM_COL32(255, 255, 255, 255), state.focus);

    ctx.drawList->PushClipRect(ImVec2(drawn.pos.x + pad, drawn.pos.y + drawn.size.y - pad - titleHeight),
                               ImVec2(drawn.pos.x + drawn.size.x - pad, drawn.pos.y + drawn.size.y), true);
    if (textSize.x <= availWidth) {
        DrawLabel(ctx, titleFont, kTitleFontSize,
                  {drawn.pos.x + (drawn.size.x - textSize.x) * 0.5f, textY}, textColor, title);
    } else if (state.focus > 0.5f) {
        const float gap = ctx.layout.Scale(36.0f);
        const float cycle = textSize.x + gap;
        const float scrollSpeed = ctx.layout.Scale(58.0f);
        const float activeTime = std::max(0.0f, state.marqueeTime - 0.45f);
        const float offset = std::fmod(activeTime * scrollSpeed, cycle);
        const float startX = drawn.pos.x + pad - offset;
        DrawLabel(ctx, titleFont, kTitleFontSize, {startX, textY}, textColor, title);
        DrawLabel(ctx, titleFont, kTitleFontSize, {startX + cycle, textY}, textColor, title);
    } else {
        DrawLabel(ctx, titleFont, kTitleFontSize, {drawn.pos.x + pad, textY}, textColor, title);
    }
    ctx.drawList->PopClipRect();
    return pressed;
}

bool HologramActionTile(const DrawContext &ctx, const char *id, Rect rect, std::string_view symbol,
                        std::string_view label, TileState &state) {
    bool pressed = false;
    const Rect drawn = HologramFrame(ctx, id, rect, 0, state, pressed);
    const Vec2 center{drawn.pos.x + drawn.size.x * 0.5f, drawn.pos.y + drawn.size.y * 0.42f};
    DrawDotTextCentered(ctx, center, 9.0f, symbol, WithAlpha(kHoloColor, 0.8f + 0.2f * state.focus));
    DrawDotTextCentered(ctx, {center.x, drawn.pos.y + drawn.size.y * 0.8f}, 2.6f, label,
                        LerpColor(IM_COL32(170, 225, 255, 230), IM_COL32(255, 255, 255, 255), state.focus));
    DrawHoloScanlines(ctx, drawn, 1.0f - state.focus * 0.6f);
    return pressed;
}

void DrawAudioCube(const DrawContext &ctx, const PodAnchor &pod, const AudioCube &cube, ImU32 color, float phase) {
    const float time = static_cast<float>(ctx.time);
    const float podRadius = ctx.layout.Scale(pod.radius);
    const Vec2 podCenter = ctx.layout.CanvasPoint(pod.center);

    // Hover above the middle of the pod, bobbing and bouncing moderately with the audio
    const float level = cube.Level();
    const float wobble = cube.Wobble();
    const float bounce = std::sin(time * 7.0f + phase) * wobble * podRadius * 0.09f - level * podRadius * 0.05f;
    const Vec2 center{podCenter.x,
                      podCenter.y - podRadius * 0.12f + std::sin(time * 1.3f + phase) * podRadius * 0.05f + bounce};
    const float radius = podRadius * 0.48f * cube.Scale();

    // A glow on the pod under the cube, brighter with the music, and a soft blurred contact shadow
    for (int i = 0; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        ctx.drawList->AddCircleFilled(ToImVec2(center), radius * (1.15f + t * 0.55f),
                                      WithAlpha(color, (0.10f + 0.28f * level) * (1.0f - t)), 40);
    }
    const ImVec2 shadowCenter(podCenter.x, podCenter.y + podRadius * 0.55f);
    constexpr int kShadowLayers = 10;
    for (int i = 0; i < kShadowLayers; ++i) {
        const float t = 1.0f - static_cast<float>(i) / static_cast<float>(kShadowLayers);
        const float spread = 0.25f + 1.15f * t;
        const int layerAlpha = static_cast<int>(18.0f * (1.0f - t * 0.65f));
        ctx.drawList->AddEllipseFilled(shadowCenter, ImVec2(radius * 0.95f * spread, radius * 0.24f * spread),
                                       IM_COL32(0, 0, 0, layerAlpha), 0.0f, 32);
    }

    ShapeCache &shapes = ctx.shapes;
    ShapeMesh(shapes.cubeMesh, cube.Morph(), radius, shapes.positions);
    DeformMesh(shapes.positions, wobble, time + phase);
    MeshView view{};
    view.yaw = cube.Spin() + phase;
    view.pitch = 0.55f + 0.15f * std::sin(time * 0.7f + phase) + 0.18f * wobble * std::cos(time * 5.0f + phase);
    view.centerX = center.x;
    view.centerY = center.y;
    view.cameraDistance = 4.5f;
    view.lightLevels = 6;
    ProjectMesh(shapes.cubeMesh, shapes.positions, radius, view, shapes.quads);
    DrawFlatShaded(ctx, shapes.quads, color, false);
}

bool SphereButton(const DrawContext &ctx, const char *id, std::string_view assetKey, std::string_view iconGlyph,
                  Vec2 center, float refRadius, ImU32 color, SphereState &state) {
    const float idleRadius = ctx.layout.Scale(refRadius);
    const float hitHalf = idleRadius * 1.3f;

    ImGui::SetCursorScreenPos(ImVec2(center.x - hitHalf, center.y - hitHalf));
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(hitHalf * 2.0f, hitHalf * 2.0f), ImGuiButtonFlags_EnableNav);
    const bool focused = ImGui::IsItemFocused();

    // Ease the focus animation in and out; the rate makes the transition take about 150 ms
    constexpr float kFocusRate = 12.0f;
    const float blend = 1.0f - std::exp(-kFocusRate * ctx.deltaTime);
    state.focus += ((focused ? 1.0f : 0.0f) - state.focus) * blend;

    // Idle spheres turn slowly; the focused one spins up, like the selected option in the Saturn BIOS
    state.spin += ctx.deltaTime * (0.35f + 2.4f * state.focus);

    const float radius = idleRadius * (1.0f + 0.22f * state.focus);

    // Focus halo behind the sphere
    if (state.focus > 0.01f) {
        if (const ThemeAsset *glow = ctx.theme.Find("sphere_glow")) {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ctx.time) * 4.0f);
            const float glowRadius = radius * 1.6f;
            DrawAsset(ctx, *glow,
                      {{center.x - glowRadius, center.y - glowRadius}, {glowRadius * 2.0f, glowRadius * 2.0f}},
                      WithAlpha(IM_COL32_WHITE, state.focus * (0.75f + 0.25f * pulse)));
        } else {
            DrawMeshHalo(ctx, center, radius, color, state.focus, state.spin);
        }
    }

    const std::string sphereId = "sphere_" + std::string{assetKey};
    if (const ThemeAsset *sphere = ctx.theme.Find(sphereId)) {
        // Sprite sheet spheres only play while focused
        const ThemeAsset *drawn = sphere;
        ThemeAsset still;
        if (!focused && sphere->frames > 1) {
            still = *sphere;
            still.fps = 0.0f;
            drawn = &still;
        }
        DrawAsset(ctx, *drawn, {{center.x - radius, center.y - radius}, {radius * 2.0f, radius * 2.0f}});
    } else {
        ShapeCache &shapes = ctx.shapes;
        ShapeMesh(shapes.sphereMesh, 1.0f, radius, shapes.positions);
        MeshView view{};
        view.yaw = state.spin;
        view.pitch = 0.42f;
        view.centerX = center.x;
        view.centerY = center.y;
        view.cameraDistance = 4.5f;
        view.lightLevels = 16;
        ProjectMesh(shapes.sphereMesh, shapes.positions, radius, view, shapes.quads);
        DrawFlatShaded(ctx, shapes.quads, color, false);
    }

    // Saturn BIOS-style emblem glyph floating in front of the sphere: solid interior with a crisp white stroke
    ImFont *iconFont = ctx.theme.GetIconFont();
    if (iconFont == nullptr) {
        iconFont = ctx.theme.GetFont();
    }
    if (iconFont != nullptr && !iconGlyph.empty()) {
        const float iconSize = radius * 0.88f;
        const ImVec2 glyphSize =
            iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, iconGlyph.data(), iconGlyph.data() + iconGlyph.size());
        const ImVec2 basePos(center.x - glyphSize.x * 0.5f, center.y - glyphSize.y * 0.5f);

        // Soft drop shadow on the sphere surface
        const float shadowOffset = std::max(1.5f, ctx.layout.Scale(3.5f));
        ctx.drawList->AddText(iconFont, iconSize, ImVec2(basePos.x, basePos.y + shadowOffset), IM_COL32(0, 0, 0, 155),
                              iconGlyph.data(), iconGlyph.data() + iconGlyph.size());

        // Crisp white stroke around the solid glyph
        const float stroke = std::max(1.5f, ctx.layout.Scale(2.4f));
        constexpr int kStrokeSteps = 12;
        for (int i = 0; i < kStrokeSteps; ++i) {
            const float angle = static_cast<float>(i) * (6.2831853f / static_cast<float>(kStrokeSteps));
            const ImVec2 offsetPos(basePos.x + std::cos(angle) * stroke, basePos.y + std::sin(angle) * stroke);
            ctx.drawList->AddText(iconFont, iconSize, offsetPos, IM_COL32(255, 255, 255, 250), iconGlyph.data(),
                                  iconGlyph.data() + iconGlyph.size());
        }

        // Solid interior tinted to complement the sphere color
        const ImU32 fillColor =
            LerpColor(LerpColor(IM_COL32(18, 26, 48, 255), color, 0.42f), LerpColor(color, IM_COL32_WHITE, 0.25f),
                      state.focus * 0.65f);
        ctx.drawList->AddText(iconFont, iconSize, basePos, fillColor, iconGlyph.data(),
                              iconGlyph.data() + iconGlyph.size());
    }

    return pressed;
}

} // namespace platform::uwp::shell

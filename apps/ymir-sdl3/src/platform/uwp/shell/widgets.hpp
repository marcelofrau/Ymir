#pragma once

/**
@file
@brief Drawing primitives and gamepad-driven widgets of the Xbox shell.

Visual language: a 90s console BIOS rendered with a high-tech finish. The cyan bar is a dot-matrix LCD, menu
spheres are faceted, flat-shaded quad meshes like the Saturn's VDP1 polygons, focus halos use the Saturn's "mesh"
checkerboard transparency, and a faint scanline pass sits on top of everything.

The widgets are ordinary ImGui items (an `InvisibleButton` for hit testing and navigation) drawn by hand on the
window draw list. ImGui's own gamepad navigation moves the focus between them with the d-pad and the left stick and
activates them with A, so the shell does not need a focus system of its own. ImGui's rectangular navigation cursor
is hidden; each widget draws its own focus effect instead.

Every widget draws an art asset when the theme provides one and a procedural version otherwise.
*/

#include "audio_cube.hpp"
#include "layout.hpp"
#include "marquee.hpp"
#include "quad_mesh.hpp"
#include "starfield.hpp"
#include "theme.hpp"

#include <imgui.h>

#include <span>
#include <string_view>
#include <vector>

namespace app::services {

class GraphicsService;

} // namespace app::services

namespace platform::uwp::shell {

/// @brief Meshes and scratch buffers reused by the widgets from frame to frame, owned by the shell.
struct ShapeCache {
    QuadMesh sphereMesh = BuildCubeMesh(7);
    QuadMesh cubeMesh = BuildCubeMesh(6);
    std::vector<Vec3> positions;
    std::vector<ProjectedQuad> quads;
};

/// @brief Everything a widget needs to draw itself during one frame.
struct DrawContext {
    ImDrawList *drawList;
    const Layout &layout;
    const Theme &theme;
    app::services::GraphicsService &graphics;
    ShapeCache &shapes;
    double time;     // seconds since the shell started, drives the animations
    float deltaTime; // seconds since the previous frame
};

/// @brief The controller buttons the legend can show.
enum class Glyph { A, B, X, Y, LB, RB, View, Menu, DPad };

/// @brief A legend entry: a controller button and what it does.
struct ButtonHint {
    Glyph glyph;
    std::string_view label;
};

/// @brief Animation state of a grid tile, kept by the screen across frames.
struct TileState {
    float focus = 0.0f;       // eases from 0 (idle) to 1 (focused)
    float marqueeTime = 0.0f; // seconds spent focused, drives the title marquee when the name overflows
};

/// @brief Animation state of a sphere button, kept by the screen across frames.
struct SphereState {
    float focus = 0.0f; // eases from 0 (idle) to 1 (focused)
    float spin = 0.0f;  // accumulated rotation, in radians
};

/// @brief Converts a shell point to an ImGui vector.
inline ImVec2 ToImVec2(Vec2 v) {
    return {v.x, v.y};
}

// -----------------------------------------------------------------------------
// Backdrop and dashboards

/// @brief Draws the space backdrop and the starfield over the whole display.
///
/// The backdrop is the `bg_space` asset, or a deep-space gradient without it. Over the artwork, which has stars of
/// its own, the procedural stars are kept faint while cruising and only come out in full during a warp jump.
void DrawSpaceBackground(const DrawContext &ctx, const Starfield &starfield, std::vector<ProjectedStar> &scratch);

/// @brief Draws the bottom dashboard, the cockpit with the two pods (`dash_bottom` asset, or a placeholder panel).
void DrawBottomDashboard(const DrawContext &ctx);

/// @brief Draws the top dashboard (`dash_top` asset, or a placeholder bar) with its dot-matrix LCD: the title on the
/// left, the marquee after it.
/// @param[in] ctx the draw context
/// @param[in] marquee the scrolling messages; its offset is in LCD columns
void DrawTopDashboard(const DrawContext &ctx, const Marquee &marquee);

/// @brief Retrieves how many LCD columns the marquee has, after the title.
[[nodiscard]] int MarqueeColumns(const DrawContext &ctx);

/// @brief Draws the subtle version label in the bottom-right corner using the shell font at size 20.
void DrawVersionLabel(const DrawContext &ctx, std::string_view version);

/// @brief Draws a faint scanline pass over the whole display, the CRT finish of the shell.
void DrawScanlines(const DrawContext &ctx);

// -----------------------------------------------------------------------------
// Panels and text

/// @brief Draws an asset frame stretched over a rectangle.
/// @param[in] ctx the draw context
/// @param[in] asset the asset to draw
/// @param[in] rect the destination rectangle, in display coordinates
/// @param[in] tint the color multiplied with the texture
void DrawAsset(const DrawContext &ctx, const ThemeAsset &asset, Rect rect, ImU32 tint = IM_COL32_WHITE);

/// @brief Draws a translucent overlay panel with a title, used for messages and dialogs over the backdrop.
/// @param[in] ctx the draw context
/// @param[in] rect the panel area, in display coordinates
/// @param[in] title the panel title
void DrawOverlayPanel(const DrawContext &ctx, Rect rect, std::string_view title);

/// @brief Draws text with a vector font at a reference size.
/// @param[in] ctx the draw context
/// @param[in] font the font to use
/// @param[in] refSize the font size in reference units
/// @param[in] pos the top-left position, in display coordinates
/// @param[in] color the text color
/// @param[in] text the text
/// @param[in] wrapWidth the wrap width in display pixels; 0 disables wrapping
void DrawLabel(const DrawContext &ctx, ImFont *font, float refSize, Vec2 pos, ImU32 color, std::string_view text,
               float wrapWidth = 0.0f);

/// @brief Draws vector-font text horizontally centered on a point.
void DrawLabelCentered(const DrawContext &ctx, ImFont *font, float refSize, Vec2 center, ImU32 color,
                       std::string_view text);

/// @brief Measures vector-font text at a reference size.
/// @return the text size in display pixels
Vec2 MeasureLabel(const DrawContext &ctx, ImFont *font, float refSize, std::string_view text);

/// @brief Draws a title with a holographic finish: a slightly jittering cyan/magenta split behind white text.
void DrawHoloLabelCentered(const DrawContext &ctx, ImFont *font, float refSize, Vec2 center, std::string_view text);

/// @brief Draws dot-matrix text.
/// @param[in] ctx the draw context
/// @param[in] pos the top-left corner of the first glyph, in display coordinates
/// @param[in] refPitch the distance between dots, in reference units
/// @param[in] text the UTF-8 text
/// @param[in] color the color of the lit dots
void DrawDotText(const DrawContext &ctx, Vec2 pos, float refPitch, std::string_view text, ImU32 color);

/// @brief Draws dot-matrix text centered on a point.
void DrawDotTextCentered(const DrawContext &ctx, Vec2 center, float refPitch, std::string_view text, ImU32 color);

/// @brief Measures dot-matrix text.
/// @return the text size in display pixels
Vec2 MeasureDotText(const DrawContext &ctx, float refPitch, std::string_view text);

// -----------------------------------------------------------------------------
// Controller legend

/// @brief Draws a controller button glyph.
/// @param[in] ctx the draw context
/// @param[in] glyph the button
/// @param[in] center the glyph center, in display coordinates
/// @param[in] refRadius the glyph radius in reference units
void DrawGlyph(const DrawContext &ctx, Glyph glyph, Vec2 center, float refRadius);

/// @brief Draws the controller legend: each button and what it does on the current screen, centered on the
/// `legend` anchor of the theme over a dark pill so it reads on the bright cockpit floor.
void DrawLegend(const DrawContext &ctx, std::span<const ButtonHint> hints);

// -----------------------------------------------------------------------------
// 3D shapes

/// @brief Draws projected quads in flat colors derived from a base color.
/// @param[in] ctx the draw context
/// @param[in] quads the projected quads, back to front
/// @param[in] color the base color
/// @param[in] drawEdges whether to draw darker wireframe edges over each quad
void DrawFlatShaded(const DrawContext &ctx, std::span<const ProjectedQuad> quads, ImU32 color, bool drawEdges = true);

/// @brief Draws an audio-reactive cube floating over a pod of the bottom dashboard.
///
/// The cube is opaque and hovers a little above the pod, bobbing gently. Its shape, size and spin come from `cube`;
/// a soft glow on the pod brightens with the music.
///
/// @param[in] ctx the draw context
/// @param[in] pod where the pod is, from the theme layout
/// @param[in] cube the animation state of the cube
/// @param[in] color the cube color
/// @param[in] phase a per-cube offset of the bobbing, so the two cubes do not move in lockstep
void DrawAudioCube(const DrawContext &ctx, const PodAnchor &pod, const AudioCube &cube, ImU32 color, float phase);

/// @brief A game in the library grid, drawn as a hologram floating over the sky.
///
/// The tile is a translucent cyan panel with corner brackets and holographic scanlines. The cover (if any) is tinted
/// cyan and slightly transparent until the tile is focused, when it lifts, glows and shows in full color. Without a
/// cover, the product number and the title are shown in dot-matrix text.
///
/// @param[in] ctx the draw context
/// @param[in] id the ImGui id of the tile
/// @param[in] rect the tile area, in display coordinates
/// @param[in] title the game title
/// @param[in] productCode the product number, shown when there is no cover
/// @param[in] cover the cover, or `nullptr`
/// @param[in] seed a per-tile number that desynchronizes the hologram flicker
/// @param[in,out] state the animation state of this tile
/// @return `true` on the frame the tile is activated
bool HologramTile(const DrawContext &ctx, const char *id, Rect rect, std::string_view title,
                  std::string_view productCode, const ThemeAsset *cover, int seed, TileState &state);

/// @brief A hologram tile for an action instead of a game, such as adding a library folder.
/// @param[in] ctx the draw context
/// @param[in] id the ImGui id of the tile
/// @param[in] rect the tile area, in display coordinates
/// @param[in] symbol a short symbol drawn large in the middle (e.g. "+")
/// @param[in] label the action name
/// @param[in,out] state the animation state of this tile
/// @return `true` on the frame the tile is activated
bool HologramActionTile(const DrawContext &ctx, const char *id, Rect rect, std::string_view symbol,
                        std::string_view label, TileState &state);

/// @brief Shortens text so it fits a width in dot-matrix columns, ending it with "..." when cut.
/// @param[in] text the UTF-8 text
/// @param[in] maxWidth the available width, in display pixels
/// @param[in] pitch the dot pitch, in display pixels
[[nodiscard]] std::string FitDotText(std::string_view text, float maxWidth, float pitch);

/// @brief A menu option shown as a sphere, in the style of the Saturn BIOS.
///
/// Uses the `sphere_<assetKey>` asset when available (static or as a sprite sheet, which then plays while the
/// sphere is focused). Without it, the sphere is a faceted quad mesh in `color` that spins slowly and faster while
/// focused. The focus halo is the optional `sphere_glow` asset, or a ring of mesh-transparency dots with orbiting
/// pixel blocks.
///
/// @param[in] ctx the draw context
/// @param[in] id the ImGui id of the button
/// @param[in] assetKey the asset id suffix, e.g. `disc` for `sphere_disc`
/// @param[in] center the sphere center, in display coordinates
/// @param[in] refRadius the idle sphere radius in reference units; it grows while focused
/// @param[in] color the color of the procedural sphere
/// @param[in,out] state the animation state of this sphere
/// @return `true` on the frame the sphere is activated
bool SphereButton(const DrawContext &ctx, const char *id, std::string_view assetKey, std::string_view iconGlyph,
                  Vec2 center, float refRadius, ImU32 color, SphereState &state);

} // namespace platform::uwp::shell

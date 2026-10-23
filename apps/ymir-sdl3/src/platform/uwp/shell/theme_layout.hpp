#pragma once

/**
@file
@brief Positions of the shell elements, tied to the artwork.

The art decides where things go: the text has to sit inside the cyan bar of the top dashboard and the cubes have to
float over the pods of the bottom dashboard. These anchors are therefore data, read from the `[layout]` section of
`theme.toml`, so new artwork can move them without a rebuild. The defaults match the embedded artwork.

Every value is in coordinates of the 1920x1080 reference canvas (see `Layout::CanvasPoint()`).
*/

#include <platform/uwp/shell/layout.hpp>

namespace platform::uwp::shell {

/// @brief A pod of the bottom dashboard, where an audio-reactive cube floats.
struct PodAnchor {
    Vec2 center;
    float radius;
};

/// @brief A horizontal row of option spheres.
struct SphereRow {
    float firstX; // center of the first sphere
    float lastX;  // center of the last sphere
    float y;      // vertical center of the row
    float radius; // idle sphere radius
};

/// @brief Where every shell element goes, in reference canvas coordinates.
struct ThemeLayout {
    Vec2 dashTop{294.0f, -14.0f};  // top-left corner of the top dashboard (aligned so bezel touches y = 0)
    Vec2 dashBottom{0.0f, 675.0f}; // top-left corner of the bottom dashboard

    // The dot-matrix LCD inside the cyan bar: "YMIR" and the marquee. The grid is centered in the rectangle with one
    // dot every lcdPitch reference units
    Rect lcd{{422.0f, 26.0f}, {1075.0f, 75.0f}};
    float lcdPitch = 5.0f;

    PodAnchor podLeft{{183.0f, 814.0f}, 112.0f};
    PodAnchor podRight{{1735.0f, 814.0f}, 112.0f};

    // The library grid floats in the sky between the dashboards; the action spheres sit on the cockpit console
    Rect grid{{330.0f, 165.0f}, {1260.0f, 430.0f}};
    int gridColumns = 6;
    SphereRow spheres{660.0f, 1260.0f, 905.0f, 34.0f};

    Vec2 focusLabel{960.0f, 192.0f};       // center of the focused item name
    Vec2 focusDescription{960.0f, 228.0f}; // center of the focused item description

    Rect messagePanel{{330.0f, 262.0f}, {1260.0f, 400.0f}}; // panels such as "BIOS NOT FOUND"

    Vec2 legend{960.0f, 1044.0f};      // center of the controller button legend
    Vec2 versionPos{1896.0f, 1064.0f}; // bottom-right anchor of the subtle version text
};

} // namespace platform::uwp::shell

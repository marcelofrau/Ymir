#pragma once

/**
@file
@brief Resolution-independent layout for the Xbox shell.

The shell is designed against a fixed 1920x1080 reference canvas. `Layout` maps reference coordinates to the real
display: it scales uniformly (preserving the aspect ratio), centers the canvas with letterbox bars when the aspect
ratio differs, and insets the content by a TV safe area so nothing important lands in the overscan region.

This file has no ImGui or WinRT dependency so it can be unit tested on the desktop.
*/

namespace platform::uwp::shell {

/// @brief A 2D point or size in either reference or display coordinates.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

/// @brief An axis-aligned rectangle given by its top-left corner and its size.
struct Rect {
    Vec2 pos;
    Vec2 size;
};

/// @brief Maps the 1920x1080 reference canvas of the shell onto the real display.
class Layout {
public:
    /// @brief Width of the reference canvas the shell is designed against.
    static constexpr float kReferenceWidth = 1920.0f;

    /// @brief Height of the reference canvas the shell is designed against.
    static constexpr float kReferenceHeight = 1080.0f;

    /// @brief Recomputes the mapping for a new display size.
    /// @param[in] displayWidth the display width in pixels
    /// @param[in] displayHeight the display height in pixels
    /// @param[in] safeAreaFraction fraction of each dimension reserved as overscan margin on each side, clamped to
    /// [0, 0.25]
    void Update(float displayWidth, float displayHeight, float safeAreaFraction);

    /// @brief Converts a reference length to display pixels.
    /// @param[in] value the length in reference units
    /// @return the length in display pixels
    [[nodiscard]] float Scale(float value) const {
        return value * m_scale;
    }

    /// @brief Converts a display length back to reference units.
    /// @param[in] value the length in display pixels
    /// @return the length in reference units
    [[nodiscard]] float Unscale(float value) const {
        return m_scale > 0.0f ? value / m_scale : 0.0f;
    }

    /// @brief Converts a point on the reference canvas to display coordinates.
    /// The point is placed relative to the full canvas, ignoring the safe area. Use it for full-bleed elements.
    /// @param[in] point the point in reference coordinates
    /// @return the point in display coordinates
    [[nodiscard]] Vec2 CanvasPoint(Vec2 point) const;

    /// @brief Converts a point on the reference canvas to display coordinates inside the safe area.
    /// (0, 0) maps to the top-left corner of the safe area and (1920, 1080) to its bottom-right corner, so content
    /// laid out against the reference canvas never touches the overscan margin.
    /// @param[in] point the point in reference coordinates
    /// @return the point in display coordinates
    [[nodiscard]] Vec2 SafePoint(Vec2 point) const;

    /// @brief Retrieves the whole display area, in display coordinates.
    [[nodiscard]] Rect Display() const {
        return m_display;
    }

    /// @brief Retrieves the area covered by the letterboxed reference canvas, in display coordinates.
    [[nodiscard]] Rect Canvas() const {
        return m_canvas;
    }

    /// @brief Retrieves the safe area inside the canvas, in display coordinates.
    [[nodiscard]] Rect SafeArea() const {
        return m_safe;
    }

    /// @brief Retrieves the current reference-to-display scale factor used for lengths.
    [[nodiscard]] float ScaleFactor() const {
        return m_scale;
    }

private:
    Rect m_display{};
    Rect m_canvas{};
    Rect m_safe{};
    float m_scale = 1.0f;      // uniform scale of the canvas
    float m_safeScaleX = 1.0f; // horizontal scale of the safe area relative to the reference canvas
    float m_safeScaleY = 1.0f; // vertical scale of the safe area relative to the reference canvas
};

} // namespace platform::uwp::shell

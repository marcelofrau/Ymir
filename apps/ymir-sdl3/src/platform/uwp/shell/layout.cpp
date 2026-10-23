#include "layout.hpp"

#include <algorithm>

namespace platform::uwp::shell {

void Layout::Update(float displayWidth, float displayHeight, float safeAreaFraction) {
    displayWidth = std::max(displayWidth, 1.0f);
    displayHeight = std::max(displayHeight, 1.0f);
    safeAreaFraction = std::clamp(safeAreaFraction, 0.0f, 0.25f);

    m_display = {{0.0f, 0.0f}, {displayWidth, displayHeight}};

    // Fit the reference canvas inside the display without distorting it, then center it so any mismatch in aspect
    // ratio turns into equal bars on both sides
    m_scale = std::min(displayWidth / kReferenceWidth, displayHeight / kReferenceHeight);
    const Vec2 canvasSize{kReferenceWidth * m_scale, kReferenceHeight * m_scale};
    m_canvas = {{(displayWidth - canvasSize.x) * 0.5f, (displayHeight - canvasSize.y) * 0.5f}, canvasSize};

    // The safe area shrinks the canvas by the same fraction on every side. Lengths keep using the uniform scale so
    // round shapes stay round; only the placement of points is squeezed into the safe area
    const Vec2 inset{canvasSize.x * safeAreaFraction, canvasSize.y * safeAreaFraction};
    m_safe = {{m_canvas.pos.x + inset.x, m_canvas.pos.y + inset.y},
              {canvasSize.x - inset.x * 2.0f, canvasSize.y - inset.y * 2.0f}};
    m_safeScaleX = m_safe.size.x / kReferenceWidth;
    m_safeScaleY = m_safe.size.y / kReferenceHeight;
}

Vec2 Layout::CanvasPoint(Vec2 point) const {
    return {m_canvas.pos.x + point.x * m_scale, m_canvas.pos.y + point.y * m_scale};
}

Vec2 Layout::SafePoint(Vec2 point) const {
    return {m_safe.pos.x + point.x * m_safeScaleX, m_safe.pos.y + point.y * m_safeScaleY};
}

} // namespace platform::uwp::shell

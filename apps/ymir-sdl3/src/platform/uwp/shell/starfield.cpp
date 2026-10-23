#include "starfield.hpp"

#include <algorithm>
#include <cmath>

namespace platform::uwp::shell {

Starfield::Starfield(std::size_t starCount, std::uint32_t seed)
    : m_rng(seed) {
    m_stars.resize(starCount);
    for (Star &star : m_stars) {
        // Spread the initial stars over the whole depth range so the first frame is already a full sky
        Respawn(star, true);
    }
}

void Starfield::Update(float deltaTime) {
    deltaTime = std::max(deltaTime, 0.0f);

    // Ease the speed exponentially towards the target. The time constant is short enough for a warp jump to settle
    // within about a second, and the formula is frame-rate independent
    constexpr float kEaseRate = 2.5f;
    const float blend = 1.0f - std::exp(-kEaseRate * deltaTime);
    m_speed += (m_targetSpeed - m_speed) * blend;

    for (Star &star : m_stars) {
        star.z -= m_speed * deltaTime;
        if (star.z <= kNearPlane) {
            Respawn(star, false);
        }
    }
}

void Starfield::Project(float centerX, float centerY, float focalLength, std::vector<ProjectedStar> &out) const {
    out.clear();
    out.reserve(m_stars.size());

    for (const Star &star : m_stars) {
        const float x = centerX + star.x / star.z * focalLength;
        const float y = centerY + star.y / star.z * focalLength;

        // The streak starts where the star was a moment ago. At cruise speed the two points are almost the same, so
        // stars look like dots; at warp speed they stretch into lines
        const float prevZ = std::min(star.z + m_speed * kStreakSeconds, 1.0f);
        const float prevX = centerX + star.x / prevZ * focalLength;
        const float prevY = centerY + star.y / prevZ * focalLength;

        // Fade stars in from the far plane so they do not pop into existence
        const float brightness = std::clamp((1.0f - star.z) * 1.25f, 0.0f, 1.0f);
        if (brightness <= 0.0f) {
            continue;
        }
        const float radius = 0.6f + brightness * 1.6f;
        out.push_back({x, y, prevX, prevY, brightness, radius});
    }
}

void Starfield::Respawn(Star &star, bool anywhere) {
    std::uniform_real_distribution<float> lateral(-1.0f, 1.0f);
    std::uniform_real_distribution<float> depth(anywhere ? kNearPlane : 0.85f, 1.0f);

    star.x = lateral(m_rng);
    star.y = lateral(m_rng);
    star.z = depth(m_rng);
}

} // namespace platform::uwp::shell

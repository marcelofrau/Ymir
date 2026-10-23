#pragma once

/**
@file
@brief Procedural "warp" starfield, the backdrop of the Xbox shell.

The Sega Saturn BIOS shows a field of stars flying towards the viewer. `Starfield` simulates that: stars live in a
3D box in front of the camera, move towards it at an animatable speed and are projected with a simple perspective
divide. The speed eases towards a target, so screen transitions can briefly "jump to warp" and settle again.

This file has no ImGui or WinRT dependency so it can be unit tested on the desktop. Drawing is left to the caller.
*/

#include <cstdint>
#include <random>
#include <vector>

namespace platform::uwp::shell {

/// @brief A star projected onto the screen, ready to be drawn.
struct ProjectedStar {
    float x, y;         // current position in display pixels
    float prevX, prevY; // tail of the streak, in display pixels; almost (x, y) at cruise speed
    float brightness;   // 0 (far away, invisible) to 1 (right in front of the camera)
    float radius;       // dot radius in display pixels
};

/// @brief Simulates a field of stars flying towards the viewer.
class Starfield {
public:
    /// @brief Speed of the drift while the shell is idle, in depth units per second.
    static constexpr float kCruiseSpeed = 0.12f;

    /// @brief Speed of a transition "jump to warp", in depth units per second.
    static constexpr float kWarpSpeed = 1.6f;

    /// @brief Creates a starfield with a fixed seed, so the initial sky is the same on every launch.
    /// @param[in] starCount the number of stars to simulate
    /// @param[in] seed the random seed
    explicit Starfield(std::size_t starCount = 600, std::uint32_t seed = 0x5A7u);

    /// @brief Advances the simulation.
    /// @param[in] deltaTime the elapsed time in seconds; negative values are treated as zero
    void Update(float deltaTime);

    /// @brief Sets the speed the field eases towards.
    /// @param[in] speed the target speed in depth units per second
    void SetTargetSpeed(float speed) {
        m_targetSpeed = speed;
    }

    /// @brief Kicks the field into warp speed; it eases back to the target speed on its own.
    void Warp() {
        m_speed = kWarpSpeed;
    }

    /// @brief Retrieves the current speed.
    [[nodiscard]] float Speed() const {
        return m_speed;
    }

    /// @brief Projects every visible star onto a viewport.
    /// @param[in] centerX the horizontal vanishing point, in display pixels
    /// @param[in] centerY the vertical vanishing point, in display pixels
    /// @param[in] focalLength the projection scale in display pixels; half the viewport width works well
    /// @param[out] out receives the projected stars; it is cleared first
    void Project(float centerX, float centerY, float focalLength, std::vector<ProjectedStar> &out) const;

private:
    struct Star {
        float x, y; // lateral position, in [-1, 1]
        float z;    // depth, in (kNearPlane, 1]; smaller is closer
    };

    static constexpr float kNearPlane = 0.02f;

    // How far back in time the streak of a star reaches. Tying the streak to the speed instead of to the previous
    // frame keeps its length independent of the frame rate
    static constexpr float kStreakSeconds = 0.06f;

    void Respawn(Star &star, bool anywhere);

    std::vector<Star> m_stars;
    std::minstd_rand m_rng;
    float m_speed = kCruiseSpeed;
    float m_targetSpeed = kCruiseSpeed;
};

} // namespace platform::uwp::shell

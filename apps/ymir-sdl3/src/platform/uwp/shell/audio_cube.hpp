#pragma once

/**
@file
@brief The motion of the audio-reactive cubes that float over the pods of the bottom dashboard.

Each cube follows one channel of the music: it rests as a slowly turning cube in silence and, as the sound gets
louder, swells, spins faster and morphs towards a sphere, with the bass making it wobble. The audio levels go
through an envelope with a fast attack and a slow release, so the cubes pulse with the beat instead of jittering
with every sample block.

This file has no ImGui or WinRT dependency so it can be unit tested on the desktop. Drawing is left to the caller.
*/

namespace platform::uwp::shell {

/// @brief Audio levels of one channel, in [0, 1].
struct AudioLevels {
    float rms = 0.0f;  // overall loudness
    float bass = 0.0f; // loudness of the low frequencies
};

/// @brief Follows a signal with separate attack and release times.
class Envelope {
public:
    /// @brief Creates an envelope.
    /// @param[in] attackSeconds time constant while the signal rises
    /// @param[in] releaseSeconds time constant while the signal falls
    Envelope(float attackSeconds, float releaseSeconds)
        : m_attack(attackSeconds)
        , m_release(releaseSeconds) {}

    /// @brief Moves the envelope towards a new input.
    /// @param[in] input the current signal value
    /// @param[in] deltaTime the elapsed time in seconds
    /// @return the new envelope value
    float Update(float input, float deltaTime);

    /// @brief Retrieves the current envelope value.
    [[nodiscard]] float Value() const {
        return m_value;
    }

private:
    float m_attack;
    float m_release;
    float m_value = 0.0f;
};

/// @brief The animation state of one audio-reactive cube.
class AudioCube {
public:
    /// @brief Advances the animation.
    /// @param[in] deltaTime the elapsed time in seconds
    /// @param[in] levels the current levels of the channel this cube follows
    void Update(float deltaTime, AudioLevels levels);

    /// @brief Retrieves how far the cube has morphed into a sphere: 0 is a cube, 1 a sphere.
    [[nodiscard]] float Morph() const;

    /// @brief Retrieves the size multiplier, 1 in silence.
    [[nodiscard]] float Scale() const;

    /// @brief Retrieves how strongly the surface wobbles, 0 in silence.
    [[nodiscard]] float Wobble() const;

    /// @brief Retrieves the accumulated rotation, in radians.
    [[nodiscard]] float Spin() const {
        return m_spin;
    }

    /// @brief Retrieves the loudness envelope, in [0, 1].
    [[nodiscard]] float Level() const {
        return m_level.Value();
    }

private:
    Envelope m_level{0.03f, 0.35f};
    Envelope m_bass{0.02f, 0.25f};
    float m_spin = 0.0f;
};

} // namespace platform::uwp::shell

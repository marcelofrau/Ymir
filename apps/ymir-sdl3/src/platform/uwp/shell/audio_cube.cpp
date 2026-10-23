#include "audio_cube.hpp"

#include <algorithm>
#include <cmath>

namespace platform::uwp::shell {

float Envelope::Update(float input, float deltaTime) {
    deltaTime = std::max(deltaTime, 0.0f);
    const float timeConstant = std::max(input > m_value ? m_attack : m_release, 1e-4f);
    m_value += (input - m_value) * (1.0f - std::exp(-deltaTime / timeConstant));
    return m_value;
}

void AudioCube::Update(float deltaTime, AudioLevels levels) {
    // Gentle-medium sensitivity: follows the music smoothly without over-reacting
    const float boostedRms = std::clamp(levels.rms * 2.5f, 0.0f, 1.0f);
    const float boostedBass = std::clamp(levels.bass * 3.4f, 0.0f, 1.0f);
    m_level.Update(boostedRms, deltaTime);
    m_bass.Update(boostedBass, deltaTime);

    // A slow idle turn that speeds up with the music
    m_spin += std::max(deltaTime, 0.0f) * (0.45f + 2.6f * m_level.Value() + 0.8f * m_bass.Value());
}

float AudioCube::Morph() const {
    return std::clamp(std::pow(m_level.Value(), 0.82f), 0.0f, 1.0f);
}

float AudioCube::Scale() const {
    return 1.0f + 0.18f * m_bass.Value() + 0.07f * m_level.Value();
}

float AudioCube::Wobble() const {
    return 0.14f * m_bass.Value() + 0.04f * m_level.Value();
}

} // namespace platform::uwp::shell

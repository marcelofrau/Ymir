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
    // Music RMS rarely goes above 0.5, so the levels are boosted before they drive the shape
    m_level.Update(std::clamp(levels.rms * 2.2f, 0.0f, 1.0f), deltaTime);
    m_bass.Update(std::clamp(levels.bass * 3.0f, 0.0f, 1.0f), deltaTime);

    // A slow idle turn that speeds up with the music
    m_spin += std::max(deltaTime, 0.0f) * (0.45f + 3.0f * m_level.Value());
}

float AudioCube::Morph() const {
    return std::clamp(std::pow(m_level.Value(), 0.8f), 0.0f, 1.0f);
}

float AudioCube::Scale() const {
    return 1.0f + 0.2f * m_bass.Value() + 0.08f * m_level.Value();
}

float AudioCube::Wobble() const {
    return 0.14f * m_bass.Value();
}

} // namespace platform::uwp::shell

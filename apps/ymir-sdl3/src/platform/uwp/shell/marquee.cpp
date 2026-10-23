#include "marquee.hpp"

#include <algorithm>
#include <utility>

namespace platform::uwp::shell {

void Marquee::Post(std::string text) {
    if (text.empty()) {
        return;
    }
    m_events.push_back(std::move(text));
    while (m_events.size() > kMaxQueuedEvents) {
        m_events.pop_front();
    }
    // Interrupt idle text (or an event that has already been scrolling) so the new message appears immediately
    if (m_active && (!m_currentIsEvent || m_offset < 0.0f)) {
        m_active = false;
    }
}

void Marquee::SetIdleTexts(std::vector<std::string> texts) {
    m_idleTexts = std::move(texts);
    m_nextIdle = 0;
}

void Marquee::Update(float deltaTime, float areaWidth, float speed, const MeasureTextFn &measure) {
    if (!m_active && !PickNext(areaWidth, measure)) {
        return;
    }

    float dt = std::max(deltaTime, 0.0f);
    if (m_holdTimer > 0.0f) {
        const float held = std::min(dt, m_holdTimer);
        m_holdTimer -= held;
        dt -= held;
    }
    m_offset -= dt * speed;

    // The text is done once its tail has left the area
    if (m_offset + m_currentWidth < 0.0f) {
        m_active = false;
    }
}

bool Marquee::PickNext(float /*areaWidth*/, const MeasureTextFn &measure) {
    if (!m_events.empty()) {
        m_current = std::move(m_events.front());
        m_events.pop_front();
        m_currentIsEvent = true;
    } else if (!m_idleTexts.empty()) {
        m_nextIdle %= m_idleTexts.size();
        m_current = m_idleTexts[m_nextIdle++];
        m_currentIsEvent = false;
    } else {
        return false;
    }

    m_currentWidth = measure ? measure(m_current) : 0.0f;
    m_offset = 0.0f;
    m_holdTimer = kHoldSeconds;
    m_active = true;
    return true;
}

} // namespace platform::uwp::shell

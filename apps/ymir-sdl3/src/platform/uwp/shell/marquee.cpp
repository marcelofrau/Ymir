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
}

void Marquee::SetIdleTexts(std::vector<std::string> texts) {
    m_idleTexts = std::move(texts);
    m_nextIdle = 0;
}

void Marquee::Update(float deltaTime, float areaWidth, float speed, const MeasureTextFn &measure) {
    if (!m_active && !PickNext(areaWidth, measure)) {
        return;
    }

    m_offset -= std::max(deltaTime, 0.0f) * speed;

    // The text is done once its tail has left the area; the next one enters from the right edge, which leaves a
    // full area width of empty space between consecutive texts
    if (m_offset + m_currentWidth < 0.0f) {
        m_active = false;
    }
}

bool Marquee::PickNext(float areaWidth, const MeasureTextFn &measure) {
    if (!m_events.empty()) {
        m_current = std::move(m_events.front());
        m_events.pop_front();
    } else if (!m_idleTexts.empty()) {
        m_nextIdle %= m_idleTexts.size();
        m_current = m_idleTexts[m_nextIdle++];
    } else {
        return false;
    }

    m_currentWidth = measure ? measure(m_current) : 0.0f;
    m_offset = areaWidth;
    m_active = true;
    return true;
}

} // namespace platform::uwp::shell

#pragma once

/**
@file
@brief The scrolling message line inside the cyan bar of the top dashboard.

Texts scroll from the right edge of the marquee area to the left, one at a time. Events (messages the app reports,
such as a reset or a disc being loaded) jump the queue; when there are none, the marquee cycles through a list of
idle texts (what is in the tray, tips).

This file has no ImGui or WinRT dependency so it can be unit tested on the desktop. Text measuring is injected.
*/

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace platform::uwp::shell {

/// @brief Measures the width of a text, in the same units as the marquee area.
using MeasureTextFn = std::function<float(std::string_view text)>;

/// @brief A queue of texts scrolling through a fixed-width area.
class Marquee {
public:
    /// @brief Most events kept waiting; older ones are dropped so the marquee never falls far behind the app.
    static constexpr std::size_t kMaxQueuedEvents = 8;

    /// @brief Queues an event. Events are shown before any idle text, in the order they were posted.
    /// @param[in] text the event text; empty texts are ignored
    void Post(std::string text);

    /// @brief Replaces the idle texts shown when no event is waiting. The text on screen finishes scrolling first.
    /// @param[in] texts the idle texts, shown in order and repeated
    void SetIdleTexts(std::vector<std::string> texts);

    /// @brief Advances the scroll.
    /// @param[in] deltaTime the elapsed time in seconds
    /// @param[in] areaWidth the width of the marquee area
    /// @param[in] speed the scroll speed, in area units per second
    /// @param[in] measure measures a text in area units
    void Update(float deltaTime, float areaWidth, float speed, const MeasureTextFn &measure);

    /// @brief Retrieves the text on screen; empty if there is nothing to show.
    [[nodiscard]] std::string_view Current() const {
        return m_active ? std::string_view{m_current} : std::string_view{};
    }

    /// @brief Retrieves the horizontal position of the start of the current text, relative to the left edge of the
    /// area. Starts at the area width and decreases.
    [[nodiscard]] float Offset() const {
        return m_offset;
    }

    /// @brief Retrieves the number of events still waiting.
    [[nodiscard]] std::size_t PendingEvents() const {
        return m_events.size();
    }

private:
    bool PickNext(float areaWidth, const MeasureTextFn &measure);

    std::deque<std::string> m_events;
    std::vector<std::string> m_idleTexts;
    std::size_t m_nextIdle = 0;

    std::string m_current;
    float m_currentWidth = 0.0f;
    float m_offset = 0.0f;
    bool m_active = false;
};

} // namespace platform::uwp::shell

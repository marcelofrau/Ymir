#include "stall_watchdog.hpp"

#include "uwp_platform.hpp"

#include <ymir/util/thread_name.hpp>

#include <fmt/format.h>

namespace platform::uwp {

namespace {

    /// @brief How often the monitor thread checks for progress.
    constexpr std::chrono::milliseconds kPollInterval{500};

    /// @brief How often a stall that keeps going is reported again after the first report.
    constexpr std::chrono::milliseconds kRepeatInterval{5000};

} // namespace

StallWatchdog::~StallWatchdog() {
    Stop();
}

void StallWatchdog::Start(const char *name, std::chrono::milliseconds threshold) {
    if (m_thread.joinable()) {
        return;
    }
    m_name = name;
    m_threshold = threshold;
    {
        std::lock_guard lock{m_mutex};
        m_stopRequested = false;
    }
    m_thread = std::thread{[this] { Run(); }};
}

void StallWatchdog::Stop() {
    if (!m_thread.joinable()) {
        return;
    }
    {
        std::lock_guard lock{m_mutex};
        m_stopRequested = true;
    }
    m_wakeUp.notify_all();
    m_thread.join();
}

void StallWatchdog::Mark(const char *phase) noexcept {
    m_phase.store(phase, std::memory_order_relaxed);
    // Release ordering publishes the phase together with the new sequence number
    m_sequence.fetch_add(1, std::memory_order_release);
}

void StallWatchdog::SetFrame(uint64_t frame) noexcept {
    m_frame.store(frame, std::memory_order_relaxed);
}

void StallWatchdog::Run() {
    using clock = std::chrono::steady_clock;
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;

    util::SetCurrentThreadName("Stall watchdog");

    uint64_t lastSequence = m_sequence.load(std::memory_order_acquire);
    auto lastProgress = clock::now();
    auto nextReport = lastProgress + m_threshold;
    bool stalled = false;

    std::unique_lock lock{m_mutex};
    while (true) {
        if (m_wakeUp.wait_for(lock, kPollInterval, [this] { return m_stopRequested; })) {
            break;
        }

        const uint64_t sequence = m_sequence.load(std::memory_order_acquire);
        const auto now = clock::now();

        if (sequence != lastSequence) {
            if (stalled) {
                Trace(fmt::format("[Ymir watchdog] {} thread resumed after {} ms, now in phase '{}' (frame {})", m_name,
                                  duration_cast<milliseconds>(now - lastProgress).count(),
                                  m_phase.load(std::memory_order_relaxed), m_frame.load(std::memory_order_relaxed)));
                stalled = false;
            }
            lastSequence = sequence;
            lastProgress = now;
            nextReport = now + m_threshold;
            continue;
        }

        if (now >= nextReport) {
            Trace(fmt::format("[Ymir watchdog] {} thread stalled for {} ms in phase '{}' (frame {})", m_name,
                              duration_cast<milliseconds>(now - lastProgress).count(),
                              m_phase.load(std::memory_order_relaxed), m_frame.load(std::memory_order_relaxed)));
            stalled = true;
            nextReport = now + kRepeatInterval;
        }
    }
}

} // namespace platform::uwp

#pragma once

/**
@file
@brief Detects when a thread stops making progress, to locate hangs on Xbox runs that have no debugger attached.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace platform::uwp {

/// @brief Watches a thread that reports its progress through `Mark()` and logs where it is stuck when it stops
/// reporting.
///
/// The watched thread calls `Mark()` at the start of each phase of its work. A monitor thread wakes up periodically.
/// If no new mark has been seen for longer than the threshold, it writes a `[Ymir watchdog] ... stalled` line to the
/// log through `Trace()`, naming the last phase and frame. It repeats that line while the stall lasts and writes a
/// `resumed` line once the thread moves again.
///
/// This tells which phase a hang happens in even when the app is launched from the tile, where there is no debugger
/// to pause the process and inspect it. `Mark()` is lock-free and cheap enough to call several times per frame.
class StallWatchdog {
public:
    StallWatchdog() = default;
    ~StallWatchdog();

    StallWatchdog(const StallWatchdog &) = delete;
    StallWatchdog(StallWatchdog &&) = delete;
    StallWatchdog &operator=(const StallWatchdog &) = delete;
    StallWatchdog &operator=(StallWatchdog &&) = delete;

    /// @brief Starts the monitor thread. Does nothing if it is already running.
    /// @param[in] name the name of the watched thread, used in the log lines; must be a string literal
    /// @param[in] threshold how long the watched thread may stay in one phase before it is reported as stalled
    void Start(const char *name, std::chrono::milliseconds threshold);

    /// @brief Stops and joins the monitor thread. Safe to call more than once and when it was never started.
    void Stop();

    /// @brief Records that the watched thread entered a new phase.
    /// @param[in] phase a short description of the phase; must be a string literal, since only the pointer is kept
    void Mark(const char *phase) noexcept;

    /// @brief Records the number of the frame being processed, which is included in the stall reports.
    /// @param[in] frame the frame number
    void SetFrame(uint64_t frame) noexcept;

private:
    /// @brief Body of the monitor thread.
    void Run();

    const char *m_name = "";
    std::chrono::milliseconds m_threshold{};

    std::atomic<const char *> m_phase{"(no phase marked yet)"};
    std::atomic<uint64_t> m_sequence{0}; ///< Incremented by every `Mark()`; the monitor compares snapshots of it
    std::atomic<uint64_t> m_frame{0};

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wakeUp;
    bool m_stopRequested = false; ///< Guarded by `m_mutex`
};

} // namespace platform::uwp

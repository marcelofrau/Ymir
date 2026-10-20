#pragma once

#include "audio_backend.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/event.hpp>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace app {

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    bool Init(int sampleRate, SDL_AudioFormat format, int channels, uint32 bufferSize);
    void Deinit();

    bool Start();
    bool Stop();
    bool IsRunning() const;

    bool GetAudioStreamFormat(int *sampleRate, SDL_AudioFormat *format, int *channels);

    void ReceiveSample(sint16 left, sint16 right);

    void Snapshot(std::span<Sample, 2048> out) const {
        const uint32 readPos = m_readPos.load(std::memory_order_relaxed);
        std::copy(m_buffer.begin() + readPos, m_buffer.end(), out.begin());
        std::copy(m_buffer.begin(), m_buffer.begin() + readPos, out.begin() + out.size() - readPos);
    }

    void SetGain(float gain) {
        m_gain = gain;
        UpdateGain();
    }

    float GetGain() const {
        return m_gain;
    }

    void SetMute(bool mute) {
        m_mute = mute;
        UpdateGain();
    }

    bool IsMute() const {
        return m_mute;
    }

    void SetSync(bool sync) {
        m_sync = sync;
        if (!sync) {
            m_bufferNotFullEvent.Set();
        }
    }

    bool IsSync() const {
        return m_sync;
    }

    void SetSilent(bool silent) {
        m_silent = silent;
        if (silent) {
            m_bufferNotFullEvent.Set();
        }
    }

    bool IsSilent() const {
        return m_silent;
    }

    uint32 GetBufferCount() const {
        const uint32 readPos = m_readPos.load(std::memory_order_relaxed);
        const uint32 writePos = m_writePos.load(std::memory_order_relaxed);
        return (writePos >= readPos) ? (writePos - readPos) : (m_buffer.size() - (readPos - writePos));
    }

    uint32 GetBufferCapacity() const {
        return m_buffer.size();
    }

    /// @brief Copies frames out of the ring buffer for the active backend.
    ///
    /// Always returns exactly `frameCount` frames, repeating whatever is available when the buffer has run dry.
    /// This is the crude form of audio stretching used when the emulator runs slower than real time.
    int FillFrames(Sample *out, int frameCount);

private:
    std::unique_ptr<AudioBackend> m_backend;

    int m_sampleRate = 44100;
    int m_channels = 2;

    std::array<Sample, 2048> m_buffer{};
    std::atomic_uint32_t m_readPos = 0;
    std::atomic_uint32_t m_writePos = 0;
    util::Event m_bufferNotFullEvent{true};

    std::atomic_bool m_sync = true;
    std::atomic_bool m_silent = false;

    float m_gain = 0.8f;
    bool m_mute = false;

    void UpdateGain();
};

} // namespace app

#pragma once

#include "audio_backend.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;
struct IXAudio2VoiceCallback;

namespace app {

/// @brief Audio backend that plays back through XAudio2.
///
/// XAudio2 is the native audio API on Xbox and is fully synchronous: creating the engine, the mastering voice and
/// the source voice all complete inline. That matters here because SDL3's WASAPI path is not usable on the Xbox.
/// Opening a device there goes through ActivateAudioInterfaceAsync(), whose completion is delivered to the app's
/// ASTA dispatcher, after which SDL waits on a semaphore with no timeout. When the caller is the UI thread (the
/// normal case during startup) the activation never completes and the app deadlocks on the splash screen.
///
/// Measured on the Xbox: pumping the CoreWindow dispatcher roughly 5000 times over 10 seconds does not unblock it,
/// so that activation is not serviced by the CoreWindow dispatcher at all. XAudio2 sidesteps the problem entirely.
///
/// Frames are pulled from the owner into a small pool of source buffers. A buffer still in flight is never
/// overwritten; when every buffer is busy the audio callback simply writes nothing rather than blocking the
/// emulator thread.
class XAudio2Backend final : public AudioBackend {
public:
    XAudio2Backend() noexcept;
    ~XAudio2Backend() override;

    XAudio2Backend(const XAudio2Backend &) = delete;
    XAudio2Backend &operator=(const XAudio2Backend &) = delete;

    bool Init(int sampleRate, int channels, uint32 bufferSize, RequestCallback onRequest, void *userdata) override;
    void Deinit() override;

    bool Start() override;
    bool Stop() override;
    bool IsRunning() const override;

    void SetGain(float gain) override;
    void SetMute(bool mute) override;

private:
    /// @brief Frames accumulated per submitted buffer. 512 frames at 44.1 kHz is about 11.6 ms of audio,
    /// matching the desktop SDL backend buffer size (512) for low audio/video sync latency.
    static constexpr size_t kFramesPerBuffer = 512;

    /// @brief Buffers the source voice cycles through. Enough to absorb scheduling jitter without adding
    /// noticeable latency.
    static constexpr size_t kBufferCount = 4;

    /// @brief Identifies which buffer a completion callback refers to.
    struct BufferContext {
        XAudio2Backend *owner;
        size_t index;
    };

    /// @brief Receives XAudio2 voice callbacks. Defined in the .cpp because it derives from an XAudio2 interface.
    class VoiceCallback;

    /// @brief Marks one buffer as free once XAudio2 has finished playing it.
    void OnBufferEnd(size_t index) noexcept;

    /// @brief Pulls frames from the owner and submits the buffer. Called when the pool runs dry.
    void RefillLocked();

    /// @brief Applies the current gain and mute state to the source voice, if one exists.
    void UpdateVolumeLocked();

    /// @brief Releases every XAudio2 resource.
    void ReleaseLocked() noexcept;

    IXAudio2 *m_engine = nullptr;
    IXAudio2MasteringVoice *m_masteringVoice = nullptr;
    IXAudio2SourceVoice *m_sourceVoice = nullptr;
    VoiceCallback *m_voiceCallback = nullptr;

    RequestCallback m_onRequest = nullptr;
    void *m_userdata = nullptr;

    int m_sampleRate = 44100;
    int m_channels = 2;

    std::array<std::array<Sample, kFramesPerBuffer>, kBufferCount> m_buffers{};
    std::array<BufferContext, kBufferCount> m_contexts{};
    std::array<bool, kBufferCount> m_inFlight{};
    size_t m_nextBuffer = 0;

    std::atomic_bool m_running = false;

    float m_gain = 0.8f;
    bool m_mute = false;

    mutable std::mutex m_mutex;
};

} // namespace app

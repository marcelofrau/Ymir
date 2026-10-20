#pragma once

#include <ymir/core/types.hpp>

namespace app {

/// @brief One stereo frame of signed 16-bit PCM.
struct Sample {
    sint16 left, right;
};

/// @brief Platform audio output device.
///
/// Decouples AudioSystem (which owns the ring buffer, gain, mute and the sync/silent policy, all platform
/// independent) from the API that actually plays sound. Both backends are pull based: the device asks for frames
/// and the backend forwards that request to a callback supplied by AudioSystem.
///
///  - Desktop uses an SDL audio stream (SDLAudioBackend).
///  - UWP/Xbox uses XAudio2 (XAudio2Backend) because SDL's WASAPI path deadlocks there.
class AudioBackend {
public:
    /// @brief Invoked by the backend whenever the device needs more audio.
    /// @param userdata Opaque pointer supplied to Init().
    /// @param out Destination for the frames.
    /// @param frameCount Number of stereo frames the device is asking for.
    /// @return Number of frames actually written, which may be fewer than requested.
    ///
    /// The callback may return fewer frames than asked, which is how AudioSystem implements its crude audio
    /// stretching. The backend is expected to pad the shortfall rather than block.
    using RequestCallback = int (*)(void *userdata, Sample *out, int frameCount);

    virtual ~AudioBackend() = default;

    /// @brief Opens the output device.
    /// @param sampleRate Sample rate in Hz.
    /// @param channels Number of output channels.
    /// @param bufferSize Requested device buffer size in sample frames, as a hint.
    /// @param onRequest Callback the backend invokes when the device needs audio.
    /// @param userdata Opaque pointer passed back to onRequest.
    /// @return True on success. On failure the reason must be retrievable from SDL_GetError() so the caller can
    /// display it, regardless of which platform backend is active.
    virtual bool Init(int sampleRate, int channels, uint32 bufferSize, RequestCallback onRequest, void *userdata) = 0;

    /// @brief Closes the output device, releasing every resource acquired by Init().
    virtual void Deinit() = 0;

    /// @brief Begins playback.
    virtual bool Start() = 0;

    /// @brief Pauses playback.
    virtual bool Stop() = 0;

    /// @brief Checks whether playback is currently running.
    virtual bool IsRunning() const = 0;

    /// @brief Sets the linear output gain, in the range [0, 1].
    virtual void SetGain(float gain) = 0;

    /// @brief Mutes or unmutes the output without changing the gain.
    virtual void SetMute(bool mute) = 0;
};

} // namespace app

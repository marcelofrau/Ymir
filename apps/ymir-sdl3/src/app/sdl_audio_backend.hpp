#pragma once

#include "audio_backend.hpp"

#include <SDL3/SDL_audio.h>

namespace app {

/// @brief Audio backend that plays back through an SDL audio stream.
///
/// This is the desktop path. It is deliberately not used on UWP/Xbox, where opening a device deadlocks inside
/// SDL's WASAPI implementation (see XAudio2Backend for the details).
class SDLAudioBackend final : public AudioBackend {
public:
    SDLAudioBackend() noexcept = default;
    ~SDLAudioBackend() override;

    SDLAudioBackend(const SDLAudioBackend &) = delete;
    SDLAudioBackend &operator=(const SDLAudioBackend &) = delete;

    bool Init(int sampleRate, int channels, uint32 bufferSize, RequestCallback onRequest, void *userdata) override;
    void Deinit() override;

    bool Start() override;
    bool Stop() override;
    bool IsRunning() const override;

    void SetGain(float gain) override;
    void SetMute(bool mute) override;

private:
    /// @brief Trampoline from SDL's C callback into the AudioBackend request callback.
    static void SDLCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount);

    SDL_AudioStream *m_stream = nullptr;
    RequestCallback m_onRequest = nullptr;
    void *m_userdata = nullptr;
    float m_gain = 0.8f;
    bool m_mute = false;
    bool m_running = false;
};

} // namespace app

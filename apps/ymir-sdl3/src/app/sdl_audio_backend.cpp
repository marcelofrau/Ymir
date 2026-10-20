#include "sdl_audio_backend.hpp"

#include "audio_system.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>

#include <string>

namespace app {

SDLAudioBackend::~SDLAudioBackend() {
    Deinit();
}

void SDLAudioBackend::SDLCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int) {
    auto *self = static_cast<SDLAudioBackend *>(userdata);
    if (self == nullptr || self->m_onRequest == nullptr || additionalAmount <= 0) {
        return;
    }

    // SDL asks for a byte count; convert it to stereo frames and let the owner fill a scratch buffer.
    const int frameCount = additionalAmount / static_cast<int>(sizeof(Sample));
    if (frameCount <= 0) {
        return;
    }

    // A fixed scratch buffer avoids per-callback allocation on the audio thread. Requests larger than this are
    // served in chunks.
    constexpr int kScratchFrames = 4096;
    Sample scratch[kScratchFrames];

    int remaining = frameCount;
    while (remaining > 0) {
        const int chunk = (remaining < kScratchFrames) ? remaining : kScratchFrames;
        const int written = self->m_onRequest(self->m_userdata, scratch, chunk);
        if (written <= 0) {
            // The owner had nothing to give. Feed silence instead of looping forever.
            SDL_PutAudioStreamData(stream, scratch, 0);
            break;
        }
        SDL_PutAudioStreamData(stream, scratch, written * static_cast<int>(sizeof(Sample)));
        if (written < chunk) {
            // Short read: the owner implements crude stretching, so stop here and let the device re-ask.
            break;
        }
        remaining -= written;
    }
}

bool SDLAudioBackend::Init(int sampleRate, int channels, uint32 bufferSize, RequestCallback onRequest,
                           void *userdata) {
    Deinit();

    m_onRequest = onRequest;
    m_userdata = userdata;

    const std::string bufferSizeStr = std::to_string(bufferSize);
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, bufferSizeStr.c_str());

    SDL_AudioSpec spec{};
    spec.freq = sampleRate;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels;

    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SDLCallback, this);
    if (m_stream == nullptr) {
        return false;
    }

    SetGain(m_gain);
    SetMute(m_mute);
    return true;
}

void SDLAudioBackend::Deinit() {
    if (m_stream != nullptr) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_running = false;
}

bool SDLAudioBackend::Start() {
    if (m_stream == nullptr) {
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        return false;
    }
    m_running = true;
    return true;
}

bool SDLAudioBackend::Stop() {
    if (m_stream == nullptr) {
        return false;
    }
    if (!SDL_PauseAudioStreamDevice(m_stream)) {
        return false;
    }
    m_running = false;
    return true;
}

bool SDLAudioBackend::IsRunning() const {
    return m_running;
}

void SDLAudioBackend::SetGain(float gain) {
    m_gain = gain;
    if (m_stream != nullptr) {
        SDL_SetAudioStreamGain(m_stream, m_mute ? 0.0f : m_gain);
    }
}

void SDLAudioBackend::SetMute(bool mute) {
    m_mute = mute;
    if (m_stream != nullptr) {
        SDL_SetAudioStreamGain(m_stream, m_mute ? 0.0f : m_gain);
    }
}

} // namespace app

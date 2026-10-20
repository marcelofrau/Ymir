#include "audio_system.hpp"

#include "sdl_audio_backend.hpp"

#if YMIR_PLATFORM_UWP
    #include "xaudio2_backend.hpp"
#endif

#include <SDL3/SDL_log.h>

#include <cstring>

namespace app {

namespace {

/// @brief Copies frames out of the ring buffer, implementing the crude stretching used when the emulator runs
/// slower than real time.
///
/// This is the callback both backends use to pull audio. It always writes exactly the requested number of frames,
/// repeating whatever is available if the buffer has run dry, which keeps the device fed without introducing
/// clicks from silence.
int RequestFrames(void *userdata, Sample *out, int frameCount) {
    auto *self = static_cast<AudioSystem *>(userdata);
    if (self == nullptr || out == nullptr || frameCount <= 0) {
        return 0;
    }
    return self->FillFrames(out, frameCount);
}

} // namespace

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
    Deinit();
}

bool AudioSystem::Init(int sampleRate, SDL_AudioFormat format, int channels, uint32 bufferSize) {
    (void)format; // The emulator always produces signed 16-bit PCM.

    Deinit();

#if YMIR_PLATFORM_UWP
    m_backend = std::make_unique<XAudio2Backend>();
#else
    m_backend = std::make_unique<SDLAudioBackend>();
#endif

    if (!m_backend->Init(sampleRate, channels, bufferSize, &RequestFrames, this)) {
        // The backend reports the reason through SDL_GetError(), which the caller displays.
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to initialize the audio backend: %s", SDL_GetError());
        m_backend.reset();
        return false;
    }

    m_backend->SetGain(m_gain);
    m_backend->SetMute(m_mute);
    return true;
}

void AudioSystem::Deinit() {
    m_bufferNotFullEvent.Set();
    if (m_backend != nullptr) {
        m_backend->Deinit();
        m_backend.reset();
    }
}

bool AudioSystem::Start() {
    if (m_backend == nullptr) {
        return false;
    }
    return m_backend->Start();
}

bool AudioSystem::Stop() {
    if (m_backend == nullptr) {
        return false;
    }
    const bool result = m_backend->Stop();
    m_bufferNotFullEvent.Set();
    return result;
}

bool AudioSystem::IsRunning() const {
    return m_backend != nullptr && m_backend->IsRunning();
}

bool AudioSystem::GetAudioStreamFormat(int *sampleRate, SDL_AudioFormat *format, int *channels) {
    if (m_backend == nullptr) {
        return false;
    }
    // Both backends play signed 16-bit PCM at the requested rate, so report the configured format directly
    // instead of asking the device.
    *sampleRate = m_sampleRate;
    *format = SDL_AUDIO_S16LE;
    *channels = m_channels;
    return true;
}

int AudioSystem::FillFrames(Sample *out, int frameCount) {
    if (m_silent) {
        std::memset(out, 0, static_cast<size_t>(frameCount) * sizeof(Sample));
        return frameCount;
    }

    const uint32 readPos = m_readPos.load(std::memory_order_acquire);
    const int len1 = std::min<int>(frameCount, m_buffer.size() - readPos);
    const int len2 = std::min<int>(frameCount - len1, readPos);
    std::copy(m_buffer.begin() + readPos, m_buffer.begin() + readPos + len1, out);
    std::copy(m_buffer.begin(), m_buffer.begin() + len2, out + len1);

    // Repeat from the start when the buffer did not hold enough frames. This is the crude stretching that sounds
    // acceptable when the emulator is not running at full speed.
    const int written = len1 + len2;
    for (int i = written; i < frameCount; ++i) {
        out[i] = out[i % (written > 0 ? written : 1)];
    }

    m_readPos.store((readPos + len1 + len2) % m_buffer.size(), std::memory_order_release);
    m_bufferNotFullEvent.Set();
    return frameCount;
}

void AudioSystem::ReceiveSample(sint16 left, sint16 right) {
    // If we're doing audio sync, wait until the buffer is no longer full.
    // Otherwise, simply overrun the buffer.
    while (m_sync && !m_silent) {
        const uint32 readPos = m_readPos.load(std::memory_order_acquire);
        const uint32 writePos = m_writePos.load(std::memory_order_relaxed);
        const uint32 count = (writePos >= readPos) ? (writePos - readPos) : (m_buffer.size() - (readPos - writePos));
        if (count < m_buffer.size() - 1) {
            break;
        }

        m_bufferNotFullEvent.Reset();

        const uint32 recheckReadPos = m_readPos.load(std::memory_order_acquire);
        const uint32 recheckCount = (writePos >= recheckReadPos) ? (writePos - recheckReadPos)
                                                                 : (m_buffer.size() - (recheckReadPos - writePos));
        if (recheckCount < m_buffer.size() - 1) {
            m_bufferNotFullEvent.Set();
            break;
        }

        m_bufferNotFullEvent.Wait();
    }

    const uint32 writePos = m_writePos.load(std::memory_order_relaxed);
    m_buffer[writePos] = {left, right};
    m_writePos.store((writePos + 1) % m_buffer.size(), std::memory_order_release);
}

void AudioSystem::UpdateGain() {
    if (m_backend != nullptr) {
        m_backend->SetGain(m_gain);
        m_backend->SetMute(m_mute);
    }
}

} // namespace app

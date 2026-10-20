#include "audio_system.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>

#if YMIR_PLATFORM_UWP
    // TEMPORARY boot tracing for the Xbox audio bring-up. Remove once startup is stable.
    #include <fmt/format.h>
    #define YMIR_AUDIO_TRACE(...) OutputDebugStringA(fmt::format("[Ymir audio] " __VA_ARGS__).c_str())
#else
    #define YMIR_AUDIO_TRACE(...)
#endif

#include <string>

#if YMIR_PLATFORM_UWP
    #include <SDL3/SDL_error.h>
    #include <SDL3/SDL_events.h>
    #include <SDL3/SDL_timer.h>

    #include <atomic>
    #include <chrono>
    #include <memory>
    #include <thread>
#endif

namespace app {

#if YMIR_PLATFORM_UWP
namespace {

/// @brief How long to wait for the audio device to open before giving up on UWP.
constexpr std::chrono::seconds kUWPAudioOpenTimeout{10};

/// @brief Opens the default playback device without blocking the UI thread.
///
/// On UWP, SDL_OpenAudioDeviceStream() blocks the calling thread while SDL's WASAPI management thread calls
/// ActivateAudioInterfaceAsync() and waits for its completion handler. Audio activation must be serviced by the app's
/// UI (ASTA) thread, which is the thread that runs main() and normally opens the device. With that thread blocked
/// waiting for SDL, the activation never completes and the app hangs on the splash screen.
///
/// The device is opened from a worker thread instead, while the UI thread keeps dispatching CoreWindow events through
/// SDL_PumpEvents() so that the activation can complete. If it still does not complete within the timeout, this gives
/// up with an error instead of hanging forever.
SDL_AudioStream *OpenAudioDeviceStreamWithoutBlockingUIThread(const SDL_AudioSpec &audioSpec,
                                                              SDL_AudioStreamCallback callback, void *userdata) {
    // Shared with the worker thread, which may outlive this function if the open times out
    struct OpenState {
        std::atomic<bool> done{false};
        SDL_AudioStream *stream = nullptr;
        std::string error;
    };
    auto state = std::make_shared<OpenState>();

    YMIR_AUDIO_TRACE("open-on-worker: starting worker thread\n");
    std::thread opener{[state, audioSpec, callback, userdata] {
        YMIR_AUDIO_TRACE("open-on-worker: worker calling SDL_OpenAudioDeviceStream\n");
        state->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, callback, userdata);
        YMIR_AUDIO_TRACE("open-on-worker: worker returned {}\n", state->stream != nullptr ? "a stream" : "nullptr");
        if (state->stream == nullptr) {
            // SDL errors are per-thread; copy it so the caller's thread can report it
            state->error = SDL_GetError();
        }
        state->done.store(true, std::memory_order_release);
    }};

    const auto deadline = std::chrono::steady_clock::now() + kUWPAudioOpenTimeout;
    int pumpIterations = 0;
    while (!state->done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        SDL_PumpEvents(); // dispatches CoreWindow events, which lets the audio activation complete
        SDL_Delay(1);
        ++pumpIterations;
    }
    YMIR_AUDIO_TRACE("open-on-worker: wait loop ended after {} pump iterations, done={}\n", pumpIterations,
                     state->done.load(std::memory_order_acquire));

    if (!state->done.load(std::memory_order_acquire)) {
        // The worker is stuck inside SDL; leave it be; it only touches the shared state it owns a reference to
        YMIR_AUDIO_TRACE("open-on-worker: TIMED OUT, detaching the worker thread\n");
        opener.detach();
        SDL_SetError("Timed out after %d seconds opening the default audio playback device",
                     static_cast<int>(kUWPAudioOpenTimeout.count()));
        return nullptr;
    }

    opener.join();
    if (state->stream == nullptr) {
        SDL_SetError("%s", state->error.c_str());
    }
    return state->stream;
}

} // namespace
#endif

bool AudioSystem::Init(int sampleRate, SDL_AudioFormat format, int channels, uint32 bufferSize) {
    {
        std::string bufferSizeStr = std::to_string(bufferSize);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, bufferSizeStr.c_str());
    }

    SDL_AudioSpec audioSpec{};
    audioSpec.freq = sampleRate;
    audioSpec.format = format;
    audioSpec.channels = channels;

    // Xbox/UWP: opening the default playback device can block while WASAPI negotiates the endpoint.
    // These traces (synchronous, debugger-only) pin down whether the block happens inside SDL or after it.
    YMIR_AUDIO_TRACE("Init(): opening default playback device (freq={} format={:04X} channels={})\n", sampleRate,
                     (unsigned)format, channels);
    const SDL_AudioStreamCallback callback = [](void *userdata, SDL_AudioStream *stream, int additional_amount,
                                                int total_amount) {
        static_cast<AudioSystem *>(userdata)->ProcessAudioCallback(stream, additional_amount, total_amount);
    };
#if YMIR_PLATFORM_UWP
    m_audioStream = OpenAudioDeviceStreamWithoutBlockingUIThread(audioSpec, callback, this);
#else
    m_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, callback, this);
#endif
    YMIR_AUDIO_TRACE("Init(): SDL_OpenAudioDeviceStream returned {}\n",
                     m_audioStream != nullptr ? "a stream" : "nullptr");

    if (m_audioStream == nullptr) {
        YMIR_AUDIO_TRACE("Init(): FAILED, SDL error: {}\n", SDL_GetError());
        return false;
    }
    UpdateGain();
    YMIR_AUDIO_TRACE("Init(): OK\n");
    return true;
}

void AudioSystem::Deinit() {
    m_bufferNotFullEvent.Set();
    if (m_audioStream != nullptr) {
        SDL_DestroyAudioStream(m_audioStream);
    }
}

bool AudioSystem::Start() {
    YMIR_AUDIO_TRACE("Start(): resuming stream device\n");
    if (SDL_ResumeAudioStreamDevice(m_audioStream)) {
        YMIR_AUDIO_TRACE("Start(): OK\n");
        m_running = true;
        return true;
    } else {
        return false;
    }
}

bool AudioSystem::Stop() {
    if (SDL_PauseAudioStreamDevice(m_audioStream)) {
        m_running = false;
        m_bufferNotFullEvent.Set();
        return true;
    } else {
        return false;
    }
}

bool AudioSystem::IsRunning() const {
    return m_running;
}

bool AudioSystem::GetAudioStreamFormat(int *sampleRate, SDL_AudioFormat *format, int *channels) {
    SDL_AudioSpec srcSpec{};
    SDL_AudioSpec dstSpec{};
    if (SDL_GetAudioStreamFormat(m_audioStream, &srcSpec, &dstSpec)) {
        *sampleRate = srcSpec.freq;
        *format = srcSpec.format;
        *channels = srcSpec.channels;
        return true;
    } else {
        return false;
    }
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
    SDL_SetAudioStreamGain(m_audioStream, m_mute ? 0.0f : m_gain);
}

void AudioSystem::ProcessAudioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) {
    const int sampleCount = additional_amount / sizeof(Sample);
    if (m_silent) {
        const sint16 zero = 0;
        for (int i = 0; i < sampleCount; i++) {
            SDL_PutAudioStreamData(stream, &zero, sizeof(zero));
        }
    } else {
        // Copy the requested amount of samples into the buffer, even if there aren't that many samples available.
        // This effectively implements a crude form of audio stretching that sounds acceptable when the emulator is not
        // running at 100% speed.
        const uint32 readPos = m_readPos.load(std::memory_order_acquire);
        const int len1 = std::min<int>(sampleCount, m_buffer.size() - readPos);
        const int len2 = std::min<int>(sampleCount - len1, readPos);
        SDL_PutAudioStreamData(stream, &m_buffer[readPos], len1 * sizeof(Sample));
        SDL_PutAudioStreamData(stream, &m_buffer[0], len2 * sizeof(Sample));

        m_readPos.store((readPos + len1 + len2) % m_buffer.size(), std::memory_order_release);
        m_bufferNotFullEvent.Set();
    }
}

} // namespace app

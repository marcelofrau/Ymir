#include "xaudio2_backend.hpp"

#include "audio_system.hpp"

#include <platform/uwp/uwp_platform.hpp>

#include <SDL3/SDL_error.h>

#include <fmt/format.h>

// SDL_LogInfo is not used for the diagnostics below: SDL filters the audio category at ERROR priority by default, so
// those lines never reached the log. platform::uwp::Trace writes them to the debugger and ymir-uwp.log directly.

#include <xaudio2.h>

#include <cstring>

namespace app {

/// @brief Receives XAudio2 voice callbacks so that finished buffers can be recycled.
///
/// Callbacks are invoked from an XAudio2-managed thread, hence the mutex in the owning backend.
class XAudio2Backend::VoiceCallback final : public IXAudio2VoiceCallback {
public:
    explicit VoiceCallback(XAudio2Backend &owner) noexcept : m_owner(owner) {}

    void OnVoiceProcessingPassStart(UINT32) override {}
    void OnVoiceProcessingPassEnd() override {}
    void OnStreamEnd() override {}
    void OnVoiceError(void *, HRESULT) override {}
    void OnBufferStart(void *) override {}
    void OnLoopEnd(void *) override {}

    void OnBufferEnd(void *context) override {
        auto *buffer = static_cast<BufferContext *>(context);
        if (buffer != nullptr && buffer->owner != nullptr) {
            buffer->owner->OnBufferEnd(buffer->index);
        }
    }

private:
    XAudio2Backend &m_owner;
};

XAudio2Backend::XAudio2Backend() noexcept : m_voiceCallback(new VoiceCallback(*this)) {}

XAudio2Backend::~XAudio2Backend() {
    Deinit();
    delete m_voiceCallback;
    m_voiceCallback = nullptr;
}

bool XAudio2Backend::Init(int sampleRate, int channels, uint32 bufferSize, RequestCallback onRequest,
                          void *userdata) {
    // bufferSize is only a hint for the SDL path; XAudio2 manages its own mixing buffer.
    (void)bufferSize;

    Deinit();

    m_sampleRate = sampleRate;
    m_channels = channels;
    m_onRequest = onRequest;
    m_userdata = userdata;

    std::lock_guard lock(m_mutex);

    if (FAILED(XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR)) || m_engine == nullptr) {
        m_engine = nullptr;
        SDL_SetError("Failed to create the XAudio2 engine");
        platform::uwp::Trace("[Ymir audio] XAudio2Create failed");
        return false;
    }
    platform::uwp::Trace("[Ymir audio] XAudio2 engine created");

    // CreateMasteringVoice infers the channel layout from InputChannels; there is no channel mask parameter in
    // this SDK version.
    if (FAILED(m_engine->CreateMasteringVoice(&m_masteringVoice, static_cast<UINT32>(m_channels),
                                             static_cast<UINT32>(m_sampleRate), 0, nullptr, nullptr,
                                             AudioCategory_GameEffects)) ||
        m_masteringVoice == nullptr) {
        m_masteringVoice = nullptr;
        ReleaseLocked();
        SDL_SetError("Failed to create the XAudio2 mastering voice");
        platform::uwp::Trace("[Ymir audio] CreateMasteringVoice failed");
        return false;
    }
    platform::uwp::Trace(
        fmt::format("[Ymir audio] XAudio2 mastering voice created ({} ch, {} Hz)", m_channels, m_sampleRate));

    // Samples are signed 16-bit PCM, matching what the emulator produces.
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(m_channels);
    format.nSamplesPerSec = static_cast<DWORD>(m_sampleRate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    if (FAILED(m_engine->CreateSourceVoice(&m_sourceVoice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                           m_voiceCallback)) ||
        m_sourceVoice == nullptr) {
        m_sourceVoice = nullptr;
        ReleaseLocked();
        SDL_SetError("Failed to create the XAudio2 source voice");
        platform::uwp::Trace("[Ymir audio] CreateSourceVoice failed");
        return false;
    }
    platform::uwp::Trace("[Ymir audio] XAudio2 source voice created");

    m_inFlight.fill(false);
    m_nextBuffer = 0;
    for (size_t i = 0; i < kBufferCount; ++i) {
        m_contexts[i] = {this, i};
    }

    UpdateVolumeLocked();
    return true;
}

void XAudio2Backend::Deinit() {
    std::lock_guard lock(m_mutex);
    ReleaseLocked();
    m_onRequest = nullptr;
    m_userdata = nullptr;
}

void XAudio2Backend::ReleaseLocked() noexcept {
    m_running.store(false, std::memory_order_relaxed);

    if (m_sourceVoice != nullptr) {
        m_sourceVoice->Stop(0);
        m_sourceVoice->FlushSourceBuffers();
        m_sourceVoice->DestroyVoice();
        m_sourceVoice = nullptr;
    }
    if (m_masteringVoice != nullptr) {
        m_masteringVoice->DestroyVoice();
        m_masteringVoice = nullptr;
    }
    if (m_engine != nullptr) {
        m_engine->Release();
        m_engine = nullptr;
    }

    m_inFlight.fill(false);
    m_nextBuffer = 0;
}

bool XAudio2Backend::Start() {
    std::lock_guard lock(m_mutex);
    if (m_sourceVoice == nullptr) {
        return false;
    }
    if (FAILED(m_sourceVoice->Start(0))) {
        return false;
    }

    // Queue an initial set of buffers so the voice has something to play before the first callback arrives.
    for (size_t i = 0; i < kBufferCount; ++i) {
        RefillLocked();
    }

    m_running.store(true, std::memory_order_relaxed);
    platform::uwp::Trace("[Ymir audio] XAudio2 playback started");
    return true;
}

bool XAudio2Backend::Stop() {
    std::lock_guard lock(m_mutex);
    if (m_sourceVoice == nullptr) {
        return false;
    }
    m_sourceVoice->Stop(0);
    m_sourceVoice->FlushSourceBuffers();
    m_running.store(false, std::memory_order_relaxed);
    m_inFlight.fill(false);
    m_nextBuffer = 0;
    return true;
}

bool XAudio2Backend::IsRunning() const {
    return m_running.load(std::memory_order_relaxed);
}

void XAudio2Backend::SetGain(float gain) {
    std::lock_guard lock(m_mutex);
    m_gain = gain;
    UpdateVolumeLocked();
}

void XAudio2Backend::SetMute(bool mute) {
    std::lock_guard lock(m_mutex);
    m_mute = mute;
    UpdateVolumeLocked();
}

void XAudio2Backend::UpdateVolumeLocked() {
    if (m_sourceVoice != nullptr) {
        m_sourceVoice->SetVolume(m_mute ? 0.0f : m_gain);
    }
}

void XAudio2Backend::OnBufferEnd(size_t index) noexcept {
    std::lock_guard lock(m_mutex);
    if (index < kBufferCount) {
        m_inFlight[index] = false;
    }

    // XAudio2 finished a buffer, so the voice is about to run dry unless another one is queued now.
    if (m_running.load(std::memory_order_relaxed)) {
        RefillLocked();
    }
}

void XAudio2Backend::RefillLocked() {
    if (m_sourceVoice == nullptr || m_onRequest == nullptr) {
        return;
    }

    // Find a buffer that is not currently queued on the voice.
    size_t slot = kBufferCount;
    for (size_t i = 0; i < kBufferCount; ++i) {
        const size_t candidate = (m_nextBuffer + i) % kBufferCount;
        if (!m_inFlight[candidate]) {
            slot = candidate;
            break;
        }
    }
    if (slot == kBufferCount) {
        // Every buffer is queued; wait for the next completion callback instead of blocking.
        return;
    }

    const int written = m_onRequest(m_userdata, m_buffers[slot].data(), static_cast<int>(kFramesPerBuffer));
    if (written <= 0) {
        return;
    }

    // Silence the tail of a short read so the voice still receives a full buffer.
    if (written < static_cast<int>(kFramesPerBuffer)) {
        std::memset(m_buffers[slot].data() + written, 0,
                    (kFramesPerBuffer - static_cast<size_t>(written)) * sizeof(Sample));
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(kFramesPerBuffer * static_cast<size_t>(m_channels) * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(m_buffers[slot].data());
    buffer.pContext = &m_contexts[slot];

    if (FAILED(m_sourceVoice->SubmitSourceBuffer(&buffer))) {
        return;
    }

    m_inFlight[slot] = true;
    m_nextBuffer = (slot + 1) % kBufferCount;
}

} // namespace app

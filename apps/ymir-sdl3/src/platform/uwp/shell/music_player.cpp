#include "music_player.hpp"

#include "file_system.hpp"
#include "shell_log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace platform::uwp::shell {

namespace {

    /// @brief Fade duration, in seconds.
    constexpr float kFadeSeconds = 1.0f;

    /// @brief Biggest music file accepted; a 10-minute FLAC is about 100 MB.
    constexpr std::uint64_t kMaxTrackBytes = 256ull * 1024 * 1024;

    /// @brief Decoded source frames per decoding step.
    constexpr std::size_t kDecodeFrames = 2048;

    /// @brief Cutoff of the low-pass filter that isolates the bass the cubes wobble with, in Hz.
    constexpr float kBassCutoffHz = 150.0f;

} // namespace

MusicPlayer::~MusicPlayer() {
    m_decodeThread = {}; // requests the stop and joins
    m_backend.Deinit();
}

bool MusicPlayer::Start(const std::filesystem::path &folder, bool shuffle) {
    if (m_started) {
        return !m_tracks.empty();
    }
    m_started = true;
    m_shuffle = shuffle;

    std::vector<fs::DirectoryEntry> entries;
    if (fs::ListDirectory(folder, entries)) {
        for (const fs::DirectoryEntry &entry : entries) {
            if (!entry.isDirectory && IsSupportedMusicExtension(entry.path.extension().string())) {
                m_tracks.push_back(entry.path);
            }
        }
    }
    std::sort(m_tracks.begin(), m_tracks.end());
    devlog::info<grp::shell>("Background music: {} tracks in {}", m_tracks.size(), folder);
    if (m_tracks.empty()) {
        return false;
    }

    m_order.resize(m_tracks.size());
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        m_order[i] = i;
    }
    if (m_shuffle) {
        std::shuffle(m_order.begin(), m_order.end(), m_rng);
    }

    if (!m_backend.Init(kOutputRate, 2, 512, &MusicPlayer::OnRequest, this)) {
        devlog::warn<grp::shell>("Background music disabled: could not open the audio output");
        m_tracks.clear();
        return false;
    }

    m_decodeThread = std::jthread{[this](std::stop_token stopToken) { DecodeLoop(stopToken); }};
    return true;
}

void MusicPlayer::SetPlaying(bool playing) {
    if (m_tracks.empty()) {
        return;
    }
    m_playing = playing;
    m_targetGain = playing ? 1.0f : 0.0f;
    if (playing && !m_backendRunning) {
        m_backendRunning = m_backend.Start();
    }
}

void MusicPlayer::SetVolume(float volume) {
    m_backend.SetGain(std::clamp(volume, 0.0f, 1.0f));
}

void MusicPlayer::Update() {
    // Once the fade out is over the voice only outputs silence; pausing it saves the mixing work while in game
    if (m_backendRunning && !m_playing && m_gain.load(std::memory_order_relaxed) <= 0.0f) {
        m_backend.Stop();
        m_backendRunning = false;
        for (int channel = 0; channel < 2; ++channel) {
            m_rms[channel] = 0.0f;
            m_bass[channel] = 0.0f;
        }
    }
}

std::optional<std::string> MusicPlayer::TakeTrackChange() {
    std::scoped_lock lock{m_trackMutex};
    std::optional<std::string> change;
    change.swap(m_trackChange);
    return change;
}

AudioLevels MusicPlayer::Levels(int channel) const {
    channel = std::clamp(channel, 0, 1);
    return {m_rms[channel].load(std::memory_order_relaxed), m_bass[channel].load(std::memory_order_relaxed)};
}

int MusicPlayer::OnRequest(void *userdata, app::Sample *out, int frameCount) {
    return static_cast<MusicPlayer *>(userdata)->Fill(out, frameCount);
}

int MusicPlayer::Fill(app::Sample *out, int frameCount) {
    float gain = m_gain.load(std::memory_order_relaxed);
    const float target = m_targetGain.load(std::memory_order_relaxed);

    // Fully faded out: output silence without consuming the buffer, so playback resumes where it stopped
    if (gain <= 0.0f && target <= 0.0f) {
        std::fill_n(out, frameCount, app::Sample{0, 0});
        return frameCount;
    }

    const std::size_t readPos = m_readPos.load(std::memory_order_relaxed);
    const std::size_t writePos = m_writePos.load(std::memory_order_acquire);
    const std::size_t available = writePos - readPos;
    const std::size_t frames = std::min<std::size_t>(available, static_cast<std::size_t>(frameCount));

    const float gainStep = 1.0f / (kFadeSeconds * static_cast<float>(kOutputRate));
    const float lowPassAlpha =
        1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * kBassCutoffHz / static_cast<float>(kOutputRate));
    float sumSquares[2]{0.0f, 0.0f};
    float bassSquares[2]{0.0f, 0.0f};

    for (std::size_t i = 0; i < frames; ++i) {
        gain = gain < target ? std::min(gain + gainStep, target) : std::max(gain - gainStep, target);
        const app::Sample source = m_ring[(readPos + i) % kRingFrames];
        const float in[2]{source.left / 32768.0f * gain, source.right / 32768.0f * gain};
        for (int channel = 0; channel < 2; ++channel) {
            sumSquares[channel] += in[channel] * in[channel];
            m_lowPass[channel] += (in[channel] - m_lowPass[channel]) * lowPassAlpha;
            bassSquares[channel] += m_lowPass[channel] * m_lowPass[channel];
        }
        out[i] = {static_cast<sint16>(std::clamp(in[0] * 32767.0f, -32768.0f, 32767.0f)),
                  static_cast<sint16>(std::clamp(in[1] * 32767.0f, -32768.0f, 32767.0f))};
    }
    m_readPos.store(readPos + frames, std::memory_order_release);
    m_gain.store(gain, std::memory_order_relaxed);

    // A buffer underrun (the decoder fell behind, or between tracks) is padded with silence
    std::fill(out + frames, out + frameCount, app::Sample{0, 0});

    if (frames > 0) {
        for (int channel = 0; channel < 2; ++channel) {
            m_rms[channel].store(std::sqrt(sumSquares[channel] / frames), std::memory_order_relaxed);
            m_bass[channel].store(std::sqrt(bassSquares[channel] / frames), std::memory_order_relaxed);
        }
    }
    return frameCount;
}

void MusicPlayer::DecodeLoop(std::stop_token stopToken) {
    std::vector<app::Sample> chunk(1024);
    while (!stopToken.stop_requested()) {
        const std::size_t readPos = m_readPos.load(std::memory_order_acquire);
        const std::size_t writePos = m_writePos.load(std::memory_order_relaxed);
        const std::size_t freeFrames = kRingFrames - (writePos - readPos);

        // Idle once a fade out is over (the voice stops consuming then) or while the buffer is full enough. Decoding
        // continues during the fade out itself, so it does not run dry before the music has gone silent
        const bool silent = !m_playing && m_gain.load(std::memory_order_relaxed) <= 0.0f;
        if (silent || freeFrames < chunk.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        const std::size_t produced = Produce(chunk.data(), chunk.size());
        if (produced == 0) {
            // Every track failed to open; do not spin
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        for (std::size_t i = 0; i < produced; ++i) {
            m_ring[(writePos + i) % kRingFrames] = chunk[i];
        }
        m_writePos.store(writePos + produced, std::memory_order_release);
    }
}

bool MusicPlayer::OpenNextTrack() {
    for (std::size_t attempt = 0; attempt < m_order.size(); ++attempt) {
        if (m_orderPos >= m_order.size()) {
            m_orderPos = 0;
            if (m_shuffle) {
                std::shuffle(m_order.begin(), m_order.end(), m_rng);
            }
        }
        const std::filesystem::path &path = m_tracks[m_order[m_orderPos++]];

        auto data = fs::ReadWholeFile(path, kMaxTrackBytes);
        if (!data) {
            devlog::warn<grp::shell>("Background music: could not read {}", path);
            continue;
        }
        m_decoder = OpenAudioDecoder(std::make_shared<const std::string>(std::move(*data)), path.extension().string());
        if (m_decoder == nullptr) {
            devlog::warn<grp::shell>("Background music: could not decode {}", path);
            continue;
        }

        m_step = static_cast<double>(m_decoder->SampleRate()) / static_cast<double>(kOutputRate);
        m_source.clear();
        m_sourceStart = 0;
        m_sourcePos = 0.0;
        devlog::info<grp::shell>("Background music: playing {}", path);
        {
            std::scoped_lock lock{m_trackMutex};
            const std::u8string stem = path.stem().u8string();
            m_trackChange = std::string{stem.begin(), stem.end()};
        }
        return true;
    }
    m_decoder.reset();
    return false;
}

std::size_t MusicPlayer::Produce(app::Sample *out, std::size_t frames) {
    std::size_t produced = 0;
    while (produced < frames) {
        if (m_decoder == nullptr && !OpenNextTrack()) {
            return produced;
        }

        // Linear resampling needs the source frame at the current position and the next one
        const auto index = static_cast<std::size_t>(m_sourcePos);
        const std::size_t sourceFrames = m_source.size() / 2 - m_sourceStart;
        if (index + 1 >= sourceFrames) {
            // Drop the frames already consumed, then decode more
            m_source.erase(m_source.begin(), m_source.begin() + static_cast<std::ptrdiff_t>(m_sourceStart * 2));
            m_sourceStart = 0;
            const std::size_t oldSize = m_source.size();
            m_source.resize(oldSize + kDecodeFrames * 2);
            const std::size_t decoded = m_decoder->Read(m_source.data() + oldSize, kDecodeFrames);
            m_source.resize(oldSize + decoded * 2);
            if (decoded == 0) {
                // End of the track: move on to the next one
                m_decoder.reset();
                m_source.clear();
                m_sourcePos = 0.0;
            }
            continue;
        }

        const float frac = static_cast<float>(m_sourcePos - static_cast<double>(index));
        const float *a = &m_source[(m_sourceStart + index) * 2];
        const float *b = a + 2;
        const float left = a[0] + (b[0] - a[0]) * frac;
        const float right = a[1] + (b[1] - a[1]) * frac;
        out[produced++] = {static_cast<sint16>(std::clamp(left, -1.0f, 1.0f) * 32767.0f),
                           static_cast<sint16>(std::clamp(right, -1.0f, 1.0f) * 32767.0f)};

        m_sourcePos += m_step;
        const auto consumed = static_cast<std::size_t>(m_sourcePos);
        if (consumed > 0) {
            m_sourceStart += consumed;
            m_sourcePos -= static_cast<double>(consumed);
        }
    }
    return produced;
}

} // namespace platform::uwp::shell

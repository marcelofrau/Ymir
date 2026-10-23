#include "music_player.hpp"

#include "file_system.hpp"
#include "shell_log.hpp"

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

CMRC_DECLARE(Ymir_sdl3_shell_rc);

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

    struct EmbeddedTrackSpec {
        const char *file;
        const char *title;
    };

    /// @brief Embedded BGM playlist played in sequence and looped.
    constexpr std::array<EmbeddedTrackSpec, 3> kEmbeddedTracks{{
        {"bgm_01_flight_episode_1.mp3", "Flight - Episode 1"},
        {"bgm_02_grandia_mullen.mp3", "Grandia - Mullen"},
        {"bgm_03_grandia_d2_080.mp3", "Grandia - D2 080"},
    }};

    constexpr std::array<const char *, 3> kEmbeddedSfxFiles{{
        "sfx_boot.mp3",
        "sfx_scroll.mp3",
        "sfx_select.mp3",
    }};

    /// @brief Decodes an embedded MP3 clip into 48 kHz stereo PCM samples.
    std::vector<app::Sample> DecodeEmbeddedClip(std::string_view fileName) {
        auto embedfs = cmrc::Ymir_sdl3_shell_rc::get_filesystem();
        const std::string name{fileName};
        if (!embedfs.exists(name)) {
            return {};
        }
        const cmrc::file file = embedfs.open(name);
        auto data = std::make_shared<const std::string>(file.begin(), file.end());
        std::unique_ptr<AudioDecoder> decoder = OpenAudioDecoder(std::move(data), ".mp3");
        if (decoder == nullptr) {
            return {};
        }

        std::vector<float> source;
        std::vector<float> chunk(kDecodeFrames * 2);
        while (true) {
            const std::size_t read = decoder->Read(chunk.data(), kDecodeFrames);
            if (read == 0) {
                break;
            }
            source.insert(source.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read * 2));
        }
        const std::size_t sourceFrames = source.size() / 2;
        if (sourceFrames == 0) {
            return {};
        }

        const double step = static_cast<double>(decoder->SampleRate()) / static_cast<double>(MusicPlayer::kOutputRate);
        const auto outFrames = static_cast<std::size_t>(static_cast<double>(sourceFrames) / step);
        std::vector<app::Sample> out;
        out.reserve(outFrames);

        for (double pos = 0.0; static_cast<std::size_t>(pos) + 1 < sourceFrames; pos += step) {
            const auto index = static_cast<std::size_t>(pos);
            const float frac = static_cast<float>(pos - static_cast<double>(index));
            const float *a = &source[index * 2];
            const float *b = a + 2;
            const float left = a[0] + (b[0] - a[0]) * frac;
            const float right = a[1] + (b[1] - a[1]) * frac;
            out.push_back({static_cast<sint16>(std::clamp(left, -1.0f, 1.0f) * 32767.0f),
                           static_cast<sint16>(std::clamp(right, -1.0f, 1.0f) * 32767.0f)});
        }
        return out;
    }

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

    // Decode UI sound effects into memory so button presses trigger with zero latency
    for (std::size_t i = 0; i < kSfxCount; ++i) {
        m_sfxSamples[i] = DecodeEmbeddedClip(kEmbeddedSfxFiles[i]);
    }

    // Built-in BGM playlist in sequence
    auto embedfs = cmrc::Ymir_sdl3_shell_rc::get_filesystem();
    for (const EmbeddedTrackSpec &spec : kEmbeddedTracks) {
        if (embedfs.exists(spec.file)) {
            m_tracks.push_back(Track{.title = spec.title, .embeddedName = spec.file, .filePath = {}});
        }
    }

    // Optional extra tracks placed by the user in <profile>/music/
    std::vector<fs::DirectoryEntry> entries;
    std::vector<std::filesystem::path> userFiles;
    if (fs::ListDirectory(folder, entries)) {
        for (const fs::DirectoryEntry &entry : entries) {
            if (!entry.isDirectory && IsSupportedMusicExtension(entry.path.extension().string())) {
                userFiles.push_back(entry.path);
            }
        }
    }
    std::sort(userFiles.begin(), userFiles.end());
    for (std::filesystem::path &path : userFiles) {
        const std::u8string stem = path.stem().u8string();
        m_tracks.push_back(Track{
            .title = std::string{stem.begin(), stem.end()},
            .embeddedName = {},
            .filePath = std::move(path),
        });
    }

    devlog::info<grp::shell>("Background music: {} tracks ({} custom in {})", m_tracks.size(), userFiles.size(),
                             folder);

    m_order.resize(m_tracks.size());
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        m_order[i] = i;
    }
    if (m_shuffle && m_order.size() > 1) {
        std::shuffle(m_order.begin(), m_order.end(), m_rng);
    }

    if (!m_backend.Init(kOutputRate, 2, 512, &MusicPlayer::OnRequest, this)) {
        devlog::warn<grp::shell>("Background music disabled: could not open the audio output");
        m_tracks.clear();
        return false;
    }
    m_backend.SetGain(1.0f);

    if (!m_tracks.empty()) {
        m_decodeThread = std::jthread{[this](std::stop_token stopToken) { DecodeLoop(stopToken); }};
    }
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

void MusicPlayer::PlaySfx(Sfx sfx) {
    const auto idx = static_cast<std::size_t>(sfx);
    if (idx >= kSfxCount || m_sfxSamples[idx].empty()) {
        return;
    }
    // Launching a game plays the select chime instead of the button scroll sound
    if (sfx == Sfx::Select) {
        m_sfxPos[static_cast<std::size_t>(Sfx::Scroll)].store(kSfxInactive, std::memory_order_relaxed);
    }
    m_sfxPos[idx].store(0, std::memory_order_release);
    if (!m_backendRunning) {
        m_backendRunning = m_backend.Start();
    }
}

void MusicPlayer::SetVolume(float volume) {
    m_musicVolume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool MusicPlayer::HasActiveSfx() const {
    for (std::size_t i = 0; i < kSfxCount; ++i) {
        if (m_sfxPos[i].load(std::memory_order_relaxed) < m_sfxSamples[i].size()) {
            return true;
        }
    }
    return false;
}

void MusicPlayer::Update() {
    // Once the fade out is over and no SFX is playing, the voice only outputs silence; pausing it saves mixing work
    if (m_backendRunning && !m_playing && m_gain.load(std::memory_order_relaxed) <= 0.0f && !HasActiveSfx()) {
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
    const float musicVolume = m_musicVolume.load(std::memory_order_relaxed);
    const bool bgmActive = gain > 0.0f || target > 0.0f;

    std::array<std::size_t, kSfxCount> sfxPos{};
    bool anySfx = false;
    for (std::size_t s = 0; s < kSfxCount; ++s) {
        sfxPos[s] = m_sfxPos[s].load(std::memory_order_acquire);
        if (sfxPos[s] < m_sfxSamples[s].size()) {
            anySfx = true;
        }
    }

    // Fully faded out and no SFX playing: output silence without consuming the BGM buffer
    if (!bgmActive && !anySfx) {
        std::fill_n(out, frameCount, app::Sample{0, 0});
        return frameCount;
    }

    const std::size_t readPos = m_readPos.load(std::memory_order_relaxed);
    const std::size_t writePos = m_writePos.load(std::memory_order_acquire);
    const std::size_t available = bgmActive ? (writePos - readPos) : 0;
    const std::size_t bgmFrames = std::min<std::size_t>(available, static_cast<std::size_t>(frameCount));

    const float gainStep = 1.0f / (kFadeSeconds * static_cast<float>(kOutputRate));
    const float lowPassAlpha =
        1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * kBassCutoffHz / static_cast<float>(kOutputRate));
    float sumSquares[2]{0.0f, 0.0f};
    float bassSquares[2]{0.0f, 0.0f};

    const auto totalFrames = static_cast<std::size_t>(frameCount);
    for (std::size_t i = 0; i < totalFrames; ++i) {
        float outSample[2]{0.0f, 0.0f};
        float meterSample[2]{0.0f, 0.0f};
        if (i < bgmFrames) {
            gain = gain < target ? std::min(gain + gainStep, target) : std::max(gain - gainStep, target);
            const app::Sample source = m_ring[(readPos + i) % kRingFrames];
            const float bgmLeft = (source.left / 32768.0f) * gain;
            const float bgmRight = (source.right / 32768.0f) * gain;
            constexpr float kBgmMasterGain = 0.5f;
            outSample[0] += bgmLeft * musicVolume * kBgmMasterGain;
            outSample[1] += bgmRight * musicVolume * kBgmMasterGain;
            meterSample[0] += bgmLeft;
            meterSample[1] += bgmRight;
        }
        for (std::size_t s = 0; s < kSfxCount; ++s) {
            if (sfxPos[s] < m_sfxSamples[s].size()) {
                const app::Sample sfxSample = m_sfxSamples[s][sfxPos[s]++];
                const float sfxLeft = sfxSample.left / 32768.0f;
                const float sfxRight = sfxSample.right / 32768.0f;
                const float sfxGain = (s == static_cast<std::size_t>(Sfx::Boot)) ? 0.85f : 0.18f;
                outSample[0] += sfxLeft * sfxGain;
                outSample[1] += sfxRight * sfxGain;
                meterSample[0] += sfxLeft;
                meterSample[1] += sfxRight;
            }
        }
        for (int channel = 0; channel < 2; ++channel) {
            outSample[channel] = std::clamp(outSample[channel], -1.0f, 1.0f);
            meterSample[channel] = std::clamp(meterSample[channel], -1.0f, 1.0f);
            sumSquares[channel] += meterSample[channel] * meterSample[channel];
            m_lowPass[channel] += (meterSample[channel] - m_lowPass[channel]) * lowPassAlpha;
            bassSquares[channel] += m_lowPass[channel] * m_lowPass[channel];
        }
        out[i] = {static_cast<sint16>(outSample[0] * 32767.0f), static_cast<sint16>(outSample[1] * 32767.0f)};
    }

    if (bgmFrames > 0) {
        m_readPos.store(readPos + bgmFrames, std::memory_order_release);
        m_gain.store(gain, std::memory_order_relaxed);
    }
    for (std::size_t s = 0; s < kSfxCount; ++s) {
        const std::size_t next = sfxPos[s] < m_sfxSamples[s].size() ? sfxPos[s] : kSfxInactive;
        m_sfxPos[s].store(next, std::memory_order_release);
    }

    if (totalFrames > 0) {
        for (int channel = 0; channel < 2; ++channel) {
            m_rms[channel].store(std::sqrt(sumSquares[channel] / totalFrames), std::memory_order_relaxed);
            m_bass[channel].store(std::sqrt(bassSquares[channel] / totalFrames), std::memory_order_relaxed);
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
    auto embedfs = cmrc::Ymir_sdl3_shell_rc::get_filesystem();
    for (std::size_t attempt = 0; attempt < m_order.size(); ++attempt) {
        if (m_orderPos >= m_order.size()) {
            m_orderPos = 0;
            if (m_shuffle && m_order.size() > 1) {
                std::shuffle(m_order.begin(), m_order.end(), m_rng);
            }
        }
        const Track &track = m_tracks[m_order[m_orderPos++]];

        std::shared_ptr<const std::string> trackBytes;
        std::string extension = ".mp3";
        if (!track.embeddedName.empty()) {
            if (!embedfs.exists(track.embeddedName)) {
                continue;
            }
            const cmrc::file file = embedfs.open(track.embeddedName);
            trackBytes = std::make_shared<const std::string>(file.begin(), file.end());
        } else {
            auto data = fs::ReadWholeFile(track.filePath, kMaxTrackBytes);
            if (!data) {
                devlog::warn<grp::shell>("Background music: could not read {}", track.filePath);
                continue;
            }
            trackBytes = std::make_shared<const std::string>(std::move(*data));
            extension = track.filePath.extension().string();
        }

        m_decoder = OpenAudioDecoder(std::move(trackBytes), extension);
        if (m_decoder == nullptr) {
            devlog::warn<grp::shell>("Background music: could not decode {}", track.title);
            continue;
        }

        m_step = static_cast<double>(m_decoder->SampleRate()) / static_cast<double>(kOutputRate);
        m_source.clear();
        m_sourceStart = 0;
        m_sourcePos = 0.0;
        devlog::info<grp::shell>("Background music: playing {}", track.title);
        {
            std::scoped_lock lock{m_trackMutex};
            m_trackChange = track.title;
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

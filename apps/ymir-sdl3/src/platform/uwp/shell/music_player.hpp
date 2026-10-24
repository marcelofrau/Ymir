#pragma once

/**
@file
@brief Background music of the Xbox shell.

Plays the tracks found in `<profile>/music/` (OGG, MP3, FLAC or WAV) in a loop while the shell screens are shown,
fading in and out as the player moves between the shell and the game. The tracks are supplied by the user: they are
never part of the repository or of the package.

The music has its own XAudio2 output (a second `app::XAudio2Backend`), separate from the emulator's, so the
emulator's audio path is not touched. The shell pauses the emulation on its screens, so the two never play at once.

Threads:
- a decoder thread reads the tracks, resamples them to the output rate and fills a ring buffer;
- the XAudio2 voice thread pulls from the ring buffer, applies the fades and measures the levels the
  audio-reactive cubes follow;
- the GUI thread starts and stops playback and reads the levels.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <platform/uwp/shell/audio_cube.hpp>
#include <platform/uwp/shell/audio_decoders.hpp>

#include <app/xaudio2_backend.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace platform::uwp::shell {

/// @brief Sound effects of the Xbox shell, sampled from the Sega Saturn BIOS menu.
enum class Sfx : std::size_t {
    Boot = 0,   // played once when the application opens
    Scroll = 1, // played on button presses and focus navigation
    Select = 2, // played when launching a game
    Count = 3,
};

/// @brief Plays the background music and UI sound effects of the shell.
class MusicPlayer {
public:
    /// @brief Output sample rate; tracks at other rates are resampled.
    static constexpr int kOutputRate = 48000;

    MusicPlayer() = default;
    ~MusicPlayer();

    MusicPlayer(const MusicPlayer &) = delete;
    MusicPlayer &operator=(const MusicPlayer &) = delete;

    /// @brief Loads the embedded SFX and BGM tracks (plus any custom tracks in `folder`) and opens the audio output.
    /// @param[in] folder the folder with optional extra music files (not searched recursively)
    /// @param[in] shuffle whether to play the tracks in random order instead of sequentially
    /// @return `true` if the audio output is open
    bool Start(const std::filesystem::path &folder, bool shuffle);

    /// @brief Starts or stops BGM playback with a fade. Stopping keeps the position, so playback resumes where it was.
    void SetPlaying(bool playing);

    /// @brief Triggers a UI sound effect immediately.
    void PlaySfx(Sfx sfx);

    /// @brief Sets the music volume, in [0, 1].
    void SetVolume(float volume);

    /// @brief Housekeeping on the GUI thread: pauses the output once fade out and active SFX are complete.
    void Update();

    /// @brief Retrieves the title of the track that started since the last call, if any.
    [[nodiscard]] std::optional<std::string> TakeTrackChange();

    /// @brief Retrieves the current levels of a channel.
    /// @param[in] channel 0 for left, 1 for right
    [[nodiscard]] AudioLevels Levels(int channel) const;

    /// @brief Retrieves how many tracks were found.
    [[nodiscard]] std::size_t TrackCount() const {
        return m_tracks.size();
    }

private:
    struct Track {
        std::string title;
        std::string embeddedName;       // non-empty for tracks embedded in ymir-sdl3-shell-rc
        std::filesystem::path filePath; // non-empty for custom tracks in <profile>/music/
    };

    static int OnRequest(void *userdata, app::Sample *out, int frameCount);
    int Fill(app::Sample *out, int frameCount);
    [[nodiscard]] bool HasActiveSfx() const;

    void DecodeLoop(std::stop_token stopToken);
    bool OpenNextTrack();
    std::size_t Produce(app::Sample *out, std::size_t frames);

    // Track list, written once by Start() before the decoder thread exists
    std::vector<Track> m_tracks;
    std::vector<std::size_t> m_order;
    std::size_t m_orderPos = 0;
    bool m_shuffle = false;
    std::minstd_rand m_rng{std::random_device{}()};

    // Pre-decoded UI sound effects at kOutputRate
    static constexpr std::size_t kSfxCount = static_cast<std::size_t>(Sfx::Count);
    static constexpr std::size_t kSfxInactive = ~std::size_t{0};
    std::array<std::vector<app::Sample>, kSfxCount> m_sfxSamples{};
    std::array<std::atomic<std::size_t>, kSfxCount> m_sfxPos{kSfxInactive, kSfxInactive, kSfxInactive};

    // Decoder thread state
    std::unique_ptr<AudioDecoder> m_decoder;
    std::vector<float> m_source;   // decoded stereo frames not yet resampled
    std::size_t m_sourceStart = 0; // first frame of m_source still needed
    double m_sourcePos = 0.0;      // resampling position, in source frames from m_sourceStart
    double m_step = 1.0;           // source frames per output frame
    int m_failedOpens = 0;

    // Single-producer single-consumer ring buffer between the decoder and the voice thread
    static constexpr std::size_t kRingFrames = 16384;
    std::vector<app::Sample> m_ring = std::vector<app::Sample>(kRingFrames);
    std::atomic<std::size_t> m_readPos{0};
    std::atomic<std::size_t> m_writePos{0};

    // Fades and BGM volume. The target is set by the GUI thread; the gain ramps per sample on the voice thread
    std::atomic<float> m_targetGain{0.0f};
    std::atomic<float> m_gain{0.0f};
    std::atomic<float> m_musicVolume{0.6f};
    std::atomic_bool m_playing{false};

    // Levels measured on the voice thread
    std::atomic<float> m_rms[2]{0.0f, 0.0f};
    std::atomic<float> m_bass[2]{0.0f, 0.0f};
    float m_lowPass[2]{0.0f, 0.0f};

    std::mutex m_trackMutex;
    std::optional<std::string> m_trackChange;

    app::XAudio2Backend m_backend;
    bool m_started = false;
    bool m_backendRunning = false;

    std::jthread m_decodeThread;
};

} // namespace platform::uwp::shell

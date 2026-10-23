#include "audio_decoders.hpp"

// dr_mp3's implementation is already compiled into ymir-core (the CD image loader decodes MP3 tracks) and dr_flac's
// into libchdr (CHD images compress audio tracks with FLAC; same dr_flac version, 0.13.4). dr_wav is only used here,
// so its implementation lives in this file
#include <dr_libs/dr_mp3.h>

#include <dr_libs/dr_flac.h>

#define DR_WAV_IMPLEMENTATION
#include <dr_libs/dr_wav.h>

// The stb target defines STB_VORBIS_HEADER_ONLY publicly, so this only brings in the declarations
#include <stb_vorbis.c>

#include <algorithm>
#include <cctype>
#include <vector>

namespace platform::uwp::shell {

namespace {

    std::string Lowercase(std::string_view text) {
        std::string out{text};
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    /// @brief Converts frames with any channel count to stereo, in place in `out`.
    void ToStereo(const float *in, std::size_t frames, int channels, float *out) {
        for (std::size_t i = 0; i < frames; ++i) {
            const float left = in[i * channels];
            const float right = channels >= 2 ? in[i * channels + 1] : left;
            out[i * 2] = left;
            out[i * 2 + 1] = right;
        }
    }

    /// @brief Shared plumbing: decoders that read N-channel float frames into a scratch buffer.
    class ChannelDecoder : public AudioDecoder {
    public:
        explicit ChannelDecoder(std::shared_ptr<const std::string> data)
            : m_data(std::move(data)) {}

        int SampleRate() const final {
            return m_sampleRate;
        }

        std::size_t Read(float *out, std::size_t frames) final {
            if (m_channels == 2) {
                return ReadFrames(out, frames);
            }
            m_scratch.resize(frames * static_cast<std::size_t>(m_channels));
            const std::size_t read = ReadFrames(m_scratch.data(), frames);
            ToStereo(m_scratch.data(), read, m_channels, out);
            return read;
        }

    protected:
        virtual std::size_t ReadFrames(float *out, std::size_t frames) = 0;

        std::shared_ptr<const std::string> m_data;
        int m_sampleRate = 44100;
        int m_channels = 2;

    private:
        std::vector<float> m_scratch;
    };

    class VorbisDecoder final : public ChannelDecoder {
    public:
        explicit VorbisDecoder(std::shared_ptr<const std::string> data)
            : ChannelDecoder(std::move(data)) {
            int error = 0;
            m_vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char *>(m_data->data()),
                                              static_cast<int>(m_data->size()), &error, nullptr);
            if (m_vorbis != nullptr) {
                const stb_vorbis_info info = stb_vorbis_get_info(m_vorbis);
                m_sampleRate = static_cast<int>(info.sample_rate);
                m_channels = std::max(info.channels, 1);
            }
        }
        ~VorbisDecoder() override {
            if (m_vorbis != nullptr) {
                stb_vorbis_close(m_vorbis);
            }
        }
        bool Valid() const {
            return m_vorbis != nullptr;
        }

    private:
        std::size_t ReadFrames(float *out, std::size_t frames) override {
            const int read = stb_vorbis_get_samples_float_interleaved(m_vorbis, m_channels, out,
                                                                      static_cast<int>(frames) * m_channels);
            return static_cast<std::size_t>(std::max(read, 0));
        }

        stb_vorbis *m_vorbis = nullptr;
    };

    class Mp3Decoder final : public ChannelDecoder {
    public:
        explicit Mp3Decoder(std::shared_ptr<const std::string> data)
            : ChannelDecoder(std::move(data)) {
            m_valid = drmp3_init_memory(&m_mp3, m_data->data(), m_data->size(), nullptr) != 0;
            if (m_valid) {
                m_sampleRate = static_cast<int>(m_mp3.sampleRate);
                m_channels = std::max(static_cast<int>(m_mp3.channels), 1);
            }
        }
        ~Mp3Decoder() override {
            if (m_valid) {
                drmp3_uninit(&m_mp3);
            }
        }
        bool Valid() const {
            return m_valid;
        }

    private:
        std::size_t ReadFrames(float *out, std::size_t frames) override {
            return static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&m_mp3, frames, out));
        }

        drmp3 m_mp3{};
        bool m_valid = false;
    };

    class FlacDecoder final : public ChannelDecoder {
    public:
        explicit FlacDecoder(std::shared_ptr<const std::string> data)
            : ChannelDecoder(std::move(data)) {
            m_flac = drflac_open_memory(m_data->data(), m_data->size(), nullptr);
            if (m_flac != nullptr) {
                m_sampleRate = static_cast<int>(m_flac->sampleRate);
                m_channels = std::max(static_cast<int>(m_flac->channels), 1);
            }
        }
        ~FlacDecoder() override {
            if (m_flac != nullptr) {
                drflac_close(m_flac);
            }
        }
        bool Valid() const {
            return m_flac != nullptr;
        }

    private:
        std::size_t ReadFrames(float *out, std::size_t frames) override {
            return static_cast<std::size_t>(drflac_read_pcm_frames_f32(m_flac, frames, out));
        }

        drflac *m_flac = nullptr;
    };

    class WavDecoder final : public ChannelDecoder {
    public:
        explicit WavDecoder(std::shared_ptr<const std::string> data)
            : ChannelDecoder(std::move(data)) {
            m_valid = drwav_init_memory(&m_wav, m_data->data(), m_data->size(), nullptr) != 0;
            if (m_valid) {
                m_sampleRate = static_cast<int>(m_wav.sampleRate);
                m_channels = std::max(static_cast<int>(m_wav.channels), 1);
            }
        }
        ~WavDecoder() override {
            if (m_valid) {
                drwav_uninit(&m_wav);
            }
        }
        bool Valid() const {
            return m_valid;
        }

    private:
        std::size_t ReadFrames(float *out, std::size_t frames) override {
            return static_cast<std::size_t>(drwav_read_pcm_frames_f32(&m_wav, frames, out));
        }

        drwav m_wav{};
        bool m_valid = false;
    };

    template <typename TDecoder>
    std::unique_ptr<AudioDecoder> Open(std::shared_ptr<const std::string> data) {
        auto decoder = std::make_unique<TDecoder>(std::move(data));
        if (!decoder->Valid()) {
            return nullptr;
        }
        return decoder;
    }

} // namespace

bool IsSupportedMusicExtension(std::string_view extension) {
    const std::string ext = Lowercase(extension);
    return ext == ".ogg" || ext == ".mp3" || ext == ".flac" || ext == ".wav";
}

std::unique_ptr<AudioDecoder> OpenAudioDecoder(std::shared_ptr<const std::string> data, std::string_view extension) {
    if (data == nullptr || data->empty()) {
        return nullptr;
    }
    const std::string ext = Lowercase(extension);
    if (ext == ".ogg") {
        return Open<VorbisDecoder>(std::move(data));
    }
    if (ext == ".mp3") {
        return Open<Mp3Decoder>(std::move(data));
    }
    if (ext == ".flac") {
        return Open<FlacDecoder>(std::move(data));
    }
    if (ext == ".wav") {
        return Open<WavDecoder>(std::move(data));
    }
    return nullptr;
}

} // namespace platform::uwp::shell

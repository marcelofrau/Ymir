#pragma once

/**
@file
@brief Music file decoders for the background music of the Xbox shell.

OGG Vorbis (stb_vorbis), MP3 (dr_mp3), FLAC (dr_flac) and WAV (dr_wav), all decoding from a file already read into
memory, so the decoders never touch the file system themselves (see `fs::ReadWholeFile()`).

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace platform::uwp::shell {

/// @brief A decoder producing interleaved stereo float samples.
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    /// @brief Retrieves the sample rate of the track, in Hz.
    [[nodiscard]] virtual int SampleRate() const = 0;

    /// @brief Decodes the next frames. Mono tracks are duplicated to both channels and tracks with more than two
    /// channels keep the first two.
    /// @param[out] out receives `frames` stereo frames (2 floats each), in [-1, 1]
    /// @param[in] frames the number of frames to decode
    /// @return the number of frames decoded; less than `frames` (0 included) means the end of the track
    virtual std::size_t Read(float *out, std::size_t frames) = 0;
};

/// @brief Determines whether a file extension is a supported music format.
/// @param[in] extension the extension with the dot, in any case (e.g. `.OGG`)
[[nodiscard]] bool IsSupportedMusicExtension(std::string_view extension);

/// @brief Opens a decoder for a track.
/// @param[in] data the whole file, kept alive by the decoder
/// @param[in] extension the extension with the dot, which picks the format
/// @return the decoder, or `nullptr` if the format is not supported or the data is not valid
[[nodiscard]] std::unique_ptr<AudioDecoder> OpenAudioDecoder(std::shared_ptr<const std::string> data,
                                                             std::string_view extension);

} // namespace platform::uwp::shell

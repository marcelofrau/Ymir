#pragma once

/**
@file
@brief A 5x7 dot-matrix font, for the LCD-style text of the Xbox shell.

The cyan bar of the top dashboard reads like the character LCD of a piece of 90s hardware, so its text is drawn as
lit dots on a grid of unlit ones instead of with a vector font. The glyph set covers printable ASCII plus a music
note; anything else is shown as a hollow box, like an LCD module with no such character in its ROM.

This file has no ImGui dependency so it can be unit tested on the desktop. Drawing is left to the caller.
*/

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace platform::uwp::shell::dotmatrix {

/// @brief Width of a glyph, in dots.
inline constexpr int kGlyphWidth = 5;

/// @brief Height of a glyph, in dots.
inline constexpr int kGlyphHeight = 7;

/// @brief Horizontal distance between the starts of two consecutive glyphs, in dots (one blank column between them).
inline constexpr int kAdvance = kGlyphWidth + 1;

/// @brief A glyph: one bit mask per row, top to bottom. Bit 4 is the leftmost dot, bit 0 the rightmost.
using GlyphRows = std::array<std::uint8_t, kGlyphHeight>;

/// @brief Looks up the glyph of a code point.
/// @param[in] codePoint the Unicode code point
/// @return the glyph; a hollow box for characters outside the glyph set
[[nodiscard]] const GlyphRows &GlyphFor(char32_t codePoint);

/// @brief Decodes UTF-8 text into code points. Malformed sequences decode to U+FFFD.
/// @param[in] text the UTF-8 text
/// @return the code points
[[nodiscard]] std::u32string Decode(std::string_view text);

/// @brief Computes the width of a line of text, in dots, without the trailing blank column.
/// @param[in] text the UTF-8 text
/// @return the width in dots; 0 for empty text
[[nodiscard]] int TextColumns(std::string_view text);

/// @brief Determines whether a dot of a glyph is lit.
/// @param[in] glyph the glyph
/// @param[in] column the column, in [0, kGlyphWidth)
/// @param[in] row the row, in [0, kGlyphHeight)
[[nodiscard]] inline bool IsLit(const GlyphRows &glyph, int column, int row) {
    return (glyph[row] >> (kGlyphWidth - 1 - column)) & 1u;
}

} // namespace platform::uwp::shell::dotmatrix

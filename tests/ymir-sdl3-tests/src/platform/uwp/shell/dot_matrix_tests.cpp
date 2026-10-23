#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/dot_matrix.hpp>

using namespace platform::uwp::shell;

TEST_CASE("Dot-matrix text width counts glyphs and the gaps between them", "[shell][dotmatrix]") {
    CHECK(dotmatrix::TextColumns("") == 0);
    CHECK(dotmatrix::TextColumns("A") == dotmatrix::kGlyphWidth);
    CHECK(dotmatrix::TextColumns("YMIR") == 4 * dotmatrix::kAdvance - 1);
}

TEST_CASE("Dot-matrix decodes UTF-8 and counts characters, not bytes", "[shell][dotmatrix]") {
    const std::u32string decoded = dotmatrix::Decode("a\xC3\xA9\xE2\x99\xAA");
    REQUIRE(decoded.size() == 3);
    CHECK(decoded[0] == U'a');
    CHECK(decoded[1] == 0xE9u);
    CHECK(decoded[2] == 0x266Au);
    CHECK(dotmatrix::TextColumns("a\xC3\xA9\xE2\x99\xAA") == 3 * dotmatrix::kAdvance - 1);
}

TEST_CASE("Dot-matrix turns malformed UTF-8 into replacement characters", "[shell][dotmatrix]") {
    const std::u32string decoded = dotmatrix::Decode("\xFF"
                                                     "a\xC3");
    REQUIRE(decoded.size() == 3);
    CHECK(decoded[0] == 0xFFFDu);
    CHECK(decoded[1] == U'a');
    CHECK(decoded[2] == 0xFFFDu);
}

TEST_CASE("Dot-matrix glyphs", "[shell][dotmatrix]") {
    // The space is blank, the letter T has a full-width top bar, unknown characters get the hollow box
    const auto &space = dotmatrix::GlyphFor(U' ');
    for (int row = 0; row < dotmatrix::kGlyphHeight; ++row) {
        for (int column = 0; column < dotmatrix::kGlyphWidth; ++column) {
            CHECK_FALSE(dotmatrix::IsLit(space, column, row));
        }
    }

    const auto &t = dotmatrix::GlyphFor(U'T');
    for (int column = 0; column < dotmatrix::kGlyphWidth; ++column) {
        CHECK(dotmatrix::IsLit(t, column, 0));
    }
    CHECK(dotmatrix::IsLit(t, 2, 6));
    CHECK_FALSE(dotmatrix::IsLit(t, 0, 6));

    const auto &missing = dotmatrix::GlyphFor(char32_t{0x3042}); // Hiragana A
    CHECK(dotmatrix::IsLit(missing, 0, 0));
    CHECK(dotmatrix::IsLit(missing, 4, 6));
    CHECK_FALSE(dotmatrix::IsLit(missing, 2, 3));
}

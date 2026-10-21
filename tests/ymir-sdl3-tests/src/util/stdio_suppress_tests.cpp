#include <catch2/catch_test_macros.hpp>

#include <util/stdio_suppress.hpp>

#include <cstdio>
#include <string>

#if defined(_WIN32)
    #include <io.h>

    #define ymir_fileno _fileno
#else
    #define ymir_fileno fileno
#endif

namespace {

/// @brief A suppressor over an arbitrary stream.
///
/// The public suppressors only wrap stdout and stderr, which are the streams the test runner itself writes to. This
/// subclass allows testing the redirection against a scratch stream instead, so the test never disturbs its own
/// output.
class StreamSuppressor final : public util::StdioSuppressor {
public:
    explicit StreamSuppressor(FILE *file)
        : util::StdioSuppressor(file) {}
};

std::string readAll(FILE *file) {
    std::fflush(file);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::string contents;
    if (size > 0) {
        contents.resize(static_cast<size_t>(size));
        const size_t read = std::fread(contents.data(), 1, contents.size(), file);
        contents.resize(read);
    }
    return contents;
}

} // namespace

TEST_CASE("StdioSuppressor silences a stream and restores it afterwards", "[util][stdio]") {
    FILE *file = std::tmpfile();
    REQUIRE(file != nullptr);

    std::fputs("A", file);
    std::fflush(file);

    {
        StreamSuppressor suppressor{file};
        std::fputs("B", file);
        std::fflush(file);
    }

    std::fputs("C", file);
    std::fflush(file);

    // "B" was written while the stream pointed at the null device, so only "A" and "C" must have landed in the file.
    CHECK(readAll(file) == "AC");

    std::fclose(file);
}

TEST_CASE("StdioSuppressor never leaves the stream closed", "[util][stdio]") {
    FILE *file = std::tmpfile();
    REQUIRE(file != nullptr);

    const int descriptor = ymir_fileno(file);
    REQUIRE(descriptor >= 0);

    {
        StreamSuppressor suppressor{file};
        // The descriptor is redirected, not replaced, and the stream object itself is untouched.
        CHECK(ymir_fileno(file) == descriptor);
    }

    // The stream must still be usable. freopen() closes the stream when it fails, which used to leave fileno() invalid
    // and made the restore in the destructor assert on the Xbox.
    CHECK(ymir_fileno(file) == descriptor);
    CHECK(ymir_fileno(file) >= 0);

    std::fputs("still writable", file);
    std::fflush(file);
    CHECK(readAll(file) == "still writable");

    std::fclose(file);
}

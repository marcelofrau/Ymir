#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/marquee.hpp>

#include <string>

using platform::uwp::shell::Marquee;

using Catch::Approx;

namespace {

// Ten units per character keeps the arithmetic in the tests readable
float Measure(std::string_view text) {
    return static_cast<float>(text.size()) * 10.0f;
}

// Scrolls the current text out of an area of the given width at 100 units per second
void ScrollOut(Marquee &marquee, float areaWidth) {
    for (int i = 0; i < 1000 && !marquee.Current().empty(); ++i) {
        marquee.Update(0.1f, areaWidth, 100.0f, Measure);
    }
}

} // namespace

TEST_CASE("Marquee shows nothing without texts", "[shell][marquee]") {
    Marquee marquee;
    marquee.Update(0.1f, 500.0f, 100.0f, Measure);
    CHECK(marquee.Current().empty());
}

TEST_CASE("Marquee appears immediately at offset 0, holds briefly and scrolls left", "[shell][marquee]") {
    Marquee marquee;
    marquee.SetIdleTexts({"hello"});

    marquee.Update(0.0f, 500.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "hello");
    CHECK(marquee.Offset() == Approx(0.0f));

    marquee.Update(Marquee::kHoldSeconds + 0.5f, 500.0f, 100.0f, Measure);
    CHECK(marquee.Offset() == Approx(-50.0f));
}

TEST_CASE("Marquee shows events before idle texts, in order", "[shell][marquee]") {
    Marquee marquee;
    marquee.SetIdleTexts({"idle"});
    marquee.Post("first");
    marquee.Post("second");

    marquee.Update(0.0f, 100.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "first");
    ScrollOut(marquee, 100.0f);

    marquee.Update(0.0f, 100.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "second");
    ScrollOut(marquee, 100.0f);

    marquee.Update(0.0f, 100.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "idle");
}

TEST_CASE("Marquee finishes a text only after its tail leaves the area", "[shell][marquee]") {
    Marquee marquee;
    marquee.SetIdleTexts({"abcd"}); // 40 units wide

    marquee.Update(0.0f, 100.0f, 100.0f, Measure);
    // Hold + 40 (text) units must scroll by before the text is gone: kHoldSeconds + 0.4 s at 100 units per second
    marquee.Update(Marquee::kHoldSeconds + 0.3f, 100.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "abcd");
    marquee.Update(0.2f, 100.0f, 100.0f, Measure);
    CHECK(marquee.Current().empty());
}

TEST_CASE("Marquee cycles through the idle texts", "[shell][marquee]") {
    Marquee marquee;
    marquee.SetIdleTexts({"a", "b"});

    marquee.Update(0.0f, 10.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "a");
    ScrollOut(marquee, 10.0f);
    marquee.Update(0.0f, 10.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "b");
    ScrollOut(marquee, 10.0f);
    marquee.Update(0.0f, 10.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "a");
}

TEST_CASE("Marquee drops the oldest events beyond the queue limit and ignores empty ones", "[shell][marquee]") {
    Marquee marquee;
    marquee.Post("");
    CHECK(marquee.PendingEvents() == 0);

    for (std::size_t i = 0; i < Marquee::kMaxQueuedEvents + 3; ++i) {
        marquee.Post("event " + std::to_string(i));
    }
    CHECK(marquee.PendingEvents() == Marquee::kMaxQueuedEvents);

    marquee.Update(0.0f, 10.0f, 100.0f, Measure);
    CHECK(marquee.Current() == "event 3");
}

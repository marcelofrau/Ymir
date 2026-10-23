#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/layout.hpp>

using platform::uwp::shell::Layout;
using platform::uwp::shell::Vec2;

using Catch::Approx;

TEST_CASE("Shell layout maps the reference canvas 1:1 at 1080p without a safe area", "[shell][layout]") {
    Layout layout;
    layout.Update(1920.0f, 1080.0f, 0.0f);

    CHECK(layout.ScaleFactor() == Approx(1.0f));
    const Vec2 p = layout.SafePoint({100.0f, 200.0f});
    CHECK(p.x == Approx(100.0f));
    CHECK(p.y == Approx(200.0f));
    CHECK(layout.Scale(70.0f) == Approx(70.0f));
}

TEST_CASE("Shell layout scales uniformly to 4K", "[shell][layout]") {
    Layout layout;
    layout.Update(3840.0f, 2160.0f, 0.0f);

    CHECK(layout.ScaleFactor() == Approx(2.0f));
    CHECK(layout.Scale(70.0f) == Approx(140.0f));
    const Vec2 p = layout.CanvasPoint({960.0f, 540.0f});
    CHECK(p.x == Approx(1920.0f));
    CHECK(p.y == Approx(1080.0f));
}

TEST_CASE("Shell layout letterboxes a display that is wider than 16:9", "[shell][layout]") {
    Layout layout;
    layout.Update(2560.0f, 1080.0f, 0.0f);

    // Height limits the scale, so the canvas keeps 1920 wide and is centered with 320 px bars on each side
    CHECK(layout.ScaleFactor() == Approx(1.0f));
    CHECK(layout.Canvas().pos.x == Approx(320.0f));
    CHECK(layout.Canvas().pos.y == Approx(0.0f));
    CHECK(layout.Canvas().size.x == Approx(1920.0f));
    const Vec2 origin = layout.CanvasPoint({0.0f, 0.0f});
    CHECK(origin.x == Approx(320.0f));
}

TEST_CASE("Shell layout keeps content inside the safe area", "[shell][layout]") {
    Layout layout;
    layout.Update(1920.0f, 1080.0f, 0.05f);

    const Vec2 topLeft = layout.SafePoint({0.0f, 0.0f});
    const Vec2 bottomRight = layout.SafePoint({1920.0f, 1080.0f});
    CHECK(topLeft.x == Approx(96.0f));
    CHECK(topLeft.y == Approx(54.0f));
    CHECK(bottomRight.x == Approx(1824.0f));
    CHECK(bottomRight.y == Approx(1026.0f));

    // Lengths are not squeezed by the safe area, so round shapes stay round
    CHECK(layout.Scale(100.0f) == Approx(100.0f));
}

TEST_CASE("Shell layout clamps an out-of-range safe area and a degenerate display", "[shell][layout]") {
    Layout layout;
    layout.Update(0.0f, -5.0f, 0.9f);

    CHECK(layout.Display().size.x >= 1.0f);
    CHECK(layout.Display().size.y >= 1.0f);
    CHECK(layout.SafeArea().size.x >= 0.0f);
    CHECK(layout.SafeArea().size.y >= 0.0f);
    CHECK(layout.Unscale(layout.Scale(42.0f)) == Approx(42.0f));
}

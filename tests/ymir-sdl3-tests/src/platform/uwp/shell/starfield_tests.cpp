#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/starfield.hpp>

#include <vector>

using platform::uwp::shell::ProjectedStar;
using platform::uwp::shell::Starfield;

using Catch::Approx;

TEST_CASE("Starfield starts as a full sky at cruise speed", "[shell][starfield]") {
    Starfield starfield{200};
    std::vector<ProjectedStar> stars;
    starfield.Project(960.0f, 540.0f, 960.0f, stars);

    CHECK(starfield.Speed() == Approx(Starfield::kCruiseSpeed));
    CHECK(!stars.empty());
    CHECK(stars.size() <= 200);
    for (const ProjectedStar &star : stars) {
        CHECK(star.brightness > 0.0f);
        CHECK(star.brightness <= 1.0f);
        CHECK(star.radius > 0.0f);
    }
}

TEST_CASE("Starfield is deterministic for a given seed", "[shell][starfield]") {
    Starfield a{100, 1234u};
    Starfield b{100, 1234u};
    a.Update(0.5f);
    b.Update(0.5f);

    std::vector<ProjectedStar> starsA;
    std::vector<ProjectedStar> starsB;
    a.Project(0.0f, 0.0f, 100.0f, starsA);
    b.Project(0.0f, 0.0f, 100.0f, starsB);

    REQUIRE(starsA.size() == starsB.size());
    for (std::size_t i = 0; i < starsA.size(); ++i) {
        CHECK(starsA[i].x == starsB[i].x);
        CHECK(starsA[i].y == starsB[i].y);
    }
}

TEST_CASE("Starfield warp eases back to the target speed", "[shell][starfield]") {
    Starfield starfield{10};
    starfield.Warp();
    CHECK(starfield.Speed() == Approx(Starfield::kWarpSpeed));

    // A few seconds at 60 fps settle the speed close to the cruise speed
    for (int i = 0; i < 180; ++i) {
        starfield.Update(1.0f / 60.0f);
    }
    CHECK(starfield.Speed() == Approx(Starfield::kCruiseSpeed).margin(0.01f));
}

TEST_CASE("Starfield streaks get longer at warp speed", "[shell][starfield]") {
    Starfield cruise{50, 7u};
    Starfield warp{50, 7u};
    warp.Warp();

    std::vector<ProjectedStar> cruiseStars;
    std::vector<ProjectedStar> warpStars;
    cruise.Project(960.0f, 540.0f, 960.0f, cruiseStars);
    warp.Project(960.0f, 540.0f, 960.0f, warpStars);
    REQUIRE(cruiseStars.size() == warpStars.size());

    float cruiseLength = 0.0f;
    float warpLength = 0.0f;
    for (std::size_t i = 0; i < cruiseStars.size(); ++i) {
        const auto length = [](const ProjectedStar &s) {
            const float dx = s.x - s.prevX;
            const float dy = s.y - s.prevY;
            return dx * dx + dy * dy;
        };
        cruiseLength += length(cruiseStars[i]);
        warpLength += length(warpStars[i]);
    }
    CHECK(warpLength > cruiseLength);
}

TEST_CASE("Starfield ignores negative time steps", "[shell][starfield]") {
    Starfield a{30, 99u};
    Starfield b{30, 99u};
    a.Update(-1.0f);

    std::vector<ProjectedStar> starsA;
    std::vector<ProjectedStar> starsB;
    a.Project(0.0f, 0.0f, 100.0f, starsA);
    b.Project(0.0f, 0.0f, 100.0f, starsB);
    REQUIRE(starsA.size() == starsB.size());
    for (std::size_t i = 0; i < starsA.size(); ++i) {
        CHECK(starsA[i].x == starsB[i].x);
    }
}

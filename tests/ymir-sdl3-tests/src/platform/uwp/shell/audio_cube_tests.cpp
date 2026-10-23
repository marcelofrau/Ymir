#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/audio_cube.hpp>
#include <platform/uwp/shell/quad_mesh.hpp>

#include <vector>

using namespace platform::uwp::shell;

using Catch::Approx;

TEST_CASE("Envelope rises faster than it falls", "[shell][cube]") {
    Envelope envelope{0.03f, 0.35f};
    envelope.Update(1.0f, 0.05f);
    const float afterAttack = envelope.Value();
    CHECK(afterAttack > 0.75f);

    Envelope release{0.03f, 0.35f};
    for (int i = 0; i < 100; ++i) {
        release.Update(1.0f, 0.05f);
    }
    release.Update(0.0f, 0.05f);
    // The same 50 ms that took the envelope most of the way up only take it down a little
    CHECK(release.Value() > 0.8f);
}

TEST_CASE("Envelope ignores negative time steps", "[shell][cube]") {
    Envelope envelope{0.03f, 0.35f};
    envelope.Update(1.0f, -1.0f);
    CHECK(envelope.Value() == Approx(0.0f));
}

TEST_CASE("Audio cube is a still cube in silence", "[shell][cube]") {
    AudioCube cube;
    for (int i = 0; i < 120; ++i) {
        cube.Update(1.0f / 60.0f, {});
    }
    CHECK(cube.Morph() == Approx(0.0f).margin(1e-4f));
    CHECK(cube.Scale() == Approx(1.0f).margin(1e-4f));
    CHECK(cube.Wobble() == Approx(0.0f).margin(1e-4f));
    CHECK(cube.Spin() > 0.0f); // still turning slowly
}

TEST_CASE("Audio cube morphs towards a sphere and swells with loud music", "[shell][cube]") {
    AudioCube cube;
    for (int i = 0; i < 60; ++i) {
        cube.Update(1.0f / 60.0f, {0.5f, 0.4f});
    }
    CHECK(cube.Morph() > 0.9f);
    CHECK(cube.Scale() > 1.2f);
    CHECK(cube.Wobble() > 0.1f);

    // And settles back after the music stops
    for (int i = 0; i < 300; ++i) {
        cube.Update(1.0f / 60.0f, {});
    }
    CHECK(cube.Morph() < 0.05f);
}

TEST_CASE("Mesh deformation with no amount leaves the shape untouched", "[shell][cube]") {
    const QuadMesh mesh = BuildCubeMesh(3);
    std::vector<Vec3> positions;
    ShapeMesh(mesh, 1.0f, 10.0f, positions);
    const std::vector<Vec3> original = positions;

    DeformMesh(positions, 0.0f, 1.23f);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        CHECK(positions[i].x == original[i].x);
        CHECK(positions[i].y == original[i].y);
        CHECK(positions[i].z == original[i].z);
    }

    DeformMesh(positions, 0.2f, 1.23f);
    bool moved = false;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        moved = moved || positions[i].x != original[i].x;
    }
    CHECK(moved);
}

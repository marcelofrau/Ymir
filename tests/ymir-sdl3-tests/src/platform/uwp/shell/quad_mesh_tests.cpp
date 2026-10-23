#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <platform/uwp/shell/quad_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace platform::uwp::shell;

using Catch::Approx;

namespace {

float Length(Vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

TEST_CASE("Cube mesh has six faces of subdivisions squared quads", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(4);
    CHECK(mesh.quads.size() == 6u * 4u * 4u);
    CHECK(mesh.vertices.size() == 6u * 5u * 5u);
    for (const auto &quad : mesh.quads) {
        for (const auto index : quad) {
            CHECK(index < mesh.vertices.size());
        }
    }
    CHECK(BuildCubeMesh(0).quads.size() == 6u);
}

TEST_CASE("Morph 1 puts every vertex on the sphere", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(3);
    std::vector<Vec3> positions;
    ShapeMesh(mesh, 1.0f, 50.0f, positions);
    REQUIRE(positions.size() == mesh.vertices.size());
    for (const Vec3 &p : positions) {
        CHECK(Length(p) == Approx(50.0f).margin(1e-3f));
    }
}

TEST_CASE("Morph 0 keeps the vertices on the faces of a cube", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(3);
    std::vector<Vec3> positions;
    ShapeMesh(mesh, 0.0f, 10.0f, positions);
    float maxCoordinate = 0.0f;
    for (const Vec3 &p : positions) {
        maxCoordinate = std::max({maxCoordinate, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    }
    // Every vertex lies on one of the faces, so its largest coordinate is the half size of the cube
    REQUIRE(maxCoordinate > 0.0f);
    CHECK(maxCoordinate < 10.0f);
    for (const Vec3 &p : positions) {
        CHECK(std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) == Approx(maxCoordinate));
    }
}

TEST_CASE("Projection culls the back of the sphere and orders quads clockwise", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(4);
    std::vector<Vec3> positions;
    ShapeMesh(mesh, 1.0f, 100.0f, positions);

    std::vector<ProjectedQuad> quads;
    MeshView view{};
    view.centerX = 500.0f;
    view.centerY = 300.0f;
    ProjectMesh(mesh, positions, 100.0f, view, quads);

    // Roughly half of the quads face the camera; perspective hides a few more
    CHECK(quads.size() > mesh.quads.size() / 4);
    CHECK(quads.size() < mesh.quads.size() * 3 / 4);

    for (std::size_t i = 0; i < quads.size(); ++i) {
        const ProjectedQuad &q = quads[i];
        float area = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const int n = (k + 1) % 4;
            area += q.x[k] * q.y[n] - q.x[n] * q.y[k];
        }
        CHECK(area >= 0.0f);
        CHECK(q.light >= 0.0f);
        CHECK(q.light <= 1.0f);
        if (i > 0) {
            CHECK(quads[i - 1].depth <= q.depth);
        }
    }
}

TEST_CASE("Projection posterizes the light into the requested levels", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(6);
    std::vector<Vec3> positions;
    ShapeMesh(mesh, 1.0f, 100.0f, positions);

    std::vector<ProjectedQuad> quads;
    MeshView view{};
    view.lightLevels = 4;
    ProjectMesh(mesh, positions, 100.0f, view, quads);
    for (const ProjectedQuad &q : quads) {
        const float scaled = q.light * 4.0f;
        CHECK(scaled == Approx(std::round(scaled)).margin(1e-4f));
    }
}

TEST_CASE("Projection rejects mismatched input", "[shell][mesh]") {
    const QuadMesh mesh = BuildCubeMesh(2);
    std::vector<ProjectedQuad> quads{ProjectedQuad{}};
    ProjectMesh(mesh, {}, 10.0f, MeshView{}, quads);
    CHECK(quads.empty());
}

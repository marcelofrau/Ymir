#include "quad_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace platform::uwp::shell {

namespace {

    Vec3 Sub(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }

    Vec3 Cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    float Dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Normalize(Vec3 v) {
        const float length = std::sqrt(Dot(v, v));
        return length > 0.0f ? Vec3{v.x / length, v.y / length, v.z / length} : Vec3{0.0f, 0.0f, 1.0f};
    }

    Vec3 Rotate(Vec3 v, float yaw, float pitch) {
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const Vec3 r1{v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy};
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        return {r1.x, r1.y * cp - r1.z * sp, r1.y * sp + r1.z * cp};
    }

    // Light from the top-left, slightly in front, like the highlight on the Saturn BIOS spheres
    const Vec3 kLightDir = Normalize({-0.55f, -0.65f, 0.75f});

    // The cube is drawn a bit smaller than the sphere it morphs into: a cube with the sphere's radius as its half
    // size would look much bigger because of its corners
    constexpr float kCubeHalfSize = 0.78f;

} // namespace

QuadMesh BuildCubeMesh(int subdivisions) {
    subdivisions = std::max(subdivisions, 1);
    QuadMesh mesh;

    // Each face is described by its normal axis and two in-plane axes spanning it
    struct Face {
        Vec3 normal, u, v;
    };
    constexpr std::array<Face, 6> kFaces{{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};

    const int side = subdivisions + 1;
    for (const Face &face : kFaces) {
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                const float s = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(subdivisions);
                const float t = -1.0f + 2.0f * static_cast<float>(j) / static_cast<float>(subdivisions);
                mesh.vertices.push_back({face.normal.x + face.u.x * s + face.v.x * t,
                                         face.normal.y + face.u.y * s + face.v.y * t,
                                         face.normal.z + face.u.z * s + face.v.z * t});
            }
        }
        for (int j = 0; j < subdivisions; ++j) {
            for (int i = 0; i < subdivisions; ++i) {
                const auto at = [&](int ii, int jj) { return static_cast<std::uint16_t>(base + jj * side + ii); };
                mesh.quads.push_back({at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)});
            }
        }
    }
    return mesh;
}

void ShapeMesh(const QuadMesh &mesh, float morph, float radius, std::vector<Vec3> &out) {
    morph = std::clamp(morph, 0.0f, 1.0f);
    out.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3 cube = mesh.vertices[i];
        const Vec3 sphere = Normalize(cube);
        out[i] = {(cube.x * kCubeHalfSize + (sphere.x - cube.x * kCubeHalfSize) * morph) * radius,
                  (cube.y * kCubeHalfSize + (sphere.y - cube.y * kCubeHalfSize) * morph) * radius,
                  (cube.z * kCubeHalfSize + (sphere.z - cube.z * kCubeHalfSize) * morph) * radius};
    }
}

void DeformMesh(std::vector<Vec3> &positions, float amount, float time) {
    if (amount == 0.0f) {
        return;
    }
    for (Vec3 &p : positions) {
        const Vec3 d = Normalize(p);
        // Gentle surface waves plus subtle squash-and-stretch and torsional twist driven by the audio wobble
        const float wave = std::sin(d.x * 3.4f + time * 5.0f) * std::cos(d.y * 3.0f - time * 4.0f) +
                           0.5f * std::sin(d.z * 4.0f + time * 6.2f);
        const float factor = 1.0f + amount * wave * 0.58f;
        const float stretchY = 1.0f + amount * 0.16f * std::sin(time * 6.8f + d.y * 1.6f);
        const float twist = d.y * amount * 0.28f * std::sin(time * 4.5f);
        const float ct = std::cos(twist);
        const float st = std::sin(twist);
        const float rx = p.x * ct - p.z * st;
        const float rz = p.x * st + p.z * ct;
        p = {rx * factor, p.y * factor * stretchY, rz * factor};
    }
}

void ProjectMesh(const QuadMesh &mesh, const std::vector<Vec3> &positions, float radius, const MeshView &view,
                 std::vector<ProjectedQuad> &out) {
    out.clear();
    if (positions.size() != mesh.vertices.size() || radius <= 0.0f) {
        return;
    }

    const float camera = std::max(view.cameraDistance, 1.5f) * radius;
    for (const auto &quad : mesh.quads) {
        std::array<Vec3, 4> p;
        for (int k = 0; k < 4; ++k) {
            p[k] = Rotate(positions[quad[k]], view.yaw, view.pitch);
        }

        // Flat shading uses one normal per quad, from its diagonals. Quads facing away are culled; the shape is
        // convex, so the remaining ones never overlap and the sort below only matters for the deformed shapes
        const Vec3 centroid{(p[0].x + p[1].x + p[2].x + p[3].x) * 0.25f, (p[0].y + p[1].y + p[2].y + p[3].y) * 0.25f,
                            (p[0].z + p[1].z + p[2].z + p[3].z) * 0.25f};
        Vec3 normal = Normalize(Cross(Sub(p[2], p[0]), Sub(p[3], p[1])));
        // The shapes are centered on the origin, so the outward normal points away from it whatever the winding
        if (Dot(normal, centroid) < 0.0f) {
            normal = {-normal.x, -normal.y, -normal.z};
        }
        const Vec3 toCamera = Normalize(Sub({0.0f, 0.0f, camera}, centroid));
        if (Dot(normal, toCamera) <= 0.0f) {
            continue;
        }

        // Ambient plus diffuse plus a tight specular, then posterized into a few bands like 90s flat shading
        const float diffuse = std::max(Dot(normal, kLightDir), 0.0f);
        const Vec3 halfway = Normalize({kLightDir.x + toCamera.x, kLightDir.y + toCamera.y, kLightDir.z + toCamera.z});
        const float specular = std::pow(std::max(Dot(normal, halfway), 0.0f), 24.0f);
        float light = std::clamp(0.22f + 0.68f * diffuse + 0.55f * specular, 0.0f, 1.0f);
        if (view.lightLevels > 0) {
            const auto levels = static_cast<float>(view.lightLevels);
            light = std::round(light * levels) / levels;
        }

        ProjectedQuad projected{};
        for (int k = 0; k < 4; ++k) {
            const float scale = camera / (camera - p[k].z);
            projected.x[k] = view.centerX + p[k].x * scale;
            projected.y[k] = view.centerY + p[k].y * scale;
        }
        // ImGui's anti-aliased polygon fill expects clockwise corners on screen (positive area with +y down)
        float area = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const int n = (k + 1) % 4;
            area += projected.x[k] * projected.y[n] - projected.x[n] * projected.y[k];
        }
        if (area < 0.0f) {
            std::swap(projected.x[1], projected.x[3]);
            std::swap(projected.y[1], projected.y[3]);
        }
        projected.light = light;
        projected.depth = centroid.z;
        out.push_back(projected);
    }

    std::sort(out.begin(), out.end(), [](const ProjectedQuad &a, const ProjectedQuad &b) { return a.depth < b.depth; });
}

} // namespace platform::uwp::shell

#pragma once

/**
@file
@brief Flat-shaded quad meshes, the "32-bit polygon" look of the Xbox shell.

The Saturn's VDP1 draws quads, not triangles, and its games are full of faceted, flat-shaded quad geometry. The
shell's spheres (and, later, the audio-reactive cubes) follow that look: a cube whose faces are split into a grid of
quads, morphed anywhere between a cube (`morph` 0) and a sphere (`morph` 1), rotated, projected and shaded one flat
color per quad with a posterized light ramp.

This file has no ImGui dependency so it can be unit tested on the desktop. Drawing is left to the caller.
*/

#include <array>
#include <cstdint>
#include <vector>

namespace platform::uwp::shell {

/// @brief A 3D vector. +x is right, +y is down (screen convention), +z points towards the viewer.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/// @brief A cube with each face split into a grid of quads.
struct QuadMesh {
    std::vector<Vec3> vertices;                      // on the surface of the [-1, 1] cube
    std::vector<std::array<std::uint16_t, 4>> quads; // vertex indices, in order around each quad
};

/// @brief A quad ready to be drawn.
struct ProjectedQuad {
    std::array<float, 4> x; // display coordinates of the corners, clockwise on screen
    std::array<float, 4> y;
    float light; // 0 (unlit) to 1 (fully lit), already posterized
    float depth; // larger is closer to the viewer
};

/// @brief How a mesh is placed and lit.
struct MeshView {
    float yaw = 0.0f;     // rotation around the vertical axis, in radians
    float pitch = 0.0f;   // rotation around the horizontal axis, in radians
    float centerX = 0.0f; // display position of the mesh center
    float centerY = 0.0f;
    float cameraDistance = 4.0f; // distance from the camera to the center, in units of the mesh radius
    int lightLevels = 6;         // posterization steps of the light ramp; 0 or less keeps it continuous
};

/// @brief Builds a cube mesh.
/// @param[in] subdivisions quads per face edge, at least 1
[[nodiscard]] QuadMesh BuildCubeMesh(int subdivisions);

/// @brief Computes the vertex positions of a cube/sphere morph.
/// @param[in] mesh the cube mesh
/// @param[in] morph 0 for a cube, 1 for a sphere; values in between blend the two
/// @param[in] radius the sphere radius; the cube is scaled so that both shapes look about the same size
/// @param[out] out receives one position per mesh vertex
void ShapeMesh(const QuadMesh &mesh, float morph, float radius, std::vector<Vec3> &out);

/// @brief Pushes the vertices of a shaped mesh in and out along their direction from the center, a slow wave that
/// makes the surface look alive.
/// @param[in,out] positions the shaped vertex positions
/// @param[in] amount the wave amplitude, as a fraction of each vertex's distance; 0 leaves the shape untouched
/// @param[in] time the animation time in seconds
void DeformMesh(std::vector<Vec3> &positions, float amount, float time);

/// @brief Rotates, projects, culls and lights the quads of a shaped mesh, sorted back to front.
/// @param[in] mesh the cube mesh
/// @param[in] positions the shaped vertex positions from `ShapeMesh()`
/// @param[in] radius the radius used to shape the mesh, which sets the perspective strength
/// @param[in] view placement and lighting
/// @param[out] out receives the visible quads; it is cleared first
void ProjectMesh(const QuadMesh &mesh, const std::vector<Vec3> &positions, float radius, const MeshView &view,
                 std::vector<ProjectedQuad> &out);

} // namespace platform::uwp::shell

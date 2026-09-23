#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>

#include "moteur/mesh.hpp"

namespace {

// Every non-degenerate triangle must be counter-clockwise seen from the side its normal points to,
// and that normal must point away from `center` (the outside of a closed shape).
void check_triangles_face_out(const moteur::MeshData& mesh, glm::vec3 center, bool closed) {
    int checked = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const moteur::Vertex3D& a = mesh.vertices[mesh.indices[i]];
        const moteur::Vertex3D& b = mesh.vertices[mesh.indices[i + 1]];
        const moteur::Vertex3D& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 cross = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::length(cross) < 1e-6f) {
            continue;  // degenerate triangle at a sphere pole: skipped by the GPU too
        }
        const glm::vec3 face_normal = glm::normalize(cross);
        const glm::vec3 vertex_normal = glm::normalize(a.normal + b.normal + c.normal);
        CHECK(glm::dot(face_normal, vertex_normal) > 0.9f);
        if (closed) {
            const glm::vec3 centroid = (a.position + b.position + c.position) / 3.0f;
            CHECK(glm::dot(face_normal, centroid - center) > 0.0f);
        }
        ++checked;
    }
    CHECK(checked > 0);
}

void check_unit_normals(const moteur::MeshData& mesh) {
    for (const moteur::Vertex3D& vertex : mesh.vertices) {
        CHECK(glm::length(vertex.normal) == doctest::Approx(1.0f).epsilon(1e-5));
    }
}

}  // namespace

TEST_CASE("Aabb starts empty and grows around its points") {
    moteur::Aabb box;
    CHECK(box.empty());
    box.add({1.0f, 2.0f, 3.0f});
    CHECK_FALSE(box.empty());
    CHECK(box.size() == glm::vec3(0.0f));
    box.add({-1.0f, 4.0f, 0.0f});
    CHECK(box.min == glm::vec3(-1.0f, 2.0f, 0.0f));
    CHECK(box.max == glm::vec3(1.0f, 4.0f, 3.0f));
    CHECK(box.center() == glm::vec3(0.0f, 3.0f, 1.5f));
}

TEST_CASE("make_cube: 6 faces, sharp edges, facing out") {
    const moteur::MeshData cube = moteur::make_cube({2.0f, 1.0f, 0.5f});
    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);
    CHECK(cube.triangle_count() == 12);
    const moteur::Aabb box = cube.bounds();
    CHECK(box.min == glm::vec3(-1.0f, -0.5f, -0.25f));
    CHECK(box.max == glm::vec3(1.0f, 0.5f, 0.25f));
    check_unit_normals(cube);
    check_triangles_face_out(cube, glm::vec3(0.0f), true);
}

TEST_CASE("make_plane: one quad on the ground, facing up") {
    const moteur::MeshData plane = moteur::make_plane({4.0f, 2.0f});
    CHECK(plane.vertices.size() == 4);
    CHECK(plane.triangle_count() == 2);
    const moteur::Aabb box = plane.bounds();
    CHECK(box.min == glm::vec3(-2.0f, 0.0f, -1.0f));
    CHECK(box.max == glm::vec3(2.0f, 0.0f, 1.0f));
    for (const moteur::Vertex3D& vertex : plane.vertices) {
        CHECK(vertex.normal == glm::vec3(0.0f, 1.0f, 0.0f));
    }
    check_triangles_face_out(plane, glm::vec3(0.0f, -1.0f, 0.0f), true);
}

TEST_CASE("make_sphere: vertices on the radius, normals outward, facing out") {
    const moteur::MeshData sphere = moteur::make_sphere(2.0f, 12, 6);
    CHECK(sphere.vertices.size() == 13 * 7);
    CHECK(sphere.triangle_count() == 12 * 6 * 2);
    for (const moteur::Vertex3D& vertex : sphere.vertices) {
        CHECK(glm::length(vertex.position) == doctest::Approx(2.0f).epsilon(1e-5));
        CHECK(glm::length(vertex.normal - vertex.position / 2.0f) < 1e-5f);
    }
    const moteur::Aabb box = sphere.bounds();
    CHECK(box.max.y == doctest::Approx(2.0f));
    CHECK(box.min.y == doctest::Approx(-2.0f));
    check_triangles_face_out(sphere, glm::vec3(0.0f), true);
}

TEST_CASE("make_sphere keeps a minimum resolution") {
    const moteur::MeshData sphere = moteur::make_sphere(1.0f, 1, 1);
    CHECK(sphere.vertices.size() == 4 * 3);  // 3 segments, 2 rings
    CHECK(sphere.triangle_count() == 3 * 2 * 2);
}

namespace {

// Tangents: unit length, perpendicular to the normal, pointing along increasing u, and a bitangent
// (cross(normal, tangent) * w) pointing towards decreasing v, which is up in the image.
void check_tangents(const moteur::MeshData& mesh) {
    for (const moteur::Vertex3D& vertex : mesh.vertices) {
        const glm::vec3 t(vertex.tangent);
        CHECK(glm::length(t) == doctest::Approx(1.0f).epsilon(1e-3));
        CHECK(std::abs(glm::dot(t, vertex.normal)) < 1e-3f);
        CHECK(std::abs(vertex.tangent.w) == 1.0f);
    }
    int checked = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const moteur::Vertex3D& a = mesh.vertices[mesh.indices[i]];
        const moteur::Vertex3D& b = mesh.vertices[mesh.indices[i + 1]];
        const moteur::Vertex3D& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 e1 = b.position - a.position, e2 = c.position - a.position;
        const glm::vec2 d1 = b.uv - a.uv, d2 = c.uv - a.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-8f || glm::length(glm::cross(e1, e2)) < 1e-6f) {
            continue;  // degenerate in space or in texture coordinates (sphere poles)
        }
        const glm::vec3 along_u = (e1 * d2.y - e2 * d1.y) / det;  // dP/du
        const glm::vec3 along_v = (e2 * d1.x - e1 * d2.x) / det;  // dP/dv
        const glm::vec3 bitangent = glm::cross(a.normal, glm::vec3(a.tangent)) * a.tangent.w;
        CHECK(glm::dot(glm::vec3(a.tangent), along_u) > 0.0f);
        CHECK(glm::dot(bitangent, along_v) < 0.0f);
        ++checked;
    }
    CHECK(checked > 0);
}

}  // namespace

TEST_CASE("compute_tangents follows the texture coordinates of every primitive") {
    check_tangents(moteur::make_cube({2.0f, 1.0f, 0.5f}));
    check_tangents(moteur::make_plane({4.0f, 2.0f}));
    check_tangents(moteur::make_sphere(1.0f, 16, 8));
}

TEST_CASE("compute_tangents: a plane facing up has its tangent along +X and its bitangent along -Z") {
    // u grows along +X; v = 0 is at -Z, so "up in the image" is -Z.
    const moteur::MeshData plane = moteur::make_plane();
    for (const moteur::Vertex3D& vertex : plane.vertices) {
        CHECK(glm::length(glm::vec3(vertex.tangent) - glm::vec3(1, 0, 0)) < 1e-4f);
        const glm::vec3 bitangent = glm::cross(vertex.normal, glm::vec3(vertex.tangent)) * vertex.tangent.w;
        CHECK(glm::length(bitangent - glm::vec3(0, 0, -1)) < 1e-4f);
    }
}

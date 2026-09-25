#include "moteur/mesh.hpp"

#include <glm/gtc/constants.hpp>
#include <mikktspace.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "moteur/renderer.hpp"

namespace moteur {

namespace {

// Adds a quad whose corners a, b, c, d are counter-clockwise seen from `normal`.
void add_quad(MeshData& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal) {
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);  // replaced by compute_tangents()
    mesh.vertices.push_back({a, normal, {0.0f, 1.0f}, tangent});
    mesh.vertices.push_back({b, normal, {1.0f, 1.0f}, tangent});
    mesh.vertices.push_back({c, normal, {1.0f, 0.0f}, tangent});
    mesh.vertices.push_back({d, normal, {0.0f, 0.0f}, tangent});
    for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh.indices.push_back(first + corner);
    }
}

// MikkTSpace reads the mesh and writes the tangents through these callbacks.
MeshData* mesh_of(const SMikkTSpaceContext* context) {
    return static_cast<MeshData*>(context->m_pUserData);
}

const Vertex3D& corner(const SMikkTSpaceContext* context, int face, int vertex) {
    const MeshData& mesh = *mesh_of(context);
    return mesh.vertices[mesh.indices[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(vertex)]];
}

int face_count(const SMikkTSpaceContext* context) {
    return static_cast<int>(mesh_of(context)->indices.size() / 3);
}

int vertices_of_face(const SMikkTSpaceContext*, int) {
    return 3;
}

void get_position(const SMikkTSpaceContext* context, float out[], int face, int vertex) {
    const glm::vec3& p = corner(context, face, vertex).position;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void get_normal(const SMikkTSpaceContext* context, float out[], int face, int vertex) {
    const glm::vec3& n = corner(context, face, vertex).normal;
    out[0] = n.x;
    out[1] = n.y;
    out[2] = n.z;
}

void get_texcoord(const SMikkTSpaceContext* context, float out[], int face, int vertex) {
    // glTF puts v = 0 at the top of the image; MikkTSpace, like Blender, expects v to grow upwards.
    // Flipping it makes the bitangent point up in the image, where the green channel of a normal map points.
    const glm::vec2& uv = corner(context, face, vertex).uv;
    out[0] = uv.x;
    out[1] = 1.0f - uv.y;
}

void set_tangent(const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int vertex) {
    MeshData& mesh = *mesh_of(context);
    const std::uint32_t index = mesh.indices[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(vertex)];
    mesh.vertices[index].tangent = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

}  // namespace

void compute_tangents(MeshData& mesh) {
    if (mesh.indices.size() < 3) {
        return;
    }
    SMikkTSpaceInterface callbacks = {};
    callbacks.m_getNumFaces = face_count;
    callbacks.m_getNumVerticesOfFace = vertices_of_face;
    callbacks.m_getPosition = get_position;
    callbacks.m_getNormal = get_normal;
    callbacks.m_getTexCoord = get_texcoord;
    callbacks.m_setTSpaceBasic = set_tangent;
    SMikkTSpaceContext context = {};
    context.m_pInterface = &callbacks;
    context.m_pUserData = &mesh;
    genTangSpaceDefault(&context);
}

Aabb MeshData::bounds() const {
    Aabb box;
    for (const Vertex3D& vertex : vertices) {
        box.add(vertex.position);
    }
    return box;
}

MeshData make_cube(glm::vec3 size) {
    const glm::vec3 h = size * 0.5f;
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    // Each face: corners counter-clockwise when looking at it from outside.
    add_quad(mesh, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0, 0, 1});      // +Z
    add_quad(mesh, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {0, 0, -1});  // -Z
    add_quad(mesh, {h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {1, 0, 0});      // +X
    add_quad(mesh, {-h.x, -h.y, -h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z}, {-1, 0, 0});  // -X
    add_quad(mesh, {-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}, {0, 1, 0});      // +Y
    add_quad(mesh, {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z}, {0, -1, 0});  // -Y
    compute_tangents(mesh);
    return mesh;
}

MeshData make_plane(glm::vec2 size) {
    const glm::vec2 h = size * 0.5f;
    MeshData mesh;
    add_quad(mesh, {-h.x, 0.0f, h.y}, {h.x, 0.0f, h.y}, {h.x, 0.0f, -h.y}, {-h.x, 0.0f, -h.y}, {0, 1, 0});
    compute_tangents(mesh);
    return mesh;
}

MeshData make_sphere(float radius, int segments, int rings) {
    segments = std::max(segments, 3);
    rings = std::max(rings, 2);
    MeshData mesh;
    // A grid of (segments + 1) x (rings + 1) vertices: the seam column is duplicated so the texture
    // coordinates can go from 0 to 1 without wrapping.
    for (int ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float polar = v * glm::pi<float>();  // 0 at the top pole
        for (int segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float azimuth = u * glm::two_pi<float>();
            const glm::vec3 normal(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                                   -std::sin(polar) * std::sin(azimuth));
            mesh.vertices.push_back({normal * radius, normal, {u, v}, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
        }
    }
    const auto row = static_cast<std::uint32_t>(segments + 1);
    for (std::uint32_t ring = 0; ring < static_cast<std::uint32_t>(rings); ++ring) {
        for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(segments); ++segment) {
            const std::uint32_t top_left = ring * row + segment;
            const std::uint32_t bottom_left = top_left + row;
            // Azimuth grows counter-clockwise seen from above, so these are counter-clockwise from outside.
            // The pole rows give degenerate triangles, which the GPU skips.
            for (const std::uint32_t index : {top_left, bottom_left, bottom_left + 1, top_left, bottom_left + 1, top_left + 1}) {
                mesh.indices.push_back(index);
            }
        }
    }
    compute_tangents(mesh);
    return mesh;
}

Mesh Mesh::create(Renderer& renderer, const MeshData& data, const char* name) {
    if (data.vertices.empty() || data.indices.empty()) {
        throw std::invalid_argument(std::string("Mesh '") + (name != nullptr ? name : "") + "' has no geometry");
    }
    const std::string base = name != nullptr ? name : "mesh";
    Mesh mesh;
    mesh.vertices = renderer.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, data.vertices.data(),
                                           data.vertices.size() * sizeof(Vertex3D), (base + ".vertices").c_str());
    mesh.indices = renderer.create_buffer(SDL_GPU_BUFFERUSAGE_INDEX, data.indices.data(),
                                          data.indices.size() * sizeof(std::uint32_t), (base + ".indices").c_str());
    mesh.index_count = static_cast<std::uint32_t>(data.indices.size());
    mesh.bounds = data.bounds();
    mesh.gpu_bytes = data.vertices.size() * sizeof(Vertex3D) + data.indices.size() * sizeof(std::uint32_t);
    return mesh;
}

}  // namespace moteur

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "moteur/mesh.hpp"
#include "moteur/mesh_batcher.hpp"
#include "moteur/renderer.hpp"

namespace {

// Meshes without GPU buffers: the batcher only reads their box and index count.
moteur::Mesh make_mesh(std::uint32_t triangles) {
    moteur::Mesh mesh;
    mesh.index_count = triangles * 3;
    mesh.bounds.add(glm::vec3(-0.5f));
    mesh.bounds.add(glm::vec3(0.5f));
    return mesh;
}

moteur::MeshDraw make_draw(const moteur::Mesh& mesh, glm::vec3 position, glm::vec4 color = glm::vec4(1.0f)) {
    moteur::MeshDraw draw;
    draw.mesh = &mesh;
    draw.world = glm::translate(glm::mat4(1.0f), position);
    draw.material.base_color = color;
    draw.bounds = moteur::transform_box(mesh.bounds, draw.world);
    return draw;
}

// Sees the box [-10, 10] x [-10, 10] x [-10, 10] from above.
moteur::Frustum view_of_the_middle() {
    const glm::mat4 projection = glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 40.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    return moteur::Frustum::from_view_projection(projection * view);
}

// Sees everything the tests place.
moteur::Frustum view_of_everything() {
    const glm::mat4 projection = glm::orthoRH_ZO(-1000.0f, 1000.0f, -1000.0f, 1000.0f, 0.0f, 2000.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f, 1000.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    return moteur::Frustum::from_view_projection(projection * view);
}

}  // namespace

TEST_CASE("MeshBatcher: one batch per mesh and textures, whatever the colors") {
    const moteur::Mesh cube = make_mesh(12);
    const moteur::Mesh sphere = make_mesh(100);
    const moteur::Texture wood;  // only its address matters
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(cube, {0, 0, 0}, {1, 0, 0, 1}));
    draws.push_back(make_draw(sphere, {1, 0, 0}, {0, 1, 0, 1}));
    draws.push_back(make_draw(cube, {2, 0, 0}, {0, 0, 1, 1}));  // same batch as the first, other color
    draws.push_back(make_draw(cube, {3, 0, 0}));
    draws.back().material.base_color_texture = &wood;          // other textures: its own batch
    draws.push_back(make_draw(sphere, {4, 0, 0}, {1, 1, 0, 1}));

    moteur::MeshBatcher batcher;
    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    REQUIRE(batcher.batches().size() == 3);
    CHECK(batcher.visible() == 5);
    CHECK(batcher.submitted() == 5);
    CHECK(batcher.triangles() == 3 * 12 + 2 * 100);

    // In order of first appearance; each batch contiguous, in recording order.
    const auto& batches = batcher.batches();
    CHECK(batches[0].mesh == &cube);
    CHECK(batches[0].first_instance == 0);
    CHECK(batches[0].count == 2);
    CHECK(batches[1].mesh == &sphere);
    CHECK(batches[1].first_instance == 2);
    CHECK(batches[1].count == 2);
    CHECK(batches[2].mesh == &cube);
    CHECK(batches[2].material->base_color_texture == &wood);
    CHECK(batches[2].count == 1);

    const auto& instances = batcher.instances();
    CHECK(instances[0].base_color == glm::vec4(1, 0, 0, 1));
    CHECK(instances[1].base_color == glm::vec4(0, 0, 1, 1));
    CHECK(instances[2].base_color == glm::vec4(0, 1, 0, 1));
    CHECK(instances[3].base_color == glm::vec4(1, 1, 0, 1));
    CHECK(instances[4].world[0].w == 3.0f);  // the textured cube, at x = 3
}

TEST_CASE("MeshBatcher: single-sided batches come first, then double-sided ones") {
    const moteur::Mesh a = make_mesh(1);
    const moteur::Mesh b = make_mesh(1);
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(a, {0, 0, 0}));
    draws.back().material.double_sided = true;
    draws.push_back(make_draw(b, {0, 0, 0}));
    draws.push_back(make_draw(a, {0, 0, 0}));  // the same mesh, single-sided: another batch

    moteur::MeshBatcher batcher;
    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    REQUIRE(batcher.batches().size() == 3);
    CHECK_FALSE(batcher.batches()[0].material->double_sided);
    CHECK(batcher.batches()[0].mesh == &b);
    CHECK_FALSE(batcher.batches()[1].material->double_sided);
    CHECK(batcher.batches()[1].mesh == &a);
    CHECK(batcher.batches()[2].material->double_sided);
}

TEST_CASE("MeshBatcher: draws outside the frustum are dropped") {
    const moteur::Mesh cube = make_mesh(12);
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(cube, {0, 0, 0}));     // inside
    draws.push_back(make_draw(cube, {30, 0, 0}));    // far to the side
    draws.push_back(make_draw(cube, {10.3f, 0, 0}));  // straddles the edge: kept
    draws.push_back(make_draw(cube, {0, 0, -11}));   // just beyond the top edge
    draws.push_back(make_draw(cube, {-5, 0, 5}));    // inside

    moteur::MeshBatcher batcher;
    batcher.build(draws, view_of_the_middle(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.submitted() == 5);
    CHECK(batcher.visible() == 3);
    CHECK(batcher.triangles() == 3 * 12);
    REQUIRE(batcher.batches().size() == 1);
    CHECK(batcher.batches()[0].count == 3);
    CHECK(batcher.instances()[1].world[0].w == doctest::Approx(10.3f));

    // Nothing visible: no batch at all.
    std::vector<moteur::MeshDraw> far_away = {make_draw(cube, {100, 0, 0})};
    batcher.build(far_away, view_of_the_middle(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.batches().empty());
    CHECK(batcher.instances().empty());
    CHECK(batcher.submitted() == 1);
}

TEST_CASE("MeshBatcher: the order of the batches does not depend on what is visible") {
    const moteur::Mesh cube = make_mesh(12);
    const moteur::Mesh sphere = make_mesh(100);
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(sphere, {50, 0, 0}));  // outside the view, but recorded first
    draws.push_back(make_draw(cube, {0, 0, 0}));
    draws.push_back(make_draw(sphere, {1, 0, 0}));

    moteur::MeshBatcher everything;
    everything.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    moteur::MeshBatcher middle;
    middle.build(draws, view_of_the_middle(), moteur::MeshBatcher::Pass::Main);
    REQUIRE(everything.batches().size() == 2);
    REQUIRE(middle.batches().size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(middle.batches()[i].mesh == everything.batches()[i].mesh);
    }
    CHECK(middle.batches()[0].mesh == &sphere);
    CHECK(middle.batches()[0].count == 1);
    CHECK(middle.batches()[1].first_instance == 1);
}

TEST_CASE("MeshBatcher: a rotated object is culled with its rotated box") {
    // A long thin bar just outside the view, rotated so that it reaches inside.
    moteur::Mesh bar;
    bar.index_count = 36;
    bar.bounds.add({-6.0f, -0.1f, -0.1f});
    bar.bounds.add({6.0f, 0.1f, 0.1f});
    moteur::MeshDraw draw;
    draw.mesh = &bar;
    draw.world = glm::rotate(glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -14.0f}), glm::radians(90.0f), {0, 1, 0});
    draw.bounds = moteur::transform_box(bar.bounds, draw.world);  // now along Z: from -20 to -8
    moteur::MeshBatcher batcher;
    batcher.build({draw}, view_of_the_middle(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.visible() == 1);
}

TEST_CASE("MeshBatcher: the shadow pass keeps casters only and ignores textures") {
    const moteur::Mesh cube = make_mesh(12);
    const moteur::Texture wood;
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(cube, {0, 0, 0}));
    draws.push_back(make_draw(cube, {1, 0, 0}));
    draws.back().material.base_color_texture = &wood;
    draws.push_back(make_draw(cube, {2, 0, 0}));
    draws.back().material.casts_shadow = false;  // a flame

    moteur::MeshBatcher batcher;
    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Shadow);
    CHECK(batcher.submitted() == 2);
    CHECK(batcher.visible() == 2);
    REQUIRE(batcher.batches().size() == 1);
    CHECK(batcher.batches()[0].count == 2);

    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.visible() == 3);
    CHECK(batcher.batches().size() == 2);
}

TEST_CASE("MeshBatcher: instance data holds the matrix rows and the material") {
    moteur::MeshDraw draw;
    draw.world = glm::scale(glm::translate(glm::mat4(1.0f), {1.0f, 2.0f, 3.0f}), {2.0f, 1.0f, 1.0f});
    draw.material.metallic = 0.25f;
    draw.material.roughness = 0.75f;
    draw.material.normal_scale = 0.5f;
    draw.material.occlusion_strength = 0.1f;
    draw.material.emissive = {3.0f, 2.0f, 1.0f};
    const moteur::MeshInstance instance = moteur::MeshBatcher::make_instance(draw);

    // Rows: a point is (dot(row0, p), dot(row1, p), dot(row2, p)).
    const glm::vec4 p(1.0f, 1.0f, 1.0f, 1.0f);
    const glm::vec3 moved(glm::dot(instance.world[0], p), glm::dot(instance.world[1], p), glm::dot(instance.world[2], p));
    CHECK(moved == glm::vec3(draw.world * p));
    CHECK(moved == glm::vec3(3.0f, 3.0f, 4.0f));

    // A slanted surface stretched along X: its normal must stay perpendicular to it.
    const glm::vec3 tangent = glm::vec3(draw.world * glm::vec4(1.0f, -1.0f, 0.0f, 0.0f));  // along the surface
    const glm::vec3 normal(1.0f, 1.0f, 0.0f);
    const glm::vec3 world_normal(glm::dot(glm::vec3(instance.normal_matrix[0]), normal),
                                 glm::dot(glm::vec3(instance.normal_matrix[1]), normal),
                                 glm::dot(glm::vec3(instance.normal_matrix[2]), normal));
    CHECK(glm::dot(world_normal, tangent) == doctest::Approx(0.0f).epsilon(1e-6));

    CHECK(instance.factors == glm::vec4(0.25f, 0.75f, 0.5f, 0.1f));
    CHECK(instance.emissive == glm::vec4(3.0f, 2.0f, 1.0f, 0.0f));
}

TEST_CASE("MeshBatcher: many groups (beyond the plain scan) are found again") {
    std::vector<moteur::Mesh> meshes;
    for (int i = 0; i < 40; ++i) {
        meshes.push_back(make_mesh(1));
    }
    std::vector<moteur::MeshDraw> draws;
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 40; ++i) {
            // Every other round backwards, so the previous draw never helps.
            const int m = round % 2 == 0 ? i : 39 - i;
            draws.push_back(make_draw(meshes[static_cast<std::size_t>(m)], {0, 0, 0}));
        }
    }
    moteur::MeshBatcher batcher;
    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    REQUIRE(batcher.batches().size() == 40);
    for (std::size_t i = 0; i < 40; ++i) {
        CHECK(batcher.batches()[i].mesh == &meshes[i]);
        CHECK(batcher.batches()[i].count == 3);
        CHECK(batcher.batches()[i].first_instance == 3 * i);
    }
    // Built again (the table is cleared between frames): the same.
    batcher.build(draws, view_of_everything(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.batches().size() == 40);
}

TEST_CASE("MeshBatcher: a subset of the draws") {
    const moteur::Mesh cube = make_mesh(12);
    const moteur::Mesh sphere = make_mesh(100);
    std::vector<moteur::MeshDraw> draws;
    draws.push_back(make_draw(cube, {0, 0, 0}, {1, 0, 0, 1}));
    draws.push_back(make_draw(sphere, {1, 0, 0}));
    draws.push_back(make_draw(cube, {2, 0, 0}, {0, 1, 0, 1}));
    draws.push_back(make_draw(cube, {50, 0, 0}, {0, 0, 1, 1}));  // listed, but outside the view

    moteur::MeshBatcher batcher;
    batcher.build(draws, std::vector<std::uint32_t>{0, 2, 3}, view_of_the_middle(), moteur::MeshBatcher::Pass::Main);
    CHECK(batcher.submitted() == 3);
    CHECK(batcher.visible() == 2);
    REQUIRE(batcher.batches().size() == 1);
    CHECK(batcher.batches()[0].mesh == &cube);
    CHECK(batcher.instances()[0].base_color == glm::vec4(1, 0, 0, 1));
    CHECK(batcher.instances()[1].base_color == glm::vec4(0, 1, 0, 1));
}

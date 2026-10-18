#pragma once

#include "moteur/collision_scene.hpp"

// Levels for the tests of the 3D world (milestone 9, parts 5 and 6), shared by several files.

namespace test_levels {

inline moteur::Aabb box(glm::vec3 low, glm::vec3 high) {
    moteur::Aabb b;
    b.add(low);
    b.add(high);
    return b;
}

// The test level: a ground of 40 x 40 m; a wall along x = 5; ten steps of 0.2 x 0.3 m up to a
// platform 2 m high; a bridge deck 2.5 m above the ground (2 m wide, x 9.5 .. 11.5); a block 0.6 m high (too high a step);
// a ramp of 30 degrees and one of 60 degrees; a low ceiling over a step.
inline moteur::CollisionMeshData test_level_mesh() {
    moteur::CollisionMeshData mesh;
    mesh.add_box(box({-20, -1, -20}, {20, 0, 20}));                                   // ground
    mesh.add_box(box({5, 0, -20}, {6, 3, 0}), moteur::SurfaceKind::Wall);            // wall at x 5..6, z < 0
    mesh.add_stairs({-10, 0, 10}, 0, 10, 0.2f, 0.3f, 2.0f);                           // x -10 .. -7, up to 2 m
    mesh.add_box(box({-7, 0, 9}, {-3, 2, 11}));                                       // the platform at the top
    mesh.add_box(box({9.5f, 2.5f, -2}, {11.5f, 2.7f, 8}));                             // a bridge deck, 2 m wide
    mesh.add_box(box({0, 0, 15}, {2, 0.6f, 17}), moteur::SurfaceKind::Obstacle);      // too high a step
    mesh.add_ramp({-15, 0, -10}, 0, 3.4641f, 2.0f, 2.0f);                            // 30 degrees
    mesh.add_ramp({-15, 0, -15}, 0, 1.1547f, 2.0f, 2.0f);                            // 60 degrees
    mesh.add_box(box({14, 0, 14}, {16, 0.2f, 16}));                                   // a step...
    mesh.add_box(box({14, 1.6f, 13}, {16, 3, 17}), moteur::SurfaceKind::Wall);       // ...under a low ceiling
    return mesh;
}

inline moteur::CollisionScene test_level() {
    moteur::CollisionScene scene;
    scene.add(test_level_mesh(), glm::mat4(1.0f), 7);
    scene.build();
    return scene;
}

}  // namespace test_levels

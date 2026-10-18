#include "moteur/character_controller.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace moteur {

namespace {

// A contact normal more vertical than this is ground (from below) or a ceiling (from above), not a
// wall to slide along.
constexpr float kVerticalNormal = 0.7f;
// A wall met at a grazing angle is pushed out along the plane by depth / this at least: a cap on
// the push for nearly flat contacts.
constexpr float kMinHorizontal = 0.3f;

float length_of(glm::vec3 v) {
    return std::sqrt(glm::dot(v, v));
}

// The capsule's segment for the walls: its bottom lifted by the step height (steps are climbed, not
// pushed against), its top at the head.
void wall_segment(glm::vec3 feet, const CharacterShape& shape, const ControllerSettings& settings, glm::vec3& a,
                  glm::vec3& b) {
    const float bottom = feet.y + settings.step_height + shape.radius;
    const float top = std::max(feet.y + shape.height - shape.radius, bottom);
    a = {feet.x, bottom, feet.z};
    b = {feet.x, top, feet.z};
}

// The capsule pushed out of the walls on the plane, the deepest contact first, `max_passes` times
// at most. Sets `normal` to the last wall's plane normal; returns whether a wall was met.
bool resolve_walls(const CollisionScene& scene, glm::vec3& feet, const CharacterShape& shape,
                   const ControllerSettings& settings, std::vector<Contact>& contacts, glm::vec3& normal) {
    bool met = false;
    for (int pass = 0; pass < settings.max_passes; ++pass) {
        glm::vec3 a, b;
        wall_segment(feet, shape, settings, a, b);
        scene.capsule_contacts(a, b, shape.radius, kSurfaceSolid, contacts);
        bool pushed = false;
        for (const Contact& contact : contacts) {
            if (contact.normal.y > kVerticalNormal || contact.normal.y < -kVerticalNormal) {
                continue;  // ground under the lifted capsule, or a ceiling: not a wall
            }
            const glm::vec3 flat(contact.normal.x, 0.0f, contact.normal.z);
            const float length = length_of(flat);
            if (length < 1e-6f) {
                continue;
            }
            const glm::vec3 out = flat / length;
            feet += out * ((contact.depth + settings.skin) / std::max(length, kMinHorizontal));
            normal = out;
            met = true;
            pushed = true;
            break;  // contacts change once moved: look again
        }
        if (!pushed) {
            break;
        }
    }
    return met;
}

// True if something solid is above the capsule's lower part within its height (a ceiling too low).
bool head_blocked(const CollisionScene& scene, glm::vec3 feet, const CharacterShape& shape, std::vector<Contact>& contacts) {
    const glm::vec3 a(feet.x, feet.y + shape.radius + 0.01f, feet.z);
    const glm::vec3 b(feet.x, std::max(feet.y + shape.height - shape.radius, a.y), feet.z);
    scene.capsule_contacts(a, b, shape.radius * 0.9f, kSurfaceSolid, contacts);
    return std::any_of(contacts.begin(), contacts.end(),
                       [](const Contact& contact) { return contact.normal.y < -kVerticalNormal; });
}

}  // namespace

std::optional<GroundHit> ground_under(const CollisionScene& scene, glm::vec3 feet, const ControllerSettings& settings) {
    return scene.ground_at(feet, settings.step_height, settings.snap_distance, settings.min_ground_normal_y);
}

CharacterMove move_character(const CollisionScene& scene, glm::vec3 feet, glm::vec3 horizontal, float dt,
                             const CharacterMotion& motion, const CharacterShape& shape,
                             const ControllerSettings& settings) {
    CharacterMove result;
    result.motion = motion;
    std::vector<Contact> contacts;
    const glm::vec3 flat(horizontal.x, 0.0f, horizontal.z);
    const float distance = length_of(flat);
    const float step_length = std::max(shape.radius * 0.5f, 0.05f);
    const int steps = distance > 0.0f ? std::clamp(static_cast<int>(std::ceil(distance / step_length)), 1, 64) : 0;
    const glm::vec3 step = steps > 0 ? flat / static_cast<float>(steps) : glm::vec3(0.0f);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 before = feet;
        feet += step;
        glm::vec3 normal(0.0f);
        if (resolve_walls(scene, feet, shape, settings, contacts, normal)) {
            result.hit_wall = true;
            result.wall_normal = normal;
        }
        if (result.motion.grounded) {
            // Stay on the ground: up a step, or down within the snap distance.
            if (const std::optional<GroundHit> ground = ground_under(scene, feet, settings)) {
                const float old_height = feet.y;
                feet.y = ground->height;
                if (ground->height > old_height && head_blocked(scene, feet, shape, contacts)) {
                    feet = before;  // a step under a ceiling too low: refused
                    result.hit_wall = true;
                    continue;
                }
                if (std::fabs(feet.y - old_height) > 0.01f) {
                    result.stepped = true;
                }
                result.motion.ground_normal = ground->normal;
                result.motion.ground_kind = ground->kind;
                if (ground->kind == SurfaceKind::Void) {
                    result.motion.grounded = false;  // the void: it falls from here
                    result.motion.vertical_speed = 0.0f;
                }
            } else {
                result.motion.grounded = false;  // off a ledge
                result.motion.vertical_speed = 0.0f;
            }
        }
    }

    if (!result.motion.grounded && dt > 0.0f) {
        // Falling (or rising): gravity, then the ground met on the way down.
        float& speed = result.motion.vertical_speed;
        speed = std::max(speed - settings.gravity * dt, -settings.max_fall_speed);
        const float drop = speed * dt;
        if (drop < 0.0f) {
            const std::optional<GroundHit> ground = scene.ground_at(
                feet, 0.0f, -drop, settings.min_ground_normal_y, kSurfaceWalkable | kSurfaceObstacle);
            if (ground) {
                feet.y = ground->height;
                result.motion.grounded = true;
                result.motion.vertical_speed = 0.0f;
                result.motion.ground_normal = ground->normal;
                result.motion.ground_kind = ground->kind;
                result.landed = true;
            } else {
                feet.y += drop;
            }
        } else {
            // Rising: stopped by a ceiling above the head.
            const glm::vec3 head(feet.x, feet.y + shape.height, feet.z);
            if (const std::optional<RayHit> ceiling = scene.raycast(head, {0.0f, 1.0f, 0.0f}, drop, kSurfaceSolid)) {
                feet.y += ceiling->distance;
                speed = 0.0f;
            } else {
                feet.y += drop;
            }
        }
        glm::vec3 normal(0.0f);
        if (resolve_walls(scene, feet, shape, settings, contacts, normal)) {
            result.hit_wall = true;
            result.wall_normal = normal;
        }
    }
    result.position = feet;
    return result;
}

glm::vec3 push_out_of_level(const CollisionScene& scene, glm::vec3 feet, const CharacterShape& shape,
                            const ControllerSettings& settings) {
    std::vector<Contact> contacts;
    glm::vec3 normal(0.0f);
    resolve_walls(scene, feet, shape, settings, contacts, normal);
    return feet;
}

bool character_fits(const CollisionScene& scene, glm::vec3 feet, const CharacterShape& shape,
                    const ControllerSettings& settings) {
    glm::vec3 a, b;
    wall_segment(feet, shape, settings, a, b);
    std::vector<Contact> contacts;
    scene.capsule_contacts(a, b, shape.radius, kSurfaceSolid, contacts);
    return std::none_of(contacts.begin(), contacts.end(), [](const Contact& contact) {
        return contact.normal.y <= kVerticalNormal && contact.normal.y >= -kVerticalNormal;
    });
}

}  // namespace moteur

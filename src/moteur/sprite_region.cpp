#include "moteur/sprite_region.hpp"

namespace moteur {

PlacedSprite place_region(const SpriteRegion& region, glm::vec2 anchor, float scale, bool flip_x, bool flip_y) {
    glm::vec2 offset = region.offset;
    glm::vec2 pivot = region.pivot;

    // Mirroring the original image turns a position x into (source width - x); the trimmed
    // image, which spans [offset, offset + size], then starts at source width - offset - size.
    if (flip_x) {
        offset.x = region.source_size.x - offset.x - region.size.x;
        pivot.x = region.source_size.x - pivot.x;
    }
    if (flip_y) {
        offset.y = region.source_size.y - offset.y - region.size.y;
        pivot.y = region.source_size.y - pivot.y;
    }
    return {anchor + (offset - pivot) * scale, region.size * scale};
}

}  // namespace moteur

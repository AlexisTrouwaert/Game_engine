#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "moteur/collision.hpp"
#include "moteur/movement.hpp"
#include "moteur/particles.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/visibility.hpp"
#include "moteur/world.hpp"

// Milestone 6: collisions, pathfinding, queries, sight and movement on grids drawn in characters.
// '.' floor, '#' wall, '=' fence (blocks the way, not the sight), '%' tall grass (walkable, opaque).

namespace {

struct Grid {
    moteur::NavGrid nav;
    moteur::TileMap tiles{1, 1};
    moteur::Tileset tileset;
};

Grid make_grid(const std::vector<std::string>& rows) {
    Grid g;
    const moteur::TileId floor = g.tileset.add({"", true, false});
    const moteur::TileId wall = g.tileset.add({"", false, true});
    const moteur::TileId fence = g.tileset.add({"", false, false});
    const moteur::TileId grass = g.tileset.add({"", true, true});
    g.tiles = moteur::TileMap(static_cast<int>(rows[0].size()), static_cast<int>(rows.size()), 2);
    for (int j = 0; j < static_cast<int>(rows.size()); ++j) {
        for (int i = 0; i < static_cast<int>(rows[0].size()); ++i) {
            g.tiles.set(0, {i, j}, floor);
            switch (rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]) {
                case '#': g.tiles.set(1, {i, j}, wall); break;
                case '=': g.tiles.set(1, {i, j}, fence); break;
                case '%': g.tiles.set(1, {i, j}, grass); break;
                default: break;
            }
        }
    }
    g.nav = moteur::NavGrid(g.tiles, g.tileset);
    return g;
}

bool near(glm::vec2 a, glm::vec2 b, float tolerance = 1e-3f) {
    return glm::length(a - b) <= tolerance;
}

// Every point of a path, and every segment between them, fits the circle.
bool path_fits(const moteur::NavGrid& grid, glm::vec2 start, const std::vector<glm::vec2>& points, float radius) {
    glm::vec2 from = start;
    for (const glm::vec2 p : points) {
        if (!moteur::segment_clear(grid, from, p, radius)) {
            return false;
        }
        from = p;
    }
    return true;
}

const std::vector<std::string> kRoom = {
    "##########",
    "#........#",
    "#........#",
    "#...##...#",
    "#...##...#",
    "#........#",
    "##########",
};

}  // namespace

TEST_CASE("Circles fit, are pushed out of walls and slide along them") {
    const Grid g = make_grid(kRoom);
    CHECK(moteur::circle_fits(g.nav, {1.5f, 1.5f}, 0.5f));      // touching two walls exactly
    CHECK_FALSE(moteur::circle_fits(g.nav, {1.4f, 1.5f}, 0.5f));
    CHECK(near(moteur::push_out_of_walls(g.nav, {1.2f, 2.0f}, 0.4f), {1.4f, 2.0f}));
    // Into a corner: out of both walls.
    CHECK(near(moteur::push_out_of_walls(g.nav, {1.1f, 1.1f}, 0.3f), {1.3f, 1.3f}));
    // A centre inside a wall: out by the nearest face.
    CHECK(moteur::circle_fits(g.nav, moteur::push_out_of_walls(g.nav, {4.9f, 3.5f}, 0.3f), 0.3f));

    // Moving diagonally into the left wall: x stops at the wall, z keeps going (slides).
    const glm::vec2 end = moteur::move_circle(g.nav, {2.0f, 2.0f}, {-2.0f, 1.0f}, 0.3f);
    CHECK(end.x == doctest::Approx(1.3f).epsilon(1e-4));
    CHECK(end.y == doctest::Approx(3.0f).epsilon(1e-4));
    // Round the corner of the pillar: never inside, comes out the other side.
    const glm::vec2 round = moteur::move_circle(g.nav, {3.5f, 2.6f}, {3.0f, 0.0f}, 0.3f);
    CHECK(moteur::circle_fits(g.nav, round, 0.3f));
    CHECK(round.x > 6.0f);
}

TEST_CASE("nearest_fit and segment_clear") {
    const Grid g = make_grid(kRoom);
    CHECK(near(*moteur::nearest_fit(g.nav, {2.0f, 2.0f}, 0.3f), {2.0f, 2.0f}));
    const auto out = moteur::nearest_fit(g.nav, {4.6f, 3.6f}, 0.3f);  // inside the pillar
    REQUIRE(out);
    CHECK(moteur::circle_fits(g.nav, *out, 0.3f));
    CHECK(moteur::segment_clear(g.nav, {1.5f, 1.5f}, {8.5f, 1.5f}, 0.5f));
    CHECK_FALSE(moteur::segment_clear(g.nav, {1.5f, 3.5f}, {8.5f, 3.5f}, 0.3f));  // through the pillar
    CHECK_FALSE(moteur::segment_clear(g.nav, {1.5f, 2.6f}, {8.5f, 2.6f}, 0.7f));  // too wide to pass by
    CHECK(moteur::segment_clear(g.nav, {1.5f, 2.6f}, {8.5f, 2.6f}, 0.3f));
}

TEST_CASE("Colliders push each other apart, by weight, the same in any order") {
    const Grid g = make_grid(kRoom);
    auto run = [&](bool reversed, int weight_b) {
        entt::registry registry;
        std::vector<entt::entity> e(2);
        for (int k = 0; k < 2; ++k) {
            e[static_cast<std::size_t>(k)] = registry.create();
        }
        if (reversed) {
            std::swap(e[0], e[1]);
        }
        moteur::Transform ta, tb;
        ta.position = {3.0f, 0.0f, 2.0f};
        tb.position = {3.4f, 0.0f, 2.0f};
        registry.emplace<moteur::Transform>(e[0], ta);
        registry.emplace<moteur::Transform>(e[1], tb);
        registry.emplace<moteur::Collider>(e[0], moteur::Collider{0.35f});
        moteur::Collider cb{0.35f};
        cb.push_weight = weight_b;
        registry.emplace<moteur::Collider>(e[1], cb);
        moteur::SpatialHash hash;
        moteur::SeparationSettings settings;
        settings.stiffness = 1.0f;
        settings.iterations = 1;
        const moteur::CollisionStats stats = moteur::separate_colliders(registry, g.nav, hash, settings);
        CHECK(stats.overlaps == 1);
        return std::pair{moteur::plane_position(registry, e[0]), moteur::plane_position(registry, e[1])};
    };
    const auto [a, b] = run(false, 1);
    CHECK(a.x == doctest::Approx(2.85f));
    CHECK(b.x == doctest::Approx(3.55f));
    const auto [a2, b2] = run(true, 1);  // created in the other order
    CHECK(a2 == a);
    CHECK(b2 == b);
    const auto [a3, b3] = run(false, 0);  // b immovable
    CHECK(a3.x == doctest::Approx(2.7f));
    CHECK(b3.x == doctest::Approx(3.4f));
}

TEST_CASE("Clearance: a circle passes where the clearance is at least its radius") {
    const Grid g = make_grid({
        "#######",
        "#.....#",
        "###.###",
        "#.....#",
        "#######",
    });
    const moteur::ClearanceMap clearance(g.nav);
    CHECK(clearance.clearance({3, 2}) == doctest::Approx(0.5f));  // a gap of one cell
    CHECK(clearance.clearance({0, 0}) == 0.0f);
    CHECK(clearance.fits({3, 2}, 0.4f));
    CHECK_FALSE(clearance.fits({3, 2}, 0.6f));
    CHECK(clearance.version() == g.nav.version());

    // A wall put in the gap: the cells around follow, as if computed from scratch.
    Grid changed = make_grid({
        "#######",
        "#.....#",
        "###.###",
        "#.....#",
        "#######",
    });
    moteur::ClearanceMap updated(changed.nav);
    changed.nav.set({3, 2}, false, true);
    updated.update_around({3, 2});
    const moteur::ClearanceMap fresh(changed.nav);
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 7; ++i) {
            CHECK(updated.clearance({i, j}) == fresh.clearance({i, j}));
        }
    }
    CHECK(updated.version() == changed.nav.version());
}

TEST_CASE("A* goes round walls without cutting corners and arrives at the goal") {
    const Grid g = make_grid({
        "##########",
        "#........#",
        "#.######.#",
        "#.#....#.#",
        "#.#.##.#.#",
        "#...##...#",
        "##########",
    });
    const moteur::ClearanceMap clearance(g.nav);
    moteur::PathOptions options;
    options.radius = 0.3f;
    options.smooth = false;
    const moteur::PathResult raw = moteur::find_path(g.nav, clearance, {3.5f, 5.5f}, {6.5f, 5.5f}, options);
    REQUIRE(raw.status == moteur::PathStatus::Found);
    CHECK(raw.points.back() == glm::vec2(6.5f, 5.5f));
    CHECK(path_fits(g.nav, {3.5f, 5.5f}, raw.points, 0.3f));
    // The straight way is 3 cells, but the pillar is in the way: up and round it.
    CHECK(raw.cost > 30);
    options.smooth = true;
    const moteur::PathResult smooth = moteur::find_path(g.nav, clearance, {3.5f, 5.5f}, {6.5f, 5.5f}, options);
    CHECK(smooth.points.size() < raw.points.size());
    CHECK(path_fits(g.nav, {3.5f, 5.5f}, smooth.points, 0.3f));
    CHECK(smooth.cost == raw.cost);
    // The same search again gives the same path.
    CHECK(moteur::find_path(g.nav, clearance, {3.5f, 5.5f}, {6.5f, 5.5f}, options).points == smooth.points);
}

TEST_CASE("A*: a big circle avoids the narrow passage; an unreachable goal gives the nearest cell") {
    const Grid g = make_grid({
        "#########",
        "#...#...#",
        "#.......#",  // a gap of one cell
        "#...#...#",
        "#...#...#",
        "#.......#",  // a gap of one cell, but lower: same for this test
        "#...#...#",
        "#########",
    });
    const moteur::ClearanceMap clearance(g.nav);
    moteur::PathOptions small;
    small.radius = 0.3f;
    CHECK(moteur::find_path(g.nav, clearance, {2.0f, 3.5f}, {6.5f, 3.5f}, small).status == moteur::PathStatus::Found);
    moteur::PathOptions big;
    big.radius = 0.7f;  // wider than the gaps
    const moteur::PathResult blocked = moteur::find_path(g.nav, clearance, {2.0f, 3.5f}, {6.5f, 3.5f}, big);
    CHECK(blocked.status == moteur::PathStatus::Partial);

    const Grid island = make_grid({
        "#########",
        "#.......#",
        "#..===..#",
        "#..=.=..#",
        "#..===..#",
        "#.......#",
        "#########",
    });
    const moteur::ClearanceMap island_clearance(island.nav);
    const moteur::PathResult partial = moteur::find_path(island.nav, island_clearance, {1.5f, 1.5f}, {4.5f, 3.5f}, small);
    REQUIRE(partial.status == moteur::PathStatus::Partial);
    REQUIRE_FALSE(partial.points.empty());
    const glm::vec2 end = partial.points.back();
    CHECK(glm::length(end - glm::vec2(4.5f, 3.5f)) <= 2.01f);  // next to the fence
}

TEST_CASE("Flow field: every reached cell leads to the target, costs match A*") {
    const Grid g = make_grid(kRoom);
    const moteur::ClearanceMap clearance(g.nav);
    moteur::FlowField field;
    const int reached = field.compute(g.nav, clearance, {8.5f, 5.5f}, 0.3f);
    CHECK(reached > 30);
    CHECK(field.next(field.target_cell()) == field.target_cell());
    CHECK(field.cost({0, 0}) == moteur::FlowField::kUnreached);
    // Following next() from a cell reaches the target in cost / 10 steps at most.
    glm::ivec2 cell{1, 1};
    int steps = 0;
    while (cell != field.target_cell() && steps < 100) {
        const glm::ivec2 next = field.next(cell);
        CHECK(field.cost(next) < field.cost(cell));
        cell = next;
        ++steps;
    }
    CHECK(cell == field.target_cell());
    moteur::PathOptions options;
    options.radius = 0.3f;
    options.smooth = false;
    CHECK(moteur::find_path(g.nav, clearance, {1.5f, 1.5f}, {8.5f, 5.5f}, options).cost == field.cost({1, 1}));
    const glm::vec2 d = field.direction({1.5f, 1.5f});
    CHECK(glm::length(d) == doctest::Approx(1.0f));
}

TEST_CASE("Spatial queries: circle, cone and rectangle, sorted by distance then identifier") {
    entt::registry registry;
    moteur::SpatialHash hash(2.0f);
    std::vector<entt::entity> e;
    const glm::vec2 places[] = {{1.0f, 0.0f}, {3.0f, 0.0f}, {0.0f, 3.0f}, {-2.0f, 0.0f}, {1.0f, 0.0f}};
    for (const glm::vec2 p : places) {
        e.push_back(registry.create());
        hash.insert(e.back(), p, 0.3f, e.size() == 4 ? 2u : 1u);
    }
    const auto circle = hash.query_circle({0.0f, 0.0f}, 1.0f);
    REQUIRE(circle.size() == 2);
    CHECK(circle[0] == e[0]);  // same distance: lower identifier first
    CHECK(circle[1] == e[4]);
    CHECK(hash.query_circle({0.0f, 0.0f}, 2.0f, 2u) == std::vector<entt::entity>{e[3]});
    const auto cone = hash.query_cone({0.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, 0.7f);
    CHECK(cone == std::vector<entt::entity>{e[0], e[4], e[1]});
    CHECK(hash.query_cone({0.0f, 0.0f}, {1.0f, 0.0f}, 4.0f, -1.0f).size() == 5);
    const auto rect = hash.query_rect({0.0f, 0.0f}, {0.0f, 1.0f}, 3.0f, 0.5f);
    CHECK(rect == std::vector<entt::entity>{e[2]});
}

TEST_CASE("Rays stop at walls, fences only for the way, never through a corner") {
    const Grid g = make_grid({
        "########",
        "#......#",
        "#.=....#",
        "#...#..#",
        "#..#...#",
        "########",
    });
    const auto wall = moteur::raycast(g.nav, {1.5f, 3.5f}, {6.5f, 3.5f});
    REQUIRE(wall);
    CHECK(wall->cell == glm::ivec2(4, 3));
    CHECK(wall->point.x == doctest::Approx(4.0f));
    CHECK(wall->normal == glm::vec2(-1.0f, 0.0f));
    CHECK(wall->distance == doctest::Approx(2.5f));
    // The fence: seen through, but it stops what flies.
    CHECK_FALSE(moteur::raycast(g.nav, {1.5f, 2.5f}, {5.5f, 2.5f}, moteur::RayBlock::Opaque));
    CHECK(moteur::raycast(g.nav, {1.5f, 2.5f}, {5.5f, 2.5f}, moteur::RayBlock::Unwalkable)->cell == glm::ivec2(2, 2));
    // Exactly through the corner between (3, 4) and (4, 3): blocked.
    CHECK(moteur::raycast(g.nav, {3.5f, 3.5f}, {4.5f, 4.5f}));
    CHECK_FALSE(moteur::raycast(g.nav, {1.5f, 1.5f}, {3.5f, 3.5f}));  // corners between open cells
    CHECK_FALSE(moteur::line_of_sight(g.nav, {3.5f, 3.5f}, {4.5f, 4.5f}));  // the diagonal of the corner
    CHECK(moteur::line_of_sight(g.nav, {1.5f, 1.5f}, {6.5f, 1.5f}));
}

TEST_CASE("Line of sight and field of view are symmetric") {
    const Grid g = make_grid({
        "##############",
        "#............#",
        "#..#.....%...#",
        "#..#..##.....#",
        "#.......#....#",
        "#..##........#",
        "#............#",
        "##############",
    });
    for (int a = 0; a < 14 * 8; a += 3) {
        for (int b = 0; b < 14 * 8; b += 5) {
            const glm::ivec2 ca(a % 14, a / 14), cb(b % 14, b / 14);
            if (!g.nav.walkable(ca) || !g.nav.walkable(cb)) {
                continue;
            }
            CAPTURE(ca);
            CAPTURE(cb);
            CHECK(moteur::line_of_sight(g.nav, moteur::cell_centre(ca), moteur::cell_centre(cb)) ==
                  moteur::line_of_sight(g.nav, moteur::cell_centre(cb), moteur::cell_centre(ca)));
            moteur::FieldOfView from_a, from_b;
            from_a.compute(g.nav, ca, 20);
            from_b.compute(g.nav, cb, 20);
            CHECK(from_a.visible(cb) == from_b.visible(ca));
        }
    }
}

TEST_CASE("Field of view: walls seen, nothing behind them; exploration accumulates") {
    const Grid g = make_grid({
        "#########",
        "#.......#",
        "#.......#",
        "####.####",
        "#.......#",
        "#.......#",
        "#########",
    });
    moteur::FieldOfView view;
    view.compute(g.nav, {4, 1}, 10);
    CHECK(view.visible({4, 1}));
    CHECK(view.visible({1, 2}));
    CHECK(view.visible({0, 3}));      // the wall of the room
    CHECK(view.visible({4, 4}));      // through the door
    CHECK_FALSE(view.visible({1, 5}));  // behind the wall, out of the door's cone
    moteur::FieldOfView small;
    small.compute(g.nav, {4, 1}, 2);
    CHECK_FALSE(small.visible({1, 1}));  // beyond the radius
    moteur::ExploredMap explored(9, 7);
    const int first = explored.add(small);
    CHECK(first == explored.count());
    CHECK(explored.add(small) == 0);
    CHECK(explored.add(view) > 0);
    CHECK(explored.explored({4, 4}));
    CHECK_FALSE(explored.explored({1, 5}));
}

TEST_CASE("Movers follow their path, slide on walls, and give their real speed") {
    const Grid g = make_grid(kRoom);
    const moteur::ClearanceMap clearance(g.nav);
    entt::registry registry;
    const entt::entity e = registry.create();
    moteur::Transform start;
    start.position = {1.5f, 0.0f, 3.5f};
    registry.emplace<moteur::Transform>(e, start);
    registry.emplace<moteur::Collider>(e, moteur::Collider{0.3f});
    moteur::Mover& mover = registry.emplace<moteur::Mover>(e);
    mover.speed = 3.0f;
    mover.go_to({8.5f, 3.5f});  // the pillar is in the way
    moteur::SpatialHash hash;
    const float dt = 1.0f / 60.0f;
    int ticks = 0;
    while (registry.get<moteur::Mover>(e).state != moteur::MoveState::Arrived && ticks < 600) {
        moteur::MovementStats stats;
        moteur::plan_paths(registry, g.nav, clearance, 4, stats);
        moteur::move_movers(registry, g.nav, dt, nullptr, stats);
        moteur::separate_colliders(registry, g.nav, hash);
        moteur::finish_movers(registry, dt);
        CHECK(moteur::circle_fits(g.nav, moteur::plane_position(registry, e), 0.3f));
        ++ticks;
    }
    CHECK(registry.get<moteur::Mover>(e).state == moteur::MoveState::Arrived);
    CHECK(near(moteur::plane_position(registry, e), {8.5f, 3.5f}));
    CHECK(ticks < 200);  // about 8 m at 3 m/s: under 3 s

    // Direct, into the wall: it slides, and its real speed is the slide's.
    moteur::Mover& direct = registry.get<moteur::Mover>(e);
    direct.move({1.0f, -1.0f});  // up-right: the top wall is 2 cells away
    for (int i = 0; i < 120; ++i) {
        moteur::MovementStats stats;
        moteur::move_movers(registry, g.nav, dt, nullptr, stats);
        moteur::finish_movers(registry, dt);
    }
    const glm::vec2 p = moteur::plane_position(registry, e);
    CHECK(p.y == doctest::Approx(1.3f).epsilon(1e-3));
    CHECK(registry.get<moteur::Mover>(e).actual_speed < 0.1f);  // in the corner now
}

TEST_CASE("facing_rotation turns +z towards the facing") {
    for (const glm::vec2 f : {glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, -1.0f),
                              glm::vec2(-0.6f, 0.8f), glm::vec2(-1.0f, 0.0f)}) {
        const glm::vec3 forward = moteur::facing_rotation(f) * glm::vec3(0.0f, 0.0f, 1.0f);
        CHECK(forward.x == doctest::Approx(f.x).epsilon(1e-4));
        CHECK(forward.z == doctest::Approx(f.y).epsilon(1e-4));
        CHECK(forward.y == doctest::Approx(0.0f));
    }
}

TEST_CASE("Worst cases, measured: A* across a big maze, flow field and field of view on 400 x 400") {
    // A maze of 199 x 199 corridors (401 x 401 cells), recursive backtracker with a fixed seed.
    constexpr int kCorridors = 199;
    constexpr int kSide = 2 * kCorridors + 1;
    std::vector<std::string> rows(kSide, std::string(kSide, '#'));
    std::vector<std::uint8_t> seen(kCorridors * kCorridors, 0);
    std::vector<glm::ivec2> stack{{0, 0}};
    seen[0] = 1;
    rows[1][1] = '.';
    std::uint32_t random = 12345;
    while (!stack.empty()) {
        const glm::ivec2 c = stack.back();
        glm::ivec2 options[4];
        int count = 0;
        for (const glm::ivec2 d : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
            const glm::ivec2 n = c + d;
            if (n.x >= 0 && n.y >= 0 && n.x < kCorridors && n.y < kCorridors && !seen[static_cast<std::size_t>(n.y * kCorridors + n.x)]) {
                options[count++] = n;
            }
        }
        if (count == 0) {
            stack.pop_back();
            continue;
        }
        random = random * 1664525u + 1013904223u;
        const glm::ivec2 n = options[(random >> 16) % static_cast<std::uint32_t>(count)];
        rows[static_cast<std::size_t>(c.y + n.y + 1)][static_cast<std::size_t>(c.x + n.x + 1)] = '.';
        rows[static_cast<std::size_t>(2 * n.y + 1)][static_cast<std::size_t>(2 * n.x + 1)] = '.';
        seen[static_cast<std::size_t>(n.y * kCorridors + n.x)] = 1;
        stack.push_back(n);
    }
    const Grid g = make_grid(rows);
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

    auto t0 = Clock::now();
    const moteur::ClearanceMap clearance(g.nav);
    const double clearance_ms = ms(Clock::now() - t0);

    moteur::PathOptions options;
    options.radius = 0.3f;
    options.max_nodes = 1000000;
    t0 = Clock::now();
    const moteur::PathResult path = moteur::find_path(g.nav, clearance, {1.5f, 1.5f}, {kSide - 1.5f, kSide - 1.5f}, options);
    const double path_ms = ms(Clock::now() - t0);
    CHECK(path.status == moteur::PathStatus::Found);
    CHECK(path_fits(g.nav, {1.5f, 1.5f}, path.points, 0.3f));

    moteur::FlowField field;
    t0 = Clock::now();
    const int reached = field.compute(g.nav, clearance, {1.5f, 1.5f}, 0.3f, 1 << 30);
    const double field_ms = ms(Clock::now() - t0);
    CHECK(reached > kCorridors * kCorridors);

    // The field of view in an open room of 400 x 400, radius 20.
    const Grid open = make_grid(std::vector<std::string>(400, std::string(400, '.')));
    moteur::FieldOfView view;
    t0 = Clock::now();
    for (int i = 0; i < 100; ++i) {
        view.compute(open.nav, {200, 200}, 20);
    }
    const double view_ms = ms(Clock::now() - t0) / 100.0;
    CHECK(view.cells().size() > 1200);

    MESSAGE("clearance 401 x 401: " << clearance_ms << " ms; A* across the maze: " << path.expanded << " cells, "
                                    << path_ms << " ms; flow field: " << reached << " cells, " << field_ms
                                    << " ms; field of view r = 20: " << view_ms << " ms");
}

TEST_CASE("20 000 particles, measured") {
    moteur::ParticleSystem particles(20000, 3);
    const auto fire = moteur::make_asset(moteur::ParticleEffect::parse(
        R"({"version": 1, "emitters": [{"texture": "a.png", "rate": 500, "loop": true, "duration": 1,
            "lifetime": [1, 2], "shape": "sphere", "radius": 0.5, "speed": [0.5, 1], "gravity": -1, "drag": 0.5,
            "size": [[0, 0.2], [1, 0.0]], "color": [[0, [1, 1, 1, 0]], [1, [0, 0, 0, 0]]], "spin": [-1, 1]}]})",
        "load.json"));
    for (int i = 0; i < 40; ++i) {
        particles.play(fire, glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
    }
    for (int i = 0; i < 120; ++i) {
        particles.update(1.0f / 60.0f);
    }
    CHECK(particles.stats().particles >= 19000);  // the budget, less those that died this frame
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    for (int i = 0; i < 60; ++i) {
        particles.update(1.0f / 60.0f);
    }
    const double update_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / 60.0;
    MESSAGE("20 000 particles: update " << update_ms << " ms per frame");
}

#include "engine.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace usk {
namespace {

constexpr float epsilon = 0.0001f;
constexpr float playerRadius = 0.24f;
constexpr float playerHeight = 1.68f;
constexpr float playerStep = 0.49f;

bool same(Vec a, Vec b) { return length(a - b) < epsilon; }

bool contains(const Sector& sector, Vec pos) {
    for (const auto& wall : sector.walls) {
        if (cross(wall.b - wall.a, pos - wall.a) < -epsilon) return false;
    }
    return true;
}

float distanceToWall(Vec p, const Wall& w) {
    Vec edge = w.b - w.a;
    float u = std::clamp(dot(p - w.a, edge) / dot(edge, edge), 0.0f, 1.0f);
    return length(p - (w.a + edge * u));
}

bool passable(const Level& level, int sector, const Wall& wall, float body, float step) {
    if (wall.next < 0) return false;
    const auto& a = level.sectors[sector];
    const auto& b = level.sectors[wall.next];
    return std::abs(b.floor - a.floor) <= step
        && level.portalTop(a, wall) - std::max(a.floor, b.floor) >= body;
}

Material parseMaterial(char tag) {
    switch (tag) {
        case 's': return Material::Steel;
        case 'r': return Material::Rust;
        case 'h': return Material::Hazard;
        case 'c': return Material::Reactor;
        default: throw std::runtime_error("Nieznany material sciany");
    }
}

Kind parseKind(const std::string& name) {
    if (name == "guard") return Kind::Guard;
    if (name == "key") return Kind::Key;
    if (name == "ammo") return Kind::Ammo;
    if (name == "health") return Kind::Health;
    if (name == "exit") return Kind::Exit;
    throw std::runtime_error("Nieznany obiekt: " + name);
}

bool validPoint(Vec p) { return std::abs(p.x) <= 4096 && std::abs(p.y) <= 4096; }

} // namespace

Level::Level(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Nie mozna otworzyc mapy: " + path.string());

    bool hasSpawn = false;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (const auto end = line.find('#'); end != std::string::npos) line.resize(end);
        std::istringstream row(line);
        std::string type;
        if (!(row >> type)) continue;
        auto fail = [&]() {
            throw std::runtime_error("Nieprawidlowa mapa, linia " + std::to_string(lineNumber));
        };
        if (type == "sector") {
            Sector sector;
            int vertices;
            if (!(row >> sector.floor >> sector.ceiling >> sector.light >> vertices)
                || std::abs(sector.floor) > 4096 || std::abs(sector.ceiling) > 4096
                || !std::isfinite(sector.light) || sector.ceiling - sector.floor < playerHeight
                || sector.light < 0.15f || sector.light > 1.3f || vertices < 3 || vertices > 64) {
                fail();
            }
            for (int i = 0; i < vertices; ++i) {
                Wall wall;
                char material;
                if (!(row >> wall.a.x >> wall.a.y >> material) || !validPoint(wall.a)) fail();
                wall.material = parseMaterial(material);
                sector.walls.push_back(wall);
            }
            float doubleArea = 0;
            const Vec origin = sector.walls.front().a;
            for (int i = 0; i < vertices; ++i) {
                auto& wall = sector.walls[i];
                wall.b = sector.walls[(i + 1) % vertices].a;
                if (same(wall.a, wall.b)) fail();
                doubleArea += cross(wall.a - origin, wall.b - origin);
            }
            if (doubleArea <= epsilon) fail();
            for (int i = 0; i < vertices; ++i) {
                const Vec a = sector.walls[i].a;
                for (int j = i + 1; j < vertices; ++j) {
                    if (same(a, sector.walls[j].a)) fail();
                }
                if (!contains(sector, a)) fail();
            }
            sectors.push_back(std::move(sector));
        } else if (type == "spawn") {
            if (hasSpawn || !(row >> spawnSector >> spawn.x >> spawn.y >> spawnAngle)
                || !validPoint(spawn) || !std::isfinite(spawnAngle)) fail();
            spawnAngle = std::remainder(spawnAngle, 360.0f) * pi / 180;
            hasSpawn = true;
        } else if (type == "door") {
            Door door;
            if (!(row >> door.a >> door.b) || door.a == door.b) fail();
            doors.push_back(door);
        } else if (type == "thing") {
            std::string name;
            Thing thing;
            if (!(row >> name >> thing.sector >> thing.pos.x >> thing.pos.y) || !validPoint(thing.pos)) fail();
            thing.kind = parseKind(name);
            things.push_back(thing);
        } else {
            fail();
        }
        std::string extra;
        if (row >> extra) fail();
    }

    auto validSector = [&](int id) { return id >= 0 && id < static_cast<int>(sectors.size()); };
    if (!hasSpawn || !validSector(spawnSector)) throw std::runtime_error("Brak poprawnego startu mapy");
    for (int a = 0; a < static_cast<int>(sectors.size()); ++a) {
        for (int b = a + 1; b < static_cast<int>(sectors.size()); ++b) {
            for (auto& left : sectors[a].walls) {
                for (auto& right : sectors[b].walls) {
                    if (!same(left.a, right.b) || !same(left.b, right.a)) continue;
                    if (left.next >= 0 || right.next >= 0) {
                        throw std::runtime_error("Krawedz portalu nalezy do ponad dwoch sektorow");
                    }
                    left.next = b;
                    right.next = a;
                }
            }
        }
    }

    for (int i = 0; i < static_cast<int>(doors.size()); ++i) {
        const auto& door = doors[i];
        if (!validSector(door.a) || !validSector(door.b)) {
            throw std::runtime_error("Drzwi wskazuja nieistniejacy sektor");
        }
        for (const auto& [from, to] : {std::pair{door.a, door.b}, std::pair{door.b, door.a}}) {
            Wall* portal = nullptr;
            for (auto& wall : sectors[from].walls) {
                if (wall.next != to) continue;
                if (portal || wall.door >= 0) throw std::runtime_error("Niejednoznaczne drzwi");
                portal = &wall;
            }
            if (!portal) throw std::runtime_error("Drzwi wymagaja wspolnej krawedzi");
            portal->door = i;
        }
    }

    if (!contains(sectors[spawnSector], spawn)
        || !hasClearance(spawn, spawnSector, playerRadius, playerHeight, playerStep)) {
        throw std::runtime_error("Start gracza koliduje z geometria mapy");
    }
    for (const auto& thing : things) {
        if (!validSector(thing.sector) || !contains(sectors[thing.sector], thing.pos)
            || (thing.kind == Kind::Guard && !hasClearance(thing.pos, thing.sector, 0.22f, 1.45f, 0.45f))) {
            throw std::runtime_error("Obiekt ma nieprawidlowe polozenie");
        }
    }
}

Boundary Level::ray(Vec from, Vec dir, int sector, float after) const {
    Boundary hit{nullptr, std::numeric_limits<float>::infinity(), 0};
    for (const auto& wall : sectors[sector].walls) {
        Vec edge = wall.b - wall.a;
        float denom = cross(dir, edge);
        if (denom <= std::numeric_limits<float>::epsilon() * length(edge) * length(dir)) continue;
        const Vec offset = wall.a - from;
        float depth = cross(offset, edge) / denom;
        float u = cross(offset, dir) / denom;
        if (depth >= after && depth < hit.depth && u >= -epsilon && u <= 1 + epsilon) {
            hit = {&wall, depth, std::clamp(u, 0.0f, 1.0f) * length(edge)};
        }
    }
    return hit;
}

float Level::portalTop(const Sector& source, const Wall& wall) const {
    const auto& target = sectors[wall.next];
    float top = std::min(source.ceiling, target.ceiling);
    if (wall.door < 0) return top;
    float bottom = std::max(source.floor, target.floor);
    return bottom + (top - bottom) * doors[wall.door].open;
}

bool Level::hasClearance(Vec pos, int sector, float radius, float body, float step) const {
    // Check both sides of openings within the collision circle.
    std::vector<int> nearby{sector};
    for (std::size_t i = 0; i < nearby.size(); ++i) {
        const int id = nearby[i];
        for (const auto& wall : sectors[id].walls) {
            if (distanceToWall(pos, wall) >= radius) continue;
            if (!passable(*this, id, wall, body, step)) return false;
            if (std::find(nearby.begin(), nearby.end(), wall.next) == nearby.end()) nearby.push_back(wall.next);
        }
    }
    return true;
}

bool Level::move(Vec& from, int& sector, Vec delta, float radius, float body, float step) const {
    const Vec to = from + delta;
    int next = sector;
    const float distance = length(delta);
    if (distance == 0) return true;
    float t = -epsilon / distance;
    for (std::size_t i = 0; i < sectors.size() && !contains(sectors[next], to); ++i) {
        auto hit = ray(from, delta, next, t);
        if (!hit.wall || hit.depth > 1 + epsilon || !passable(*this, next, *hit.wall, body, step)) return false;
        next = hit.wall->next;
        t = hit.depth;
    }
    if (!contains(sectors[next], to) || !hasClearance(to, next, radius, body, step)) return false;
    from = to;
    sector = next;
    return true;
}

bool Level::visible(Vec from, int sector, Vec to, float zFrom, float zTo) const {
    const Vec delta = to - from;
    float t = 0;
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const auto& current = sectors[sector];
        auto hit = ray(from, delta, sector, t);
        if (!hit.wall || hit.depth >= 1 - epsilon) {
            return zTo >= current.floor && zTo <= current.ceiling;
        }
        if (hit.wall->next < 0) return false;
        float z = zFrom + (zTo - zFrom) * hit.depth;
        float bottom = std::max(current.floor, sectors[hit.wall->next].floor);
        if (z < bottom || z >= portalTop(current, *hit.wall)) return false;
        sector = hit.wall->next;
        t = hit.depth;
    }
    return false;
}

Game::Game(std::filesystem::path path) : level(path), mapPath_(std::move(path)) {
    pos = level.spawn;
    sector = level.spawnSector;
    angle = level.spawnAngle;
    foot = level.sectors[sector].floor;
    health = 100;
    ammo = 28;
    total = static_cast<int>(std::count_if(level.things.begin(), level.things.end(),
        [](const Thing& thing) { return thing.kind == Kind::Guard; }));
}

void Game::reset() {
    *this = Game(mapPath_);
    state = State::Playing;
    say("ODNAJDZ KARTE DOSTEPU");
}

void Game::say(std::string text) {
    message = std::move(text);
    messageTime = 2.7f;
}

void Game::use() {
    auto hit = level.ray(pos, direction(angle), sector);
    if (!hit.wall || hit.depth > 1.85f || hit.wall->door < 0) return;
    auto& door = level.doors[hit.wall->door];
    if (door.moving || door.open >= 1) return;
    if (!key) {
        say("WYMAGANA KARTA DOSTEPU");
        return;
    }
    door.moving = true;
    say("OTWIERANIE GRODZI");
}

void Game::shoot() {
    if (reload > 0) return;
    if (ammo <= 0) {
        reload = 0.25f;
        say("BRAK AMUNICJI");
        return;
    }
    --ammo;
    recoil = 0.17f;
    reload = 0.23f;
    const Vec forward = direction(angle);
    const float eye = foot + 1.46f;
    Thing* target = nullptr;
    float best = 26.0f;
    for (auto& thing : level.things) {
        if (thing.kind != Kind::Guard || !thing.active) continue;
        const Vec offset = thing.pos - pos;
        float along = dot(offset, forward);
        float error = std::abs(cross(forward, offset));
        const float bottom = level.sectors[thing.sector].floor;
        if (along <= 0 || along >= best || error > 0.3f || eye < bottom
            || eye > bottom + 1.65f) continue;
        if (!level.visible(pos, sector, pos + forward * along, eye, eye)) continue;
        best = along;
        target = &thing;
    }
    if (!target) return;
    target->hp -= 25;
    target->hurt = 0.17f;
    if (target->hp <= 0) {
        target->active = false;
        ++kills;
    }
}

void Game::updateThings(float dt) {
    for (auto& thing : level.things) {
        thing.hurt = std::max(0.0f, thing.hurt - dt);
        if (!thing.active) continue;
        const Vec offset = pos - thing.pos;
        const float distance = length(offset);
        if (thing.kind != Kind::Guard) {
            if (distance > 0.6f || thing.sector != sector || health <= 0) continue;
            if (thing.kind == Kind::Exit) {
                state = State::Won;
                secs = static_cast<int>(elapsed);
                continue;
            }
            if (thing.kind == Kind::Health) {
                if (health >= 100) continue;
                health = std::min(100, health + 32);
                say("UZUPELNIONO ZDROWIE");
            } else if (thing.kind == Kind::Ammo) {
                if (ammo >= 99) continue;
                ammo = std::min(99, ammo + 16);
                say("UZUPELNIONO AMUNICJE");
            } else if (thing.kind == Kind::Key) {
                key = true;
                say("KARTA DOSTEPU ZDOBYTA");
            }
            thing.active = false;
            continue;
        }

        if (state != State::Playing) continue;
        const bool sees = distance < 13.0f &&
            level.visible(thing.pos, thing.sector, pos,
                          level.sectors[thing.sector].floor + 1.28f, foot + 1.32f);
        thing.timer = std::max(0.0f, thing.timer - dt);
        if (thing.windup > 0) {
            thing.windup -= dt;
            if (thing.windup <= 0) {
                thing.timer = 1.65f;
                if (sees && distance < 6.1f && thing.hurt <= 0) {
                    health = std::max(0, health - 12);
                    damage = 0.36f;
                    if (health == 0) state = State::Dead;
                }
            }
            continue;
        }
        if (!sees || thing.hurt > 0) continue;
        if (distance < 4.6f && thing.timer <= 0) {
            thing.windup = 0.47f;
        } else if (distance > 2.4f) {
            const Vec step = offset * (dt * 1.1f / distance);
            level.move(thing.pos, thing.sector, {step.x, 0}, 0.22f, 1.45f, 0.45f);
            level.move(thing.pos, thing.sector, {0, step.y}, 0.22f, 1.45f, 0.45f);
        }
    }
}

void Game::update(const Input& input, float dt) {
    if (state == State::Title || state == State::Dead || state == State::Won) {
        if (input.confirm) reset();
        return;
    }
    if (state == State::Paused) {
        if (input.confirm || input.pause) state = State::Playing;
        return;
    }
    if (input.pause) {
        state = State::Paused;
        return;
    }
    elapsed += dt;
    recoil = std::max(0.0f, recoil - dt);
    damage = std::max(0.0f, damage - dt);
    reload = std::max(0.0f, reload - dt);
    messageTime = std::max(0.0f, messageTime - dt);
    if (input.map) showMap = !showMap;
    angle = std::remainder(angle + input.turn * dt * 2.25f + input.mouse * 0.0031f, 2 * pi);

    const Vec forward = direction(angle);
    const Vec right{-forward.y, forward.x};
    const Vec wish = forward * input.forward + right * input.strafe;
    const float speed = input.sprint ? 5.5f : 3.65f;
    const Vec motion = wish * (dt * speed / std::max(1.0f, length(wish)));
    const Vec before = pos;
    if (length(motion) > epsilon) {
        level.move(pos, sector, {motion.x, 0}, playerRadius, playerHeight, playerStep);
        level.move(pos, sector, {0, motion.y}, playerRadius, playerHeight, playerStep);
    }
    stride += length(pos - before) * 6.8f;
    foot += (level.sectors[sector].floor - foot) * std::min(1.0f, dt * 13.0f);

    if (input.use) use();
    if (input.fire) shoot();
    for (auto& door : level.doors) {
        if (door.moving) door.open = std::min(1.0f, door.open + dt * 1.2f);
    }
    updateThings(dt);
}

} // namespace usk

#include "engine.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace usk {
namespace {

constexpr float focal = 385.0f;
constexpr float horizon = 153.0f;
constexpr Pixel amber = 0xFFB357;
constexpr Pixel teal = 0x7ACEBE;
constexpr Pixel ivory = 0xE8E6D8;
constexpr Pixel quiet = 0x9BAFAD;
constexpr Pixel panel = 0x101819;
constexpr int tileSize = 64;

constexpr Pixel rgb(int r, int g, int b) {
    return (static_cast<Pixel>(r) << 16) | (static_cast<Pixel>(g) << 8) | static_cast<Pixel>(b);
}

Pixel shade(Pixel color, float factor) {
    return rgb(std::min(255, static_cast<int>(((color >> 16) & 255) * factor)),
               std::min(255, static_cast<int>(((color >> 8) & 255) * factor)),
               std::min(255, static_cast<int>((color & 255) * factor)));
}

Pixel blend(Pixel from, Pixel to, float t) {
    return rgb(static_cast<int>(((from >> 16) & 255) * (1 - t) + ((to >> 16) & 255) * t),
               static_cast<int>(((from >> 8) & 255) * (1 - t) + ((to >> 8) & 255) * t),
               static_cast<int>((from & 255) * (1 - t) + (to & 255) * t));
}

float lightAt(float intensity, float depth) {
    return std::clamp(intensity / (1.0f + depth * 0.074f), 0.19f, 1.14f);
}

int noise(int x, int y) {
    auto n = static_cast<std::uint32_t>(x) * 1597334677u ^ static_cast<std::uint32_t>(y) * 3812015801u;
    return static_cast<int>(((n ^ (n >> 16)) * 2246822519u) >> 27);
}

Picture makeTile(Material material) {
    Picture img(tileSize, tileSize);
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const int grit = (noise(x, y) - 15) / 3;
            Pixel color = 0;
            if (material == Material::Steel || material == Material::Rust) {
                const bool rust = material == Material::Rust;
                int rib = (x >= 29 && x <= 35) ? 12 : 0;
                int stripe = ((y % 32) == 4 || (y % 32) == 5) ? -15 : 0;
                color = rust ? rgb(104 + grit + rib + stripe, 71 + grit + stripe, 54 + grit + stripe)
                             : rgb(61 + grit + rib + stripe, 82 + grit + rib + stripe, 84 + grit + rib + stripe);
                if (x == 0 || x == 63 || y == 0 || y == 63 || x == 36) color = 0x202C2D;
                if (x == 1 || y == 1 || x == 29) color = rust ? 0x976749 : 0x637D7C;
                if ((x == 5 || x == 57) && (y == 7 || y == 56)) color = 0xA1A49A;
            } else if (material == Material::Hazard || material == Material::Door ||
                       material == Material::Reactor) {
                color = rgb(47 + grit, 59 + grit, 60 + grit);
                if (x < 4 || x >= 60 || y <= 2 || y >= 61) color = 0x202A2A;
                if (x == 4 || y == 3) color = 0x789190;
                if (material == Material::Hazard && x >= 7 && x < 14) color = 0xC18841;
                if (material == Material::Hazard && y > 44 && y < 55 && x > 18 && x < 57)
                    color = (x + y) % 16 < 8 ? 0xBF8133 : 0x2E2A20;
                if (material == Material::Door && y >= 13 && y < 43 && x >= 10 && x < 54) {
                    color = (y % 6 < 2) ? 0x273233 : 0x536362;
                    if (x > 29 && x < 34) color = 0xC66B34;
                }
                if (material == Material::Reactor && x >= 9 && x < 55 && y >= 15 && y < 46) {
                    color = (x % 9 < 3) ? 0x172D2B : (y % 8 < 2 ? 0x519F8D : 0x6DDAC1);
                    if (y == 15 || y == 45) color = 0xA4F2C9;
                }
            } else if (material == Material::Floor) {
                int wear = grit + ((x / 32 + y / 32) % 2) * 5;
                color = rgb(46 + wear, 53 + wear, 52 + wear);
                if (x % 32 == 0 || y % 32 == 0) color = 0x242D2E;
                if (x % 32 == 1 || y % 32 == 1) color = 0x424C4B;
                if (x % 9 == 3 && y % 9 == 3) color = 0x505653;
            } else {
                color = rgb(44 + grit, 51 + grit, 53 + grit);
                if (x < 3 || y < 3 || x >= 61 || y >= 61) color = 0x263031;
                if (x >= 22 && x < 42 && y >= 11 && y < 53) {
                    color = 0x253736;
                    if (x >= 26 && x <= 38 && y > 14 && y < 49)
                        color = (y % 7 < 2) ? 0x648A83 : 0xA6C3AA;
                }
            }
            img.data[y * tileSize + x] = color;
        }
    }
    return img;
}

Picture guard() {
    Picture p(48, 64);
    p.poly({{10,61},{12,42},{5,38},{6,25},{14,19},{14,9},{21,4},{33,5},{38,12},{38,20},
            {44,26},{45,43},{38,43},{40,61},{27,62},{24,49},{22,62}}, 0x171C1B);
    p.poly({{15,36},{33,36},{37,58},{28,58},{24,45},{22,45},{19,58},{13,58}}, 0x485052);
    p.poly({{12,24},{19,18},{33,18},{39,25},{35,37},{14,37}}, 0x65716B);
    p.poly({{13,25},{22,24},{25,29},{30,24},{38,24},{33,34},{19,34}}, 0x81877A);
    p.poly({{8,26},{13,25},{15,36},{10,43},{6,40}}, 0x4D5C56);
    p.poly({{35,25},{40,24},{44,30},{43,41},{38,45},{34,37}}, 0x4D5C56);
    p.poly({{15,10},{19,6},{32,6},{36,11},{36,18},{32,23},{20,23},{15,19}}, 0x818D81);
    p.rect(18, 12, 17, 8, 0x232C2B);
    p.poly({{18,13},{26,15},{34,12},{32,16},{26,18},{19,16}}, 0xF35445);
    p.rect(21, 14, 4, 2, 0xFDC487);
    p.rect(16, 36, 18, 5, 0x222C2A);
    p.rect(23, 36, 5, 4, 0xAD8C50);
    p.rect(32, 31, 10, 10, 0x252E2D);
    p.rect(35, 32, 10, 3, 0x6C7C75);
    p.rect(13, 56, 9, 5, 0x272C2A);
    p.rect(28, 56, 11, 5, 0x272C2A);
    return p;
}

Picture item(Kind kind) {
    Picture p(32, 28);
    if (kind == Kind::Key) {
        p.poly({{4,8},{24,8},{29,12},{29,23},{5,23},{2,20},{2,11}}, 0x392A16);
        p.rect(5, 9, 20, 11, 0xE6AF51);
        p.rect(7, 11, 7, 7, 0x4B3C2C);
        p.rect(17, 12, 7, 2, 0xFFF6AF);
        p.rect(17, 16, 5, 2, 0x9C6031);
    } else if (kind == Kind::Ammo) {
        p.poly({{3,11},{12,7},{27,7},{30,11},{30,23},{3,23}}, 0x252F29);
        p.rect(5, 12, 22, 8, 0x757746);
        p.rect(9, 8, 4, 11, 0xDAA850);
        p.rect(15, 8, 4, 11, 0xDAA850);
        p.rect(21, 8, 4, 11, 0xDAA850);
        p.rect(5, 19, 22, 3, 0x404631);
    } else {
        p.poly({{3,12},{9,7},{25,7},{30,12},{30,23},{3,23}}, 0x253C36);
        p.rect(5, 12, 22, 9, 0x75A28D);
        p.rect(7, 9, 18, 4, 0xA8CAB3);
        p.rect(14, 12, 4, 8, 0xA63839);
        p.rect(12, 14, 8, 4, 0xA63839);
    }
    return p;
}

Picture gate() {
    Picture p(56, 86);
    p.poly({{4,84},{4,18},{12,6},{44,6},{52,18},{52,84},{42,84},{42,23},{37,17},
            {19,17},{14,23},{14,84}}, 0x162424);
    p.poly({{7,83},{7,20},{14,10},{42,10},{49,20},{49,83},{45,83},{45,21},{39,14},
            {17,14},{11,21},{11,83}}, 0x50AE9B);
    for (int i = 0; i < 5; ++i) p.rect(8, 28 + i * 10, 3, 3, 0xB1EBC6);
    p.rect(17, 82, 22, 3, 0x6BE5B2);
    return p;
}

Picture weapon() {
    Picture p(150, 111);
    p.poly({{0,110},{11,90},{36,77},{54,63},{56,32},{64,26},{64,11},{89,11},
            {89,26},{97,32},{101,69},{116,84},{141,99},{149,110}}, 0x1B2524);
    p.poly({{1,111},{15,94},{47,80},{59,79},{67,111}}, 0x435653);
    p.poly({{93,78},{114,86},{136,99},{149,111},{92,111}}, 0x435653);
    p.poly({{18,96},{46,79},{57,69},{63,74},{60,91},{37,100}}, 0x88725A);
    p.poly({{91,72},{105,78},{126,94},{115,103},{91,94}}, 0x88725A);
    p.poly({{53,98},{56,45},{65,34},{88,34},{97,46},{100,98},{88,111},{65,111}}, 0x2B3837);
    p.poly({{58,45},{65,35},{87,35},{93,44},{90,76},{63,76}}, 0x879392);
    p.poly({{63,46},{68,38},{84,38},{89,46},{85,70},{67,70}}, 0x566867);
    p.poly({{64,40},{64,17},{69,11},{84,11},{89,17},{89,40}}, 0x3C4A4A);
    p.rect(67, 16, 20, 5, 0x9AA5A0);
    p.rect(69, 12, 15, 5, 0x212C2F);
    p.rect(73, 8, 7, 5, 0x66766C);
    p.rect(61, 79, 32, 6, 0x162322);
    p.rect(68, 88, 18, 11, 0x101A1B);
    p.rect(71, 91, 11, 4, 0xD99347);
    p.rect(63, 104, 28, 4, 0x526563);
    return p;
}

using Glyph = std::array<unsigned char, 7>;

Glyph glyph(char c) {
    static constexpr std::array<Glyph, 26> letters{{
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,10,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    }};
    static constexpr std::array<Glyph, 10> numbers{{
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    }};
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return numbers[c - '0'];
    if (c == '-') return {0,0,0,31,0,0,0};
    if (c == '/') return {1,1,2,4,8,16,16};
    if (c == ':') return {0,4,4,0,4,4,0};
    if (c == '.') return {0,0,0,0,0,6,6};
    return {};
}

} // namespace

Picture::Picture(int width, int height) : w(width), h(height), data(width * height) {}

void Picture::rect(int x, int y, int width, int height, Pixel color) {
    for (int j = std::max(0, y); j < std::min(h, y + height); ++j) {
        for (int i = std::max(0, x); i < std::min(w, x + width); ++i) data[j * w + i] = color;
    }
}

void Picture::poly(std::initializer_list<Vec> points, Pixel color) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool inside = false;
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            auto prev = points.end() - 1;
            for (auto it = points.begin(); it != points.end(); ++it) {
                if ((it->y > py) != (prev->y > py) &&
                    px < (prev->x - it->x) * (py - it->y) / (prev->y - it->y) + it->x) {
                    inside = !inside;
                }
                prev = it;
            }
            if (inside) data[y * w + x] = color;
        }
    }
}

Pixel Picture::tiled(float u, float v) const {
    int x = static_cast<int>((u - std::floor(u)) * w);
    int y = static_cast<int>((v - std::floor(v)) * h);
    return texel(std::min(w - 1, x), std::min(h - 1, y));
}

Renderer::Renderer() {
    for (int i = 0; i < static_cast<int>(tiles_.size()); ++i) tiles_[i] = makeTile(static_cast<Material>(i));
    objects_[0] = guard();
    objects_[1] = item(Kind::Key);
    objects_[2] = item(Kind::Ammo);
    objects_[3] = item(Kind::Health);
    objects_[4] = gate();
    objects_[5] = Picture(40, 12);
    objects_[5].poly({{2,10},{12,5},{23,7},{30,3},{38,8},{34,11},{6,11}}, 0x533034);
    objects_[5].rect(9, 8, 20, 3, 0x6B3433);
    weapon_ = weapon();
}

void Renderer::world(const Game& game, Vec forward, Vec right, float eye) {
    std::fill(frame_.begin(), frame_.end(), 0x111B20);
    depth_.fill(std::numeric_limits<float>::infinity());

    for (int x = 0; x < width; ++x) {
        const Vec ray = forward + right * ((x + 0.5f - width / 2.0f) / focal);
        int current = game.sector, top = 0, bottom = viewHeight;
        float near = -0.0001f;
        for (std::size_t i = 0; i < game.level.sectors.size() && top < bottom; ++i) {
            const auto& sector = game.level.sectors[current];
            const auto hit = game.level.ray(game.pos, ray, current, near);
            if (!hit.wall) break;
            const auto& wall = *hit.wall;
            const float far = std::max(hit.depth, 0.0001f);
            const bool portal = wall.next >= 0;
            const float openingFloor = portal ? std::max(sector.floor, game.level.sectors[wall.next].floor) : 0;
            const float openingCeiling = portal ? game.level.portalTop(sector, wall) : 0;
            const bool open = portal && openingCeiling > openingFloor;
            const Vec edge = wall.b - wall.a;
            const float wallShade = lightAt(sector.light, far) * (0.88f + 0.12f * std::abs(edge.y) / length(edge));
            int nextTop = bottom, nextBottom = top;
            for (int y = top; y < bottom; ++y) {
                const float slope = (horizon - y - 0.5f) / focal;
                const float z = eye + slope * far;
                const auto idx = y * width + x;
                if (z < sector.floor || z > sector.ceiling) {
                    const bool floor = z < sector.floor;
                    const float planeZ = floor ? sector.floor : sector.ceiling;
                    const float d = std::clamp((planeZ - eye) / slope, std::max(near, 0.0f), far);
                    const Vec onPlane = game.pos + ray * d;
                    const auto mat = floor ? Material::Floor : Material::Ceiling;
                    const auto color = tiles_[static_cast<int>(mat)].tiled(onPlane.x * 0.5f, onPlane.y * 0.5f);
                    frame_[idx] = shade(color, lightAt(sector.light, d) * (floor ? 0.92f : 0.83f));
                    depth_[idx] = d;
                } else if (open && z >= openingFloor && z < openingCeiling) {
                    nextTop = std::min(nextTop, y);
                    nextBottom = y + 1;
                } else {
                    const bool door = wall.door >= 0 && z >= openingCeiling &&
                                      z < std::min(sector.ceiling, game.level.sectors[wall.next].ceiling) &&
                                      z >= openingFloor;
                    const auto material = door ? Material::Door : wall.material;
                    const float base = door ? openingCeiling : sector.floor;
                    frame_[idx] = shade(tiles_[static_cast<int>(material)].tiled(hit.u * 0.5f, (base - z) * 0.5f),
                                        wallShade);
                    depth_[idx] = far;
                }
            }
            if (!open) break;
            current = wall.next;
            near = hit.depth;
            top = nextTop;
            bottom = nextBottom;
        }
    }
}

void Renderer::sprites(const Game& game, Vec forward, Vec right, float eye) {
    for (const auto& thing : game.level.things) {
        if (!thing.active && thing.kind != Kind::Guard) continue;
        const int sprite = thing.active ? static_cast<int>(thing.kind) : 5;
        const auto& image = objects_[sprite];
        const Vec offset = thing.pos - game.pos;
        const float d = dot(forward, offset);
        if (d < 0.12f) continue;
        const float objectHeight = std::array{1.84f, 0.52f, 0.48f, 0.48f, 2.64f, 0.26f}[sprite];
        const float cx = width / 2.0f + dot(right, offset) * focal / d;
        const auto& sector = game.level.sectors[thing.sector];
        const float yb = horizon + (eye - sector.floor) * focal / d;
        const float h = objectHeight * focal / d;
        const float w = h * image.w / image.h;
        const float x0 = cx - w / 2;
        const float y0 = yb - h;
        const float light = lightAt(sector.light, d);
        for (int y = std::max(0, static_cast<int>(y0)); y < std::min(viewHeight, static_cast<int>(yb + 1)); ++y) {
            int ty = static_cast<int>((y + 0.5f - y0) * image.h / h);
            if (ty < 0 || ty >= image.h) continue;
            for (int x = std::max(0, static_cast<int>(x0)); x < std::min(width, static_cast<int>(x0 + w + 1)); ++x) {
                int tx = static_cast<int>((x + 0.5f - x0) * image.w / w);
                if (tx < 0 || tx >= image.w) continue;
                const auto idx = y * width + x;
                if (d >= depth_[idx]) continue;
                Pixel color = image.texel(tx, ty);
                if (!color) continue;
                if (thing.hurt > 0) color = blend(color, 0xEE6837, 0.53f);
                if (thing.windup > 0 && tx >= 31 && tx < 45 && ty >= 27 && ty <= 42)
                    color = blend(color, amber, 0.70f);
                frame_[idx] = shade(color, light);
                depth_[idx] = d;
            }
        }
    }
}

void Renderer::rect(int x, int y, int w, int h, Pixel color) {
    const int first = std::max(x, 0);
    const int last = std::min(width, x + w);
    if (last <= first) return;
    for (int j = std::max(y, 0); j < std::min(height, y + h); ++j) {
        std::fill(frame_.begin() + j * width + first, frame_.begin() + j * width + last, color);
    }
}

void Renderer::line(int x0, int y0, int x1, int y1, Pixel color) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * err;
        if (twice >= dy) { err += dy; x0 += sx; }
        if (twice <= dx) { err += dx; y0 += sy; }
    }
}

void Renderer::text(int x, int y, const std::string& s, int scale, Pixel color) {
    for (const auto c : s) {
        auto bits = glyph(c);
        for (int i = 0; i < 7; ++i) {
            for (int j = 0; j < 5; ++j) {
                if (bits[i] & (16 >> j)) rect(x + j * scale, y + i * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
    }
}

void Renderer::blit(const Picture& img, int x, int y) {
    for (int j = std::max(0, -y); j < std::min(viewHeight - y, img.h); ++j) {
        for (int i = std::max(0, -x); i < std::min(width - x, img.w); ++i) {
            if (const auto color = img.texel(i, j)) frame_[(y + j) * width + x + i] = color;
        }
    }
}

void Renderer::hud(const Game& game) {
    rect(0, viewHeight, width, height - viewHeight, panel);
    rect(0, viewHeight, width, 1, 0x516867);
    rect(0, viewHeight + 1, width, 2, 0x273735);
    const std::array<int, 5> sections{20, 161, 292, 429, 539};
    for (int x : {144, 275, 413, 523}) rect(x, 325, 1, 25, 0x2D3E3E);
    const std::array<std::string, 5> labels{"ZDROWIE", "AMUNICJA", "KARTA", "STRAZNICY", "POZIOM"};
    const std::array<std::string, 5> values{std::to_string(game.health), std::to_string(game.ammo),
        game.key ? "JEST" : "BRAK", std::to_string(game.kills) + "/" + std::to_string(game.total), "01"};
    for (std::size_t i = 0; i < sections.size(); ++i) {
        text(sections[i], 324, labels[i], 1, quiet);
        Pixel color = ivory;
        if (i == 0 && game.health <= 25) color = 0xF26457;
        if (i == 2) color = game.key ? amber : quiet;
        if (i == 4) color = teal;
        text(sections[i], 336, values[i], 2, color);
    }
    rect(62, 344, 60, 3, 0x303A35);
    rect(62, 344, game.health * 60 / 100, 3, game.health > 25 ? teal : 0xF26457);
}

void Renderer::automap(const Game& game) {
    Vec lo = game.pos, hi = game.pos;
    for (const auto& sector : game.level.sectors) {
        for (const auto& wall : sector.walls) {
            lo.x = std::min(lo.x, wall.a.x);
            lo.y = std::min(lo.y, wall.a.y);
            hi.x = std::max(hi.x, wall.a.x);
            hi.y = std::max(hi.y, wall.a.y);
        }
    }
    const float scale = std::min(155.0f / std::max(0.1f, hi.x - lo.x),
                                 69.0f / std::max(0.1f, hi.y - lo.y));
    auto px = [&](float x) { return 452 + static_cast<int>((x - lo.x) * scale); };
    auto py = [&](float y) { return 44 + static_cast<int>((y - lo.y) * scale); };
    rect(441, 17, 181, 105, 0x142322);
    rect(441, 17, 181, 1, 0x567A71);
    text(452, 27, "MAPA SEKTOROW", 1, teal);
    for (const auto& sector : game.level.sectors) {
        for (const auto& wall : sector.walls) {
            const Pixel color = wall.door >= 0 ? amber : (wall.next >= 0 ? 0x405953 : 0x87A39C);
            line(px(wall.a.x), py(wall.a.y), px(wall.b.x), py(wall.b.y), color);
        }
    }
    const Vec tip = game.pos + direction(game.angle) * 1.5f;
    line(px(game.pos.x), py(game.pos.y), px(tip.x), py(tip.y), amber);
    rect(px(game.pos.x) - 1, py(game.pos.y) - 1, 3, 3, amber);
}

void Renderer::overlay(const Game& game) {
    for (int y = 0; y < viewHeight; ++y) {
        for (int x = 0; x < width; ++x) {
            const float tint = std::clamp(0.89f - (x - 300) * 0.0022f, 0.19f, 0.89f);
            auto& pixel = frame_[y * width + x];
            pixel = blend(pixel, 0x0F191A, tint);
        }
    }
    rect(23, 23, 2, 265, 0x395352);
    rect(23, 23, 2, 53, amber);
    if (game.state == State::Title) {
        text(40, 32, "KOMPLEKS PRZEMYSLOWY  /  SEKTOR 01", 1, teal);
        text(40, 66, "USKOK", 8, ivory);
        text(40, 133, "PROTOKOL AWARYJNY", 2, amber);
        text(40, 164, "ZNAJDZ KARTE I DOTRZYJ DO WYJSCIA.", 1, quiet);
        rect(40, 198, 159, 27, amber);
        text(51, 208, "ENTER  /  START", 1, 0x27241C);
        text(40, 246, "WASD RUCH   MYSZ / STRZALKI OBROT   SHIFT BIEG", 1, quiet);
        text(40, 263, "LPM / SPACJA STRZAL   E DRZWI   TAB MAPA   ESC PAUZA", 1, quiet);
    } else {
        const std::string title = game.state == State::Paused ? "PAUZA" :
                                  (game.state == State::Won ? "EWAKUACJA" : "UTRATA SYGNALU");
        text(40, 81, title, 5, ivory);
        if (game.state == State::Won) {
            text(40, 135, "CZAS: " + std::to_string(game.secs) + " S   WYNIK: " +
                 std::to_string(game.kills) + "/" + std::to_string(game.total), 2, teal);
        }
        const std::string action = game.state == State::Paused ? "ENTER  /  WZNOW" : "ENTER  /  OD NOWA";
        rect(40, 183, 159, 27, amber);
        text(51, 193, action, 1, 0x27241C);
    }
}

const Frame& Renderer::draw(const Game& game) {
    const Vec forward = direction(game.angle);
    const Vec right{-forward.y, forward.x};
    const float eye = game.foot + 1.46f + std::sin(game.stride) * 0.023f;
    world(game, forward, right, eye);
    sprites(game, forward, right, eye);
    if (game.state != State::Title) {
        const int gunX = width / 2 - weapon_.w / 2 + static_cast<int>(std::sin(game.stride * 0.5f) * 5);
        const int gunY = viewHeight - weapon_.h + static_cast<int>(std::abs(std::cos(game.stride * 0.5f)) * 5)
                       + static_cast<int>(game.recoil * 80);
        blit(weapon_, gunX, gunY);
        if (game.recoil > 0.099f) {
            const int x = gunX + 76, y = gunY + 12;
            line(x - 15, y - 14, x + 11, y + 4, 0xF0A54D);
            line(x + 13, y - 18, x - 11, y + 3, 0xF0A54D);
            rect(x - 2, y - 25, 5, 24, amber);
            rect(x - 7, y - 10, 15, 7, 0xFFF2AE);
            rect(x - 3, y - 14, 7, 15, 0xFFF2AE);
        }
        if (game.damage > 0) {
            for (int y = 0; y < viewHeight; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int edge = std::min({x, width - 1 - x, y, viewHeight - 1 - y});
                    const float glow = std::max(0.0f, 1 - edge / 73.0f) * game.damage * 1.4f;
                    if (glow > 0) {
                        auto& pixel = frame_[y * width + x];
                        pixel = blend(pixel, 0xB5282D, glow);
                    }
                }
            }
        }
        const int h = static_cast<int>(horizon);
        rect(width / 2 - 7, h, 4, 1, ivory);
        rect(width / 2 + 4, h, 4, 1, ivory);
        rect(width / 2, h - 7, 1, 4, ivory);
        rect(width / 2, h + 4, 1, 4, ivory);
        if (game.messageTime > 0) {
            const int w = static_cast<int>(game.message.size()) * 6;
            rect((width - w) / 2 - 9, 18, w + 18, 19, panel);
            text((width - w) / 2, 24, game.message, 1, amber);
        }
        if (game.showMap) automap(game);
    }
    if (game.state != State::Playing) overlay(game);
    hud(game);
    return frame_;
}

} // namespace usk

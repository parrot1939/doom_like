#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace usk {

constexpr int width = 640;
constexpr int height = 360;
constexpr int viewHeight = 315;
constexpr float pi = 3.14159265359f;
using Pixel = std::uint32_t;
using Frame = std::array<Pixel, width * height>;

struct Vec {
    float x{}, y{};
    Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
    Vec operator*(float s) const { return {x * s, y * s}; }
};

inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec a, Vec b) { return a.x * b.y - a.y * b.x; }
inline float length(Vec v) { return std::sqrt(dot(v, v)); }
inline Vec direction(float angle) { return {std::cos(angle), std::sin(angle)}; }

enum class Material { Steel, Rust, Hazard, Reactor, Door, Floor, Ceiling, Count };
enum class Kind { Guard, Key, Ammo, Health, Exit };

struct Wall {
    Vec a, b;
    Material material{};
    int next = -1;
    int door = -1;
};

struct Sector {
    float floor{}, ceiling{}, light{};
    std::vector<Wall> walls;
};

struct Door {
    int a{}, b{};
    float open{};
    bool moving{};
};

struct Thing {
    Kind kind{};
    Vec pos;
    int sector{}, hp = 50;
    float timer{}, windup{}, hurt{};
    bool active = true;
};

struct Boundary {
    const Wall* wall{};
    float depth{};
    float u{};
};

class Level {
public:
    explicit Level(const std::filesystem::path& path);
    Boundary ray(Vec from, Vec dir, int sector, float after = 0) const;
    bool move(Vec& from, int& sector, Vec delta, float radius,
              float bodyHeight, float stepHeight) const;
    bool visible(Vec from, int sector, Vec to, float zFrom, float zTo) const;
    float portalTop(const Sector& source, const Wall& wall) const;

    std::vector<Sector> sectors;
    std::vector<Door> doors;
    std::vector<Thing> things;
    Vec spawn;
    int spawnSector{};
    float spawnAngle{};

private:
    bool hasClearance(Vec pos, int sector, float radius,
                      float bodyHeight, float stepHeight) const;
};

struct Input {
    float forward{}, strafe{}, turn{}, mouse{};
    bool fire{}, use{}, map{}, pause{}, confirm{}, sprint{};
};

enum class State { Title, Playing, Paused, Dead, Won };

class Game {
public:
    explicit Game(std::filesystem::path mapPath);
    void update(const Input& input, float dt);
    void reset();

    Level level;
    Vec pos;
    int sector{};
    float angle{}, foot{}, elapsed{}, stride{};
    float recoil{}, damage{}, reload{}, messageTime{};
    int health{}, ammo{}, kills{}, total{}, secs{};
    bool key{}, showMap{};
    State state = State::Title;
    std::string message;

private:
    std::filesystem::path mapPath_;
    void say(std::string text);
    void use();
    void shoot();
    void updateThings(float dt);
};

struct Picture {
    int w{}, h{};
    std::vector<Pixel> data;
    explicit Picture(int width = 0, int height = 0);
    void rect(int x, int y, int width, int height, Pixel color);
    void poly(std::initializer_list<Vec> points, Pixel color);
    Pixel texel(int x, int y) const { return data[y * w + x]; }
    Pixel tiled(float u, float v) const;
};

class Renderer {
public:
    Renderer();
    const Frame& draw(const Game& game);
private:
    Frame frame_{};
    std::array<float, width * viewHeight> depth_{};
    std::array<Picture, static_cast<int>(Material::Count)> tiles_;
    std::array<Picture, 6> objects_;
    Picture weapon_;

    void world(const Game& game, Vec forward, Vec right, float eye);
    void sprites(const Game& game, Vec forward, Vec right, float eye);
    void hud(const Game& game);
    void automap(const Game& game);
    void overlay(const Game& game);
    void rect(int x, int y, int w, int h, Pixel color);
    void line(int x0, int y0, int x1, int y1, Pixel color);
    void text(int x, int y, const std::string& s, int scale, Pixel color);
    void blit(const Picture& img, int x, int y);
};

void runWindow(Game& game, Renderer& renderer, bool smoke);
void saveFrame(const Frame& frame, const std::filesystem::path& path);

} // namespace usk

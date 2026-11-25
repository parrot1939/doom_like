#include "engine.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace usk {

void saveFrame(const Frame& frame, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (auto pixel : frame) {
        std::array<char, 3> rgb{
            static_cast<char>((pixel >> 16) & 255),
            static_cast<char>((pixel >> 8) & 255),
            static_cast<char>(pixel & 255)
        };
        file.write(rgb.data(), rgb.size());
    }
    if (!file) throw std::runtime_error("Nie mozna zapisac obrazu: " + path.string());
}

} // namespace usk

int main(int argc, char** argv) {
    try {
        std::filesystem::path map = "usk.map";
        if (!std::filesystem::exists(map)) {
            map = std::filesystem::absolute(argv[0]).parent_path() / "usk.map";
        }
        std::filesystem::path capture;
        bool title = false, smoke = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "uskok [--map mapa] [--capture obraz.ppm [--title]] [--smoke]\n";
                return 0;
            }
            if (arg == "--title") title = true;
            else if (arg == "--smoke") smoke = true;
            else if ((arg == "--map" || arg == "--capture") && i + 1 < argc) {
                (arg == "--map" ? map : capture) = argv[++i];
            } else {
                throw std::runtime_error("Nieznana lub niekompletna opcja: " + std::string(arg));
            }
        }
        auto renderer = std::make_unique<usk::Renderer>();
        usk::Game game(map);
        if (!capture.empty()) {
            if (!title) game.reset();
            usk::saveFrame(renderer->draw(game), capture);
        } else {
            usk::runWindow(game, *renderer, smoke);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "USKOK: " << error.what() << '\n';
        return 1;
    }
}

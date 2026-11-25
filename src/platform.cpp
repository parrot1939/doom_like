#include "engine.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace usk {
namespace {

// Minimal SDL2 ABI used for the window, input and audio. The engine owns every pixel.
struct Window;
struct DisplayRenderer;
struct Texture;
struct Rect;
struct AudioSpec {
    int frequency{};
    std::uint16_t format{};
    std::uint8_t channels{}, silence{};
    std::uint16_t samples{}, padding{};
    std::uint32_t size{};
    void (*callback)(void*, std::uint8_t*, int){};
    void* userdata{};
};

class Library {
public:
    Library() {
#ifdef _WIN32
        handle_ = LoadLibraryW(L"SDL2.dll");
#else
#ifdef __APPLE__
        for (auto file : {"libSDL2-2.0.0.dylib", "libSDL2.dylib", "SDL2.framework/SDL2",
                          "/Library/Frameworks/SDL2.framework/SDL2"}) {
#else
        for (auto file : {"libSDL2-2.0.so.0", "libSDL2.so"}) {
#endif
            handle_ = dlopen(file, RTLD_NOW | RTLD_LOCAL);
            if (handle_) break;
        }
#endif
        if (!handle_) throw std::runtime_error("Brak biblioteki uruchomieniowej SDL2. Zobacz README.md.");
    }

    ~Library() {
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    template <class T> void load(T& fn, const char* name) const {
#ifdef _WIN32
        auto address = GetProcAddress(handle_, name);
#else
        auto address = dlsym(handle_, name);
#endif
        if (!address) throw std::runtime_error(std::string("SDL2 nie udostepnia ") + name);
        static_assert(sizeof(address) == sizeof(T));
        fn = std::bit_cast<T>(address);
    }

private:
#ifdef _WIN32
    HMODULE handle_{};
#else
    void* handle_{};
#endif
};

class Sdl {
    Library library_;
public:
    int (*Init)(std::uint32_t);
    int (*InitSubSystem)(std::uint32_t);
    void (*Quit)();
    const char* (*GetError)();
    int (*SetHint)(const char*, const char*);
    Window* (*CreateWindow)(const char*, int, int, int, int, std::uint32_t);
    void (*DestroyWindow)(Window*);
    std::uint32_t (*GetWindowFlags)(Window*);
    DisplayRenderer* (*CreateRenderer)(Window*, int, std::uint32_t);
    void (*DestroyRenderer)(DisplayRenderer*);
    Texture* (*CreateTexture)(DisplayRenderer*, std::uint32_t, int, int, int);
    void (*DestroyTexture)(Texture*);
    int (*RenderSetLogicalSize)(DisplayRenderer*, int, int);
    int (*UpdateTexture)(Texture*, const Rect*, const void*, int);
    int (*RenderClear)(DisplayRenderer*);
    int (*RenderCopy)(DisplayRenderer*, Texture*, const Rect*, const Rect*);
    void (*RenderPresent)(DisplayRenderer*);
    void (*PumpEvents)();
    int (*HasEvent)(std::uint32_t);
    void (*FlushEvents)(std::uint32_t, std::uint32_t);
    const std::uint8_t* (*GetKeyboardState)(int*);
    std::uint32_t (*GetRelativeMouseState)(int*, int*);
    int (*SetRelativeMouseMode)(int);
    std::uint32_t (*OpenAudioDevice)(const char*, int, const AudioSpec*, AudioSpec*, int);
    void (*CloseAudioDevice)(std::uint32_t);
    void (*PauseAudioDevice)(std::uint32_t, int);
    int (*QueueAudio)(std::uint32_t, const void*, std::uint32_t);
    std::uint32_t (*GetQueuedAudioSize)(std::uint32_t);

    Sdl() {
#define LOAD(symbol) library_.load(symbol, "SDL_" #symbol)
        LOAD(Init);
        LOAD(InitSubSystem);
        LOAD(Quit);
        LOAD(GetError);
        LOAD(SetHint);
        LOAD(CreateWindow);
        LOAD(DestroyWindow);
        LOAD(GetWindowFlags);
        LOAD(CreateRenderer);
        LOAD(DestroyRenderer);
        LOAD(CreateTexture);
        LOAD(DestroyTexture);
        LOAD(RenderSetLogicalSize);
        LOAD(UpdateTexture);
        LOAD(RenderClear);
        LOAD(RenderCopy);
        LOAD(RenderPresent);
        LOAD(PumpEvents);
        LOAD(HasEvent);
        LOAD(FlushEvents);
        LOAD(GetKeyboardState);
        LOAD(GetRelativeMouseState);
        LOAD(SetRelativeMouseMode);
        LOAD(OpenAudioDevice);
        LOAD(CloseAudioDevice);
        LOAD(PauseAudioDevice);
        LOAD(QueueAudio);
        LOAD(GetQueuedAudioSize);
#undef LOAD
    }
};

struct Session {
    Sdl& sdl;
    Window* window{};
    DisplayRenderer* renderer{};
    Texture* texture{};
    std::uint32_t audio{};

    ~Session() {
        if (audio) sdl.CloseAudioDevice(audio);
        if (texture) sdl.DestroyTexture(texture);
        if (renderer) sdl.DestroyRenderer(renderer);
        if (window) sdl.DestroyWindow(window);
        sdl.Quit();
    }
};

enum Scan { A = 4, D = 7, E = 8, Q = 20, S = 22, W = 26, Enter = 40, Escape = 41,
            Tab = 43, Space = 44, Right = 79, Left = 80, Down = 81, Up = 82, LShift = 225, RShift = 229 };

enum class Sound { Shot, Wound, Pickup, Door, Win };
constexpr int audioRate = 22050;
constexpr auto audioS16 = std::endian::native == std::endian::little ? 0x8010 : 0x9010;

void play(const Session& session, Sound sound) {
    if (!session.audio || session.sdl.GetQueuedAudioSize(session.audio) > audioRate / 4) return;
    const float duration = sound == Sound::Win ? 0.36f : 0.16f;
    const int count = static_cast<int>(duration * audioRate);
    std::vector<std::int16_t> samples(count);
    std::uint32_t rng = 5489;
    float phase = 0;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / count;
        rng = rng * 1664525u + 1013904223u;
        const float noise = static_cast<float>(rng >> 24) / 127.5f - 1;
        float value;
        if (sound == Sound::Shot || sound == Sound::Wound || sound == Sound::Door) {
            const float hz = (sound == Sound::Door ? 74.0f : 155.0f) * (1 - t * 0.65f);
            phase += 2 * pi * hz / audioRate;
            value = (std::sin(phase) * 0.55f + noise * 0.45f) * (1 - t) * (1 - t);
        } else {
            const float hz = t < 0.34f ? 392.0f : (t < 0.67f ? 523.25f : 659.25f);
            phase += 2 * pi * hz / audioRate;
            value = std::sin(phase) * (1 - t) * 0.68f;
        }
        samples[i] = static_cast<std::int16_t>(value * 9300);
    }
    session.sdl.QueueAudio(session.audio, samples.data(),
                           static_cast<std::uint32_t>(samples.size() * sizeof(samples[0])));
}

} // namespace

void runWindow(Game& game, Renderer& engine, bool smoke) {
    Sdl sdl;
    Session session{sdl};
    constexpr std::uint32_t video = 0x20, audio = 0x10;
    auto fail = [&](const char* why) { throw std::runtime_error(std::string(why) + ": " + sdl.GetError()); };
    if (sdl.Init(video) < 0) fail("Inicjalizacja obrazu");
    sdl.SetHint("SDL_RENDER_SCALE_QUALITY", "0");
    constexpr int centered = 0x2FFF0000;
    constexpr std::uint32_t windowResizable = 0x20, focus = 0x200;
    session.window = sdl.CreateWindow("USKOK - Protokol awaryjny", centered, centered, width * 2,
                                     height * 2, windowResizable);
    if (!session.window) fail("Tworzenie okna");
    constexpr std::uint32_t acceleratedVsync = 0x06;
    session.renderer = sdl.CreateRenderer(session.window, -1, acceleratedVsync);
    if (!session.renderer) session.renderer = sdl.CreateRenderer(session.window, -1, 0);
    if (!session.renderer) fail("Inicjalizacja prezentacji");
    if (sdl.RenderSetLogicalSize(session.renderer, width, height) < 0) fail("Ustawianie rozmiaru");
    constexpr std::uint32_t rgb888 = 0x16161804;
    constexpr int streaming = 1;
    session.texture = sdl.CreateTexture(session.renderer, rgb888, streaming, width, height);
    if (!session.texture) fail("Tworzenie bufora ekranu");

    if (sdl.InitSubSystem(audio) == 0) {
        AudioSpec wanted;
        wanted.frequency = audioRate;
        wanted.format = audioS16;
        wanted.channels = 1;
        wanted.samples = 512;
        session.audio = sdl.OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (session.audio) sdl.PauseAudioDevice(session.audio, 0);
    }

    bool lastPause = false, lastConfirm = false, lastUse = false, lastMap = false, capture = false;
    auto previous = std::chrono::steady_clock::now();
    int frames = 0;
    while (true) {
        const auto start = std::chrono::steady_clock::now();
        const float dt = std::min(0.033f, std::chrono::duration<float>(start - previous).count());
        previous = start;

        sdl.PumpEvents();
        constexpr std::uint32_t quit = 0x100;
        if (sdl.HasEvent(quit)) break;
        sdl.FlushEvents(0, 0xFFFF);
        const auto* keys = sdl.GetKeyboardState(nullptr);
        const bool focused = (sdl.GetWindowFlags(session.window) & focus) != 0;
        auto held = [&](Scan key) { return focused && keys[key] != 0; };
        auto pressed = [&](Scan key, bool& last) {
            const bool now = held(key);
            const bool result = now && !last;
            last = now;
            return result;
        };
        if (held(Q) && game.state != State::Playing) break;
        int mouseX = 0;
        const auto buttons = sdl.GetRelativeMouseState(&mouseX, nullptr);
        Input input;
        input.forward = (held(W) || held(Up)) - (held(S) || held(Down));
        input.strafe = held(D) - held(A);
        input.turn = held(Right) - held(Left);
        input.mouse = capture ? static_cast<float>(mouseX) : 0;
        input.fire = held(Space) || (capture && (buttons & 1));
        input.sprint = held(LShift) || held(RShift);
        input.use = pressed(E, lastUse);
        input.map = pressed(Tab, lastMap);
        input.confirm = pressed(Enter, lastConfirm);
        input.pause = pressed(Escape, lastPause) || (!focused && game.state == State::Playing);

        const int oldAmmo = game.ammo, oldHealth = game.health;
        const bool oldKey = game.key;
        const auto oldState = game.state;
        const float oldNotice = game.messageTime;
        if (smoke) {
            input = {};
            input.confirm = frames == 0;
            input.fire = frames == 1;
        }
        game.update(input, dt);
        if (oldState != State::Won && game.state == State::Won) play(session, Sound::Win);
        else if (game.health < oldHealth) play(session, Sound::Wound);
        else if (game.ammo < oldAmmo) play(session, Sound::Shot);
        else if (game.health > oldHealth || game.ammo > oldAmmo || (!oldKey && game.key)) play(session, Sound::Pickup);
        else if (input.use && game.messageTime > oldNotice && game.message == "OTWIERANIE GRODZI")
            play(session, Sound::Door);

        const bool wantCapture = game.state == State::Playing && focused && !smoke;
        if (wantCapture != capture && sdl.SetRelativeMouseMode(wantCapture ? 1 : 0) == 0) capture = wantCapture;
        const auto& frame = engine.draw(game);
        if (sdl.UpdateTexture(session.texture, nullptr, frame.data(), width * sizeof(Pixel)) < 0 ||
            sdl.RenderClear(session.renderer) < 0 ||
            sdl.RenderCopy(session.renderer, session.texture, nullptr, nullptr) < 0) fail("Prezentacja klatki");
        sdl.RenderPresent(session.renderer);
        if (smoke && ++frames >= 4) break;
        std::this_thread::sleep_until(start + std::chrono::milliseconds(16));
    }
}

} // namespace usk

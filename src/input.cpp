#include "input.h"
#include "platform.h"

#include <SDL2/SDL.h>

namespace input {

// Índices de botão do port SDL2 do OpenOrbis (samples/SDL2/Game.h)
enum {
    PAD_CROSS = 0, PAD_CIRCLE = 1, PAD_SQUARE = 2, PAD_TRIANGLE = 3,
    PAD_L1 = 4, PAD_R1 = 5, PAD_OPTIONS = 9, PAD_L3 = 11, PAD_R3 = 12,
    PAD_UP = 13, PAD_DOWN = 14, PAD_LEFT = 15, PAD_RIGHT = 16,
    PAD_TOUCH = 17, PAD_L2 = 18, PAD_R2 = 19,
};
static const int kMaxButtons = 32;

static SDL_Joystick* g_joy = nullptr;
static bool g_prevBtn[kMaxButtons] = {false};

// Direções só pelo direcional (o analógico é ignorado). O SDL informa o direcional
// de dois jeitos, botões e hat; cada um tem a própria detecção de "apertou agora",
// então um deles preso numa direção não bloqueia os toques seguintes.
enum { SRC_BTN = 0, SRC_HAT = 1, SRC_COUNT = 2 };
static Btn g_src[SRC_COUNT] = {Btn::None, Btn::None};
static Btn g_held = Btn::None;     // direção que está repetindo
static int g_heldSrc = -1;         // fonte que iniciou a repetição
static uint64_t g_nextRepeat = 0;
static Btn g_lastEdge = Btn::None; // último toque emitido (descarta duplicado de outra fonte)
static uint64_t g_lastEdgeAt = 0;

static Btn mapButton(int b)
{
    switch (b) {
    case PAD_CROSS: return Btn::Cross;
    case PAD_CIRCLE: return Btn::Circle;
    case PAD_SQUARE: return Btn::Square;
    case PAD_TRIANGLE: return Btn::Triangle;
    case PAD_L1: return Btn::L1;
    case PAD_R1: return Btn::R1;
    case PAD_L2: return Btn::L2;
    case PAD_R2: return Btn::R2;
    case PAD_OPTIONS: return Btn::Options;
    case PAD_TOUCH: return Btn::Touchpad;
    default: return Btn::None;
    }
}

static const char* dirName(Btn d)
{
    switch (d) {
    case Btn::Up: return "cima";
    case Btn::Down: return "baixo";
    case Btn::Left: return "esq";
    case Btn::Right: return "dir";
    default: return "-";
    }
}

// Lê a direção de cada fonte neste quadro
static void readDirections(Btn out[SRC_COUNT], int& hatRaw)
{
    for (int i = 0; i < SRC_COUNT; i++) out[i] = Btn::None;
    hatRaw = 0;
    if (!g_joy) return;
    if (SDL_JoystickNumButtons(g_joy) > PAD_RIGHT) {
        if (SDL_JoystickGetButton(g_joy, PAD_UP)) out[SRC_BTN] = Btn::Up;
        else if (SDL_JoystickGetButton(g_joy, PAD_DOWN)) out[SRC_BTN] = Btn::Down;
        else if (SDL_JoystickGetButton(g_joy, PAD_LEFT)) out[SRC_BTN] = Btn::Left;
        else if (SDL_JoystickGetButton(g_joy, PAD_RIGHT)) out[SRC_BTN] = Btn::Right;
    }
    if (SDL_JoystickNumHats(g_joy) > 0) {
        Uint8 h = SDL_JoystickGetHat(g_joy, 0);
        hatRaw = h;
        if (h & SDL_HAT_UP) out[SRC_HAT] = Btn::Up;
        else if (h & SDL_HAT_DOWN) out[SRC_HAT] = Btn::Down;
        else if (h & SDL_HAT_LEFT) out[SRC_HAT] = Btn::Left;
        else if (h & SDL_HAT_RIGHT) out[SRC_HAT] = Btn::Right;
    }
}

std::vector<Btn> poll()
{
    std::vector<Btn> out;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_JOYDEVICEADDED && !g_joy) {
            g_joy = SDL_JoystickOpen(0);
            for (bool& b : g_prevBtn) b = false;
        }
    }
    if (!g_joy) return out;
    SDL_JoystickUpdate();

    // botões de ação: borda de subida (apertou agora)
    int nb = SDL_JoystickNumButtons(g_joy);
    if (nb > kMaxButtons) nb = kMaxButtons;
    for (int i = 0; i < nb; i++) {
        bool down = SDL_JoystickGetButton(g_joy, i) != 0;
        if (down && !g_prevBtn[i]) {
            Btn b = mapButton(i);
            if (b != Btn::None) out.push_back(b);
        }
        g_prevBtn[i] = down;
    }

    // direções: estado atual de cada fonte, com borda de subida por fonte
    uint64_t now = platform::ticksMs();
    Btn cur[SRC_COUNT];
    int hatRaw;
    readDirections(cur, hatRaw);

    bool changed = false;
    for (int s = 0; s < SRC_COUNT; s++) {
        if (cur[s] == g_src[s]) continue;
        changed = true;
        Btn d = cur[s];
        g_src[s] = d;
        if (d == Btn::None) continue;
        // o mesmo toque costuma chegar como botão e como hat: só conta uma vez
        if (d == g_lastEdge && now - g_lastEdgeAt < 120) {
            if (g_held == d && cur[g_heldSrc] != d) g_heldSrc = s;
            continue;
        }
        out.push_back(d);
        g_lastEdge = d;
        g_lastEdgeAt = now;
        g_held = d;
        g_heldSrc = s;
        g_nextRepeat = now + 380;
    }
    if (changed)
        platform::log("direcional: botoes=%s hat=%s(%d)", dirName(cur[SRC_BTN]), dirName(cur[SRC_HAT]), hatRaw);

    // auto-repetição enquanto a fonte que iniciou continua segurando
    if (g_held != Btn::None) {
        if (g_heldSrc < 0 || cur[g_heldSrc] != g_held) {
            g_held = Btn::None;
            g_heldSrc = -1;
        } else if (now >= g_nextRepeat) {
            out.push_back(g_held);
            g_nextRepeat = now + 85;
        }
    }
    return out;
}

} // namespace input

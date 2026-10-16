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

static SDL_Joystick* g_joy = nullptr;
static Btn g_held = Btn::None;     // direção mantida
static uint64_t g_nextRepeat = 0;
static Btn g_axisDir = Btn::None;

void init()
{
    if (SDL_NumJoysticks() > 0) g_joy = SDL_JoystickOpen(0);
    if (!g_joy) platform::log("Nenhum controle encontrado");
}

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
    case PAD_UP: return Btn::Up;
    case PAD_DOWN: return Btn::Down;
    case PAD_LEFT: return Btn::Left;
    case PAD_RIGHT: return Btn::Right;
    default: return Btn::None;
    }
}

static bool isDir(Btn b) { return b == Btn::Up || b == Btn::Down || b == Btn::Left || b == Btn::Right; }

static void press(std::vector<Btn>& out, Btn b)
{
    if (b == Btn::None) return;
    out.push_back(b);
    if (isDir(b)) {
        g_held = b;
        g_nextRepeat = platform::ticksMs() + 380;
    }
}

static void release(Btn b)
{
    if (b == g_held) g_held = Btn::None;
}

std::vector<Btn> poll()
{
    std::vector<Btn> out;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_JOYBUTTONDOWN: press(out, mapButton(ev.jbutton.button)); break;
        case SDL_JOYBUTTONUP: release(mapButton(ev.jbutton.button)); break;
        case SDL_JOYHATMOTION: {
            Btn b = Btn::None;
            if (ev.jhat.value & SDL_HAT_UP) b = Btn::Up;
            else if (ev.jhat.value & SDL_HAT_DOWN) b = Btn::Down;
            else if (ev.jhat.value & SDL_HAT_LEFT) b = Btn::Left;
            else if (ev.jhat.value & SDL_HAT_RIGHT) b = Btn::Right;
            if (b == Btn::None) g_held = Btn::None;
            else press(out, b);
            break;
        }
        case SDL_JOYAXISMOTION: {
            // analógico esquerdo (eixos 0/1) como direcional
            if (ev.jaxis.axis > 1) break;
            int v = ev.jaxis.value;
            Btn b = Btn::None;
            if (ev.jaxis.axis == 0) b = v < -20000 ? Btn::Left : (v > 20000 ? Btn::Right : Btn::None);
            else b = v < -20000 ? Btn::Up : (v > 20000 ? Btn::Down : Btn::None);
            bool sameAxis = (ev.jaxis.axis == 0) == (g_axisDir == Btn::Left || g_axisDir == Btn::Right);
            if (b == Btn::None) {
                if (sameAxis && g_axisDir != Btn::None) { release(g_axisDir); g_axisDir = Btn::None; }
            } else if (b != g_axisDir) {
                g_axisDir = b;
                press(out, b);
            }
            break;
        }
        case SDL_JOYDEVICEADDED:
            if (!g_joy) g_joy = SDL_JoystickOpen(0);
            break;
        default: break;
        }
    }
    // auto-repetição
    uint64_t now = platform::ticksMs();
    if (g_held != Btn::None && now >= g_nextRepeat) {
        out.push_back(g_held);
        g_nextRepeat = now + 85;
    }
    return out;
}

} // namespace input

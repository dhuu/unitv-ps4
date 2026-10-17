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
static Btn g_dir = Btn::None;      // direção mantida no quadro anterior
static uint64_t g_nextRepeat = 0;

void init()
{
    if (SDL_NumJoysticks() > 0) g_joy = SDL_JoystickOpen(0);
    if (!g_joy) platform::log("Nenhum controle encontrado");
    else platform::log("Controle: %d botões, %d hats, %d eixos", SDL_JoystickNumButtons(g_joy),
                       SDL_JoystickNumHats(g_joy), SDL_JoystickNumAxes(g_joy));
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
    default: return Btn::None;
    }
}

// Direção atual combinando direcional (botões ou hat) e analógico esquerdo
static Btn currentDirection()
{
    if (!g_joy) return Btn::None;
    int nb = SDL_JoystickNumButtons(g_joy);
    if (nb > PAD_RIGHT) {
        if (SDL_JoystickGetButton(g_joy, PAD_UP)) return Btn::Up;
        if (SDL_JoystickGetButton(g_joy, PAD_DOWN)) return Btn::Down;
        if (SDL_JoystickGetButton(g_joy, PAD_LEFT)) return Btn::Left;
        if (SDL_JoystickGetButton(g_joy, PAD_RIGHT)) return Btn::Right;
    }
    if (SDL_JoystickNumHats(g_joy) > 0) {
        Uint8 h = SDL_JoystickGetHat(g_joy, 0);
        if (h & SDL_HAT_UP) return Btn::Up;
        if (h & SDL_HAT_DOWN) return Btn::Down;
        if (h & SDL_HAT_LEFT) return Btn::Left;
        if (h & SDL_HAT_RIGHT) return Btn::Right;
    }
    if (SDL_JoystickNumAxes(g_joy) >= 2) {
        // histerese: precisa passar de 24000 para ativar; mantém enquanto acima de 14000
        int x = SDL_JoystickGetAxis(g_joy, 0), y = SDL_JoystickGetAxis(g_joy, 1);
        int on = 24000, keep = 14000;
        auto active = [&](Btn d, int v, bool neg) {
            int lim = g_dir == d ? keep : on;
            return neg ? v < -lim : v > lim;
        };
        if (active(Btn::Up, y, true)) return Btn::Up;
        if (active(Btn::Down, y, false)) return Btn::Down;
        if (active(Btn::Left, x, true)) return Btn::Left;
        if (active(Btn::Right, x, false)) return Btn::Right;
    }
    return Btn::None;
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

    // direções: lidas pelo estado atual (evita eventos duplicados ou "soltar" perdido)
    uint64_t now = platform::ticksMs();
    Btn d = currentDirection();
    if (d != g_dir) {
        g_dir = d;
        if (d != Btn::None) {
            out.push_back(d);
            g_nextRepeat = now + 380;
        }
    } else if (d != Btn::None && now >= g_nextRepeat) {
        out.push_back(d);       // auto-repetição enquanto segura
        g_nextRepeat = now + 85;
    }
    return out;
}

} // namespace input

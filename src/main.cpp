// UnitV Pro para PS4 — port do app Android (com.willdev.unitvpro) usando o OpenOrbis Toolchain
#include "platform.h"
#include "http.h"
#include "gfx.h"
#include "input.h"
#include "jobs.h"
#include "storage.h"
#include "ui.h"
#include "screens.h"
#include "player.h"

#include <orbis/SystemService.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    platform::init();
    platform::log("Iniciando UnitV Pro");

    if (!gfx::init()) {
        platform::log("Falha ao iniciar gráficos");
        for (;;) platform::sleepMs(1000);
    }
    if (!http::init()) platform::log("Falha ao iniciar rede");

    input::init();
    jobs::init(3);
    images::init();
    storage::load();

    app::push(makeSplash());
    app::run();

    // Encerra o app e volta ao menu do PS4
    player::close();
    gfx::shutdown();
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) platform::sleepMs(1000);
    return 0;
}

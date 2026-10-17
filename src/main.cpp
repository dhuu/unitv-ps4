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
#include <pthread.h>

// declarada no musl só com _GNU_SOURCE; existe na libc do OpenOrbis
extern "C" int pthread_setattr_default_np(const pthread_attr_t*);

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    // pilha padrão de 1 MB para todas as threads (a do musl é pequena; o FFmpeg cria as suas)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 1024 * 1024);
        pthread_setattr_default_np(&attr);
        pthread_attr_destroy(&attr);
    }
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

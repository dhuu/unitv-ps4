// Captura de crash: grava no log o sinal, a instrução que falhou e endereços da pilha.
// Os endereços podem ser traduzidos com o mapa de símbolos gerado no CI (UnitVPro.map).
#include "crash.h"
#include "platform.h"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <pthread.h>

extern "C" {
typedef void (*OrbisExceptionHandler)(int, void*);
int sceKernelInstallExceptionHandler(int signum, OrbisExceptionHandler handler);
int sceKernelRemoveExceptionHandler(int signum);
}

namespace crash {

// Contexto entregue pelo kernel (FreeBSD/Orbis): uc_sigmask(16) + 48 bytes, depois o mcontext
struct Mcontext {
    uint64_t onstack, rdi, rsi, rdx, rcx, r8, r9, rax, rbx, rbp, r10, r11, r12, r13, r14, r15;
    int32_t trapno;
    uint16_t fs, gs;
    uint64_t addr;
    int32_t flags;
    uint16_t es, ds;
    uint64_t err, rip, cs, rflags, rsp, ss;
};
struct Ucontext {
    uint32_t sigmask[4];
    int32_t pad[12];
    Mcontext mc;
};

static uint64_t g_base = 0; // endereço de execução de crash::install (referência para o mapa)

static void handler(int sig, void* ctx)
{
    Ucontext* uc = (Ucontext*)ctx;
    const Mcontext& m = uc->mc;
    platform::log("==== CRASH sinal %d (thread %p) ====", sig, (void*)pthread_self());
    platform::log("rip=%016llx addr=%016llx rsp=%016llx rbp=%016llx", (unsigned long long)m.rip,
                  (unsigned long long)m.addr, (unsigned long long)m.rsp, (unsigned long long)m.rbp);
    platform::log("rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx rdi=%016llx rsi=%016llx",
                  (unsigned long long)m.rax, (unsigned long long)m.rbx, (unsigned long long)m.rcx,
                  (unsigned long long)m.rdx, (unsigned long long)m.rdi, (unsigned long long)m.rsi);
    platform::log("referencia crash::install=%016llx (rip relativo: %+lld)", (unsigned long long)g_base,
                  (long long)(m.rip - g_base));
    // possíveis endereços de retorno na pilha (próximos do código do app)
    const uint64_t* sp = (const uint64_t*)m.rsp;
    int found = 0;
    for (int i = 0; i < 512 && found < 16; i++) {
        uint64_t v = sp[i];
        if (v > g_base - (64ull << 20) && v < g_base + (64ull << 20)) {
            platform::log("  pilha[%d] = %016llx (relativo %+lld)", i, (unsigned long long)v, (long long)(v - g_base));
            found++;
        }
    }
    platform::log("==== fim do relatorio ====");
    // devolve ao sistema: o crash segue normalmente (tela de erro do PS4)
    sceKernelRemoveExceptionHandler(sig);
}

static void onTerminate()
{
    platform::log("==== CRASH: std::terminate (excecao C++ nao tratada) ====");
    try {
        std::exception_ptr e = std::current_exception();
        if (e) std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        platform::log("excecao: %s", ex.what());
    } catch (...) {
        platform::log("excecao desconhecida");
    }
    abort();
}

void install()
{
    g_base = (uint64_t)(void*)&install;
    std::set_terminate(onTerminate);
    const int sigs[] = {4 /*SIGILL*/, 6 /*SIGABRT*/, 8 /*SIGFPE*/, 10 /*SIGBUS*/, 11 /*SIGSEGV*/};
    for (int s : sigs) {
        int r = sceKernelInstallExceptionHandler(s, handler);
        if (r < 0) platform::log("exception handler %d: 0x%08X", s, r);
    }
    platform::log("captura de crash ativa (crash::install=%016llx)", (unsigned long long)g_base);
}

} // namespace crash

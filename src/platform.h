#pragma once
#include <string>
#include <cstdint>
#include <functional>

// Caminhos do pacote e de dados graváveis no PS4
#define APP_ASSETS "/app0/assets"
#define APP_DATA_DIR "/data/UnitVPro"

namespace platform {

bool init();                    // carrega módulos do sistema (rede, http, ssl, áudio, ime...)
int32_t userId();               // usuário inicial do PS4
std::string macAddress();       // MAC da interface de rede no formato AA:BB:CC:DD:EE:FF
uint64_t ticksMs();             // relógio monotônico em ms
int64_t unixTime();             // segundos desde epoch (UTC)
void sleepMs(uint32_t ms);
int freeMemoryMB();             // memória flexível livre (MB), -1 se indisponível
void log(const char* fmt, ...);
bool readFile(const std::string& path, std::string& out);
bool writeFile(const std::string& path, const std::string& data);
void ensureDir(const std::string& path);
// Cria uma thread (desanexada) com pilha do tamanho pedido.
// A pilha padrão da libkernel do PS4 é pequena demais para rede/decodificação.
bool startThread(std::function<void()> fn, size_t stackBytes = 512 * 1024);

} // namespace platform

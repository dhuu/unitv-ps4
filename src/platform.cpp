#include "platform.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/Net.h>
#include <orbis/CommonDialog.h>
#include <orbis/AudioOut.h>

namespace platform {

static int32_t g_userId = -1;

bool init()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);

    sceUserServiceInitialize(NULL);
    sceUserServiceGetInitialUser(&g_userId);
    sceCommonDialogInitialize();
    sceAudioOutInit();

    ensureDir(APP_DATA_DIR);
    return true;
}

int32_t userId() { return g_userId; }

std::string macAddress()
{
    OrbisNetEtherAddr addr;
    memset(&addr, 0, sizeof(addr));
    char buf[32];
    if (sceNetGetMacAddress(&addr, 0) < 0)
        return "02:00:00:00:00:00";
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr.data[0], addr.data[1], addr.data[2], addr.data[3], addr.data[4], addr.data[5]);
    return buf;
}

uint64_t ticksMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

int64_t unixTime() { return (int64_t)time(NULL); }

void sleepMs(uint32_t ms) { sceKernelUsleep(ms * 1000); }

void log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[UnitVPro] %s\n", buf);
    // também grava em /data/UnitVPro/log.txt (pode ser copiado via FTP para diagnóstico)
    static FILE* lf = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        mkdir(APP_DATA_DIR, 0777);
        lf = fopen(APP_DATA_DIR "/log.txt", "w");
    }
    if (lf) {
        fprintf(lf, "[%llu] %s\n", (unsigned long long)ticksMs(), buf);
        fflush(lf);
    }
}

bool readFile(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& data)
{
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) return false;
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

void ensureDir(const std::string& path)
{
    mkdir(path.c_str(), 0777);
}

} // namespace platform

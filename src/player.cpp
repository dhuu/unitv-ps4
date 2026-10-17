#include "player.h"
#include "avplayer_abi.h"
#include "platform.h"
#include "http.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

#include <orbis/libkernel.h>
#include <orbis/AudioOut.h>

namespace player {

static std::atomic<SceAvPlayerHandle> g_h{nullptr};
static std::thread g_openThread;
static std::atomic<bool> g_opening{false};
static std::unique_ptr<http::RangeFile> g_file; // filmes/episódios: o app baixa e entrega os bytes ao AvPlayer
static std::atomic<int> g_state{(int)State::Idle};
static std::atomic<bool> g_audioRun{false};
static std::thread g_audioThread;
static std::string g_error;
static std::mutex g_mtx;
static uint64_t g_openedAt = 0;
static uint64_t g_duration = 0;
static bool g_everPlayed = false;
static bool g_userPaused = false;

// Buffer convertido (ARGB na resolução da tela) para redesenhar entre frames
static std::vector<uint32_t> g_frame;
static int g_frameW = 0, g_frameH = 0;
static bool g_hasFrame = false;

// --------------------------------------------------------------------------
// Alocadores exigidos pelo AvPlayer
// --------------------------------------------------------------------------
static void* allocCb(void*, uint32_t align, uint32_t size)
{
    if (align < 16) align = 16;
    void* p = nullptr;
    if (posix_memalign(&p, align, size) != 0) return nullptr;
    return p;
}

static void freeCb(void*, void* mem) { free(mem); }

struct DirectBlock { off_t phys; size_t size; };
static std::map<void*, DirectBlock> g_textures;
static std::mutex g_texMtx;

// Memória de vídeo: memória direta mapeada para CPU+GPU (o decodificador escreve nela)
static void* allocTextureCb(void*, uint32_t align, uint32_t size)
{
    const size_t page = 64 * 1024;
    size_t sz = ((size_t)size + page - 1) & ~(page - 1);
    size_t al = align < page ? page : align;
    off_t phys = 0;
    int ret = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), sz, al, ORBIS_KERNEL_WC_GARLIC, &phys);
    if (ret < 0) { platform::log("AllocateDirectMemory 0x%08X", ret); return nullptr; }
    void* addr = nullptr;
    ret = sceKernelMapDirectMemory(&addr, sz, ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW, 0, phys, al);
    if (ret < 0) {
        platform::log("MapDirectMemory 0x%08X", ret);
        sceKernelReleaseDirectMemory(phys, sz);
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(g_texMtx);
    g_textures[addr] = {phys, sz};
    return addr;
}

static void freeTextureCb(void*, void* mem)
{
    std::lock_guard<std::mutex> lk(g_texMtx);
    auto it = g_textures.find(mem);
    if (it == g_textures.end()) return;
    sceKernelMunmap(mem, it->second.size);
    sceKernelReleaseDirectMemory(it->second.phys, it->second.size);
    g_textures.erase(it);
}

static void eventCb(void*, int32_t eventId, int32_t, void* data)
{
    switch (eventId) {
    case SCE_AVPLAYER_STATE_READY:
        platform::log("AvPlayer READY");
        break;
    case SCE_AVPLAYER_STATE_PLAY:
        g_everPlayed = true;
        g_state = (int)State::Playing;
        break;
    case SCE_AVPLAYER_STATE_PAUSE:
        g_state = (int)State::Paused;
        break;
    case SCE_AVPLAYER_STATE_BUFFERING:
        if (g_state != (int)State::Paused) g_state = (int)State::Buffering;
        break;
    case SCE_AVPLAYER_STATE_STOP:
        g_state = g_everPlayed ? (int)State::Ended : (int)State::Error;
        if (!g_everPlayed) {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_error.empty()) g_error = "Não foi possível abrir o vídeo (formato ou link não suportado).";
        }
        break;
    case SCE_AVPLAYER_WARNING_ID: {
        int32_t code = data ? *(int32_t*)data : 0;
        platform::log("AvPlayer warning 0x%08X", code);
        break;
    }
    case SCE_AVPLAYER_DRM_ERROR:
    case SCE_AVPLAYER_ENCRYPTION: {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_error = "Conteúdo protegido/criptografado não suportado.";
        g_state = (int)State::Error;
        break;
    }
    default: break;
    }
}

// --------------------------------------------------------------------------
// Áudio: lê PCM s16 do AvPlayer, converte para estéreo 48 kHz e envia ao AudioOut
// --------------------------------------------------------------------------
static void audioLoop()
{
    const int kGrain = 1024; // amostras por canal por saída
    int port = -1;
    std::vector<int16_t> pending;  // estéreo intercalado, 48 kHz
    pending.reserve(kGrain * 8);
    double resamplePos = 0.0;
    int16_t lastL = 0, lastR = 0;

    while (g_audioRun) {
        SceAvPlayerFrameInfo fi;
        memset(&fi, 0, sizeof(fi));
        bool got = g_h && sceAvPlayerGetAudioData(g_h, &fi);
        if (!got || !fi.pData) {
            platform::sleepMs(4);
            continue;
        }
        if (port < 0) {
            port = sceAudioOutOpen(platform::userId(), ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kGrain, 48000,
                                   ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
            if (port < 0) {
                platform::log("sceAudioOutOpen 0x%08X", port);
                platform::sleepMs(50);
                continue;
            }
        }
        int ch = fi.details.audio.channelCount ? fi.details.audio.channelCount : 2;
        uint32_t rate = fi.details.audio.sampleRate ? fi.details.audio.sampleRate : 48000;
        const int16_t* src = (const int16_t*)fi.pData;
        size_t frames = fi.details.audio.size / (sizeof(int16_t) * (size_t)ch);

        // downmix para estéreo
        auto frameLR = [&](size_t i, int16_t& l, int16_t& r) {
            const int16_t* f = src + i * (size_t)ch;
            if (ch == 1) { l = r = f[0]; return; }
            if (ch == 2) { l = f[0]; r = f[1]; return; }
            // 5.1/7.1: L R C LFE Ls Rs ...
            int c = ch > 2 ? f[2] : 0;
            int ls = ch > 4 ? f[4] : 0, rs = ch > 5 ? f[5] : 0;
            int L = f[0] + (c * 7 / 10) + (ls * 7 / 10);
            int R = f[1] + (c * 7 / 10) + (rs * 7 / 10);
            L = L > 32767 ? 32767 : (L < -32768 ? -32768 : L);
            R = R > 32767 ? 32767 : (R < -32768 ? -32768 : R);
            l = (int16_t)L; r = (int16_t)R;
        };

        if (rate == 48000) {
            for (size_t i = 0; i < frames; i++) {
                int16_t l, r;
                frameLR(i, l, r);
                pending.push_back(l);
                pending.push_back(r);
            }
        } else {
            // reamostragem linear simples (ex.: 44.1 kHz -> 48 kHz)
            double step = (double)rate / 48000.0;
            while (resamplePos < (double)frames) {
                size_t i0 = (size_t)resamplePos;
                double t = resamplePos - (double)i0;
                int16_t l0, r0, l1, r1;
                if (i0 == 0 && t < 0) { l0 = lastL; r0 = lastR; } else frameLR(i0, l0, r0);
                if (i0 + 1 < frames) frameLR(i0 + 1, l1, r1); else { l1 = l0; r1 = r0; }
                pending.push_back((int16_t)(l0 + (l1 - l0) * t));
                pending.push_back((int16_t)(r0 + (r1 - r0) * t));
                resamplePos += step;
            }
            resamplePos -= (double)frames;
            if (frames) frameLR(frames - 1, lastL, lastR);
        }

        while (pending.size() >= (size_t)kGrain * 2) {
            sceAudioOutOutput(port, pending.data()); // bloqueia: dá o ritmo do áudio
            pending.erase(pending.begin(), pending.begin() + kGrain * 2);
        }
    }
    if (port >= 0) {
        sceAudioOutOutput(port, NULL);
        sceAudioOutClose(port);
    }
}

// --------------------------------------------------------------------------

// Callbacks de "arquivo" do AvPlayer apontando para o RangeFile (HTTP Range)
static int32_t fileOpenCb(void*, const char*) { return 0; }
static int32_t fileCloseCb(void*) { return 0; }
static int32_t fileReadCb(void*, uint8_t* buf, uint64_t pos, uint32_t len)
{
    return g_file ? g_file->read(buf, pos, len) : -1;
}
static uint64_t fileSizeCb(void*) { return g_file ? g_file->size() : 0; }

static void setError(const std::string& msg)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_error = msg;
    g_state = (int)State::Error;
}

static bool hasExt(const std::string& url, const char* ext)
{
    std::string u = url;
    size_t q = u.find('?');
    if (q != std::string::npos) u = u.substr(0, q);
    size_t n = strlen(ext);
    return u.size() >= n && u.compare(u.size() - n, n, ext) == 0;
}

static void openWorker(std::string url)
{
    bool hls = hasExt(url, ".m3u8");
    bool useFile = !hls && !hasExt(url, ".ts");

    if (useFile) {
        g_file.reset(new http::RangeFile());
        if (!g_file->open(url)) {
            g_file.reset();
            setError("Não foi possível conectar ao vídeo (servidor não respondeu ou link inválido).");
            g_opening = false;
            return;
        }
    }

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = allocCb;
    init.memoryReplacement.deallocate = freeCb;
    init.memoryReplacement.allocateTexture = allocTextureCb;
    init.memoryReplacement.deallocateTexture = freeTextureCb;
    init.eventReplacement.eventCallback = eventCb;
    if (useFile) {
        init.fileReplacement.open = fileOpenCb;
        init.fileReplacement.close = fileCloseCb;
        init.fileReplacement.readOffset = fileReadCb;
        init.fileReplacement.size = fileSizeCb;
    }
    init.debugLevel = 0;
    init.basePriority = 700;
    init.numOutputVideoFramebuffers = 2;
    init.autoStart = true;
    init.defaultLanguage = "por";

    SceAvPlayerHandle h = sceAvPlayerInit(&init);
    if (!h) {
        setError("Falha ao iniciar o player do sistema (libSceAvPlayer).");
        g_file.reset();
        g_opening = false;
        return;
    }

    int ret;
    if (useFile) {
        // nome "virtual": o AvPlayer lê pelos callbacks; a extensão indica o formato
        std::string name = "/app0/stream.mp4";
        platform::log("Abrindo (via HTTP Range): %s", url.c_str());
        ret = sceAvPlayerAddSource(h, name.c_str());
    } else {
        int httpCtx = 0, sslCtx = 0;
        http::contexts(httpCtx, sslCtx);
        SceAvPlayerPostInitData post;
        memset(&post, 0, sizeof(post));
        post.httpCtx.httpContextId = (uint32_t)httpCtx;
        post.httpCtx.sslContextId = (uint32_t)sslCtx;
        int pr = sceAvPlayerPostInit(h, &post);
        platform::log("sceAvPlayerPostInit 0x%08X", pr);

        SceAvPlayerSourceDetails sd;
        memset(&sd, 0, sizeof(sd));
        sd.uri.name = url.c_str();
        sd.uri.length = (uint32_t)url.size();
        sd.sourceType = hls ? SCE_AVPLAYER_SOURCE_TYPE_HLS : SCE_AVPLAYER_SOURCE_TYPE_UNKNOWN;
        platform::log("Abrindo (stream): %s", url.c_str());
        ret = sceAvPlayerAddSourceEx(h, 0, &sd);
        if (ret < 0) {
            platform::log("AddSourceEx 0x%08X, tentando AddSource", ret);
            ret = sceAvPlayerAddSource(h, url.c_str());
        }
    }

    if (ret < 0) {
        char buf[200];
        snprintf(buf, sizeof(buf), "O player do PS4 não conseguiu abrir este vídeo (0x%08X).", ret);
        setError(buf);
        sceAvPlayerClose(h);
        g_file.reset();
        g_opening = false;
        return;
    }
    g_h = h;
    g_audioRun = true;
    g_audioThread = std::thread(audioLoop);
    g_opening = false;
}

bool open(const std::string& url)
{
    close();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_error.clear();
    }
    g_everPlayed = false;
    g_userPaused = false;
    g_hasFrame = false;
    g_duration = 0;

    g_state = (int)State::Opening;
    g_openedAt = platform::ticksMs();
    g_opening = true;
    // A abertura faz rede (pode demorar): roda fora da thread da interface
    g_openThread = std::thread(openWorker, url);
    return true;
}

void close()
{
    if (g_openThread.joinable()) g_openThread.join();
    if (g_audioRun) {
        g_audioRun = false;
        if (g_audioThread.joinable()) g_audioThread.join();
    }
    if (g_h) {
        sceAvPlayerStop(g_h);
        sceAvPlayerClose(g_h);
        g_h = nullptr;
    }
    g_file.reset();
    g_hasFrame = false;
    g_state = (int)State::Idle;
}

bool isOpen() { return g_h != nullptr || g_opening; }

State state() { return (State)g_state.load(); }

std::string errorText()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_error;
}

uint64_t positionMs() { return g_h ? sceAvPlayerCurrentTime(g_h) : 0; }

uint64_t durationMs()
{
    if (!g_h) return 0;
    if (g_duration == 0) {
        int n = sceAvPlayerStreamCount(g_h);
        for (int i = 0; i < n && i < 8; i++) {
            SceAvPlayerStreamInfo si;
            memset(&si, 0, sizeof(si));
            if (sceAvPlayerGetStreamInfo(g_h, (uint32_t)i, &si) >= 0 && si.duration > g_duration)
                g_duration = si.duration;
        }
    }
    return g_duration;
}

bool hasVideoFrame() { return g_hasFrame; }

void pause()
{
    if (g_h) { sceAvPlayerPause(g_h); g_userPaused = true; g_state = (int)State::Paused; }
}

void resume()
{
    if (g_h) { sceAvPlayerResume(g_h); g_userPaused = false; g_state = (int)State::Playing; }
}

void togglePause()
{
    if (state() == State::Paused) resume();
    else pause();
}

void seekTo(uint64_t ms)
{
    if (g_h) sceAvPlayerJumpToTime(g_h, ms);
}

// --------------------------------------------------------------------------
// Conversão NV12 -> pixels da tela (escala com "letterbox")
// --------------------------------------------------------------------------
static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static void convertFrame(const uint8_t* base, int pitch, int alignedH, int cropL, int cropT, int visW, int visH,
                         bool hd, int outW, int outH, uint32_t rs, uint32_t gs, uint32_t bs, uint32_t amask)
{
    if (visW <= 0 || visH <= 0) return;
    if ((int)g_frame.size() != outW * outH) g_frame.assign((size_t)outW * outH, amask);
    g_frameW = outW;
    g_frameH = outH;

    // área de destino mantendo proporção
    int dw = outW, dh = (int)((int64_t)visH * outW / visW);
    if (dh > outH) { dh = outH; dw = (int)((int64_t)visW * outH / visH); }
    int dx = (outW - dw) / 2, dy = (outH - dh) / 2;

    // coeficientes (faixa limitada) BT.709 para HD, BT.601 para SD — ponto fixo 16.16
    int cy = 76309;
    int crv = hd ? 117489 : 104597, cgu = hd ? 13975 : 25674, cgv = hd ? 34925 : 53278, cbu = hd ? 138438 : 132201;

    static std::vector<int> xmap;
    xmap.resize((size_t)dw);
    for (int x = 0; x < dw; x++) xmap[(size_t)x] = cropL + (int)((int64_t)x * visW / dw);

    const uint8_t* yPlane = base;
    const uint8_t* uvPlane = base + (size_t)pitch * alignedH;
    static std::vector<uint8_t> yRow, uvRow;
    yRow.resize((size_t)pitch);
    uvRow.resize((size_t)pitch);
    int lastSy = -1, lastSuv = -1;

    // bordas pretas
    for (int y = 0; y < dy; y++) memset(&g_frame[(size_t)y * outW], 0, (size_t)outW * 4);
    for (int y = dy + dh; y < outH; y++) memset(&g_frame[(size_t)y * outW], 0, (size_t)outW * 4);

    for (int y = 0; y < dh; y++) {
        int sy = cropT + (int)((int64_t)y * visH / dh);
        int suv = sy >> 1;
        // copia a linha da memória de vídeo (write-combined, leitura lenta) para memória cacheada
        if (sy != lastSy) { memcpy(yRow.data(), yPlane + (size_t)sy * pitch, (size_t)pitch); lastSy = sy; }
        if (suv != lastSuv) { memcpy(uvRow.data(), uvPlane + (size_t)suv * pitch, (size_t)pitch); lastSuv = suv; }
        uint32_t* out = &g_frame[(size_t)(y + dy) * outW];
        for (int x = 0; x < dx; x++) out[x] = amask;
        for (int x = dx + dw; x < outW; x++) out[x] = amask;
        out += dx;
        const uint8_t* yr = yRow.data();
        const uint8_t* uvr = uvRow.data();
        for (int x = 0; x < dw; x++) {
            int sx = xmap[(size_t)x];
            int Y = (yr[sx] - 16) * cy;
            int u = uvr[sx & ~1] - 128;
            int v = uvr[sx | 1] - 128;
            uint8_t r = clamp8((Y + crv * v) >> 16);
            uint8_t g = clamp8((Y - cgu * u - cgv * v) >> 16);
            uint8_t b = clamp8((Y + cbu * u) >> 16);
            out[x] = ((uint32_t)r << rs) | ((uint32_t)g << gs) | ((uint32_t)b << bs) | amask;
        }
    }
    g_hasFrame = true;
}

static int shiftOf(uint32_t mask)
{
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; s++; }
    return s;
}

void render(SDL_Surface* dst)
{
    if (!dst) return;
    if (g_h) {
        SceAvPlayerFrameInfoEx fi;
        memset(&fi, 0, sizeof(fi));
        if (sceAvPlayerGetVideoDataEx(g_h, &fi) && fi.pData) {
            const SceAvPlayerVideoEx& v = fi.details.video;
            int pitch = v.pitch ? (int)v.pitch : (int)v.width;
            int alignedH = (int)v.height;
            int visW = pitch - (int)v.cropLeftOffset - (int)v.cropRightOffset;
            int visH = alignedH - (int)v.cropTopOffset - (int)v.cropBottomOffset;
            if (visW <= 0 || visW > pitch) visW = (int)v.width;
            if (visH <= 0 || visH > alignedH) visH = alignedH;
            SDL_PixelFormat* pf = dst->format;
            convertFrame((const uint8_t*)fi.pData, pitch, alignedH, (int)v.cropLeftOffset, (int)v.cropTopOffset,
                         visW, visH, visH >= 700, dst->w, dst->h, (uint32_t)shiftOf(pf->Rmask),
                         (uint32_t)shiftOf(pf->Gmask), (uint32_t)shiftOf(pf->Bmask), pf->Amask);
            if (g_state == (int)State::Opening || g_state == (int)State::Buffering) {
                if (!g_userPaused) g_state = (int)State::Playing;
            }
            g_everPlayed = true;
        }
        if (g_everPlayed && g_state == (int)State::Playing && !sceAvPlayerIsActive(g_h))
            g_state = (int)State::Ended;
    }

    // tempo limite para abrir
    if (g_state == (int)State::Opening && !g_opening && platform::ticksMs() - g_openedAt > 40000) {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_error = "Tempo esgotado ao conectar ao vídeo.";
        g_state = (int)State::Error;
    }

    if (g_hasFrame && g_frameW == dst->w && g_frameH == dst->h) {
        SDL_LockSurface(dst);
        for (int y = 0; y < dst->h; y++)
            memcpy((uint8_t*)dst->pixels + (size_t)y * dst->pitch, &g_frame[(size_t)y * g_frameW], (size_t)g_frameW * 4);
        SDL_UnlockSurface(dst);
    } else {
        SDL_FillRect(dst, NULL, SDL_MapRGB(dst->format, 0, 0, 0));
    }
}

} // namespace player

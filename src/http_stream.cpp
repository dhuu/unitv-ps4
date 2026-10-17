// Download por faixas (HTTP Range) para alimentar o AvPlayer com arquivos remotos
#include "http.h"
#include "platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <orbis/Http.h>

#define UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

namespace http {

typedef int32_t (*SetTimeoutFn)(int32_t, uint32_t);

static int skipSsl(int, unsigned int, void* const[], int, void*) { return 1; }

static uint64_t parseTotal(const char* hdr, size_t len, uint64_t from, int status)
{
    std::string h(hdr ? hdr : "", hdr ? len : 0);
    std::string low = h;
    for (auto& c : low) c = (char)tolower((unsigned char)c);
    size_t p = low.find("content-range:");
    if (p != std::string::npos) {
        size_t slash = low.find('/', p);
        size_t eol = low.find('\n', p);
        if (slash != std::string::npos && (eol == std::string::npos || slash < eol)) {
            uint64_t t = strtoull(h.c_str() + slash + 1, nullptr, 10);
            if (t) return t;
        }
    }
    p = low.find("content-length:");
    if (p != std::string::npos) {
        uint64_t l = strtoull(h.c_str() + p + 15, nullptr, 10);
        if (l) return status == 206 ? from + l : l;
    }
    return 0;
}

bool streamOpen(Stream& st, const std::string& url, uint64_t from)
{
    streamClose(st);
    int httpCtx, sslCtx;
    contexts(httpCtx, sslCtx);
    uint32_t usec = 20u * 1000000u;
    st.tpl = sceHttpCreateTemplate(httpCtx, UA, ORBIS_HTTP_VERSION_1_1, 1);
    if (st.tpl < 0) return false;
    sceHttpsSetSslCallback(st.tpl, skipSsl, NULL);
    sceHttpsDisableOption(st.tpl, 0x01);
    sceHttpSetConnectTimeOut(st.tpl, usec);
    sceHttpSetResolveTimeOut(st.tpl, usec);
    ((SetTimeoutFn)(void*)&sceHttpSetRecvTimeOut)(st.tpl, usec);
    st.conn = sceHttpCreateConnectionWithURL(st.tpl, url.c_str(), true);
    if (st.conn < 0) { streamClose(st); return false; }
    st.req = sceHttpCreateRequestWithURL(st.conn, ORBIS_METHOD_GET, url.c_str(), 0);
    if (st.req < 0) { streamClose(st); return false; }
    char range[64];
    snprintf(range, sizeof(range), "bytes=%llu-", (unsigned long long)from);
    sceHttpAddRequestHeader(st.req, "Range", range, 0);
    sceHttpAddRequestHeader(st.req, "Accept", "*/*", 0);
    int ret = sceHttpSendRequest(st.req, NULL, 0);
    if (ret < 0) { platform::log("stream send 0x%08X", ret); streamClose(st); return false; }
    int32_t status = 0;
    sceHttpGetStatusCode(st.req, &status);
    if (status != 200 && status != 206) { platform::log("stream HTTP %d", status); streamClose(st); return false; }
    char* hdr = nullptr;
    size_t hlen = 0;
    if (sceHttpGetAllResponseHeaders(st.req, &hdr, &hlen) >= 0) st.total = parseTotal(hdr, hlen, from, status);
    st.open = true;
    st.pos = 0;
    if (status == 206 || from == 0) {
        st.pos = from;
    } else {
        // servidor ignorou o Range: descarta bytes até chegar em "from"
        std::vector<char> skip(64 * 1024);
        while (st.pos < from) {
            uint32_t want = (uint32_t)std::min<uint64_t>(skip.size(), from - st.pos);
            int n = sceHttpReadData(st.req, skip.data(), want);
            if (n <= 0) { streamClose(st); return false; }
            st.pos += (uint64_t)n;
        }
    }
    return true;
}

int streamRead(Stream& st, void* buf, uint32_t len)
{
    if (!st.open) return -1;
    int n = sceHttpReadData(st.req, buf, len);
    if (n > 0) st.pos += (uint64_t)n;
    return n;
}

void streamClose(Stream& st)
{
    if (st.req >= 0) sceHttpDeleteRequest(st.req);
    if (st.conn >= 0) sceHttpDeleteConnection(st.conn);
    if (st.tpl >= 0) sceHttpDeleteTemplate(st.tpl);
    st.req = st.conn = st.tpl = -1;
    st.open = false;
}

typedef int32_t (*SetAutoRedirectFn)(int32_t, int32_t);

std::string resolveRedirects(const std::string& urlIn)
{
    std::string url = urlIn;
    int httpCtx, sslCtx;
    contexts(httpCtx, sslCtx);
    for (int hop = 0; hop < 6; hop++) {
        int tpl = sceHttpCreateTemplate(httpCtx, UA, ORBIS_HTTP_VERSION_1_1, 1);
        if (tpl < 0) break;
        sceHttpsSetSslCallback(tpl, skipSsl, NULL);
        sceHttpsDisableOption(tpl, 0x01);
        ((SetAutoRedirectFn)(void*)&sceHttpSetAutoRedirect)(tpl, 0);
        sceHttpSetConnectTimeOut(tpl, 15u * 1000000u);
        ((SetTimeoutFn)(void*)&sceHttpSetRecvTimeOut)(tpl, 15u * 1000000u);
        int conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), true);
        int req = conn >= 0 ? sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0) : -1;
        std::string next;
        if (req >= 0) {
            sceHttpAddRequestHeader(req, "Range", "bytes=0-0", 0);
            if (sceHttpSendRequest(req, NULL, 0) >= 0) {
                int32_t status = 0;
                sceHttpGetStatusCode(req, &status);
                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    char* hdr = nullptr;
                    size_t hlen = 0;
                    if (sceHttpGetAllResponseHeaders(req, &hdr, &hlen) >= 0 && hdr) {
                        std::string h(hdr, hlen), low = h;
                        for (auto& c : low) c = (char)tolower((unsigned char)c);
                        size_t p = low.find("\nlocation:");
                        if (p == std::string::npos && low.compare(0, 9, "location:") == 0) p = 0;
                        else if (p != std::string::npos) p += 1;
                        if (p != std::string::npos) {
                            size_t st = p + 9, en = h.find_first_of("\r\n", st);
                            next = h.substr(st, en == std::string::npos ? std::string::npos : en - st);
                            while (!next.empty() && next[0] == ' ') next.erase(0, 1);
                        }
                    }
                }
            }
        }
        if (req >= 0) sceHttpDeleteRequest(req);
        if (conn >= 0) sceHttpDeleteConnection(conn);
        sceHttpDeleteTemplate(tpl);
        if (next.empty()) break;
        if (next[0] == '/') { // relativo ao host
            size_t s3 = url.find("://");
            size_t slash = s3 == std::string::npos ? std::string::npos : url.find('/', s3 + 3);
            next = (slash == std::string::npos ? url : url.substr(0, slash)) + next;
        }
        platform::log("Redirect -> %s", next.c_str());
        url = next;
    }
    return url;
}

static const uint64_t kBlock = 512 * 1024;
static const size_t kMaxBlocks = 24; // ~12 MB de cache

bool RangeFile::open(const std::string& url)
{
    std::lock_guard<std::mutex> lk(mtx_);
    url_ = url;
    cache_.clear();
    if (!streamOpen(st_, url_, 0)) return false;
    size_ = st_.total;
    platform::log("RangeFile aberto, tamanho=%llu", (unsigned long long)size_);
    return size_ > 0;
}

void RangeFile::close()
{
    std::lock_guard<std::mutex> lk(mtx_);
    streamClose(st_);
    cache_.clear();
}

bool RangeFile::fetch(uint64_t idx, Block& out)
{
    uint64_t start = idx * kBlock;
    if (start >= size_) return false;
    uint64_t want = std::min<uint64_t>(kBlock, size_ - start);
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!st_.open || st_.pos != start) {
            if (!streamOpen(st_, url_, start)) { platform::sleepMs(300); continue; }
        }
        out.idx = idx;
        out.data.resize((size_t)want);
        uint64_t got = 0;
        while (got < want) {
            int n = streamRead(st_, out.data.data() + got, (uint32_t)(want - got));
            if (n <= 0) break;
            got += (uint64_t)n;
        }
        if (got == want) return true;
        streamClose(st_); // conexão caiu: tenta reabrir
    }
    return false;
}

int RangeFile::read(uint8_t* buf, uint64_t pos, uint32_t len)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (pos >= size_) return 0;
    if (pos + len > size_) len = (uint32_t)(size_ - pos);
    uint32_t done = 0;
    while (done < len) {
        uint64_t p = pos + done;
        uint64_t idx = p / kBlock;
        Block* b = nullptr;
        for (auto& c : cache_)
            if (c.idx == idx && !c.data.empty()) { b = &c; break; }
        if (!b) {
            Block nb;
            if (!fetch(idx, nb)) return done ? (int)done : -1;
            if (cache_.size() >= kMaxBlocks) {
                size_t old = 0;
                for (size_t i = 1; i < cache_.size(); i++)
                    if (cache_[i].used < cache_[old].used) old = i;
                cache_[old] = std::move(nb);
                b = &cache_[old];
            } else {
                cache_.push_back(std::move(nb));
                b = &cache_.back();
            }
        }
        b->used = ++tick_;
        uint64_t off = p - idx * kBlock;
        uint32_t n = (uint32_t)std::min<uint64_t>(b->data.size() - off, len - done);
        memcpy(buf + done, b->data.data() + off, n);
        done += n;
    }
    return (int)done;
}

} // namespace http

// zlib imports: the guest's 32-bit z_stream is mirrored into a host z_stream around each call.
#include <map>
#include <mutex>
#include <string>

#include <zlib.h>

#include "guest_mem.h"
#include "hle.h"

namespace compat {

namespace {

// Guest z_stream (32-bit) field offsets.
enum : u32 {
    ZS_NEXT_IN = 0, ZS_AVAIL_IN = 4, ZS_TOTAL_IN = 8, ZS_NEXT_OUT = 12, ZS_AVAIL_OUT = 16, ZS_TOTAL_OUT = 20,
    ZS_MSG = 24, ZS_STATE = 28, ZS_DATA_TYPE = 44, ZS_ADLER = 48,
};

std::mutex g_mu;
std::map<u32, z_stream*> g_streams;
std::map<std::string, u32> g_msgs;

z_stream* host_stream(u32 g, bool create) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_streams.find(g);
    if (it != g_streams.end()) return it->second;
    if (!create) return nullptr;
    auto* z = new z_stream();
    g_streams[g] = z;
    return z;
}

void drop_stream(u32 g) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_streams.find(g);
    if (it != g_streams.end()) { delete it->second; g_streams.erase(it); }
}

void to_host(u32 g, z_stream* z) {
    u32 ni = rd32(g + ZS_NEXT_IN), no = rd32(g + ZS_NEXT_OUT);
    z->next_in = ni ? HP<Bytef>(ni) : Z_NULL;
    z->avail_in = rd32(g + ZS_AVAIL_IN);
    z->next_out = no ? HP<Bytef>(no) : Z_NULL;
    z->avail_out = rd32(g + ZS_AVAIL_OUT);
    z->total_in = rd32(g + ZS_TOTAL_IN);
    z->total_out = rd32(g + ZS_TOTAL_OUT);
}

void to_guest(u32 g, const z_stream* z) {
    wr32(g + ZS_NEXT_IN, z->next_in ? G(z->next_in) : 0);
    wr32(g + ZS_AVAIL_IN, z->avail_in);
    wr32(g + ZS_NEXT_OUT, z->next_out ? G(z->next_out) : 0);
    wr32(g + ZS_AVAIL_OUT, z->avail_out);
    wr32(g + ZS_TOTAL_IN, static_cast<u32>(z->total_in));
    wr32(g + ZS_TOTAL_OUT, static_cast<u32>(z->total_out));
    u32 msg = 0;
    if (z->msg) {
        std::lock_guard<std::mutex> lk(g_mu);
        auto& m = g_msgs[z->msg];
        if (!m) m = data_strdup(z->msg);
        msg = m;
    }
    wr32(g + ZS_MSG, msg);
    wr32(g + ZS_STATE, g);  // non-null marker; the real state lives on the host
    wr32(g + ZS_DATA_TYPE, static_cast<u32>(z->data_type));
    wr32(g + ZS_ADLER, static_cast<u32>(z->adler));
}

template <typename F> void with_stream(Ctx& c, bool create, F&& f) {
    u32 g = c.arg(0);
    z_stream* z = host_stream(g, create);
    if (!z) { c.ret(static_cast<u32>(Z_STREAM_ERROR)); return; }
    to_host(g, z);
    int r = f(z);
    to_guest(g, z);
    c.ret(static_cast<u32>(r));
}

void h_inflateInit_(Ctx& c) { with_stream(c, true, [](z_stream* z) { return inflateInit(z); }); }
void h_inflateInit2_(Ctx& c) {
    int wb = static_cast<int>(c.arg(1));
    with_stream(c, true, [wb](z_stream* z) { return inflateInit2(z, wb); });
}
void h_inflate(Ctx& c) {
    int fl = static_cast<int>(c.arg(1));
    with_stream(c, false, [fl](z_stream* z) { return inflate(z, fl); });
}
void h_inflateReset(Ctx& c) { with_stream(c, false, [](z_stream* z) { return inflateReset(z); }); }
void h_inflateEnd(Ctx& c) {
    u32 g = c.arg(0);  // with_stream overwrites r0 with the result
    with_stream(c, false, [](z_stream* z) { return inflateEnd(z); });
    drop_stream(g);
    wr32(g + ZS_STATE, 0);
}
void h_deflateInit2_(Ctx& c) {
    int level = static_cast<int>(c.arg(1)), method = static_cast<int>(c.arg(2)), wb = static_cast<int>(c.arg(3));
    int mem = static_cast<int>(c.arg(4)), strat = static_cast<int>(c.arg(5));
    with_stream(c, true, [=](z_stream* z) { return deflateInit2(z, level, method, wb, mem, strat); });
}
void h_deflate(Ctx& c) {
    int fl = static_cast<int>(c.arg(1));
    with_stream(c, false, [fl](z_stream* z) { return deflate(z, fl); });
}
void h_deflateReset(Ctx& c) { with_stream(c, false, [](z_stream* z) { return deflateReset(z); }); }
void h_deflateEnd(Ctx& c) {
    u32 g = c.arg(0);
    with_stream(c, false, [](z_stream* z) { return deflateEnd(z); });
    drop_stream(g);
    wr32(g + ZS_STATE, 0);
}
void h_crc32(Ctx& c) {
    u32 buf = c.arg(1);
    c.ret(static_cast<u32>(crc32(c.arg(0), buf ? HP<Bytef>(buf) : Z_NULL, c.arg(2))));
}
void h_uncompress(Ctx& c) {
    uLongf dlen = rd32(c.arg(1));
    int r = uncompress(HP<Bytef>(c.arg(0)), &dlen, HP<Bytef>(c.arg(2)), c.arg(3));
    wr32(c.arg(1), static_cast<u32>(dlen));
    c.ret(static_cast<u32>(r));
}

std::map<u32, gzFile> g_gz;
void h_gzopen(Ctx& c) {
    std::string path = map_path(HS(c.arg(0)));
    gzFile f = gzopen(path.c_str(), HS(c.arg(1)));
    if (!f) { c.ret(0); return; }
    u32 token = gmalloc(4);
    std::lock_guard<std::mutex> lk(g_mu);
    g_gz[token] = f;
    c.ret(token);
}
gzFile gz_for(u32 t) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_gz.find(t);
    return it == g_gz.end() ? nullptr : it->second;
}
void h_gzread(Ctx& c) {
    gzFile f = gz_for(c.arg(0));
    c.ret(f ? static_cast<u32>(gzread(f, H(c.arg(1)), c.arg(2))) : 0xFFFFFFFFu);
}
void h_gzclose(Ctx& c) {
    gzFile f = gz_for(c.arg(0));
    if (!f) { c.ret(static_cast<u32>(Z_STREAM_ERROR)); return; }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_gz.erase(c.arg(0));
    }
    gfree(c.arg(0));
    c.ret(static_cast<u32>(gzclose(f)));
}

}  // namespace

void hle_register_zlib() {
#define R(n) hle_func(#n, h_##n)
    R(inflateInit_); R(inflateInit2_); R(inflate); R(inflateReset); R(inflateEnd); R(deflateInit2_); R(deflate);
    R(deflateReset); R(deflateEnd); R(crc32); R(uncompress); R(gzopen); R(gzread); R(gzclose);
#undef R
}

}  // namespace compat

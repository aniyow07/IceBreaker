// Minimal PNG writer for harness screenshots (RGBA8, zlib from the host).
#include <cstdio>

#include <zlib.h>

#include "harness.h"

namespace compat {

namespace {
void be32(std::vector<unsigned char>& v, unsigned x) {
    v.push_back(static_cast<unsigned char>(x >> 24));
    v.push_back(static_cast<unsigned char>(x >> 16));
    v.push_back(static_cast<unsigned char>(x >> 8));
    v.push_back(static_cast<unsigned char>(x));
}
void chunk(std::FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> buf;
    be32(buf, static_cast<unsigned>(data.size()));
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    unsigned crc = static_cast<unsigned>(crc32(0, buf.data() + 4, static_cast<uInt>(buf.size() - 4)));
    be32(buf, crc);
    std::fwrite(buf.data(), 1, buf.size(), f);
}
}  // namespace

bool png_write(const std::string& path, int w, int h, const std::vector<unsigned char>& px) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
    for (int y = h - 1; y >= 0; --y) {  // GL rows are bottom-up
        raw.push_back(0);
        const unsigned char* row = &px[static_cast<size_t>(y) * w * 4];
        raw.insert(raw.end(), row, row + w * 4);
    }
    uLongf clen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> comp(clen);
    if (compress2(comp.data(), &clen, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return false;
    comp.resize(clen);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    be32(ihdr, static_cast<unsigned>(w));
    be32(ihdr, static_cast<unsigned>(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", comp);
    chunk(f, "IEND", {});
    std::fclose(f);
    return true;
}

}  // namespace compat

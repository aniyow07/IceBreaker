#include "zip_reader.h"

#include <algorithm>
#include <cstdio>

#include <zlib.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace compat {

namespace {
u32 le32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (u32(p[3]) << 24); }
u16 le16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
}  // namespace

bool zip_read(const std::string& path, const std::string& member, std::vector<u8>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    long tail = size < 66000 ? size : 66000;
    std::vector<u8> t(static_cast<size_t>(tail));
    std::fseek(f, size - tail, SEEK_SET);
    bool ok = std::fread(t.data(), 1, t.size(), f) == t.size();
    long eocd = -1;
    for (long i = tail - 22; ok && i >= 0; --i)
        if (le32(&t[static_cast<size_t>(i)]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { std::fclose(f); return false; }
    u32 cd_size = le32(&t[static_cast<size_t>(eocd) + 12]), cd_off = le32(&t[static_cast<size_t>(eocd) + 16]);
    std::vector<u8> cd(cd_size);
    std::fseek(f, static_cast<long>(cd_off), SEEK_SET);
    if (std::fread(cd.data(), 1, cd.size(), f) != cd.size()) { std::fclose(f); return false; }

    for (size_t p = 0; p + 46 <= cd.size();) {
        if (le32(&cd[p]) != 0x02014b50) break;
        u16 method = le16(&cd[p + 10]);
        u32 csize = le32(&cd[p + 20]), usize = le32(&cd[p + 24]);
        u16 nlen = le16(&cd[p + 28]), xlen = le16(&cd[p + 30]), clen = le16(&cd[p + 32]);
        u32 lho = le32(&cd[p + 42]);
        std::string name(reinterpret_cast<const char*>(&cd[p + 46]), nlen);
        p += 46 + nlen + xlen + clen;
        if (name != member) continue;

        u8 lh[30];
        std::fseek(f, static_cast<long>(lho), SEEK_SET);
        if (std::fread(lh, 1, 30, f) != 30 || le32(lh) != 0x04034b50) break;
        std::fseek(f, static_cast<long>(lho + 30 + le16(lh + 26) + le16(lh + 28)), SEEK_SET);
        std::vector<u8> comp(csize);
        if (std::fread(comp.data(), 1, csize, f) != csize) break;
        std::fclose(f);
        if (method == 0) { out = std::move(comp); return out.size() == usize; }
        if (method != 8) return false;
        out.assign(usize, 0);
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
        z.next_in = comp.data();
        z.avail_in = csize;
        z.next_out = out.data();
        z.avail_out = usize;
        int r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        return r == Z_STREAM_END && z.total_out == usize;
    }
    std::fclose(f);
    return false;
}

bool ZipIndex::open(const std::string& path) {
#if !defined(_WIN32)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) {
        ::close(fd);
        return false;
    }
    void* m = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (m == MAP_FAILED) return false;
    data_ = static_cast<const u8*>(m);
    size_ = static_cast<size_t>(st.st_size);
#else
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    owned_.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    bool ok = std::fread(owned_.data(), 1, owned_.size(), f) == owned_.size();
    std::fclose(f);
    if (!ok || owned_.size() < 22) return false;
    data_ = owned_.data();
    size_ = owned_.size();
#endif
    size_t eocd = 0;
    bool found = false;
    size_t lowest = size_ > 66000 ? size_ - 66000 : 0;
    for (size_t i = size_ - 22;; --i) {
        if (le32(data_ + i) == 0x06054b50) { eocd = i; found = true; break; }
        if (i == lowest) break;
    }
    if (!found) return false;
    u32 cd_size = le32(data_ + eocd + 12), cd_off = le32(data_ + eocd + 16);
    if (u64(cd_off) + cd_size > size_) return false;
    for (u64 p = cd_off; p + 46 <= u64(cd_off) + cd_size;) {
        const u8* h = data_ + p;
        if (le32(h) != 0x02014b50) break;
        Entry e{le16(h + 10), le32(h + 20), le32(h + 24), le32(h + 42)};
        u16 nlen = le16(h + 28), xlen = le16(h + 30), clen = le16(h + 32);
        entries_.emplace_back(std::string(reinterpret_cast<const char*>(h + 46), nlen), e);
        p += 46u + nlen + xlen + clen;
    }
    // Stable sort keeps central-directory order for duplicate names (minizip returns the first match).
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const std::pair<std::string, Entry>& a, const std::pair<std::string, Entry>& b) {
                         return a.first < b.first;
                     });
    return true;
}

const ZipIndex::Entry* ZipIndex::find(const std::string& name) const {
    auto it = std::lower_bound(entries_.begin(), entries_.end(), name,
                               [](const std::pair<std::string, Entry>& a, const std::string& n) { return a.first < n; });
    return (it != entries_.end() && it->first == name) ? &it->second : nullptr;
}

bool ZipIndex::extract(const Entry& e, u8* dst) const {
    if (u64(e.local_header) + 30 > size_) return false;
    const u8* lh = data_ + e.local_header;
    if (le32(lh) != 0x04034b50) return false;
    u64 off = u64(e.local_header) + 30 + le16(lh + 26) + le16(lh + 28);
    if (off + e.csize > size_) return false;
    if (e.method == 0) {
        if (e.csize != e.usize) return false;
        std::memcpy(dst, data_ + off, e.usize);
        return true;
    }
    if (e.method != 8) return false;
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(data_ + off);
    z.avail_in = e.csize;
    z.next_out = dst;
    z.avail_out = e.usize;
    int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return r == Z_STREAM_END && z.total_out == e.usize;
}

}  // namespace compat

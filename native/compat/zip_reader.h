#pragma once
#include <string>
#include <utility>
#include <vector>

#include "common.h"

namespace compat {

// Reads one member of a ZIP/APK file (stored or deflated). Returns false if absent or damaged.
bool zip_read(const std::string& zip_path, const std::string& member, std::vector<u8>& out);

// Indexed, memory-mapped view of one ZIP/APK. The central directory is parsed once.
class ZipIndex {
public:
    struct Entry {
        u32 method, csize, usize, local_header;
    };
    bool open(const std::string& path);
    const Entry* find(const std::string& name) const;
    // Decompresses an entry into dst (entry.usize bytes). Returns false on damage.
    bool extract(const Entry& e, u8* dst) const;

private:
    std::vector<u8> owned_;  // used when mmap is unavailable
    const u8* data_ = nullptr;
    size_t size_ = 0;
    std::vector<std::pair<std::string, Entry>> entries_;
};

}  // namespace compat

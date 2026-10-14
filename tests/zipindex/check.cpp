// Extracts every entry of an APK with compat::ZipIndex and prints "name<TAB>size<TAB>crc32".
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>
#include "zip_reader.h"
int main(int argc, char** argv) {
    compat::ZipIndex z;
    if (!z.open(argv[1])) { std::puts("OPEN FAILED"); return 1; }
    char line[4096];
    while (std::fgets(line, sizeof line, stdin)) {
        std::string n(line);
        while (!n.empty() && (n.back() == '\n' || n.back() == '\r')) n.pop_back();
        const auto* e = z.find(n);
        if (!e) { std::printf("%s\tMISSING\n", n.c_str()); continue; }
        std::vector<unsigned char> b(e->usize + 1);
        bool ok = z.extract(*e, b.data());
        std::printf("%s\t%u\t%08lx%s\n", n.c_str(), e->usize, crc32(0, b.data(), e->usize), ok ? "" : "\tFAIL");
    }
}

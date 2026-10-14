#pragma once
#include <functional>
#include <string>
#include <vector>

#include "common.h"

namespace compat {

struct Module {
    std::string name;
    u32 base = 0;      // guest address that ELF vaddr 0 maps to
    u32 size = 0;      // span of all PT_LOAD segments
    u32 symtab = 0, strtab = 0, hash = 0;
    u32 nbucket = 0, nchain = 0;
    u32 rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0;
    u32 init_array = 0, init_array_sz = 0, init_fn = 0;
    u32 exidx = 0, exidx_count = 0;
    bool contains(u32 a) const { return a >= base && a < base + size; }
};

// Resolves an undefined symbol name to a guest address (0 = not found).
using SymbolResolver = std::function<u32(const char* name, bool is_function)>;

// Maps an ELF32 ARM shared object into guest memory at `base`.
Module* load_module(const std::string& name, const std::vector<u8>& image, u32 base);
// Applies relocations. Definitions inside the module win (DT_SYMBOLIC), then `resolve`.
void link_module(Module& m, const SymbolResolver& resolve);
// Runs DT_INIT and DT_INIT_ARRAY on the calling thread.
void init_module(Module& m);

u32 module_symbol(const Module& m, const char* name);  // exported definition or 0
const Module* module_for_address(u32 addr);
const std::vector<Module*>& modules();

}  // namespace compat

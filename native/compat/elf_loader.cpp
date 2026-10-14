#include "elf_loader.h"

#include "cpu.h"
#include "guest_mem.h"

namespace compat {

namespace {

#pragma pack(push, 1)
struct Ehdr {
    u8 ident[16];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Phdr {
    u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
};
struct Sym {
    u32 name, value, size;
    u8 info, other;
    u16 shndx;
};
#pragma pack(pop)

constexpr u32 PT_LOAD = 1, PT_DYNAMIC = 2, PT_ARM_EXIDX = 0x70000001;
constexpr u32 DT_NULL = 0, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_INIT = 12, DT_REL = 17, DT_RELSZ = 18,
              DT_PLTRELSZ = 2, DT_JMPREL = 23, DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27;
constexpr u32 R_ARM_ABS32 = 2, R_ARM_GLOB_DAT = 21, R_ARM_JUMP_SLOT = 22, R_ARM_RELATIVE = 23;

std::vector<Module*> g_modules;

u32 elf_hash(const char* s) {
    u32 h = 0;
    for (const u8* p = reinterpret_cast<const u8*>(s); *p; ++p) {
        h = (h << 4) + *p;
        u32 g = h & 0xF0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

Sym sym_at(const Module& m, u32 index) {
    Sym s;
    std::memcpy(&s, H(m.symtab + index * sizeof(Sym)), sizeof s);
    return s;
}

}  // namespace

const std::vector<Module*>& modules() { return g_modules; }

Module* load_module(const std::string& name, const std::vector<u8>& img, u32 base) {
    if (img.size() < sizeof(Ehdr)) fatal("%s: too small for ELF", name.c_str());
    Ehdr eh;
    std::memcpy(&eh, img.data(), sizeof eh);
    if (std::memcmp(eh.ident, "\x7f" "ELF", 4) != 0 || eh.ident[4] != 1 || eh.ident[5] != 1 || eh.machine != 40)
        fatal("%s: not a little-endian ELF32 ARM object", name.c_str());

    auto* m = new Module();
    m->name = name;
    m->base = base;
    u32 dyn = 0;
    u32 hi = 0;
    for (u32 i = 0; i < eh.phnum; ++i) {
        Phdr ph;
        std::memcpy(&ph, img.data() + eh.phoff + i * eh.phentsize, sizeof ph);
        if (ph.type == PT_LOAD) {
            if (u64(ph.offset) + ph.filesz > img.size()) fatal("%s: segment outside file", name.c_str());
            mem_commit(base + ph.vaddr, ph.memsz);
            std::memcpy(H(base + ph.vaddr), img.data() + ph.offset, ph.filesz);
            std::memset(H(base + ph.vaddr + ph.filesz), 0, ph.memsz - ph.filesz);
            if (ph.vaddr + ph.memsz > hi) hi = ph.vaddr + ph.memsz;
        } else if (ph.type == PT_DYNAMIC) {
            dyn = ph.vaddr;
        } else if (ph.type == PT_ARM_EXIDX) {
            m->exidx = base + ph.vaddr;
            m->exidx_count = ph.memsz / 8;
        }
    }
    m->size = hi;
    if (!dyn) fatal("%s: no PT_DYNAMIC", name.c_str());

    for (u32 d = base + dyn;; d += 8) {
        u32 tag = rd32(d), val = rd32(d + 4);
        if (tag == DT_NULL) break;
        switch (tag) {
        case DT_HASH: m->hash = base + val; break;
        case DT_STRTAB: m->strtab = base + val; break;
        case DT_SYMTAB: m->symtab = base + val; break;
        case DT_REL: m->rel = base + val; break;
        case DT_RELSZ: m->relsz = val; break;
        case DT_JMPREL: m->jmprel = base + val; break;
        case DT_PLTRELSZ: m->pltrelsz = val; break;
        case DT_INIT_ARRAY: m->init_array = base + val; break;
        case DT_INIT_ARRAYSZ: m->init_array_sz = val; break;
        case DT_INIT: m->init_fn = base + val; break;
        default: break;
        }
    }
    if (!m->hash || !m->symtab || !m->strtab) fatal("%s: missing symbol tables", name.c_str());
    m->nbucket = rd32(m->hash);
    m->nchain = rd32(m->hash + 4);
    g_modules.push_back(m);
    LOGI("loaded %s at %08x (size %08x, exidx %u entries)", name.c_str(), base, m->size, m->exidx_count);
    return m;
}

u32 module_symbol(const Module& m, const char* name) {
    u32 h = elf_hash(name);
    for (u32 i = rd32(m.hash + 8 + 4 * (h % m.nbucket)); i; i = rd32(m.hash + 8 + 4 * (m.nbucket + i))) {
        Sym s = sym_at(m, i);
        if (s.shndx != 0 && std::strcmp(HS(m.strtab + s.name), name) == 0) return m.base + s.value;
    }
    return 0;
}

void link_module(Module& m, const SymbolResolver& resolve) {
    int unresolved = 0;
    auto apply = [&](u32 table, u32 bytes) {
        for (u32 r = table; r < table + bytes; r += 8) {
            u32 off = rd32(r), info = rd32(r + 4);
            u32 type = info & 0xFF, symi = info >> 8;
            u32 where = m.base + off;
            if (type == R_ARM_RELATIVE) {
                wr32(where, rd32(where) + m.base);
                continue;
            }
            u32 s = 0;
            if (symi) {
                Sym sym = sym_at(m, symi);
                const char* nm = HS(m.strtab + sym.name);
                if (sym.shndx != 0) {
                    s = m.base + sym.value;
                } else {
                    bool is_func = (sym.info & 0xF) == 2 || type == R_ARM_JUMP_SLOT;
                    s = resolve(nm, is_func);
                    if (!s && (sym.info >> 4) != 2 /* STB_WEAK */) {
                        LOGE("%s: unresolved symbol %s", m.name.c_str(), nm);
                        ++unresolved;
                    }
                }
            }
            switch (type) {
            case R_ARM_ABS32: wr32(where, rd32(where) + s); break;
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT: wr32(where, s); break;
            default: fatal("%s: unsupported relocation type %u at %08x", m.name.c_str(), type, off);
            }
        }
    };
    if (m.rel) apply(m.rel, m.relsz);
    if (m.jmprel) apply(m.jmprel, m.pltrelsz);
    if (unresolved) fatal("%s: %d unresolved symbols", m.name.c_str(), unresolved);
}

void init_module(Module& m) {
    if (m.init_fn) call_guest(m.init_fn);
    for (u32 i = 0; i < m.init_array_sz / 4; ++i) {
        u32 f = rd32(m.init_array + 4 * i);
        if (f != 0 && f != 0xFFFFFFFFu) call_guest(f);
    }
    LOGI("%s: ran %u constructors", m.name.c_str(), m.init_array_sz / 4);
}

const Module* module_for_address(u32 a) {
    for (const Module* m : g_modules)
        if (m->contains(a)) return m;
    return nullptr;
}

}  // namespace compat

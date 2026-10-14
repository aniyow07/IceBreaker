#pragma once
#include "common.h"

namespace compat {

// Fixed guest address-space layout (all inside one 4 GiB host reservation).
constexpr u32 kStubBase = 0x00010000;   // SVC trampolines for imported functions
constexpr u32 kStubEnd = 0x00100000;
constexpr u32 kDataBase = 0x00100000;   // static data owned by the compat layer
constexpr u32 kDataEnd = 0x01000000;
constexpr u32 kLibBase = 0x01000000;    // loaded ELF images
constexpr u32 kLibEnd = 0x10000000;
constexpr u32 kHeapBase = 0x10000000;   // guest malloc heap
constexpr u32 kHeapEnd = 0xE0000000;
constexpr u32 kStackBase = 0xE0000000;  // per-host-thread guest stacks
constexpr u32 kStackEnd = 0xF0000000;
constexpr u32 kStackSize = 2u << 20;

bool mem_init();
void mem_commit(u32 addr, u32 size);   // make [addr, addr+size) readable/writable
bool mem_is_committed(u32 addr);       // debug check (slow)

// Bump allocation of permanent compat-layer data (never freed).
u32 data_alloc(u32 size, u32 align = 8);
u32 data_strdup(const char* s);

// Guest heap, used for the guest's malloc/free/realloc and for objects the
// compat layer hands to guest code that the guest may free.
u32 gmalloc(u32 size);
u32 gcalloc(u32 n, u32 size);
u32 grealloc(u32 addr, u32 size);
void gfree(u32 addr);
u32 gusable(u32 addr);
u32 gstrdup(const char* s);

// Allocate a guest stack for the calling host thread; returns its top.
u32 stack_alloc();

}  // namespace compat

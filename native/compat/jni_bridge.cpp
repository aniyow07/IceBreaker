// Guest-facing JNI: the original libraries call through standard JNIEnv/JavaVM
// function tables; every slot is an SVC stub that forwards to the host JNI.
#include "jni_bridge.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include "cpu.h"
#include "guest_mem.h"
#include "hle.h"

namespace compat {

namespace {

JavaVM* g_host_vm = nullptr;
bool g_log_jni = std::getenv("COMPAT_LOG_JNI") != nullptr;
thread_local JNIEnv* t_env = nullptr;
u32 g_guest_vm = 0, g_guest_env = 0;
u32 g_env_first_svc = 0, g_vm_first_svc = 0;
constexpr u32 kEnvSlots = 233, kVmSlots = 8;

// ---- handles ------------------------------------------------------------------------------
struct HandleEntry {
    jobject ref = nullptr;
    bool local = false;
    u32 gen = 0;
};
std::mutex g_h_mu;
std::vector<HandleEntry> g_handles(1);
std::vector<u32> g_free_handles;
struct LocalRec {
    u32 handle, gen;
};
thread_local std::vector<LocalRec> t_locals;

u32 handle_index(u32 h) { return h >> 2; }

u32 alloc_handle(jobject global, bool local) {
    std::lock_guard<std::mutex> lk(g_h_mu);
    u32 idx;
    if (!g_free_handles.empty()) { idx = g_free_handles.back(); g_free_handles.pop_back(); }
    else { idx = static_cast<u32>(g_handles.size()); g_handles.emplace_back(); }
    HandleEntry& e = g_handles[idx];
    e.ref = global;
    e.local = local;
    ++e.gen;
    u32 h = idx << 2;
    if (local) t_locals.push_back({h, e.gen});
    return h;
}

void release_handle(u32 h, u32 expect_gen = 0) {
    jobject ref = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_h_mu);
        u32 idx = handle_index(h);
        if (!idx || idx >= g_handles.size()) return;
        HandleEntry& e = g_handles[idx];
        if (!e.ref || (expect_gen && e.gen != expect_gen)) return;
        ref = e.ref;
        e.ref = nullptr;
        g_free_handles.push_back(idx);
    }
    if (JNIEnv* env = host_env()) env->DeleteGlobalRef(ref);
}

// ---- method / field ids --------------------------------------------------------------------
struct MemberInfo {
    void* id;
    std::string sig;   // full JNI signature
    char ret;          // return type character
    std::string params;  // one char per parameter
    std::string name;
};
std::mutex g_id_mu;
std::vector<MemberInfo> g_members(1);
std::map<std::pair<void*, std::string>, u32> g_member_index;

std::string param_types(const std::string& sig) {
    std::string out;
    size_t i = sig.find('(') + 1;
    while (i < sig.size() && sig[i] != ')') {
        char ch = sig[i];
        if (ch == '[') {
            while (sig[i] == '[') ++i;
            if (sig[i] == 'L') i = sig.find(';', i);
            out += 'L';
            ++i;
        } else if (ch == 'L') {
            i = sig.find(';', i) + 1;
            out += 'L';
        } else {
            out += ch;
            ++i;
        }
    }
    return out;
}

u32 member_handle(void* id, const std::string& sig, const char* name = "") {
    if (!id) return 0;
    std::lock_guard<std::mutex> lk(g_id_mu);
    auto key = std::make_pair(id, sig);
    auto it = g_member_index.find(key);
    if (it != g_member_index.end()) return it->second;
    MemberInfo mi;
    mi.id = id;
    mi.sig = sig;
    mi.name = name;
    size_t rp = sig.find(')');
    mi.ret = rp == std::string::npos ? sig[0] : sig[rp + 1];
    if (mi.ret == '[') mi.ret = 'L';
    mi.params = rp == std::string::npos ? "" : param_types(sig);
    u32 h = static_cast<u32>(g_members.size()) << 2 | 2;
    g_members.push_back(mi);
    g_member_index[key] = h;
    return h;
}

const MemberInfo* member(u32 h) {
    std::lock_guard<std::mutex> lk(g_id_mu);
    u32 i = h >> 2;
    return (i && i < g_members.size()) ? &g_members[i] : nullptr;
}

// ---- argument marshalling --------------------------------------------------------------------
enum class ArgMode { Varargs, VaList, Array };

std::vector<jvalue> read_args(const MemberInfo& mi, ArgMode mode, Ctx& c, int slot) {
    std::vector<jvalue> out(mi.params.size());
    if (mode == ArgMode::Array) {
        u32 arr = c.arg(slot);
        for (size_t i = 0; i < mi.params.size(); ++i) {
            u32 a = arr + 8 * static_cast<u32>(i);
            jvalue& v = out[i];
            switch (mi.params[i]) {
            case 'Z': v.z = static_cast<jboolean>(*HP<u8>(a)); break;
            case 'B': v.b = static_cast<jbyte>(*HP<u8>(a)); break;
            case 'C': v.c = rd16(a); break;
            case 'S': v.s = static_cast<jshort>(rd16(a)); break;
            case 'I': v.i = static_cast<jint>(rd32(a)); break;
            case 'F': v.f = f32_from(rd32(a)); break;
            case 'J': v.j = static_cast<jlong>(rd64(a)); break;
            case 'D': { u64 b = rd64(a); std::memcpy(&v.d, &b, 8); break; }
            default: v.l = to_host(rd32(a)); break;
            }
        }
        return out;
    }
    VaReader va = mode == ArgMode::Varargs ? VaReader(c, slot) : VaReader(c.arg(slot));
    for (size_t i = 0; i < mi.params.size(); ++i) {
        jvalue& v = out[i];
        switch (mi.params[i]) {
        case 'Z': v.z = static_cast<jboolean>(va.next32()); break;
        case 'B': v.b = static_cast<jbyte>(va.next32()); break;
        case 'C': v.c = static_cast<jchar>(va.next32()); break;
        case 'S': v.s = static_cast<jshort>(va.next32()); break;
        case 'I': v.i = static_cast<jint>(va.next32()); break;
        case 'F': v.f = static_cast<jfloat>(va.nextd()); break;  // promoted to double
        case 'J': v.j = static_cast<jlong>(va.next64()); break;
        case 'D': v.d = va.nextd(); break;
        default: v.l = to_host(va.next32()); break;
        }
    }
    return out;
}

void set_return(Ctx& c, char t, const jvalue& v) {
    switch (t) {
    case 'Z': c.ret(v.z); break;
    case 'B': c.ret(static_cast<u32>(static_cast<s32>(v.b))); break;
    case 'C': c.ret(v.c); break;
    case 'S': c.ret(static_cast<u32>(static_cast<s32>(v.s))); break;
    case 'I': c.ret(static_cast<u32>(v.i)); break;
    case 'F': c.retf(v.f); break;
    case 'J': c.ret64(static_cast<u64>(v.j)); break;
    case 'D': c.retd(v.d); break;
    case 'V': c.ret(0); break;
    default: c.ret(to_guest(v.l)); break;
    }
}

const char kCallTypes[] = "LZBCSIJFDV";  // order of Call<Type>Method families

enum class CallKind { Virtual, Nonvirtual, Static };

void do_call(Ctx& c, CallKind kind, int type_index, ArgMode mode) {
    JNIEnv* env = host_env();
    int mid_slot = kind == CallKind::Nonvirtual ? 3 : 2;
    const MemberInfo* mi = member(c.arg(mid_slot));
    if (!mi) { LOGE("JNI call with invalid method id %08x", c.arg(mid_slot)); c.ret(0); return; }
    std::vector<jvalue> args = read_args(*mi, mode, c, mid_slot + 1);
    // The original Cocos2dxActivity.showPreloadCicle adds a full-size software-layer WebView at the
    // screen centre to show assets/prelo.gif during loads. On Android 16 (Galaxy S23) that WebView is
    // drawn as an opaque black rectangle over the lower-right of the screen until it is removed
    // (screen recording tests/device-s23/arm64/blackscreen, ~32.6 s). Loads now finish in well under a
    // second, so the spinner itself was never visible; the call is skipped to avoid the black box.
    if (kind == CallKind::Static && mi->name == "showPreloadCicle") {
        static bool logged = false;
        if (!logged) { LOGI("skipping Cocos2dxActivity.showPreloadCicle (black WebView box on modern Android)"); logged = true; }
        c.ret(0);
        return;
    }
    if (kind == CallKind::Static && mi->name == "setAnimationInterval" && args.size() == 1)
        g_animation_interval_ms.store(args[0].d * 1000.0);
    jobject target = to_host(c.arg(1));
    auto mid = static_cast<jmethodID>(mi->id);
    jclass cls = kind == CallKind::Nonvirtual ? static_cast<jclass>(to_host(c.arg(2))) : nullptr;
    jvalue r{};
    char t = kCallTypes[type_index];
    const jvalue* a = args.data();
#define CALL3(Type, field)                                                                                \
    if (kind == CallKind::Static) r.field = env->CallStatic##Type##MethodA(static_cast<jclass>(target), mid, a); \
    else if (kind == CallKind::Virtual) r.field = env->Call##Type##MethodA(target, mid, a);              \
    else r.field = env->CallNonvirtual##Type##MethodA(target, cls, mid, a);
    switch (t) {
    case 'L': { CALL3(Object, l) break; }
    case 'Z': { CALL3(Boolean, z) break; }
    case 'B': { CALL3(Byte, b) break; }
    case 'C': { CALL3(Char, c) break; }
    case 'S': { CALL3(Short, s) break; }
    case 'I': { CALL3(Int, i) break; }
    case 'J': { CALL3(Long, j) break; }
    case 'F': { CALL3(Float, f) break; }
    case 'D': { CALL3(Double, d) break; }
    case 'V':
        if (kind == CallKind::Static) env->CallStaticVoidMethodA(static_cast<jclass>(target), mid, a);
        else if (kind == CallKind::Virtual) env->CallVoidMethodA(target, mid, a);
        else env->CallNonvirtualVoidMethodA(target, cls, mid, a);
        break;
    }
#undef CALL3
    set_return(c, t, r);
}

const char kFieldTypes[] = "LZBCSIJFD";

void do_get_field(Ctx& c, bool is_static, int type_index) {
    JNIEnv* env = host_env();
    const MemberInfo* fi = member(c.arg(2));
    if (!fi) { c.ret(0); return; }
    jobject o = to_host(c.arg(1));
    auto fid = static_cast<jfieldID>(fi->id);
    jvalue r{};
    char t = kFieldTypes[type_index];
#define GETF(Type, field)                                                                \
    r.field = is_static ? env->GetStatic##Type##Field(static_cast<jclass>(o), fid) : env->Get##Type##Field(o, fid);
    switch (t) {
    case 'L': GETF(Object, l) break;
    case 'Z': GETF(Boolean, z) break;
    case 'B': GETF(Byte, b) break;
    case 'C': GETF(Char, c) break;
    case 'S': GETF(Short, s) break;
    case 'I': GETF(Int, i) break;
    case 'J': GETF(Long, j) break;
    case 'F': GETF(Float, f) break;
    case 'D': GETF(Double, d) break;
    }
#undef GETF
    set_return(c, t, r);
}

void do_set_field(Ctx& c, bool is_static, int type_index) {
    JNIEnv* env = host_env();
    const MemberInfo* fi = member(c.arg(2));
    if (!fi) return;
    jobject o = to_host(c.arg(1));
    auto fid = static_cast<jfieldID>(fi->id);
    char t = kFieldTypes[type_index];
    // The value is the 4th argument; 64-bit values occupy the aligned pair (r2? no: slots 4,5 after alignment).
    jvalue v{};
    switch (t) {
    case 'J': v.j = static_cast<jlong>(c.arg64(4)); break;
    case 'D': v.d = c.argd(4); break;
    case 'F': v.f = c.argf(3); break;
    case 'L': v.l = to_host(c.arg(3)); break;
    default: v.i = static_cast<jint>(c.arg(3)); break;
    }
#define SETF(Type, field)                                                                    \
    if (is_static) env->SetStatic##Type##Field(static_cast<jclass>(o), fid, v.field); else env->Set##Type##Field(o, fid, v.field);
    switch (t) {
    case 'L': SETF(Object, l) break;
    case 'Z': SETF(Boolean, z) break;
    case 'B': SETF(Byte, b) break;
    case 'C': SETF(Char, c) break;
    case 'S': SETF(Short, s) break;
    case 'I': SETF(Int, i) break;
    case 'J': SETF(Long, j) break;
    case 'F': SETF(Float, f) break;
    case 'D': SETF(Double, d) break;
    }
#undef SETF
}

// ---- primitive arrays ---------------------------------------------------------------------------
const char kArrayTypes[] = "ZBCSIJFD";
u32 elem_size(char t) {
    switch (t) {
    case 'Z': case 'B': return 1;
    case 'C': case 'S': return 2;
    case 'J': case 'D': return 8;
    default: return 4;
    }
}

void array_region(JNIEnv* env, char t, jarray arr, jsize start, jsize len, void* buf, bool get) {
#define REGION(Type, ctype)                                                                                 \
    if (get) env->Get##Type##ArrayRegion(static_cast<ctype##Array>(arr), start, len, static_cast<ctype*>(buf)); \
    else env->Set##Type##ArrayRegion(static_cast<ctype##Array>(arr), start, len, static_cast<const ctype*>(buf));
    switch (t) {
    case 'Z': REGION(Boolean, jboolean) break;
    case 'B': REGION(Byte, jbyte) break;
    case 'C': REGION(Char, jchar) break;
    case 'S': REGION(Short, jshort) break;
    case 'I': REGION(Int, jint) break;
    case 'J': REGION(Long, jlong) break;
    case 'F': REGION(Float, jfloat) break;
    case 'D': REGION(Double, jdouble) break;
    }
#undef REGION
}

char array_type_of(JNIEnv* env, jarray arr) {
    static const char* names[] = {"[Z", "[B", "[C", "[S", "[I", "[J", "[F", "[D"};
    for (int i = 0; i < 8; ++i) {
        jclass k = env->FindClass(names[i]);
        bool is = k && env->IsInstanceOf(arr, k);
        if (k) env->DeleteLocalRef(k);
        if (is) return kArrayTypes[i];
    }
    return 'B';
}

struct PinnedArray {
    u32 handle;
    char type;
};
std::mutex g_pin_mu;
std::unordered_map<u32, PinnedArray> g_pinned;  // guest buffer -> source array

u32 pin_array(Ctx& c, char t) {
    JNIEnv* env = host_env();
    auto arr = static_cast<jarray>(to_host(c.arg(1)));
    if (!arr) return 0;
    jsize len = env->GetArrayLength(arr);
    u32 buf = gmalloc(static_cast<u32>(len) * elem_size(t) + 1);
    array_region(env, t, arr, 0, len, H(buf), true);
    if (c.arg(2)) *HP<u8>(c.arg(2)) = 1;
    std::lock_guard<std::mutex> lk(g_pin_mu);
    g_pinned[buf] = {c.arg(1), t};
    return buf;
}

void unpin_array(Ctx& c) {
    u32 buf = c.arg(2), mode = c.arg(3);
    PinnedArray p{};
    {
        std::lock_guard<std::mutex> lk(g_pin_mu);
        auto it = g_pinned.find(buf);
        if (it == g_pinned.end()) return;
        p = it->second;
        if (mode != 1 /* JNI_COMMIT */) g_pinned.erase(it);
    }
    JNIEnv* env = host_env();
    auto arr = static_cast<jarray>(to_host(c.arg(1)));
    if (mode != 2 /* JNI_ABORT */ && arr) array_region(env, p.type, arr, 0, env->GetArrayLength(arr), H(buf), false);
    if (mode != 1) gfree(buf);
}

// ---- the JNIEnv dispatcher ------------------------------------------------------------------------
std::string guest_sig(u32 a) { return a ? HS(a) : ""; }

void env_dispatch(Ctx& c) {
    const u32 idx = c.svc - g_env_first_svc;
    JNIEnv* env = host_env();
    if (!env) fatal("guest JNI call %u on a thread without a JNIEnv", idx);

    if (idx >= 34 && idx <= 63) { do_call(c, CallKind::Virtual, static_cast<int>((idx - 34) / 3), static_cast<ArgMode>((idx - 34) % 3)); return; }
    if (idx >= 64 && idx <= 93) { do_call(c, CallKind::Nonvirtual, static_cast<int>((idx - 64) / 3), static_cast<ArgMode>((idx - 64) % 3)); return; }
    if (idx >= 114 && idx <= 143) { do_call(c, CallKind::Static, static_cast<int>((idx - 114) / 3), static_cast<ArgMode>((idx - 114) % 3)); return; }
    if (idx >= 95 && idx <= 103) { do_get_field(c, false, static_cast<int>(idx - 95)); return; }
    if (idx >= 104 && idx <= 112) { do_set_field(c, false, static_cast<int>(idx - 104)); return; }
    if (idx >= 145 && idx <= 153) { do_get_field(c, true, static_cast<int>(idx - 145)); return; }
    if (idx >= 154 && idx <= 162) { do_set_field(c, true, static_cast<int>(idx - 154)); return; }
    if (idx >= 175 && idx <= 182) {
        char t = kArrayTypes[idx - 175];
        jsize n = static_cast<jsize>(c.arg(1));
        jarray a = nullptr;
        switch (t) {
        case 'Z': a = env->NewBooleanArray(n); break;
        case 'B': a = env->NewByteArray(n); break;
        case 'C': a = env->NewCharArray(n); break;
        case 'S': a = env->NewShortArray(n); break;
        case 'I': a = env->NewIntArray(n); break;
        case 'J': a = env->NewLongArray(n); break;
        case 'F': a = env->NewFloatArray(n); break;
        case 'D': a = env->NewDoubleArray(n); break;
        }
        c.ret(to_guest(a));
        return;
    }
    if (idx >= 183 && idx <= 190) { c.ret(pin_array(c, kArrayTypes[idx - 183])); return; }
    if (idx >= 191 && idx <= 198) { unpin_array(c); return; }
    if (idx >= 199 && idx <= 214) {
        bool get = idx <= 206;
        char t = kArrayTypes[get ? idx - 199 : idx - 207];
        array_region(env, t, static_cast<jarray>(to_host(c.arg(1))), static_cast<jsize>(c.arg(2)),
                     static_cast<jsize>(c.arg(3)), H(c.arg(4)), get);
        return;
    }

    switch (idx) {
    case 4: c.ret(static_cast<u32>(env->GetVersion())); return;
    case 6: {
        jclass k = env->FindClass(HS(c.arg(1)));
        if (!k) { env->ExceptionClear(); LOGW("JNI FindClass failed: %s", HS(c.arg(1))); }
        else if (g_log_jni) LOGI("JNI FindClass %s", HS(c.arg(1)));
        c.ret(to_guest(k));
        return;
    }
    case 10: c.ret(to_guest(env->GetSuperclass(static_cast<jclass>(to_host(c.arg(1)))))); return;
    case 11: c.ret(env->IsAssignableFrom(static_cast<jclass>(to_host(c.arg(1))), static_cast<jclass>(to_host(c.arg(2))))); return;
    case 13: c.ret(static_cast<u32>(env->Throw(static_cast<jthrowable>(to_host(c.arg(1)))))); return;
    case 14: c.ret(static_cast<u32>(env->ThrowNew(static_cast<jclass>(to_host(c.arg(1))), HS(c.arg(2))))); return;
    case 15: c.ret(to_guest(env->ExceptionOccurred())); return;
    case 16: env->ExceptionDescribe(); return;
    case 17: env->ExceptionClear(); return;
    case 18: fatal("guest JNI FatalError: %s", HS(c.arg(1)));
    case 19: c.ret(0); return;                 // PushLocalFrame
    case 20: c.ret(c.arg(1)); return;          // PopLocalFrame(result)
    case 21: {                                 // NewGlobalRef
        jobject o = to_host(c.arg(1));
        c.ret(o ? alloc_handle(env->NewGlobalRef(o), false) : 0);
        return;
    }
    case 22: case 23: release_handle(c.arg(1)); return;  // DeleteGlobalRef / DeleteLocalRef
    case 24: c.ret(env->IsSameObject(to_host(c.arg(1)), to_host(c.arg(2)))); return;
    case 25: { jobject o = to_host(c.arg(1)); c.ret(o ? alloc_handle(env->NewGlobalRef(o), true) : 0); return; }
    case 26: c.ret(0); return;                 // EnsureLocalCapacity
    case 27: c.ret(to_guest(env->AllocObject(static_cast<jclass>(to_host(c.arg(1)))))); return;
    case 28: case 29: case 30: {               // NewObject / V / A
        const MemberInfo* mi = member(c.arg(2));
        if (!mi) { c.ret(0); return; }
        std::vector<jvalue> args = read_args(*mi, static_cast<ArgMode>(idx - 28), c, 3);
        c.ret(to_guest(env->NewObjectA(static_cast<jclass>(to_host(c.arg(1))), static_cast<jmethodID>(mi->id), args.data())));
        return;
    }
    case 31: c.ret(to_guest(env->GetObjectClass(to_host(c.arg(1))))); return;
    case 32: c.ret(env->IsInstanceOf(to_host(c.arg(1)), static_cast<jclass>(to_host(c.arg(2))))); return;
    case 33: case 113: case 94: case 144: {    // Get(Static)MethodID / Get(Static)FieldID
        auto cls = static_cast<jclass>(to_host(c.arg(1)));
        const char* name = HS(c.arg(2));
        std::string sig = guest_sig(c.arg(3));
        void* id = nullptr;
        if (idx == 33) id = env->GetMethodID(cls, name, sig.c_str());
        else if (idx == 113) id = env->GetStaticMethodID(cls, name, sig.c_str());
        else if (idx == 94) id = env->GetFieldID(cls, name, sig.c_str());
        else id = env->GetStaticFieldID(cls, name, sig.c_str());
        if (g_log_jni && id) LOGI("JNI id %s %s", name, sig.c_str());
        if (!id) {
            // A failed lookup leaves NoSuchMethodError/NoSuchFieldError pending; the game only checks
            // for a null id, so clear it rather than let the next JNI call run with an exception pending.
            env->ExceptionClear();
            LOGW("JNI lookup failed: %s %s", name, sig.c_str());
        }
        c.ret(member_handle(id, sig, name));
        return;
    }
    case 163: {                                // NewString(const jchar*, len)
        c.ret(to_guest(env->NewString(HP<jchar>(c.arg(1)), static_cast<jsize>(c.arg(2)))));
        return;
    }
    case 164: c.ret(static_cast<u32>(env->GetStringLength(static_cast<jstring>(to_host(c.arg(1)))))); return;
    case 165: {                                // GetStringChars
        auto s = static_cast<jstring>(to_host(c.arg(1)));
        jsize n = env->GetStringLength(s);
        u32 buf = gmalloc(2 * static_cast<u32>(n) + 2);
        env->GetStringRegion(s, 0, n, HP<jchar>(buf));
        wr16(buf + 2 * static_cast<u32>(n), 0);
        if (c.arg(2)) *HP<u8>(c.arg(2)) = 1;
        c.ret(buf);
        return;
    }
    case 166: case 170: case 225: gfree(c.arg(2)); return;  // ReleaseStringChars / UTFChars / Critical
    case 167: c.ret(to_guest(env->NewStringUTF(HS(c.arg(1))))); return;
    case 168: c.ret(static_cast<u32>(env->GetStringUTFLength(static_cast<jstring>(to_host(c.arg(1)))))); return;
    case 169: {                                // GetStringUTFChars
        auto s = static_cast<jstring>(to_host(c.arg(1)));
        if (!s) { c.ret(0); return; }
        const char* chars = env->GetStringUTFChars(s, nullptr);
        u32 g = gstrdup(chars ? chars : "");
        if (chars) env->ReleaseStringUTFChars(s, chars);
        if (c.arg(2)) *HP<u8>(c.arg(2)) = 1;
        c.ret(g);
        return;
    }
    case 171: c.ret(static_cast<u32>(env->GetArrayLength(static_cast<jarray>(to_host(c.arg(1)))))); return;
    case 172: {
        c.ret(to_guest(env->NewObjectArray(static_cast<jsize>(c.arg(1)), static_cast<jclass>(to_host(c.arg(2))), to_host(c.arg(3)))));
        return;
    }
    case 173: c.ret(to_guest(env->GetObjectArrayElement(static_cast<jobjectArray>(to_host(c.arg(1))), static_cast<jsize>(c.arg(2))))); return;
    case 174: env->SetObjectArrayElement(static_cast<jobjectArray>(to_host(c.arg(1))), static_cast<jsize>(c.arg(2)), to_host(c.arg(3))); return;
    case 215: LOGW("guest RegisterNatives ignored"); c.ret(0); return;
    case 216: c.ret(0); return;
    case 217: c.ret(static_cast<u32>(env->MonitorEnter(to_host(c.arg(1))))); return;
    case 218: c.ret(static_cast<u32>(env->MonitorExit(to_host(c.arg(1))))); return;
    case 219: wr32(c.arg(1), g_guest_vm); c.ret(0); return;
    case 220: env->GetStringRegion(static_cast<jstring>(to_host(c.arg(1))), static_cast<jsize>(c.arg(2)), static_cast<jsize>(c.arg(3)), HP<jchar>(c.arg(4))); return;
    case 221: env->GetStringUTFRegion(static_cast<jstring>(to_host(c.arg(1))), static_cast<jsize>(c.arg(2)), static_cast<jsize>(c.arg(3)), HP<char>(c.arg(4))); return;
    case 222: {                                // GetPrimitiveArrayCritical
        auto arr = static_cast<jarray>(to_host(c.arg(1)));
        c.ret(arr ? pin_array(c, array_type_of(env, arr)) : 0);
        return;
    }
    case 223: unpin_array(c); return;
    case 224: {                                // GetStringCritical -> copy like GetStringChars
        auto s = static_cast<jstring>(to_host(c.arg(1)));
        jsize n = env->GetStringLength(s);
        u32 buf = gmalloc(2 * static_cast<u32>(n) + 2);
        env->GetStringRegion(s, 0, n, HP<jchar>(buf));
        c.ret(buf);
        return;
    }
    case 226: { jobject o = to_host(c.arg(1)); c.ret(o ? alloc_handle(env->NewGlobalRef(o), false) : 0); return; }
    case 227: release_handle(c.arg(1)); return;
    case 228: c.ret(env->ExceptionCheck()); return;
    case 229: case 230: case 231: c.ret(0); return;
    case 232: c.ret(1); return;
    default:
        LOGE("guest used unsupported JNI function #%u", idx);
        c.ret(0);
        return;
    }
}

void vm_dispatch(Ctx& c) {
    const u32 idx = c.svc - g_vm_first_svc;
    switch (idx) {
    case 3: c.ret(0); return;  // DestroyJavaVM
    case 4: case 7: {          // AttachCurrentThread(AsDaemon)
        if (!t_env && g_host_vm) {
            JNIEnv* e = nullptr;
#if defined(__ANDROID__)
            g_host_vm->AttachCurrentThread(&e, nullptr);
#else
            g_host_vm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
#endif
            t_env = e;
        }
        if (c.arg(1)) wr32(c.arg(1), g_guest_env);
        c.ret(t_env ? 0 : 0xFFFFFFFFu);
        return;
    }
    case 5: c.ret(0); return;  // DetachCurrentThread: the host thread stays attached
    case 6: {                  // GetEnv
        JNIEnv* e = host_env();
        if (!e) { c.ret(static_cast<u32>(JNI_EDETACHED)); return; }
        wr32(c.arg(1), g_guest_env);
        c.ret(0);
        return;
    }
    default:
        LOGE("guest used unsupported JavaVM function #%u", idx);
        c.ret(0xFFFFFFFFu);
    }
}

}  // namespace

std::atomic<double> g_animation_interval_ms{0.0};

JNIEnv* host_env() {
    if (!t_env && g_host_vm) {
        JNIEnv* e = nullptr;
        if (g_host_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) t_env = e;
    }
    return t_env;
}

u32 to_guest(jobject o) {
    if (!o) return 0;
    JNIEnv* env = host_env();
    jobject g = env->NewGlobalRef(o);
    env->DeleteLocalRef(o);
    return alloc_handle(g, true);
}

jobject to_host(u32 h) {
    if (!h) return nullptr;
    std::lock_guard<std::mutex> lk(g_h_mu);
    u32 idx = handle_index(h);
    if (!idx || idx >= g_handles.size() || !g_handles[idx].ref) {
        LOGE("guest used invalid JNI handle %08x", h);
        return nullptr;
    }
    return g_handles[idx].ref;
}

NativeFrame::NativeFrame(JNIEnv* env) : prev_env_(t_env), mark_(t_locals.size()) { t_env = env; }

NativeFrame::~NativeFrame() {
    while (t_locals.size() > mark_) {
        LocalRec r = t_locals.back();
        t_locals.pop_back();
        bool still_local;
        {
            std::lock_guard<std::mutex> lk(g_h_mu);
            u32 idx = handle_index(r.handle);
            still_local = idx < g_handles.size() && g_handles[idx].ref && g_handles[idx].gen == r.gen && g_handles[idx].local;
        }
        if (still_local) release_handle(r.handle, r.gen);
    }
    t_env = prev_env_;
}

void jni_init(JavaVM* vm) {
    g_host_vm = vm;
    if (g_guest_env) return;
    u32 env_table = data_alloc(4 * kEnvSlots);
    for (u32 i = 0; i < kEnvSlots; ++i) {
        u32 stub = make_stub("JNIEnv", env_dispatch);
        if (i == 0) g_env_first_svc = rd32(stub) & 0xFFFFFF;
        wr32(env_table + 4 * i, stub);
    }
    g_guest_env = data_alloc(4);
    wr32(g_guest_env, env_table);

    u32 vm_table = data_alloc(4 * kVmSlots);
    for (u32 i = 0; i < kVmSlots; ++i) {
        u32 stub = make_stub("JavaVM", vm_dispatch);
        if (i == 0) g_vm_first_svc = rd32(stub) & 0xFFFFFF;
        wr32(vm_table + 4 * i, stub);
    }
    g_guest_vm = data_alloc(4);
    wr32(g_guest_vm, vm_table);
}

u32 guest_vm() { return g_guest_vm; }
u32 guest_env() { return g_guest_env; }

u64 call_guest_args(u32 fn, const GuestArgs& a) { return call_guest_v(fn, a.slots().data(), a.slots().size()); }

}  // namespace compat

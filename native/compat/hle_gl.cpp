// OpenGL ES 1.1 imports: arguments are unpacked from the guest's soft-float registers,
// guest pointers are translated, and the call goes to the host GL table.
#include <map>
#include <mutex>

#include "gl_api.h"
#include "guest_mem.h"
#include "hle.h"

namespace compat {

GlApi g_gl;

namespace {

constexpr GLenum GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893;
// Buffer bindings decide whether a pointer argument is a guest address or a buffer offset.
thread_local GLuint t_array_buffer = 0, t_element_buffer = 0;

const void* ptr_or_offset(u32 v, bool buffer_bound) {
    if (buffer_bound) return reinterpret_cast<const void*>(static_cast<uintptr_t>(v));
    return v ? H(v) : nullptr;
}
const void* gptr(u32 v) { return v ? H(v) : nullptr; }

void h_glAlphaFunc(Ctx& c) { g_gl.glAlphaFunc(c.arg(0), c.argf(1)); }
void h_glBindBuffer(Ctx& c) {
    if (c.arg(0) == GL_ARRAY_BUFFER) t_array_buffer = c.arg(1);
    if (c.arg(0) == GL_ELEMENT_ARRAY_BUFFER) t_element_buffer = c.arg(1);
    g_gl.glBindBuffer(c.arg(0), c.arg(1));
}
void h_glBindFramebufferOES(Ctx& c) { g_gl.glBindFramebufferOES(c.arg(0), c.arg(1)); }
void h_glBindTexture(Ctx& c) { g_gl.glBindTexture(c.arg(0), c.arg(1)); }
void h_glBlendFunc(Ctx& c) { g_gl.glBlendFunc(c.arg(0), c.arg(1)); }
void h_glBufferData(Ctx& c) {
    g_gl.glBufferData(c.arg(0), static_cast<s32>(c.arg(1)), gptr(c.arg(2)), c.arg(3));
}
void h_glBufferSubData(Ctx& c) {
    g_gl.glBufferSubData(c.arg(0), static_cast<s32>(c.arg(1)), static_cast<s32>(c.arg(2)), gptr(c.arg(3)));
}
void h_glCheckFramebufferStatusOES(Ctx& c) { c.ret(g_gl.glCheckFramebufferStatusOES(c.arg(0))); }
void h_glClear(Ctx& c) { g_gl.glClear(c.arg(0)); }
void h_glClearColor(Ctx& c) { g_gl.glClearColor(c.argf(0), c.argf(1), c.argf(2), c.argf(3)); }
void h_glClearDepthf(Ctx& c) { g_gl.glClearDepthf(c.argf(0)); }
void h_glColor4f(Ctx& c) { g_gl.glColor4f(c.argf(0), c.argf(1), c.argf(2), c.argf(3)); }
void h_glColor4ub(Ctx& c) {
    g_gl.glColor4ub(static_cast<GLubyte>(c.arg(0)), static_cast<GLubyte>(c.arg(1)), static_cast<GLubyte>(c.arg(2)),
                    static_cast<GLubyte>(c.arg(3)));
}
void h_glColorMask(Ctx& c) {
    g_gl.glColorMask(static_cast<GLboolean>(c.arg(0)), static_cast<GLboolean>(c.arg(1)),
                     static_cast<GLboolean>(c.arg(2)), static_cast<GLboolean>(c.arg(3)));
}
void h_glColorPointer(Ctx& c) {
    g_gl.glColorPointer(static_cast<GLint>(c.arg(0)), c.arg(1), static_cast<GLsizei>(c.arg(2)),
                        ptr_or_offset(c.arg(3), t_array_buffer != 0));
}
void h_glCompressedTexImage2D(Ctx& c) {
    g_gl.glCompressedTexImage2D(c.arg(0), static_cast<GLint>(c.arg(1)), c.arg(2), static_cast<GLsizei>(c.arg(3)),
                                static_cast<GLsizei>(c.arg(4)), static_cast<GLint>(c.arg(5)),
                                static_cast<GLsizei>(c.arg(6)), gptr(c.arg(7)));
}
void h_glDeleteBuffers(Ctx& c) { g_gl.glDeleteBuffers(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1))); }
void h_glDeleteFramebuffersOES(Ctx& c) {
    g_gl.glDeleteFramebuffersOES(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1)));
}
void h_glDeleteTextures(Ctx& c) { g_gl.glDeleteTextures(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1))); }
void h_glDepthFunc(Ctx& c) { g_gl.glDepthFunc(c.arg(0)); }
void h_glDisable(Ctx& c) { g_gl.glDisable(c.arg(0)); }
void h_glDisableClientState(Ctx& c) { g_gl.glDisableClientState(c.arg(0)); }
void h_glDrawArrays(Ctx& c) {
    g_gl.glDrawArrays(c.arg(0), static_cast<GLint>(c.arg(1)), static_cast<GLsizei>(c.arg(2)));
}
void h_glDrawElements(Ctx& c) {
    g_gl.glDrawElements(c.arg(0), static_cast<GLsizei>(c.arg(1)), c.arg(2),
                        ptr_or_offset(c.arg(3), t_element_buffer != 0));
}
void h_glEnable(Ctx& c) { g_gl.glEnable(c.arg(0)); }
void h_glEnableClientState(Ctx& c) { g_gl.glEnableClientState(c.arg(0)); }
void h_glFramebufferTexture2DOES(Ctx& c) {
    g_gl.glFramebufferTexture2DOES(c.arg(0), c.arg(1), c.arg(2), c.arg(3), static_cast<GLint>(c.arg(4)));
}
void h_glFrustumf(Ctx& c) { g_gl.glFrustumf(c.argf(0), c.argf(1), c.argf(2), c.argf(3), c.argf(4), c.argf(5)); }
void h_glGenBuffers(Ctx& c) { g_gl.glGenBuffers(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1))); }
void h_glGenFramebuffersOES(Ctx& c) { g_gl.glGenFramebuffersOES(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1))); }
void h_glGenTextures(Ctx& c) { g_gl.glGenTextures(static_cast<GLsizei>(c.arg(0)), HP<GLuint>(c.arg(1))); }
void h_glGenerateMipmapOES(Ctx& c) { g_gl.glGenerateMipmapOES(c.arg(0)); }
void h_glGetError(Ctx& c) { c.ret(g_gl.glGetError()); }
void h_glGetFloatv(Ctx& c) { g_gl.glGetFloatv(c.arg(0), HP<GLfloat>(c.arg(1))); }
void h_glGetIntegerv(Ctx& c) { g_gl.glGetIntegerv(c.arg(0), HP<GLint>(c.arg(1))); }

std::mutex g_str_mu;
std::map<u32, u32> g_strings;
void h_glGetString(Ctx& c) {
    std::lock_guard<std::mutex> lk(g_str_mu);
    auto it = g_strings.find(c.arg(0));
    if (it != g_strings.end()) { c.ret(it->second); return; }
    const GLubyte* s = g_gl.glGetString(c.arg(0));
    if (!s) { c.ret(0); return; }
    u32 g = data_strdup(reinterpret_cast<const char*>(s));
    g_strings[c.arg(0)] = g;
    c.ret(g);
}
void h_glLineWidth(Ctx& c) { g_gl.glLineWidth(c.argf(0)); }
void h_glLoadIdentity(Ctx&) { g_gl.glLoadIdentity(); }
void h_glMatrixMode(Ctx& c) { g_gl.glMatrixMode(c.arg(0)); }
void h_glMultMatrixf(Ctx& c) { g_gl.glMultMatrixf(HP<GLfloat>(c.arg(0))); }
void h_glOrthof(Ctx& c) { g_gl.glOrthof(c.argf(0), c.argf(1), c.argf(2), c.argf(3), c.argf(4), c.argf(5)); }
void h_glPixelStorei(Ctx& c) { g_gl.glPixelStorei(c.arg(0), static_cast<GLint>(c.arg(1))); }
void h_glPointSizePointerOES(Ctx& c) {
    if (g_gl.glPointSizePointerOES)
        g_gl.glPointSizePointerOES(c.arg(0), static_cast<GLsizei>(c.arg(1)), ptr_or_offset(c.arg(2), t_array_buffer != 0));
}
void h_glPopMatrix(Ctx&) { g_gl.glPopMatrix(); }
void h_glPushMatrix(Ctx&) { g_gl.glPushMatrix(); }
void h_glReadPixels(Ctx& c) {
    g_gl.glReadPixels(static_cast<GLint>(c.arg(0)), static_cast<GLint>(c.arg(1)), static_cast<GLsizei>(c.arg(2)),
                      static_cast<GLsizei>(c.arg(3)), c.arg(4), c.arg(5), H(c.arg(6)));
}
void h_glRotatef(Ctx& c) { g_gl.glRotatef(c.argf(0), c.argf(1), c.argf(2), c.argf(3)); }
void h_glScissor(Ctx& c) {
    g_gl.glScissor(static_cast<GLint>(c.arg(0)), static_cast<GLint>(c.arg(1)), static_cast<GLsizei>(c.arg(2)),
                   static_cast<GLsizei>(c.arg(3)));
}
void h_glTexCoordPointer(Ctx& c) {
    g_gl.glTexCoordPointer(static_cast<GLint>(c.arg(0)), c.arg(1), static_cast<GLsizei>(c.arg(2)),
                           ptr_or_offset(c.arg(3), t_array_buffer != 0));
}
void h_glTexEnvi(Ctx& c) { g_gl.glTexEnvi(c.arg(0), c.arg(1), static_cast<GLint>(c.arg(2))); }
void h_glTexImage2D(Ctx& c) {
    g_gl.glTexImage2D(c.arg(0), static_cast<GLint>(c.arg(1)), static_cast<GLint>(c.arg(2)),
                      static_cast<GLsizei>(c.arg(3)), static_cast<GLsizei>(c.arg(4)), static_cast<GLint>(c.arg(5)),
                      c.arg(6), c.arg(7), gptr(c.arg(8)));
}
void h_glTexParameteri(Ctx& c) { g_gl.glTexParameteri(c.arg(0), c.arg(1), static_cast<GLint>(c.arg(2))); }
void h_glTranslatef(Ctx& c) { g_gl.glTranslatef(c.argf(0), c.argf(1), c.argf(2)); }
void h_glVertexPointer(Ctx& c) {
    g_gl.glVertexPointer(static_cast<GLint>(c.arg(0)), c.arg(1), static_cast<GLsizei>(c.arg(2)),
                         ptr_or_offset(c.arg(3), t_array_buffer != 0));
}
void h_glViewport(Ctx& c) {
    g_gl.glViewport(static_cast<GLint>(c.arg(0)), static_cast<GLint>(c.arg(1)), static_cast<GLsizei>(c.arg(2)),
                    static_cast<GLsizei>(c.arg(3)));
}

}  // namespace

void hle_register_gl() {
#define COMPAT_GL_REGISTER(ret, name, params) hle_func(#name, h_##name);
    COMPAT_GL_FUNCS(COMPAT_GL_REGISTER)
#undef COMPAT_GL_REGISTER
}

}  // namespace compat

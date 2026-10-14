// PC harness: maps the GLES 1.1 entry points onto desktop OpenGL (compatibility
// profile) in a hidden window, rendering into an offscreen framebuffer that stands in
// for the Android window surface (guest framebuffer 0).
#include <windows.h>

#include <GL/gl.h>

#include "gl_api.h"
#include "harness.h"

namespace compat {

namespace {

HWND g_wnd;
HDC g_dc;
HGLRC g_rc;
GLuint g_fbo = 0, g_color_rb = 0, g_depth_rb = 0;
int g_w = 0, g_h = 0;

using PFN_BindBuffer = void(APIENTRY*)(GLenum, GLuint);
using PFN_BufferData = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFN_BufferSubData = void(APIENTRY*)(GLenum, GLintptr, GLsizeiptr, const void*);
using PFN_GenDel = void(APIENTRY*)(GLsizei, GLuint*);
using PFN_DelConst = void(APIENTRY*)(GLsizei, const GLuint*);
using PFN_BindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using PFN_CheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using PFN_FramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFN_GenerateMipmap = void(APIENTRY*)(GLenum);
using PFN_CompressedTexImage2D = void(APIENTRY*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
using PFN_GenRenderbuffers = void(APIENTRY*)(GLsizei, GLuint*);
using PFN_BindRenderbuffer = void(APIENTRY*)(GLenum, GLuint);
using PFN_RenderbufferStorage = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using PFN_FramebufferRenderbuffer = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);

PFN_BindFramebuffer p_BindFramebuffer;
PFN_GenDel p_GenFramebuffers;
PFN_DelConst p_DeleteFramebuffers;
PFN_CheckFramebufferStatus p_CheckFramebufferStatus;
PFN_FramebufferTexture2D p_FramebufferTexture2D;
PFN_GenerateMipmap p_GenerateMipmap;

template <typename T> T proc(const char* n) {
    PROC p = wglGetProcAddress(n);
    return reinterpret_cast<T>(p);
}

// GLES entry points that differ from desktop GL 1.x.
void GLAPIENTRY w_Orthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glOrtho(l, r, b, t, n, f); }
void GLAPIENTRY w_Frustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glFrustum(l, r, b, t, n, f); }
void GLAPIENTRY w_ClearDepthf(GLfloat d) { glClearDepth(d); }
void GLAPIENTRY w_BindFramebuffer(GLenum t, GLuint fb) { p_BindFramebuffer(t, fb ? fb : g_fbo); }
void GLAPIENTRY w_GetIntegerv(GLenum p, GLint* v) {
    glGetIntegerv(p, v);
    if (p == 0x8CA6 /* GL_FRAMEBUFFER_BINDING */ && *v == static_cast<GLint>(g_fbo)) *v = 0;
}

}  // namespace

bool harness_gl_create(int w, int h) {
    g_w = w;
    g_h = h;
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "IcebreakerHarness";
    RegisterClassA(&wc);
    g_wnd = CreateWindowA("IcebreakerHarness", "harness", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                          wc.hInstance, nullptr);
    g_dc = GetDC(g_wnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    SetPixelFormat(g_dc, ChoosePixelFormat(g_dc, &pfd), &pfd);
    g_rc = wglCreateContext(g_dc);
    if (!g_rc || !wglMakeCurrent(g_dc, g_rc)) return false;

    p_BindFramebuffer = proc<PFN_BindFramebuffer>("glBindFramebuffer");
    p_GenFramebuffers = proc<PFN_GenDel>("glGenFramebuffers");
    p_DeleteFramebuffers = proc<PFN_DelConst>("glDeleteFramebuffers");
    p_CheckFramebufferStatus = proc<PFN_CheckFramebufferStatus>("glCheckFramebufferStatus");
    p_FramebufferTexture2D = proc<PFN_FramebufferTexture2D>("glFramebufferTexture2D");
    p_GenerateMipmap = proc<PFN_GenerateMipmap>("glGenerateMipmap");
    auto genRb = proc<PFN_GenRenderbuffers>("glGenRenderbuffers");
    auto bindRb = proc<PFN_BindRenderbuffer>("glBindRenderbuffer");
    auto rbStorage = proc<PFN_RenderbufferStorage>("glRenderbufferStorage");
    auto fbRb = proc<PFN_FramebufferRenderbuffer>("glFramebufferRenderbuffer");
    if (!p_BindFramebuffer || !genRb) return false;

    p_GenFramebuffers(1, &g_fbo);
    p_BindFramebuffer(0x8D40, g_fbo);
    genRb(1, &g_color_rb);
    bindRb(0x8D41, g_color_rb);
    rbStorage(0x8D41, GL_RGBA8, w, h);
    fbRb(0x8D40, 0x8CE0, 0x8D41, g_color_rb);
    genRb(1, &g_depth_rb);
    bindRb(0x8D41, g_depth_rb);
    rbStorage(0x8D41, 0x81A6 /* GL_DEPTH_COMPONENT24 */, w, h);
    fbRb(0x8D40, 0x8D00, 0x8D41, g_depth_rb);
    return p_CheckFramebufferStatus(0x8D40) == 0x8CD5;
}

void harness_gl_read(std::vector<unsigned char>& rgba) {
    rgba.resize(static_cast<size_t>(g_w) * g_h * 4);
    GLint prev = 0;
    glGetIntegerv(0x8CA6, &prev);
    p_BindFramebuffer(0x8D40, g_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    p_BindFramebuffer(0x8D40, static_cast<GLuint>(prev));
}

bool gl_api_load(GlApi& a) {
#define DIRECT(n) a.n = reinterpret_cast<decltype(a.n)>(&::n);
    DIRECT(glAlphaFunc) DIRECT(glBindTexture) DIRECT(glBlendFunc) DIRECT(glClear) DIRECT(glClearColor)
    DIRECT(glColor4f) DIRECT(glColor4ub) DIRECT(glColorMask) DIRECT(glColorPointer) DIRECT(glDeleteTextures)
    DIRECT(glDepthFunc) DIRECT(glDisable) DIRECT(glDisableClientState) DIRECT(glDrawArrays) DIRECT(glDrawElements)
    DIRECT(glEnable) DIRECT(glEnableClientState) DIRECT(glGenTextures) DIRECT(glGetError) DIRECT(glGetFloatv)
    DIRECT(glGetString) DIRECT(glLineWidth) DIRECT(glLoadIdentity) DIRECT(glMatrixMode)
    DIRECT(glMultMatrixf) DIRECT(glPixelStorei) DIRECT(glPopMatrix) DIRECT(glPushMatrix) DIRECT(glReadPixels)
    DIRECT(glRotatef) DIRECT(glScissor) DIRECT(glTexCoordPointer) DIRECT(glTexEnvi) DIRECT(glTexImage2D)
    DIRECT(glTexParameteri) DIRECT(glTranslatef) DIRECT(glVertexPointer) DIRECT(glViewport)
#undef DIRECT
    a.glGetIntegerv = w_GetIntegerv;
    a.glOrthof = w_Orthof;
    a.glFrustumf = w_Frustumf;
    a.glClearDepthf = w_ClearDepthf;
    a.glBindFramebufferOES = w_BindFramebuffer;
    a.glGenFramebuffersOES = p_GenFramebuffers;
    a.glDeleteFramebuffersOES = p_DeleteFramebuffers;
    a.glCheckFramebufferStatusOES = p_CheckFramebufferStatus;
    a.glFramebufferTexture2DOES = p_FramebufferTexture2D;
    a.glGenerateMipmapOES = p_GenerateMipmap;
    a.glBindBuffer = proc<PFN_BindBuffer>("glBindBuffer");
    a.glBufferData = proc<PFN_BufferData>("glBufferData");
    a.glBufferSubData = proc<PFN_BufferSubData>("glBufferSubData");
    a.glGenBuffers = proc<PFN_GenDel>("glGenBuffers");
    a.glDeleteBuffers = proc<PFN_DelConst>("glDeleteBuffers");
    a.glCompressedTexImage2D = proc<PFN_CompressedTexImage2D>("glCompressedTexImage2D");
    a.glPointSizePointerOES = nullptr;  // not in desktop GL; the HLE skips the call
    return a.glBindBuffer && a.glGenBuffers && a.glCompressedTexImage2D;
}

}  // namespace compat

#pragma once
// The OpenGL ES 1.1 entry points libgame.so imports, as a table of host function
// pointers. On Android they point at libGLESv1_CM; the PC harness fills them from
// desktop OpenGL (compatibility profile) instead.
#include <cstddef>
#include <cstdint>

namespace compat {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLubyte = unsigned char;
using GLbitfield = unsigned int;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

#define COMPAT_GL_FUNCS(X)                                                                                   \
    X(void, glAlphaFunc, (GLenum a, GLfloat b))                                                              \
    X(void, glBindBuffer, (GLenum a, GLuint b))                                                              \
    X(void, glBindFramebufferOES, (GLenum a, GLuint b))                                                      \
    X(void, glBindTexture, (GLenum a, GLuint b))                                                             \
    X(void, glBlendFunc, (GLenum a, GLenum b))                                                               \
    X(void, glBufferData, (GLenum a, GLsizeiptr b, const void* c, GLenum d))                                 \
    X(void, glBufferSubData, (GLenum a, GLintptr b, GLsizeiptr c, const void* d))                            \
    X(GLenum, glCheckFramebufferStatusOES, (GLenum a))                                                       \
    X(void, glClear, (GLbitfield a))                                                                         \
    X(void, glClearColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d))                                      \
    X(void, glClearDepthf, (GLfloat a))                                                                      \
    X(void, glColor4f, (GLfloat a, GLfloat b, GLfloat c, GLfloat d))                                         \
    X(void, glColor4ub, (GLubyte a, GLubyte b, GLubyte c, GLubyte d))                                        \
    X(void, glColorMask, (GLboolean a, GLboolean b, GLboolean c, GLboolean d))                               \
    X(void, glColorPointer, (GLint a, GLenum b, GLsizei c, const void* d))                                   \
    X(void, glCompressedTexImage2D, (GLenum a, GLint b, GLenum c, GLsizei d, GLsizei e, GLint f, GLsizei g,   \
                                     const void* h))                                                         \
    X(void, glDeleteBuffers, (GLsizei a, const GLuint* b))                                                   \
    X(void, glDeleteFramebuffersOES, (GLsizei a, const GLuint* b))                                           \
    X(void, glDeleteTextures, (GLsizei a, const GLuint* b))                                                  \
    X(void, glDepthFunc, (GLenum a))                                                                         \
    X(void, glDisable, (GLenum a))                                                                           \
    X(void, glDisableClientState, (GLenum a))                                                                \
    X(void, glDrawArrays, (GLenum a, GLint b, GLsizei c))                                                    \
    X(void, glDrawElements, (GLenum a, GLsizei b, GLenum c, const void* d))                                  \
    X(void, glEnable, (GLenum a))                                                                            \
    X(void, glEnableClientState, (GLenum a))                                                                 \
    X(void, glFramebufferTexture2DOES, (GLenum a, GLenum b, GLenum c, GLuint d, GLint e))                    \
    X(void, glFrustumf, (GLfloat a, GLfloat b, GLfloat c, GLfloat d, GLfloat e, GLfloat f))                  \
    X(void, glGenBuffers, (GLsizei a, GLuint* b))                                                            \
    X(void, glGenFramebuffersOES, (GLsizei a, GLuint* b))                                                    \
    X(void, glGenTextures, (GLsizei a, GLuint* b))                                                           \
    X(void, glGenerateMipmapOES, (GLenum a))                                                                 \
    X(GLenum, glGetError, ())                                                                                \
    X(void, glGetFloatv, (GLenum a, GLfloat* b))                                                             \
    X(void, glGetIntegerv, (GLenum a, GLint* b))                                                             \
    X(const GLubyte*, glGetString, (GLenum a))                                                               \
    X(void, glLineWidth, (GLfloat a))                                                                        \
    X(void, glLoadIdentity, ())                                                                              \
    X(void, glMatrixMode, (GLenum a))                                                                        \
    X(void, glMultMatrixf, (const GLfloat* a))                                                               \
    X(void, glOrthof, (GLfloat a, GLfloat b, GLfloat c, GLfloat d, GLfloat e, GLfloat f))                    \
    X(void, glPixelStorei, (GLenum a, GLint b))                                                              \
    X(void, glPointSizePointerOES, (GLenum a, GLsizei b, const void* c))                                     \
    X(void, glPopMatrix, ())                                                                                 \
    X(void, glPushMatrix, ())                                                                                \
    X(void, glReadPixels, (GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, void* g))             \
    X(void, glRotatef, (GLfloat a, GLfloat b, GLfloat c, GLfloat d))                                         \
    X(void, glScissor, (GLint a, GLint b, GLsizei c, GLsizei d))                                             \
    X(void, glTexCoordPointer, (GLint a, GLenum b, GLsizei c, const void* d))                                \
    X(void, glTexEnvi, (GLenum a, GLenum b, GLint c))                                                        \
    X(void, glTexImage2D, (GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h,     \
                           const void* i))                                                                   \
    X(void, glTexParameteri, (GLenum a, GLenum b, GLint c))                                                  \
    X(void, glTranslatef, (GLfloat a, GLfloat b, GLfloat c))                                                 \
    X(void, glVertexPointer, (GLint a, GLenum b, GLsizei c, const void* d))                                  \
    X(void, glViewport, (GLint a, GLint b, GLsizei c, GLsizei d))

struct GlApi {
#define COMPAT_GL_MEMBER(ret, name, params) ret(GLAPIENTRY* name) params = nullptr;
    COMPAT_GL_FUNCS(COMPAT_GL_MEMBER)
#undef COMPAT_GL_MEMBER
};

// Implemented per platform (gl_android.cpp / harness gl_wgl.cpp).
bool gl_api_load(GlApi& api);
extern GlApi g_gl;

}  // namespace compat

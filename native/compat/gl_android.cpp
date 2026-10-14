// Android: the GLES 1.1 table points straight at the system libGLESv1_CM.
#define GL_GLEXT_PROTOTYPES 1
#include <GLES/gl.h>
#include <GLES/glext.h>

#include "gl_api.h"

namespace compat {

bool gl_api_load(GlApi& a) {
#define COMPAT_GL_ASSIGN(ret, name, params) a.name = reinterpret_cast<decltype(a.name)>(&::name);
    COMPAT_GL_FUNCS(COMPAT_GL_ASSIGN)
#undef COMPAT_GL_ASSIGN
    return true;
}

}  // namespace compat

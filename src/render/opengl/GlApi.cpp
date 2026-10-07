#include "render/opengl/GlApi.h"

#include <SDL3/SDL_video.h>

namespace mm2::render::gl {

bool Api::load(std::string* missing) {
    bool ok = true;
    auto get = [&](auto& fn, const char* name, bool required) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(SDL_GL_GetProcAddress(name));
        if (!fn && required) {
            ok = false;
            if (missing) {
                if (!missing->empty())
                    *missing += ", ";
                *missing += name;
            }
        }
    };
#define MM2_GL_LOAD(ret, name, params) get(name, "gl" #name, true);
    MM2_GL_FUNCTIONS(MM2_GL_LOAD)
#undef MM2_GL_LOAD
    get(DebugMessageCallback, "glDebugMessageCallback", false);
    get(ObjectLabel, "glObjectLabel", false);
    return ok;
}

} // namespace mm2::render::gl

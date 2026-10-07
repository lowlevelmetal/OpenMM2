#include "render/Types.h"

namespace mm2::render {

std::uint32_t PipelineState::key() const {
    std::uint32_t k = 0;
    int shift = 0;
    auto put = [&](std::uint32_t v, int bits) {
        k |= (v & ((1u << bits) - 1)) << shift;
        shift += bits;
    };
    put(static_cast<std::uint32_t>(vertexFormat), 2);
    put(static_cast<std::uint32_t>(topology), 2);
    put(static_cast<std::uint32_t>(blend), 3);
    put(static_cast<std::uint32_t>(cull), 2);
    put(static_cast<std::uint32_t>(frontFace), 1);
    put(depthTest, 1);
    put(depthWrite, 1);
    put(static_cast<std::uint32_t>(depthCompare), 3);
    put(depthBias, 1);
    put(colorWrite, 1);
    return k;
}

} // namespace mm2::render

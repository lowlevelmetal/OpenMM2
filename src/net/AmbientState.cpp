#include "net/AmbientState.h"

#include <algorithm>
#include <cmath>

namespace mm2::net {

void AmbientStateMsg::setOrigin(const Vec3& p) {
    const float v[3] = {p.x, p.y, p.z};
    for (int i = 0; i < 3; ++i) {
        const float r = std::isfinite(v[i]) ? std::round(v[i]) : 0.0f;
        origin[i] = static_cast<std::int16_t>(std::clamp(r, -32768.0f, 32767.0f));
    }
}

std::size_t ambientEntityBits(const AmbientEntity& e) {
    WriteStream s;
    AmbientEntity copy = e;
    serializeAmbientEntity(s, copy, e.position);
    return s.writer().bitCount();
}

} // namespace mm2::net

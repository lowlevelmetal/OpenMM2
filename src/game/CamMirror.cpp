// The rear-view mirror's camera: mmMirror, ported from Midtown Madness 2
// (MM2Recomp, build 3393).
#include "game/CamMirror.h"

#include "core/StringUtil.h"
#include "game/CamMath.h"
#include "vfs/Vfs.h"

#include <format>

namespace mm2::game {

void MirrorParams::load(const data::DatNode& n) {
    // mmMirror::FileIO
    n.read("Position", position);
    n.read("Size", size);
    n.read("Fov", fov);
    n.read("Aspect", aspect);
    n.read("NearClip", nearClip);
    n.read("FarClip", farClip);
}

std::optional<MirrorParams> loadMirrorParams(const vfs::Vfs& vfs, std::string_view car, std::string* error) {
    const std::string path = std::format("tune/{}.mmmirror", str::lower(car));
    auto bytes = vfs.readAll(path);
    if (!bytes) {
        if (error)
            *error = std::format("{} not found", path);
        return std::nullopt;
    }
    std::string err;
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    auto dat = data::parseDat(text, &err);
    if (!dat || !dat->top()) {
        if (error)
            *error = std::format("{}: {}", path, dat ? "empty" : err);
        return std::nullopt;
    }
    MirrorParams p;
    p.load(*dat->top());
    return p;
}

RearViewMirror::RearViewMirror() { setParams({}); }

void RearViewMirror::setParams(const MirrorParams& params) {
    // mmMirror::Init: identity turned pi about YAXIS, the position, then
    // m0 negated.
    m_params = params;
    m_local = Mat34::identity();
    cam::rotate(m_local, cam::kYAxis, cam::kPi);
    m_local.m3 = m_params.position;
    m_local.m0 = {m_local.m0.x * -1.0f, m_local.m0.y * -1.0f, m_local.m0.z * -1.0f};
}

void RearViewMirror::load(const vfs::Vfs& vfs, std::string_view car) {
    // The file's Position replaces the one Init set; a missing file keeps
    // the defaults (asNode::Load fails quietly).
    setParams(loadMirrorParams(vfs, car).value_or(MirrorParams{}));
}

Mat34 RearViewMirror::worldMatrix(const Mat34& car) const {
    // mmMirror::Cull: Matrix34::Dot(mirror, car).
    Mat34 m = m_local;
    cam::dot(m, car);
    return m;
}

RearViewMirror::Viewport RearViewMirror::viewport(int screenWidth, int screenHeight) const {
    // mmMirror::Reset: gfxViewport::SetWindow(width - w - 1, 1, w, h).
    Viewport v;
    v.width = static_cast<int>(static_cast<float>(screenWidth) * m_params.size.x);
    v.height = static_cast<int>(static_cast<float>(screenHeight) * m_params.size.y);
    v.x = screenWidth - v.width - 1;
    v.y = 1;
    return v;
}

} // namespace mm2::game

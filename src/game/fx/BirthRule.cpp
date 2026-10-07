#include "game/fx/BirthRule.h"

namespace mm2::game::fx {

bool loadBirthRule(const data::DatNode& b, BirthRule& r) {
    int found = 0;
    auto f = [&](const char* key, float& out) { found += b.read(key, out) ? 1 : 0; };
    auto i = [&](const char* key, int& out) { found += b.read(key, out) ? 1 : 0; };
    auto v = [&](const char* key, Vec3& out) { found += b.read(key, out) ? 1 : 0; };
    v("Position", r.position);
    v("PositionVar", r.positionVar);
    v("Velocity", r.velocity);
    v("VelocityVar", r.velocityVar);
    f("Life", r.life);
    f("LifeVar", r.lifeVar);
    f("Mass", r.mass);
    f("MassVar", r.massVar);
    f("Radius", r.radius);
    f("RadiusVar", r.radiusVar);
    f("DRadius", r.dRadius);
    f("DRadiusVar", r.dRadiusVar);
    f("Drag", r.drag);
    f("DragVar", r.dragVar);
    f("Damp", r.damp);
    f("DampVar", r.dampVar);
    f("SpewRate", r.spewRate);
    f("SpewTimeLimit", r.spewTimeLimit);
    f("Gravity", r.gravity);
    i("DAlpha", r.dAlpha);
    i("DAlphaVar", r.dAlphaVar);
    i("DRotation", r.dRotation);
    i("DRotationVar", r.dRotationVar);
    i("TexFrameStart", r.texFrameStart);
    i("TexFrameEnd", r.texFrameEnd);
    i("InitialBlast", r.initialBlast);
    i("BirthFlags", r.birthFlags);
    f("Height", r.height);
    f("Intensity", r.intensity);
    if (auto c = b.child("Color"); c && !c->numbers.empty()) {
        r.color = static_cast<std::uint32_t>(static_cast<std::int64_t>(c->numbers[0]));
        ++found;
    }
    return found > 0;
}

std::optional<BirthRule> parseBirthRuleFile(std::string_view text, std::string* error) {
    auto file = data::parseDat(text, error);
    if (!file || !file->top()) {
        if (error && error->empty())
            *error = "empty file";
        return std::nullopt;
    }
    BirthRule r;
    if (!loadBirthRule(*file->top(), r)) {
        if (error)
            *error = "no birth rule fields";
        return std::nullopt;
    }
    return r;
}

} // namespace mm2::game::fx

#include "game/fx/VehicleEffects.h"

#include "phys/Material.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::fx {

BirthRule VehicleFxSetup::engineSmokeDefaults() {
    // vehCarDamage::Init: the shared EngineSmokeRule gets these before the
    // car's .vehCarDamage file (whose FileIO includes the rule's) is read.
    BirthRule r;
    r.velocityVar = {1.0f, 2.0f, 1.0f};
    r.velocity = {0.0f, 1.0f, 0.0f};
    r.life = 0.8f;
    r.lifeVar = 0.4f;
    r.mass = 0.2f;
    r.drag = 1.0f;
    r.radius = 0.3f;
    r.radiusVar = 0.1f;
    r.dAlpha = -15;
    r.dRadius = 0.03f;
    r.gravity = 3.0f;
    return r;
}

VehicleEffects::VehicleEffects(const EffectLibrary& library, const VehicleFxSetup& setup)
    : m_smokeRule(setup.smokeRule), m_setup(setup), m_sparks(setup.sparkColors) {
    const ParticleSheet sheet = EffectLibrary::wheelSheet();
    m_wheelPtx.init(kWheelParticles, sheet.framesWide, sheet.framesHigh);
    m_wheelPtx.rng().seed(0x1234u);
    for (int i = 0; i < EffectLibrary::kWheelRules; ++i)
        if (const BirthRule* r = library.wheelRule(i))
            m_wheelRules[static_cast<std::size_t>(i)] = *r;
    // mmGame::InitWeather: in rain the smoke rule becomes a copy of splash.
    if (setup.rain)
        m_wheelRules[4] = m_wheelRules[6];
    m_smoke.init(kSmokeParticles, 2, 2);
    m_smoke.rng().seed(0x5A0Cu);
    reset();
}

void VehicleEffects::reset() {
    for (auto& t : m_tracks)
        t.reset();
    m_wheelPtx.reset();
    m_wheelFraction = {};
    // vehCarDamage::ClearDamage.
    m_smoke.reset();
    m_smokeFraction = 0.0f;
    m_smokeRule.texFrameStart = m_smokeRule.texFrameEnd = 0;
    m_ticker.reset();
    m_sparks.reset();
    m_shards.reset();
    m_damagePoint.reset();
}

void VehicleEffects::impact(const phys::Impact& impact, const phys::CarSim& car) {
    // vehCarDamage::InsertImpact/ApplyImpact: the impulse scaled by the other
    // body's share of the masses must pass ImpactThreshold, at 10 mph or
    // more unless the other party is a vehicle.
    float share = 1.0f;
    if (impact.other)
        share = impact.other->ics.mass / (car.body.ics.mass + impact.other->ics.mass);
    const float value = std::abs(impact.impulse) * share;
    const float mph = car.speedMph();
    if (!(car.damage.params.impactThreshold < value) || (mph < 10.0f && !impact.other))
        return;
    // Sparks: 16 x impact x frame seconds of them (at most the pool's 64).
    if (15.0f < mph)
        m_sparks.radialBlast(static_cast<int>(16.0f * value * FixedTicker::kStep), impact.point, impact.normal);
    m_shards.emit(impact.point, value, car.speed(), car.body.ics.matrix);
    if (!m_damagePoint)
        m_damagePoint = car.modelMatrix().untransform(impact.point);
}

std::optional<Vec3> VehicleEffects::takeDamagePoint() {
    auto p = m_damagePoint;
    m_damagePoint.reset();
    return p;
}

void VehicleEffects::update(float dt, const phys::CarSim& car, const VehicleFxContext& context) {
    for (int i = m_ticker.advance(dt); i > 0; --i)
        step(FixedTicker::kStep, car, context);
}

// vehWheelPtx::Blast: particles thrown off a sliding wheel.
void VehicleEffects::blastWheel(const phys::Wheel& w, float dt, float threshold, int index, int slot) {
    if (!(w.slide > threshold))
        return;
    BirthRule rule = m_wheelRules[static_cast<std::size_t>(index)];
    // Load factor: 0.25 .. 0.5 while the suspension carries less than the
    // static load, rising to 1 as it compresses towards SuspensionLimit.
    float load;
    if (w.suspensionForce < w.normalLoad) {
        load = (w.suspensionForce / w.normalLoad + 1.0f) * 0.25f;
    } else {
        load = ((w.suspensionForce - w.normalLoad) / (w.spring * w.params.suspensionLimit) + 1.0f) * 0.5f;
        if (load > 1.0f)
            load = 1.0f;
    }
    const float radius = load * w.width * rule.radius * 0.5f;
    // Born at the edge of the tyre the tread leaves the ground (behind the
    // wheel when rolling forward), lifted by their radius.
    const float spin = -w.rotationSpeed;
    const float edge = (spin > 0.0f ? 1.0f : spin < 0.0f ? -1.0f : 0.0f) * w.radius;
    const Mat34& cf = w.contactFrame;
    rule.position = {edge * cf.m2.x + cf.m3.x, edge * cf.m2.y + cf.m3.y + radius, edge * cf.m2.z + cf.m3.z};
    // The rule's velocity scales the contact's sideways speed, the tread
    // speed and the slip (against the back axis), in the contact frame.
    const float side = w.latVelocity * rule.velocity.x;
    const float tread = std::abs(w.rotationSpeed) * w.radius * rule.velocity.y;
    const float slip = -w.slipVelocity * rule.velocity.z;
    rule.velocity = {slip * cf.m2.x + tread * cf.m1.x + side * cf.m0.x, slip * cf.m2.y + tread * cf.m1.y + side * cf.m0.y,
                     slip * cf.m2.z + tread * cf.m1.z + side * cf.m0.z};
    rule.radius = radius;
    // InitialBlast is the rate: particles per second at full load.
    float& fraction = m_wheelFraction[static_cast<std::size_t>(slot)];
    const float count = static_cast<float>(rule.initialBlast) * dt * load + fraction;
    const int n = static_cast<int>(count);
    fraction = count - static_cast<float>(n);
    if (n >= 1)
        m_wheelPtx.blast(n, &rule);
}

// vehCarDamage::SpewSmoke: `amount` particles per update, accumulated.
void VehicleEffects::spewSmoke(const Mat34& car, const Vec3& offset, float amount) {
    m_smokeFraction += amount;
    const int n = static_cast<int>(m_smokeFraction);
    if (n == 0)
        return;
    BirthRule rule = m_smokeRule;
    rule.velocity = car.transformDir(m_smokeRule.velocity);
    rule.position = car.transform(offset);
    m_smokeFraction -= static_cast<float>(n);
    m_smoke.blast(n, &rule);
}

void VehicleEffects::step(float dt, const phys::CarSim& car, const VehicleFxContext& context) {
    // vehCar::Update: the four tracks (vehCar::UpdateTrack), then vehWheelPtx.
    for (std::size_t i = 0; i < 4; ++i) {
        const phys::Wheel& w = car.wheels[i];
        auto& track = m_tracks[i];
        const bool water = w.material && w.material->name == "water";
        if (!track.laying())
            track.setWidth(w.width);
        track.update(w.intersection.position, w.matrix.m0, w.skidding && !water && context.tracksAllowed);    }
    for (const phys::Wheel& w : car.wheels) {
        if (!w.material)
            continue;
        for (int slot = 0; slot < 2; ++slot) {
            const int index = w.material->ptxIndex[slot];
            if (index >= 0 && index < EffectLibrary::kWheelRules)
                blastWheel(w, dt, w.material->ptxThreshold[slot], index, slot);
        }
    }
    m_wheelPtx.update(dt);

    // vehCarDamage::Update: smoke above MedDamage, four steps to MaxDamage,
    // each with its own frame of fxpt8; one puff per update.
    const Mat34& body = car.body.ics.matrix;
    const auto& d = car.damage.params;
    const float f = std::clamp((car.damage.currentDamage - d.medDamage) / (d.maxDamage - d.medDamage), 0.0f, 1.0f);
    const int level = static_cast<int>(std::ceil(static_cast<double>(f * 4.0f)));
    if (level != 0) {
        constexpr int kFrame[] = {0, 1, 0, 3, 2};
        m_smokeRule.texFrameStart = m_smokeRule.texFrameEnd = kFrame[std::min(level, 4)];
        Vec3 offset = d.smokeOffset;
        if (d.smokeOffset2 != Vec3{}) {
            if (d.doublePivot) {
                // Alternate between the two pivots.
                offset = m_nextPivot == 0 ? d.smokeOffset : d.smokeOffset2;
                m_nextPivot = (m_nextPivot - 1) & 1;
            } else {
                // A random point between them.
                const float t = m_smoke.rng().frand();
                offset = (d.smokeOffset2 - d.smokeOffset) * t + d.smokeOffset;
            }
        }
        spewSmoke(body, offset, 1.0f); // ParticleMultiplier = 1
    }
    m_smoke.update(dt);
    m_sparks.update(dt);
    m_shards.update(dt);
    // Exhaust pivots smoke with the revs above 2000 rpm, whatever the damage
    // (with the frame the damage level last chose).
    if (m_setup.exhaust[0] || m_setup.exhaust[1]) {
        const float revs = std::clamp((car.engine.rpm - 2000.0f) / (car.params.engine.maxRPM - 2000.0f), 0.0f, 1.0f);
        for (const auto& pivot : m_setup.exhaust)
            if (pivot)
                spewSmoke(body, *pivot, revs);
    }
}

void VehicleEffects::draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards,
                          SkidRenderer& skids, const Mat34& cameraBasis) {
    skids.draw(device, textures, {&m_tracks[0], &m_tracks[1], &m_tracks[2], &m_tracks[3]});
    if (m_wheelPtx.count())
        cards.draw(device, cameraBasis, m_wheelPtx, textures.get(EffectLibrary::wheelSheet().texture));
    if (m_smoke.count())
        cards.draw(device, cameraBasis, m_smoke, textures.get("fxpt8"));
    m_sparks.draw(device);
    m_shards.draw(device, textures, m_setup.shardTextures);
}

} // namespace mm2::game::fx

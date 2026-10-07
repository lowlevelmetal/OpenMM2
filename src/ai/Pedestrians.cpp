// Pedestrians. Structure and constants after MM1's aiPedestrian (Open1560
// game.asm, GPL-3.0); see Pedestrians.h and docs/ai.md.
#include "ai/Pedestrians.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

Vec3 forwardFromYaw(float yaw) {
    return {-std::sin(yaw), 0.0f, -std::cos(yaw)};
}
float yawFromForward(const Vec3& f) {
    return std::atan2(-f.x, -f.z);
}
Vec3 leftOf(const Vec3& f) {
    return {f.z, 0.0f, -f.x};
}

float wrapAngle(float a) {
    while (a > kPi)
        a -= kTwoPi;
    while (a < -kPi)
        a += kTwoPi;
    return a;
}

bool contains(std::string_view s, std::string_view part) {
    return s.find(part) != std::string_view::npos;
}

} // namespace

Pedestrians::Pedestrians(const RoadNetwork& network, std::vector<PedTypeInfo> types,
                         const PedSettings& settings, std::uint64_t seed)
    : m_net(network), m_types(std::move(types)), m_settings(settings), m_rng(seed) {
    m_peds.resize(static_cast<std::size_t>(std::max(0, m_settings.maxPeds)));
    m_sidewalkActive.assign(m_net.sidewalks().size(), 0);
}

bool Pedestrians::isLooping(const asset::PedAnimState* s) const {
    return s && str::iequals(s->next, s->name);
}

float Pedestrians::stateDuration(const asset::PedAnimState* s) const {
    if (!s)
        return 1.0f;
    return static_cast<float>(std::max(1, s->lastFrame - s->firstFrame + 1)) / kPedAnimFps;
}

bool Pedestrians::busy(const Ped& p) const {
    return p.state && (contains(p.state->name, "DIVE") || contains(p.state->name, "GROUND"));
}

void Pedestrians::setState(Ped& p, std::string_view name) {
    if (p.type < 0 || static_cast<std::size_t>(p.type) >= m_types.size())
        return;
    const asset::PedAnimState* s = m_types[static_cast<std::size_t>(p.type)].table.find(name);
    if (!s)
        return;
    p.state = s;
    p.stateTime = 0.0f;
}

void Pedestrians::spawn(int sidewalkId, const Vec3& playerPos, std::vector<int>& freeSlots) {
    if (m_types.empty())
        return;
    const Sidewalk& walk = m_net.sidewalks()[static_cast<std::size_t>(sidewalkId)];
    const int count = static_cast<int>(walk.centre.length * m_settings.density / 10.0f);
    for (int i = 0; i < count && !freeSlots.empty(); ++i) {
        const float s = m_rng.frand() * walk.centre.length;
        const Vec3 pos = walk.centre.pointAt(s);
        if (pos.dist2(playerPos) < sq(kPedAwareRadius))
            continue; // don't appear in plain sight
        Ped& p = m_peds[static_cast<std::size_t>(freeSlots.back())];
        freeSlots.pop_back();
        p = Ped{};
        p.active = true;
        p.type = m_rng.irand(static_cast<int>(m_types.size()));
        p.variant = m_rng.irand(std::max(1, m_types[static_cast<std::size_t>(p.type)].variants));
        p.sidewalk = sidewalkId;
        p.s = s;
        p.dir = m_rng.frand() < 0.5f ? -1 : 1;
        // aiPedestrian::Reset: sin(frand * 2 pi) * 1.8 across the walkway,
        // kept on this sidewalk's width.
        const float spread = std::min(kPedLateralSpread, std::max(0.0f, walk.halfWidth - 0.4f));
        p.lateral = std::sin(m_rng.frand() * 6.2831f) * spread;
        Vec3 dir;
        walk.centre.pointAt(s, &dir);
        dir = dir * static_cast<float>(p.dir);
        p.heading = yawFromForward(dir);
        p.position = pos + leftOf(dir.normalized()) * p.lateral;
        setState(p, "WALK");
        if (!p.state) {
            p.active = false;
            continue;
        }
        p.stateTime = m_rng.frand() * stateDuration(p.state);
    }
}

void Pedestrians::applyRootMotion(Ped& p, float dt) {
    if (!p.state)
        return;
    const float duration = stateDuration(p.state);
    const Vec3 fwd = forwardFromYaw(p.heading);
    const Vec3 left = leftOf(fwd);
    p.position +=
        fwd * (p.state->forwardDistance / duration * dt) + left * (p.state->sideDistance / duration * dt);
}

void Pedestrians::wander(Ped& p, float dt) {
    const Sidewalk& walk = m_net.sidewalks()[static_cast<std::size_t>(p.sidewalk)];
    const bool walking = p.state && str::iequals(p.state->name, "WALK");
    if (!walking) {
        // Standing: wait, then set off again.
        if (p.state && str::istartsWith(p.state->name, "STAND") && isLooping(p.state)) {
            p.standTimer -= dt;
            if (p.standTimer <= 0.0f)
                setState(p, "STAND_WALK");
        } else if (p.state && str::iequals(p.state->name, "ANTIC")) {
            setState(p, "ANTIC_WALK");
        }
        if (p.state && !str::iequals(p.state->name, "STAND_WALK") &&
            !str::iequals(p.state->name, "ANTIC_WALK"))
            return;
    }
    // Walking speed from the animation's root motion.
    const asset::PedAnimState* walkState = m_types[static_cast<std::size_t>(p.type)].table.find("WALK");
    const float speed = walkState ? walkState->forwardDistance / stateDuration(walkState) : 1.4f;
    p.s += speed * dt * static_cast<float>(p.dir);

    if (p.s < 0.0f || p.s > walk.centre.length) {
        // End of the sidewalk: carry on along another one at this corner, or turn round.
        const int node = p.s < 0.0f ? walk.startIntersection : walk.endIntersection;
        const Vec3 here = walk.centre.pointAt(p.s);
        int bestWalk = -1;
        float bestS = 0.0f, bestD = 12.0f;
        int bestDir = 1;
        std::vector<int> candidates;
        for (int w : m_net.sidewalksAt(node, p.sidewalk)) {
            const Sidewalk& o = m_net.sidewalks()[static_cast<std::size_t>(w)];
            const float dStart =
                Vec2{o.centre.points.front().x - here.x, o.centre.points.front().z - here.z}.mag();
            const float dEnd =
                Vec2{o.centre.points.back().x - here.x, o.centre.points.back().z - here.z}.mag();
            if (std::min(dStart, dEnd) < bestD + 6.0f)
                candidates.push_back(w);
            if (dStart < bestD) {
                bestD = dStart;
                bestWalk = w;
                bestS = 0.0f;
                bestDir = 1;
            }
            if (dEnd < bestD) {
                bestD = dEnd;
                bestWalk = w;
                bestS = o.centre.length;
                bestDir = -1;
            }
        }
        if (!candidates.empty() && m_rng.frand() < 0.5f) {
            // Pick any nearby corner sidewalk (inferred: the original picks
            // the next road segment in aiPedestrian::PickNextRdSeg).
            const int w =
                candidates[static_cast<std::size_t>(m_rng.irand(static_cast<int>(candidates.size())))];
            const Sidewalk& o = m_net.sidewalks()[static_cast<std::size_t>(w)];
            const float dStart =
                Vec2{o.centre.points.front().x - here.x, o.centre.points.front().z - here.z}.mag();
            const float dEnd =
                Vec2{o.centre.points.back().x - here.x, o.centre.points.back().z - here.z}.mag();
            bestWalk = w;
            bestS = dStart <= dEnd ? 0.0f : o.centre.length;
            bestDir = dStart <= dEnd ? 1 : -1;
        }
        if (bestWalk >= 0 && m_sidewalkActive[static_cast<std::size_t>(bestWalk)]) {
            p.sidewalk = bestWalk;
            p.s = bestS;
            p.dir = bestDir;
        } else {
            p.dir = -p.dir;
            p.s = clampf(p.s, 0.0f, walk.centre.length);
        }
        // Now and then stop for a moment (inferred).
        if (m_rng.frand() < 0.25f) {
            setState(p, "WALK_STAND");
            p.standTimer = 2.0f + m_rng.frand() * 4.0f;
        }
    }

    const Sidewalk& cur = m_net.sidewalks()[static_cast<std::size_t>(p.sidewalk)];
    Vec3 dir;
    const Vec3 c = cur.centre.pointAt(p.s, &dir);
    dir = dir * static_cast<float>(p.dir);
    const float spread = std::max(0.0f, cur.halfWidth - 0.4f);
    p.lateral = clampf(p.lateral, -spread, spread);
    p.position = c + leftOf(dir) * p.lateral;
    // Turn towards the walking direction at a limited rate (Wander turns
    // when the error exceeds 0.15 rad, flt_639354).
    const float target = yawFromForward(dir);
    const float err = wrapAngle(target - p.heading);
    const float maxTurn = 3.0f * dt;
    p.heading = wrapAngle(p.heading + clampf(err, -maxTurn, maxTurn));
}

void Pedestrians::react(Ped& p, const Vec3& playerPos, const Vec3& playerVel) {
    if (busy(p))
        return;
    const Vec2 v{playerVel.x, playerVel.z};
    const Vec2 r{p.position.x - playerPos.x, p.position.z - playerPos.z};
    const float dist = r.mag();
    const float speed2 = v.mag2();
    const bool anticipating = p.state && str::iequals(p.state->name, "ANTIC");
    if (dist > kPedAwareRadius || speed2 < 4.0f) {
        if (anticipating)
            setState(p, "ANTIC_WALK");
        return;
    }
    // Closest approach of the car's straight-line path (DetectPlayerAnticipate /
    // DetectPlayerCollision use the player's velocity the same way; the
    // thresholds below are the decoded constants, their exact roles inferred).
    const float tClosest = r.dot(v) / speed2;
    const Vec2 miss = r - v * tClosest;
    const float missDist = miss.mag();
    if (tClosest <= 0.0f) {
        if (anticipating)
            setState(p, "ANTIC_WALK");
        return;
    }
    if (missDist < 2.5f && tClosest < 1.25f) { // flt_61B610 1.25
        // Dive away from the car's line.
        const Vec3 fwd = forwardFromYaw(p.heading);
        const Vec3 left = leftOf(fwd);
        const Vec2 away = missDist > 1e-3f             ? miss * (1.0f / missDist)
                          : Vec2{-v.y, v.x}.mag2() > 0 ? Vec2{-v.y, v.x}
                                                       : Vec2{1, 0};
        const bool goLeft = left.x * away.x + left.z * away.y >= 0.0f;
        const bool walking = p.state && str::iequals(p.state->name, "WALK");
        if (walking)
            setState(p, goLeft ? "WALK_LDIVE" : "WALK_RDIVE");
        else
            setState(p, goLeft ? "ANTIC_LDIVE" : "ANTIC_RDIVE");
        p.onSidewalk = false;
        return;
    }
    if (missDist < kPedCollisionRadius && tClosest < 3.0f && !anticipating) {
        const bool walking = p.state && str::iequals(p.state->name, "WALK");
        setState(p, walking ? "WALK_ANTIC" : "STAND_ANTIC");
    }
}

void Pedestrians::advanceAnimation(Ped& p, float dt) {
    if (!p.state)
        return;
    p.stateTime += dt;
    const float duration = stateDuration(p.state);
    while (p.stateTime >= duration) {
        if (isLooping(p.state)) {
            p.stateTime -= duration;
            break;
        }
        const float carry = p.stateTime - duration;
        const std::string next = p.state->next;
        setState(p, next);
        p.stateTime = carry;
        if (p.state && str::istartsWith(p.state->name, "STAND") && isLooping(p.state)) {
            p.standTimer = 1.0f + m_rng.frand() * 3.0f;
            if (!p.onSidewalk) {
                // Back on the feet after a dive: rejoin the walkway.
                const Sidewalk& walk = m_net.sidewalks()[static_cast<std::size_t>(p.sidewalk)];
                p.s = walk.centre.project(p.position);
                p.onSidewalk = true;
            }
        }
        if (!p.state)
            break;
    }
}

void Pedestrians::step(float dt, const Vec3& playerPos, const Vec3& playerVel) {
    // Sidewalks within the active radius get pedestrians; others lose them.
    std::vector<int> freeSlots;
    for (int i = static_cast<int>(m_peds.size()) - 1; i >= 0; --i)
        if (!m_peds[static_cast<std::size_t>(i)].active)
            freeSlots.push_back(i);
    for (std::size_t w = 0; w < m_net.sidewalks().size(); ++w) {
        const Sidewalk& walk = m_net.sidewalks()[w];
        float d = 0.0f;
        walk.centre.project(playerPos, &d);
        const bool active = d < kPedActiveRadius;
        if (active && !m_sidewalkActive[w]) {
            m_sidewalkActive[w] = 1;
            spawn(static_cast<int>(w), playerPos, freeSlots);
        } else if (!active && m_sidewalkActive[w]) {
            m_sidewalkActive[w] = 0;
        }
    }
    for (auto& p : m_peds) {
        if (!p.active)
            continue;
        if (p.position.dist2(playerPos) > sq(kPedActiveRadius + 25.0f)) {
            p.active = false; // culled (aiPedestrian::Update uses 75 m)
            continue;
        }
        react(p, playerPos, playerVel);
        advanceAnimation(p, dt);
        if (!p.state) {
            p.active = false;
            continue;
        }
        if (busy(p) || str::iequals(p.state->name, "WALK_ANTIC") ||
            str::iequals(p.state->name, "STAND_ANTIC") || str::iequals(p.state->name, "ANTIC") ||
            str::iequals(p.state->name, "WALK_STAND")) {
            if (str::iequals(p.state->name, "ANTIC")) {
                // Face the oncoming car.
                const Vec3 toCar = playerPos - p.position;
                if (Vec2{toCar.x, toCar.z}.mag2() > 0.01f) {
                    const float err = wrapAngle(yawFromForward(toCar) - p.heading);
                    p.heading = wrapAngle(p.heading + clampf(err, -4.0f * dt, 4.0f * dt));
                }
            }
            applyRootMotion(p, dt);
            const Sidewalk& walk = m_net.sidewalks()[static_cast<std::size_t>(p.sidewalk)];
            p.position.y = walk.centre.pointAt(walk.centre.project(p.position)).y;
        } else {
            wander(p, dt);
        }
    }
    publish();
}

void Pedestrians::publish() {
    m_public.clear();
    for (std::size_t i = 0; i < m_peds.size(); ++i) {
        const Ped& p = m_peds[i];
        if (!p.active || !p.state)
            continue;
        Pedestrian out;
        out.id = static_cast<int>(i);
        out.type = p.type;
        out.typeName = m_types[static_cast<std::size_t>(p.type)].name;
        out.variant = p.variant;
        out.sidewalk = p.sidewalk;
        Mat34 m = Mat34::rotationY(p.heading);
        m.m3 = p.position;
        out.transform = m;
        out.state = p.state->name;
        out.animFile = p.state->animFile;
        out.frame = static_cast<float>(p.state->firstFrame - 1) + p.stateTime * kPedAnimFps;
        m_public.push_back(std::move(out));
    }
}

std::size_t Pedestrians::activeCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_peds, [](const Ped& p) { return p.active; }));
}

float Pedestrians::distanceFromSidewalk(int pedId) const {
    if (pedId < 0 || static_cast<std::size_t>(pedId) >= m_peds.size())
        return 0.0f;
    const Ped& p = m_peds[static_cast<std::size_t>(pedId)];
    if (!p.active)
        return 0.0f;
    const Sidewalk& walk = m_net.sidewalks()[static_cast<std::size_t>(p.sidewalk)];
    float d = 0.0f;
    walk.centre.project(p.position, &d);
    return d;
}

} // namespace mm2::ai

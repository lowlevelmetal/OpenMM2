// Ambient traffic. Ported from MM1 (Open1560 game.asm, GPL-3.0); see Traffic.h
// for the routine list and docs/ai.md for evidence levels.
#include "ai/Traffic.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

// Hermite basis of aiRailSet::ComputeXZCurve (stru_6A7B58, decoded from the
// aiRailSet constructor): with v = (p0, p1, m0, m1), coefficients = M * v.
Vec4 hermite(float p0, float p1, float m0, float m1) {
    return {2.0f * p0 - 2.0f * p1 + m0 + m1, -3.0f * p0 + 3.0f * p1 - 2.0f * m0 - m1, m0, p0};
}

// aiRailSet::SolveXZCurve: position and derivative of the cubic at t.
void solveCurve(const Vec4& cx, const Vec4& cz, float t, Vec3& pos, Vec3& dir) {
    pos.x = ((cx.x * t + cx.y) * t + cx.z) * t + cx.w;
    pos.y = 0.0f;
    pos.z = ((cz.x * t + cz.y) * t + cz.z) * t + cz.w;
    // c - t * (b * -2 - (a * t) * 3) = 3at^2 + 2bt + c
    dir.x = cx.z - t * (cx.y * -2.0f - (cx.x * t) * 3.0f);
    dir.y = 0.0f;
    dir.z = cz.z - t * (cz.y * -2.0f - (cz.x * t) * 3.0f);
}

Mat34 frameFromForward(const Vec3& forward, const Vec3& position) {
    Vec3 f = forward.mag2() > 1e-10f ? forward.normalized() : Vec3{0, 0, -1};
    const Vec3 back = -f;
    Vec3 right = Vec3::yAxis().cross(back);
    if (right.mag2() < 1e-10f)
        right = Vec3::xAxis();
    right = right.normalized();
    Mat34 m;
    m.m0 = right;
    m.m1 = back.cross(right);
    m.m2 = back;
    m.m3 = position;
    return m;
}

// Signed yaw from a to b (positive = turning left), ignoring height.
float yawBetween(const Vec3& a, const Vec3& b) {
    const float ca = std::atan2(-a.x, -a.z);
    const float cb = std::atan2(-b.x, -b.z);
    float d = cb - ca;
    while (d > kPi)
        d -= kTwoPi;
    while (d < -kPi)
        d += kTwoPi;
    return d;
}

} // namespace

Traffic::Traffic(const RoadNetwork& network, TrafficLights& lights, std::vector<VehicleData> types,
                 const TrafficSettings& settings, std::uint64_t seed)
    : m_net(network), m_lights(lights), m_types(std::move(types)), m_settings(settings), m_rng(seed) {
    if (m_types.empty()) {
        VehicleData d;
        d.model = "va_sedans_s";
        m_types.push_back(d);
    }
    m_cars.resize(static_cast<std::size_t>(std::max(0, m_settings.maxCars)));
    // Per-vehicle randomisation, in the order the original constructs a car:
    // aiRailSet ctor (lane randomness), aiVehicleSpline ctor (reaction ticks),
    // aiGoalRandomDrive ctor (speed excess, acceleration, separation).
    for (auto& c : m_cars) {
        c.laneRandomness = std::sin(m_rng.frand() * 6.2831f) * 0.5f;    // flt_61B98C, flt_61B990
        c.totReactTicks = 8 - static_cast<int>(m_rng.frand() * -17.0f); // flt_61B9C0, __ftol truncates
        c.exceedLimit = m_exceedCounter + m_exceedCounter;
        m_exceedCounter -= 1.0f;
        if (m_exceedCounter < 0.0f)
            m_exceedCounter = 4.0f;
        c.vehicleAccel = 5.0f - m_rng.frand() * -3.0f; // flt_61BA94 - frand * flt_61BA90
        c.separation = 0.5f - m_rng.frand() * -2.5f;   // flt_61BA9C - frand * flt_61BA98
        c.intersectionReactDist = kIntersectionReactDist;
    }
    m_laneCars.resize(m_net.lanes().size());
    m_turning.resize(m_net.intersections().size());
    m_pathActive.assign(m_net.paths().size(), 0);
}

int Traffic::pickType() {
    if (m_settings.types.empty())
        return m_rng.irand(static_cast<int>(m_types.size()));
    const float r = m_rng.frand();
    for (std::size_t i = 0; i < m_settings.types.size(); ++i) {
        if (r <= m_settings.types[i].cumulative) {
            const auto& model = m_settings.types[i].model;
            for (std::size_t t = 0; t < m_types.size(); ++t)
                if (str::iequals(m_types[t].model, model))
                    return static_cast<int>(t);
        }
    }
    return m_rng.irand(static_cast<int>(m_types.size()));
}

void Traffic::spawnOnLane(int laneId, const Vec3& playerPos, std::vector<int>& freeSlots) {
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(laneId)];
    const PathInfo& path = m_net.paths()[static_cast<std::size_t>(lane.path)];
    // Density: the road's exception if it has one, else the map's; times the
    // player's density setting (MMSTATE AmbientDensity). aiMap::NumCars:
    // ftol(length * density / 8).
    const float density =
        (path.hasException ? path.density : m_settings.mapDensity) * m_settings.densityScale;
    const int count = static_cast<int>(lane.line.length * density / kAmbientCarSpacing);
    if (count <= 0)
        return;
    const float spacing = lane.line.length / static_cast<float>(count + 1);
    // Positions of cars already on this lane (and those placed below).
    std::vector<float> taken;
    for (const Car& o : m_cars)
        if (o.active && !o.physical && o.lane == laneId && !o.turning)
            taken.push_back(o.s);
    for (int i = 1; i <= count && !freeSlots.empty(); ++i) {
        const float s = std::sin(m_rng.frand() * 6.28f) * spacing * 0.5f + static_cast<float>(i) * spacing;
        const Vec3 p = lane.line.pointAt(s);
        if (p.dist2(playerPos) < sq(kAmbientMinSpawnDistance))
            continue; // never pop in near the player
        // Deviation from MM1: the original's jitter (+-half the spacing) can
        // place neighbours on top of each other; skip such spots.
        const float minGap = m_types[0].size.z + 1.5f;
        if (std::ranges::any_of(taken, [&](float t) { return std::abs(t - s) < minGap; }))
            continue;
        if (s > lane.line.length - 6.0f)
            continue; // not past the stop line
        taken.push_back(s);
        Car& c = m_cars[static_cast<std::size_t>(freeSlots.back())];
        freeSlots.pop_back();
        c.active = true;
        c.type = pickType();
        c.variant = static_cast<std::uint16_t>(m_rng.irand(0x7FFF));
        c.lane = laneId;
        c.s = s;
        c.nextLane = -1;
        c.turning = false;
        c.enterInt = false;
        c.stopped = false;
        c.speed = 0.0f; // aiVehicleAmbient::Reset
        c.accel = 0.0f;
        c.targetVelocity = 0.0f;
        c.curReactTicks = 0;
        c.tireRotation = 0.0f;
        c.horn = false;
        c.physical = false;
        c.transform = Mat34::identity();
        c.steerAngle = 0.0f;
        c.stillTime = 0.0f;
        const VehicleData& d = m_types[static_cast<std::size_t>(c.type)];
        // aiVehicleSpline::Init takes bumper distances from the model's box;
        // the AI data's Size is the same box (inferred).
        c.frontBumper = c.backBumper = d.size.z * 0.5f;
        chooseNextLane(c);
        solvePosition(c);
        c.prevPosition = c.transform.m3;
    }
}

void Traffic::despawn(Car& car) {
    car.active = false;
    car.lane = -1;
    car.nextLane = -1;
    car.turning = false;
}

bool Traffic::chooseNextLane(Car& car) {
    car.nextLane = -1;
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    const auto exits = m_net.exits(lane.toIntersection, lane.path);
    car.deadEnd = exits.empty();
    if (exits.empty())
        return false;

    // Turn of each road leaving the intersection, relative to this lane.
    Vec3 inDir;
    lane.line.pointAt(lane.line.length, &inDir);
    struct Option {
        int path;
        float yaw;
    };
    std::vector<Option> options;
    for (int l : exits) {
        const Lane& cand = m_net.lanes()[static_cast<std::size_t>(l)];
        if (std::ranges::any_of(options, [&](const Option& o) { return o.path == cand.path; }))
            continue;
        Vec3 outDir;
        cand.line.pointAt(0.0f, &outDir);
        options.push_back({cand.path, yawBetween(inDir, outDir)});
    }
    // Lane discipline (aiMap::ChooseNextLeftStraightLink / RightStraightLink /
    // StraightLink): on a multi-lane road the lane nearest the centre line may
    // turn across traffic or go straight, the outer lane may turn away from
    // traffic or go straight, middle lanes go straight. Turning "across" is
    // left when driving on the right and right when driving on the left.
    int sameSide = 0;
    for (int l : m_net.paths()[static_cast<std::size_t>(lane.path)].lanes)
        if (m_net.lanes()[static_cast<std::size_t>(l)].side == lane.side)
            ++sameSide;
    const float kStraight = 0.35f;
    auto isLeft = [&](const Option& o) { return o.yaw > kStraight; };
    auto isRight = [&](const Option& o) { return o.yaw < -kStraight; };
    auto isStraight = [&](const Option& o) { return !isLeft(o) && !isRight(o); };
    std::vector<Option> allowed;
    if (sameSide <= 1) {
        allowed = options;
    } else {
        const bool inner = lane.index == 0, outer = lane.index == sameSide - 1;
        const bool across = !m_net.driveOnLeft(); // inner lane turns left on the right-hand side
        for (const auto& o : options) {
            const bool ok = isStraight(o) || (inner && (across ? isLeft(o) : isRight(o))) ||
                            (outer && (across ? isRight(o) : isLeft(o)));
            if (ok)
                allowed.push_back(o);
        }
        if (allowed.empty())
            allowed = options;
    }
    const Option pick = allowed[static_cast<std::size_t>(m_rng.irand(static_cast<int>(allowed.size())))];

    int best = -1, bestDiff = 1 << 30;
    int pathLanes = 0;
    for (int l : exits)
        if (m_net.lanes()[static_cast<std::size_t>(l)].path == pick.path)
            ++pathLanes;
    for (int l : exits) {
        const Lane& cand = m_net.lanes()[static_cast<std::size_t>(l)];
        if (cand.path != pick.path)
            continue;
        // Keep the lane position: inner to inner, outer to outer.
        const int want = sameSide <= 1 ? 0
                                       : (lane.index == sameSide - 1 ? pathLanes - 1
                                                                     : std::min(lane.index, pathLanes - 1));
        const int diff = std::abs(cand.index - want);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = l;
        }
    }
    car.nextLane = best;
    // Indicators (aiGoalRandomDrive::SolveVelocity -> aiRailSet::SolveTurnType).
    car.signal = isLeft(pick) ? TurnSignal::Left : (isRight(pick) ? TurnSignal::Right : TurnSignal::None);
    return best >= 0;
}

void Traffic::startTurn(Car& car) {
    const Lane& from = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    const Lane& to = m_net.lanes()[static_cast<std::size_t>(car.nextLane)];
    Vec3 m0, m1;
    const Vec3 p0 = from.line.pointAt(from.line.length, &m0);
    const Vec3 p1 = to.line.pointAt(0.0f, &m1);
    // Tangent length: the chord between the lane ends (inferred; the
    // original scales aiPath::IntersectionEntry/ExitVector by a distance).
    const float chord = std::max(Vec2{p1.x - p0.x, p1.z - p0.z}.mag(), 0.5f);
    car.curveX = hermite(p0.x, p1.x, m0.x * chord, m1.x * chord);
    car.curveZ = hermite(p0.z, p1.z, m0.z * chord, m1.z * chord);
    car.turnY0 = p0.y;
    car.turnY1 = p1.y;
    float length = 0.0f;
    Vec3 prev = p0, dir;
    for (int i = 1; i <= 16; ++i) {
        Vec3 p;
        solveCurve(car.curveX, car.curveZ, static_cast<float>(i) / 16.0f, p, dir);
        length += Vec2{p.x - prev.x, p.z - prev.z}.mag();
        prev = p;
    }
    car.turnLength = std::max(length, 0.1f);
    car.turning = true;
}

float Traffic::distanceToIntersection(const Car& car) const {
    if (car.turning || car.lane < 0)
        return 0.0f;
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    return lane.line.length - car.s - car.frontBumper;
}

int Traffic::leadCar(const Car& car, float& distance) const {
    distance = std::numeric_limits<float>::max();
    if (car.lane < 0)
        return -1;
    const int self = static_cast<int>(&car - m_cars.data());
    // Position along the rail: lane distance, or negative while turning into
    // nextLane (the turn is treated as a prefix of the next lane).
    auto railPos = [&](const Car& c, int& lane) {
        if (c.turning) {
            lane = c.nextLane;
            return c.turnS - c.turnLength;
        }
        lane = c.lane;
        return c.s;
    };
    int myLane = -1;
    const float myPos = railPos(car, myLane);
    int best = -1;
    auto consider = [&](int laneId, float offset) {
        if (laneId < 0)
            return;
        // Cars on the lane itself.
        for (int idx : m_laneCars[static_cast<std::size_t>(laneId)]) {
            if (idx == self)
                continue;
            const Car& o = m_cars[static_cast<std::size_t>(idx)];
            int ol = -1;
            const float op = railPos(o, ol);
            const float d = op + offset - myPos;
            if (d > 0.0f && d < distance) {
                distance = d;
                best = idx;
            }
        }
    };
    consider(myLane, 0.0f);
    // Cars turning out of the same lane share the start of the turn,
    // whichever road they are heading for.
    {
        const Lane& src = m_net.lanes()[static_cast<std::size_t>(car.lane)];
        if (src.toIntersection >= 0) {
            const float myTurnPos = car.turning ? car.turnS : -(src.line.length - car.s);
            for (int idx : m_turning[static_cast<std::size_t>(src.toIntersection)]) {
                if (idx == self)
                    continue;
                const Car& o = m_cars[static_cast<std::size_t>(idx)];
                if (o.lane != car.lane || !o.turning)
                    continue;
                const float d = o.turnS - myTurnPos;
                if (d > 0.0f && d < distance) {
                    distance = d;
                    best = idx;
                }
            }
        }
    }
    // Near the end of the lane, look into the next lane too.
    if (!car.turning && car.nextLane >= 0) {
        const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
        if (lane.line.length - car.s < kFollowReactDist + 10.0f) {
            // Turning cars are listed on their next lane with negative positions.
            const float turnLen = 10.0f; // approximate turn length for lookahead
            consider(car.nextLane, lane.line.length + turnLen);
        }
    }
    return best;
}

bool Traffic::okayToEnter(Car& car) {
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    switch (lane.rule) {
    case EntryRule::TrafficLight:
        return lane.lightSlot < 0 || m_lights.state(lane.lightSlot) == LightState::Green;
    case EntryRule::StopSign: {
        // Stop, take a ticket, then go in arrival order (lowest ticket among
        // the first cars of the lanes arriving here).
        if (!(car.speed < 0.5f && distanceToIntersection(car) < 1.5f)) // flt_61BA9C, flt_61BAC4
            return false;
        if (!car.stopped) {
            car.stopped = true;
            car.waitTicket = m_ticketCounter;
        }
        const Intersection& node = m_net.intersections()[static_cast<std::size_t>(lane.toIntersection)];
        int first = -1, firstTicket = std::numeric_limits<int>::max();
        for (int laneId : node.incoming) {
            const auto& list = m_laneCars[static_cast<std::size_t>(laneId)];
            // The lane's leader (largest s) that is on the lane, not turning.
            for (int idx : list) {
                const Car& o = m_cars[static_cast<std::size_t>(idx)];
                if (o.turning)
                    continue;
                if (o.stopped && o.waitTicket < firstTicket) {
                    firstTicket = o.waitTicket;
                    first = idx;
                }
                break;
            }
        }
        const int self = static_cast<int>(&car - m_cars.data());
        if (first < 0)
            return false;
        if (first == self)
            return true;
        // A car from the same road has the turn: go together if this car leads its lane.
        const Car& f = m_cars[static_cast<std::size_t>(first)];
        if (m_net.lanes()[static_cast<std::size_t>(f.lane)].path != lane.path)
            return false;
        const auto& mine = m_laneCars[static_cast<std::size_t>(car.lane)];
        return !mine.empty() && mine.front() == self;
    }
    case EntryRule::Uncontrolled:
        return true;
    }
    return true;
}

bool Traffic::roadCapacity(const Car& car) const {
    if (car.nextLane < 0)
        return false;
    // Room for the whole car on the next lane, so it never stops inside the
    // intersection ("don't block the box"; inferred from aiPath::RoadCapacity's
    // use). A slow or stopped last car needs extra room.
    const float need = car.frontBumper + car.backBumper + car.separation + 3.0f;
    for (int idx : m_laneCars[static_cast<std::size_t>(car.nextLane)]) {
        const Car& o = m_cars[static_cast<std::size_t>(idx)];
        if (&o == &car)
            continue;
        const float pos = (o.turning ? o.turnS - o.turnLength : o.s) - o.backBumper;
        const float margin = o.speed < 1.0f ? 4.0f : 0.0f;
        if (pos < need + margin)
            return false;
    }
    // Cars still turning into it from elsewhere count as well.
    return true;
}

bool Traffic::anyVehiclesComingThisWay(const Car& car) const {
    // Cars from other roads still in the first part of their turn through this
    // intersection block entry (inferred simplification of
    // aiGoalRandomDrive::AnyVehiclesComingThisWay).
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    for (int idx : m_turning[static_cast<std::size_t>(lane.toIntersection)]) {
        const Car& o = m_cars[static_cast<std::size_t>(idx)];
        if (&o == &car)
            continue;
        if (m_net.lanes()[static_cast<std::size_t>(o.lane)].path == lane.path)
            continue;
        if (o.turnS < o.turnLength * 0.7f)
            return true;
    }
    // Also cars from other roads that have committed to the intersection and
    // are about to enter it (they keep going after their light changes).
    const Intersection& node = m_net.intersections()[static_cast<std::size_t>(lane.toIntersection)];
    for (int laneId : node.incoming) {
        if (m_net.lanes()[static_cast<std::size_t>(laneId)].path == lane.path)
            continue;
        for (int idx : m_laneCars[static_cast<std::size_t>(laneId)]) {
            const Car& o = m_cars[static_cast<std::size_t>(idx)];
            if (&o == &car || o.turning)
                continue;
            if (o.enterInt && distanceToIntersection(o) < 12.0f && o.speed > 0.5f)
                return true;
            break; // only the lane's leader can be about to enter
        }
    }
    return false;
}

void Traffic::avoidCollision(Car& car, float leadSpeed, float leadAccel, float leadBack, bool leadTurning,
                             float d) {
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    const float limit = lane.speedLimit + car.exceedLimit;
    const float gap = car.frontBumper + leadBack;
    const float matchSpeed = leadSpeed <= limit ? leadSpeed : limit;
    if (d < gap) {
        // Overlapping: stop dead (the original eases off at -v^2/6 when the
        // lead is on rail type 2, which has no counterpart here).
        car.speed = 0.0f;
        car.accel = 0.0f;
        car.targetVelocity = 0.0f;
        return;
    }
    if (d < gap + car.separation) {
        if (leadSpeed > car.speed) {
            car.accel = car.vehicleAccel;
            car.targetVelocity = matchSpeed;
        } else {
            car.speed *= 0.75f; // flt_61BACC
            if (leadAccel > 0.0f) {
                car.accel = leadAccel <= car.vehicleAccel ? leadAccel : car.vehicleAccel;
                car.targetVelocity = matchSpeed;
            } else {
                car.accel = 0.0f;
                car.targetVelocity = 0.0f;
            }
        }
        return;
    }
    if (leadSpeed > car.speed) {
        car.accel = car.vehicleAccel;
        car.targetVelocity = matchSpeed;
        return;
    }
    // Brake to reach the lead's speed exactly at the gap.
    float room = d - (gap + car.separation);
    if (car.turning == leadTurning)
        room -= 2.0f; // flt_61BA80 when both cars are on the same kind of rail
    room = std::max(room, 0.05f);
    car.accel = (leadSpeed * leadSpeed - car.speed * car.speed) / (room + room);
    car.targetVelocity = matchSpeed;
}

void Traffic::solveVelocity(Car& car, float dt) {
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    const float limit = lane.speedLimit + car.exceedLimit;
    const float dist = distanceToIntersection(car);
    float leadDist = std::numeric_limits<float>::max();
    const int leadIdx = leadCar(car, leadDist);
    const Car* lead = leadIdx >= 0 ? &m_cars[static_cast<std::size_t>(leadIdx)] : nullptr;
    const bool reacted = car.curReactTicks > car.totReactTicks;

    auto accelerateToLimit = [&] {
        car.targetVelocity = limit - -0.0001f; // flt_61BAB8
        car.accel = car.vehicleAccel;
    };
    auto followLead = [&] {
        if (leadDist < kFollowReactDist) {
            if (reacted)
                avoidCollision(car, lead->speed, lead->accel, lead->backBumper, lead->turning, leadDist);
        } else if (car.targetVelocity < limit || car.accel < car.vehicleAccel) {
            accelerateToLimit();
        }
    };

    if (dist < car.intersectionReactDist && dist > 0.0f && !car.turning) {
        if (!car.enterInt && okayToEnter(car)) {
            if (car.deadEnd) {
                car.enterInt = true; // nowhere to turn: drive on and leave the populated area
            } else if (car.nextLane >= 0 && roadCapacity(car)) {
                if (!anyVehiclesComingThisWay(car)) {
                    car.enterInt = true;
                    if (car.speed < 0.01f && dist < 0.5f)
                        car.curReactTicks = car.totReactTicks - 3;
                }
            } else {
                chooseNextLane(car);
            }
        }
        if (car.enterInt) {
            if (lead) {
                followLead();
            } else if (reacted && car.targetVelocity < limit) {
                accelerateToLimit();
            }
        } else if (lead && !lead->enterInt && !lead->turning && lead->lane == car.lane) {
            followLead();
        } else {
            // Nothing ahead on this road: stop at the line.
            if (reacted && car.speed <= 2.0f && dist > 1.0f) {
                // Creep forward from a stop that ended short of the line
                // (SolveVelocity: CurSpeed = 2, IntersectionReactDist = 5).
                car.speed = 2.0f;
                car.intersectionReactDist = kRestartReactDist;
                accelerateToLimit();
            } else if (car.targetVelocity != 0.0f) {
                const float d = std::max(dist - 0.25f, 0.05f);
                car.accel = -(car.speed * car.speed) / (d + d);
                car.targetVelocity = 0.0f;
            } else if (car.speed < 0.2f && dist < 0.5f) {
                car.speed = 0.0f;
            }
            // Deviation from MM1: a lead that has already committed to the
            // intersection but is still ahead on this lane is also followed
            // (MM1 only brakes for the stop line here, which lets a car whose
            // next road is full drive into a slow car in front of it).
            if (lead && leadDist < kFollowReactDist && !lead->turning && lead->lane == car.lane) {
                const float accel = car.accel, target = car.targetVelocity, speed = car.speed;
                avoidCollision(car, lead->speed, lead->accel, lead->backBumper, lead->turning, leadDist);
                car.accel = std::min(accel, car.accel);
                car.targetVelocity = std::min(target, car.targetVelocity);
                car.speed = std::min(speed, car.speed);
            }
        }
    } else {
        // Away from intersections (or turning): follow or speed up.
        if (lead) {
            followLead();
        } else if (car.targetVelocity < limit) {
            accelerateToLimit();
        }
    }

    // Integration (SolveVelocity epilogue).
    car.speed += car.accel * dt;
    if ((car.speed < car.targetVelocity - -0.05f && car.accel < 0.0f) ||
        (car.speed > car.targetVelocity && car.accel > 0.0f)) {
        car.speed = car.targetVelocity;
        car.accel = 0.0f;
    }
    if (car.speed < 0.0f)
        car.speed = 0.0f;
    if (lead && lead->speed < 0.01f && car.speed < 0.01f && leadDist < kFollowReactDist)
        car.curReactTicks = 0;
}

void Traffic::advance(Car& car, float dt) {
    const float move = car.speed * dt;
    if (car.turning) {
        car.turnS += move;
        if (car.turnS >= car.turnLength) {
            const float over = car.turnS - car.turnLength;
            car.lane = car.nextLane;
            car.s = over;
            car.turning = false;
            car.enterInt = false;
            car.stopped = false;
            car.intersectionReactDist = kIntersectionReactDist;
            const PathInfo& path = m_net.paths()[static_cast<std::size_t>(
                m_net.lanes()[static_cast<std::size_t>(car.lane)].path)];
            (void)path;
            if (!chooseNextLane(car))
                car.nextLane = -1;
        }
        return;
    }
    car.s += move;
    const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
    if (car.s >= lane.line.length) {
        if (car.nextLane < 0 || !m_pathActive[static_cast<std::size_t>(
                                    m_net.lanes()[static_cast<std::size_t>(car.nextLane)].path)]) {
            despawn(car); // driving out of the populated area
            return;
        }
        const float over = car.s - lane.line.length;
        startTurn(car);
        car.turnS = over;
    }
}

void Traffic::solvePosition(Car& car) {
    Vec3 pos, dir;
    if (car.turning) {
        const float t = clampf(car.turnS / car.turnLength, 0.0f, 1.0f);
        solveCurve(car.curveX, car.curveZ, t, pos, dir);
        pos.y = lerp(car.turnY0, car.turnY1, t);
        const float dy = car.turnY1 - car.turnY0;
        dir.y = dy;
        if (dir.mag2() < 1e-8f)
            dir = Vec3{0, 0, -1};
    } else {
        const Lane& lane = m_net.lanes()[static_cast<std::size_t>(car.lane)];
        pos = lane.line.pointAt(car.s, &dir);
    }
    const Vec3 fwd = dir.normalized();
    // Lateral offset within the lane (aiRailSet::LaneRandomness).
    const Vec3 left{fwd.z, 0.0f, -fwd.x};
    pos += left * car.laneRandomness;
    // Steering from the change of heading (inferred; the original solves
    // wheel angles in aiVehicleSpline::SolvePositionAndOrientation).
    const Vec3 oldFwd = -car.transform.m2;
    const float travelled = Vec2{pos.x - car.transform.m3.x, pos.z - car.transform.m3.z}.mag();
    if (travelled > 1e-3f && travelled < 5.0f) {
        const float yawRate = yawBetween(oldFwd, fwd) / travelled; // curvature
        const float wheelbase = car.frontBumper + car.backBumper - 1.4f;
        car.steerAngle = std::atan(std::max(wheelbase, 1.0f) * yawRate);
    }
    car.transform = frameFromForward(fwd, pos);
}

void Traffic::avoidPlayer(Car& car, const Vec3& playerPos, const Vec3& playerVel, float playerRadius) {
    // aiGoalRandomDrive::Update: within 25 m and in the car's way, the player
    // is treated like a stopped vehicle ahead (MM1 hands over to
    // aiGoalAvoidPlayer, which stops and honks; inferred equivalent).
    car.horn = false;
    const Vec3 rel = playerPos - car.transform.m3;
    if (rel.mag2() > sq(kPlayerZoneDistance))
        return;
    const Vec3 fwd = -car.transform.m2;
    const float ahead = rel.dot(fwd);
    if (ahead <= 0.0f)
        return;
    const VehicleData& d = m_types[static_cast<std::size_t>(car.type)];
    const float lateral = std::abs(rel.dot(car.transform.m0));
    if (lateral > d.size.x * 0.5f + playerRadius * 0.6f)
        return;
    const float playerSpeed = std::max(0.0f, playerVel.dot(fwd));
    if (playerSpeed + 0.5f >= car.speed && ahead > car.frontBumper + playerRadius + car.separation + 4.0f)
        return;
    // Only ever brake harder than the car already does (e.g. for a red light).
    const float accel = car.accel, target = car.targetVelocity, speed = car.speed;
    avoidCollision(car, playerSpeed, 0.0f, playerRadius, false, ahead);
    car.accel = std::min(accel, car.accel);
    car.targetVelocity = std::min(target, car.targetVelocity);
    car.speed = std::min(speed, car.speed);
    car.horn = car.speed < 0.5f;
}

void Traffic::rebuildLaneLists() {
    for (auto& l : m_laneCars)
        l.clear();
    for (auto& t : m_turning)
        t.clear();
    for (std::size_t i = 0; i < m_cars.size(); ++i) {
        const Car& c = m_cars[i];
        if (!c.active || c.physical || c.lane < 0)
            continue;
        if (c.turning) {
            m_laneCars[static_cast<std::size_t>(c.nextLane)].push_back(static_cast<int>(i));
            const Lane& lane = m_net.lanes()[static_cast<std::size_t>(c.lane)];
            if (lane.toIntersection >= 0)
                m_turning[static_cast<std::size_t>(lane.toIntersection)].push_back(static_cast<int>(i));
        } else {
            m_laneCars[static_cast<std::size_t>(c.lane)].push_back(static_cast<int>(i));
        }
    }
    for (auto& l : m_laneCars) {
        std::ranges::sort(l, [&](int a, int b) {
            const Car& ca = m_cars[static_cast<std::size_t>(a)];
            const Car& cb = m_cars[static_cast<std::size_t>(b)];
            const float pa = ca.turning ? ca.turnS - ca.turnLength : ca.s;
            const float pb = cb.turning ? cb.turnS - cb.turnLength : cb.s;
            return pa > pb || (pa == pb && a < b);
        });
    }
}

void Traffic::updateActivePaths(const Vec3& playerPos) {
    std::vector<int> freeSlots;
    for (int i = static_cast<int>(m_cars.size()) - 1; i >= 0; --i)
        if (!m_cars[static_cast<std::size_t>(i)].active)
            freeSlots.push_back(i);
    const float r2 = sq(m_settings.activeRadius);
    for (std::size_t p = 0; p < m_net.paths().size(); ++p) {
        const PathInfo& path = m_net.paths()[p];
        bool active = m_populateAll;
        if (!active) {
            const Vec3 closest = vmax(path.bounds.min, vmin(playerPos, path.bounds.max));
            active = Vec2{closest.x - playerPos.x, closest.z - playerPos.z}.mag2() < r2;
        }
        const bool was = m_pathActive[p] != 0;
        if (active && !was) {
            m_pathActive[p] = 1;
            for (int lane : path.lanes)
                spawnOnLane(lane, playerPos, freeSlots);
        } else if (!active && was) {
            m_pathActive[p] = 0;
            for (auto& c : m_cars)
                if (c.active && !c.physical && !c.turning && c.lane >= 0 &&
                    m_net.lanes()[static_cast<std::size_t>(c.lane)].path == static_cast<int>(p))
                    despawn(c);
        }
    }
}

void Traffic::topUp(const Vec3& playerPos) {
    // Keep the populated area at its density as cars leave it: lanes with
    // fewer cars than their share get one more, out of the player's sight
    // (inferred; replaces cars recycled by the original's road map).
    m_activeLanes.clear();
    for (std::size_t p = 0; p < m_net.paths().size(); ++p)
        if (m_pathActive[p])
            for (int l : m_net.paths()[p].lanes)
                m_activeLanes.push_back(l);
    if (m_activeLanes.empty())
        return;
    std::vector<int> freeSlots;
    for (int i = static_cast<int>(m_cars.size()) - 1; i >= 0; --i)
        if (!m_cars[static_cast<std::size_t>(i)].active)
            freeSlots.push_back(i);
    for (int tries = 0; tries < 8 && !freeSlots.empty(); ++tries) {
        const int laneId =
            m_activeLanes[static_cast<std::size_t>(m_rng.irand(static_cast<int>(m_activeLanes.size())))];
        const Lane& lane = m_net.lanes()[static_cast<std::size_t>(laneId)];
        const PathInfo& path = m_net.paths()[static_cast<std::size_t>(lane.path)];
        const float density =
            (path.hasException ? path.density : m_settings.mapDensity) * m_settings.densityScale;
        const int target = static_cast<int>(lane.line.length * density / kAmbientCarSpacing);
        if (static_cast<int>(m_laneCars[static_cast<std::size_t>(laneId)].size()) >= target)
            continue;
        const float s = 2.0f + m_rng.frand() * std::max(0.0f, lane.line.length - 10.0f);
        const Vec3 p = lane.line.pointAt(s);
        if (p.dist2(playerPos) < sq(kAmbientMinSpawnDistance))
            continue;
        bool clear = true;
        for (int idx : m_laneCars[static_cast<std::size_t>(laneId)]) {
            const Car& o = m_cars[static_cast<std::size_t>(idx)];
            const float pos = o.turning ? o.turnS - o.turnLength : o.s;
            if (std::abs(pos - s) < m_types[0].size.z + 8.0f)
                clear = false;
        }
        if (!clear)
            continue;
        // Reuse the lane spawner with a single car at s.
        Car& c = m_cars[static_cast<std::size_t>(freeSlots.back())];
        freeSlots.pop_back();
        c.active = true;
        c.type = pickType();
        c.variant = static_cast<std::uint16_t>(m_rng.irand(0x7FFF));
        c.lane = laneId;
        c.s = s;
        c.nextLane = -1;
        c.turning = false;
        c.enterInt = false;
        c.stopped = false;
        c.speed = lane.speedLimit; // already moving: it drove in from elsewhere
        c.accel = 0.0f;
        c.targetVelocity = lane.speedLimit + c.exceedLimit;
        c.curReactTicks = c.totReactTicks + 1;
        c.tireRotation = 0.0f;
        c.horn = false;
        c.physical = false;
        c.transform = Mat34::identity();
        c.steerAngle = 0.0f;
        c.stillTime = 0.0f;
        c.frontBumper = c.backBumper = m_types[static_cast<std::size_t>(c.type)].size.z * 0.5f;
        c.intersectionReactDist = kIntersectionReactDist;
        chooseNextLane(c);
        solvePosition(c);
        c.prevPosition = c.transform.m3;
        m_laneCars[static_cast<std::size_t>(laneId)].push_back(static_cast<int>(&c - m_cars.data()));
    }
}

void Traffic::populateAll() {
    m_populateAll = true;
}

void Traffic::step(float dt, const Vec3& playerPos, const Vec3& playerVel, float playerRadius) {
    ++m_ticketCounter;
    updateActivePaths(playerPos);
    rebuildLaneLists();
    m_topUpTimer += dt;
    if (m_topUpTimer >= 1.0f) {
        m_topUpTimer = 0.0f;
        topUp(playerPos);
        rebuildLaneLists();
    }

    // Green lights restart the queues behind them (aiPath::ResetVehicleReactTicks).
    m_lights.forEachNewGreen([&](int slot) {
        for (const Lane& lane : m_net.lanes()) {
            if (lane.lightSlot != slot)
                continue;
            for (int idx : m_laneCars[static_cast<std::size_t>(lane.id)]) {
                Car& c = m_cars[static_cast<std::size_t>(idx)];
                if (c.turning)
                    continue;
                if (c.speed >= 0.01f) // flt_61B9C8
                    break;
                c.curReactTicks = 0;
            }
        }
    });

    for (auto& c : m_cars) {
        if (!c.active || c.physical || c.lane < 0)
            continue;
        solveVelocity(c, dt);
        avoidPlayer(c, playerPos, playerVel, playerRadius);
        c.prevPosition = c.transform.m3;
        advance(c, dt);
        if (!c.active)
            continue;
        solvePosition(c);
        // Recycle cars that have been stuck for a long time out of sight
        // (gridlock relief; inferred, the original's mechanism is unknown).
        c.stillTime = c.speed < 0.1f ? c.stillTime + dt : 0.0f;
        if (c.stillTime > kGridlockSeconds &&
            c.transform.m3.dist2(playerPos) > sq(kAmbientMinSpawnDistance)) {
            despawn(c);
            continue;
        }
        // aiVehicleSpline::Update: tyre rotation advances with distance.
        c.tireRotation += dt * c.speed;
        if (c.tireRotation > kTireRotationWrap)
            c.tireRotation -= kTireRotationWrap;
        ++c.curReactTicks;
    }
    publish();
}

void Traffic::publish() {
    m_public.clear();
    for (std::size_t i = 0; i < m_cars.size(); ++i) {
        const Car& c = m_cars[i];
        if (!c.active)
            continue;
        AmbientCar a;
        a.id = static_cast<int>(i);
        a.data = &m_types[static_cast<std::size_t>(c.type)];
        a.model = a.data->model;
        a.variant = c.variant;
        a.transform = c.transform;
        a.speed = c.speed;
        a.velocity = -c.transform.m2 * c.speed;
        a.tireRotation = c.tireRotation;
        a.steer = c.steerAngle;
        a.braking = c.accel < -0.5f || (c.speed < 0.5f && c.targetVelocity == 0.0f);
        a.signal = c.signal;
        a.horn = c.horn;
        a.physical = c.physical;
        m_public.push_back(a);
    }
}

std::size_t Traffic::activeCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_cars, [](const Car& c) { return c.active; }));
}

void Traffic::impact(int carId, const Vec3& impulse) {
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return;
    Car& c = m_cars[static_cast<std::size_t>(carId)];
    if (!c.active || c.physical)
        return;
    c.physical = true;
    publish();
    if (m_onImpact) {
        for (auto& a : m_public)
            if (a.id == carId)
                m_onImpact(a, impulse);
    }
}

void Traffic::release(int carId) {
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return;
    despawn(m_cars[static_cast<std::size_t>(carId)]);
}

Traffic::DebugCar Traffic::debug(int carId) const {
    DebugCar d;
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return d;
    const Car& c = m_cars[static_cast<std::size_t>(carId)];
    d.lane = c.lane;
    d.nextLane = c.nextLane;
    d.s = c.turning ? c.turnS : c.s;
    d.turning = c.turning;
    d.entered = c.enterInt;
    d.accel = c.accel;
    d.targetVelocity = c.targetVelocity;
    d.reactTicks = c.curReactTicks;
    d.totReactTicks = c.totReactTicks;
    if (c.active && c.lane >= 0)
        d.lead = leadCar(c, d.leadDistance);
    return d;
}

} // namespace mm2::ai

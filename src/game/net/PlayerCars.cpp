// The players' cars of a network race, simulated by the host (see
// PlayerCars.h and docs/multiplayer.md, "Players' cars").

#include "game/net/PlayerCars.h"

#include "game/Interpolation.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace mm2::game {

// --- Inputs --------------------------------------------------------------------------

net::CarInputFrame inputFrame(const phys::PedalInput& recorded) {
    // The pedals as mmReplayManager::Update records them (controls::
    // replayQuantize): steering x 127 in a signed byte, the pedals x 255.
    const auto pedal = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    net::CarInputFrame f;
    f.steering = static_cast<std::int8_t>(std::clamp(std::lround(recorded.steering * 127.0f), -127L, 127L));
    f.throttle = pedal(recorded.accelerator);
    f.brake = pedal(recorded.brake);
    f.handbrake = pedal(recorded.handbrake);
    return f;
}

phys::PedalInput pedalsOf(const net::CarInputFrame& f) {
    // GetSteering / GetThrottle / GetBrakes / GetHandBrakes read the bytes
    // back x 1/127 and x 1/255.
    phys::PedalInput p;
    p.steering = static_cast<float>(static_cast<int>(f.steering)) * 0.007874016f;
    p.accelerator = static_cast<float>(f.throttle) * 0.003921569f;
    p.brake = static_cast<float>(f.brake) * 0.003921569f;
    p.handbrake = static_cast<float>(f.handbrake) * 0.003921569f;
    return p;
}

void NetCarDriver::attach(const SimVehicle& car) {
    m_baseMass = car.sim().body.ics.mass;
    m_extraMass = 0;
}

bool NetCarDriver::apply(SimVehicle& car, const net::CarInputFrame& in) {
    phys::CarSim& sim = car.sim();
    // mmPlayer::UpdateRegen (Cops and Robbers, while the car carries no gold).
    const bool cleared = (in.flags & net::kInputRegen) && sim.regenerate();
    phys::Transmission& trans = sim.trans;
    phys::ArcadeControls& controls = car.controls();
    // mmGame::UpdateGameInput's gearbox keys, in its order: automatic <->
    // manual (a reverse taken with the swapped pedals goes back to drive
    // first), shift up, shift down, reverse (or first from reverse).
    const bool automatic = (in.flags & net::kInputAutomatic) != 0;
    if (automatic != trans.isAutomatic) {
        if (controls.swapThrottle)
            trans.setDrive();
        controls.swapThrottle = false;
        trans.automatic(automatic);
    }
    if ((in.events & net::kInputShiftUp) && !trans.isAutomatic)
        trans.upshift();
    if ((in.events & net::kInputShiftDown) && !trans.isAutomatic)
        trans.downshift();
    if (in.events & net::kInputReverse) {
        controls.swapThrottle = false;
        trans.setCurrentGear(trans.currentGear != phys::Transmission::kReverse ? phys::Transmission::kReverse
                                                                               : phys::Transmission::kFirst);
    }
    controls.autoReverse = (in.flags & net::kInputAutoReverse) != 0;
    // mmMultiCR::FondleCarMass: the gold's mass on the carrier
    // (phInertialCS::Init with the mass changed).
    if (in.extraMass != m_extraMass) {
        phys::InertialCS& ics = sim.body.ics;
        ics.init(m_baseMass + static_cast<float>(in.extraMass), ics.inertia.x, ics.inertia.y, ics.inertia.z);
        m_extraMass = in.extraMass;
    }
    // mmGame::UpdateSteeringBrakes, network games: in a forward gear the
    // throttle is capped (the gold's weight).
    phys::PedalInput pedals = pedalsOf(in);
    if (trans.getCurrentGear() > 0)
        pedals.accelerator =
            std::clamp(pedals.accelerator, 0.0f, static_cast<float>(in.throttleCap) * 0.003921569f);
    const bool finished = (in.flags & net::kInputFinished) != 0;
    sim.raceFinished = finished;
    if (finished)
        car.drive({});
    else if (in.flags & net::kInputHeld)
        car.hold(pedals);
    else
        car.drive(pedals);
    return cleared;
}

void NetCarDriver::command(SimVehicle& car, const net::CarCommand& c) {
    switch (c.kind) {
    case net::CarCommandKind::Reset: car.reset(); return; // mmPlayer::Reset
    case net::CarCommandKind::ResetTo:
        car.sim().setResetPosRaw(c.position);
        car.sim().resetRotation = c.rotation;
        car.reset();
        return;
    case net::CarCommandKind::RespawnAt: car.respawnAt(c.position, c.rotation); return;
    case net::CarCommandKind::ClearDamage: car.sim().damage.reset(); return; // vehCar::ClearDamage
    }
}

// --- The host ------------------------------------------------------------------------

void HostInputQueue::receive(const net::PlayerInputMsg& msg) {
    if (msg.frames.empty() || msg.first == 0)
        return;
    const auto count = static_cast<std::uint32_t>(msg.frames.size());
    if (m_next == 0) {
        // The first message: its first input comes first (once a few are in
        // hand, ready()).
        m_next = msg.first;
        m_last = msg.frames.front();
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t seq = msg.first + i;
        if (seq < m_next || seq > m_next + kMaxAhead)
            continue; // already applied (a repeat, or too late), or far ahead
        m_frames[seq] = msg.frames[i];
        m_newest = std::max(m_newest, seq);
    }
    for (const auto& c : msg.commands)
        if (c.seq > m_lastCommand && c.seq <= m_next + kMaxAhead && m_commands.size() < 16)
            m_commands[c.seq] = c;
}

std::optional<HostInputQueue::Next> HostInputQueue::next() {
    if (m_next == 0)
        return std::nullopt;
    Next n;
    n.seq = m_next;
    if (const auto it = m_frames.find(m_next); it != m_frames.end()) {
        n.frame = it->second;
        n.real = true;
        m_last = n.frame;
        m_starved = 0;
        m_frames.erase(m_frames.begin(), std::next(it));
    } else {
        // Late or lost: the last input again (without its keys), then the
        // car coasts.
        ++m_missed;
        ++m_starved;
        n.frame = m_last;
        n.frame.events = 0;
        if (m_starved > kRepeatSamples) {
            n.frame.steering = 0;
            n.frame.throttle = 0;
            n.frame.brake = 0;
            n.frame.handbrake = 0;
        }
    }
    // The commands due by this sample (a late one at once).
    for (auto it = m_commands.begin(); it != m_commands.end() && it->first <= m_next;) {
        n.commands.push_back(it->second);
        m_lastCommand = std::max(m_lastCommand, it->first);
        it = m_commands.erase(it);
    }
    const std::int32_t waiting = static_cast<std::int32_t>(m_newest) - static_cast<std::int32_t>(m_next);
    m_leastWaiting = std::min(m_leastWaiting, waiting);
    ++m_next;
    // Too far behind its client for a whole second: the newest few only.
    m_slackLeast = std::min(m_slackLeast, waiting);
    if (++m_slackSamples >= kSlackSamples) {
        if (m_slackLeast > kMaxSlack) {
            const std::uint32_t to = m_newest + 1 - kStartMargin;
            m_skipped += to - m_next;
            m_frames.erase(m_frames.begin(), m_frames.lower_bound(to));
            m_next = to;
        }
        m_slackLeast = 1 << 20;
        m_slackSamples = 0;
    }
    return n;
}

std::optional<net::CarCommand> HostInputQueue::placement() const {
    for (const auto& [seq, c] : m_commands)
        if (c.kind == net::CarCommandKind::ResetTo || c.kind == net::CarCommandKind::RespawnAt)
            return c;
    return std::nullopt;
}

std::int32_t HostInputQueue::takeLeastWaiting() {
    const std::int32_t w = m_leastWaiting;
    m_leastWaiting = 1 << 20;
    if (w == (1 << 20))
        return static_cast<std::int32_t>(m_newest) - static_cast<std::int32_t>(m_next) + 1;
    return w;
}

net::OwnCarState ownCarState(const SimVehicle& car, std::uint32_t resets) {
    const phys::CarSim& sim = car.sim();
    const phys::InertialCS& ics = sim.body.ics;
    net::OwnCarState s;
    s.matrix = ics.matrix;
    s.linearMomentum = ics.linearMomentum;
    s.angularMomentum = ics.angularMomentum;
    s.linearVelocity = ics.linearVelocity;
    s.angularVelocity = ics.angularVelocity;
    s.lastPush = ics.lastPush;
    for (std::size_t i = 0; i < 4; ++i) {
        const phys::Wheel& w = sim.wheels[i];
        s.wheels[i] = {w.rotationSpeed, w.rotation,           w.suspension,
                       w.suspensionVelocity, w.currentTireDispLat, w.currentTireDispLong};
    }
    s.engineSpeed = sim.engine.rotationSpeed;
    s.gearChangeTime = sim.engine.gearChangeTime;
    s.prevGearRpm = sim.engine.prevGearRPM;
    s.changingGear = sim.engine.changingGear;
    s.gear = sim.trans.currentGear;
    s.automatic = sim.trans.isAutomatic;
    s.gearChanged = sim.trans.gearChanged;
    s.timeInGear = sim.trans.timeInGear;
    for (std::size_t i = 0; i < 3; ++i)
        s.drivetrainSpeed[i] = sim.drivetrains[i].rotationSpeed;
    s.damage = sim.damage.currentDamage;
    s.stuckState = sim.stuck.state;
    s.stuckActive = sim.stuck.active;
    s.stuckTime = sim.stuck.stuckTime;
    s.stuckPosition = sim.stuck.impactPosition;
    s.random = sim.randomState;
    s.swapThrottle = car.controls().swapThrottle;
    s.held = car.held();
    s.resets = resets;
    s.force = ics.linearForce;
    s.torque = ics.angularTorque;
    for (std::size_t i = 0; i < 4; ++i)
        s.tireResistance[i] = sim.wheels[i].tireResistance;
    s.linearImpulse = ics.linearImpulse;
    s.angularImpulse = ics.angularImpulse;
    s.linearPush = ics.linearPush;
    s.turnForce = ics.turnForce;
    s.framePush = ics.framePush;
    const Vec3 zero{};
    s.contact = !(s.linearImpulse == zero && s.angularImpulse == zero && s.linearPush == zero &&
                  s.turnForce == zero && s.framePush == zero);
    return s;
}

void applyOwnCarState(SimVehicle& car, const net::OwnCarState& s) {
    phys::CarSim& sim = car.sim();
    phys::InertialCS& ics = sim.body.ics;
    ics.matrix = s.matrix;
    ics.linearMomentum = s.linearMomentum;
    ics.angularMomentum = s.angularMomentum;
    ics.linearVelocity = s.linearVelocity;
    ics.angularVelocity = s.angularVelocity;
    ics.lastPush = s.lastPush;
    ics.linearForce = s.force;
    ics.angularTorque = s.torque;
    ics.linearImpulse = s.linearImpulse;
    ics.angularImpulse = s.angularImpulse;
    ics.linearPush = s.linearPush;
    ics.turnForce = s.turnForce;
    ics.framePush = s.framePush;
    for (std::size_t i = 0; i < 4; ++i)
        sim.wheels[i].tireResistance = s.tireResistance[i];
    for (std::size_t i = 0; i < 4; ++i) {
        phys::Wheel& w = sim.wheels[i];
        const auto& o = s.wheels[i];
        w.rotationSpeed = o.rotationSpeed;
        w.rotation = o.rotation;
        w.suspension = o.suspension;
        w.suspensionVelocity = o.suspensionVelocity;
        w.currentTireDispLat = o.tireDispLat;
        w.currentTireDispLong = o.tireDispLong;
    }
    sim.engine.rotationSpeed = s.engineSpeed;
    sim.engine.gearChangeTime = s.gearChangeTime;
    sim.engine.prevGearRPM = s.prevGearRpm;
    sim.engine.changingGear = s.changingGear;
    sim.trans.currentGear = std::clamp(s.gear, 0, phys::Transmission::kSlots - 1);
    sim.trans.isAutomatic = s.automatic;
    sim.trans.gearChanged = s.gearChanged;
    sim.trans.timeInGear = s.timeInGear;
    for (std::size_t i = 0; i < 3; ++i)
        sim.drivetrains[i].rotationSpeed = s.drivetrainSpeed[i];
    sim.damage.currentDamage = s.damage;
    sim.stuck.state = s.stuckState;
    sim.stuck.active = s.stuckActive;
    sim.stuck.stuckTime = s.stuckTime;
    sim.stuck.impactPosition = s.stuckPosition;
    sim.randomState = s.random;
    car.controls().swapThrottle = s.swapThrottle;
    car.setHeld(s.held);
    // The bound follows the body; the next sweep starts where it is (the
    // host's bound matrix does not travel: it differs from the body's only
    // by a push during a contact).
    sim.body.syncBoundMatrix();
    sim.body.collider.lastMatrix = sim.body.boundMatrix;
}

bool ResetRules::allows(const net::CarCommand& c) const {
    const auto inCity = [this](const Vec3& at) {
        constexpr float kMargin = 200.0f;
        return at.x >= city.min.x - kMargin && at.x <= city.max.x + kMargin && at.y >= city.min.y - kMargin &&
               at.y <= city.max.y + kMargin && at.z >= city.min.z - kMargin && at.z <= city.max.z + kMargin;
    };
    switch (c.kind) {
    case net::CarCommandKind::ResetTo: return !placed && inCity(c.position);
    case net::CarCommandKind::ClearDamage: return placed && (debug || wrecked || copsAndRobbers);
    case net::CarCommandKind::Reset:
    case net::CarCommandKind::RespawnAt: break;
    }
    if (!placed || c.seq < lastMoveSeq + kMoveInterval)
        return false;
    if (c.kind == net::CarCommandKind::RespawnAt) {
        if (!inCity(c.position))
            return false;
        const bool atCheckpoint = std::ranges::any_of(respawnPoints, [&](const Mat34& at) {
            return at.m3.dist2(c.position) < 0.25f &&
                   std::abs(std::remainder(phys::resetRotationOf(at) - c.rotation, 6.2831853f)) < 0.05f;
        });
        if (!atCheckpoint && !debug)
            return false;
    }
    return debug || waterSamples >= kWaterSamples || height < kDropHeight;
}

net::VehicleSnapshot carSnapshot(const SimVehicle& car, const net::CarInputFrame& input) {
    const phys::CarSim& sim = car.sim();
    const Mat34 model = sim.modelMatrix();
    net::VehicleSnapshot s;
    s.position = model.m3;
    s.orientation = Quat::fromMatrix(model);
    s.linearVelocity = sim.body.ics.frameVelocity;
    s.angularVelocity = sim.body.ics.angularVelocity;
    const phys::PedalInput pedals = pedalsOf(input);
    s.controls.steering = pedals.steering;
    s.controls.throttle = sim.engine.throttle;
    s.controls.brake = sim.brakes;
    s.controls.handbrake = sim.handBrake;
    s.controls.gear = static_cast<std::int8_t>(sim.trans.getCurrentGear());
    s.damage = std::clamp(sim.damage.damage, 0.0f, 1.0f);
    if (input.flags & net::kInputHorn)
        s.flags |= net::kVehicleHorn;
    if (input.flags & net::kInputHeadlights)
        s.flags |= net::kVehicleHeadlights;
    if (sim.brakes != 0.0f)
        s.flags |= net::kVehicleBrakeLights;
    if (sim.damage.wrecked())
        s.flags |= net::kVehicleWrecked;
    return s;
}

// --- The client ----------------------------------------------------------------------

void CarPrediction::save(Entry& e, const SimVehicle& car, const NetCarDriver& driver) const {
    e.car = car.sim().saveState();
    if (const phys::Trailer* t = car.trailer())
        e.trailer = t->saveState();
    else
        e.trailer.reset();
    e.controls = car.controls();
    e.held = car.held();
    e.driver = driver;
}

void CarPrediction::restore(const Entry& e, SimVehicle& car, NetCarDriver& driver) {
    car.sim().restoreState(e.car);
    if (phys::Trailer* t = car.trailer(); t && e.trailer)
        t->restoreState(*e.trailer);
    car.controls() = e.controls;
    car.setHeld(e.held);
    driver = e.driver;
}

void CarPrediction::command(SimVehicle& car, net::CarCommand c) {
    c.seq = m_next;
    NetCarDriver::command(car, c);
    m_pending.push_back(c);
    m_unacked.push_back(c);
    if (m_unacked.size() > net::kMaxCarCommands)
        m_unacked.erase(m_unacked.begin());
}

void CarPrediction::beginSample(SimVehicle& car, NetCarDriver& driver, const net::CarInputFrame& input) {
    Entry e;
    e.seq = m_next;
    e.input = input;
    e.commands = std::exchange(m_pending, {});
    driver.apply(car, input);
    m_history.push_back(std::move(e));
}

void CarPrediction::endSample(const SimVehicle& car, const NetCarDriver& driver) {
    if (m_history.empty() || m_history.back().seq != m_next)
        return;
    save(m_history.back(), car, driver);
    ++m_next;
    while (m_history.size() > m_options.history)
        m_history.pop_front();
}

std::optional<net::PlayerInputMsg> CarPrediction::message() const {
    if (m_history.empty() || m_history.back().seq != m_next - 1)
        return std::nullopt;
    const std::uint32_t newest = m_history.back().seq;
    std::uint32_t first = std::max(m_acked + 1, m_history.front().seq);
    if (newest + 1 - first > net::kMaxInputFrames)
        first = newest + 1 - static_cast<std::uint32_t>(net::kMaxInputFrames);
    if (first > newest)
        first = newest; // everything acknowledged: the newest again keeps the host's clock fed
    net::PlayerInputMsg msg;
    msg.first = first;
    for (const Entry& e : m_history)
        if (e.seq >= first)
            msg.frames.push_back(e.input);
    msg.commands = m_unacked;
    return msg;
}

namespace {

// A companion's body, its host state's place and velocity, putting it to
// that state and driving it for a sample (a player's car, or OpenMM2's
// shared traffic's police car or knocked car).
phys::Body& companionBody(const CarPrediction::Companion& c) {
    return c.body ? *c.body : c.car->sim().body;
}
Vec3 companionPosition(const CarPrediction::Companion& c) {
    return c.state ? c.state->matrix.m3 : c.position;
}
Vec3 companionVelocity(const CarPrediction::Companion& c) {
    return c.state ? c.state->linearVelocity : c.velocity;
}
void rebaseCompanion(const CarPrediction::Companion& c) {
    if (c.rebase)
        c.rebase();
    else if (c.car && c.state)
        applyOwnCarState(*c.car, *c.state);
}
void driveCompanion(const CarPrediction::Companion& c) {
    if (c.drive)
        c.drive();
    else if (c.driver && c.car)
        c.driver->apply(*c.car, c.input);
}

} // namespace

CarPrediction::Correction CarPrediction::acknowledge(SimVehicle& car, NetCarDriver& driver,
                                                     phys::World& world, std::uint32_t ack,
                                                     const net::OwnCarState& host,
                                                     const std::function<void()>& beforeLast,
                                                     const std::function<void(std::uint32_t)>& beforeEach,
                                                     std::span<const Companion> companions) {
    Correction out;
    if (ack <= m_acked)
        return out; // an older or repeated state
    m_acked = ack;
    ++m_stats.acks;
    std::erase_if(m_unacked, [ack](const net::CarCommand& c) { return c.seq <= ack; });
    if (m_history.empty() || ack < m_history.front().seq || ack > m_history.back().seq) {
        ++m_stats.unreplayable;
        return out;
    }
    const std::size_t index = ack - m_history.front().seq;
    Entry& base = m_history[index];
    const phys::InertialCS& predicted = base.car.ics;
    out.positionError = (predicted.matrix.m3 - host.matrix.m3).mag();
    out.velocityError = (predicted.linearVelocity - host.linearVelocity).mag();
    float rotation = 0.0f;
    for (const auto& [a, b] : {std::pair{predicted.matrix.m0, host.matrix.m0},
                               std::pair{predicted.matrix.m1, host.matrix.m1},
                               std::pair{predicted.matrix.m2, host.matrix.m2}})
        rotation = std::max({rotation, std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
    out.rotationError = rotation;
    out.damage = base.car.damage.currentDamage != host.damage;
    out.held = base.held != host.held;
    out.gear = base.car.trans.currentGear != host.gear;
    const bool differs = out.positionError > m_options.positionTolerance ||
                         out.velocityError > m_options.velocityTolerance ||
                         rotation > m_options.rotationTolerance || out.damage || out.held || out.gear;
    const std::size_t later = m_history.size() - 1 - index;
    const bool replay = differs || !companions.empty();
    if (replay && later > m_options.maxReplay) {
        ++m_stats.unreplayable;
        m_history.erase(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(index));
        return out;
    }
    // Companions that cannot meet the car before the newest sample (farther
    // from it, at the acknowledged sample and now, than companionReach and
    // the way their speeds close in the samples between) run again alone,
    // the car held where each sample had it: the car's own prediction is
    // then not run again against bodies held still (a prop it pushed in
    // those samples meets it as a wall when they run again).
    bool alone = !differs && !companions.empty();
    const float span = static_cast<float>(later + 1) * phys::kFixedSampleStep;
    const Vec3 carNow = car.sim().body.ics.matrix.m3;
    for (const Companion& c : companions) {
        const float closing = (companionVelocity(c) - predicted.linearVelocity).mag() * span;
        const float reach = m_options.companionReach + closing;
        if (companionPosition(c).dist2(predicted.matrix.m3) < reach * reach ||
            companionBody(c).ics.matrix.m3.dist2(carNow) < reach * reach)
            alone = false;
    }
    if (replay && alone) {
        std::vector<phys::Body*> bodies;
        for (const Companion& c : companions) {
            rebaseCompanion(c);
            bodies.push_back(&companionBody(c));
        }
        // (In the world's order: the companions come in player order.)
        world.beginReplay(world.time() -
                          static_cast<double>(later) * static_cast<double>(phys::kFixedSampleStep));
        for (std::size_t k = index + 1; k < m_history.size(); ++k) {
            restore(m_history[k - 1], car, driver); // the car as the sample met it
            for (const Companion& c : companions)
                driveCompanion(c);
            if (beforeEach)
                beforeEach(m_history[k].seq);
            if (k + 1 == m_history.size() && beforeLast)
                beforeLast();
            world.replaySample(bodies, phys::kFixedSampleStep);
            ++out.replayed;
        }
        restore(m_history.back(), car, driver);
        for (const auto& c : m_pending)
            NetCarDriver::command(car, c);
        out.rebased = true;
        m_stats.replayedSamples += static_cast<std::uint64_t>(out.replayed);
    } else if (replay) {
        const Vec3 before = car.sim().modelMatrix().m3;
        restore(base, car, driver);
        if (differs) {
            applyOwnCarState(car, host);
            save(base, car, driver);
        }
        // The bodies in the world's order (the players' cars by number), so
        // that they collide in the order they do on the host.
        std::vector<phys::Body*> bodies;
        for (const Companion& c : companions)
            if (c.first)
                bodies.push_back(&companionBody(c));
        bodies.push_back(&car.sim().body);
        if (phys::Trailer* t = car.trailer())
            bodies.push_back(&t->body);
        for (const Companion& c : companions)
            if (!c.first)
                bodies.push_back(&companionBody(c));
        for (const Companion& c : companions)
            rebaseCompanion(c);
        // OpenMM2: the props the replay meets (World::collideHeld), the samples
        // having run for real one after another up to the world's time now.
        world.beginReplay(world.time() -
                          static_cast<double>(later) * static_cast<double>(phys::kFixedSampleStep));
        for (std::size_t k = index + 1; k < m_history.size(); ++k) {
            Entry& e = m_history[k];
            for (const auto& c : e.commands)
                NetCarDriver::command(car, c);
            driver.apply(car, e.input);
            for (const Companion& c : companions)
                driveCompanion(c);
            if (beforeEach)
                beforeEach(e.seq);
            if (k + 1 == m_history.size() && beforeLast)
                beforeLast();
            world.replaySample(bodies, phys::kFixedSampleStep);
            save(e, car, driver);
            ++out.replayed;
        }
        // The resets asked for the next sample stand again.
        for (const auto& c : m_pending)
            NetCarDriver::command(car, c);
        out.corrected = differs;
        out.rebased = !companions.empty();
        out.moved = car.sim().modelMatrix().m3 - before;
        if (differs)
            ++m_stats.corrections;
        m_stats.replayedSamples += static_cast<std::uint64_t>(out.replayed);
    }
    m_history.erase(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(index));
    return out;
}

// --- CorrectionBlend -----------------------------------------------------------------

namespace {

Mat34 rotationOf(const Mat34& m) {
    Mat34 r = m;
    r.m3 = {};
    return r;
}

} // namespace

void CorrectionBlend::add(const Mat34& before, const Mat34& after) {
    // `before` is where the car is drawn (what is still being drawn away
    // included): the offset takes the drawn car there from the corrected one.
    const Vec3 moved = before.m3 - after.m3;
    if (moved.mag() > snapDistance) {
        clear();
        return;
    }
    m_position = moved;
    // before = D x after (the drawn car keeps its turn): D = before x after^T.
    m_rotation = Mat34::mul(rotationOf(before), rotationOf(after).fastInverse());
    m_rotation.m3 = {};
    m_active = true;
}

void CorrectionBlend::update(float dt) {
    if (!m_active)
        return;
    const float keep = std::exp2(-std::max(0.0f, dt) / halfLife);
    m_position = m_position * keep;
    m_rotation = blendTransform(Mat34::identity(), m_rotation, keep);
    m_rotation.m3 = {};
    const bool straight = std::abs(m_rotation.m0.x - 1.0f) < 1e-6f &&
                          std::abs(m_rotation.m1.y - 1.0f) < 1e-6f &&
                          std::abs(m_rotation.m2.z - 1.0f) < 1e-6f;
    if (m_position.mag() < 0.0005f && straight)
        clear();
}

void CorrectionBlend::clear() {
    m_position = {};
    m_rotation = Mat34::identity();
    m_active = false;
}

Mat34 CorrectionBlend::apply(const Mat34& drawn) const {
    if (!m_active)
        return drawn;
    Mat34 m = Mat34::mul(m_rotation, rotationOf(drawn));
    m.m3 = drawn.m3 + m_position;
    return m;
}

} // namespace mm2::game

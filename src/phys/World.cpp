#include "phys/World.h"

#include <algorithm>

// Collision detection and response in this file are OpenMM2 code, not a port:
// the Angel engine's own collision (mmBoundTemplate / asBound::Impact in MM1,
// phBound / phImpact in MM2) has not been ported yet. Bodies are integrated by
// the ported InertialCS; contact impulses change momentum immediately and
// penetration is resolved through InertialCS::applyPush + moveICS as in the
// original.

namespace mm2::phys {
namespace {

void tangentBasis(const Vec3& n, Vec3& t1, Vec3& t2) {
    t1 = std::abs(n.y) < 0.9f ? Vec3{0, 1, 0}.cross(n) : Vec3{1, 0, 0}.cross(n);
    t1 = t1.normalized();
    t2 = n.cross(t1);
}

bool asleep(const Body* b) {
    return b && b->ics.state == InertialCS::Asleep;
}

void wake(Body* b) {
    if (b && b->ics.state == InertialCS::Asleep) {
        b->ics.state = InertialCS::Awake;
        b->ics.counter = 0.0f;
    }
}

} // namespace

bool FlatGround::probe(const Vec3& a, const Vec3& b, RayHit& hit) const {
    const float da = a.y - m_height, db = b.y - m_height;
    if ((da > 0 && db > 0) || (da < 0 && db < 0) || da == db)
        return false;
    hit.t = da / (da - db);
    hit.position = a + (b - a) * hit.t;
    hit.normal = {0, 1, 0};
    hit.material = 0;
    hit.polygon = -1;
    return true;
}

Obb Body::obb() const {
    Obb o;
    const Mat34& m = ics.matrix;
    o.center = m.transform(shape.offset);
    o.axis[0] = m.m0;
    o.axis[1] = m.m1;
    o.axis[2] = m.m2;
    o.half = shape.kind == Shape::Kind::Box ? shape.half : Vec3{shape.radius, shape.radius, shape.radius};
    return o;
}

Aabb Body::aabb() const {
    if (shape.kind == Shape::Kind::Sphere) {
        const Vec3 c = ics.matrix.transform(shape.offset);
        const Vec3 r{shape.radius, shape.radius, shape.radius};
        return {c - r, c + r};
    }
    return obb().aabb();
}

void World::add(Body* body) {
    if (std::ranges::find(m_bodies, body) != m_bodies.end())
        return;
    const InertialCS defaults;
    if (body->ics.gravity == defaults.gravity)
        body->ics.gravity = {0.0f, -kGravity, 0.0f};
    m_bodies.push_back(body);
}

void World::remove(Body* body) {
    std::erase(m_bodies, body);
}

void World::add(Joint3Dof* joint) {
    if (std::ranges::find(m_joints, joint) == m_joints.end())
        m_joints.push_back(joint);
}

void World::remove(Joint3Dof* joint) {
    std::erase(m_joints, joint);
}

bool World::probe(const Vec3& a, const Vec3& b, RayHit& hit) const {
    return m_static.raycast(a, b, hit);
}

int World::advanceFixed(float frameDelta, float sampleStep, int maxSamples) {
    m_accumulator += frameDelta;
    int n = 0;
    while (m_accumulator >= sampleStep && n < maxSamples) {
        step(sampleStep);
        m_accumulator -= sampleStep;
        ++n;
    }
    if (n == maxSamples && m_accumulator > sampleStep)
        m_accumulator = 0; // drop time rather than spiral
    return n;
}

int World::advanceOversampled(float frameDelta, float sampleStep, int maxSamples) {
    int n = 1;
    if (frameDelta >= sampleStep)
        n = std::min(static_cast<int>(frameDelta / sampleStep) + 1, maxSamples);
    const float dt = frameDelta / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        step(dt);
    return n;
}

void World::step(float dt) {
    if (dt <= 0)
        return;
    const float invDt = 1.0f / dt;

    for (Body* b : m_bodies) {
        b->m_prevPosition = b->ics.position();
        if (b->controller)
            b->controller->beforeIntegrate(*b, dt, *this);
    }
    for (Body* b : m_bodies)
        b->ics.update(dt, invDt);
    for (Joint3Dof* j : m_joints)
        j->update(dt, invDt);
    for (Body* b : m_bodies)
        if (b->controller)
            b->controller->afterIntegrate(*b, dt, *this);

    for (Body* b : m_bodies)
        if (b->collideStatic && !asleep(b))
            continuous(*b);
    m_contacts.clear();
    collide(m_contacts);
    solve(m_contacts);
    pushApart(m_contacts);
    report(m_contacts);
    for (Body* b : m_bodies)
        if (b->controller)
            b->controller->afterCollisions(*b, dt, *this);
    m_time += dt;
}

void World::continuous(Body& body) {
    const Vec3 from = body.m_prevPosition;
    const Vec3 to = body.ics.position();
    const Vec3& h = body.shape.kind == Shape::Kind::Box
                        ? body.shape.half
                        : Vec3{body.shape.radius, body.shape.radius, body.shape.radius};
    const float minHalf = std::min({h.x, h.y, h.z});
    if (from.dist2(to) < minHalf * minHalf)
        return;
    RayHit hit;
    if (!m_static.raycast(from, to, hit) || (to - from).dot(hit.normal) >= 0)
        return;
    body.ics.matrix.m3 = hit.position + hit.normal * 0.02f;
}

void World::collide(std::vector<ContactPoint>& contacts) {
    Contact buf[kMaxContacts];
    auto addContact = [&](Body* a, Body* b, const Contact& c) {
        ContactPoint cp;
        cp.a = a;
        cp.b = b;
        cp.c = c;
        if (b) {
            cp.bounce = a->elasticity() * b->elasticity();
            cp.friction = std::sqrt(std::max(a->friction() * b->friction(), 0.0f));
        } else {
            const Material& m = m_materials[c.material];
            cp.bounce = a->elasticity() * m.elasticity;
            cp.friction = a->friction() * m.friction;
        }
        contacts.push_back(cp);
    };

    for (Body* a : m_bodies) {
        if (!a->collideStatic || asleep(a))
            continue;
        Aabb box = a->aabb();
        box.min -= Vec3{0.05f, 0.05f, 0.05f};
        box.max += Vec3{0.05f, 0.05f, 0.05f};
        m_static.query(box, m_query);
        for (std::uint32_t pi : m_query) {
            const Polygon& poly = m_static.polygon(pi);
            const int n = a->shape.kind == Shape::Kind::Box
                              ? collideObbPolygon(a->obb(), poly, buf, kMaxContacts)
                              : collideSpherePolygon(a->ics.matrix.transform(a->shape.offset),
                                                     a->shape.radius, poly, buf);
            for (int i = 0; i < n; ++i)
                addContact(a, nullptr, buf[i]);
        }
    }

    for (std::size_t i = 0; i < m_bodies.size(); ++i) {
        Body* a = m_bodies[i];
        if (!a->collideBodies)
            continue;
        const Aabb ba = a->aabb();
        for (std::size_t j = i + 1; j < m_bodies.size(); ++j) {
            Body* b = m_bodies[j];
            if (!b->collideBodies || (asleep(a) && asleep(b)))
                continue;
            bool jointed = false;
            for (const Joint3Dof* jt : m_joints)
                if (!jt->isBroken() && ((jt->ics1 == &a->ics && jt->ics2 == &b->ics) ||
                                        (jt->ics1 == &b->ics && jt->ics2 == &a->ics)))
                    jointed = true;
            if (jointed)
                continue;
            const Aabb bb = b->aabb();
            if (ba.max.x < bb.min.x || ba.min.x > bb.max.x || ba.max.y < bb.min.y || ba.min.y > bb.max.y ||
                ba.max.z < bb.min.z || ba.min.z > bb.max.z)
                continue;
            const bool sa = a->shape.kind == Shape::Kind::Sphere, sb = b->shape.kind == Shape::Kind::Sphere;
            int n;
            if (!sa && !sb) {
                n = collideObbObb(a->obb(), b->obb(), buf, kMaxContacts);
            } else if (sa && sb) {
                n = collideSphereSphere(a->ics.matrix.transform(a->shape.offset), a->shape.radius,
                                        b->ics.matrix.transform(b->shape.offset), b->shape.radius, buf);
            } else if (sa) {
                n = collideSphereObb(a->ics.matrix.transform(a->shape.offset), a->shape.radius, b->obb(),
                                     buf);
            } else {
                n = collideSphereObb(b->ics.matrix.transform(b->shape.offset), b->shape.radius, a->obb(),
                                     buf);
                for (int k = 0; k < n; ++k)
                    buf[k].normal = -buf[k].normal;
            }
            if (n > 0) {
                wake(a);
                wake(b);
            }
            for (int k = 0; k < n; ++k)
                addContact(a, b, buf[k]);
        }
    }

    for (auto& cp : contacts) {
        const Vec3& n = cp.c.normal;
        tangentBasis(n, cp.t1, cp.t2);
        auto k = [&](const Vec3& dir) {
            float sum = 1.0f / std::max(cp.a->ics.effectiveMass(dir, cp.c.point), 1e-12f);
            if (cp.b)
                sum += 1.0f / std::max(cp.b->ics.effectiveMass(dir, cp.c.point), 1e-12f);
            return sum > 0 ? 1.0f / sum : 0.0f;
        };
        cp.massN = k(n);
        cp.massT1 = k(cp.t1);
        cp.massT2 = k(cp.t2);
        Vec3 vrel = cp.a->ics.getVelocity(&cp.c.point);
        if (cp.b)
            vrel -= cp.b->ics.getVelocity(&cp.c.point);
        cp.approach = vrel.dot(n);
        cp.bounce = cp.approach < -restitutionThreshold ? -cp.bounce * cp.approach : 0.0f;
    }
}

void World::solve(std::vector<ContactPoint>& contacts) {
    for (int it = 0; it < solverIterations; ++it) {
        for (auto& cp : contacts) {
            const Vec3& p = cp.c.point;
            const Vec3& n = cp.c.normal;
            auto relVel = [&] {
                Vec3 v = cp.a->ics.getVelocity(&p);
                if (cp.b)
                    v -= cp.b->ics.getVelocity(&p);
                return v;
            };
            auto apply = [&](const Vec3& j) {
                cp.a->ics.applyImpulseNow(j, p);
                if (cp.b)
                    cp.b->ics.applyImpulseNow(-j, p);
            };
            Vec3 v = relVel();
            float dj = cp.massN * (cp.bounce - v.dot(n));
            const float newJn = std::max(cp.jn + dj, 0.0f);
            dj = newJn - cp.jn;
            cp.jn = newJn;
            if (dj != 0)
                apply(n * dj);
            v = relVel();
            const float maxF = cp.friction * cp.jn;
            const float nj1 = clampf(cp.jt1 - cp.massT1 * v.dot(cp.t1), -maxF, maxF);
            const float nj2 = clampf(cp.jt2 - cp.massT2 * v.dot(cp.t2), -maxF, maxF);
            const float d1 = nj1 - cp.jt1, d2 = nj2 - cp.jt2;
            cp.jt1 = nj1;
            cp.jt2 = nj2;
            if (d1 != 0 || d2 != 0)
                apply(cp.t1 * d1 + cp.t2 * d2);
        }
    }
}

void World::pushApart(std::vector<ContactPoint>& contacts) {
    for (auto& cp : contacts) {
        const float depth = cp.c.depth - pushSlop;
        if (depth <= 0)
            continue;
        const float wa = cp.a->ics.invMass;
        const float wb = cp.b ? cp.b->ics.invMass : 0.0f;
        const float total = wa + wb;
        if (total <= 0)
            continue;
        const float corr = depth * pushFactor;
        cp.a->ics.applyPush(cp.c.normal * (corr * wa / total));
        cp.a->ics.numImpulses = std::max(cp.a->ics.numImpulses, 1);
        if (cp.b) {
            cp.b->ics.applyPush(cp.c.normal * (-corr * wb / total));
            cp.b->ics.numImpulses = std::max(cp.b->ics.numImpulses, 1);
        }
    }
    // Linked bodies keep their pushes: Joint3Dof::update shares them between
    // the two bodies and FinishUpdate applies them, as in the original (MM1
    // only calls MoveICS for AI cars).
    for (Body* b : m_bodies)
        if (!(b->ics.constraints & InertialCS::kConstrainLink))
            b->ics.moveICS();
}

void World::report(std::vector<ContactPoint>& contacts) {
    struct Best {
        Body* self;
        Impact impact;
    };
    std::vector<Best> best;
    auto consider = [&](Body* self, Body* other, const ContactPoint& cp, const Vec3& normal) {
        if (!self->controller)
            return;
        for (auto& b : best) {
            if (b.self == self && b.impact.other == other && (other || b.impact.material == cp.c.material)) {
                if (cp.jn > b.impact.impulse)
                    b.impact = {other, cp.c.material, cp.c.point, normal, cp.jn, -cp.approach};
                return;
            }
        }
        best.push_back({self, {other, cp.c.material, cp.c.point, normal, cp.jn, -cp.approach}});
    };
    for (const auto& cp : contacts) {
        consider(cp.a, cp.b, cp, cp.c.normal);
        if (cp.b)
            consider(cp.b, cp.a, cp, -cp.c.normal);
    }
    for (auto& b : best)
        b.self->controller->onImpact(*b.self, b.impact);
}

} // namespace mm2::phys

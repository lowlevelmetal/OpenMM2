#include "ai/PathGeometry.h"

#include <bit>
#include <cmath>

namespace mm2::ai {

namespace {

// The angle of centre section v -> v + 1 in vertex v's frame (positive to
// the right of the direction of the vertex index; the frame's z points back
// and its x to the left).
float sectionAngle(const city::AiPath& p, int v) {
    const auto i = static_cast<std::size_t>(v);
    const float dx = p.center[i + 1].x - p.center[i].x;
    const float dz = p.center[i + 1].z - p.center[i].z;
    const Vec3& x = p.xAxis[i];
    const Vec3& z = p.zAxis[i];
    return std::atan2(-x.x * dx + -x.z * dz, -z.x * dx + -z.z * dz);
}

Vec3 unitXZ(Vec3 v) {
    // Vector3::Normalize after zeroing y (InvMag 0 for a zero vector).
    v.y = 0.0f;
    const float m2 = (v.z * v.z + v.y * v.y) + v.x * v.x;
    const float inv = m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

} // namespace

const std::vector<Vec3>& pathBoundary(const city::AiRoadSide& side, int which) {
    static const std::vector<Vec3> none;
    const std::size_t k = static_cast<std::size_t>(side.numLanes) + side.numSidewalks + side.numTrams +
                          side.numTrains + static_cast<std::size_t>(which);
    return k < side.polylines.size() ? side.polylines[k] : none;
}

float pathCenterDist(const city::AiPath& path, int i) {
    // aiPath +0x100 holds one value per vertex; the file stores the first
    // (zero) before the others (city::AiPath::unknown).
    if (i <= 0)
        return std::bit_cast<float>(path.unknown);
    const auto k = static_cast<std::size_t>(i - 1);
    return k < path.centerLengths.size() ? path.centerLengths[k] : path.centerLengths.back();
}

float pathCenterLength(const city::AiPath& path, int a, int b) {
    return pathCenterDist(path, b) - pathCenterDist(path, a);
}

std::vector<SharpTurn> initRoadTurns(const city::AiPath& p) {
    std::vector<SharpTurn> turns;
    const int n = static_cast<int>(p.center.size());
    const std::vector<Vec3>& leftCurb = pathBoundary(p.left, 0);
    const std::vector<Vec3>& rightCurb = pathBoundary(p.right, 0);
    if (leftCurb.size() < p.center.size() || rightCurb.size() < p.center.size())
        return turns;
    auto at = [](const std::vector<Vec3>& a, int i) -> const Vec3& { return a[static_cast<std::size_t>(i)]; };
    // A turn of two sections from vertex v: its corner is where the curbs
    // of v and v + 2, moved 1.5 m into the road, cross (along -z of v and z
    // of v + 2); its height that of the curb at v + 1.
    auto mergedTurn = [&](int v, float angle) {
        SharpTurn t;
        t.vertex = v;
        t.angle = angle;
        const bool left = angle < 0.0f;
        t.dir = left ? -1.0f : 1.0f;
        const std::vector<Vec3>& curb = left ? leftCurb : rightCurb;
        const Vec3& xa = at(p.xAxis, v);
        const Vec3& xb = at(p.xAxis, v + 2);
        float ax, az, bx, bz;
        if (left) {
            ax = at(curb, v).x - xa.x * 1.5f;
            az = at(curb, v).z - xa.z * 1.5f;
            bx = at(curb, v + 2).x - xb.x * 1.5f;
            bz = at(curb, v + 2).z - xb.z * 1.5f;
        } else {
            ax = xa.x * 1.5f + at(curb, v).x;
            az = xa.z * 1.5f + at(curb, v).z;
            bx = xb.x * 1.5f + at(curb, v + 2).x;
            bz = xb.z * 1.5f + at(curb, v + 2).z;
        }
        const float dax = -at(p.zAxis, v).x, daz = -at(p.zAxis, v).z;
        const float dbx = at(p.zAxis, v + 2).x, dbz = at(p.zAxis, v + 2).z;
        const float s = (daz * ax + ((bz * dax - az * dax) - daz * bx)) / (daz * dbx - dax * dbz);
        t.point = {dbx * s + bx, at(curb, v + 1).y, dbz * s + bz};
        turns.push_back(t);
    };
    for (int c = 1; c < n - 1; ++c) {
        const float angle = sectionAngle(p, c);
        if (angle < -0.7f || 0.7f < angle) {
            // A sharp section: a turn at c, unless it is shorter than 10 m
            // (and not one of the last two), when it is taken with the next.
            if (10.0f <= pathCenterLength(p, c, c + 1) || n - 2 <= c) {
                SharpTurn t;
                t.vertex = c;
                t.angle = angle;
                // The inside curb at c, 1 m into the road.
                if (angle < 0.0f) {
                    t.dir = -1.0f;
                    t.point = at(leftCurb, c) - at(p.xAxis, c);
                } else {
                    t.dir = 1.0f;
                    t.point = at(p.xAxis, c) + at(rightCurb, c);
                }
                turns.push_back(t);
                continue;
            }
            const float total = sectionAngle(p, c + 1) + angle;
            const int v = c++;
            if (total < -0.7f || 0.7f < total)
                mergedTurn(v, total);
            // Inferred: when the two sections together turn less than
            // 0.7 rad, MM2 still counts a turn without filling its record
            // (an unset pointer); no retail road has such a pair, and
            // OpenMM2 adds nothing.
            continue;
        }
        // A gentle section shorter than 10 m (not one of the last two) is
        // taken with the next: a turn if the two make one.
        if (pathCenterLength(p, c, c + 1) < 10.0f && c < n - 2) {
            const float total = sectionAngle(p, c + 1) + angle;
            const int v = c++;
            if (total < -0.7f || 0.7f < total)
                mergedTurn(v, total);
        }
    }
    return turns;
}

void calcRoadTurns(const city::AiPath& p, std::span<SharpTurn> turns, const Vec3& pos, bool forward) {
    const int n = static_cast<int>(p.center.size());
    // The room is limited by the second side's road part (its params[2n - 1],
    // n its lanes), whichever direction the car drives.
    const int lanes = p.right.numLanes;
    const float limit = lanes > 0 ? p.right.params[static_cast<std::size_t>(2 * lanes - 1)] : 0.0f;
    const float maxRoom = (limit + limit) - 1.5f;
    for (SharpTurn& t : turns) {
        const int v = t.vertex;
        const auto iv = static_cast<std::size_t>(v);
        const float half = (3.14f - std::abs(t.angle)) * 0.5f;
        float room;
        if (forward) {
            room = (pos.x - t.point.x) * p.xAxis[iv].x + (pos.z - t.point.z) * p.xAxis[iv].z;
        } else {
            // Against the vertex order: measured across the section after the
            // turn (the second one of a merged turn).
            int k = 1;
            if (v + 2 < n && sectionAngle(p, v) != t.angle)
                k = 2;
            const Vec3& x = p.xAxis[static_cast<std::size_t>(v + k)];
            room = -x.x * (pos.x - t.point.x) + -x.z * (pos.z - t.point.z);
        }
        room = room * t.dir;
        if (3.0f <= room) {
            if (maxRoom < room)
                room = maxRoom;
        } else {
            room = 3.0f;
        }
        t.radius = room / (1.0f - std::sin(half));
        t.setback = std::cos(half) * t.radius;
        const Vec3& x = p.xAxis[iv];
        const Vec3& z = p.zAxis[iv];
        const float across = (t.radius - room) * t.dir;
        t.center = {(t.setback * z.x + t.point.x) - across * x.x,
                    (t.setback * z.y + t.point.y) - across * x.y,
                    (t.setback * z.z + t.point.z) - across * x.z};
        const float r = t.radius * t.dir;
        t.startDir = unitXZ({(x.x * r + t.center.x) - t.center.x, (x.y * r + t.center.y) - t.center.y,
                             (x.z * r + t.center.z) - t.center.z});
        const float a = t.angle * t.dir;
        const float s = std::sin(a) * t.radius;
        const float c = std::cos(a) * t.radius * t.dir;
        t.endDir = unitXZ({((t.center.x - z.x * s) + c * x.x) - t.center.x,
                           ((t.center.y - z.y * s) + c * x.y) - t.center.y,
                           ((t.center.z - z.z * s) + c * x.z) - t.center.z});
    }
}

int isSharpTurn(const city::AiPath& path, std::span<const SharpTurn> turns, int vertex, bool forward) {
    const int count = static_cast<int>(turns.size());
    const int n = static_cast<int>(path.center.size());
    for (int i = 0; i < count; ++i) {
        const int v = forward ? turns[static_cast<std::size_t>(i)].vertex
                              : n - turns[static_cast<std::size_t>(count - 1 - i)].vertex - 1;
        if (v == vertex)
            return i;
    }
    return -1;
}

const SharpTurn& sharpTurn(std::span<const SharpTurn> turns, int i, bool forward) {
    const auto j = static_cast<std::size_t>(i);
    const std::size_t k = forward ? j : turns.size() - 1 - j;
    return turns[k];
}

int sharpTurnVertIndex(const city::AiPath& path, std::span<const SharpTurn> turns, int i, bool forward) {
    if (forward)
        return turns[static_cast<std::size_t>(i)].vertex;
    return static_cast<int>(path.center.size()) - sharpTurn(turns, i, false).vertex - 1;
}

} // namespace mm2::ai

// MM2's box bound (phBoundBox): its segment tests (TestEdge, TestProbe) and
// its impact searches against a sphere (FindImpactSphereToBox) and against
// another box (FindImpactsBoxToBox, FindImpactsBoxToBoxOffset), ported from
// the code of midtown2.exe build 3393 (MM2Recomp). See docs/physics.md,
// "Collision".
//
// The original runs the x87 FPU in single precision, so every operation
// rounds to float. The three-term sums below are added in the order the
// original's code adds them (SumOrder): a different association can change
// the last bit, and the box-against-box search compares such values with
// thresholds and sign bits. Comparisons follow the original's for ordered
// values; its handling of NaN (the x87 reports "unordered" as "less") is not
// reproduced.

#include "phys/Bound.h"
#include "phys/Collision.h"
#include "phys/Geometry.h"
#include "phys/Impact.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace mm2::phys {
namespace {

// --- The box's topology (phBoundBox's static tables) ---------------------------------------

// Corners: 0 (+,+,+) 1 (-,+,+) 2 (-,-,+) 3 (+,-,+) 4 (+,+,-) 5 (-,+,-) 6 (-,-,-) 7 (+,-,-).
// Faces: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z.
constexpr std::array<std::array<int, 4>, 6> kFaceVertices{{
    {4, 0, 3, 7}, {2, 1, 5, 6}, {1, 0, 4, 5}, {7, 3, 2, 6}, {3, 0, 1, 2}, {5, 4, 7, 6},
}};
// The edge index of each side (v[i], v[i+1]) of a face.
constexpr std::array<std::array<int, 4>, 6> kFaceEdges{{
    {8, 3, 11, 7}, {1, 9, 5, 10}, {0, 8, 4, 9}, {11, 2, 10, 6}, {3, 0, 1, 2}, {4, 7, 6, 5},
}};
constexpr std::array<std::array<int, 2>, 12> kEdgeVertices{{
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
}};
// The two faces meeting at each edge.
constexpr std::array<std::array<int, 2>, 12> kEdgeFaces{{
    {2, 4}, {1, 4}, {3, 4}, {0, 4}, {2, 5}, {1, 5}, {3, 5}, {0, 5}, {0, 2}, {1, 2}, {1, 3}, {0, 3},
}};
// The three edges meeting at each corner.
constexpr std::array<std::array<int, 3>, 8> kCornerEdges{{
    {0, 3, 8}, {0, 9, 1}, {2, 1, 10}, {3, 2, 11}, {4, 8, 7}, {4, 5, 9}, {5, 6, 10}, {6, 7, 11},
}};
// The corner with a given sign pattern, indexed by x + 2y + 4z (1 = positive).
constexpr std::array<int, 8> kCornerBySign{6, 7, 5, 4, 2, 3, 1, 0};
// The corner signs as +-1, which the original fills in a static initialiser
// (scaled by a box's half size they give its corners, and the box-against-
// box edge test needs the full edge lengths).
constexpr std::array<Vec3, 8> kCornerSigns{{
    {1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}, {1, -1, -1},
}};

// --- Float sums in the original's order ------------------------------------------------------

// The order in which the original adds the three products of a dot product
// or of one component of a matrix product: (p[i] + p[j]) + p[k].
struct SumOrder {
    int i, j, k;
};
constexpr SumOrder kXYZ{0, 1, 2};
constexpr SumOrder kXZY{0, 2, 1};
constexpr SumOrder kYXZ{1, 0, 2};
constexpr SumOrder kYZX{1, 2, 0};
constexpr SumOrder kZXY{2, 0, 1};
constexpr SumOrder kZYX{2, 1, 0};

float dot(const Vec3& a, const Vec3& b, SumOrder o) {
    return (a[o.i] * b[o.i] + a[o.j] * b[o.j]) + a[o.k] * b[o.k];
}

// Component c of the direction v placed by m (v.x * m0 + v.y * m1 + v.z * m2),
// summed over the rows in order o.
float rowSum(const Mat34& m, const Vec3& v, int c, SumOrder o) {
    return (v[o.i] * m.row(o.i)[c] + v[o.j] * m.row(o.j)[c]) + v[o.k] * m.row(o.k)[c];
}

// A direction placed by m's rotation; each component has its own order.
Vec3 rotate(const Mat34& m, const Vec3& v, SumOrder ox, SumOrder oy, SumOrder oz) {
    return {rowSum(m, v, 0, ox), rowSum(m, v, 1, oy), rowSum(m, v, 2, oz)};
}

// A point placed by m (the translation added last).
Vec3 place(const Mat34& m, const Vec3& p, SumOrder ox, SumOrder oy, SumOrder oz) {
    return {rowSum(m, p, 0, ox) + m.m3.x, rowSum(m, p, 1, oy) + m.m3.y, rowSum(m, p, 2, oz) + m.m3.z};
}

// --- Segment tests ---------------------------------------------------------------------------

// The parameter range of a segment inside the box and the faces it enters
// and leaves through (the slab test shared by phBoundBox::TestEdge and
// TestProbeSlave). t runs from 0 at the segment's start to 1 at its end;
// the starting values -1 and 2 mean "not clipped".
struct BoxClip {
    int entryFace = -1;
    int exitFace = -1;
    float tEnter = -1.0f;
    float tExit = 2.0f;
};

// One axis of the slab test: false when the segment a->b misses [lo, hi] or
// the range left is empty.
bool clipSlab(float a, float b, float lo, float hi, int maxFace, int minFace, BoxClip& c) {
    const float d = b - a;
    if (d == 0.0f)
        return !(a < lo) && a <= hi;
    const float inv = 1.0f / d;
    const float tMax = (hi - a) * inv;
    if (a < b) {
        // Moving towards +: enters through the min face, leaves through the max face.
        if (tMax < 0.0f)
            return false;
        if (tMax <= 1.0f && tMax < c.tExit) {
            c.tExit = tMax;
            c.exitFace = maxFace;
        }
        const float tMin = (lo - a) * inv;
        if (tMin > 1.0f)
            return false;
        if (!(tMin < 0.0f) && tMin > c.tEnter) {
            c.tEnter = tMin;
            c.entryFace = minFace;
        }
    } else {
        if (tMax > 1.0f)
            return false;
        if (!(tMax < 0.0f) && tMax > c.tEnter) {
            c.tEnter = tMax;
            c.entryFace = maxFace;
        }
        const float tMin = (lo - a) * inv;
        if (tMin < 0.0f)
            return false;
        if (tMin <= 1.0f && tMin < c.tExit) {
            c.tExit = tMin;
            c.exitFace = minFace;
        }
    }
    return c.tEnter <= c.tExit;
}

bool clipToBox(const Segment& seg, const Vec3& boxMin, const Vec3& boxMax, BoxClip& c) {
    return clipSlab(seg.a.x, seg.b.x, boxMin.x, boxMax.x, 0, 1, c) &&
           clipSlab(seg.a.y, seg.b.y, boxMin.y, boxMax.y, 2, 3, c) &&
           clipSlab(seg.a.z, seg.b.z, boxMin.z, boxMax.z, 4, 5, c);
}

// The crossing of the segment with face `face` at t: the point, the face
// normal and how far `inside` (the segment end behind the face) lies behind
// the face's plane.
void fillCrossing(const BoundBox& box, const Segment& seg, int face, float t, const Vec3& inside,
                  IntersectionPoint& out) {
    const Vec3& n = BoundBox::faceNormals()[static_cast<std::size_t>(face)];
    const Vec3& v = box.vertex(kFaceVertices[static_cast<std::size_t>(face)][0]);
    out.depth = -dot({inside.x - v.x, inside.y - v.y, inside.z - v.z}, n, kZYX);
    out.normal = n;
    out.position = {(seg.b.x - seg.a.x) * t + seg.a.x, (seg.b.y - seg.a.y) * t + seg.a.y,
                    (seg.b.z - seg.a.z) * t + seg.a.z};
    out.t = t;
}

// --- phConvexPoly::ConvexPolyIntersect --------------------------------------------------------

// phConvexPoly::Data: one vertex of the intersection of two convex polygons.
struct ClipRecord {
    enum Type : int { EdgeEdge = 0, UVertex = 1, VVertex = 2 };
    int type = 0; // EdgeEdge: edges u and v cross; UVertex / VVertex: a vertex of one inside the other
    int u = 0;    // edge or vertex index in polygon u
    int v = 0;    // edge or vertex index in polygon v
    float tu = 0; // EdgeEdge: where along edge u (u[i] -> u[i+1]) they cross
    float tv = 0;
};

// The caller's buffer in BoxToBoxFaceImpacts holds 8 records (enough for two
// quads); the original writes past it in degenerate cases, here further
// records are dropped.
constexpr int kMaxClipRecords = 8;

// phConvexPoly::ConvexPolyIntersect and its helpers (PrecomputeRays,
// AdvanceV, Get*Out, Record*): an O'Rourke-style walk around two convex
// polygons, u and v, recording the crossings of their edges and the vertices
// of one inside the other. The members are the statics the original keeps
// (the names say which values the original's helper of that name computes;
// after the walk swaps polygons they hold the other polygon's values).
class ConvexPolyIntersect {
public:
    int run(int uCount, const Vec2* u, int vCount, const Vec2* v, ClipRecord* out);

private:
    void advanceV();
    void getvHeadOut();
    void getuHeadOut();
    void getuTailOut();
    void recordEE();
    void recordTail(bool vVertex);
    void recordUTail() { recordTail(m_advanceU); }
    void recordVTail() { recordTail(!m_advanceU); }
    void recordNoIsect(int code);
    void recordInteriorCollides(bool vVertices);
    // Which side of a ray (an edge p[i] -> p[i+1] and its constant) the point q lies on.
    static float side(const Vec2& ray, float rayConst, const Vec2& q) {
        return (q.y * ray.x - ray.y * q.x) - rayConst;
    }
    float uSide(int i, const Vec2& q) const {
        return side(m_uRay[static_cast<std::size_t>(i)], m_uRayConst[static_cast<std::size_t>(i)], q);
    }
    float vSide(int i, const Vec2& q) const {
        return side(m_vRay[static_cast<std::size_t>(i)], m_vRayConst[static_cast<std::size_t>(i)], q);
    }
    static void precomputeRays(int count, const Vec2* p, Vec2* rays, float* consts);

    void stateOutside();
    void stateUInside();
    void stateCrossing();
    void stateTouching();
    void swapAndCopyHeads() {
        m_advanceU = !m_advanceU;
        m_vHead = m_uHead;
        m_vTail = m_uTail;
    }

    const Vec2* m_u = nullptr;
    const Vec2* m_v = nullptr;
    int m_uCount = 0, m_vCount = 0;
    int m_uLeft = 0, m_vLeft = 0; // edges still to walk once the first record is made
    std::array<Vec2, 4> m_uRay{}, m_vRay{};
    std::array<float, 4> m_uRayConst{}, m_vRayConst{};
    int m_steps = 0;
    int m_state = 0;
    bool m_advanceU = false;
    int m_ui = 0, m_vi = 0;
    int m_count = 0;
    ClipRecord* m_out = nullptr;
    float m_uHead = 0, m_uTail = 0, m_vHead = 0, m_vTail = 0;
};

void ConvexPolyIntersect::precomputeRays(int count, const Vec2* p, Vec2* rays, float* consts) {
    // phConvexPoly::PrecomputeRays: ray i = p[i+1] - p[i].
    for (int i = count - 1; i >= 0; --i) {
        const Vec2& next = p[i + 1 < count ? i + 1 : 0];
        rays[i] = {next.x - p[i].x, next.y - p[i].y};
        consts[i] = rays[i].x * p[i].y - rays[i].y * p[i].x;
    }
}

void ConvexPolyIntersect::advanceV() {
    // phConvexPoly::AdvanceV: steps the polygon whose turn it is; the walk
    // ends once both have gone round after the first record.
    if (m_advanceU) {
        ++m_ui;
        --m_uLeft;
        if (m_ui >= m_uCount)
            m_ui = 0;
    } else {
        ++m_vi;
        --m_vLeft;
        if (m_vi >= m_vCount)
            m_vi = 0;
    }
    if (m_count > 0 && ((m_uLeft <= 0 && m_vLeft <= 0) || m_uLeft < 0 || m_vLeft < 0))
        m_state = 4;
}

void ConvexPolyIntersect::getvHeadOut() {
    if (!m_advanceU) {
        const int i = m_vi + 1 < m_vCount ? m_vi + 1 : 0;
        m_vHead = uSide(m_ui, m_v[i]);
    } else {
        const int i = m_ui + 1 < m_uCount ? m_ui + 1 : 0;
        m_vHead = vSide(m_vi, m_u[i]);
    }
}

void ConvexPolyIntersect::getuHeadOut() {
    if (m_advanceU) {
        const int i = m_vi + 1 < m_vCount ? m_vi + 1 : 0;
        m_uHead = uSide(m_ui, m_v[i]);
    } else {
        const int i = m_ui + 1 < m_uCount ? m_ui + 1 : 0;
        m_uHead = vSide(m_vi, m_u[i]);
    }
}

void ConvexPolyIntersect::getuTailOut() {
    if (!m_advanceU)
        m_uTail = vSide(m_vi, m_u[m_ui]);
    else
        m_uTail = uSide(m_ui, m_v[m_vi]);
}

void ConvexPolyIntersect::recordEE() {
    // phConvexPoly::RecordEE: the current edges cross. Meeting the first
    // record again closes the walk.
    if (m_count > 0 && m_out[0].type == ClipRecord::EdgeEdge && m_out[0].u == m_ui && m_out[0].v == m_vi) {
        m_state = 4;
        return;
    }
    if (m_count == 0) {
        m_uLeft = m_uCount;
        m_vLeft = m_vCount;
    }
    if (m_count >= kMaxClipRecords)
        return;
    ClipRecord& r = m_out[m_count];
    if (!m_advanceU) {
        r.tu = m_uTail / (m_uTail - m_uHead);
        r.tv = m_vTail / (m_vTail - m_vHead);
    } else {
        r.tu = m_vTail / (m_vTail - m_vHead);
        r.tv = m_uTail / (m_uTail - m_uHead);
    }
    r.type = ClipRecord::EdgeEdge;
    r.u = m_ui;
    r.v = m_vi;
    ++m_count;
}

void ConvexPolyIntersect::recordTail(bool vVertex) {
    // phConvexPoly::RecordTail: the current vertex of u (or v) lies inside
    // the other polygon.
    if (m_count == 0) {
        m_uLeft = m_uCount;
        m_vLeft = m_vCount;
    }
    if (!vVertex) {
        if (m_count > 0 && m_out[0].type == ClipRecord::UVertex && m_out[0].u == m_ui) {
            m_state = 4;
            return;
        }
        if (m_count >= kMaxClipRecords)
            return;
        m_out[m_count].type = ClipRecord::UVertex;
        m_out[m_count].u = m_ui;
    } else {
        if (m_count > 0 && m_out[0].type == ClipRecord::VVertex && m_out[0].v == m_vi) {
            m_state = 4;
            return;
        }
        if (m_count >= kMaxClipRecords)
            return;
        m_out[m_count].type = ClipRecord::VVertex;
        m_out[m_count].v = m_vi;
    }
    ++m_count;
}

void ConvexPolyIntersect::recordNoIsect(int code) {
    // phConvexPoly::RecordNoIsect: overwrites the first record (the count is
    // not changed).
    m_out[0].type = code;
    m_out[0].u = m_ui;
    m_out[0].v = m_vi;
}

void ConvexPolyIntersect::recordInteriorCollides(bool vVertices) {
    // phConvexPoly::RecordInteriorCollides: one polygon lies inside the
    // other; every vertex of the inner one is recorded.
    if (vVertices) {
        for (m_vi = 0; m_vi < m_vCount; ++m_vi)
            recordTail(true);
    } else {
        for (m_ui = 0; m_ui < m_uCount; ++m_ui)
            recordTail(false);
    }
}

void ConvexPolyIntersect::stateOutside() {
    // State 0: advancing without contact.
    bool wasBehind = m_vHead < m_vTail;
    for (;;) {
        if (--m_steps <= 0) {
            recordInteriorCollides(m_advanceU);
            m_state = 4;
            return;
        }
        advanceV();
        m_vTail = m_vHead;
        getvHeadOut();
        if (!(m_vHead < 0.0f)) {
            if (wasBehind) {
                if (!(m_vHead < m_vTail)) {
                    m_state = 4;
                    if (m_vHead == m_vTail)
                        recordNoIsect(-1);
                    else
                        recordNoIsect((m_advanceU ? 1 : 0) - 3);
                    return;
                }
            } else {
                wasBehind = m_vHead < m_vTail;
            }
            continue;
        }
        getuTailOut();
        getuHeadOut();
        if (m_uTail < 0.0f) {
            if (m_uHead <= 0.0f) {
                m_state = m_uHead == 0.0f ? 3 : 1;
                swapAndCopyHeads();
                return;
            }
            m_state = 2;
            if (m_vTail <= 0.0f)
                recordVTail();
            else
                recordEE();
            swapAndCopyHeads();
            return;
        }
        wasBehind = false;
        swapAndCopyHeads();
    }
}

void ConvexPolyIntersect::stateUInside() {
    // State 1.
    for (;;) {
        if (--m_steps <= 0) {
            recordInteriorCollides(!m_advanceU);
            m_state = 4;
            return;
        }
        advanceV();
        m_vTail = m_vHead;
        getuHeadOut();
        if (!(m_uHead < 0.0f)) {
            getuTailOut();
            m_state = 0;
            swapAndCopyHeads();
            return;
        }
        getvHeadOut();
        if (m_vHead < 0.0f)
            continue;
        getuTailOut();
        if (m_uTail > 0.0f) {
            if (m_vHead != 0.0f) {
                m_state = 2;
                recordEE();
            } else {
                m_state = 3;
            }
            return;
        }
        if (m_uTail != 0.0f) {
            m_state = 0;
            return;
        }
        if (m_vHead > 0.0f) {
            m_state = 2;
            recordUTail();
            return;
        }
    }
}

void ConvexPolyIntersect::stateCrossing() {
    // State 2.
    do {
        advanceV();
        if (m_state == 4)
            return;
        m_vTail = m_vHead;
        getvHeadOut();
    } while (!(m_vHead < 0.0f));
    getuHeadOut();
    if (m_uHead <= 0.0f) {
        m_state = 3;
    } else if (m_vTail <= 0.0f) {
        recordVTail();
    } else {
        getuTailOut();
        recordEE();
    }
    swapAndCopyHeads();
}

void ConvexPolyIntersect::stateTouching() {
    // State 3.
    for (;;) {
        advanceV();
        if (m_state == 4)
            return;
        m_vTail = m_vHead;
        getuHeadOut();
        recordVTail();
        if (m_state == 4)
            return;
        if (!(m_uHead < 0.0f)) {
            m_state = 2;
            swapAndCopyHeads();
            return;
        }
        getvHeadOut();
        if (m_vHead > 0.0f)
            break;
    }
    m_state = 2;
    if (m_vTail < 0.0f) {
        getuTailOut();
        recordEE();
    }
}

int ConvexPolyIntersect::run(int uCount, const Vec2* u, int vCount, const Vec2* v, ClipRecord* out) {
    m_v = v;
    m_vCount = vCount;
    m_vLeft = vCount;
    m_u = u;
    m_uCount = uCount;
    m_uLeft = uCount;
    m_out = out;
    precomputeRays(uCount, u, m_uRay.data(), m_uRayConst.data());
    precomputeRays(vCount, v, m_vRay.data(), m_vRayConst.data());
    m_steps = (vCount + uCount) * 2;
    m_advanceU = false;
    m_count = 0;
    m_ui = 0;
    m_vi = 0;
    m_uHead = vSide(0, u[1]);
    m_vHead = uSide(0, v[1]);
    m_uTail = vSide(0, u[0]);
    m_vTail = uSide(0, v[0]);

    // The starting state from the first edges of both polygons.
    if (m_vHead < 0.0f) {
        if (m_uHead < 0.0f) {
            m_state = 1;
            if (m_uTail < m_uHead) {
                m_advanceU = true;
                m_vHead = m_uHead;
                m_vTail = m_uTail;
            }
        } else {
            if (!(m_uTail < 0.0f) || m_vTail <= 0.0f) {
                m_state = 0;
            } else if (m_uHead == 0.0f) {
                m_state = 3;
            } else {
                m_state = 2;
                recordEE();
            }
            swapAndCopyHeads();
        }
    } else if (m_uHead < 0.0f) {
        if (m_uTail <= 0.0f || !(m_vTail < 0.0f)) {
            m_state = 0;
        } else if (m_vHead == 0.0f) {
            m_state = 3;
        } else {
            m_state = 2;
            recordEE();
        }
    } else if (m_vHead < m_vTail) {
        m_state = 0;
    } else if (m_uHead < m_uTail) {
        m_state = 0;
        m_advanceU = true;
        m_vHead = m_uHead;
        m_vTail = m_uTail;
    } else {
        recordNoIsect(-1);
        m_state = 4;
    }

    for (;;) {
        switch (m_state) {
        case 0:
            stateOutside();
            break;
        case 1:
            stateUInside();
            break;
        case 2:
            stateCrossing();
            break;
        case 3:
            stateTouching();
            break;
        default:
            return m_count;
        }
    }
}

// Vector3::GetVector2: a point projected on the plane of box face `face`.
Vec2 projectOnFace(const Vec3& p, int face) {
    switch (face) {
    case 0:
        return {p.y, p.z};
    case 1:
        return {p.z, p.y};
    case 2:
        return {p.z, p.x};
    case 3:
        return {p.x, p.z};
    case 4:
        return {p.x, p.y};
    default:
        return {p.y, p.x};
    }
}

// --- Box against box ---------------------------------------------------------------------------

// Bits of the edge-pair table: which face passes asked for an edge-edge
// check of (edge of A, edge of B), and which pairs a face contact covers.
constexpr std::uint8_t kCheckFromA = 0x1; // a corner of A near a face of B
constexpr std::uint8_t kCheckFromB = 0x2; // a corner of B near a face of A
constexpr std::uint8_t kAvoidCheck = 0x4;
constexpr std::uint8_t kCheckEdges = kCheckFromA | kCheckFromB;

// The bookkeeping phBoundBox's box-against-box search keeps in statics, for
// one FindImpactsBoxToBox call. "Corner box" and "face box" are the roles of
// a face pass: the first pass puts A's corners against B's faces, the second
// B's corners against A's faces.
struct BoxPairSearch {
    // rotation[i][j] = (A's axis i) . (B's axis j): row i is A's axis i in
    // B's frame, column j is B's axis j in A's frame.
    std::array<std::array<float, 3>, 3> rotation{};
    // FindFaceDots per rotation entry: faceDot is its sign, 0 when it is
    // within 0.035 of 0; faceSign is its sign even then (0 only for 0).
    std::array<std::array<int, 3>, 3> faceDot{};
    std::array<std::array<int, 3>, 3> faceSign{};
    // MakeTransformedCorners: the corner box's axes (rows) and corners in
    // the face box's frame (around the face box's centre).
    std::array<Vec3, 3> axes{};
    std::array<Vec3, 8> corners{};
    std::array<std::array<std::uint8_t, 12>, 12> edgeChecks{}; // [edge of A][edge of B]
    float penetration = 0; // the smaller penetration of the two bounds
    float soonTime = 0;    // 1.5 samples
    int count = 0;         // impacts of the best contact so far; -1 once a separating plane is found
    float bestTime = 0;    // UseThisImpact's time to impact of the current impacts
    float bestDepth = 0;   // and their depth beyond the penetration
};

// The face box of a face pass as BoxToBoxFaceImpacts / BoxToBoxFaceImpactsOffset see it.
struct FaceBox {
    Vec3 half;                    // half extents
    Vec3 centroid;                // added to points before placing them (Offset only)
    std::array<Vec3, 8> corners;  // its corners around its centre
    bool offset = false;          // which of the two originals (they differ in summation order)
};

// phBoundBox::FindFaceDots.
void findFaceDots(float r, int& dot, int& sign) {
    if (r > 0.035f) {
        dot = 1;
        sign = 1;
    } else if (r > 0.0f) {
        dot = 0;
        sign = 1;
    } else if (r < -0.035f) {
        dot = -1;
        sign = -1;
    } else if (r < 0.0f) {
        dot = 0;
        sign = -1;
    } else {
        dot = 0;
        sign = 0;
    }
}

// phBoundBox::RemoveFaceDotZero: the largest rotation entry rounded to 0
// gets its sign back.
void removeFaceDotZero(BoxPairSearch& s) {
    int row = -1, col = -1;
    float best = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const auto ui = static_cast<std::size_t>(i), uj = static_cast<std::size_t>(j);
            if (s.faceDot[ui][uj] == 0 && s.faceSign[ui][uj] != 0) {
                const float a = std::fabs(s.rotation[ui][uj]);
                if (a > best) {
                    best = a;
                    row = i;
                    col = j;
                }
            }
        }
    }
    if (row != -1 || col != -1)
        s.faceDot[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] =
            s.faceSign[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
}

// phBoundBox::CheckFourFaceDotPattern: every row and column of faceDot has
// one or two zeros.
bool checkFourFaceDotPattern(const BoxPairSearch& s) {
    for (std::size_t i = 0; i < 3; ++i) {
        int rowZeros = 0, colZeros = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            if (s.faceDot[i][k] == 0)
                ++rowZeros;
            if (s.faceDot[k][i] == 0)
                ++colZeros;
        }
        if ((rowZeros != 1 && rowZeros != 2) || (colZeros != 1 && colZeros != 2))
            return false;
    }
    return true;
}

// phBoundBox::RemoveFifthFaceDotZero: of the zeros whose removal leaves a
// valid four-zero pattern, the largest gets its sign back.
void removeFifthFaceDotZero(BoxPairSearch& s) {
    int row = -1, col = -1;
    float best = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const auto ui = static_cast<std::size_t>(i), uj = static_cast<std::size_t>(j);
            if (s.faceDot[ui][uj] == 0 && s.faceSign[ui][uj] != 0 && std::fabs(s.rotation[ui][uj]) > best) {
                s.faceDot[ui][uj] = 1;
                if (checkFourFaceDotPattern(s)) {
                    best = std::fabs(s.rotation[ui][uj]);
                    row = i;
                    col = j;
                }
                s.faceDot[ui][uj] = 0;
            }
        }
    }
    if (row < 0) {
        // No candidate: the original indexes its tables with -4 and copies a
        // word of the (negative) corner-sign table over faceSign[2][0],
        // which then reads as negative.
        s.faceSign[2][0] = -1;
        return;
    }
    s.faceDot[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] =
        s.faceSign[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
}

// FindImpactsBoxToBox(Offset): FindFaceDots over the rotation, then the
// pattern fixes for 2 to 5 zeros.
void classifyRotation(BoxPairSearch& s) {
    int zeros = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            findFaceDots(s.rotation[i][j], s.faceDot[i][j], s.faceSign[i][j]);
            if (s.faceDot[i][j] == 0)
                ++zeros;
        }
    }
    if (zeros == 2) {
        removeFaceDotZero(s);
    } else if (zeros == 3) {
        removeFaceDotZero(s);
        removeFaceDotZero(s);
    } else if (zeros == 4) {
        if (!checkFourFaceDotPattern(s)) {
            removeFaceDotZero(s);
            removeFaceDotZero(s);
            removeFaceDotZero(s);
        }
    } else if (zeros == 5) {
        removeFifthFaceDotZero(s);
    }
}

// phBoundBox::MakeTransformedCorners: the corner box (half extents `half`,
// centre at `pos` in the face box's frame) placed in the face box's frame.
// aInB: the corner box is A (rotation rows), else B (rotation columns).
void makeTransformedCorners(BoxPairSearch& s, const Vec3& half, const Vec3& pos, bool aInB) {
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j)
            s.axes[i][static_cast<int>(j)] = aInB ? s.rotation[i][j] : s.rotation[j][i];
    const Vec3 u = s.axes[0] * half.x;
    const Vec3 v = s.axes[1] * half.y;
    const Vec3 w = s.axes[2] * half.z;
    auto& c = s.corners;
    c[0] = {v.x + u.x, v.y + u.y, v.z + u.z};
    c[1] = {v.x - u.x, v.y - u.y, v.z - u.z};
    c[2] = -c[0];
    c[3] = -c[1];
    for (std::size_t k = 0; k < 4; ++k)
        c[k] = {w.x + c[k].x, c[k].y + w.y, c[k].z + w.z};
    c[4] = -c[2];
    c[5] = -c[3];
    c[6] = -c[0];
    c[7] = -c[1];
    for (Vec3& p : c)
        p = {p.x + pos.x, pos.y + p.y, pos.z + p.z};
}

// phBoundBox::AddEdgeChecks: the edges at `corner` of the corner box against
// the edges of `face` of the face box.
void addEdgeChecks(BoxPairSearch& s, int corner, int face, bool cornerBoxIsA) {
    for (const int e : kCornerEdges[static_cast<std::size_t>(corner)]) {
        for (const int f : kFaceEdges[static_cast<std::size_t>(face)]) {
            if (cornerBoxIsA)
                s.edgeChecks[static_cast<std::size_t>(e)][static_cast<std::size_t>(f)] |= kCheckFromA;
            else
                s.edgeChecks[static_cast<std::size_t>(f)][static_cast<std::size_t>(e)] |= kCheckFromB;
        }
    }
}

// phBoundBox::AvoidEdgeChecks (both overloads; the two-argument one is the
// edgeBoxIsA case): `edge` of the corner box against the edges of `face`
// need no edge-edge check.
void avoidEdgeChecks(BoxPairSearch& s, int edge, int face, bool edgeBoxIsA) {
    for (const int f : kFaceEdges[static_cast<std::size_t>(face)]) {
        if (edgeBoxIsA)
            s.edgeChecks[static_cast<std::size_t>(edge)][static_cast<std::size_t>(f)] |= kAvoidCheck;
        else
            s.edgeChecks[static_cast<std::size_t>(f)][static_cast<std::size_t>(edge)] |= kAvoidCheck;
    }
}

// phBoundBox::UseThisImpact: whether a candidate contact (world position,
// normal from the face box towards the corner box, depth) beats the impacts
// found so far: an earlier time to impact (within 1.5 samples) or a smaller
// depth. m1/last1 place the corner box now and at the end of the previous
// sample, m2/last2 the face box. Time to impact is the depth beyond the
// penetration over the closing displacement along the normal (in samples,
// compared with 1.5 * seconds as the original does).
bool useThisImpact(BoxPairSearch& s, const Vec3& position, const Vec3& normal, float depth, const Mat34& m1,
                   const Mat34& last1, const Mat34& m2, const Mat34& last2) {
    const Vec3 d1 = geom::getDisp(m1, last1, position);
    const Vec3 d2 = geom::getDisp(m2, last2, position);
    const float closing = dot({d2.x - d1.x, d2.y - d1.y, d2.z - d1.z}, normal, kZYX);
    float beyond = depth - s.penetration;
    float time;
    if (closing <= 0.0f) {
        time = FLT_MAX;
    } else if (beyond <= 0.0f) {
        time = 0.0f;
        beyond = 0.0f;
    } else {
        time = beyond / closing;
    }
    if (s.count > 0 && ((s.bestTime < s.soonTime && s.bestTime <= time) || s.bestDepth <= beyond))
        return false;
    s.bestTime = time;
    s.bestDepth = beyond;
    return true;
}

// The face box's face normal placed in the world (BoxToBoxFaceImpacts and
// BoxToBoxFaceImpactsOffset add in different orders).
Vec3 faceNormalToWorld(const FaceBox& box, const Mat34& m, const Vec3& n) {
    return box.offset ? rotate(m, n, kZYX, kZYX, kYZX) : rotate(m, n, kZYX, kXZY, kXYZ);
}

// A point in the face box's centred frame placed in the world (the Offset
// variant first moves it by the face box's centroid).
Vec3 facePointToWorld(const FaceBox& box, const Mat34& m, const Vec3& p) {
    if (box.offset)
        return place(m, {p.x + box.centroid.x, p.y + box.centroid.y, p.z + box.centroid.z}, kXZY, kZYX, kZYX);
    return place(m, p, kZYX, kXZY, kXZY);
}

void writeImpact(Impact& imp, int kind, int elementA, int elementB, const Vec3& position, const Vec3& normal,
                 float depth, Collider* a, Collider* b) {
    imp.kind = kind;
    imp.elementA = elementA;
    imp.elementB = elementB;
    imp.position = position;
    imp.normal = normal;
    imp.depth = depth;
    imp.colliderA = a;
    imp.colliderB = b;
}

// The corner box's face parallel to `face` clipped against it
// (BoxToBoxFaceImpacts' face-face case, first pass only): one impact per
// vertex of the overlap. dx, dy, dz: the corner box's axes against the face
// normal (+-1 or 0, see boxToBoxFaceImpacts).
void faceFaceImpacts(BoxPairSearch& s, const FaceBox& box, int face, const Vec3& n, int dx, int dy, int dz,
                     Impact* impacts, const Mat34& m1, const Mat34& last1, const Mat34& m2,
                     const Mat34& last2, Collider* c1, Collider* c2) {
    // The touching face of the corner box and its outward normal.
    int faceC;
    Vec3 nc;
    if (dx == 1) {
        faceC = 0;
        nc = s.axes[0];
    } else if (dx == -1) {
        faceC = 1;
        nc = -s.axes[0];
    } else if (dy == 1) {
        faceC = 2;
        nc = s.axes[1];
    } else if (dy == -1) {
        faceC = 3;
        nc = -s.axes[1];
    } else if (dz == 1) {
        faceC = 4;
        nc = s.axes[2];
    } else if (dz == -1) {
        faceC = 5;
        nc = -s.axes[2];
    } else {
        // Cannot happen (a unit axis has a component above 0.035); the
        // original would go on with uninitialised values.
        return;
    }
    const auto& vertsC = kFaceVertices[static_cast<std::size_t>(faceC)];
    const auto& vertsF = kFaceVertices[static_cast<std::size_t>(face)];

    // u: the corner box's face; v: this face, in reverse order.
    std::array<Vec2, 4> polyC{}, polyF{};
    for (std::size_t k = 0; k < 4; ++k) {
        polyC[k] = projectOnFace(s.corners[static_cast<std::size_t>(vertsC[k])], face);
        polyF[k] = projectOnFace(box.corners[static_cast<std::size_t>(vertsF[3 - k])], face);
        avoidEdgeChecks(s, kFaceEdges[static_cast<std::size_t>(faceC)][k], face, true);
    }
    std::array<ClipRecord, kMaxClipRecords> records{};
    const int found = ConvexPolyIntersect().run(4, polyC.data(), 4, polyF.data(), records.data());
    if (found == 0)
        return;
    if (found < 1) {
        s.count = -1;
        return;
    }

    // Records behind the face (negative depth) are dropped. The original
    // repeats the previous point for a record of another type (the
    // no-intersection code RecordNoIsect can leave in the first record) when
    // a flag kept in a byte of its m2 argument is set; that flag starts with
    // the pointer's high byte, taken as clear here, so such records are
    // dropped.
    std::array<float, kMaxClipRecords> depths{};
    std::array<Vec3, kMaxClipRecords> points{};
    std::array<int, kMaxClipRecords> kinds{}, elementsA{}, elementsB{};
    int count = 0;
    const Vec3& faceVertex = box.corners[static_cast<std::size_t>(vertsF[0])];
    for (int r = 0; r < found; ++r) {
        const ClipRecord& rec = records[static_cast<std::size_t>(r)];
        Vec3 point, used;
        float depth = 0;
        int elementA = 0, elementB = 0;
        if (rec.type == ClipRecord::EdgeEdge) {
            // Where the corner box's face edge u crosses this face's edge v.
            const int next = rec.u + 1 == 4 ? 0 : rec.u + 1;
            const Vec3& pa = s.corners[static_cast<std::size_t>(vertsC[static_cast<std::size_t>(next)])];
            const Vec3& pb = s.corners[static_cast<std::size_t>(vertsC[static_cast<std::size_t>(rec.u)])];
            const float t = rec.tu;
            point = {(pa.x - pb.x) * t + pb.x, (pa.y - pb.y) * t + pb.y, (pa.z - pb.z) * t + pb.z};
            depth = dot(faceVertex - point, n, kZYX);
            used = n;
            elementA = kFaceEdges[static_cast<std::size_t>(faceC)][static_cast<std::size_t>(rec.u)];
            // v runs over the face's vertices backwards: its edge k is the
            // face's edge 2 - k (3 for k = 3).
            const auto& edgesF = kFaceEdges[static_cast<std::size_t>(face)];
            elementB = rec.v == 0   ? edgesF[2]
                       : rec.v == 2 ? edgesF[0]
                                    : edgesF[static_cast<std::size_t>(rec.v)];
        } else if (rec.type == ClipRecord::UVertex) {
            // A corner of the corner box's face inside this face.
            point = s.corners[static_cast<std::size_t>(vertsC[static_cast<std::size_t>(rec.u)])];
            depth = dot(faceVertex - point, n, kZYX);
            used = n;
            elementA = vertsC[static_cast<std::size_t>(rec.u)];
            elementB = face;
        } else if (rec.type == ClipRecord::VVertex) {
            // A corner of this face inside the corner box's face: depth
            // along that face's normal. As in the original, elementA is the
            // face box's vertex and elementB the corner box's face.
            const int vertex = vertsF[static_cast<std::size_t>(3 - rec.v)];
            point = box.corners[static_cast<std::size_t>(vertex)];
            depth = dot(s.corners[static_cast<std::size_t>(vertsC[0])] - point, nc, kZYX);
            used = nc;
            elementA = vertex;
            elementB = faceC;
        } else {
            continue;
        }
        if (depth < 0.0f)
            continue;
        // The point moves half the depth towards the other box.
        const float half = depth * 0.5f;
        const auto i = static_cast<std::size_t>(count);
        depths[i] = depth;
        points[i] = {half * used.x + point.x, used.y * half + point.y, used.z * half + point.z};
        kinds[i] = rec.type;
        elementsA[i] = elementA;
        elementsB[i] = elementB;
        ++count;
    }
    if (count == 0) {
        s.count = -1;
        return;
    }

    // Their average decides whether the contact replaces the impacts so far.
    float sumDepth = depths[0];
    Vec3 sum = points[0];
    for (std::size_t i = 1; i < static_cast<std::size_t>(count); ++i) {
        sumDepth += depths[i];
        sum.x += points[i].x;
        sum.y += points[i].y;
        sum.z += points[i].z;
    }
    const float inv = 1.0f / static_cast<float>(count);
    const Vec3 normal = faceNormalToWorld(box, m2, n);
    const Vec3 average = facePointToWorld(box, m2, {sum.x * inv, sum.y * inv, sum.z * inv});
    if (!useThisImpact(s, average, normal, inv * sumDepth, m1, last1, m2, last2))
        return;
    // The points themselves are placed without the Offset variant's
    // centroid (as in the original).
    const Vec3 normalC = box.offset ? rotate(m2, nc, kXZY, kZYX, kZYX) : rotate(m2, nc, kXZY, kXZY, kXZY);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        const Vec3 position =
            box.offset ? place(m2, points[i], kZYX, kXZY, kXZY) : place(m2, points[i], kZYX, kZYX, kZYX);
        writeImpact(impacts[i], kinds[i], elementsA[i], elementsB[i], position,
                    kinds[i] == Impact::VertexB ? -normalC : normal, depths[i], c1, c2);
    }
    s.count = count;
}

// An edge of the corner box parallel to `face` (BoxToBoxFaceImpacts'
// edge-face case): the part of it over the face gives one or two impacts.
// corner: the corner box's deepest corner; p: its position.
void edgeFaceImpacts(BoxPairSearch& s, const FaceBox& box, int face, const Vec3& n, int corner, const Vec3& p,
                     int dx, int dy, int dz, Impact* impacts, bool first, const Mat34& m1, const Mat34& last1,
                     const Mat34& m2, const Mat34& last2, Collider* c1, Collider* c2) {
    // The edge through the deepest corner along the zero component.
    int edge;
    if (dx == 1)
        edge = dy == 1 ? 8 : dy == 0 ? (dz != 1 ? 7 : 3) : 11;
    else if (dx == 0)
        edge = dy == 1 ? (dz != 1 ? 4 : 0) : (dz != 1 ? 6 : 2);
    else
        edge = dy == 1 ? 9 : dy == 0 ? (dz != 1 ? 5 : 1) : 10;
    avoidEdgeChecks(s, edge, face, first);
    int other = kEdgeVertices[static_cast<std::size_t>(edge)][0];
    if (other == corner)
        other = kEdgeVertices[static_cast<std::size_t>(edge)][1];
    const Vec3& q = s.corners[static_cast<std::size_t>(other)];
    const Vec3 dir{q.x - p.x, q.y - p.y, q.z - p.z};

    float t0, t1;
    int side0, side1;
    if (!geom::findTValuesLineToBoxFace(p, dir, n, box.half, t0, t1, side0, side1)) {
        addEdgeChecks(s, corner, face, first);
        addEdgeChecks(s, other, face, first);
        return;
    }
    if (t0 < 0.0f)
        t0 = 0.0f;
    if (!(t1 <= 1.0f))
        t1 = 1.0f;
    const Vec3 q0{dir.x * t0 + p.x, dir.y * t0 + p.y, dir.z * t0 + p.z};
    const Vec3 q1{dir.x * t1 + p.x, dir.y * t1 + p.y, dir.z * t1 + p.z};
    const float plane = std::fabs(dot(n, box.half, kZYX));
    const float depth1 = plane - dot(q1, n, kZYX);
    const float depth0 = plane - dot(q0, n, kZYX);
    const auto& edgesF = kFaceEdges[static_cast<std::size_t>(face)];

    if (depth0 < 0.0f) {
        if (depth1 < 0.0f) {
            s.count = -1;
            return;
        }
        // Only the far end is in.
        const Vec3 normal = faceNormalToWorld(box, m2, n);
        const float half1 = depth1 * 0.5f;
        const Vec3 position =
            facePointToWorld(box, m2, {n.x * half1 + q1.x, q1.y + n.y * half1, q1.z + n.z * half1});
        int kind, elementA, elementB;
        if (t1 == 1.0f) {
            kind = Impact::VertexA;
            elementA = other;
            elementB = face;
            addEdgeChecks(s, other, face, first);
        } else {
            kind = Impact::EdgeEdge;
            elementA = edge;
            elementB = edgesF[static_cast<std::size_t>(side1)];
        }
        if (useThisImpact(s, position, normal, depth1, m1, last1, m2, last2)) {
            writeImpact(impacts[0], kind, elementA, elementB, position, normal, depth1, c1, c2);
            s.count = 1;
        }
        return;
    }

    const Vec3 normal = faceNormalToWorld(box, m2, n);
    const float half0 = depth0 * 0.5f;
    const Vec3 position0 =
        facePointToWorld(box, m2, {n.x * half0 + q0.x, q0.y + n.y * half0, q0.z + n.z * half0});
    if (depth1 < 0.0f) {
        // Only the near end is in.
        int kind, elementA, elementB;
        if (t0 == 0.0f) {
            kind = Impact::VertexA;
            elementA = corner;
            elementB = face;
            addEdgeChecks(s, corner, face, first);
        } else {
            kind = Impact::EdgeEdge;
            elementA = edge;
            elementB = edgesF[static_cast<std::size_t>(side0)];
        }
        if (useThisImpact(s, position0, normal, depth0, m1, last1, m2, last2)) {
            writeImpact(impacts[0], kind, elementA, elementB, position0, normal, depth0, c1, c2);
            s.count = 1;
        }
        return;
    }

    // Both ends are in: two impacts, judged by their midpoint. The far one
    // is placed first and then moved along the world normal.
    Vec3 position1 = facePointToWorld(box, m2, q1);
    const float half1 = depth1 * 0.5f;
    position1 = {half1 * normal.x + position1.x, normal.y * half1 + position1.y,
                 normal.z * half1 + position1.z};
    const Vec3 middle{(position1.x + position0.x) * 0.5f, (position1.y + position0.y) * 0.5f,
                      (position1.z + position0.z) * 0.5f};
    if (!useThisImpact(s, middle, normal, (depth1 + depth0) * 0.5f, m1, last1, m2, last2))
        return;
    Impact& nearEnd = impacts[0];
    Impact& farEnd = impacts[1];
    nearEnd.depth = depth0;
    nearEnd.normal = normal;
    nearEnd.position = position0;
    farEnd.depth = depth1;
    farEnd.normal = normal;
    farEnd.position = position1;
    if (t0 == 0.0f) {
        nearEnd.kind = Impact::VertexA;
        nearEnd.elementA = corner;
        nearEnd.elementB = face;
        addEdgeChecks(s, corner, face, first);
    } else {
        nearEnd.kind = Impact::EdgeEdge;
        nearEnd.elementA = edge;
        nearEnd.elementB = edgesF[static_cast<std::size_t>(side0)];
    }
    if (t1 <= t0) {
        // One point only. The original returns here without setting this
        // impact's colliders (impacts[0] keeps whatever the buffer held,
        // usually this pair's from an earlier candidate); OpenMM2 sets them
        // so a stale entry can never name another pair's bodies.
        nearEnd.colliderA = c1;
        nearEnd.colliderB = c2;
        s.count = 1;
        return;
    }
    if (t1 == 1.0f) {
        farEnd.kind = Impact::VertexA;
        farEnd.elementA = other;
        farEnd.elementB = face;
        addEdgeChecks(s, other, face, first);
    } else {
        farEnd.kind = Impact::EdgeEdge;
        farEnd.elementA = edge;
        farEnd.elementB = edgesF[static_cast<std::size_t>(side1)];
    }
    nearEnd.colliderA = farEnd.colliderA = c1;
    nearEnd.colliderB = farEnd.colliderB = c2;
    s.count = 2;
}

// phBoundBox::BoxToBoxFaceImpacts / BoxToBoxFaceImpactsOffset: the corner
// box (s.corners, placed by MakeTransformedCorners) against each face of
// `box` that it was in front of at the end of the last sample (relLast: its
// centre then, in the face box's centred frame). Its deepest corner decides
// the case: a corner on the face, an edge along it or (first pass only) a
// face on it. Impacts replace the list when UseThisImpact prefers them
// (they are written from impacts[0]); colliderA is the corner box's (c1),
// normals point from the face box towards it. m1/last1 place the corner
// box, m2/last2 the face box. Sets s.count to -1 on finding a separating
// face.
void boxToBoxFaceImpacts(BoxPairSearch& s, const FaceBox& box, const Vec3& relLast, Impact* impacts,
                         bool first, const Mat34& m1, const Mat34& last1, const Mat34& m2, const Mat34& last2,
                         Collider* c1, Collider* c2) {
    for (int face = 0; face < 6; ++face) {
        const Vec3& n = BoundBox::faceNormals()[static_cast<std::size_t>(face)];
        if (!(dot(n, relLast, kXYZ) > 0.0f))
            continue;

        // The face normal's components along the corner box's axes as
        // FindFaceDots signs, oriented so that they are the signs of the
        // corner box's deepest corner (0 where an edge or face is parallel).
        const bool even = (face & 1) == 0;
        const int toward = even ? -1 : 1;
        const auto axis = static_cast<std::size_t>(face >> 1);
        std::array<int, 3> dots{}, signs{};
        for (std::size_t k = 0; k < 3; ++k) {
            dots[k] = first ? s.faceDot[k][axis] : s.faceDot[axis][k];
            signs[k] = first ? s.faceSign[k][axis] : s.faceSign[axis][k];
        }
        const int dx = dots[0] * toward, dy = dots[1] * toward, dz = dots[2] * toward;
        const int bx = (signs[0] >= 0) != even ? 1 : 0;
        const int by = (signs[1] >= 0) != even ? 1 : 0;
        const int bz = (signs[2] >= 0) != even ? 1 : 0;
        const int corner = kCornerBySign[static_cast<std::size_t>(bx + 2 * by + 4 * bz)];
        const Vec3 p = s.corners[static_cast<std::size_t>(corner)];

        const float plane = std::fabs(dot(n, box.half, kZYX));
        const float depth = plane - (box.offset ? dot(n, p, kZYX) : dot(n, p, kXZY));
        if (depth < 0.0f) {
            // The deepest corner is in front of the face: they are apart.
            s.count = -1;
            return;
        }

        if (dx * dy * dz != 0) {
            // A corner on the face, when it lies within the face.
            const bool within = ((face == 0 || face == 1) && std::fabs(p.y) <= box.half.y &&
                                 std::fabs(p.z) <= box.half.z) ||
                                ((face == 2 || face == 3) && std::fabs(p.x) <= box.half.x &&
                                 std::fabs(p.z) <= box.half.z) ||
                                ((face == 4 || face == 5) && std::fabs(p.x) <= box.half.x &&
                                 std::fabs(p.y) <= box.half.y);
            if (within) {
                const Vec3 normal = faceNormalToWorld(box, m2, n);
                const float half = depth * 0.5f;
                const Vec3 position =
                    facePointToWorld(box, m2, {n.x * half + p.x, n.y * half + p.y, n.z * half + p.z});
                if (useThisImpact(s, position, normal, depth, m1, last1, m2, last2)) {
                    writeImpact(impacts[0], Impact::VertexA, corner, face, position, normal, depth, c1, c2);
                    s.count = 1;
                } else {
                    // The other corners closer to the face than its extent
                    // get edge checks. The original compares them (in the
                    // face box's frame) with the world normal it just
                    // computed, as kept here.
                    const float limit = std::fabs(dot(normal, box.half, kZYX));
                    for (int c = 0; c < 8; ++c) {
                        const Vec3& pc = s.corners[static_cast<std::size_t>(c)];
                        const float along = box.offset ? dot(normal, pc, kXZY) : dot(normal, pc, kZXY);
                        if (c != corner && along < limit)
                            addEdgeChecks(s, c, face, first);
                    }
                }
            }
            addEdgeChecks(s, corner, face, first);
        } else if (dx * dy == 0 && dz * dy == 0 && dz * dx == 0) {
            // Two zeros: a face of the corner box lies on this face.
            addEdgeChecks(s, corner, face, first);
            if (first)
                faceFaceImpacts(s, box, face, n, dx, dy, dz, impacts, m1, last1, m2, last2, c1, c2);
        } else {
            edgeFaceImpacts(s, box, face, n, corner, p, dx, dy, dz, impacts, first, m1, last1, m2, last2, c1,
                            c2);
        }
        if (s.count == -1)
            return;
    }
}

// The edge pass of FindImpactsBoxToBox(Offset): pairs of edges flagged by
// both face passes (and not covered by a face contact) whose Gauss-map arcs
// cross, closest points from SegSegDistNorm. MakeTransformedCorners has put
// B's corners and axes in A's centred frame. Returns the final impact count.
int boxEdgeImpacts(BoxPairSearch& s, const BoundBox& a, const BoundBox& b, const Mat34& ma,
                   const Mat34& lastA, const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb,
                   Impact* impacts, const Vec3& bInA, bool offset) {
    // B's face normals in A's frame.
    const std::array<Vec3, 6> normalsB{s.axes[0], -s.axes[0], s.axes[1], -s.axes[1], s.axes[2], -s.axes[2]};
    const auto& normalsA = BoundBox::faceNormals();
    const Vec3 halfA = offset ? a.boxMax - a.centroid : a.boxMax;
    for (std::size_t i = 0; i < 12; ++i) {
        for (std::size_t j = 0; j < 12; ++j) {
            if (s.edgeChecks[i][j] != kCheckEdges)
                continue;
            const Vec3& sa0 = kCornerSigns[static_cast<std::size_t>(kEdgeVertices[i][0])];
            const Vec3& sa1 = kCornerSigns[static_cast<std::size_t>(kEdgeVertices[i][1])];
            const Vec3 dA{(sa1.x - sa0.x) * halfA.x, (sa1.y - sa0.y) * halfA.y, (sa1.z - sa0.z) * halfA.z};
            const Vec3& nB1 = normalsB[static_cast<std::size_t>(kEdgeFaces[j][0])];
            const Vec3& nB2 = normalsB[static_cast<std::size_t>(kEdgeFaces[j][1])];
            const float sB2 = offset ? dot(dA, nB2, kZXY) : dot(dA, nB2, kZYX);
            const float sB1 = offset ? dot(dA, nB1, kYXZ) : dot(dA, nB1, kXZY);
            if (std::signbit(sB2) == std::signbit(sB1))
                continue;
            const Vec3& nA1 = normalsA[static_cast<std::size_t>(kEdgeFaces[i][0])];
            const Vec3& nA2 = normalsA[static_cast<std::size_t>(kEdgeFaces[i][1])];
            const Vec3& b0 = s.corners[static_cast<std::size_t>(kEdgeVertices[j][0])];
            const Vec3& b1 = s.corners[static_cast<std::size_t>(kEdgeVertices[j][1])];
            const Vec3 dB = b1 - b0;
            const float sA2 = offset ? dot(dB, nA2, kYZX) : dot(dB, nA2, kZXY);
            const float sA1 = offset ? dot(dB, nA1, kXZY) : dot(dB, nA1, kZYX);
            if (std::signbit(sA2) == std::signbit(sA1))
                continue;
            if (!(dot(nA2 + nA1, nB2 + nB1, kXZY) < 0.0f))
                continue;

            const Vec3 a0 = sa0.mul(halfA);
            const Vec3 a1 = sa1.mul(halfA);
            Vec3 normal, pointA, pointB;
            float distance;
            int ok;
            geom::segSegDistNorm(a0, a1, dA, b0, b1, dB, normal, pointA, pointB, distance, ok);
            if (ok == 0)
                continue;
            // normal points from B's edge towards A's edge.
            const float along1 = dot(normal, nB1, kZYX);
            const float along2 = offset ? dot(normal, nB2, kXZY) : dot(normal, nB2, kZYX);
            if (along1 > 0.0f) {
                if (along2 > 0.0f)
                    return 0; // A's edge is outside both of B's faces: apart.
                continue;
            }
            if (along2 > 0.0f || !(dot(normal, bInA, kXZY) > 0.0f))
                continue;

            const float half = distance * 0.5f;
            Vec3 middle{normal.x * half + pointB.x, normal.y * half + pointB.y, normal.z * half + pointB.z};
            Vec3 position;
            if (offset) {
                // The original adds B's centroid here and takes A's off the
                // world position (mixing frames); kept as is.
                middle = {middle.x + b.centroid.x, middle.y + b.centroid.y, middle.z + b.centroid.z};
                position = place(ma, middle, kYXZ, kZYX, kXZY);
                position = {position.x - a.centroid.x, position.y - a.centroid.y, position.z - a.centroid.z};
            } else {
                position = place(ma, middle, kXZY, kZYX, kXZY);
            }
            const Vec3 back = -normal;
            const Vec3 normalW =
                offset ? rotate(ma, back, kXZY, kXZY, kXZY) : rotate(ma, back, kXZY, kZYX, kXZY);
            if (!useThisImpact(s, position, normalW, distance, ma, lastA, mb, lastB))
                continue;
            writeImpact(impacts[0], Impact::EdgeEdge, static_cast<int>(i), static_cast<int>(j), position,
                        normalW, distance, ca, cb);
            s.count = 1;
        }
    }
    if (s.count == 0)
        return 0;
    for (int k = 0; k < s.count; ++k) {
        impacts[k].componentA = -1;
        impacts[k].componentB = -1;
        impacts[k].penetration = s.penetration;
    }
    return s.count;
}

// phBoundBox::FindImpactsBoxToBoxOffset (reached from FindImpactsBoxToBox
// when either box has a centroid): as the plain version, with each box's
// frame centred on its centroid.
int findImpactsBoxToBoxOffset(BoxPairSearch& s, const BoundBox& a, const BoundBox& b, const Mat34& ma,
                              const Mat34& lastA, const Mat34& mb, const Mat34& lastB, Collider* ca,
                              Collider* cb, Impact* impacts, const Vec3& relPos) {
    const Vec3& cA = a.centroid;
    const Vec3& cB = b.centroid;
    // A's centre in B's centred frame, now and at the end of the last sample.
    const Vec3 offsetA = rotate(ma, cA, kYZX, kXYZ, kZYX);
    const Vec3 fromB{offsetA.x - relPos.x, offsetA.y - relPos.y, offsetA.z - relPos.z};
    const Vec3 aInB{dot(mb.m0, fromB, kZYX) - cB.x, dot(mb.m1, fromB, kZYX) - cB.y,
                    dot(mb.m2, fromB, kZYX) - cB.z};
    const Vec3 lastCentreA = place(lastA, cA, kZYX, kZYX, kZYX);
    const Vec3 lastFromB{lastCentreA.x - lastB.m3.x, lastCentreA.y - lastB.m3.y, lastCentreA.z - lastB.m3.z};
    const Vec3 lastAInB{dot(lastB.m0, lastFromB, kZYX) - cB.x, dot(lastB.m1, lastFromB, kZYX) - cB.y,
                        dot(lastB.m2, lastFromB, kZYX) - cB.z};
    const Vec3 halfA = a.boxMax - cA;
    const Vec3 halfB = b.boxMax - cB;

    makeTransformedCorners(s, halfA, aInB, true);
    classifyRotation(s);
    s.count = 0;
    // The face box's corners around its centre (corners 4..7 mirror 2, 3, 0, 1).
    const auto centredCorners = [](const Vec3& half) {
        std::array<Vec3, 8> c{};
        for (std::size_t k = 0; k < 4; ++k)
            c[k] = kCornerSigns[k].mul(half);
        c[4] = -c[2];
        c[5] = -c[3];
        c[6] = -c[0];
        c[7] = -c[1];
        return c;
    };
    const FaceBox faceB{halfB, cB, centredCorners(halfB), true};
    boxToBoxFaceImpacts(s, faceB, lastAInB, impacts, true, ma, lastA, mb, lastB, ca, cb);
    if (s.count == -1)
        return 0;

    // B's centre in A's centred frame, now and then.
    const Vec3 offsetB = rotate(mb, cB, kZYX, kZYX, kZYX);
    const Vec3 fromA{offsetB.x + relPos.x, offsetB.y + relPos.y, offsetB.z + relPos.z};
    const Vec3 bInA{dot(ma.m0, fromA, kZYX) - cA.x, dot(ma.m1, fromA, kZYX) - cA.y,
                    dot(ma.m2, fromA, kZYX) - cA.z};
    makeTransformedCorners(s, halfB, bInA, false);
    const Vec3 lastCentreB = place(lastB, cB, kXZY, kXZY, kXZY);
    const Vec3 lastFromA{lastCentreB.x - lastA.m3.x, lastCentreB.y - lastA.m3.y, lastCentreB.z - lastA.m3.z};
    const Vec3 lastBInA{dot(lastA.m0, lastFromA, kZYX) - cA.x, dot(lastA.m1, lastFromA, kZYX) - cA.y,
                        dot(lastA.m2, lastFromA, kZYX) - cA.z};
    const FaceBox faceA{halfA, cA, centredCorners(halfA), true};
    boxToBoxFaceImpacts(s, faceA, lastBInA, impacts, false, mb, lastB, ma, lastA, cb, ca);
    if (s.count == -1)
        return 0;

    return boxEdgeImpacts(s, a, b, ma, lastA, mb, lastB, ca, cb, impacts, bInA, true);
}

std::array<Vec3, 8> boxCorners(const BoundBox& box) {
    std::array<Vec3, 8> c{};
    for (std::size_t k = 0; k < 8; ++k)
        c[k] = box.vertex(static_cast<int>(k));
    return c;
}

} // namespace

// --- phBoundBox segment tests -------------------------------------------------------------------

int BoundBox::testEdge(Segment& seg, Intersection* out, int max) const {
    // phBoundBox::TestEdge: where the edge enters (b inside) and leaves (a
    // inside) the box; the original does not check `max` (it writes up to 2).
    BoxClip c;
    if (!clipToBox(seg, boxMin, boxMax, c))
        return 0;
    int count = 0;
    if (c.entryFace >= 0) {
        Intersection& hit = out[0];
        fillCrossing(*this, seg, c.entryFace, c.tEnter, seg.b, hit);
        hit.bInside = true;
        hit.polygon = c.entryFace;
        hit.poly = &polygons[static_cast<std::size_t>(c.entryFace)];
        count = 1;
    }
    // (OpenMM2 guard: the original writes the exit even when the caller's
    // table has no room left.)
    if (c.exitFace >= 0 && count < max) {
        Intersection& hit = out[count];
        fillCrossing(*this, seg, c.exitFace, c.tExit, seg.a, hit);
        hit.bInside = false;
        hit.polygon = c.exitFace;
        hit.poly = &polygons[static_cast<std::size_t>(c.exitFace)];
        ++count;
    }
    return count;
}

bool BoundBox::testProbe(Segment& seg, Intersection& out, float maxT) const {
    // phBoundBox::TestProbe / TestProbeSlave: the face the probe enters
    // through at t <= maxT.
    BoxClip c;
    if (!clipToBox(seg, boxMin, boxMax, c) || !(c.tEnter <= maxT) || c.entryFace < 0)
        return false;
    fillCrossing(*this, seg, c.entryFace, c.tEnter, seg.b, out);
    out.bInside = true;
    out.polygon = c.entryFace;
    out.poly = &polygons[static_cast<std::size_t>(c.entryFace)];
    return true;
}

// --- phBoundBox::FindImpactSphereToBox -----------------------------------------------------------

int findImpactSphereToBox(const BoundBox& box, const BoundSphere& sphere, const Mat34& sphereM,
                          const Mat34& boxM, Collider* sphereCollider, Collider* boxCollider, Impact* impacts,
                          const Vec3& relPos, const Vec3& relDisp) {
    // relPos: the sphere's position minus the box's; relDisp: the sphere's
    // displacement relative to the box over the sample (both world). The
    // sphere's centre is swept back by relDisp in the box's centred frame.
    const Vec3 half = box.boxMax - box.centroid;
    Vec3 c{dot(boxM.m0, relPos, kXYZ), dot(boxM.m1, relPos, kYZX), dot(boxM.m2, relPos, kZYX)};
    if (box.isOffset)
        c = {c.x - box.centroid.x, c.y - box.centroid.y, c.z - box.centroid.z};
    if (sphere.isOffset) {
        const Vec3 w = rotate(sphereM, sphere.centroid, kYZX, kXYZ, kXYZ);
        c = {dot(w, boxM.m0, kZYX) + c.x, dot(w, boxM.m1, kZYX) + c.y, dot(boxM.m2, w, kZYX) + c.z};
    }
    const float radius = sphere.radius;
    Vec3 disp{dot(boxM.m0, relDisp, kZXY), dot(boxM.m1, relDisp, kZYX), dot(boxM.m2, relDisp, kZYX)};
    const Vec3 start{c.x - disp.x, c.y - disp.y, c.z - disp.z};
    const float ex = half.x + radius, ey = half.y + radius, ez = half.z + radius;

    float t0 = 0, t1 = 0;
    Vec3 n0, n1;
    int f0 = 0, f1 = 0;
    int kind = 0, element = 0;
    Vec3 f; // the closest point of the box (then the impact position, box frame)
    Vec3 n; // from the box towards the sphere's centre
    float depth = 0;
    bool centreInside = false;
    if (geom::segmentToBoxIntersections(start, disp, ex, ey, ez, t0, t1, n0, n1, f0, f1) != 0) {
        // The sweep reaches the box grown by the radius: continue from there
        // with the rest of the displacement.
        c = {disp.x * t0 + start.x, disp.y * t0 + start.y, disp.z * t0 + start.z};
        const float rest = 1.0f - t0;
        disp = {rest * disp.x, disp.y * rest, disp.z * rest};
    } else if (geom::isPointInBox(c, half.x, half.y, half.z)) {
        // The centre is inside the box: out through the nearest face (by
        // the original's signed ratios), the impact half way.
        centreInside = true;
        f = c;
        kind = Impact::VertexA;
        if (c.x * half.y < c.y * half.x || c.z * half.x > c.x * half.z) {
            if (c.y * half.x < c.x * half.y || c.z * half.y > c.y * half.z) {
                element = c.z > 0.0f ? 4 : 5;
                f.z = c.z > 0.0f ? half.z : -half.z;
            } else {
                element = c.y > 0.0f ? 2 : 3;
                f.y = c.y > 0.0f ? half.y : -half.y;
            }
        } else {
            element = c.x > 0.0f ? 0 : 1;
            f.x = c.x > 0.0f ? half.x : -half.x;
        }
        const Vec3 d{f.x - c.x, f.y - c.y, f.z - c.z};
        const float length = std::sqrt((d.y * d.y + d.z * d.z) + d.x * d.x);
        n = {d.x / length, d.y / length, d.z / length};
        depth = length + radius;
        const float halfDepth = depth * 0.5f;
        f = {f.x - n.x * halfDepth, f.y - n.y * halfDepth, f.z - n.z * halfDepth};
    } else if (!geom::isPointInBox(c, ex, ey, ez)) {
        return 0;
    } else {
        disp = {0.0f, 0.0f, 0.0f};
    }

    if (!centreInside) {
        // The box's closest feature to the centre: kind 1 a face, 0 an edge,
        // 2 a corner (VertexA / EdgeEdge / VertexB), element its index; the
        // impact at that point moved by the rest of the displacement.
        const auto clampAxis = [](float v, float h, float& out) {
            if (v > h) {
                out = h;
                return 1;
            }
            if (v < -h) {
                out = -h;
                return -1;
            }
            out = v;
            return 0;
        };
        const int sx = clampAxis(c.x, half.x, f.x);
        const int sy = clampAxis(c.y, half.y, f.y);
        const int sz = clampAxis(c.z, half.z, f.z);
        const int outside = (sx != 0) + (sy != 0) + (sz != 0);
        if (outside == 0)
            return 0; // Cannot happen after the tests above (the original reads uninitialised values).
        if (outside == 3) {
            kind = Impact::VertexB;
            element = kCornerBySign[static_cast<std::size_t>((sx > 0) + 2 * (sy > 0) + 4 * (sz > 0))];
        } else if (outside == 2) {
            kind = Impact::EdgeEdge;
            if (sz == 0)
                element = sx > 0 ? (sy > 0 ? 8 : 11) : (sy > 0 ? 9 : 10);
            else if (sy == 0)
                element = sx > 0 ? (sz > 0 ? 3 : 7) : (sz > 0 ? 1 : 5);
            else
                element = sy > 0 ? (sz > 0 ? 0 : 4) : (sz > 0 ? 2 : 6);
        } else {
            kind = Impact::VertexA;
            element = sx != 0 ? (sx > 0 ? 0 : 1) : sy != 0 ? (sy > 0 ? 2 : 3) : (sz > 0 ? 4 : 5);
        }
        const Vec3 d{c.x - f.x, c.y - f.y, c.z - f.z};
        const float length = std::sqrt((d.y * d.y + d.z * d.z) + d.x * d.x);
        n = {d.x / length, d.y / length, d.z / length};
        depth = (radius - length) - dot(n, disp, kZYX);
        if (depth < 0.0f)
            return 0;
        f = {disp.x + f.x, disp.y + f.y, f.z + disp.z};
    }

    const Vec3 p{f.x + box.centroid.x, f.y + box.centroid.y, f.z + box.centroid.z};
    const Vec3 position = place(boxM, p, kYZX, kZYX, kZYX);
    const Vec3 normal = rotate(boxM, n, kYXZ, kZYX, kYXZ);
    impacts[0].makeNewImpact(sphereCollider, boxCollider, position, normal, depth, sphere, box, kind, 0,
                             element);
    return 1;
}

// --- phBoundBox::FindImpactsBoxToBox -------------------------------------------------------------

int findImpactsBoxToBox(const BoundBox& a, const BoundBox& b, const Mat34& ma, const Mat34& lastA,
                        const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb, Impact* impacts,
                        int max, const Vec3& relPos) {
    // relPos: B's position minus A's (world). The two face passes and the
    // edge pass each keep one contact (a corner, an edge or a face of one
    // box on a face of the other, or two crossing edges), replacing the
    // impacts found before when UseThisImpact prefers it.
    if (max < 8)
        return 0;
    BoxPairSearch s;
    s.rotation[0][0] = dot(ma.m0, mb.m0, kYXZ);
    s.rotation[0][1] = dot(ma.m0, mb.m1, kZYX);
    s.rotation[0][2] = dot(ma.m0, mb.m2, kZYX);
    s.rotation[1][0] = dot(ma.m1, mb.m0, kYZX);
    s.rotation[1][1] = dot(ma.m1, mb.m1, kZYX);
    s.rotation[1][2] = dot(ma.m1, mb.m2, kZYX);
    s.rotation[2][0] = dot(ma.m2, mb.m0, kYZX);
    s.rotation[2][1] = dot(ma.m2, mb.m1, kZYX);
    s.rotation[2][2] = dot(ma.m2, mb.m2, kZYX);
    s.penetration = a.penetration < b.penetration ? a.penetration : b.penetration;
    s.soonTime = sampleTime().seconds * 1.5f;
    if (a.centroid.x != 0.0f || a.centroid.y != 0.0f || a.centroid.z != 0.0f || b.centroid.x != 0.0f ||
        b.centroid.y != 0.0f || b.centroid.z != 0.0f)
        return findImpactsBoxToBoxOffset(s, a, b, ma, lastA, mb, lastB, ca, cb, impacts, relPos);

    // A's centre in B's frame, now and at the end of the last sample.
    const Vec3 back = -relPos;
    const Vec3 aInB{dot(back, mb.m0, kYXZ), dot(mb.m1, back, kZYX), dot(mb.m2, back, kZYX)};
    const Vec3 lastFromB = lastA.m3 - lastB.m3;
    const Vec3 lastAInB{dot(lastFromB, lastB.m0, kZYX), dot(lastB.m1, lastFromB, kZYX),
                        dot(lastB.m2, lastFromB, kZYX)};
    makeTransformedCorners(s, a.boxMax, aInB, true);
    classifyRotation(s);
    s.count = 0;
    const FaceBox faceB{b.boxMax, {}, boxCorners(b), false};
    boxToBoxFaceImpacts(s, faceB, lastAInB, impacts, true, ma, lastA, mb, lastB, ca, cb);
    if (s.count == -1)
        return 0;

    // B's centre in A's frame, now and then.
    const Vec3 bInA{dot(ma.m0, relPos, kZYX), dot(ma.m1, relPos, kZYX), dot(ma.m2, relPos, kZYX)};
    makeTransformedCorners(s, b.boxMax, bInA, false);
    const Vec3 lastFromA = lastB.m3 - lastA.m3;
    const Vec3 lastBInA{dot(lastFromA, lastA.m0, kZYX), dot(lastA.m1, lastFromA, kZYX),
                        dot(lastA.m2, lastFromA, kZYX)};
    const FaceBox faceA{a.boxMax, {}, boxCorners(a), false};
    boxToBoxFaceImpacts(s, faceA, lastBInA, impacts, false, mb, lastB, ma, lastA, cb, ca);
    if (s.count == -1)
        return 0;

    return boxEdgeImpacts(s, a, b, ma, lastA, mb, lastB, ca, cb, impacts, bInA, false);
}

} // namespace mm2::phys

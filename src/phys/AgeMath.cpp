// Vector3 / Matrix34 routines of the Angel engine as midtown2.exe build 3393
// compiles them (MM2Recomp), with the original's summation order. Comments
// name the MM2 member each function reproduces.

#include "phys/AgeMath.h"

#include "core/Libm.h"

#include <cmath>

namespace mm2::phys::age {
namespace {

// Matrix34::MakeRotateX / MakeRotateY / MakeRotateZ (cos and sin rounded
// from the FPU's wide result).
Mat34 makeRotateX(float angle) {
    const float c = static_cast<float>(libm::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(libm::sin(static_cast<double>(angle)));
    return {{1.0f, 0.0f, 0.0f}, {0.0f, c, s}, {0.0f, -s, c}, {}};
}

Mat34 makeRotateY(float angle) {
    const float c = static_cast<float>(libm::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(libm::sin(static_cast<double>(angle)));
    return {{c, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {s, 0.0f, c}, {}};
}

Mat34 makeRotateZ(float angle) {
    const float c = static_cast<float>(libm::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(libm::sin(static_cast<double>(angle)));
    return {{c, s, 0.0f}, {-s, c, 0.0f}, {0.0f, 0.0f, 1.0f}, {}};
}

} // namespace

float mag(const Vec3& v) {
    return std::sqrt(mag2(v));
}

float invMag(const Vec3& v) {
    const float m2 = mag2(v);
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

Vec3 dot3x3(const Vec3& v, const Mat34& m) {
    return {(m.m2.x * v.z + m.m1.x * v.y) + m.m0.x * v.x, (m.m2.y * v.z + m.m0.y * v.x) + m.m1.y * v.y,
            (m.m2.z * v.z + m.m0.z * v.x) + m.m1.z * v.y};
}

Vec3 dot3x3Transpose(const Vec3& v, const Mat34& m) {
    return {(m.m0.z * v.z + m.m0.y * v.y) + m.m0.x * v.x, (m.m1.z * v.z + m.m1.x * v.x) + m.m1.y * v.y,
            (m.m2.z * v.z + m.m2.x * v.x) + m.m2.y * v.y};
}

Mat34 makeRotateUnitAxis(const Vec3& n, float angle) {
    // The diagonal adds the cosine as fcos left it (wider than a float); the
    // products before it round to float.
    const double c = libm::cos(static_cast<double>(angle));
    const float s = static_cast<float>(libm::sin(static_cast<double>(angle)));
    const float omc = static_cast<float>(1.0 - c);
    const auto diagonal = [&](float k) {
        const float p = (k * k) * omc;
        return static_cast<float>(static_cast<double>(p) + c);
    };
    Mat34 r;
    r.m0.x = diagonal(n.x);
    r.m1.y = diagonal(n.y);
    r.m2.z = diagonal(n.z);
    r.m0.y = (n.y * n.x) * omc + s * n.z;
    r.m1.x = (n.y * n.x) * omc - s * n.z;
    r.m0.z = (n.z * n.x) * omc - s * n.y;
    r.m2.x = (n.z * n.x) * omc + s * n.y;
    r.m1.z = (n.z * n.y) * omc + s * n.x;
    r.m2.y = (n.z * n.y) * omc - s * n.x;
    r.m3 = {};
    return r;
}

Mat34 makeRotate(const Vec3& axis, float angle) {
    if (angle == 0.0f)
        return Mat34::identity();
    if (axis.x == 0.0f) {
        if (axis.y == 0.0f)
            return makeRotateZ(0.0f < axis.z ? angle : -angle);
        if (axis.z == 0.0f)
            return makeRotateY(0.0f < axis.y ? angle : -angle);
    } else if (axis.y == 0.0f && axis.z == 0.0f) {
        return makeRotateX(0.0f < axis.x ? angle : -angle);
    }
    const float len2 = (axis.z * axis.z + axis.y * axis.y) + axis.x * axis.x;
    const float inv = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
    return makeRotateUnitAxis({inv * axis.x, inv * axis.y, inv * axis.z}, angle);
}

void rotate(Mat34& m, const Vec3& axis, float angle) {
    dot3x3InPlace(m, makeRotate(axis, angle));
}

void rotateUnitAxis(Mat34& m, const Vec3& axis, float angle) {
    dot3x3InPlace(m, makeRotateUnitAxis(axis, angle));
}

Mat34 arbitraryRotation(const Vec3& axis, float angle) {
    return makeRotate(axis, angle);
}

Mat34 transpose(const Mat34& m) {
    Mat34 r;
    r.m0 = {m.m0.x, m.m1.x, m.m2.x};
    r.m1 = {m.m0.y, m.m1.y, m.m2.y};
    r.m2 = {m.m0.z, m.m1.z, m.m2.z};
    r.m3 = m.m3;
    return r;
}

Mat34 inverse(const Mat34& a) {
    const float c00 = a.m1.y * a.m2.z - a.m2.y * a.m1.z;
    const float c10 = a.m1.x * a.m2.z - a.m2.x * a.m1.z;
    const float c20 = a.m2.y * a.m1.x - a.m2.x * a.m1.y;
    const float det = (c00 * a.m0.x - c10 * a.m0.y) + c20 * a.m0.z;
    if (det == 0.0f)
        return a;
    const float inv = 1.0f / det;
    Mat34 r;
    r.m0.x = inv * c00;
    r.m1.x = -(inv * c10);
    r.m2.x = inv * c20;
    r.m3.x = -((r.m0.x * a.m3.x + r.m2.x * a.m3.z) + r.m1.x * a.m3.y);
    r.m0.y = -((a.m0.y * a.m2.z - a.m2.y * a.m0.z) * inv);
    r.m1.y = (a.m0.x * a.m2.z - a.m2.x * a.m0.z) * inv;
    r.m2.y = -((a.m2.y * a.m0.x - a.m2.x * a.m0.y) * inv);
    r.m3.y = -((r.m0.y * a.m3.x + r.m2.y * a.m3.z) + r.m1.y * a.m3.y);
    r.m0.z = (a.m0.y * a.m1.z - a.m0.z * a.m1.y) * inv;
    r.m1.z = -((a.m0.x * a.m1.z - a.m1.x * a.m0.z) * inv);
    r.m2.z = (a.m1.y * a.m0.x - a.m1.x * a.m0.y) * inv;
    r.m3.z = -((r.m0.z * a.m3.x + r.m2.z * a.m3.z) + r.m1.z * a.m3.y);
    return r;
}

Mat34 dot3x3(const Mat34& a, const Mat34& b, const Vec3& keepM3) {
    Mat34 r;
    r.m0 = {(a.m0.x * b.m0.x + a.m0.y * b.m1.x) + a.m0.z * b.m2.x,
            (a.m0.y * b.m1.y + a.m0.z * b.m2.y) + a.m0.x * b.m0.y,
            (a.m0.y * b.m1.z + a.m0.z * b.m2.z) + a.m0.x * b.m0.z};
    r.m1 = {(a.m1.x * b.m0.x + a.m1.y * b.m1.x) + a.m1.z * b.m2.x,
            (a.m1.y * b.m1.y + a.m1.z * b.m2.y) + a.m1.x * b.m0.y,
            (a.m1.y * b.m1.z + a.m1.z * b.m2.z) + a.m1.x * b.m0.z};
    r.m2 = {(a.m2.x * b.m0.x + a.m2.z * b.m2.x) + a.m2.y * b.m1.x,
            (a.m2.y * b.m1.y + a.m2.z * b.m2.y) + a.m2.x * b.m0.y,
            (a.m2.y * b.m1.z + a.m2.z * b.m2.z) + a.m2.x * b.m0.z};
    r.m3 = keepM3;
    return r;
}

void dot3x3InPlace(Mat34& a, const Mat34& b) {
    const Vec3 r0{(a.m0.x * b.m0.x + a.m0.y * b.m1.x) + a.m0.z * b.m2.x,
                  (a.m0.y * b.m1.y + a.m0.z * b.m2.y) + a.m0.x * b.m0.y,
                  (a.m0.y * b.m1.z + a.m0.z * b.m2.z) + a.m0.x * b.m0.z};
    const Vec3 r1{(a.m1.y * b.m1.x + a.m1.z * b.m2.x) + a.m1.x * b.m0.x,
                  (a.m1.y * b.m1.y + a.m1.z * b.m2.y) + a.m1.x * b.m0.y,
                  (a.m1.x * b.m0.z + a.m1.y * b.m1.z) + a.m1.z * b.m2.z};
    const Vec3 r2{(a.m2.x * b.m0.x + a.m2.y * b.m1.x) + a.m2.z * b.m2.x,
                  (a.m2.x * b.m0.y + a.m2.y * b.m1.y) + a.m2.z * b.m2.y,
                  (a.m2.y * b.m1.z + a.m2.z * b.m2.z) + a.m2.x * b.m0.z};
    a.m0 = r0;
    a.m1 = r1;
    a.m2 = r2;
}

Mat34 dot3x3Transpose(const Mat34& a, const Mat34& b) {
    Mat34 r;
    r.m0 = {(a.m0.x * b.m0.x + a.m0.y * b.m0.y) + a.m0.z * b.m0.z,
            (a.m0.y * b.m1.y + a.m0.z * b.m1.z) + a.m0.x * b.m1.x,
            (a.m0.y * b.m2.y + a.m0.z * b.m2.z) + a.m0.x * b.m2.x};
    r.m1 = {(a.m1.x * b.m0.x + a.m1.y * b.m0.y) + a.m1.z * b.m0.z,
            (a.m1.y * b.m1.y + a.m1.z * b.m1.z) + a.m1.x * b.m1.x,
            (a.m1.y * b.m2.y + a.m1.z * b.m2.z) + a.m1.x * b.m2.x};
    r.m2 = {(a.m2.x * b.m0.x + a.m2.z * b.m0.z) + a.m2.y * b.m0.y,
            (a.m2.y * b.m1.y + a.m2.z * b.m1.z) + a.m2.x * b.m1.x,
            (a.m2.y * b.m2.y + a.m2.z * b.m2.z) + a.m2.x * b.m2.x};
    r.m3 = {};
    return r;
}

void dot3x3TransposeInPlace(Mat34& a, const Mat34& b) {
    const Vec3 r0{(a.m0.x * b.m0.x + a.m0.y * b.m0.y) + a.m0.z * b.m0.z,
                  (a.m0.x * b.m1.x + a.m0.z * b.m1.z) + a.m0.y * b.m1.y,
                  (a.m0.y * b.m2.y + a.m0.z * b.m2.z) + a.m0.x * b.m2.x};
    const Vec3 r1{(a.m1.y * b.m0.y + a.m1.z * b.m0.z) + a.m1.x * b.m0.x,
                  (a.m1.x * b.m1.x + a.m1.z * b.m1.z) + a.m1.y * b.m1.y,
                  (a.m1.y * b.m2.y + a.m1.z * b.m2.z) + a.m1.x * b.m2.x};
    const Vec3 r2{(a.m2.x * b.m0.x + a.m2.y * b.m0.y) + a.m2.z * b.m0.z,
                  (a.m2.x * b.m1.x + a.m2.z * b.m1.z) + a.m2.y * b.m1.y,
                  (a.m2.y * b.m2.y + a.m2.z * b.m2.z) + a.m2.x * b.m2.x};
    a.m0 = r0;
    a.m1 = r1;
    a.m2 = r2;
}

Mat34 dot(const Mat34& a, const Mat34& b) {
    return Mat34::mul(a, b);
}

Mat34 crossProdMatrix(const Vec3& v) {
    Mat34 r;
    r.m0 = {0.0f, -v.z, v.y};
    r.m1 = {v.z, 0.0f, -v.x};
    r.m2 = {-v.y, v.x, 0.0f};
    r.m3 = {};
    return r;
}

void dot3x3CrossProdMtx(Mat34& m, const Vec3& r) {
    for (Vec3* row : {&m.m0, &m.m1, &m.m2}) {
        const Vec3 a = *row;
        *row = {a.y * r.z - r.y * a.z, r.x * a.z - a.x * r.z, r.y * a.x - a.y * r.x};
    }
}

void dot3x3CrossProdTranspose(Mat34& m, const Vec3& r) {
    for (Vec3* row : {&m.m0, &m.m1, &m.m2}) {
        const Vec3 a = *row;
        *row = {a.z * r.y - r.z * a.y, r.z * a.x - a.z * r.x, r.x * a.y - a.x * r.y};
    }
}

Mat34 add3x3(const Mat34& a, const Mat34& b) {
    Mat34 r;
    r.m0 = {b.m0.x + a.m0.x, b.m0.y + a.m0.y, b.m0.z + a.m0.z};
    r.m1 = {b.m1.x + a.m1.x, b.m1.y + a.m1.y, b.m1.z + a.m1.z};
    r.m2 = {b.m2.x + a.m2.x, b.m2.y + a.m2.y, b.m2.z + a.m2.z};
    r.m3 = a.m3;
    return r;
}

void scale3x3(Mat34& m, float s) {
    for (Vec3* row : {&m.m0, &m.m1, &m.m2})
        *row = {s * row->x, s * row->y, s * row->z};
}

void addScaled3x3(Mat34& m, const Mat34& b, float s) {
    m.m0 = {s * b.m0.x + m.m0.x, s * b.m0.y + m.m0.y, s * b.m0.z + m.m0.z};
    m.m1 = {s * b.m1.x + m.m1.x, s * b.m1.y + m.m1.y, s * b.m1.z + m.m1.z};
    m.m2 = {s * b.m2.x + m.m2.x, s * b.m2.y + m.m2.y, s * b.m2.z + m.m2.z};
}

Vec3 solveSVD(const Mat34& mat, const Vec3& b) {
    // Matrix34::SolveSVD. Elements m[row][column] of the 3x3 part.
    const float m[3][3] = {{mat.m0.x, mat.m0.y, mat.m0.z}, {mat.m1.x, mat.m1.y, mat.m1.z},
                           {mat.m2.x, mat.m2.y, mat.m2.z}};
    // The element of largest magnitude (row-major; a tie keeps the first)
    // and its row and column, 1-based as in the original.
    float largest = 0.0f;
    int row = 1;
    int col = 1;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float v = m[i][j];
            if (largest < v) {
                largest = v;
            } else if (largest < -v) {
                largest = -v;
            } else {
                continue;
            }
            row = i + 1;
            col = j + 1;
        }
    }
    if (largest == 0.0f)
        return {};
    const float tol = largest * 0.0001f;

    // The cofactor matrix, row by row.
    const float cof[3][3] = {
        {m[2][2] * m[1][1] - m[2][1] * m[1][2], -(m[2][2] * m[1][0] - m[2][0] * m[1][2]),
         m[2][1] * m[1][0] - m[2][0] * m[1][1]},
        {-(m[2][2] * m[0][1] - m[0][2] * m[2][1]), m[2][2] * m[0][0] - m[2][0] * m[0][2],
         -(m[2][1] * m[0][0] - m[2][0] * m[0][1])},
        {m[1][2] * m[0][1] - m[1][1] * m[0][2], -(m[1][2] * m[0][0] - m[0][2] * m[1][0]),
         m[1][1] * m[0][0] - m[1][0] * m[0][1]}};
    // The largest magnitude of each cofactor row and where it lies, then the
    // row with the largest of those.
    float rowMax[3];
    int rowArg[3];
    for (int i = 0; i < 3; ++i) {
        float best = 0.0f;
        if (0.0f < cof[i][0])
            best = cof[i][0];
        else if (0.0f < -cof[i][0])
            best = -cof[i][0];
        int arg = 1;
        for (int j = 1; j < 3; ++j) {
            if (best < cof[i][j]) {
                best = cof[i][j];
                arg = j + 1;
            } else if (best < -cof[i][j]) {
                best = -cof[i][j];
                arg = j + 1;
            }
        }
        rowMax[i] = best;
        rowArg[i] = arg;
    }
    int k = 1;
    float maxCof = rowMax[0];
    if (maxCof < rowMax[1]) {
        k = 2;
        maxCof = rowMax[1];
    }
    if (maxCof < rowMax[2]) {
        k = 3;
        maxCof = rowMax[2];
    }

    // Full rank: x = b * M^-1 through the cofactors. The original's rank
    // test checks rows 0 and 2 when the largest element is in row 3.
    const float det = (cof[0][2] * m[0][2] + cof[0][1] * m[0][1]) + cof[0][0] * m[0][0];
    const float absDet = std::fabs(det);
    if (tol * tol < absDet && maxCof * tol < absDet) {
        bool fullRank = false;
        if (row == 1)
            fullRank = tol < rowMax[1] && tol < rowMax[2];
        else if (row == 2 || row == 3)
            fullRank = tol < rowMax[0] && tol < rowMax[2];
        if (fullRank) {
            const float inv = 1.0f / det;
            return {((cof[0][2] * b.z + cof[0][1] * b.y) + cof[0][0] * b.x) * inv,
                    ((cof[1][2] * b.z + cof[1][1] * b.y) + cof[1][0] * b.x) * inv,
                    ((cof[2][2] * b.z + cof[2][1] * b.y) + cof[2][0] * b.x) * inv};
        }
    }

    const auto column = [&](int j) { return Vec3{m[0][j], m[1][j], m[2][j]}; };
    if (tol < maxCof) {
        // Rank 2: cofactor row k is the near-null direction n. b loses its
        // part along n; the two rows of M other than k give a 2x2 system in
        // the two components other than n's largest one, whose solution
        // (with a 0 in component k) loses its part along the cross product
        // of the matching columns.
        const Vec3 n{cof[k - 1][0], cof[k - 1][1], cof[k - 1][2]};
        const int arg = rowArg[k - 1];
        const Vec3 p = k == 1 ? Vec3{m[1][0], m[1][1], m[1][2]} : Vec3{m[0][0], m[0][1], m[0][2]};
        const Vec3 q = k == 3 ? Vec3{m[1][0], m[1][1], m[1][2]} : Vec3{m[2][0], m[2][1], m[2][2]};
        const float d = (n.x * b.x + n.z * b.z) + n.y * b.y;
        const float invN = 1.0f / ((n.x * n.x + n.z * n.z) + n.y * n.y);
        const Vec3 rb{b.x - (n.x * d) * invN, b.y - (n.y * d) * invN, b.z - (n.z * d) * invN};
        float p1, p2, q1, q2, r1, r2;
        Vec3 c1, c2;
        if (arg == 1) {
            p1 = p.y, p2 = p.z, q1 = q.y, q2 = q.z, r1 = rb.y, r2 = rb.z;
            c1 = column(1);
            c2 = column(2);
        } else if (arg == 2) {
            p1 = p.x, p2 = p.z, q1 = q.x, q2 = q.z, r1 = rb.x, r2 = rb.z;
            c1 = column(0);
            c2 = column(2);
        } else {
            p1 = p.x, p2 = p.y, q1 = q.x, q2 = q.y, r1 = rb.x, r2 = rb.y;
            c1 = column(0);
            c2 = column(1);
        }
        const float invDet = 1.0f / (q2 * p1 - q1 * p2);
        const float u1 = (q2 * r1 - q1 * r2) * invDet;
        const float u2 = (p1 * r2 - p2 * r1) * invDet;
        const Vec3 x = k == 1 ? Vec3{0.0f, u1, u2} : (k == 2 ? Vec3{u1, 0.0f, u2} : Vec3{u1, u2, 0.0f});
        const Vec3 c{c1.y * c2.z - c1.z * c2.y, c1.z * c2.x - c2.z * c1.x, c2.y * c1.x - c1.y * c2.x};
        const float xc = (x.z * c.z + x.y * c.y) + x.x * c.x;
        const float invC = 1.0f / ((c.z * c.z + c.y * c.y) + c.x * c.x);
        return {x.x - (c.x * xc) * invC, x.y - (c.y * xc) * invC, x.z - (c.z * xc) * invC};
    }

    // Rank 1: b projected on the row of the largest element, mapped back
    // through its column.
    const Vec3 r = row == 1 ? Vec3{m[0][0], m[0][1], m[0][2]}
                            : (row == 2 ? Vec3{m[1][0], m[1][1], m[1][2]} : Vec3{m[2][0], m[2][1], m[2][2]});
    const float d = (r.z * b.z + r.y * b.y) + r.x * b.x;
    const float invR = 1.0f / ((r.y * r.y + r.z * r.z) + r.x * r.x);
    float s;
    Vec3 c;
    if (col == 1) {
        s = invR * (r.x * d);
        c = column(0);
    } else if (col == 2) {
        s = (r.y * d) * invR;
        c = column(1);
    } else {
        s = (d * r.z) * invR;
        c = column(2);
    }
    const float invCol = 1.0f / ((c.y * c.y + c.z * c.z) + c.x * c.x);
    return {(c.x * s) * invCol, (c.y * s) * invCol, (s * c.z) * invCol};
}

} // namespace mm2::phys::age

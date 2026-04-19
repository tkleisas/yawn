#pragma once

// Small column-major 4×4 matrix + quaternion helpers shared by M3DModel
// (CPU-side hierarchy baking) and M3DRenderer (GPU-side MVP). Header-only
// inline — avoids pulling in a full math library for the half-dozen
// operations we need.
//
// Convention: matrices are column-major (so `m[12..14]` is the
// translation column, matching GLSL's mat4 and glTF's serialised order).

#include <array>
#include <cmath>

namespace yawn {
namespace visual {
namespace m3d {

using Mat4 = std::array<float, 16>;

inline Mat4 identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + rr] * b[c * 4 + k];
            }
            r[c * 4 + rr] = s;
        }
    }
    return r;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 m = identity();
    m[12] = x; m[13] = y; m[14] = z;
    return m;
}

inline Mat4 scale(float sx, float sy, float sz) {
    Mat4 m{};
    m[0] = sx; m[5] = sy; m[10] = sz; m[15] = 1.0f;
    return m;
}

// Quaternion (x, y, z, w) → rotation matrix (column-major).
inline Mat4 quatToMatrix(float qx, float qy, float qz, float qw) {
    const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    const float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    const float wx = qw * qx, wy = qw * qy, wz = qw * qz;
    Mat4 m = identity();
    m[0]  = 1.0f - 2.0f * (yy + zz);
    m[1]  =         2.0f * (xy + wz);
    m[2]  =         2.0f * (xz - wy);
    m[4]  =         2.0f * (xy - wz);
    m[5]  = 1.0f - 2.0f * (xx + zz);
    m[6]  =         2.0f * (yz + wx);
    m[8]  =         2.0f * (xz + wy);
    m[9]  =         2.0f * (yz - wx);
    m[10] = 1.0f - 2.0f * (xx + yy);
    return m;
}

// Euler XYZ in degrees → rotation matrix. Applied as Rz · Ry · Rx so that
// knob-driven rotations feel natural (yaw around Y first, then pitch,
// then roll), matching Blender's default intuition.
inline Mat4 eulerXYZDegrees(float rxDeg, float ryDeg, float rzDeg) {
    const float deg2rad = 3.14159265358979323846f / 180.0f;
    const float rx = rxDeg * deg2rad;
    const float ry = ryDeg * deg2rad;
    const float rz = rzDeg * deg2rad;
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);
    Mat4 X = identity();
    X[5] =  cx; X[6] =  sx; X[9] = -sx; X[10] =  cx;
    Mat4 Y = identity();
    Y[0] =  cy; Y[2] = -sy; Y[8] =  sy; Y[10] =  cy;
    Mat4 Z = identity();
    Z[0] =  cz; Z[1] =  sz; Z[4] = -sz; Z[5] =  cz;
    return multiply(Z, multiply(Y, X));
}

// Right-handed perspective, depth range [-1, 1] (GL default).
inline Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar) {
    Mat4 m{};
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return m;
}

// Right-handed look-at: eye looks toward center, up defines the roll.
inline Mat4 lookAt(float ex, float ey, float ez,
                    float cx, float cy, float cz,
                    float ux, float uy, float uz) {
    // Forward (toward target), normalized — negated to match RH view conv.
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (fl > 1e-6f) { fx /= fl; fy /= fl; fz /= fl; }
    // Right = forward × up, normalized.
    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx*sx + sy*sy + sz*sz);
    if (sl > 1e-6f) { sx /= sl; sy /= sl; sz /= sl; }
    // Adjusted up = right × forward.
    float ux2 = sy * fz - sz * fy;
    float uy2 = sz * fx - sx * fz;
    float uz2 = sx * fy - sy * fx;

    Mat4 m = identity();
    m[0] =  sx;  m[4] =  sy;  m[8]  =  sz;
    m[1] =  ux2; m[5] =  uy2; m[9]  =  uz2;
    m[2] = -fx;  m[6] = -fy;  m[10] = -fz;
    m[12] = -(sx  * ex + sy  * ey + sz  * ez);
    m[13] = -(ux2 * ex + uy2 * ey + uz2 * ez);
    m[14] =  (fx  * ex + fy  * ey + fz  * ez);
    return m;
}

inline void transformPoint(const Mat4& m, float x, float y, float z,
                            float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[4] * y + m[8]  * z + m[12];
    oy = m[1] * x + m[5] * y + m[9]  * z + m[13];
    oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

inline void transformDir(const Mat4& m, float x, float y, float z,
                          float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[4] * y + m[8]  * z;
    oy = m[1] * x + m[5] * y + m[9]  * z;
    oz = m[2] * x + m[6] * y + m[10] * z;
}

} // namespace m3d
} // namespace visual
} // namespace yawn

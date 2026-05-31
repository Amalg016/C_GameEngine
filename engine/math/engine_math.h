#ifndef ENGINE_MATH_H
#define ENGINE_MATH_H

// ---------------------------------------------------------------------------
// Minimal math library — Vec3 + Mat4.
//
// All matrices are stored in COLUMN-MAJOR order to match GLSL conventions:
//   m[col][row]  or equivalently  data[col * 4 + row].
//
// Functions are static inline — small, hot, and used in tight loops.
// ---------------------------------------------------------------------------

#include <math.h>

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static inline Vec3 vec3(float x, float y, float z) {
    return (Vec3){ x, y, z };
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return (Vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

static inline float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 vec3_normalize(Vec3 v) {
    float len = sqrtf(vec3_dot(v, v));
    if (len < 1e-8f) return (Vec3){ 0.0f, 0.0f, 0.0f };
    float inv = 1.0f / len;
    return (Vec3){ v.x * inv, v.y * inv, v.z * inv };
}

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------

typedef struct Vec2 {
    float x, y;
} Vec2;

static inline Vec2 vec2(float x, float y) {
    return (Vec2){ x, y };
}

// ---------------------------------------------------------------------------
// Rect — axis-aligned rectangle
// ---------------------------------------------------------------------------

/// Axis-aligned rectangle (used for UV sub-regions, bounding boxes, etc.).
/// Origin is at the top-left corner; (x, y) is the top-left, (w, h) is size.
typedef struct Rect {
    float x, y;     // top-left origin
    float w, h;     // width, height
} Rect;

static inline Rect rect(float x, float y, float w, float h) {
    return (Rect){ x, y, w, h };
}

// ---------------------------------------------------------------------------
// Mat4 — column-major 4×4 matrix
// ---------------------------------------------------------------------------

typedef struct Mat4 {
    float m[4][4];   // m[column][row]
} Mat4;

/// Returns the identity matrix.
static inline Mat4 mat4_identity(void) {
    return (Mat4){{
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 },
    }};
}

/// Multiply two 4×4 matrices: result = A * B.
static inline Mat4 mat4_multiply(const Mat4 *a, const Mat4 *b) {
    Mat4 result = {{{ 0 }}};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result.m[col][row] =
                a->m[0][row] * b->m[col][0] +
                a->m[1][row] * b->m[col][1] +
                a->m[2][row] * b->m[col][2] +
                a->m[3][row] * b->m[col][3];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Projection matrices
// ---------------------------------------------------------------------------

/// Orthographic projection.
///
/// Maps the axis-aligned box [left..right] × [bottom..top] × [near..far]
/// into Vulkan clip space (x,y ∈ [-1,1], z ∈ [0,1]).
static inline Mat4 mat4_ortho(float left, float right,
                               float bottom, float top,
                               float near_plane, float far_plane) {
    float rl = right - left;
    float tb = top   - bottom;
    float fn = far_plane - near_plane;

    Mat4 m = {{{ 0 }}};
    m.m[0][0] =  2.0f / rl;
    m.m[1][1] =  2.0f / tb;
    m.m[2][2] = -1.0f / fn;                 // Vulkan depth [0, 1]
    m.m[3][0] = -(right + left)   / rl;
    m.m[3][1] = -(top   + bottom) / tb;
    m.m[3][2] = -near_plane       / fn;
    m.m[3][3] =  1.0f;
    return m;
}

/// Perspective projection (Vulkan clip space, depth [0, 1]).
///
/// `fov_y`  — vertical field of view in radians.
/// `aspect` — width / height.
static inline Mat4 mat4_perspective(float fov_y, float aspect,
                                     float near_plane, float far_plane) {
    float tan_half = tanf(fov_y * 0.5f);
    float fn       = far_plane - near_plane;

    Mat4 m = {{{ 0 }}};
    m.m[0][0] =  1.0f / (aspect * tan_half);
    m.m[1][1] =  1.0f / tan_half;
    m.m[2][2] = -far_plane / fn;             // Vulkan depth [0, 1]
    m.m[2][3] = -1.0f;                       // perspective divide
    m.m[3][2] = -(far_plane * near_plane) / fn;
    return m;
}

// ---------------------------------------------------------------------------
// View matrix
// ---------------------------------------------------------------------------

/// Build a right-handed look-at view matrix.
///
/// `eye`    — camera position in world space.
/// `center` — point the camera looks at.
/// `up`     — world up direction (usually {0,1,0}).
static inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(center, eye));   // forward
    Vec3 s = vec3_normalize(vec3_cross(f, up));       // right
    Vec3 u = vec3_cross(s, f);                        // recomputed up

    Mat4 m = mat4_identity();
    m.m[0][0] =  s.x;
    m.m[1][0] =  s.y;
    m.m[2][0] =  s.z;
    m.m[0][1] =  u.x;
    m.m[1][1] =  u.y;
    m.m[2][1] =  u.z;
    m.m[0][2] = -f.x;
    m.m[1][2] = -f.y;
    m.m[2][2] = -f.z;
    m.m[3][0] = -vec3_dot(s, eye);
    m.m[3][1] = -vec3_dot(u, eye);
    m.m[3][2] =  vec3_dot(f, eye);
    return m;
}

#endif // ENGINE_MATH_H

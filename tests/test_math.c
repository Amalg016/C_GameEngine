#include "test_framework.h"
#include "engine/math/engine_math.h"

static void test_vec3_basics(void) {
    Vec3 a = vec3(1.0f, 2.0f, 3.0f);
    Vec3 b = vec3(4.0f, 5.0f, 6.0f);

    ASSERT_FLOAT_EQ(a.x, 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(a.y, 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(a.z, 3.0f, 1e-6f);

    Vec3 sub = vec3_sub(b, a);
    ASSERT_FLOAT_EQ(sub.x, 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(sub.y, 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(sub.z, 3.0f, 1e-6f);

    float dot = vec3_dot(a, b);
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    ASSERT_FLOAT_EQ(dot, 32.0f, 1e-6f);
}

static void test_vec3_cross(void) {
    Vec3 i = vec3(1.0f, 0.0f, 0.0f);
    Vec3 j = vec3(0.0f, 1.0f, 0.0f);
    Vec3 k = vec3_cross(i, j);

    ASSERT_FLOAT_EQ(k.x, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(k.y, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(k.z, 1.0f, 1e-6f);

    Vec3 k_reverse = vec3_cross(j, i);
    ASSERT_FLOAT_EQ(k_reverse.x, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(k_reverse.y, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(k_reverse.z, -1.0f, 1e-6f);
}

static void test_vec3_normalize(void) {
    Vec3 v = vec3(3.0f, 0.0f, 4.0f);
    Vec3 norm = vec3_normalize(v);

    ASSERT_FLOAT_EQ(norm.x, 0.6f, 1e-6f);
    ASSERT_FLOAT_EQ(norm.y, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(norm.z, 0.8f, 1e-6f);

    // Test zero vector boundary
    Vec3 zero = vec3(0.0f, 0.0f, 0.0f);
    Vec3 norm_zero = vec3_normalize(zero);
    ASSERT_FLOAT_EQ(norm_zero.x, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(norm_zero.y, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(norm_zero.z, 0.0f, 1e-6f);
}

static void test_vec2_and_rect(void) {
    Vec2 v = vec2(1.5f, -2.5f);
    ASSERT_FLOAT_EQ(v.x, 1.5f, 1e-6f);
    ASSERT_FLOAT_EQ(v.y, -2.5f, 1e-6f);

    Rect r = rect(10.0f, 20.0f, 100.0f, 200.0f);
    ASSERT_FLOAT_EQ(r.x, 10.0f, 1e-6f);
    ASSERT_FLOAT_EQ(r.y, 20.0f, 1e-6f);
    ASSERT_FLOAT_EQ(r.w, 100.0f, 1e-6f);
    ASSERT_FLOAT_EQ(r.h, 200.0f, 1e-6f);
}

static void test_mat4_identity(void) {
    Mat4 identity = mat4_identity();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            ASSERT_FLOAT_EQ(identity.m[col][row], expected, 1e-6f);
        }
    }
}

static void test_mat4_multiply(void) {
    Mat4 a = mat4_identity();
    Mat4 b = mat4_identity();

    // identity * identity = identity
    Mat4 mult = mat4_multiply(&a, &b);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            ASSERT_FLOAT_EQ(mult.m[col][row], expected, 1e-6f);
        }
    }

    // Set custom values
    a.m[0][0] = 2.0f; a.m[1][0] = 3.0f; a.m[2][0] = 0.0f; a.m[3][0] = 1.0f;
    a.m[0][1] = 1.0f; a.m[1][1] = 0.0f; a.m[2][1] = 2.0f; a.m[3][1] = -1.0f;
    a.m[0][2] = 0.0f; a.m[1][2] = 1.0f; a.m[2][2] = 4.0f; a.m[3][2] = 0.0f;
    a.m[0][3] = -2.0f;a.m[1][3] = 2.0f; a.m[2][3] = 1.0f; a.m[3][3] = 3.0f;

    b.m[0][0] = 1.0f; b.m[1][0] = 2.0f; b.m[2][0] = 3.0f; b.m[3][0] = 0.0f;
    b.m[0][1] = 0.0f; b.m[1][1] = -1.0f;b.m[2][1] = 1.0f; b.m[3][1] = 2.0f;
    b.m[0][2] = 2.0f; b.m[1][2] = 0.0f; b.m[2][2] = 1.0f; b.m[3][2] = -1.0f;
    b.m[0][3] = 1.0f; b.m[1][3] = 1.0f; b.m[2][3] = 0.0f; b.m[3][3] = 4.0f;

    // Col 0:
    // row 0: a[0][0]*b[0][0] + a[1][0]*b[0][1] + a[2][0]*b[0][2] + a[3][0]*b[0][3] = 2*1 + 3*0 + 0*2 + 1*1 = 3
    // row 1: a[0][1]*b[0][0] + a[1][1]*b[0][1] + a[2][1]*b[0][2] + a[3][1]*b[0][3] = 1*1 + 0*0 + 2*2 + (-1)*1 = 4
    // row 2: a[0][2]*b[0][0] + a[1][2]*b[0][1] + a[2][2]*b[0][2] + a[3][2]*b[0][3] = 0*1 + 1*0 + 4*2 + 0*1 = 8
    // row 3: a[0][3]*b[0][0] + a[1][3]*b[0][1] + a[2][3]*b[0][2] + a[3][3]*b[0][3] = -2*1 + 2*0 + 1*2 + 3*1 = 3
    Mat4 res = mat4_multiply(&a, &b);
    ASSERT_FLOAT_EQ(res.m[0][0], 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(res.m[0][1], 4.0f, 1e-6f);
    ASSERT_FLOAT_EQ(res.m[0][2], 8.0f, 1e-6f);
    ASSERT_FLOAT_EQ(res.m[0][3], 3.0f, 1e-6f);
}

static void test_mat4_projections(void) {
    // Ortho matrix
    Mat4 ortho = mat4_ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
    
    // Width = 800, Left = 0 -> m[0][0] = 2.0f / (800 - 0) = 1.0f / 400.0f = 0.0025f
    ASSERT_FLOAT_EQ(ortho.m[0][0], 0.0025f, 1e-6f);
    // Height = -600 (top=0, bottom=600) -> m[1][1] = 2.0f / (0 - 600) = -1.0f / 300.0f = -0.003333f
    ASSERT_FLOAT_EQ(ortho.m[1][1], -1.0f / 300.0f, 1e-5f);
    // Depth fn = 1.0f - (-1.0f) = 2.0f. m[2][2] = -1.0f / 2.0f = -0.5f
    ASSERT_FLOAT_EQ(ortho.m[2][2], -0.5f, 1e-6f);
    
    // Perspective matrix
    float fov_y = 1.0f; // in radians
    float aspect = 16.0f / 9.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    Mat4 proj = mat4_perspective(fov_y, aspect, near_plane, far_plane);

    // Perspective checks:
    // m[1][1] = 1.0 / tan(0.5)
    float expected_m11 = 1.0f / tanf(0.5f);
    ASSERT_FLOAT_EQ(proj.m[1][1], expected_m11, 1e-6f);
    // m[0][0] = 1.0 / (aspect * tan(0.5))
    ASSERT_FLOAT_EQ(proj.m[0][0], expected_m11 / aspect, 1e-6f);
    // perspective divide row m[2][3] should be -1.0f
    ASSERT_FLOAT_EQ(proj.m[2][3], -1.0f, 1e-6f);
}

static void test_mat4_look_at(void) {
    Vec3 eye = vec3(0.0f, 0.0f, 5.0f);
    Vec3 center = vec3(0.0f, 0.0f, 0.0f);
    Vec3 up = vec3(0.0f, 1.0f, 0.0f);

    Mat4 view = mat4_look_at(eye, center, up);

    // Look at from (0,0,5) to origin with (0,1,0) up:
    // Forward direction f = norm(0 - eye) = (0, 0, -1).
    // Right s = norm(f x up) = (-1, 0, 0) x (0, 1, 0)?
    // Wait, let's check cross: f x up = (0.0f, 0.0f, -1.0f) x (0.0f, 1.0f, 0.0f)
    // f.y * up.z - f.z * up.y = 0*0 - (-1)*1 = 1
    // f.z * up.x - f.x * up.z = (-1)*0 - 0*0 = 0
    // f.x * up.y - f.y * up.x = 0*1 - 0*0 = 0
    // So right s = (1, 0, 0).
    // Recomputed up u = s x f = (1, 0, 0) x (0, 0, -1) = (0, 1, 0).
    // Translation terms in column 3:
    // m[3][0] = -s . eye = -(0) = 0
    // m[3][1] = -u . eye = -(0) = 0
    // m[3][2] = f . eye = (0, 0, -1) . (0, 0, 5) = -5
    ASSERT_FLOAT_EQ(view.m[3][0], 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(view.m[3][1], 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(view.m[3][2], -5.0f, 1e-6f);
    ASSERT_FLOAT_EQ(view.m[3][3], 1.0f, 1e-6f);
}

void test_math_run(void) {
    printf(COLOR_BLUE COLOR_BOLD "\n--- Running Math Tests ---" COLOR_RESET "\n");
    RUN_TEST(test_vec3_basics);
    RUN_TEST(test_vec3_cross);
    RUN_TEST(test_vec3_normalize);
    RUN_TEST(test_vec2_and_rect);
    RUN_TEST(test_mat4_identity);
    RUN_TEST(test_mat4_multiply);
    RUN_TEST(test_mat4_projections);
    RUN_TEST(test_mat4_look_at);
}

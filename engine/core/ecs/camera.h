#ifndef ENGINE_CORE_ECS_CAMERA_H
#define ENGINE_CORE_ECS_CAMERA_H

#include "ecs_types.h"
#include "world.h"
#include "hierarchy.h"
#include "../../math/engine_math.h"

// ---------------------------------------------------------------------------
// Camera Component
//
// Attach a Camera to any entity to make it a viewpoint.  The camera reads
// the entity's WorldTransform (from the hierarchy system) to determine its
// position in world space, then computes view and projection matrices.
//
// Only ONE camera should be marked `is_active = true` at a time.  The
// camera system processes only the active camera each frame.
//
// Usage:
//   CameraContext cam_ctx = camera_init(world);
//
//   Entity cam = world_entity_create(world);
//   // ... add LocalTransform + WorldTransform ...
//   Camera cam_data = {
//       .projection = CAMERA_ORTHOGRAPHIC,
//       .ortho_size = 5.0f,   // half-height in world units
//       .near_plane = -1.0f,
//       .far_plane  =  1.0f,
//       .is_active  = true,
//   };
//   world_add_component(world, cam, cam_ctx.c_camera, &cam_data);
//
//   // Each frame, after hierarchy_update_transforms:
//   camera_update(world, &cam_ctx, &hctx, aspect_ratio);
//   const Mat4 *vp = camera_get_view_proj(world, &cam_ctx);
//   renderer_set_view_projection(renderer, (const float *)vp);
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/// Projection mode selector.
typedef enum CameraProjection {
    CAMERA_ORTHOGRAPHIC,
    CAMERA_PERSPECTIVE,
} CameraProjection;

/// Camera component.
typedef struct Camera {
    CameraProjection projection;

    // --- Perspective parameters ---
    float fov;             // vertical field-of-view in radians
    float near_plane;      // near clipping plane
    float far_plane;       // far clipping plane

    // --- Orthographic parameters ---
    float ortho_size;      // half-height of the view volume (world units)

    // --- Computed by camera_update() — treat as read-only ---
    Mat4 view;
    Mat4 proj;
    Mat4 view_proj;

    // --- State ---
    bool is_active;        // only the active camera is used for rendering
} Camera;

// ---------------------------------------------------------------------------
// Context — holds the ComponentId registered by camera_init().
// ---------------------------------------------------------------------------

typedef struct CameraContext {
    ComponentId c_camera;
} CameraContext;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Register the Camera component with the world.
/// Call once at startup, after hierarchy_init().
CameraContext camera_init(World *world);

/// Recompute view + projection matrices for the active camera.
///
/// Must be called AFTER hierarchy_update_transforms() so that the camera
/// entity's WorldTransform is up-to-date.
///
/// `aspect_ratio` = viewport width / viewport height.
void camera_update(World *world, const CameraContext *cam_ctx,
                   const HierarchyContext *hctx, float aspect_ratio);

/// Return a pointer to the active camera's computed view-projection matrix.
/// Returns nullptr if no active camera exists.
const Mat4 *camera_get_view_proj(const World *world,
                                  const CameraContext *cam_ctx);

/// Convenience: build default orthographic camera parameters.
static inline Camera camera_default_ortho(float ortho_size) {
    return (Camera){
        .projection = CAMERA_ORTHOGRAPHIC,
        .fov        = 0.0f,
        .near_plane = -1.0f,
        .far_plane  =  1.0f,
        .ortho_size = ortho_size,
        .view       = mat4_identity(),
        .proj       = mat4_identity(),
        .view_proj  = mat4_identity(),
        .is_active  = true,
    };
}

/// Convenience: build default perspective camera parameters.
static inline Camera camera_default_perspective(float fov_radians) {
    return (Camera){
        .projection = CAMERA_PERSPECTIVE,
        .fov        = fov_radians,
        .near_plane = 0.1f,
        .far_plane  = 100.0f,
        .ortho_size = 0.0f,
        .view       = mat4_identity(),
        .proj       = mat4_identity(),
        .view_proj  = mat4_identity(),
        .is_active  = true,
    };
}

#endif // ENGINE_CORE_ECS_CAMERA_H

#include "camera.h"
#include "component_pool.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// camera_init — register the Camera component.
// ---------------------------------------------------------------------------

CameraContext camera_init(World *world) {
    CameraContext ctx = {
        .c_camera = world_register_component(world, sizeof(Camera)),
    };

    printf("[camera] initialised (camera=%u)\n", ctx.c_camera);
    return ctx;
}

// ---------------------------------------------------------------------------
// camera_update — compute VP for the active camera.
// ---------------------------------------------------------------------------

void camera_update(World *world, const CameraContext *cam_ctx,
                   const HierarchyContext *hctx, float aspect_ratio) {
    if (world == nullptr || cam_ctx == nullptr || hctx == nullptr) return;

    ComponentPool *cam_pool = world_get_pool(world, cam_ctx->c_camera);
    if (cam_pool == nullptr || cam_pool->count == 0) return;

    for (uint32_t i = 0; i < cam_pool->count; ++i) {
        Camera *cam = (Camera *)component_pool_get_dense(cam_pool, i);
        if (!cam->is_active) continue;

        uint32_t ent_idx = component_pool_get_entity(cam_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        // Read the camera entity's world-space position.
        WorldTransform *wt = (WorldTransform *)world_get_component(
            world, ent, hctx->c_world_transform);

        float cam_x = 0.0f;
        float cam_y = 0.0f;
        if (wt != nullptr) {
            cam_x = wt->x;
            cam_y = wt->y;
        }

        // -- View matrix: camera at (cam_x, cam_y, 1) looking at (cam_x, cam_y, 0) --
        cam->view = mat4_look_at(
            vec3(cam_x, cam_y, 1.0f),    // eye
            vec3(cam_x, cam_y, 0.0f),    // center
            vec3(0.0f,  1.0f, 0.0f)      // up
        );

        // -- Projection matrix --
        switch (cam->projection) {
            case CAMERA_ORTHOGRAPHIC: {
                float half_h = cam->ortho_size;
                float half_w = half_h * aspect_ratio;
                cam->proj = mat4_ortho(
                    -half_w, half_w,
                    -half_h, half_h,
                    cam->near_plane, cam->far_plane
                );
                break;
            }
            case CAMERA_PERSPECTIVE: {
                cam->proj = mat4_perspective(
                    cam->fov, aspect_ratio,
                    cam->near_plane, cam->far_plane
                );
                break;
            }
        }

        // -- View-Projection = Projection * View --
        cam->view_proj = mat4_multiply(&cam->proj, &cam->view);

        // Only process the first active camera.
        return;
    }
}

// ---------------------------------------------------------------------------
// camera_get_view_proj — return the active camera's VP matrix.
// ---------------------------------------------------------------------------

const Mat4 *camera_get_view_proj(const World *world,
                                  const CameraContext *cam_ctx) {
    if (world == nullptr || cam_ctx == nullptr) return nullptr;

    ComponentPool *cam_pool = world_get_pool(world, cam_ctx->c_camera);
    if (cam_pool == nullptr || cam_pool->count == 0) return nullptr;

    for (uint32_t i = 0; i < cam_pool->count; ++i) {
        Camera *cam = (Camera *)component_pool_get_dense(cam_pool, i);
        if (cam->is_active) {
            return &cam->view_proj;
        }
    }

    return nullptr;
}

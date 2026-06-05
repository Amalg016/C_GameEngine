#include "scene.h"
#include "engine.h"
#include "ecs/ecs.h"
#include "asset_manager.h"
#include "sprite.h"
#include "animation.h"
#include "anim_controller.h"
#include "anim_cache.h"
#include "platformer_controller.h"
#include "../renderer/renderer.h"

// Lua host internal accessors — needed for Sprite/Velocity ComponentIds.
#include "scripting/lua_host.h"
extern bool        lua_host_sprite_registered(LuaHost *host);
extern ComponentId lua_host_get_sprite_id(LuaHost *host);
extern void        lua_host_set_sprite_id(LuaHost *host, ComponentId id);
extern bool        lua_host_velocity_registered(LuaHost *host);
extern ComponentId lua_host_get_velocity_id(LuaHost *host);
extern void        lua_host_set_velocity_id(LuaHost *host, ComponentId id);
extern bool        lua_host_animator_registered(LuaHost *host);
extern ComponentId lua_host_get_animator_id(LuaHost *host);
extern void        lua_host_set_animator_id(LuaHost *host, ComponentId id);

#include "../../third_party/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Component structs — must mirror the layouts in lua_bindings.c / main.c.
// ---------------------------------------------------------------------------

typedef struct SceneSprite {
    Sprite sprite;
} SceneSprite;

typedef struct SceneVelocity {
    float dx, dy;
} SceneVelocity;

typedef struct SceneAnimator {
    Animator animator;
} SceneAnimator;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Read the entire contents of a file into a malloc'd string.
/// Caller assumes ownership and must free() the returned buffer. Returns nullptr on failure.
[[nodiscard]] static char *read_file_to_string(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (f == nullptr) {
        fprintf(stderr, "[scene] cannot open file: %s\n", filepath);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return nullptr;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (buf == nullptr) {
        fclose(f);
        return nullptr;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/// Ensure the Sprite component type is registered (lazy, like lua_bindings.c).
static ComponentId ensure_sprite_component(World *world, LuaHost *lua) {
    if (lua != nullptr && lua_host_sprite_registered(lua)) {
        return lua_host_get_sprite_id(lua);
    }
    ComponentId id = world_register_component(world, sizeof(SceneSprite));
    if (lua != nullptr) {
        lua_host_set_sprite_id(lua, id);
    }
    return id;
}

/// Ensure the Velocity component type is registered.
static ComponentId ensure_velocity_component(World *world, LuaHost *lua) {
    if (lua != nullptr && lua_host_velocity_registered(lua)) {
        return lua_host_get_velocity_id(lua);
    }
    ComponentId id = world_register_component(world, sizeof(SceneVelocity));
    if (lua != nullptr) {
        lua_host_set_velocity_id(lua, id);
    }
    return id;
}

/// Ensure the Animator component type is registered.
static ComponentId ensure_animator_component(World *world, LuaHost *lua) {
    if (lua != nullptr && lua_host_animator_registered(lua)) {
        return lua_host_get_animator_id(lua);
    }
    ComponentId id = world_register_component(world, sizeof(SceneAnimator));
    if (lua != nullptr) {
        lua_host_set_animator_id(lua, id);
    }
    return id;
}

// ---------------------------------------------------------------------------
// Temporary ID map — maps JSON entity IDs to live Entity handles.
//
// We use a simple flat array indexed by JSON ID.  This requires knowing the
// maximum JSON ID in advance (we scan once to find it).
// ---------------------------------------------------------------------------

typedef struct IdMap {
    Entity   *entries;     // entries[json_id] = live Entity
    uint32_t  capacity;    // allocated size
} IdMap;

static IdMap idmap_create(uint32_t max_id) {
    uint32_t cap = max_id + 1;
    Entity *entries = calloc(cap, sizeof(Entity));
    if (entries == nullptr) return (IdMap){ nullptr, 0 };
    // Initialise all to ENTITY_INVALID.
    for (uint32_t i = 0; i < cap; ++i) {
        entries[i] = ENTITY_INVALID;
    }
    return (IdMap){ entries, cap };
}

static void idmap_destroy(IdMap *map) {
    free(map->entries);
    map->entries  = nullptr;
    map->capacity = 0;
}

static void idmap_set(IdMap *map, uint32_t json_id, Entity live) {
    if (json_id < map->capacity) {
        map->entries[json_id] = live;
    }
}

static Entity idmap_get(const IdMap *map, uint32_t json_id) {
    if (json_id < map->capacity) {
        return map->entries[json_id];
    }
    return ENTITY_INVALID;
}

// ---------------------------------------------------------------------------
// Temporary texture path → AssetHandle map for deserialization.
// Simple linear-scan table (scenes rarely have > 100 textures).
// ---------------------------------------------------------------------------

#define MAX_SCENE_TEXTURES 256
#define SCENE_PATH_MAX     256

typedef struct TexEntry {
    char        path[SCENE_PATH_MAX];
    AssetHandle handle;
} TexEntry;

typedef struct TexMap {
    TexEntry entries[MAX_SCENE_TEXTURES];
    uint32_t count;
} TexMap;

static AssetHandle texmap_lookup(const TexMap *map, const char *path) {
    for (uint32_t i = 0; i < map->count; ++i) {
        if (strcmp(map->entries[i].path, path) == 0) {
            return map->entries[i].handle;
        }
    }
    return ASSET_HANDLE_INVALID;
}

static void texmap_insert(TexMap *map, const char *path, AssetHandle handle) {
    if (map->count >= MAX_SCENE_TEXTURES) return;
    strncpy(map->entries[map->count].path, path, SCENE_PATH_MAX - 1);
    map->entries[map->count].path[SCENE_PATH_MAX - 1] = '\0';
    map->entries[map->count].handle = handle;
    map->count++;
}

static void release_manifest_textures(Engine *engine, const char *filepath) {
    if (filepath == nullptr) return;

    AssetManager *am = engine_get_asset_manager(engine);
    if (am == nullptr) return;

    char *json_str = read_file_to_string(filepath);
    if (json_str == nullptr) return;

    cJSON *root = cJSON_Parse(json_str);
    if (root == nullptr) {
        free(json_str);
        return;
    }

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    if (assets != nullptr) {
        cJSON *textures = cJSON_GetObjectItemCaseSensitive(assets, "textures");
        if (cJSON_IsArray(textures)) {
            cJSON *tex_path = nullptr;
            cJSON_ArrayForEach(tex_path, textures) {
                if (!cJSON_IsString(tex_path)) continue;
                const char *path = tex_path->valuestring;
                AssetHandle h = asset_manager_get_handle(am, path);
                if (h != ASSET_HANDLE_INVALID) {
                    asset_manager_release(am, h);
                }
            }
        }
    }

    cJSON_Delete(root);
    free(json_str);
}

// ===========================================================================
// scene_unload
// ===========================================================================

void scene_unload(Engine *engine) {
    if (engine == nullptr) return;

    World        *world = engine_get_world(engine);
    AssetManager *am    = engine_get_asset_manager(engine);
    LuaHost      *lua   = engine_get_lua_host(engine);

    if (world == nullptr) return;

    // Release all asset references held by Sprite components so the
    // AssetManager can free GPU resources (ref-count → 0 = freed).
    if (lua != nullptr && lua_host_sprite_registered(lua)) {
        ComponentId c_spr = lua_host_get_sprite_id(lua);
        ComponentPool *spr_pool = world_get_pool(world, c_spr);
        if (spr_pool != nullptr && spr_pool->count > 0) {
            for (uint32_t i = 0; i < spr_pool->count; ++i) {
                SceneSprite *spr = (SceneSprite *)component_pool_get_dense(
                    spr_pool, i);
                if (spr->sprite.texture != ASSET_HANDLE_INVALID) {
                    asset_manager_release(am, spr->sprite.texture);
                }
            }
            printf("[scene] released %u sprite asset reference(s)\n",
                   spr_pool->count);
        }
    }

    // Release preloaded manifest textures.
    const char *current_scene = engine_get_current_scene(engine);
    if (current_scene != nullptr) {
        release_manifest_textures(engine, current_scene);
    }

    // Release all Lua script instance references (calls on_destroy on each).
    if (lua != nullptr) {
        lua_host_scripts_clear(lua);
    }

    // Clear all entities and component data.  Pool registrations are
    // preserved so the same ComponentIds remain valid.
    world_clear(world);
    printf("[scene] unloaded\n");
}

// ===========================================================================
// scene_switch
// ===========================================================================

bool scene_switch(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;

    printf("[scene] switching to: %s\n", filepath);
    scene_unload(engine);
    return scene_load(engine, filepath);
}

// ===========================================================================
// scene_load
// ===========================================================================

bool scene_load(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;

    printf("[scene] loading: %s\n", filepath);

    // --- Grab engine subsystems -------------------------------------------
    World            *world  = engine_get_world(engine);
    AssetManager     *am     = engine_get_asset_manager(engine);
    HierarchyContext *hctx   = engine_get_hctx(engine);
    CameraContext    *cam_ctx = engine_get_cam_ctx(engine);
    LuaHost          *lua    = engine_get_lua_host(engine);  // may be nullptr

    if (world == nullptr || am == nullptr || hctx == nullptr || cam_ctx == nullptr) {
        fprintf(stderr, "[scene] engine subsystems not ready\n");
        return false;
    }

    // --- Read & parse JSON ------------------------------------------------
    char *json_str = read_file_to_string(filepath);
    if (json_str == nullptr) return false;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root == nullptr) {
        const char *err = cJSON_GetErrorPtr();
        fprintf(stderr, "[scene] JSON parse error near: %.40s\n",
                err ? err : "(unknown)");
        return false;
    }

    // --- 1. Asset pre-loading ---------------------------------------------
    TexMap texmap = {};

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    if (assets != nullptr) {
        cJSON *textures = cJSON_GetObjectItemCaseSensitive(assets, "textures");
        if (cJSON_IsArray(textures)) {
            cJSON *tex_path = nullptr;
            cJSON_ArrayForEach(tex_path, textures) {
                if (!cJSON_IsString(tex_path)) continue;
                const char *path = tex_path->valuestring;

                AssetHandle h = asset_manager_load_texture(am, path);
                if (h == ASSET_HANDLE_INVALID) {
                    fprintf(stderr, "[scene] warning: failed to load texture '%s'\n", path);
                    continue;
                }
                texmap_insert(&texmap, path, h);
                printf("[scene] pre-loaded texture: %s → handle %u\n", path, h);
            }
        }
    }

    // --- 3. Determine max JSON entity ID for the ID map -------------------
    cJSON *entities = cJSON_GetObjectItemCaseSensitive(root, "entities");
    if (!cJSON_IsArray(entities)) {
        fprintf(stderr, "[scene] missing or invalid 'entities' array\n");
        cJSON_Delete(root);
        return false;
    }

    uint32_t max_json_id = 0;
    {
        cJSON *ent_json = nullptr;
        cJSON_ArrayForEach(ent_json, entities) {
            cJSON *id_item = cJSON_GetObjectItemCaseSensitive(ent_json, "id");
            if (cJSON_IsNumber(id_item)) {
                uint32_t jid = (uint32_t)id_item->valuedouble;
                if (jid > max_json_id) max_json_id = jid;
            }
        }
    }

    IdMap idmap = idmap_create(max_json_id);
    if (idmap.entries == nullptr && max_json_id > 0) {
        fprintf(stderr, "[scene] failed to allocate ID map\n");
        cJSON_Delete(root);
        return false;
    }

    // --- 4. Entity instantiation (first pass) -----------------------------
    //
    // We also record which JSON entities have a "parent" field for the
    // second pass.
    //

    // Track deferred parent assignments.
    typedef struct { uint32_t child_json_id; uint32_t parent_json_id; } ParentLink;
    uint32_t entity_count_est = (uint32_t)cJSON_GetArraySize(entities);
    ParentLink *parent_links = calloc(entity_count_est, sizeof(ParentLink));
    uint32_t parent_link_count = 0;

    // Sprite / Velocity component IDs — lazily ensured once during the load.
    bool     need_sprite   = false;
    bool     need_velocity = false;
    bool     need_animator = false;
    ComponentId c_sprite   = UINT8_MAX;
    ComponentId c_velocity = UINT8_MAX;
    ComponentId c_animator = UINT8_MAX;

    // Pre-scan: do we need Sprite, Velocity, or Animator components?
    {
        cJSON *ent_json = nullptr;
        cJSON_ArrayForEach(ent_json, entities) {
            cJSON *comps = cJSON_GetObjectItemCaseSensitive(ent_json, "components");
            if (comps == nullptr) continue;
            if (cJSON_GetObjectItemCaseSensitive(comps, "sprite"))   need_sprite   = true;
            if (cJSON_GetObjectItemCaseSensitive(comps, "velocity")) need_velocity = true;
            if (cJSON_GetObjectItemCaseSensitive(comps, "animator")) need_animator = true;
        }
    }

    if (need_sprite)   c_sprite   = ensure_sprite_component(world, lua);
    if (need_velocity) c_velocity = ensure_velocity_component(world, lua);
    if (need_animator) c_animator = ensure_animator_component(world, lua);

    // Main entity loop.
    {
        cJSON *ent_json = nullptr;
        cJSON_ArrayForEach(ent_json, entities) {
            cJSON *id_item = cJSON_GetObjectItemCaseSensitive(ent_json, "id");
            if (!cJSON_IsNumber(id_item)) {
                fprintf(stderr, "[scene] entity missing 'id' — skipping\n");
                continue;
            }
            uint32_t json_id = (uint32_t)id_item->valuedouble;

            // Create live entity.
            Entity live = world_entity_create(world);
            if (live == ENTITY_INVALID) {
                fprintf(stderr, "[scene] failed to create entity for json_id=%u\n", json_id);
                continue;
            }
            idmap_set(&idmap, json_id, live);

            // Record parent link if present (resolved in second pass).
            cJSON *parent_item = cJSON_GetObjectItemCaseSensitive(ent_json, "parent");
            if (cJSON_IsNumber(parent_item)) {
                if (parent_link_count < entity_count_est) {
                    parent_links[parent_link_count++] = (ParentLink){
                        .child_json_id  = json_id,
                        .parent_json_id = (uint32_t)parent_item->valuedouble,
                    };
                }
            }

            // Parse components.
            cJSON *comps = cJSON_GetObjectItemCaseSensitive(ent_json, "components");
            if (comps == nullptr) continue;

            // -- local_transform -------------------------------------------------
            cJSON *lt_json = cJSON_GetObjectItemCaseSensitive(comps, "local_transform");
            if (lt_json != nullptr) {
                LocalTransform lt = {
                    .x  = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(lt_json, "x")),
                    .y  = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(lt_json, "y")),
                    .sx = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(lt_json, "sx")),
                    .sy = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(lt_json, "sy")),
                };
                world_add_component(world, live, hctx->c_local_transform, &lt);

                // Also add WorldTransform + PreviousPosition (as set_transform does).
                WorldTransform wt = { .x = lt.x, .y = lt.y, .sx = lt.sx, .sy = lt.sy };
                world_add_component(world, live, hctx->c_world_transform, &wt);

                PreviousPosition pp = { .x = lt.x, .y = lt.y };
                world_add_component(world, live, hctx->c_prev_position, &pp);
            }

            // -- camera ----------------------------------------------------------
            cJSON *cam_json = cJSON_GetObjectItemCaseSensitive(comps, "camera");
            if (cam_json != nullptr) {
                cJSON *proj_item = cJSON_GetObjectItemCaseSensitive(cam_json, "projection");
                const char *proj_str = cJSON_IsString(proj_item) ? proj_item->valuestring : "orthographic";

                Camera cam;
                if (strcmp(proj_str, "perspective") == 0) {
                    float fov = (float)cJSON_GetNumberValue(
                                    cJSON_GetObjectItemCaseSensitive(cam_json, "fov"));
                    cam = camera_default_perspective(fov);
                } else {
                    float size = (float)cJSON_GetNumberValue(
                                     cJSON_GetObjectItemCaseSensitive(cam_json, "ortho_size"));
                    cam = camera_default_ortho(size);
                }

                // Override near/far if specified.
                cJSON *near_item = cJSON_GetObjectItemCaseSensitive(cam_json, "near_plane");
                cJSON *far_item  = cJSON_GetObjectItemCaseSensitive(cam_json, "far_plane");
                if (cJSON_IsNumber(near_item)) cam.near_plane = (float)near_item->valuedouble;
                if (cJSON_IsNumber(far_item))  cam.far_plane  = (float)far_item->valuedouble;

                cJSON *active_item = cJSON_GetObjectItemCaseSensitive(cam_json, "is_active");
                if (cJSON_IsBool(active_item)) cam.is_active = cJSON_IsTrue(active_item);

                world_add_component(world, live, cam_ctx->c_camera, &cam);
            }

            // -- sprite ----------------------------------------------------------
            cJSON *spr_json = cJSON_GetObjectItemCaseSensitive(comps, "sprite");
            if (spr_json != nullptr && c_sprite != UINT8_MAX) {
                cJSON *tex_item = cJSON_GetObjectItemCaseSensitive(spr_json, "texture");
                if (cJSON_IsString(tex_item)) {
                    AssetHandle h = texmap_lookup(&texmap, tex_item->valuestring);
                    if (h == ASSET_HANDLE_INVALID) {
                        // Texture wasn't in the manifest — try loading on the fly.
                        h = asset_manager_load_texture(am, tex_item->valuestring);
                    } else {
                        // Texture was preloaded by manifest, increment ref count for Sprite component ownership
                        asset_manager_add_ref(am, h);
                    }
                    if (h != ASSET_HANDLE_INVALID) {
                        // Query texture dimensions for UV computation.
                        uint32_t tw = 0, th = 0;
                        asset_manager_get_texture_size(am, h, &tw, &th);

                        // Parse optional pixel rect for spritesheet slicing.
                        cJSON *rect_json = cJSON_GetObjectItemCaseSensitive(
                            spr_json, "rect");

                        Sprite sprite;
                        if (rect_json != nullptr) {
                            Rect pixel_rect = {
                                .x = (float)cJSON_GetNumberValue(
                                    cJSON_GetObjectItemCaseSensitive(rect_json, "x")),
                                .y = (float)cJSON_GetNumberValue(
                                    cJSON_GetObjectItemCaseSensitive(rect_json, "y")),
                                .w = (float)cJSON_GetNumberValue(
                                    cJSON_GetObjectItemCaseSensitive(rect_json, "w")),
                                .h = (float)cJSON_GetNumberValue(
                                    cJSON_GetObjectItemCaseSensitive(rect_json, "h")),
                            };
                            sprite = sprite_from_sheet(h, tw, th, pixel_rect);
                        } else {
                            // No rect — use the full texture (backward compatible).
                            sprite = sprite_from_texture(h, tw, th);
                        }

                        SceneSprite spr = { .sprite = sprite };
                        world_add_component(world, live, c_sprite, &spr);
                    } else {
                        fprintf(stderr, "[scene] warning: sprite texture '%s' not found\n",
                                tex_item->valuestring);
                    }
                }
            }

            // -- velocity --------------------------------------------------------
            cJSON *vel_json = cJSON_GetObjectItemCaseSensitive(comps, "velocity");
            if (vel_json != nullptr && c_velocity != UINT8_MAX) {
                SceneVelocity vel = {
                    .dx = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(vel_json, "dx")),
                    .dy = (float)cJSON_GetNumberValue(
                               cJSON_GetObjectItemCaseSensitive(vel_json, "dy")),
                };
                world_add_component(world, live, c_velocity, &vel);
            }

            // -- scripts ---------------------------------------------------------
            cJSON *scripts_json = cJSON_GetObjectItemCaseSensitive(comps, "scripts");
            if (cJSON_IsArray(scripts_json) && lua != nullptr) {
                cJSON *script_entry = nullptr;
                cJSON_ArrayForEach(script_entry, scripts_json) {
                    cJSON *path_item = cJSON_GetObjectItemCaseSensitive(
                        script_entry, "path");
                    if (cJSON_IsString(path_item)) {
                        lua_host_attach_script(lua, live,
                                              path_item->valuestring);
                    }
                }
            }

            // -- animator --------------------------------------------------------
            cJSON *anim_json = cJSON_GetObjectItemCaseSensitive(comps, "animator");
            if (anim_json != nullptr && c_animator != UINT8_MAX) {
                cJSON *ctrl_item = cJSON_GetObjectItemCaseSensitive(anim_json, "controller");
                if (cJSON_IsString(ctrl_item)) {
                    const char *ctrl_path = ctrl_item->valuestring;
                    AnimCache *acache = engine_get_anim_cache(engine);
                    AnimController *ctrl = nullptr;
                    if (acache != nullptr) {
                        ctrl = anim_cache_load_controller(acache, ctrl_path, am);
                    }

                    if (ctrl != nullptr) {
                        SceneAnimator sa = {};
                        animator_init(&sa.animator);
                        strncpy(sa.animator.controller_path, ctrl_path, AnimPathMaxLen - 1);
                        sa.animator.controller_path[AnimPathMaxLen - 1] = '\0';
                        sa.animator.controller = ctrl;
                        sa.animator.current_state = ctrl->default_state;
                        animator_reset_params(&sa.animator);

                        // Override saved params if present.
                        cJSON *params_j = cJSON_GetObjectItemCaseSensitive(anim_json, "params");
                        if (cJSON_IsObject(params_j)) {
                            for (uint32_t pi = 0; pi < ctrl->param_count; ++pi) {
                                cJSON *pv = cJSON_GetObjectItemCaseSensitive(params_j, ctrl->params[pi].name);
                                if (pv == nullptr) continue;
                                switch (ctrl->params[pi].type) {
                                    case ANIM_PARAM_FLOAT:
                                        if (cJSON_IsNumber(pv))
                                            sa.animator.params[pi].f = (float)pv->valuedouble;
                                        break;
                                    case ANIM_PARAM_INT:
                                        if (cJSON_IsNumber(pv))
                                            sa.animator.params[pi].i = (int32_t)pv->valuedouble;
                                        break;
                                    case ANIM_PARAM_BOOL:
                                        if (cJSON_IsBool(pv))
                                            sa.animator.params[pi].b = cJSON_IsTrue(pv);
                                        break;
                                    case ANIM_PARAM_TRIGGER:
                                        break;
                                }
                            }
                        }

                        // Start playing the default state's clip.
                        if (ctrl->state_count > 0) {
                            const AnimState *ds = &ctrl->states[ctrl->default_state];
                            animator_play(&sa.animator, ds->clip_name);
                        }

                        // Optionally start a specific clip (overrides controller default).
                        cJSON *clip_item = cJSON_GetObjectItemCaseSensitive(anim_json, "clip");
                        if (cJSON_IsString(clip_item)) {
                            animator_play(&sa.animator, clip_item->valuestring);
                        }

                        cJSON *playing_item = cJSON_GetObjectItemCaseSensitive(anim_json, "playing");
                        if (cJSON_IsBool(playing_item)) {
                            sa.animator.playing = cJSON_IsTrue(playing_item);
                        }

                        world_add_component(world, live, c_animator, &sa);
                    } else {
                        fprintf(stderr, "[scene] warning: animation controller '%s' not found or failed to load\n", ctrl_path);
                    }
                }
            }

            // -- platformer_controller -------------------------------------------
            ComponentId c_pctrl = platformer_controller_get_id();
            cJSON *pctrl_json = cJSON_GetObjectItemCaseSensitive(comps, "platformer_controller");
            if (pctrl_json != nullptr && c_pctrl != UINT8_MAX) {
                PlatformerController pctrl = {};
                
                cJSON *g_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "gravity");
                pctrl.gravity = cJSON_IsNumber(g_j) ? (float)g_j->valuedouble : 9.81f;

                cJSON *mfs_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "max_fall_speed");
                pctrl.max_fall_speed = cJSON_IsNumber(mfs_j) ? (float)mfs_j->valuedouble : 20.0f;

                cJSON *rs_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "run_speed");
                pctrl.run_speed = cJSON_IsNumber(rs_j) ? (float)rs_j->valuedouble : 5.0f;

                cJSON *ra_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "run_acceleration");
                pctrl.run_acceleration = cJSON_IsNumber(ra_j) ? (float)ra_j->valuedouble : 30.0f;

                cJSON *rd_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "run_deceleration");
                pctrl.run_deceleration = cJSON_IsNumber(rd_j) ? (float)rd_j->valuedouble : 30.0f;

                cJSON *jf_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "jump_force");
                pctrl.jump_force = cJSON_IsNumber(jf_j) ? (float)jf_j->valuedouble : 8.0f;

                cJSON *jcg_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "jump_cut_gravity_mult");
                pctrl.jump_cut_gravity_mult = cJSON_IsNumber(jcg_j) ? (float)jcg_j->valuedouble : 2.5f;

                cJSON *cot_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "coyote_time");
                pctrl.coyote_time = cJSON_IsNumber(cot_j) ? (float)cot_j->valuedouble : 0.15f;

                cJSON *jbt_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "jump_buffer_time");
                pctrl.jump_buffer_time = cJSON_IsNumber(jbt_j) ? (float)jbt_j->valuedouble : 0.1f;

                cJSON *mj_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "max_jumps");
                pctrl.max_jumps = cJSON_IsNumber(mj_j) ? (int32_t)mj_j->valuedouble : 1;

                cJSON *ds_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "dash_speed");
                pctrl.dash_speed = cJSON_IsNumber(ds_j) ? (float)ds_j->valuedouble : 15.0f;

                cJSON *dd_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "dash_duration");
                pctrl.dash_duration = cJSON_IsNumber(dd_j) ? (float)dd_j->valuedouble : 0.2f;

                cJSON *dc_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "dash_cooldown");
                pctrl.dash_cooldown = cJSON_IsNumber(dc_j) ? (float)dc_j->valuedouble : 0.6f;

                cJSON *wss_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "wall_slide_speed");
                pctrl.wall_slide_speed = cJSON_IsNumber(wss_j) ? (float)wss_j->valuedouble : 2.0f;

                cJSON *wjfx_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "wall_jump_force_x");
                pctrl.wall_jump_force_x = cJSON_IsNumber(wjfx_j) ? (float)wjfx_j->valuedouble : 6.0f;

                cJSON *wjfy_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "wall_jump_force_y");
                pctrl.wall_jump_force_y = cJSON_IsNumber(wjfy_j) ? (float)wjfy_j->valuedouble : 8.0f;

                cJSON *wjcl_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "wall_jump_control_lock");
                pctrl.wall_jump_control_lock = cJSON_IsNumber(wjcl_j) ? (float)wjcl_j->valuedouble : 0.15f;

                cJSON *kl_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "key_left");
                pctrl.key_left = cJSON_IsNumber(kl_j) ? (int32_t)kl_j->valuedouble : 65; // KEY_A

                cJSON *kr_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "key_right");
                pctrl.key_right = cJSON_IsNumber(kr_j) ? (int32_t)kr_j->valuedouble : 68; // KEY_D

                cJSON *kj_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "key_jump");
                pctrl.key_jump = cJSON_IsNumber(kj_j) ? (int32_t)kj_j->valuedouble : 32; // KEY_SPACE

                cJSON *kd_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "key_dash");
                pctrl.key_dash = cJSON_IsNumber(kd_j) ? (int32_t)kd_j->valuedouble : 340; // KEY_LEFT_SHIFT

                cJSON *edj_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "enable_double_jump");
                pctrl.enable_double_jump = cJSON_IsBool(edj_j) ? cJSON_IsTrue(edj_j) : true;

                cJSON *ewj_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "enable_wall_jump");
                pctrl.enable_wall_jump = cJSON_IsBool(ewj_j) ? cJSON_IsTrue(ewj_j) : true;

                cJSON *ed_j = cJSON_GetObjectItemCaseSensitive(pctrl_json, "enable_dash");
                pctrl.enable_dash = cJSON_IsBool(ed_j) ? cJSON_IsTrue(ed_j) : true;

                pctrl.facing_dir = 1;
                pctrl.can_dash = true;

                world_add_component(world, live, c_pctrl, &pctrl);
            }

            // -- platformer_collider ---------------------------------------------
            ComponentId c_pcol = platformer_collider_get_id();
            cJSON *pcol_json = cJSON_GetObjectItemCaseSensitive(comps, "platformer_collider");
            if (pcol_json != nullptr && c_pcol != UINT8_MAX) {
                PlatformerCollider pcol = {};

                cJSON *w_j = cJSON_GetObjectItemCaseSensitive(pcol_json, "width");
                pcol.width = cJSON_IsNumber(w_j) ? (float)w_j->valuedouble : 1.0f;

                cJSON *h_j = cJSON_GetObjectItemCaseSensitive(pcol_json, "height");
                pcol.height = cJSON_IsNumber(h_j) ? (float)h_j->valuedouble : 1.0f;

                cJSON *ox_j = cJSON_GetObjectItemCaseSensitive(pcol_json, "offset_x");
                pcol.offset_x = cJSON_IsNumber(ox_j) ? (float)ox_j->valuedouble : 0.0f;

                cJSON *oy_j = cJSON_GetObjectItemCaseSensitive(pcol_json, "offset_y");
                pcol.offset_y = cJSON_IsNumber(oy_j) ? (float)oy_j->valuedouble : 0.0f;

                cJSON *sol_j = cJSON_GetObjectItemCaseSensitive(pcol_json, "is_solid");
                pcol.is_solid = cJSON_IsBool(sol_j) ? cJSON_IsTrue(sol_j) : true;

                world_add_component(world, live, c_pcol, &pcol);
            }
        }
    }

    // --- 5. Hierarchy re-linking (second pass) ----------------------------
    for (uint32_t i = 0; i < parent_link_count; ++i) {
        Entity child_live  = idmap_get(&idmap, parent_links[i].child_json_id);
        Entity parent_live = idmap_get(&idmap, parent_links[i].parent_json_id);

        if (child_live == ENTITY_INVALID || parent_live == ENTITY_INVALID) {
            fprintf(stderr, "[scene] warning: cannot resolve parent link "
                    "(child=%u → parent=%u)\n",
                    parent_links[i].child_json_id,
                    parent_links[i].parent_json_id);
            continue;
        }

        hierarchy_set_parent(world, hctx, child_live, parent_live);
    }

    // --- 6. Initial transform propagation ---------------------------------
    hierarchy_update_transforms(world, hctx);

    // --- 7. Initialize script instances -----------------------------------
    if (lua != nullptr) {
        lua_host_scripts_init(lua);
    }

    // --- Cleanup ----------------------------------------------------------
    free(parent_links);
    idmap_destroy(&idmap);
    cJSON_Delete(root);

    printf("[scene] loaded successfully: %s (%u entities)\n",
           filepath, entity_count_est);
    return true;
}

// ===========================================================================
// scene_save
// ===========================================================================

bool scene_save(Engine *engine, const char *filepath) {
    if (engine == nullptr || filepath == nullptr) return false;

    printf("[scene] saving: %s\n", filepath);

    // --- Grab engine subsystems -------------------------------------------
    World            *world   = engine_get_world(engine);
    AssetManager     *am      = engine_get_asset_manager(engine);
    HierarchyContext *hctx    = engine_get_hctx(engine);
    CameraContext    *cam_ctx = engine_get_cam_ctx(engine);
    LuaHost          *lua     = engine_get_lua_host(engine);

    if (world == nullptr || am == nullptr || hctx == nullptr || cam_ctx == nullptr) {
        fprintf(stderr, "[scene] engine subsystems not ready\n");
        return false;
    }

    // Sprite / Velocity / Script component IDs (may not be registered).
    bool has_sprite   = (lua != nullptr && lua_host_sprite_registered(lua));
    bool has_velocity = (lua != nullptr && lua_host_velocity_registered(lua));
    bool has_script   = (lua != nullptr && lua_host_script_registered(lua));
    bool has_animator = (lua != nullptr && lua_host_animator_registered(lua));
    ComponentId c_sprite   = has_sprite   ? lua_host_get_sprite_id(lua)   : UINT8_MAX;
    ComponentId c_velocity = has_velocity ? lua_host_get_velocity_id(lua) : UINT8_MAX;
    ComponentId c_script   = has_script   ? lua_host_get_script_id(lua)   : UINT8_MAX;
    ComponentId c_animator = has_animator ? lua_host_get_animator_id(lua) : UINT8_MAX;

    // --- Build JSON tree --------------------------------------------------
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        fprintf(stderr, "[scene] failed to create JSON root\n");
        return false;
    }

    // -- Collect unique texture paths from Sprite components ---------------
    cJSON *assets_obj  = cJSON_AddObjectToObject(root, "assets");
    cJSON *tex_array   = cJSON_AddArrayToObject(assets_obj, "textures");

    // Track paths we've already added (simple dedup).
    char added_paths[MAX_SCENE_TEXTURES][SCENE_PATH_MAX];
    uint32_t added_count = 0;

    if (has_sprite) {
        ComponentPool *spr_pool = world_get_pool(world, c_sprite);
        if (spr_pool != nullptr) {
            for (uint32_t i = 0; i < spr_pool->count; ++i) {
                SceneSprite *spr = (SceneSprite *)component_pool_get_dense(spr_pool, i);
                const char *path = asset_manager_get_path(am, spr->sprite.texture);
                if (path == nullptr) continue;

                // Dedup.
                bool found = false;
                for (uint32_t j = 0; j < added_count; ++j) {
                    if (strcmp(added_paths[j], path) == 0) { found = true; break; }
                }
                if (!found && added_count < MAX_SCENE_TEXTURES) {
                    cJSON_AddItemToArray(tex_array, cJSON_CreateString(path));
                    strncpy(added_paths[added_count], path, SCENE_PATH_MAX - 1);
                    added_paths[added_count][SCENE_PATH_MAX - 1] = '\0';
                    added_count++;
                }
            }
        }
    }

    // -- Serialize entities ------------------------------------------------
    cJSON *ent_array = cJSON_AddArrayToObject(root, "entities");

    // We iterate the LocalTransform pool as the "primary" pool — every
    // meaningful scene entity has a LocalTransform.
    ComponentPool *lt_pool = world_get_pool(world, hctx->c_local_transform);
    if (lt_pool == nullptr || lt_pool->count == 0) {
        // Empty scene — write just the skeleton.
        char *json_str = cJSON_Print(root);
        if (json_str == nullptr) {
            cJSON_Delete(root);
            return false;
        }
        FILE *f = fopen(filepath, "w");
        if (f == nullptr) {
            fprintf(stderr, "[scene] cannot write file: %s\n", filepath);
            free(json_str);
            cJSON_Delete(root);
            return false;
        }
        fputs(json_str, f);
        fclose(f);
        free(json_str);
        cJSON_Delete(root);
        printf("[scene] saved (empty scene): %s\n", filepath);
        return true;
    }

    for (uint32_t i = 0; i < lt_pool->count; ++i) {
        uint32_t ent_idx = component_pool_get_entity(lt_pool, i);
        Entity ent = world_entity_from_index(world, ent_idx);

        cJSON *ent_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(ent_obj, "id", (double)ent_idx);

        // Check for parent.
        Hierarchy *hier = (Hierarchy *)world_get_component(
            world, ent, hctx->c_hierarchy);
        if (hier != nullptr && hier->parent != ENTITY_INVALID) {
            cJSON_AddNumberToObject(ent_obj, "parent",
                                    (double)entity_index(hier->parent));
        }

        // Components block.
        cJSON *comps = cJSON_AddObjectToObject(ent_obj, "components");

        // -- local_transform -------------------------------------------------
        LocalTransform *lt = (LocalTransform *)component_pool_get_dense(lt_pool, i);
        {
            cJSON *lt_obj = cJSON_AddObjectToObject(comps, "local_transform");
            cJSON_AddNumberToObject(lt_obj, "x",  (double)lt->x);
            cJSON_AddNumberToObject(lt_obj, "y",  (double)lt->y);
            cJSON_AddNumberToObject(lt_obj, "sx", (double)lt->sx);
            cJSON_AddNumberToObject(lt_obj, "sy", (double)lt->sy);
        }

        // -- camera ----------------------------------------------------------
        Camera *cam = (Camera *)world_get_component(world, ent, cam_ctx->c_camera);
        if (cam != nullptr) {
            cJSON *cam_obj = cJSON_AddObjectToObject(comps, "camera");
            cJSON_AddStringToObject(cam_obj, "projection",
                cam->projection == CAMERA_PERSPECTIVE ? "perspective" : "orthographic");
            cJSON_AddNumberToObject(cam_obj, "fov",        (double)cam->fov);
            cJSON_AddNumberToObject(cam_obj, "near_plane", (double)cam->near_plane);
            cJSON_AddNumberToObject(cam_obj, "far_plane",  (double)cam->far_plane);
            cJSON_AddNumberToObject(cam_obj, "ortho_size", (double)cam->ortho_size);
            cJSON_AddBoolToObject(cam_obj, "is_active", cam->is_active);
        }

        // -- sprite ----------------------------------------------------------
        if (has_sprite && c_sprite != UINT8_MAX) {
            SceneSprite *spr = (SceneSprite *)world_get_component(
                world, ent, c_sprite);
            if (spr != nullptr) {
                const char *tex_path = asset_manager_get_path(am, spr->sprite.texture);
                if (tex_path != nullptr) {
                    cJSON *spr_obj = cJSON_AddObjectToObject(comps, "sprite");
                    cJSON_AddStringToObject(spr_obj, "texture", tex_path);

                    // Always write the pixel rect so the sub-region is preserved.
                    cJSON *rect_obj = cJSON_AddObjectToObject(spr_obj, "rect");
                    cJSON_AddNumberToObject(rect_obj, "x", (double)spr->sprite.rect.x);
                    cJSON_AddNumberToObject(rect_obj, "y", (double)spr->sprite.rect.y);
                    cJSON_AddNumberToObject(rect_obj, "w", (double)spr->sprite.rect.w);
                    cJSON_AddNumberToObject(rect_obj, "h", (double)spr->sprite.rect.h);
                }
            }
        }

        // -- velocity --------------------------------------------------------
        if (has_velocity && c_velocity != UINT8_MAX) {
            SceneVelocity *vel = (SceneVelocity *)world_get_component(
                world, ent, c_velocity);
            if (vel != nullptr) {
                cJSON *vel_obj = cJSON_AddObjectToObject(comps, "velocity");
                cJSON_AddNumberToObject(vel_obj, "dx", (double)vel->dx);
                cJSON_AddNumberToObject(vel_obj, "dy", (double)vel->dy);
            }
        }

        // -- scripts ---------------------------------------------------------
        if (has_script && c_script != UINT8_MAX) {
            ScriptComponent *sc = (ScriptComponent *)world_get_component(
                world, ent, c_script);
            if (sc != nullptr && sc->count > 0) {
                cJSON *scripts_arr = cJSON_AddArrayToObject(comps, "scripts");
                for (uint8_t s = 0; s < sc->count; ++s) {
                    cJSON *entry = cJSON_CreateObject();
                    cJSON_AddStringToObject(entry, "path",
                                           sc->slots[s].path);
                    cJSON_AddItemToArray(scripts_arr, entry);
                }
            }
        }

        // -- animator --------------------------------------------------------
        if (has_animator && c_animator != UINT8_MAX) {
            SceneAnimator *sa = (SceneAnimator *)world_get_component(
                world, ent, c_animator);
            if (sa != nullptr && sa->animator.controller_path[0] != '\0') {
                cJSON *anim_obj = cJSON_AddObjectToObject(comps, "animator");
                cJSON_AddStringToObject(anim_obj, "controller",
                                       sa->animator.controller_path);

                if (sa->animator.controller != nullptr &&
                    sa->animator.controller->anim_data != nullptr &&
                    sa->animator.current_clip < sa->animator.controller->anim_data->clip_count) {
                    cJSON_AddStringToObject(anim_obj, "clip",
                        sa->animator.controller->anim_data->clips[sa->animator.current_clip].name);
                }
                cJSON_AddBoolToObject(anim_obj, "playing", sa->animator.playing);

                // Save runtime parameters (only non-default values).
                if (sa->animator.controller != nullptr &&
                    sa->animator.controller->param_count > 0) {
                    cJSON *params_obj = cJSON_AddObjectToObject(
                        anim_obj, "params");
                    const AnimController *ctrl = sa->animator.controller;
                    for (uint32_t pi = 0; pi < ctrl->param_count; ++pi) {
                        const AnimParam *p = &ctrl->params[pi];
                        switch (p->type) {
                            case ANIM_PARAM_FLOAT:
                                cJSON_AddNumberToObject(
                                    params_obj, p->name,
                                    (double)sa->animator.params[pi].f);
                                break;
                            case ANIM_PARAM_INT:
                                cJSON_AddNumberToObject(
                                    params_obj, p->name,
                                    (double)sa->animator.params[pi].i);
                                break;
                            case ANIM_PARAM_BOOL:
                                cJSON_AddBoolToObject(
                                    params_obj, p->name,
                                    sa->animator.params[pi].b);
                                break;
                            case ANIM_PARAM_TRIGGER:
                                break; // triggers are transient
                        }
                    }
                }
            }
        }

        // -- platformer_controller -------------------------------------------
        ComponentId c_plat_ctrl = platformer_controller_get_id();
        if (c_plat_ctrl != UINT8_MAX) {
            PlatformerController *plat = (PlatformerController *)world_get_component(world, ent, c_plat_ctrl);
            if (plat != nullptr) {
                cJSON *plat_obj = cJSON_AddObjectToObject(comps, "platformer_controller");
                cJSON_AddNumberToObject(plat_obj, "gravity", (double)plat->gravity);
                cJSON_AddNumberToObject(plat_obj, "max_fall_speed", (double)plat->max_fall_speed);
                cJSON_AddNumberToObject(plat_obj, "run_speed", (double)plat->run_speed);
                cJSON_AddNumberToObject(plat_obj, "run_acceleration", (double)plat->run_acceleration);
                cJSON_AddNumberToObject(plat_obj, "run_deceleration", (double)plat->run_deceleration);
                cJSON_AddNumberToObject(plat_obj, "jump_force", (double)plat->jump_force);
                cJSON_AddNumberToObject(plat_obj, "jump_cut_gravity_mult", (double)plat->jump_cut_gravity_mult);
                cJSON_AddNumberToObject(plat_obj, "coyote_time", (double)plat->coyote_time);
                cJSON_AddNumberToObject(plat_obj, "jump_buffer_time", (double)plat->jump_buffer_time);
                cJSON_AddNumberToObject(plat_obj, "max_jumps", (double)plat->max_jumps);
                cJSON_AddNumberToObject(plat_obj, "dash_speed", (double)plat->dash_speed);
                cJSON_AddNumberToObject(plat_obj, "dash_duration", (double)plat->dash_duration);
                cJSON_AddNumberToObject(plat_obj, "dash_cooldown", (double)plat->dash_cooldown);
                cJSON_AddNumberToObject(plat_obj, "wall_slide_speed", (double)plat->wall_slide_speed);
                cJSON_AddNumberToObject(plat_obj, "wall_jump_force_x", (double)plat->wall_jump_force_x);
                cJSON_AddNumberToObject(plat_obj, "wall_jump_force_y", (double)plat->wall_jump_force_y);
                cJSON_AddNumberToObject(plat_obj, "wall_jump_control_lock", (double)plat->wall_jump_control_lock);
                cJSON_AddNumberToObject(plat_obj, "key_left", (double)plat->key_left);
                cJSON_AddNumberToObject(plat_obj, "key_right", (double)plat->key_right);
                cJSON_AddNumberToObject(plat_obj, "key_jump", (double)plat->key_jump);
                cJSON_AddNumberToObject(plat_obj, "key_dash", (double)plat->key_dash);
                cJSON_AddBoolToObject(plat_obj, "enable_double_jump", plat->enable_double_jump);
                cJSON_AddBoolToObject(plat_obj, "enable_wall_jump", plat->enable_wall_jump);
                cJSON_AddBoolToObject(plat_obj, "enable_dash", plat->enable_dash);
            }
        }

        // -- platformer_collider ---------------------------------------------
        ComponentId c_plat_col = platformer_collider_get_id();
        if (c_plat_col != UINT8_MAX) {
            PlatformerCollider *col = (PlatformerCollider *)world_get_component(world, ent, c_plat_col);
            if (col != nullptr) {
                cJSON *col_obj = cJSON_AddObjectToObject(comps, "platformer_collider");
                cJSON_AddNumberToObject(col_obj, "width", (double)col->width);
                cJSON_AddNumberToObject(col_obj, "height", (double)col->height);
                cJSON_AddNumberToObject(col_obj, "offset_x", (double)col->offset_x);
                cJSON_AddNumberToObject(col_obj, "offset_y", (double)col->offset_y);
                cJSON_AddBoolToObject(col_obj, "is_solid", col->is_solid);
            }
        }

        cJSON_AddItemToArray(ent_array, ent_obj);
    }

    // --- Write to file ----------------------------------------------------
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == nullptr) {
        fprintf(stderr, "[scene] failed to serialize JSON\n");
        return false;
    }

    FILE *f = fopen(filepath, "w");
    if (f == nullptr) {
        fprintf(stderr, "[scene] cannot write file: %s\n", filepath);
        free(json_str);
        return false;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    printf("[scene] saved successfully: %s (%u entities)\n",
           filepath, lt_pool->count);
    return true;
}

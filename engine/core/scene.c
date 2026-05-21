#include "scene.h"
#include "engine.h"
#include "ecs/ecs.h"
#include "asset_manager.h"
#include "../renderer/renderer.h"

// Lua host internal accessors — needed for Sprite/Velocity ComponentIds.
#include "scripting/lua_host.h"
extern bool        lua_host_sprite_registered(LuaHost *host);
extern ComponentId lua_host_get_sprite_id(LuaHost *host);
extern void        lua_host_set_sprite_id(LuaHost *host, ComponentId id);
extern bool        lua_host_velocity_registered(LuaHost *host);
extern ComponentId lua_host_get_velocity_id(LuaHost *host);
extern void        lua_host_set_velocity_id(LuaHost *host, ComponentId id);

#include "../../third_party/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Component structs — must mirror the layouts in lua_bindings.c / main.c.
// ---------------------------------------------------------------------------

typedef struct SceneSprite {
    AssetHandle texture;
} SceneSprite;

typedef struct SceneVelocity {
    float dx, dy;
} SceneVelocity;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Read the entire contents of a file into a malloc'd string.
/// Caller must free() the returned buffer.  Returns nullptr on failure.
static char *read_file_to_string(const char *filepath) {
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
                if (spr->texture != ASSET_HANDLE_INVALID) {
                    asset_manager_release(am, spr->texture);
                }
            }
            printf("[scene] released %u sprite asset reference(s)\n",
                   spr_pool->count);
        }
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
    TexMap texmap = {0};

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
    ComponentId c_sprite   = UINT8_MAX;
    ComponentId c_velocity = UINT8_MAX;

    // Pre-scan: do we need Sprite or Velocity components?
    {
        cJSON *ent_json = nullptr;
        cJSON_ArrayForEach(ent_json, entities) {
            cJSON *comps = cJSON_GetObjectItemCaseSensitive(ent_json, "components");
            if (comps == nullptr) continue;
            if (cJSON_GetObjectItemCaseSensitive(comps, "sprite"))   need_sprite   = true;
            if (cJSON_GetObjectItemCaseSensitive(comps, "velocity")) need_velocity = true;
        }
    }

    if (need_sprite)   c_sprite   = ensure_sprite_component(world, lua);
    if (need_velocity) c_velocity = ensure_velocity_component(world, lua);

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
                    }
                    if (h != ASSET_HANDLE_INVALID) {
                        SceneSprite spr = { .texture = h };
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
    ComponentId c_sprite   = has_sprite   ? lua_host_get_sprite_id(lua)   : UINT8_MAX;
    ComponentId c_velocity = has_velocity ? lua_host_get_velocity_id(lua) : UINT8_MAX;
    ComponentId c_script   = has_script   ? lua_host_get_script_id(lua)   : UINT8_MAX;

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
                const char *path = asset_manager_get_path(am, spr->texture);
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
                const char *tex_path = asset_manager_get_path(am, spr->texture);
                if (tex_path != nullptr) {
                    cJSON *spr_obj = cJSON_AddObjectToObject(comps, "sprite");
                    cJSON_AddStringToObject(spr_obj, "texture", tex_path);
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

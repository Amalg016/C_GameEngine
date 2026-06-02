#ifndef ENGINE_EDITOR_PANEL_INSPECTOR_H
#define ENGINE_EDITOR_PANEL_INSPECTOR_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Inspector Panel — displays and edits components on the selected entity.
//
// Supports:
//   - Transform editing (position, scale)
//   - Camera component (projection, fov, ortho_size, near/far, active)
//   - Sprite component (texture path)
//   - Velocity component (dx, dy)
//   - Script component (per-script variables from Lua)
//   - Hierarchy info (parent, first child)
//   - Previous position (read-only, for interpolation debugging)
//   - Drop target for drag-and-drop (accepts "ENTITY" payloads)
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

typedef struct World            World;
typedef struct HierarchyContext HierarchyContext;
typedef struct CameraContext    CameraContext;
typedef struct LuaHost          LuaHost;
typedef struct AssetManager     AssetManager;
typedef struct AnimCache        AnimCache;

/// Render the inspector panel for the currently selected entity.
///
/// `p_open`          — visibility flag.
/// `world`           — the ECS world.
/// `hctx`            — hierarchy context (for component IDs).
/// `cam_ctx`         — camera context (for camera component ID).
/// `lua_host`        — Lua scripting host (for Sprite/Velocity/Script IDs
///                     and Lua VM introspection). May be nullptr.
/// `am`              — asset manager (for texture path lookups). May be nullptr.
/// `acache`          — animation cache (for loading animations/controllers). May be nullptr.
/// `selected_entity` — entity index to inspect.
/// `has_selection`    — true if an entity is currently selected.
void panel_inspector_render(bool *p_open,
                            World *world,
                            const HierarchyContext *hctx,
                            const CameraContext *cam_ctx,
                            LuaHost *lua_host,
                            AssetManager *am,
                            AnimCache *acache,
                            uint32_t selected_entity,
                            bool has_selection);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_INSPECTOR_H

#ifndef ENGINE_CORE_SCENE_H
#define ENGINE_CORE_SCENE_H

#include "ecs/ecs.h"
#include "asset_manager.h"

#include <stdbool.h>

// ---------------------------------------------------------------------------
// Scene Management — JSON-based serialization / deserialization.
//
// Provides functions to save the entire ECS state to a flat JSON file and
// to load it back, replacing the current state.
//
// The scene file stores:
//   - An asset manifest (texture paths to pre-load).
//   - An entity list with named component blocks (local_transform, hierarchy,
//     camera, sprite, velocity).
//   - Parent references (resolved via an ID mapping table during load).
//
// Usage:
//   // Save current scene:
//   scene_save(engine, "scenes/my_scene.json");
//
//   // Load a scene (replaces current state):
//   scene_load(engine, "scenes/my_scene.json");
// ---------------------------------------------------------------------------

// Forward declaration — avoids pulling in engine.h.
typedef struct Engine Engine;

/// Load a scene from a JSON file, replacing the current ECS state.
///
/// Performs these steps in order:
///   1. Safety flush — clears all entities and component data.
///   2. Asset pre-loading — loads textures referenced in the scene.
///   3. Entity instantiation — creates entities and populates components.
///   4. Hierarchy re-linking — resolves parent/child relationships using
///      an ID mapping table (JSON ID → live Entity).
///   5. Transform propagation — computes initial WorldTransforms.
///
/// Returns true on success, false on file/parse errors.
bool scene_load(Engine *engine, const char *filepath);

/// Save the current ECS state to a JSON scene file.
///
/// Iterates all live entities and serializes their components:
///   - LocalTransform (position + scale)
///   - Hierarchy (parent reference)
///   - Camera (projection type + parameters)
///   - Sprite (asset path via reverse handle lookup)
///   - Velocity (dx, dy)
///
/// Returns true on success, false on write errors.
bool scene_save(Engine *engine, const char *filepath);

#endif // ENGINE_CORE_SCENE_H

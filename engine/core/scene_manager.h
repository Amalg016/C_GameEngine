#ifndef ENGINE_CORE_SCENE_MANAGER_H
#define ENGINE_CORE_SCENE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// SceneManager — maintains an ordered list of scene file paths and tracks
// which scene is currently active.
//
// The scene list can be populated either:
//   - From a manifest JSON file (scene_manager_load_manifest).
//   - Programmatically via scene_manager_add_scene().
//
// The SceneManager does NOT load or unload scenes itself — it only tracks
// paths and the current index.  The Engine layer calls engine_switch_scene()
// to perform the actual scene transitions.
//
// Manifest file format (e.g. assets/scenes/scene_manifest.json):
//   {
//       "scenes": [
//           "assets/scenes/level_01.json",
//           "assets/scenes/level_02.json"
//       ]
//   }
// ---------------------------------------------------------------------------

/// Opaque handle — internals hidden in scene_manager.c.
typedef struct SceneManager SceneManager;

// --- Lifecycle -------------------------------------------------------------

/// Create an empty SceneManager.  Returns nullptr on allocation failure.
[[nodiscard]]
SceneManager *scene_manager_create(void);

/// Destroy the SceneManager, freeing all stored paths.
void scene_manager_destroy(SceneManager *sm);

// --- Manifest loading ------------------------------------------------------

/// Parse a JSON manifest file and populate the scene list.
/// Clears any previously added scenes.  Returns true on success.
bool scene_manager_load_manifest(SceneManager *sm, const char *manifest_path);

// --- Scene list manipulation -----------------------------------------------

/// Append a scene path to the ordered list.
/// The path string is duplicated internally.  Returns true on success.
bool scene_manager_add_scene(SceneManager *sm, const char *scene_path);

/// Remove all scenes from the list and reset the current index to -1.
void scene_manager_clear(SceneManager *sm);

// --- Queries ---------------------------------------------------------------

/// Get the total number of scenes in the list.
uint32_t scene_manager_get_count(const SceneManager *sm);

/// Get the index of the currently active scene (-1 if none).
int32_t scene_manager_get_current_index(const SceneManager *sm);

/// Get the file path of a scene by index.
/// Returns nullptr if index is out of range.
const char *scene_manager_get_scene_path(const SceneManager *sm, uint32_t index);

/// Get the file path of the currently active scene.
/// Returns nullptr if no scene is active.
const char *scene_manager_get_current_path(const SceneManager *sm);

// --- Navigation ------------------------------------------------------------

/// Set the current scene index.  Does NOT load/unload anything.
/// Pass -1 to indicate no scene is active.
void scene_manager_set_current_index(SceneManager *sm, int32_t index);

/// Find the index of a scene by its file path.
/// Returns -1 if not found.
int32_t scene_manager_find_scene(const SceneManager *sm, const char *scene_path);

#endif // ENGINE_CORE_SCENE_MANAGER_H

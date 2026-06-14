#ifndef ENGINE_EDITOR_H
#define ENGINE_EDITOR_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Editor Module — top-level editor layer that sits on top of the engine.
//
// The editor is a fully separate module compiled only when EDITOR_BUILD is
// defined.  It orchestrates the ImGui layer, dockspace, and all editor
// panels.  Runtime builds exclude this file entirely — zero overhead.
//
// Lifecycle:
//   1. editor_create()     — after engine_init()
//   2. editor_begin_frame() — inside the render loop, after game draws
//   3. editor_end_frame()   — records ImGui draw commands into cmd buffer
//   4. editor_destroy()     — before engine_destroy()
// ---------------------------------------------------------------------------

#include <stdbool.h>

typedef struct Engine Engine;
typedef struct Editor Editor;

/// Create and initialise the editor layer.
/// Initialises ImGui, creates the dockspace, and sets up all panels.
/// Must be called AFTER engine_init() (renderer must be ready).
/// Returns nullptr on failure.
[[nodiscard]]
Editor *editor_create(Engine *engine);

/// Destroy the editor and free all resources.
/// Must be called BEFORE engine_destroy().
void editor_destroy(Editor *editor);

/// Begin a new editor frame.
/// Sets up the ImGui frame, renders the dockspace, menu bar, and all panels.
/// Call after the game's on_render callback but BEFORE editor_end_frame.
void editor_begin_frame(Editor *editor);

/// End the editor frame and record draw commands into the Vulkan cmd buffer.
/// Must be called while a render pass is active (before renderer_end_frame).
void editor_end_frame(Editor *editor);

/// Save the last active scene path to the editor metadata file.
void editor_save_meta(Editor *editor, const char *scene_path);

/// Get the last active scene path read from the editor metadata file.
/// Returns nullptr if no metadata file was found or it couldn't be parsed.
const char *editor_get_last_scene(const Editor *editor);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_H

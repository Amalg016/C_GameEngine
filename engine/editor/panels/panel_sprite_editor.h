#ifndef ENGINE_EDITOR_PANEL_SPRITE_EDITOR_H
#define ENGINE_EDITOR_PANEL_SPRITE_EDITOR_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Sprite Editor Panel — visual spritesheet slicer.
//
// Displays a loaded texture with a grid overlay, lets the user configure
// slicing parameters (cols/rows), generates named sprite regions, and
// persists them to a `.sprite.meta` JSON file alongside the texture.
// ---------------------------------------------------------------------------

#include <stdbool.h>

typedef struct AssetManager AssetManager;
typedef struct Renderer     Renderer;

/// Render the sprite editor panel.
/// `p_open`   — visibility flag.
/// `am`       — asset manager for texture loading.
/// `renderer` — renderer for ImGui texture registration.
void panel_sprite_editor_render(bool *p_open,
                                AssetManager *am,
                                Renderer *renderer);

/// Open the sprite editor with a specific texture pre-loaded.
/// Called e.g. by the content browser on double-click of a .png file.
void panel_sprite_editor_open(const char *texture_path,
                              AssetManager *am,
                              Renderer *renderer);

/// Clean up any resources held by the sprite editor (e.g. ImGui textures).
/// Called during editor shutdown.
void panel_sprite_editor_shutdown(Renderer *renderer);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_SPRITE_EDITOR_H

#ifndef ENGINE_EDITOR_PANELS_PANEL_ANIMATION_EDITOR_H
#define ENGINE_EDITOR_PANELS_PANEL_ANIMATION_EDITOR_H

#ifdef EDITOR_BUILD

#include <stdbool.h>

typedef struct AssetManager AssetManager;
typedef struct Renderer Renderer;

/// Render the Animation Editor panel.
void panel_animation_editor_render(bool *p_open,
                                   AssetManager *am,
                                   Renderer *renderer);

/// Release GPU resources held by the animation editor (call before shutdown).
void panel_animation_editor_shutdown(Renderer *renderer);

#endif // EDITOR_BUILD

#endif // ENGINE_EDITOR_PANELS_PANEL_ANIMATION_EDITOR_H

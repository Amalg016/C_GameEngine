#ifndef ENGINE_EDITOR_PANEL_SCENE_LIST_H
#define ENGINE_EDITOR_PANEL_SCENE_LIST_H

#ifdef EDITOR_BUILD

#include <stdbool.h>

typedef struct Engine Engine;

/// Render the Scene List Manager panel.
void panel_scene_list_render(bool *p_open, Engine *engine);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_SCENE_LIST_H

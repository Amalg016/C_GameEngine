#ifndef ENGINE_EDITOR_PANELS_PANEL_CONTROLLER_EDITOR_H
#define ENGINE_EDITOR_PANELS_PANEL_CONTROLLER_EDITOR_H

#ifdef EDITOR_BUILD

typedef struct AssetManager AssetManager;
typedef struct Renderer     Renderer;

/// Render the Animation Controller Editor panel.
void panel_controller_editor_render(bool *p_open,
                                    AssetManager *am,
                                    Renderer *renderer);

/// Release any GPU resources held by the controller editor.
void panel_controller_editor_shutdown(Renderer *renderer);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANELS_PANEL_CONTROLLER_EDITOR_H

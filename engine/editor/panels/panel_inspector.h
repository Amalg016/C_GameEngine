#ifndef ENGINE_EDITOR_PANEL_INSPECTOR_H
#define ENGINE_EDITOR_PANEL_INSPECTOR_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Inspector Panel — displays and edits components on the selected entity.
//
// Supports:
//   - Transform editing (position, scale)
//   - Drop target for drag-and-drop (accepts "ENTITY" payloads)
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

typedef struct World            World;
typedef struct HierarchyContext HierarchyContext;

/// Render the inspector panel for the currently selected entity.
///
/// `p_open`          — visibility flag.
/// `world`           — the ECS world.
/// `hctx`            — hierarchy context (for component IDs).
/// `selected_entity` — entity index to inspect.
/// `has_selection`    — true if an entity is currently selected.
void panel_inspector_render(bool *p_open,
                            World *world,
                            const HierarchyContext *hctx,
                            uint32_t selected_entity,
                            bool has_selection);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_INSPECTOR_H

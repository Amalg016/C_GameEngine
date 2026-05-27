#ifndef ENGINE_EDITOR_PANEL_HIERARCHY_H
#define ENGINE_EDITOR_PANEL_HIERARCHY_H

#ifdef EDITOR_BUILD

// ---------------------------------------------------------------------------
// Scene Hierarchy Panel — displays all entities in a selectable list.
//
// Supports:
//   - Entity selection (click to select, syncs with Inspector)
//   - Drag source for drag-and-drop (payload type: "ENTITY")
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

typedef struct World            World;
typedef struct HierarchyContext HierarchyContext;

/// Render the scene hierarchy panel.
///
/// `p_open`          — visibility flag.
/// `world`           — the ECS world to iterate.
/// `hctx`            — hierarchy context (for component IDs).
/// `selected_entity` — in/out: currently selected entity index.
/// `has_selection`    — in/out: true if any entity is selected.
void panel_hierarchy_render(bool *p_open,
                            World *world,
                            const HierarchyContext *hctx,
                            uint32_t *selected_entity,
                            bool *has_selection);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_PANEL_HIERARCHY_H

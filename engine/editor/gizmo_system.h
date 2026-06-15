#ifndef ENGINE_EDITOR_GIZMO_SYSTEM_H
#define ENGINE_EDITOR_GIZMO_SYSTEM_H

#ifdef EDITOR_BUILD

typedef struct World World;
typedef struct HierarchyContext HierarchyContext;

/// Draw wireframe gizmos for all PlatformerColliders with show_gizmo == true.
void gizmo_system_draw_colliders(const World *world, const HierarchyContext *hctx);

#endif // EDITOR_BUILD
#endif // ENGINE_EDITOR_GIZMO_SYSTEM_H

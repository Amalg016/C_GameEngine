#include "test_framework.h"
#include "engine/core/ecs/world.h"
#include "engine/core/ecs/component_pool.h"
#include "engine/core/ecs/hierarchy.h"
#include "engine/core/ecs/ecs_types.h"
#include <stdlib.h>

// A test struct to use as a component
typedef struct TestComponent {
    int val1;
    float val2;
} TestComponent;

static void test_component_pool_basics(void) {
    ComponentPool *pool = component_pool_create(sizeof(TestComponent));
    ASSERT(pool != nullptr);
    ASSERT(pool->count == 0);
    ASSERT(pool->element_size == sizeof(TestComponent));

    Entity e1 = entity_make(1, 1);
    Entity e2 = entity_make(2, 1);
    Entity e3 = entity_make(3, 1);

    TestComponent tc1 = { .val1 = 42, .val2 = 3.14f };
    TestComponent tc2 = { .val1 = 100, .val2 = 2.71f };
    TestComponent tc3 = { .val1 = 999, .val2 = 1.41f };

    // Add elements
    void *ptr1 = component_pool_add(pool, e1, &tc1);
    ASSERT(ptr1 != nullptr);
    ASSERT(pool->count == 1);
    ASSERT(component_pool_has(pool, e1));

    TestComponent *ret1 = component_pool_get(pool, e1);
    ASSERT(ret1 != nullptr);
    ASSERT(ret1->val1 == 42);
    ASSERT_FLOAT_EQ(ret1->val2, 3.14f, 1e-6f);

    component_pool_add(pool, e2, &tc2);
    component_pool_add(pool, e3, &tc3);
    ASSERT(pool->count == 3);

    // Verify dense array contiguous contents
    TestComponent *dense_tc = pool->dense_data;
    ASSERT(dense_tc[0].val1 == 42);
    ASSERT(dense_tc[1].val1 == 100);
    ASSERT(dense_tc[2].val1 == 999);

    // Test swap-and-pop removal of the middle element (e2)
    component_pool_remove(pool, e2);
    ASSERT(pool->count == 2);
    ASSERT(!component_pool_has(pool, e2));
    ASSERT(component_pool_get(pool, e2) == nullptr);

    // After swap-and-pop, e3 (index 2) should be swapped into slot 1 (where e2 was)
    // Dense data at slot 0: e1 (42)
    // Dense data at slot 1: e3 (999)
    ASSERT(dense_tc[0].val1 == 42);
    ASSERT(dense_tc[1].val1 == 999);
    
    // Check dense IDs
    ASSERT(component_pool_get_entity(pool, 0) == 1); // e1 index
    ASSERT(component_pool_get_entity(pool, 1) == 3); // e3 index

    // Check retrieval still works for remaining entities
    TestComponent *ret3 = component_pool_get(pool, e3);
    ASSERT(ret3 != nullptr);
    ASSERT(ret3->val1 == 999);

    component_pool_clear(pool);
    ASSERT(pool->count == 0);
    ASSERT(!component_pool_has(pool, e1));

    component_pool_destroy(pool);
}

static void test_world_lifecycle_and_recycling(void) {
    World *world = world_create();
    ASSERT(world != nullptr);

    // Create a few entities
    Entity e1 = world_entity_create(world);
    Entity e2 = world_entity_create(world);
    Entity e3 = world_entity_create(world);

    ASSERT(e1 != ENTITY_INVALID);
    ASSERT(e2 != ENTITY_INVALID);
    ASSERT(e3 != ENTITY_INVALID);

    // Index 0 is reserved for ENTITY_INVALID, so first entity gets index 1
    ASSERT(entity_index(e1) == 1);
    ASSERT(entity_index(e2) == 2);
    ASSERT(entity_index(e3) == 3);

    // Initial generation is 0
    ASSERT(entity_generation(e1) == 0);
    ASSERT(entity_generation(e2) == 0);
    ASSERT(entity_generation(e3) == 0);

    ASSERT(world_entity_alive(world, e1));
    ASSERT(world_entity_alive(world, e2));
    ASSERT(world_entity_alive(world, e3));

    // Destroy e2 (index 2)
    world_entity_destroy(world, e2);
    ASSERT(!world_entity_alive(world, e2));

    // Create a new entity, which should recycle e2's index but have generation bumped to 1
    Entity e2_new = world_entity_create(world);
    ASSERT(entity_index(e2_new) == 2);
    ASSERT(entity_generation(e2_new) == 1);
    ASSERT(world_entity_alive(world, e2_new));
    
    // Stale handle of e2 (generation 0) must be dead
    ASSERT(!world_entity_alive(world, e2));

    world_destroy(world);
}

static void test_world_component_ops(void) {
    World *world = world_create();
    ComponentId c_test = world_register_component(world, sizeof(TestComponent));
    ASSERT(c_test != UINT8_MAX);

    Entity e = world_entity_create(world);

    // Test zero-initialization behavior
    void *ptr_zero = world_add_component(world, e, c_test, nullptr);
    ASSERT(ptr_zero != nullptr);
    TestComponent *tc_zero = ptr_zero;
    ASSERT(tc_zero->val1 == 0);
    ASSERT_FLOAT_EQ(tc_zero->val2, 0.0f, 1e-6f);

    // Test overwrite (remove then add)
    world_remove_component(world, e, c_test);
    ASSERT(!world_has_component(world, e, c_test));

    TestComponent tc = { .val1 = 55, .val2 = 1.23f };
    world_add_component(world, e, c_test, &tc);
    ASSERT(world_has_component(world, e, c_test));

    TestComponent *ret = world_get_component(world, e, c_test);
    ASSERT(ret != nullptr);
    ASSERT(ret->val1 == 55);
    ASSERT_FLOAT_EQ(ret->val2, 1.23f, 1e-6f);

    // Mass clear
    world_clear(world);
    ASSERT(!world_entity_alive(world, e));
    ASSERT(world_get_pool(world, c_test) != nullptr); // pool should still be registered
    ASSERT(world_get_pool(world, c_test)->count == 0);

    world_destroy(world);
}

static void test_hierarchy_tree_and_propagation(void) {
    World *world = world_create();
    HierarchyContext ctx = hierarchy_init(world);

    // Create Parent, Child, Grandchild
    Entity parent = world_entity_create(world);
    Entity child = world_entity_create(world);
    Entity grandchild = world_entity_create(world);

    // Initialise LocalTransform components
    LocalTransform lt_parent = { .x = 10.0f, .y = 20.0f, .sx = 2.0f, .sy = 3.0f };
    LocalTransform lt_child = { .x = 5.0f, .y = -2.0f, .sx = 0.5f, .sy = 1.5f };
    LocalTransform lt_grandchild = { .x = 2.0f, .y = 4.0f, .sx = 2.0f, .sy = 0.5f };

    world_add_component(world, parent, ctx.c_local_transform, &lt_parent);
    world_add_component(world, child, ctx.c_local_transform, &lt_child);
    world_add_component(world, grandchild, ctx.c_local_transform, &lt_grandchild);

    // Set hierarchy parent relationships
    hierarchy_set_parent(world, &ctx, child, parent);
    hierarchy_set_parent(world, &ctx, grandchild, child);

    // Verify sibling and parent linkages in structural hierarchy
    Hierarchy *h_parent = world_get_component(world, parent, ctx.c_hierarchy);
    Hierarchy *h_child = world_get_component(world, child, ctx.c_hierarchy);
    Hierarchy *h_grandchild = world_get_component(world, grandchild, ctx.c_hierarchy);

    ASSERT(h_parent != nullptr);
    ASSERT(h_child != nullptr);
    ASSERT(h_grandchild != nullptr);

    ASSERT(h_parent->parent == ENTITY_INVALID);
    ASSERT(h_parent->first_child == child);

    ASSERT(h_child->parent == parent);
    ASSERT(h_child->first_child == grandchild);
    ASSERT(h_child->next_sibling == ENTITY_INVALID);

    ASSERT(h_grandchild->parent == child);
    ASSERT(h_grandchild->first_child == ENTITY_INVALID);

    // Propagate transforms
    hierarchy_update_transforms(world, &ctx);

    // Retrieve computed world transforms
    WorldTransform *wt_parent = world_get_component(world, parent, ctx.c_world_transform);
    WorldTransform *wt_child = world_get_component(world, child, ctx.c_world_transform);
    WorldTransform *wt_grandchild = world_get_component(world, grandchild, ctx.c_world_transform);

    ASSERT(wt_parent != nullptr);
    ASSERT(wt_child != nullptr);
    ASSERT(wt_grandchild != nullptr);

    // Parent is root, WorldTransform == LocalTransform
    ASSERT_FLOAT_EQ(wt_parent->x, 10.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_parent->y, 20.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_parent->sx, 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_parent->sy, 3.0f, 1e-6f);

    // Child:
    // wt->x = parent_wt->x + child_lt->x * parent_wt->sx = 10.0f + 5.0f * 2.0f = 20.0f
    // wt->y = parent_wt->y + child_lt->y * parent_wt->sy = 20.0f + (-2.0f) * 3.0f = 14.0f
    // wt->sx = parent_wt->sx * child_lt->sx = 2.0f * 0.5f = 1.0f
    // wt->sy = parent_wt->sy * child_lt->sy = 3.0f * 1.5f = 4.5f
    ASSERT_FLOAT_EQ(wt_child->x, 20.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_child->y, 14.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_child->sx, 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_child->sy, 4.5f, 1e-6f);

    // Grandchild:
    // wt->x = child_wt->x + gc_lt->x * child_wt->sx = 20.0f + 2.0f * 1.0f = 22.0f
    // wt->y = child_wt->y + gc_lt->y * child_wt->sy = 14.0f + 4.0f * 4.5f = 32.0f
    // wt->sx = child_wt->sx * gc_lt->sx = 1.0f * 2.0f = 2.0f
    // wt->sy = child_wt->sy * gc_lt->sy = 4.5f * 0.5f = 2.25f
    ASSERT_FLOAT_EQ(wt_grandchild->x, 22.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_grandchild->y, 32.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_grandchild->sx, 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(wt_grandchild->sy, 2.25f, 1e-6f);

    // Test snapshotting
    // Add PreviousPosition component to grandchild
    PreviousPosition pp = { .x = -99.0f, .y = -99.0f };
    world_add_component(world, grandchild, ctx.c_prev_position, &pp);

    hierarchy_snapshot_positions(world, &ctx);

    PreviousPosition *pp_gc = world_get_component(world, grandchild, ctx.c_prev_position);
    ASSERT(pp_gc != nullptr);
    // Should match grandchild WorldTransform x & y before transform changes
    ASSERT_FLOAT_EQ(pp_gc->x, 22.0f, 1e-6f);
    ASSERT_FLOAT_EQ(pp_gc->y, 32.0f, 1e-6f);

    // Detach grandchild
    hierarchy_remove_parent(world, &ctx, grandchild);
    h_child = world_get_component(world, child, ctx.c_hierarchy);
    h_grandchild = world_get_component(world, grandchild, ctx.c_hierarchy);
    ASSERT(h_child->first_child == ENTITY_INVALID);
    ASSERT(h_grandchild->parent == ENTITY_INVALID);

    world_destroy(world);
}

void test_ecs_run(void) {
    printf(COLOR_BLUE COLOR_BOLD "\n--- Running ECS Tests ---" COLOR_RESET "\n");
    RUN_TEST(test_component_pool_basics);
    RUN_TEST(test_world_lifecycle_and_recycling);
    RUN_TEST(test_world_component_ops);
    RUN_TEST(test_hierarchy_tree_and_propagation);
}

# ECS and Clock Issues

This document registers critical bugs, desynchronization hazards, and architectural behaviors discovered in the Custom ECS and frame clock subsystems.

---

## 1. Desynchronization on `realloc` Failure in `grow_dense`

### Description
During dynamic component pool expansion in `component_pool.c`, a `realloc` failure for `dense_ids` causes the pool to return `false` while leaving `dense_data` pointing to the new larger capacity size. This desynchronizes the parallel arrays.

### Code Context
In [component_pool.c:L27-35](file:///home/amalg/PersonalProjects/CProjects/GameEngine/engine/core/ecs/component_pool.c#L27-L35):
```c
    void *new_data = realloc(pool->dense_data, (size_t)new_cap * pool->element_size);
    if (new_data == nullptr) return false;
    pool->dense_data = new_data;

    uint32_t *new_ids = realloc(pool->dense_ids, (size_t)new_cap * sizeof(uint32_t));
    if (new_ids == nullptr) return false; // <--- Returns failure, but dense_data is already mutated!
    pool->dense_ids = new_ids;
```

### Impact
> [!CAUTION]
> If memory is extremely constrained and the second allocation fails, the pool enters a corrupted state where `pool->capacity` is not updated to reflect `new_data`'s new boundaries, but `dense_data` has grown while `dense_ids` remained at the original smaller size. This will cause subsequent component operations or iterations to trigger out-of-bounds reads or writes.

### Recommended Fix
Perform both `realloc` queries using local temporary pointers, and only update the struct's fields (`dense_data`, `dense_ids`, and `capacity`) after both allocations have succeeded.

---

## 2. Silent Deactivation of Cyclic Parent-Child Subtrees

### Description
The transform hierarchy updates work by running a Depth-First Search (DFS) propagation starting exclusively from root nodes. If a parent-child loop is created, neither node is categorized as a root, causing the entire subtree to be skipped.

### Code Context
In [hierarchy.c:L203-219](file:///home/amalg/PersonalProjects/CProjects/GameEngine/engine/core/ecs/hierarchy.c#L203-L219):
```c
    // Iterate every entity that has a LocalTransform.
    for (uint32_t i = 0; i < lt_pool->count; ++i) {
        // ...
        // Skip non-roots — they'll be visited during DFS from their root.
        if (h != nullptr && h->parent != ENTITY_INVALID) continue;

        // This is a root — ensure it has a WorldTransform and propagate.
        ensure_world_transform(world, ctx->c_world_transform, ent);
        propagate_recursive(world, ctx, ent, nullptr);
    }
```

### Impact
> [!NOTE]
> Since neither element in a cycle has its parent set to `ENTITY_INVALID`, they are skipped during iteration and never visited by `propagate_recursive()`. This avoids an infinite recursion stack overflow (which is beneficial), but silently freezes/stops updating the world transforms of all entities in the cycle.

### Recommended Fix
Verify cycle detection inside `hierarchy_set_parent()` by traversing up the parent chain to ensure `parent` is not a descendant of `child` before committing the linkage.

---

## 3. Discrepancy Between Clock Elapsed Time and Timestep Accumulator

### Description
When the platform timer returns a negative delta (due to clock synchronization, NTP jumps, or thread scheduler timing adjustments), the frame tick clamps `delta` to `0.0` but still updates `elapsed` using the raw negative jump.

### Code Context
In [clock.c:L33-49](file:///home/amalg/PersonalProjects/CProjects/GameEngine/engine/core/clock.c#L33-L49):
```c
    double now   = platform_get_time();
    double delta = now - clock->last_time;
    clock->last_time = now;

    // ... clamp delta to 0.0 if negative ...

    clock->delta_time   = delta;
    clock->elapsed      = now - clock->start_time; // <--- elapsed is decremented by the raw negative jump
    clock->accumulator += delta;                   // <--- accumulator gets 0.0
```

### Impact
> [!WARNING]
> While updating `clock->last_time = now` correctly prevents the clock from freezing until real-time catches up, the raw assignment of `elapsed` causes the game's total elapsed time count to decrease. This results in a mismatch between `clock->elapsed` and the sum of all frame deltas.

### Recommended Fix
Advance `clock->elapsed` using `clock->elapsed += delta` rather than a direct subtraction of raw platform timestamps, ensuring monotonic progression of game time.

#include "asset_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define ASSET_MAP_INITIAL_CAPACITY 64
#define ASSET_PATH_MAX             256

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

typedef struct AssetEntry {
    char       path[ASSET_PATH_MAX];   // cache key (empty = unused slot)
    uint32_t   ref_count;
    AssetType  type;
    void      *gpu_data;               // opaque, owned by backend
    uint32_t   handle;                 // 1-based handle
} AssetEntry;

struct AssetManager {
    AssetEntry           *entries;
    uint32_t              capacity;
    uint32_t              count;       // number of live entries
    uint32_t              next_handle; // monotonically increasing
    AssetManagerCallbacks cbs;
};

// ---------------------------------------------------------------------------
// FNV-1a hash (used for path → slot mapping)
// ---------------------------------------------------------------------------

static uint32_t fnv1a(const char *str) {
    uint32_t hash = 2166136261u;
    for (const char *p = str; *p != '\0'; ++p) {
        hash ^= (uint8_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Find an entry by path.  Returns the index, or capacity if not found.
static uint32_t find_by_path(const AssetManager *am, const char *path) {
    if (am->count == 0) return am->capacity;

    uint32_t start = fnv1a(path) % am->capacity;
    uint32_t idx   = start;

    do {
        if (am->entries[idx].path[0] == '\0') {
            return am->capacity; // empty slot — not found
        }
        if (strcmp(am->entries[idx].path, path) == 0) {
            return idx;
        }
        idx = (idx + 1) % am->capacity;
    } while (idx != start);

    return am->capacity;
}

/// Find an entry by handle.  Returns the index, or capacity if not found.
static uint32_t find_by_handle(const AssetManager *am, AssetHandle handle) {
    if (handle == ASSET_HANDLE_INVALID) return am->capacity;

    // Linear scan — fine for the expected number of assets (< 1000).
    for (uint32_t i = 0; i < am->capacity; ++i) {
        if (am->entries[i].handle == handle && am->entries[i].path[0] != '\0') {
            return i;
        }
    }
    return am->capacity;
}

/// Find the next free slot starting from `start`.
static uint32_t find_free_slot(const AssetManager *am, uint32_t start) {
    uint32_t idx = start % am->capacity;
    uint32_t orig = idx;
    do {
        if (am->entries[idx].path[0] == '\0') return idx;
        idx = (idx + 1) % am->capacity;
    } while (idx != orig);
    return am->capacity; // full
}

/// Grow the hash map when load factor exceeds ~70%.
static bool maybe_grow(AssetManager *am) {
    if (am->count < (am->capacity * 7) / 10) return true; // no need

    uint32_t old_cap = am->capacity;
    AssetEntry *old = am->entries;
    uint32_t new_cap = old_cap * 2;

    AssetEntry *new_entries = calloc(new_cap, sizeof(AssetEntry));
    if (new_entries == nullptr) return false;

    am->entries  = new_entries;
    am->capacity = new_cap;
    am->count    = 0;

    // Re-insert all live entries.
    for (uint32_t i = 0; i < old_cap; ++i) {
        if (old[i].path[0] == '\0') continue;
        uint32_t slot = find_free_slot(am, fnv1a(old[i].path) % new_cap);
        am->entries[slot] = old[i];
        am->count++;
    }

    free(old);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AssetManager *asset_manager_create(void) {
    AssetManager *am = calloc(1, sizeof(AssetManager));
    if (am == nullptr) return nullptr;

    am->entries  = calloc(ASSET_MAP_INITIAL_CAPACITY, sizeof(AssetEntry));
    if (am->entries == nullptr) {
        free(am);
        return nullptr;
    }

    am->capacity    = ASSET_MAP_INITIAL_CAPACITY;
    am->next_handle = 1; // 0 is ASSET_HANDLE_INVALID

    printf("[asset_manager] created (capacity=%u)\n", am->capacity);
    return am;
}

void asset_manager_destroy(AssetManager *am) {
    if (am == nullptr) return;

    // Release all remaining assets.
    for (uint32_t i = 0; i < am->capacity; ++i) {
        if (am->entries[i].path[0] == '\0') continue;

        if (am->entries[i].gpu_data != nullptr &&
            am->cbs.destroy_texture != nullptr &&
            am->entries[i].type == ASSET_TYPE_TEXTURE) {
            am->cbs.destroy_texture(am->cbs.backend_ctx,
                                    am->entries[i].gpu_data);
        }

        printf("[asset_manager] leaked asset: '%s' (ref_count=%u)\n",
               am->entries[i].path, am->entries[i].ref_count);
    }

    free(am->entries);
    free(am);
    printf("[asset_manager] destroyed\n");
}

void asset_manager_set_callbacks(AssetManager               *am,
                                 const AssetManagerCallbacks *cbs) {
    if (am == nullptr || cbs == nullptr) return;
    am->cbs = *cbs;
}

AssetHandle asset_manager_load_texture(AssetManager *am,
                                       const char   *path) {
    if (am == nullptr || path == nullptr) return ASSET_HANDLE_INVALID;

    // Check cache first.
    uint32_t idx = find_by_path(am, path);
    if (idx < am->capacity) {
        am->entries[idx].ref_count++;
        printf("[asset_manager] cache hit: '%s' (ref_count=%u)\n",
               path, am->entries[idx].ref_count);
        return am->entries[idx].handle;
    }

    // Not cached — load via backend callback.
    if (am->cbs.load_texture == nullptr) {
        fprintf(stderr, "[asset_manager] no load_texture callback set\n");
        return ASSET_HANDLE_INVALID;
    }

    void *gpu_data = am->cbs.load_texture(am->cbs.backend_ctx, path);
    if (gpu_data == nullptr) {
        fprintf(stderr, "[asset_manager] failed to load texture: %s\n", path);
        return ASSET_HANDLE_INVALID;
    }

    // Grow if needed.
    if (!maybe_grow(am)) {
        am->cbs.destroy_texture(am->cbs.backend_ctx, gpu_data);
        return ASSET_HANDLE_INVALID;
    }

    // Insert into cache.
    uint32_t slot = find_free_slot(am, fnv1a(path) % am->capacity);
    if (slot >= am->capacity) {
        am->cbs.destroy_texture(am->cbs.backend_ctx, gpu_data);
        fprintf(stderr, "[asset_manager] hash map full (should not happen)\n");
        return ASSET_HANDLE_INVALID;
    }

    AssetHandle h = am->next_handle++;
    AssetEntry *e = &am->entries[slot];
    strncpy(e->path, path, ASSET_PATH_MAX - 1);
    e->path[ASSET_PATH_MAX - 1] = '\0';
    e->ref_count = 1;
    e->type      = ASSET_TYPE_TEXTURE;
    e->gpu_data  = gpu_data;
    e->handle    = h;
    am->count++;

    printf("[asset_manager] loaded: '%s' → handle %u (ref_count=1)\n", path, h);
    return h;
}

void asset_manager_add_ref(AssetManager *am, AssetHandle handle) {
    if (am == nullptr) return;
    uint32_t idx = find_by_handle(am, handle);
    if (idx < am->capacity) {
        am->entries[idx].ref_count++;
    }
}

void asset_manager_release(AssetManager *am, AssetHandle handle) {
    if (am == nullptr) return;

    uint32_t idx = find_by_handle(am, handle);
    if (idx >= am->capacity) return;

    AssetEntry *e = &am->entries[idx];
    if (e->ref_count == 0) return;

    e->ref_count--;
    printf("[asset_manager] release: '%s' handle %u (ref_count=%u)\n",
           e->path, handle, e->ref_count);

    if (e->ref_count == 0) {
        // Free GPU resources.
        if (e->gpu_data != nullptr) {
            if (e->type == ASSET_TYPE_TEXTURE && am->cbs.destroy_texture != nullptr) {
                am->cbs.destroy_texture(am->cbs.backend_ctx, e->gpu_data);
            }
        }

        printf("[asset_manager] freed: '%s'\n", e->path);
        *e = (AssetEntry){};  // clear slot
        am->count--;
    }
}

void *asset_manager_get_data(AssetManager *am, AssetHandle handle) {
    if (am == nullptr) return nullptr;
    uint32_t idx = find_by_handle(am, handle);
    if (idx >= am->capacity) return nullptr;
    return am->entries[idx].gpu_data;
}

uint32_t asset_manager_get_ref_count(AssetManager *am, AssetHandle handle) {
    if (am == nullptr) return 0;
    uint32_t idx = find_by_handle(am, handle);
    if (idx >= am->capacity) return 0;
    return am->entries[idx].ref_count;
}

const char *asset_manager_get_path(const AssetManager *am, AssetHandle handle) {
    if (am == nullptr || handle == ASSET_HANDLE_INVALID) return nullptr;

    // Linear scan — mirrors find_by_handle() but accepts const.
    for (uint32_t i = 0; i < am->capacity; ++i) {
        if (am->entries[i].handle == handle && am->entries[i].path[0] != '\0') {
            return am->entries[i].path;
        }
    }
    return nullptr;
}

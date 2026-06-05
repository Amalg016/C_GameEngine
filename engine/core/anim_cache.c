#include "anim_cache.h"
#include "asset_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr uint32_t AnimCacheMaxEntries = 32;

typedef struct AnimCacheEntry {
    char     path[AnimPathMaxLen];
    AnimData data;
    bool     used;
} AnimCacheEntry;

typedef struct CtrlCacheEntry {
    char           path[AnimPathMaxLen];
    AnimController ctrl;
    bool           used;
} CtrlCacheEntry;

struct AnimCache {
    AnimCacheEntry entries[AnimCacheMaxEntries];
    uint32_t       count;

    CtrlCacheEntry ctrl_entries[AnimCacheMaxEntries];
    uint32_t       ctrl_count;
};

AnimCache *anim_cache_create(void) {
    AnimCache *cache = calloc(1, sizeof(AnimCache));
    if (cache == nullptr) {
        fprintf(stderr, "[anim_cache] allocation failed\n");
        return nullptr;
    }
    return cache;
}

void anim_cache_destroy(AnimCache *cache, AssetManager *am) {
    if (cache == nullptr) return;
    if (am != nullptr) {
        for (uint32_t i = 0; i < AnimCacheMaxEntries; ++i) {
            if (cache->ctrl_entries[i].used) {
                if (cache->ctrl_entries[i].ctrl.texture != ASSET_HANDLE_INVALID) {
                    asset_manager_release(am, cache->ctrl_entries[i].ctrl.texture);
                    cache->ctrl_entries[i].ctrl.texture = ASSET_HANDLE_INVALID;
                }
            }
        }
    }
    free(cache);
}

AnimData *anim_cache_load(AnimCache *cache, const char *anim_path) {
    if (cache == nullptr || anim_path == nullptr) return nullptr;

    // Search for an existing entry.
    for (uint32_t i = 0; i < AnimCacheMaxEntries; ++i) {
        if (cache->entries[i].used &&
            strcmp(cache->entries[i].path, anim_path) == 0) {
            return &cache->entries[i].data;
        }
    }

    // Find a free slot.
    if (cache->count >= AnimCacheMaxEntries) {
        fprintf(stderr, "[anim_cache] cache full (%u entries)\n",
                AnimCacheMaxEntries);
        return nullptr;
    }

    AnimCacheEntry *entry = nullptr;
    for (uint32_t i = 0; i < AnimCacheMaxEntries; ++i) {
        if (!cache->entries[i].used) {
            entry = &cache->entries[i];
            break;
        }
    }

    if (entry == nullptr) return nullptr; // should not happen

    // Load from disk.
    anim_data_init(&entry->data);

    if (!anim_data_load(anim_path, &entry->data)) {
        fprintf(stderr, "[anim_cache] failed to load: %s\n", anim_path);
        return nullptr;
    }

    strncpy(entry->path, anim_path, AnimPathMaxLen - 1);
    entry->path[AnimPathMaxLen - 1] = '\0';
    entry->used = true;
    cache->count++;

    printf("[anim_cache] cached: %s (%u clip(s))\n",
           anim_path, entry->data.clip_count);

    return &entry->data;
}

AnimController *anim_cache_load_controller(AnimCache *cache,
                                           const char *ctrl_path,
                                           AssetManager *am) {
    if (cache == nullptr || ctrl_path == nullptr) return nullptr;

    // Search for an existing entry.
    for (uint32_t i = 0; i < AnimCacheMaxEntries; ++i) {
        if (cache->ctrl_entries[i].used &&
            strcmp(cache->ctrl_entries[i].path, ctrl_path) == 0) {
            return &cache->ctrl_entries[i].ctrl;
        }
    }

    // Find a free slot.
    if (cache->ctrl_count >= AnimCacheMaxEntries) {
        fprintf(stderr, "[anim_cache] controller cache full (%u entries)\n",
                AnimCacheMaxEntries);
        return nullptr;
    }

    CtrlCacheEntry *entry = nullptr;
    for (uint32_t i = 0; i < AnimCacheMaxEntries; ++i) {
        if (!cache->ctrl_entries[i].used) {
            entry = &cache->ctrl_entries[i];
            break;
        }
    }

    if (entry == nullptr) return nullptr;

    // Load from disk.
    anim_controller_init(&entry->ctrl);

    if (!anim_controller_load(ctrl_path, &entry->ctrl)) {
        fprintf(stderr, "[anim_cache] failed to load controller: %s\n",
                ctrl_path);
        return nullptr;
    }

    // Resolve anim data.
    if (entry->ctrl.anim_path[0] != '\0') {
        entry->ctrl.anim_data = anim_cache_load(cache, entry->ctrl.anim_path);
        if (entry->ctrl.anim_data != nullptr && am != nullptr) {
            entry->ctrl.texture = asset_manager_load_texture(am, entry->ctrl.anim_data->texture_path);
            asset_manager_get_texture_size(am, entry->ctrl.texture, &entry->ctrl.tex_width, &entry->ctrl.tex_height);
        }
    }

    strncpy(entry->path, ctrl_path, AnimPathMaxLen - 1);
    entry->path[AnimPathMaxLen - 1] = '\0';
    entry->used = true;
    cache->ctrl_count++;

    printf("[anim_cache] cached controller: %s (%u state(s))\n",
           ctrl_path, entry->ctrl.state_count);

    return &entry->ctrl;
}


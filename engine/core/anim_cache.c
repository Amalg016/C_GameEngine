#include "anim_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr uint32_t AnimCacheMaxEntries = 32;

typedef struct AnimCacheEntry {
    char     path[AnimPathMaxLen];
    AnimData data;
    bool     used;
} AnimCacheEntry;

struct AnimCache {
    AnimCacheEntry entries[AnimCacheMaxEntries];
    uint32_t       count;
};

AnimCache *anim_cache_create(void) {
    AnimCache *cache = calloc(1, sizeof(AnimCache));
    if (cache == nullptr) {
        fprintf(stderr, "[anim_cache] allocation failed\n");
        return nullptr;
    }
    return cache;
}

void anim_cache_destroy(AnimCache *cache) {
    if (cache == nullptr) return;
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

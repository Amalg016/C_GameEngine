#include "scene_manager.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// SceneManager internals
// ---------------------------------------------------------------------------

constexpr uint32_t InitialCapacity = 8;

struct SceneManager {
    char    **scene_paths;     // dynamic array of strdup'd file paths
    uint32_t  count;           // number of scenes in the list
    uint32_t  capacity;        // allocated slots in scene_paths
    int32_t   current_index;   // -1 = no scene active
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SceneManager *scene_manager_create(void) {
    SceneManager *sm = calloc(1, sizeof(SceneManager));
    if (sm == nullptr) return nullptr;

    sm->scene_paths = calloc(InitialCapacity, sizeof(char *));
    if (sm->scene_paths == nullptr) {
        free(sm);
        return nullptr;
    }

    sm->capacity      = InitialCapacity;
    sm->count         = 0;
    sm->current_index = -1;

    return sm;
}

void scene_manager_destroy(SceneManager *sm) {
    if (sm == nullptr) return;

    for (uint32_t i = 0; i < sm->count; ++i) {
        free(sm->scene_paths[i]);
    }
    free(sm->scene_paths);
    free(sm);
}

// ---------------------------------------------------------------------------
// Internal: grow the path array if needed.
// ---------------------------------------------------------------------------

static bool ensure_capacity(SceneManager *sm) {
    if (sm->count < sm->capacity) return true;

    uint32_t new_cap = sm->capacity * 2;
    char **new_paths = realloc(sm->scene_paths, new_cap * sizeof(char *));
    if (new_paths == nullptr) return false;

    sm->scene_paths = new_paths;
    sm->capacity    = new_cap;
    return true;
}

// ---------------------------------------------------------------------------
// Manifest loading
// ---------------------------------------------------------------------------

bool scene_manager_load_manifest(SceneManager *sm, const char *manifest_path) {
    if (sm == nullptr || manifest_path == nullptr) return false;

    FILE *f = fopen(manifest_path, "rb");
    if (f == nullptr) {
        fprintf(stderr, "[scene_manager] cannot open manifest: %s\n",
                manifest_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        fprintf(stderr, "[scene_manager] empty manifest: %s\n", manifest_path);
        return false;
    }

    char *buf = malloc((size_t)size + 1);
    if (buf == nullptr) {
        fclose(f);
        return false;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (root == nullptr) {
        fprintf(stderr, "[scene_manager] JSON parse error in manifest: %s\n",
                manifest_path);
        return false;
    }

    cJSON *scenes = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    if (!cJSON_IsArray(scenes)) {
        fprintf(stderr, "[scene_manager] manifest missing 'scenes' array: %s\n",
                manifest_path);
        cJSON_Delete(root);
        return false;
    }

    // Clear existing scenes.
    scene_manager_clear(sm);

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, scenes) {
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            if (!scene_manager_add_scene(sm, item->valuestring)) {
                fprintf(stderr, "[scene_manager] failed to add scene: %s\n",
                        item->valuestring);
            }
        }
    }

    cJSON_Delete(root);

    printf("[scene_manager] loaded manifest: %s (%u scenes)\n",
           manifest_path, sm->count);
    return true;
}

// ---------------------------------------------------------------------------
// Scene list manipulation
// ---------------------------------------------------------------------------

bool scene_manager_add_scene(SceneManager *sm, const char *scene_path) {
    if (sm == nullptr || scene_path == nullptr) return false;
    if (!ensure_capacity(sm)) return false;

    sm->scene_paths[sm->count] = strdup(scene_path);
    if (sm->scene_paths[sm->count] == nullptr) return false;

    sm->count++;
    return true;
}

void scene_manager_clear(SceneManager *sm) {
    if (sm == nullptr) return;

    for (uint32_t i = 0; i < sm->count; ++i) {
        free(sm->scene_paths[i]);
        sm->scene_paths[i] = nullptr;
    }
    sm->count         = 0;
    sm->current_index = -1;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

uint32_t scene_manager_get_count(const SceneManager *sm) {
    return sm != nullptr ? sm->count : 0;
}

int32_t scene_manager_get_current_index(const SceneManager *sm) {
    return sm != nullptr ? sm->current_index : -1;
}

const char *scene_manager_get_scene_path(const SceneManager *sm, uint32_t index) {
    if (sm == nullptr || index >= sm->count) return nullptr;
    return sm->scene_paths[index];
}

const char *scene_manager_get_current_path(const SceneManager *sm) {
    if (sm == nullptr || sm->current_index < 0 ||
        (uint32_t)sm->current_index >= sm->count) {
        return nullptr;
    }
    return sm->scene_paths[sm->current_index];
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void scene_manager_set_current_index(SceneManager *sm, int32_t index) {
    if (sm == nullptr) return;
    sm->current_index = index;
}

int32_t scene_manager_find_scene(const SceneManager *sm, const char *scene_path) {
    if (sm == nullptr || scene_path == nullptr) return -1;

    for (uint32_t i = 0; i < sm->count; ++i) {
        if (strcmp(sm->scene_paths[i], scene_path) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

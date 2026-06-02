#ifndef ENGINE_CORE_ANIM_CACHE_H
#define ENGINE_CORE_ANIM_CACHE_H

#include "animation.h"
#include "anim_controller.h"

typedef struct AnimCache AnimCache;

/// Create an animation cache. Returns nullptr on failure.
[[nodiscard]] AnimCache *anim_cache_create(void);

/// Destroy the cache and free all loaded AnimData.
void anim_cache_destroy(AnimCache *cache);

/// Load (or retrieve from cache) the AnimData at the given .anim.meta path.
/// Returns a borrowed pointer — the cache owns the AnimData.
/// Returns nullptr on failure.
AnimData *anim_cache_load(AnimCache *cache, const char *anim_path);

/// Load (or retrieve from cache) the AnimController at the given
/// .controller.meta path.
/// Returns a borrowed pointer — the cache owns the AnimController.
/// Returns nullptr on failure.
AnimController *anim_cache_load_controller(AnimCache *cache,
                                           const char *ctrl_path);

#endif // ENGINE_CORE_ANIM_CACHE_H

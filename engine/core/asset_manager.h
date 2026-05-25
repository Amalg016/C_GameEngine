#ifndef ENGINE_ASSET_MANAGER_H
#define ENGINE_ASSET_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Asset Manager — "load once, use many"
//
// Centralised cache for GPU-backed assets (textures, and later meshes).
// Assets are keyed by file path: loading the same path twice returns the same
// handle and increments a reference count.  When the ref-count drops to zero
// the GPU resources are freed.
//
// The manager itself is backend-agnostic — it delegates the actual GPU work
// to callbacks provided by the renderer backend.
// ---------------------------------------------------------------------------

typedef struct AssetManager AssetManager;

/// Opaque handle to a loaded asset.  ASSET_HANDLE_INVALID (0) means "no asset".
typedef uint32_t AssetHandle;
#define ASSET_HANDLE_INVALID 0

/// Asset types the manager knows about.
typedef enum AssetType {
    ASSET_TYPE_TEXTURE,
    // ASSET_TYPE_MESH,      // future
    // ASSET_TYPE_SHADER,    // future
} AssetType;

// --- Backend callbacks (set once by the renderer) --------------------------

/// Called when the manager needs to load a new texture from disk.
/// Must return a heap-allocated opaque pointer to GPU data (e.g. VulkanTexture*).
/// Returns nullptr on failure.
typedef void *(*AssetLoadTextureFn)(void *backend_ctx, const char *path);

/// Called when the last reference to a texture is released.
/// Must free all GPU resources AND the heap allocation returned by the load fn.
typedef void  (*AssetDestroyTextureFn)(void *backend_ctx, void *gpu_data);

typedef struct AssetManagerCallbacks {
    void                  *backend_ctx;       // e.g. VulkanContext*
    AssetLoadTextureFn     load_texture;
    AssetDestroyTextureFn  destroy_texture;
} AssetManagerCallbacks;

// --- Public API ------------------------------------------------------------

/// Create an empty asset manager.
[[nodiscard]] AssetManager *asset_manager_create(void);

/// Destroy the manager, releasing all remaining assets.
void asset_manager_destroy(AssetManager *am);

/// Set the renderer backend callbacks.  Must be called before loading assets.
void asset_manager_set_callbacks(AssetManager               *am,
                                 const AssetManagerCallbacks *cbs);

/// Load (or retrieve from cache) a texture by file path.
/// Returns ASSET_HANDLE_INVALID on failure.
AssetHandle asset_manager_load_texture(AssetManager *am,
                                       const char   *path);

/// Increment the reference count for an existing handle.
void asset_manager_add_ref(AssetManager *am, AssetHandle handle);

/// Decrement the reference count.  When it reaches zero, the GPU resources
/// are freed via the destroy callback and the cache slot is reclaimed.
void asset_manager_release(AssetManager *am, AssetHandle handle);

/// Retrieve the opaque GPU data for a handle (the pointer returned by the
/// load callback).  Returns nullptr for invalid handles.
void *asset_manager_get_data(AssetManager *am, AssetHandle handle);

/// Get the current reference count for a handle (for debugging).
uint32_t asset_manager_get_ref_count(AssetManager *am, AssetHandle handle);

/// Get the file path associated with a loaded asset handle.
/// Returns nullptr for invalid or expired handles.
const char *asset_manager_get_path(const AssetManager *am, AssetHandle handle);

#endif // ENGINE_ASSET_MANAGER_H

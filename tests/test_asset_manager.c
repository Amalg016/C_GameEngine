#include "test_framework.h"
#include "engine/core/asset_manager.h"
#include "engine/renderer/renderer.h"
#include <stdlib.h>
#include <string.h>

// Mock structure representing GPU texture data
typedef struct MockGpuTexture {
    char path[256];
    uint32_t width;
    uint32_t height;
    uint32_t magic; // to verify pointer validity
} MockGpuTexture;

static int g_mock_loads = 0;
static int g_mock_destroys = 0;

static void *mock_load_texture(void *backend_ctx, const char *path) {
    (void)backend_ctx;
    g_mock_loads++;

    MockGpuTexture *tex = malloc(sizeof(MockGpuTexture));
    if (tex == nullptr) return nullptr;

    strncpy(tex->path, path, sizeof(tex->path) - 1);
    tex->path[sizeof(tex->path) - 1] = '\0';
    tex->width = 128;
    tex->height = 256;
    tex->magic = 0xABCD1234;

    return tex;
}

static void mock_destroy_texture(void *backend_ctx, void *gpu_data) {
    (void)backend_ctx;
    g_mock_destroys++;

    MockGpuTexture *tex = (MockGpuTexture *)gpu_data;
    if (tex != nullptr) {
        ASSERT(tex->magic == 0xABCD1234);
        free(tex);
    }
}

// Mock Renderer function
static bool mock_get_texture_size(Renderer *self, void *gpu_data, uint32_t *out_w, uint32_t *out_h) {
    (void)self;
    if (gpu_data == nullptr) return false;
    MockGpuTexture *tex = (MockGpuTexture *)gpu_data;
    if (tex->magic != 0xABCD1234) {
        return false;
    }
    if (out_w) *out_w = tex->width;
    if (out_h) *out_h = tex->height;
    return true;
}

static void test_asset_manager_lifecycle(void) {
    g_mock_loads = 0;
    g_mock_destroys = 0;

    AssetManager *am = asset_manager_create();
    ASSERT(am != nullptr);

    AssetManagerCallbacks cbs = {
        .backend_ctx = nullptr,
        .load_texture = mock_load_texture,
        .destroy_texture = mock_destroy_texture
    };
    asset_manager_set_callbacks(am, &cbs);

    // Initial load
    AssetHandle h1 = asset_manager_load_texture(am, "textures/player.png");
    ASSERT(h1 != ASSET_HANDLE_INVALID);
    ASSERT(g_mock_loads == 1);
    ASSERT(g_mock_destroys == 0);

    // Verify metadata
    ASSERT_STR_EQ(asset_manager_get_path(am, h1), "textures/player.png");
    ASSERT(asset_manager_get_ref_count(am, h1) == 1);

    // Verify GPU data exists and is correct
    MockGpuTexture *tex1 = (MockGpuTexture *)asset_manager_get_data(am, h1);
    ASSERT(tex1 != nullptr);
    ASSERT(tex1->magic == 0xABCD1234);
    ASSERT_STR_EQ(tex1->path, "textures/player.png");

    // Load again (should fetch from cache)
    AssetHandle h2 = asset_manager_load_texture(am, "textures/player.png");
    ASSERT(h2 == h1);
    ASSERT(g_mock_loads == 1); // No new backend loads
    ASSERT(asset_manager_get_ref_count(am, h1) == 2);

    // Verify handle lookup
    AssetHandle looked_up = asset_manager_get_handle(am, "textures/player.png");
    ASSERT(looked_up == h1);

    // Add manual reference
    asset_manager_add_ref(am, h1);
    ASSERT(asset_manager_get_ref_count(am, h1) == 3);

    // Release references
    asset_manager_release(am, h1);
    ASSERT(asset_manager_get_ref_count(am, h1) == 2);
    ASSERT(g_mock_destroys == 0);

    asset_manager_release(am, h1);
    ASSERT(asset_manager_get_ref_count(am, h1) == 1);
    ASSERT(g_mock_destroys == 0);

    // Final release should trigger backend destroy
    asset_manager_release(am, h1);
    ASSERT(g_mock_destroys == 1);

    // Lookup should now fail
    ASSERT(asset_manager_get_handle(am, "textures/player.png") == ASSET_HANDLE_INVALID);
    ASSERT(asset_manager_get_path(am, h1) == nullptr);
    ASSERT(asset_manager_get_data(am, h1) == nullptr);

    asset_manager_destroy(am);
}

static void test_asset_manager_renderer_query(void) {
    g_mock_loads = 0;
    g_mock_destroys = 0;

    AssetManager *am = asset_manager_create();
    AssetManagerCallbacks cbs = {
        .backend_ctx = nullptr,
        .load_texture = mock_load_texture,
        .destroy_texture = mock_destroy_texture
    };
    asset_manager_set_callbacks(am, &cbs);

    AssetHandle h = asset_manager_load_texture(am, "textures/enemy.png");

    // Setup mock renderer
    Renderer mock_renderer = {};
    mock_renderer.api.get_texture_size = mock_get_texture_size;
    asset_manager_set_renderer(am, &mock_renderer);

    uint32_t w = 0, h_val = 0;
    bool success = asset_manager_get_texture_size(am, h, &w, &h_val);
    ASSERT(success);
    ASSERT(w == 128);
    ASSERT(h_val == 256);

    asset_manager_release(am, h);
    asset_manager_destroy(am);
}

void test_asset_manager_run(void) {
    printf(COLOR_BLUE COLOR_BOLD "\n--- Running Asset Manager Tests ---" COLOR_RESET "\n");
    RUN_TEST(test_asset_manager_lifecycle);
    RUN_TEST(test_asset_manager_renderer_query);
}

// Global linker implementation to satisfy asset_manager.c dependency
bool renderer_get_texture_size(Renderer *r, void *gpu_data, uint32_t *out_w, uint32_t *out_h) {
    if (r == nullptr || r->api.get_texture_size == nullptr) return false;
    return r->api.get_texture_size(r, gpu_data, out_w, out_h);
}


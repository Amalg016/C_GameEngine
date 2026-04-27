#include "../engine/core/engine.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== GameEngine starting ===\n");

    EngineConfig config = {
        .title   = "GameEngine",
        .width   = 800,
        .height  = 600,
#ifdef USE_OPENGL
        .backend = RENDERER_BACKEND_OPENGL,
#else
        .backend = RENDERER_BACKEND_VULKAN,
#endif
    };

    Engine *engine = engine_create(&config);
    if (engine == nullptr) {
        fprintf(stderr, "failed to create engine\n");
        return EXIT_FAILURE;
    }

    // Initialise the renderer — GPU is now ready for asset loading.
    if (!engine_init(engine)) {
        fprintf(stderr, "failed to init engine\n");
        engine_destroy(engine);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Demonstrate the asset manager: "load once, use many"
    // -----------------------------------------------------------------------

    AssetManager *am = engine_get_asset_manager(engine);
    Renderer     *r  = engine_get_renderer(engine);

    // First load — this actually loads the PNG from disk into GPU memory.
    AssetHandle tex_a = asset_manager_load_texture(am, "assets/images/file.png");
    if (tex_a == ASSET_HANDLE_INVALID) {
        fprintf(stderr, "failed to load texture\n");
    }

    // Second load of the SAME path — cache hit, no GPU upload, ref bumped.
    AssetHandle tex_b = asset_manager_load_texture(am, "assets/images/file.png");

    printf("[demo] tex_a = %u, tex_b = %u (same handle: %s)\n",
           tex_a, tex_b, tex_a == tex_b ? "YES ✓" : "NO ✗");
    printf("[demo] ref_count = %u (expected: 2)\n",
           asset_manager_get_ref_count(am, tex_a));

    // Bind the texture so the renderer uses it for drawing.
    void *gpu_data = asset_manager_get_data(am, tex_a);
    renderer_bind_texture(r, gpu_data);

    // Release one reference — texture stays alive (ref_count → 1).
    asset_manager_release(am, tex_b);
    printf("[demo] after one release: ref_count = %u (expected: 1)\n",
           asset_manager_get_ref_count(am, tex_a));

    // -----------------------------------------------------------------------
    // Run the main loop — texture is displayed on the quad.
    // -----------------------------------------------------------------------

    engine_run(engine);

    // Release the last reference — GPU resources freed.
    asset_manager_release(am, tex_a);

    engine_destroy(engine);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}

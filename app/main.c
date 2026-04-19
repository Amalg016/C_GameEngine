#include "../engine/platform/platform.h"
#include "../engine/renderer/renderer.h"
#include "../engine/core/engine.h"

// Backend selection at compile time.
#ifdef USE_OPENGL
#   include "../engine/renderer/opengl/opengl_renderer.h"
#else
#   include "../engine/renderer/vulkan/vulkan_renderer.h"
#endif

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== GameEngine starting ===\n");

    Platform *platform = platform_create("GameEngine", 800, 600);
    if (platform == nullptr) {
        fprintf(stderr, "failed to create platform\n");
        return EXIT_FAILURE;
    }

#ifdef USE_OPENGL
    Renderer renderer = opengl_renderer_create(platform);
#else
    Renderer renderer = vulkan_renderer_create(platform);
#endif

    engine_run(&renderer, platform);

#ifdef USE_OPENGL
    opengl_renderer_destroy(&renderer);
#else
    vulkan_renderer_destroy(&renderer);
#endif

    platform_destroy(platform);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}

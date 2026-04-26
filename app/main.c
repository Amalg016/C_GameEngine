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

    engine_run(engine);
    engine_destroy(engine);

    printf("=== GameEngine shut down cleanly ===\n");
    return EXIT_SUCCESS;
}

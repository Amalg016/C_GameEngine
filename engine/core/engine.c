#include "engine.h"
#include "../renderer/renderer.h"
#include "../platform/platform.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// engine_run — the heartbeat of the application.
//
// Completely backend-agnostic: it only talks through the Renderer and
// Platform abstractions.
// ---------------------------------------------------------------------------

void engine_run(Renderer *renderer, Platform *platform) {
    if (renderer == nullptr || platform == nullptr) {
        fprintf(stderr, "[engine] renderer or platform is null — aborting\n");
        return;
    }

    if (!renderer_init(renderer)) {
        fprintf(stderr, "[engine] renderer initialisation failed\n");
        return;
    }

    printf("[engine] entering main loop\n");

    while (!platform_should_close(platform)) {
        platform_poll_events(platform);

        if (renderer_begin_frame(renderer)) {
            renderer_draw_triangle(renderer);
            renderer_end_frame(renderer);
        }
    }

    printf("[engine] main loop finished\n");

    renderer_shutdown(renderer);
}

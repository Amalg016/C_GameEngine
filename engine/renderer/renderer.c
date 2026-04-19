#include "renderer.h"

#include <stddef.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Convenience dispatchers — forward every call through the vtable.
// ---------------------------------------------------------------------------

bool renderer_init(Renderer *r) {
    if (r == nullptr || r->api.init == nullptr) {
        fprintf(stderr, "[renderer] init: null renderer or missing backend\n");
        return false;
    }
    return r->api.init(r);
}

void renderer_shutdown(Renderer *r) {
    if (r != nullptr && r->api.shutdown != nullptr) {
        r->api.shutdown(r);
    }
}

bool renderer_begin_frame(Renderer *r) {
    if (r == nullptr || r->api.begin_frame == nullptr) return false;
    return r->api.begin_frame(r);
}

void renderer_end_frame(Renderer *r) {
    if (r != nullptr && r->api.end_frame != nullptr) {
        r->api.end_frame(r);
    }
}

void renderer_draw_triangle(Renderer *r) {
    if (r != nullptr && r->api.draw_triangle != nullptr) {
        r->api.draw_triangle(r);
    }
}

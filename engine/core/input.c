#include "input.h"

#include <string.h>

// GLFW action constants (avoid including GLFW in engine/core/).
#define GLFW_PRESS   1
#define GLFW_RELEASE 0

void input_init(Input *input) {
    memset(input, 0, sizeof(Input));
    input->_first_mouse = true;
}

void input_begin_frame(Input *input) {
    // Clear edge states — they are only valid for one frame.
    memset(input->keys_pressed,     0, sizeof(input->keys_pressed));
    memset(input->keys_released,    0, sizeof(input->keys_released));
    memset(input->buttons_pressed,  0, sizeof(input->buttons_pressed));
    memset(input->buttons_released, 0, sizeof(input->buttons_released));

    // Compute mouse delta from the previous frame.
    if (input->_first_mouse) {
        input->mouse_dx = 0.0;
        input->mouse_dy = 0.0;
    } else {
        input->mouse_dx = input->mouse_x - input->_prev_mouse_x;
        input->mouse_dy = input->mouse_y - input->_prev_mouse_y;
    }
    input->_prev_mouse_x = input->mouse_x;
    input->_prev_mouse_y = input->mouse_y;

    // Clear scroll accumulator (scroll is per-frame, not held).
    input->scroll_dx = 0.0;
    input->scroll_dy = 0.0;
}

void input_on_key(Input *input, int key, int action) {
    if (key < 0 || key >= INPUT_MAX_KEYS) return;

    if (action == GLFW_PRESS) {
        if (!input->keys_down[key]) {
            input->keys_pressed[key] = true;
        }
        input->keys_down[key] = true;
    } else if (action == GLFW_RELEASE) {
        if (input->keys_down[key]) {
            input->keys_released[key] = true;
        }
        input->keys_down[key] = false;
    }
    // GLFW_REPEAT is ignored — keys_down remains true.
}

void input_on_mouse_button(Input *input, int button, int action) {
    if (button < 0 || button >= INPUT_MAX_BUTTONS) return;

    if (action == GLFW_PRESS) {
        if (!input->buttons_down[button]) {
            input->buttons_pressed[button] = true;
        }
        input->buttons_down[button] = true;
    } else if (action == GLFW_RELEASE) {
        if (input->buttons_down[button]) {
            input->buttons_released[button] = true;
        }
        input->buttons_down[button] = false;
    }
}

void input_on_cursor_pos(Input *input, double x, double y) {
    input->mouse_x = x;
    input->mouse_y = y;
    input->_first_mouse = false;
}

void input_on_scroll(Input *input, double xoffset, double yoffset) {
    input->scroll_dx += xoffset;
    input->scroll_dy += yoffset;
}

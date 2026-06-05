#ifndef ENGINE_CORE_INPUT_H
#define ENGINE_CORE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Input — per-frame keyboard and mouse state.
//
// Design:
//   - Polled, not event-driven.  Systems query input state during update.
//   - Two states per key: "down" (held) and "pressed" / "released" (edges).
//   - input_begin_frame() must be called at the start of each frame,
//     AFTER platform_poll_events().  Edge states are valid until the
//     next input_begin_frame() call.
//
// Key codes mirror GLFW key constants (GLFW_KEY_A == 65, etc.).
// Mouse buttons mirror GLFW (0 = left, 1 = right, 2 = middle).
// ---------------------------------------------------------------------------

// GLFW supports keys up to GLFW_KEY_LAST (348).
#define INPUT_MAX_KEYS    350
#define INPUT_MAX_BUTTONS 8

/// Key codes — matches GLFW values so no translation is needed.
/// Only common keys are named here; the full range (0–349) is supported.
typedef enum Key {
    KEY_SPACE           = 32,
    KEY_APOSTROPHE      = 39,
    KEY_COMMA           = 44,
    KEY_MINUS           = 45,
    KEY_PERIOD          = 46,
    KEY_SLASH           = 47,
    KEY_0 = 48, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_SEMICOLON       = 59,
    KEY_EQUAL           = 61,
    KEY_A = 65, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
    KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
    KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U,
    KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_ESCAPE          = 256,
    KEY_ENTER           = 257,
    KEY_TAB             = 258,
    KEY_BACKSPACE       = 259,
    KEY_INSERT           = 260,
    KEY_DELETE           = 261,
    KEY_RIGHT           = 262,
    KEY_LEFT            = 263,
    KEY_DOWN            = 264,
    KEY_UP              = 265,
    KEY_PAGE_UP         = 266,
    KEY_PAGE_DOWN       = 267,
    KEY_HOME            = 268,
    KEY_END             = 269,
    KEY_CAPS_LOCK       = 280,
    KEY_F1 = 290, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LEFT_SHIFT      = 340,
    KEY_LEFT_CONTROL    = 341,
    KEY_LEFT_ALT        = 342,
    KEY_LEFT_SUPER      = 343,
    KEY_RIGHT_SHIFT     = 344,
    KEY_RIGHT_CONTROL   = 345,
    KEY_RIGHT_ALT       = 346,
    KEY_RIGHT_SUPER     = 347,
} Key;

typedef enum MouseButton {
    MOUSE_BUTTON_LEFT   = 0,
    MOUSE_BUTTON_RIGHT  = 1,
    MOUSE_BUTTON_MIDDLE = 2,
} MouseButton;

#define INPUT_GAMEPAD_BUTTON_A               0
#define INPUT_GAMEPAD_BUTTON_B               1
#define INPUT_GAMEPAD_BUTTON_X               2
#define INPUT_GAMEPAD_BUTTON_Y               3
#define INPUT_GAMEPAD_BUTTON_LEFT_BUMPER     4
#define INPUT_GAMEPAD_BUTTON_RIGHT_BUMPER    5
#define INPUT_GAMEPAD_BUTTON_BACK            6
#define INPUT_GAMEPAD_BUTTON_START           7
#define INPUT_GAMEPAD_BUTTON_GUIDE           8
#define INPUT_GAMEPAD_BUTTON_LEFT_THUMB      9
#define INPUT_GAMEPAD_BUTTON_RIGHT_THUMB     10
#define INPUT_GAMEPAD_BUTTON_DPAD_UP         11
#define INPUT_GAMEPAD_BUTTON_DPAD_RIGHT      12
#define INPUT_GAMEPAD_BUTTON_DPAD_DOWN       13
#define INPUT_GAMEPAD_BUTTON_DPAD_LEFT       14
#define INPUT_GAMEPAD_BUTTON_LAST            INPUT_GAMEPAD_BUTTON_DPAD_LEFT
#define INPUT_GAMEPAD_BUTTON_COUNT           15

#define INPUT_GAMEPAD_AXIS_LEFT_X            0
#define INPUT_GAMEPAD_AXIS_LEFT_Y            1
#define INPUT_GAMEPAD_AXIS_RIGHT_X           2
#define INPUT_GAMEPAD_AXIS_RIGHT_Y           3
#define INPUT_GAMEPAD_AXIS_LEFT_TRIGGER      4
#define INPUT_GAMEPAD_AXIS_RIGHT_TRIGGER     5
#define INPUT_GAMEPAD_AXIS_LAST              INPUT_GAMEPAD_AXIS_RIGHT_TRIGGER
#define INPUT_GAMEPAD_AXIS_COUNT             6

/// Input state — owned by the engine, updated each frame.
typedef struct Input {
    // Keyboard: current frame state.
    bool keys_down[INPUT_MAX_KEYS];         // true while held
    bool keys_pressed[INPUT_MAX_KEYS];      // true for ONE frame on press
    bool keys_released[INPUT_MAX_KEYS];     // true for ONE frame on release

    // Mouse buttons: current frame state.
    bool buttons_down[INPUT_MAX_BUTTONS];
    bool buttons_pressed[INPUT_MAX_BUTTONS];
    bool buttons_released[INPUT_MAX_BUTTONS];

    // Mouse position (in screen coordinates).
    double mouse_x;
    double mouse_y;

    // Mouse delta since last frame.
    double mouse_dx;
    double mouse_dy;

    // Scroll wheel delta this frame.
    double scroll_dx;
    double scroll_dy;

    // Gamepad state (joystick 1)
    bool  gamepad_connected;
    bool  gamepad_buttons[INPUT_GAMEPAD_BUTTON_COUNT];
    bool  gamepad_buttons_pressed[INPUT_GAMEPAD_BUTTON_COUNT];
    bool  gamepad_buttons_released[INPUT_GAMEPAD_BUTTON_COUNT];
    float gamepad_axes[INPUT_GAMEPAD_AXIS_COUNT];

    // Internal: previous frame mouse position for delta calculation.
    double _prev_mouse_x;
    double _prev_mouse_y;
    bool   _first_mouse;        // true until first mouse move event
    bool   game_input_active;   // true if gameplay keyboard input is accepted
} Input;

/// Initialise the input struct to default state.
void input_init(Input *input);

/// Call at the start of each frame AFTER platform_poll_events().
/// Clears edge states (pressed/released) and computes mouse deltas.
void input_begin_frame(Input *input);

/// Called by GLFW key callback — updates key state.
void input_on_key(Input *input, int key, int action);

/// Called by GLFW mouse button callback — updates button state.
void input_on_mouse_button(Input *input, int button, int action);

/// Called by GLFW cursor position callback — updates mouse position.
void input_on_cursor_pos(Input *input, double x, double y);

/// Called by GLFW scroll callback — accumulates scroll delta.
void input_on_scroll(Input *input, double xoffset, double yoffset);

// --- Query API (convenience, all inlined) ----------------------------------

/// True while the key is held down.
static inline bool input_key_down(const Input *input, int key) {
    return key >= 0 && key < INPUT_MAX_KEYS && input->keys_down[key];
}

/// True for exactly one frame when the key is first pressed.
static inline bool input_key_pressed(const Input *input, int key) {
    return key >= 0 && key < INPUT_MAX_KEYS && input->keys_pressed[key];
}

/// True for exactly one frame when the key is released.
static inline bool input_key_released(const Input *input, int key) {
    return key >= 0 && key < INPUT_MAX_KEYS && input->keys_released[key];
}

/// True while the mouse button is held.
static inline bool input_button_down(const Input *input, int button) {
    return button >= 0 && button < INPUT_MAX_BUTTONS
        && input->buttons_down[button];
}

/// True for one frame when the button is first pressed.
static inline bool input_button_pressed(const Input *input, int button) {
    return button >= 0 && button < INPUT_MAX_BUTTONS
        && input->buttons_pressed[button];
}

/// True for one frame when the button is released.
static inline bool input_button_released(const Input *input, int button) {
    return button >= 0 && button < INPUT_MAX_BUTTONS
        && input->buttons_released[button];
}

/// True while the gamepad button is held down.
static inline bool input_gamepad_down(const Input *input, int button) {
    return input->gamepad_connected && button >= 0 && button < INPUT_GAMEPAD_BUTTON_COUNT && input->gamepad_buttons[button];
}

/// True for exactly one frame when the gamepad button is first pressed.
static inline bool input_gamepad_pressed(const Input *input, int button) {
    return input->gamepad_connected && button >= 0 && button < INPUT_GAMEPAD_BUTTON_COUNT && input->gamepad_buttons_pressed[button];
}

/// True for exactly one frame when the gamepad button is released.
static inline bool input_gamepad_released(const Input *input, int button) {
    return input->gamepad_connected && button >= 0 && button < INPUT_GAMEPAD_BUTTON_COUNT && input->gamepad_buttons_released[button];
}

/// Retrieve the analog axis value (-1.0 to 1.0). Returns 0.0 if not connected or invalid axis.
static inline float input_gamepad_axis(const Input *input, int axis) {
    if (!input->gamepad_connected || axis < 0 || axis >= INPUT_GAMEPAD_AXIS_COUNT) return 0.0f;
    return input->gamepad_axes[axis];
}

static inline void input_set_game_active(Input *input, bool active) {
    if (input != nullptr) {
        input->game_input_active = active;
    }
}

static inline bool input_is_game_active(const Input *input) {
    return input != nullptr && input->game_input_active;
}

#endif // ENGINE_CORE_INPUT_H

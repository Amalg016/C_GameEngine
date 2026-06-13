#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "../core/input.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Platform internals — GLFW stays confined to this translation unit.
// ---------------------------------------------------------------------------

/// Wrapper stored in the GLFW window user pointer.
/// Multiple subsystems (input, Vulkan resize callback) need the user pointer,
/// so we aggregate them here instead of fighting over the single slot.
typedef struct PlatformUserData {
  Input *input;       // engine input state (set by platform_set_input)
  void *backend_data; // renderer backend context (e.g. VulkanContext*)
} PlatformUserData;

struct Platform {
  GLFWwindow *window;
  uint32_t width;
  uint32_t height;
  PlatformUserData user_data;
};

// ---------------------------------------------------------------------------
// GLFW input callbacks — forward events to the Input system.
// ---------------------------------------------------------------------------

static void glfw_key_callback(GLFWwindow *win, int key, int /*scancode*/,
                              int action, int /*mods*/) {
  PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
  if (ud != nullptr && ud->input != nullptr)
    input_on_key(ud->input, key, action);
}

static void glfw_mouse_button_callback(GLFWwindow *win, int button, int action,
                                       int /*mods*/) {
  PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
  if (ud != nullptr && ud->input != nullptr)
    input_on_mouse_button(ud->input, button, action);
}

static void glfw_cursor_pos_callback(GLFWwindow *win, double x, double y) {
  PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
  if (ud != nullptr && ud->input != nullptr)
    input_on_cursor_pos(ud->input, x, y);
}

static void glfw_scroll_callback(GLFWwindow *win, double xoff, double yoff) {
  PlatformUserData *ud = (PlatformUserData *)glfwGetWindowUserPointer(win);
  if (ud != nullptr && ud->input != nullptr)
    input_on_scroll(ud->input, xoff, yoff);
}

static void platform_load_gamepad_mappings(void) {
  const char *paths[] = {"gamecontrollerdb.txt", "third_party/gamecontrollerdb.txt"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
    FILE *f = fopen(paths[i], "rb");
    if (f == nullptr)
      continue;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
      fclose(f);
      continue;
    }

    char *buf = malloc((size_t)len + 1);
    if (buf == nullptr) {
      fclose(f);
      continue;
    }

    size_t read_bytes = fread(buf, 1, (size_t)len, f);
    buf[read_bytes] = '\0';
    fclose(f);

    if (glfwUpdateGamepadMappings(buf)) {
      printf("[platform] loaded gamepad mappings from: %s\n", paths[i]);
    } else {
      fprintf(stderr, "[platform] failed to parse/apply gamepad mappings from: %s\n", paths[i]);
    }
    free(buf);
    break;
  }
}

// ---- lifecycle ------------------------------------------------------------

Platform *platform_create(const char *title, uint32_t width, uint32_t height) {
  if (!glfwInit()) {
    fprintf(stderr, "[platform] failed to initialise GLFW\n");
    return nullptr;
  }

  platform_load_gamepad_mappings();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no OpenGL context
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow *win =
      glfwCreateWindow((int)width, (int)height, title, nullptr, nullptr);
  if (win == nullptr) {
    fprintf(stderr, "[platform] failed to create GLFW window\n");
    glfwTerminate();
    return nullptr;
  }

  Platform *p = malloc(sizeof(Platform));
  if (p == nullptr) {
    glfwDestroyWindow(win);
    glfwTerminate();
    return nullptr;
  }

  *p = (Platform){
      .window = win,
      .width = width,
      .height = height,
      .user_data = {},
  };

  // Point the GLFW user pointer at our wrapper struct.
  glfwSetWindowUserPointer(win, &p->user_data);

  return p;
}

void platform_destroy(Platform *p) {
  if (p == nullptr)
    return;
  if (p->window != nullptr)
    glfwDestroyWindow(p->window);
  glfwTerminate();
  free(p);
}

// ---- input wiring ---------------------------------------------------------

void platform_set_input(Platform *p, Input *input) {
  if (p == nullptr)
    return;

  p->user_data.input = input;

  glfwSetKeyCallback(p->window, glfw_key_callback);
  glfwSetMouseButtonCallback(p->window, glfw_mouse_button_callback);
  glfwSetCursorPosCallback(p->window, glfw_cursor_pos_callback);
  glfwSetScrollCallback(p->window, glfw_scroll_callback);

  printf("[platform] input callbacks registered\n");
}

// ---- backend data wiring --------------------------------------------------

void platform_set_backend_data(Platform *p, void *data) {
  if (p == nullptr)
    return;
  p->user_data.backend_data = data;
}

void *platform_get_backend_data_from_window(void *glfw_window) {
  if (glfw_window == nullptr)
    return nullptr;
  PlatformUserData *ud =
      (PlatformUserData *)glfwGetWindowUserPointer((GLFWwindow *)glfw_window);
  return ud != nullptr ? ud->backend_data : nullptr;
}

void platform_set_backend_data_from_window(void *glfw_window, void *data) {
  if (glfw_window == nullptr)
    return;
  PlatformUserData *ud =
      (PlatformUserData *)glfwGetWindowUserPointer((GLFWwindow *)glfw_window);
  if (ud != nullptr)
    ud->backend_data = data;
}

// ---- queries --------------------------------------------------------------

bool platform_should_close(const Platform *p) {
  return p == nullptr || glfwWindowShouldClose(p->window);
}

void platform_poll_events(const Platform *p) {
  (void)p;
  glfwPollEvents();
}

const char **platform_get_vulkan_extensions(uint32_t *count) {
  return glfwGetRequiredInstanceExtensions(count);
}

void *platform_get_window_handle(const Platform *p) {
  return p != nullptr ? p->window : nullptr;
}

void platform_get_framebuffer_size(const Platform *p, uint32_t *width,
                                   uint32_t *height) {
  if (p == nullptr)
    return;
  int w, h;
  glfwGetFramebufferSize(p->window, &w, &h);
  if (width != nullptr)
    *width = (uint32_t)w;
  if (height != nullptr)
    *height = (uint32_t)h;
}

// ---- timing ---------------------------------------------------------------

double platform_get_time(void) { return glfwGetTime(); }

void platform_poll_gamepad(const Platform *p, Input *input) {
  if (p == nullptr || input == nullptr)
    return;

  GLFWgamepadstate state;
  int active_jid = -1;

  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    if (glfwJoystickPresent(jid)) {
      if (glfwGetGamepadState(jid, &state)) {
        active_jid = jid;
        break;
      }
    }
  }

  // Print a warning for any joystick that is connected but has no gamepad mapping
  // to help users diagnose why their controller isn't responding.
  static bool warned_joysticks[GLFW_JOYSTICK_LAST + 1] = {};
  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    if (glfwJoystickPresent(jid)) {
      if (!glfwJoystickIsGamepad(jid)) {
        if (!warned_joysticks[jid]) {
          printf("[platform] WARNING: Joystick %d (%s) is connected but has no gamepad mapping.\n",
                 jid + 1, glfwGetJoystickName(jid));
          warned_joysticks[jid] = true;
        }
      } else {
        warned_joysticks[jid] = false;
      }
    } else {
      warned_joysticks[jid] = false;
    }
  }

  if (active_jid != -1) {
    if (!input->gamepad_connected) {
      printf("[platform] gamepad connected: %s (joystick %d)\n",
             glfwGetGamepadName(active_jid), active_jid + 1);
    }
    input->gamepad_connected = true;
    for (int i = 0; i < INPUT_GAMEPAD_BUTTON_COUNT; ++i) {
      bool is_down = (state.buttons[i] == GLFW_PRESS);
      bool was_down = input->gamepad_buttons[i];
      input->gamepad_buttons_pressed[i] = (is_down && !was_down);
      input->gamepad_buttons_released[i] = (!is_down && was_down);
      input->gamepad_buttons[i] = is_down;
    }
    for (int i = 0; i < INPUT_GAMEPAD_AXIS_COUNT; ++i) {
      input->gamepad_axes[i] = state.axes[i];
    }
  } else {
    if (input->gamepad_connected) {
      printf("[platform] gamepad disconnected\n");
      memset(input->gamepad_buttons, 0, sizeof(input->gamepad_buttons));
      memset(input->gamepad_buttons_pressed, 0,
             sizeof(input->gamepad_buttons_pressed));
      memset(input->gamepad_buttons_released, 0,
             sizeof(input->gamepad_buttons_released));
      memset(input->gamepad_axes, 0, sizeof(input->gamepad_axes));
      input->gamepad_connected = false;
    }
  }
}

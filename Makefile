# ============================================================================
# GameEngine — Makefile
#
#   make               Build editor (Vulkan + ImGui via cimgui)
#   make editor         Same as above
#   make vulkan         Build desktop Vulkan binary (runtime — no editor)
#   make opengl         Build desktop with OpenGL stub
#   make web            Build for web via Emscripten (OpenGL backend)
#   make shaders        Compile GLSL → SPIR-V
#   make clean          Remove build artifacts
# ============================================================================

CC        := gcc
CFLAGS    := -std=c23 -Wall -Wextra -Wpedantic -Ithird_party \
              -DIMGUI_USE_WCHAR32 \
              $(shell pkg-config --cflags lua5.4)
LDFLAGS   :=

CXX       := g++
CXXFLAGS  := -std=c++11 -O2 -Wall -Wextra -DIMGUI_USE_WCHAR32 \
              -Ithird_party/cimgui \
              -Ithird_party/cimgui/imgui \
              -Ithird_party/cimgui/imgui/backends

# --- source files ----------------------------------------------------------

SRC_ENGINE := engine/renderer/renderer.c \
              engine/core/engine.c \
              engine/core/asset_manager.c \
              engine/core/clock.c \
              engine/core/input.c \
              engine/core/scene.c \
              engine/core/scene_manager.c \
              engine/core/sprite.c \
              engine/core/sprite_meta.c \
              engine/core/animation.c \
              engine/core/anim_cache.c \
              engine/core/anim_controller.c \
              engine/core/platformer_controller.c \
              engine/core/ecs/component_pool.c \
              engine/core/ecs/world.c \
              engine/core/ecs/hierarchy.c \
              engine/core/ecs/camera.c \
              engine/core/scripting/lua_host.c \
              engine/core/scripting/lua_bindings.c \
              engine/platform/platform.c

SRC_VULKAN := engine/renderer/vulkan/vulkan_renderer.c \
              engine/renderer/vulkan/vulkan_device.c \
              engine/renderer/vulkan/vulkan_swapchain.c \
              engine/renderer/vulkan/vulkan_pipeline.c \
              engine/renderer/vulkan/vulkan_buffer.c \
              engine/renderer/vulkan/vulkan_texture.c

SRC_OPENGL := engine/renderer/opengl/opengl_renderer.c

SRC_THIRD_PARTY := third_party/stb_image.c \
                   third_party/cJSON.c

SRC_APP    := app/main.c

# --- Editor sources (compiled only for the editor target) ------------------

SRC_EDITOR := engine/editor/editor.c \
              engine/editor/ui/imgui_layer.c \
              engine/editor/panels/panel_hierarchy.c \
              engine/editor/panels/panel_inspector.c \
              engine/editor/panels/panel_content_browser.c \
              engine/editor/panels/panel_console.c \
              engine/editor/panels/panel_game_view.c \
              engine/editor/panels/panel_scene_view.c \
              engine/editor/panels/panel_sprite_editor.c \
              engine/editor/panels/panel_animation_editor.c \
              engine/editor/panels/panel_controller_editor.c \
              engine/editor/panels/panel_scene_list.c

# ImGui C++ sources from the cimgui submodule.
SRC_IMGUI  := third_party/cimgui/cimgui.cpp \
              third_party/cimgui/imgui/imgui.cpp \
              third_party/cimgui/imgui/imgui_demo.cpp \
              third_party/cimgui/imgui/imgui_draw.cpp \
              third_party/cimgui/imgui/imgui_tables.cpp \
              third_party/cimgui/imgui/imgui_widgets.cpp \
              third_party/cimgui/imgui/backends/imgui_impl_glfw.cpp \
              third_party/cimgui/imgui/backends/imgui_impl_vulkan.cpp

# Engine C++ bridge (compiled as C++, called from C).
SRC_BRIDGE := engine/editor/ui/imgui_vulkan_bridge.cpp

# --- targets ---------------------------------------------------------------

BUILD_DIR := build

.PHONY: all vulkan opengl editor web shaders clean test

all: editor

# ---- Vulkan (default) — runtime build, NO editor/ImGui --------------------

VULKAN_SRCS := $(SRC_APP) $(SRC_ENGINE) $(SRC_VULKAN) $(SRC_THIRD_PARTY)
VULKAN_BIN  := engine_vulkan

vulkan: shaders $(VULKAN_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(VULKAN_BIN) $(VULKAN_SRCS) \
		$$(pkg-config --cflags --libs glfw3) -lvulkan \
		$$(pkg-config --libs lua5.4) -lm
	@echo "[build] $(VULKAN_BIN) ready (runtime — no editor)"

# ---- OpenGL stub -----------------------------------------------------------

OPENGL_SRCS := $(SRC_APP) $(SRC_ENGINE) $(SRC_OPENGL) $(SRC_THIRD_PARTY)
OPENGL_BIN  := engine_opengl

opengl: $(OPENGL_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DUSE_OPENGL -o $(OPENGL_BIN) $(OPENGL_SRCS) \
		$$(pkg-config --cflags --libs glfw3) \
		$$(pkg-config --libs lua5.4) -lm
	@echo "[build] $(OPENGL_BIN) ready"

# ---- Editor (Vulkan + ImGui via cimgui) ------------------------------------
#
# Build steps:
#   1. Compile ImGui C++ sources (.cpp → .o) with g++.
#   2. Compile the C++ bridge file (.cpp → .o) with g++.
#   3. Compile and link everything (engine C + editor C + ImGui .o files).
#      The -DEDITOR_BUILD flag gates all editor code at compile time.
#      The -lstdc++ flag links the C++ standard library for ImGui.

IMGUI_OBJ_DIR := $(BUILD_DIR)/imgui
EDITOR_BIN    := engine_editor

IMGUI_OBJS    := $(IMGUI_OBJ_DIR)/cimgui.o \
                 $(IMGUI_OBJ_DIR)/imgui.o \
                 $(IMGUI_OBJ_DIR)/imgui_demo.o \
                 $(IMGUI_OBJ_DIR)/imgui_draw.o \
                 $(IMGUI_OBJ_DIR)/imgui_tables.o \
                 $(IMGUI_OBJ_DIR)/imgui_widgets.o \
                 $(IMGUI_OBJ_DIR)/imgui_impl_glfw.o \
                 $(IMGUI_OBJ_DIR)/imgui_impl_vulkan.o \
                 $(IMGUI_OBJ_DIR)/imgui_vulkan_bridge.o

$(IMGUI_OBJ_DIR)/%.o: third_party/cimgui/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IMGUI_OBJ_DIR)/%.o: third_party/cimgui/imgui/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IMGUI_OBJ_DIR)/imgui_impl_glfw.o: third_party/cimgui/imgui/backends/imgui_impl_glfw.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $$(pkg-config --cflags glfw3) -c $< -o $@

$(IMGUI_OBJ_DIR)/imgui_impl_vulkan.o: third_party/cimgui/imgui/backends/imgui_impl_vulkan.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IMGUI_OBJ_DIR)/imgui_vulkan_bridge.o: engine/editor/ui/imgui_vulkan_bridge.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DEDITOR_BUILD $$(pkg-config --cflags glfw3) -c $< -o $@

editor: shaders $(IMGUI_OBJS)
	@echo "[editor] Compiling engine + editor C sources..."
	$(CC) $(CFLAGS) -DEDITOR_BUILD \
		-Ithird_party/cimgui -Ithird_party/cimgui/imgui \
		-o $(EDITOR_BIN) \
		$(SRC_APP) $(SRC_ENGINE) $(SRC_VULKAN) $(SRC_EDITOR) $(SRC_THIRD_PARTY) \
		$(IMGUI_OBJS) \
		$$(pkg-config --cflags --libs glfw3) -lvulkan \
		$$(pkg-config --libs lua5.4) -lm -lstdc++
	@echo "[build] $(EDITOR_BIN) ready (editor — docking ImGui)"

# ---- Web (Emscripten) ------------------------------------------------------

EMCC       := emcc
WEB_SRCS   := $(SRC_APP) $(SRC_ENGINE) $(SRC_OPENGL) $(SRC_THIRD_PARTY)
WEB_OUT    := engine_web.html

web: $(WEB_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -DUSE_OPENGL \
		-s USE_GLFW=3 -s FULL_ES3=1 -s WASM=1 \
		-o $(WEB_OUT) $(WEB_SRCS)
	@echo "[build] $(WEB_OUT) ready"

# ---- Shaders ---------------------------------------------------------------

GLSLC     := glslc
VERT_SRC  := $(wildcard shaders/*.vert)
FRAG_SRC  := $(wildcard shaders/*.frag)
SPV_FILES := $(VERT_SRC:=.spv) $(FRAG_SRC:=.spv)

shaders: $(SPV_FILES)

shaders/%.vert.spv: shaders/%.vert
	$(GLSLC) $< -o $@
	@echo "[shader] $< → $@"

shaders/%.frag.spv: shaders/%.frag
	$(GLSLC) $< -o $@
	@echo "[shader] $< → $@"

# ---- Tests -----------------------------------------------------------------

SRC_TEST    := tests/main.c \
               tests/test_math.c \
               tests/test_ecs.c \
               tests/test_asset_manager.c \
               tests/test_clock.c \
               engine/core/ecs/component_pool.c \
               engine/core/ecs/world.c \
               engine/core/ecs/hierarchy.c \
               engine/core/clock.c \
               engine/core/asset_manager.c

TEST_BIN    := $(BUILD_DIR)/test_runner

test: $(SRC_TEST)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -o $(TEST_BIN) $(SRC_TEST) -lm
	@echo "[build] $(TEST_BIN) ready"
	@./$(TEST_BIN)

# ---- Clean ------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(VULKAN_BIN) $(OPENGL_BIN) $(EDITOR_BIN)
	rm -f engine_web.html engine_web.js engine_web.wasm
	rm -f shaders/*.spv
	@echo "[clean] done"


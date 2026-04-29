# ============================================================================
# GameEngine — Makefile
#
#   make               Build desktop Vulkan binary
#   make vulkan         Same as above
#   make opengl         Build desktop with OpenGL stub
#   make web            Build for web via Emscripten (OpenGL backend)
#   make shaders        Compile GLSL → SPIR-V
#   make clean          Remove build artifacts
# ============================================================================

CC        := gcc
CFLAGS    := -std=c23 -Wall -Wextra -Wpedantic -Ithird_party
LDFLAGS   :=

# --- source files ----------------------------------------------------------

SRC_ENGINE := engine/renderer/renderer.c \
              engine/core/engine.c \
              engine/core/asset_manager.c \
              engine/core/clock.c \
              engine/platform/platform.c

SRC_VULKAN := engine/renderer/vulkan/vulkan_renderer.c \
              engine/renderer/vulkan/vulkan_device.c \
              engine/renderer/vulkan/vulkan_swapchain.c \
              engine/renderer/vulkan/vulkan_pipeline.c \
              engine/renderer/vulkan/vulkan_buffer.c \
              engine/renderer/vulkan/vulkan_texture.c

SRC_OPENGL := engine/renderer/opengl/opengl_renderer.c

SRC_THIRD_PARTY := third_party/stb_image.c

SRC_APP    := app/main.c

# --- targets ---------------------------------------------------------------

BUILD_DIR := build

.PHONY: all vulkan opengl web shaders clean

all: vulkan

# ---- Vulkan (default) -----------------------------------------------------

VULKAN_SRCS := $(SRC_APP) $(SRC_ENGINE) $(SRC_VULKAN) $(SRC_THIRD_PARTY)
VULKAN_BIN  := engine_vulkan

vulkan: shaders $(VULKAN_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(VULKAN_BIN) $(VULKAN_SRCS) \
		$$(pkg-config --cflags --libs glfw3) -lvulkan -lm
	@echo "[build] $(VULKAN_BIN) ready"

# ---- OpenGL stub -----------------------------------------------------------

OPENGL_SRCS := $(SRC_APP) $(SRC_ENGINE) $(SRC_OPENGL)
OPENGL_BIN  := engine_opengl

opengl: $(OPENGL_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DUSE_OPENGL -o $(OPENGL_BIN) $(OPENGL_SRCS) \
		$$(pkg-config --cflags --libs glfw3) -lm
	@echo "[build] $(OPENGL_BIN) ready"

# ---- Web (Emscripten) ------------------------------------------------------

EMCC       := emcc
WEB_SRCS   := $(SRC_APP) $(SRC_ENGINE) $(SRC_OPENGL)
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

# ---- Clean ------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(VULKAN_BIN) $(OPENGL_BIN)
	rm -f engine_web.html engine_web.js engine_web.wasm
	rm -f shaders/*.spv
	@echo "[clean] done"

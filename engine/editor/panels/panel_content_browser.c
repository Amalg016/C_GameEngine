#ifdef EDITOR_BUILD

#include "panel_content_browser.h"
#include "panel_controller_editor.h"

#include "../../core/sprite_meta.h"
#include "../../core/animation.h"
#include "../../core/anim_controller.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

constexpr uint32_t PathMaxLen = 512;

/// Current directory path (relative to project root).
static char s_current_dir[PathMaxLen] = "assets";

static bool *s_p_show_controller_editor = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Returns true if `path` is a directory.
static bool is_directory(const char *path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

/// Returns true if `name` ends with `suffix`.
static bool has_suffix(const char *name, const char *suffix) {
    size_t name_len   = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) return false;
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

/// Returns true if `name` ends with a known image extension.
static bool is_image_file(const char *name) {
    return has_suffix(name, ".png")  ||
           has_suffix(name, ".jpg")  ||
           has_suffix(name, ".jpeg") ||
           has_suffix(name, ".bmp");
}

/// Returns true if `name` ends with `.sprite.meta`.
static bool is_sprite_meta_file(const char *name) {
    return has_suffix(name, ".sprite.meta");
}

/// Returns true if `name` ends with `.anim.meta`.
static bool is_anim_meta_file(const char *name) {
    return has_suffix(name, ".anim.meta");
}

/// Returns true if `name` ends with `.controller.meta`.
static bool is_controller_meta_file(const char *name) {
    return has_suffix(name, ".controller.meta");
}

// ---------------------------------------------------------------------------
// Internal — render sub-sprites for a spritesheet that has a .sprite.meta.
// ---------------------------------------------------------------------------

/// Render sprite sub-items under an expanded tree node.
/// `full_path` is the path to the texture file (e.g. "assets/images/sheet.png").
/// `name` is the display name of the texture file.
static void render_sprite_sub_items(const char *full_path,
                                    [[maybe_unused]] const char *name) {
    // Load the meta file.
    char meta_path[SpritePathMaxLen];
    sprite_meta_build_path(full_path, meta_path, SpritePathMaxLen);

    SpriteMeta meta;
    sprite_meta_init(&meta);

    if (!sprite_meta_load(meta_path, &meta)) {
        igTextDisabled("  (failed to load meta)");
        sprite_meta_destroy(&meta);
        return;
    }

    if (meta.region_count == 0) {
        igTextDisabled("  (no sprite regions defined)");
        sprite_meta_destroy(&meta);
        return;
    }

    // Render each sprite region as an indented selectable with drag source.
    for (uint32_t i = 0; i < meta.region_count; ++i) {
        const SpriteRegion *reg = &meta.regions[i];

        // Label: "  sprite_name  (32x32)"
        char label[256];
        snprintf(label, sizeof(label), "    \xf0\x9f\x96\xbc %s  (%.0fx%.0f)",
                 reg->name, reg->rect.w, reg->rect.h);

        // Unique ID to avoid collisions with other items.
        igPushID_Int((int)(i + 1000));

        if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
            // Single click — could select in future.
        }

        // ---- Drag source: sprite region payload -----------------------
        if (igBeginDragDropSource(0)) {
            SpriteDragPayload payload = {};
            strncpy(payload.texture_path, full_path,
                    SpritePathMaxLen - 1);
            payload.texture_path[SpritePathMaxLen - 1] = '\0';
            payload.rect = reg->rect;

            igSetDragDropPayload(SPRITE_REGION_DRAG_TYPE, &payload,
                                sizeof(SpriteDragPayload), 0);

            // Drag tooltip.
            igText("%s", reg->name);
            igTextDisabled("%.0f,%.0f  %.0fx%.0f",
                           reg->rect.x, reg->rect.y,
                           reg->rect.w, reg->rect.h);
            igEndDragDropSource();
        }

        igPopID();
    }

    sprite_meta_destroy(&meta);
}

// ---------------------------------------------------------------------------
// Internal — render a single file/directory entry.
//
// Returns true if the entry was rendered, false if it was skipped.
// ---------------------------------------------------------------------------

static bool render_entry(const char *name, const char *full_path,
                         bool dir_entry) {
    // Skip .sprite.meta and .anim.meta files — shown as sub-items.
    if (!dir_entry && (is_sprite_meta_file(name) ||
                       is_anim_meta_file(name))) {
        return false;
    }

    // Check if this image file has a sprite meta or anim meta.
    bool has_meta = false;
    bool has_anim = false;
    bool has_ctrl = false;
    if (!dir_entry && is_image_file(name)) {
        has_meta = sprite_meta_exists(full_path);
        has_anim = anim_meta_exists(full_path);
        // Check for controller: build .anim.meta path first, then check.
        if (has_anim) {
            char anim_path_tmp[AnimPathMaxLen];
            anim_build_meta_path(full_path, anim_path_tmp, AnimPathMaxLen);
            has_ctrl = anim_ctrl_meta_exists(anim_path_tmp);
        }
    }

    if (dir_entry) {
        // ---- Directory entry --------------------------------------------------
        char label[PathMaxLen];
        snprintf(label, PathMaxLen, "\xf0\x9f\x93\x81 %s", name);

        if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
            // Single click.
        }

        if (igIsItemHovered(0) &&
            igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
            strncpy(s_current_dir, full_path, PathMaxLen - 1);
            s_current_dir[PathMaxLen - 1] = '\0';
        }
    } else if (has_meta) {
        // ---- Image with sprite meta — expandable tree node --------------------
        char label[PathMaxLen];
        snprintf(label, PathMaxLen, "\xf0\x9f\x96\xbc %s", name);

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        bool node_open = igTreeNodeEx_Str(label, flags);

        // The whole tree header is also a drag source (full texture).
        if (igBeginDragDropSource(0)) {
            igSetDragDropPayload("ASSET_PATH", full_path,
                                strlen(full_path) + 1, 0);
            igText("%s (full texture)", name);
            igEndDragDropSource();
        }

        if (node_open) {
            render_sprite_sub_items(full_path, name);

            // Show animation clips if .anim.meta exists.
            if (has_anim) {
                char anim_path[AnimPathMaxLen];
                anim_build_meta_path(full_path, anim_path, AnimPathMaxLen);

                AnimData anim_data;
                anim_data_init(&anim_data);
                if (anim_data_load(anim_path, &anim_data)) {
                    for (uint32_t c = 0; c < anim_data.clip_count; ++c) {
                        char clip_label[256];
                        snprintf(clip_label, sizeof(clip_label),
                                 "    \xf0\x9f\x8e\xac %s  (%u frames)",
                                 anim_data.clips[c].name,
                                 anim_data.clips[c].frame_count);

                        igPushID_Int((int)(c + 2000));
                        igSelectable_Bool(clip_label, false, 0,
                                          (ImVec2){ 0, 0 });
                        igPopID();
                    }
                }
            }

                // Show controller info if .controller.meta exists.
                if (has_ctrl) {
                    char anim_path_buf[AnimPathMaxLen];
                    anim_build_meta_path(full_path, anim_path_buf,
                                         AnimPathMaxLen);
                    char ctrl_path[AnimPathMaxLen];
                    anim_ctrl_build_meta_path(anim_path_buf, ctrl_path,
                                              AnimPathMaxLen);

                    AnimController ctrl_data;
                    anim_controller_init(&ctrl_data);
                    if (anim_controller_load(ctrl_path, &ctrl_data)) {
                        igPushID_Int(5000);
                        char ctrl_label[256];
                        snprintf(ctrl_label, sizeof(ctrl_label),
                                 "    \xf0\x9f\x8e\xae Controller  "
                                 "(%u states, %u params)",
                                 ctrl_data.state_count,
                                 ctrl_data.param_count);
                        igSelectable_Bool(ctrl_label, false, 0,
                                          (ImVec2){ 0, 0 });
                        
                        if (igIsItemHovered(0) &&
                            igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                            panel_controller_editor_open(ctrl_path);
                            if (s_p_show_controller_editor != nullptr) {
                                *s_p_show_controller_editor = true;
                            }
                        }
                        igPopID();
                    }
                }

            igTreePop();
        }
    } else {
        // ---- Regular file entry -----------------------------------------------
        char label[PathMaxLen];
        if (is_controller_meta_file(name)) {
            snprintf(label, PathMaxLen, "    \xf0\x9f\x8e\xae %s", name);
        } else if (has_suffix(name, ".json")) {
            snprintf(label, PathMaxLen, "    \xf0\x9f\x8e\xac %s", name);
        } else {
            snprintf(label, PathMaxLen, "    %s", name);
        }

        if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
            // Single click.
        }

        if (is_controller_meta_file(name)) {
            if (igIsItemHovered(0) &&
                igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                panel_controller_editor_open(full_path);
                if (s_p_show_controller_editor != nullptr) {
                    *s_p_show_controller_editor = true;
                }
            }
        }

        // Drag source: asset path payload.
        if (igBeginDragDropSource(0)) {
            igSetDragDropPayload("ASSET_PATH", full_path,
                                strlen(full_path) + 1, 0);
            igText("%s", name);
            igEndDragDropSource();
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// panel_content_browser_render
// ---------------------------------------------------------------------------

void panel_content_browser_render(bool *p_open, bool *p_show_controller_editor) {
    s_p_show_controller_editor = p_show_controller_editor;
    if (!igBegin("Content Browser", p_open, 0)) {
        igEnd();
        return;
    }

    // Back button — navigate to parent directory (but not above "assets").
    if (strcmp(s_current_dir, "assets") != 0) {
        if (igButton("<- Back", (ImVec2){ 0, 0 })) {
            // Strip the last path component.
            char *last_sep = strrchr(s_current_dir, '/');
            if (last_sep != nullptr) {
                *last_sep = '\0';
            } else {
                // Fallback to root.
                snprintf(s_current_dir, PathMaxLen, "assets");
            }
        }
        igSameLine(0.0f, -1.0f);
    }

    igText("Directory: %s", s_current_dir);
    igSeparator();

    // Scrollable content area.
    if (igBeginChild_Str("ContentArea", (ImVec2){ 0, 0 }, 0, 0)) {
#ifdef _WIN32
        // Windows implementation using FindFirstFile / FindNextFile
        char search_path[1024];
        snprintf(search_path, sizeof(search_path), "%s/*", s_current_dir);

        WIN32_FIND_DATAA find_data;
        HANDLE hFind = FindFirstFileA(search_path, &find_data);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                const char *name = find_data.cFileName;
                if (name[0] == '.') continue;

                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s",
                         s_current_dir, name);

                bool dir_entry = is_directory(full_path);
                render_entry(name, full_path, dir_entry);
            } while (FindNextFileA(hFind, &find_data) != 0);

            FindClose(hFind);
        } else {
            igTextColored((ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f },
                          "Cannot open directory.");
        }
#else
        // POSIX implementation (opendir / readdir / closedir)
        DIR *dir = opendir(s_current_dir);
        if (dir == nullptr) {
            igTextColored((ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f },
                          "Cannot open directory.");
            igEndChild();
            igEnd();
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     s_current_dir, entry->d_name);

            bool dir_entry = is_directory(full_path);
            render_entry(entry->d_name, full_path, dir_entry);
        }
        closedir(dir);
#endif
    }
    igEndChild();

    igEnd();
}

#endif // EDITOR_BUILD

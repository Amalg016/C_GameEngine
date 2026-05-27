#ifdef EDITOR_BUILD

#include "panel_content_browser.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

constexpr uint32_t PathMaxLen = 512;

/// Current directory path (relative to project root).
static char s_current_dir[PathMaxLen] = "assets";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Returns true if `path` is a directory.
static bool is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

// ---------------------------------------------------------------------------
// panel_content_browser_render
// ---------------------------------------------------------------------------

void panel_content_browser_render(bool *p_open) {
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

    // List directory contents.
    DIR *dir = opendir(s_current_dir);
    if (dir == nullptr) {
        igTextColored((ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f },
                      "Cannot open directory.");
        igEnd();
        return;
    }

    // Columns layout for a grid-like feel.
    if (igBeginChild_Str("ContentArea", (ImVec2){ 0, 0 }, 0, 0)) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip hidden files and . / ..
            if (entry->d_name[0] == '.') continue;

            // Build full path.
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     s_current_dir, entry->d_name);

            bool dir_entry = is_directory(full_path);

            // Icon prefix.
            char label[PathMaxLen];
            if (dir_entry) {
                snprintf(label, PathMaxLen, "[D] %s", entry->d_name);
            } else {
                snprintf(label, PathMaxLen, "    %s", entry->d_name);
            }

            if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
                // Single click — select (no action yet).
            }

            if (dir_entry && igIsItemHovered(0) &&
                igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                strncpy(s_current_dir, full_path, PathMaxLen - 1);
                s_current_dir[PathMaxLen - 1] = '\0';
            }

            // ---- Drag source: asset path payload ------------------------
            if (!dir_entry && igBeginDragDropSource(0)) {
                igSetDragDropPayload("ASSET_PATH", full_path,
                                     strlen(full_path) + 1, 0);
                igText("%s", entry->d_name);
                igEndDragDropSource();
            }
        }
    }
    igEndChild();

    closedir(dir);
    igEnd();
}

#endif // EDITOR_BUILD

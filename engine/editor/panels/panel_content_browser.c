#ifdef EDITOR_BUILD

#include "panel_content_browser.h"

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

    // Columns layout for a grid-like feel.
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
                // Skip hidden files and . / ..
                if (name[0] == '.') continue;

                // Build full path.
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", s_current_dir, name);

                bool dir_entry = is_directory(full_path);

                // Icon prefix.
                char label[PathMaxLen];
                if (dir_entry) {
                    snprintf(label, PathMaxLen, "[D] %s", name);
                } else {
                    snprintf(label, PathMaxLen, "    %s", name);
                }

                if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
                    // Single click
                }

                if (dir_entry && igIsItemHovered(0) &&
                    igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                    strncpy(s_current_dir, full_path, PathMaxLen - 1);
                    s_current_dir[PathMaxLen - 1] = '\0';
                }

                // ---- Drag source: asset path payload ------------------------
                if (!dir_entry && igBeginDragDropSource(0)) {
                    igSetDragDropPayload("ASSET_PATH", full_path, strlen(full_path) + 1, 0);
                    igText("%s", name);
                    igEndDragDropSource();
                }
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
            // Skip hidden files and . / ..
            if (entry->d_name[0] == '.') continue;

            // Build full path.
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", s_current_dir, entry->d_name);

            bool dir_entry = is_directory(full_path);

            // Icon prefix.
            char label[PathMaxLen];
            if (dir_entry) {
                snprintf(label, PathMaxLen, "[D] %s", entry->d_name);
            } else {
                snprintf(label, PathMaxLen, "    %s", entry->d_name);
            }

            if (igSelectable_Bool(label, false, 0, (ImVec2){ 0, 0 })) {
                // Single click
            }

            if (dir_entry && igIsItemHovered(0) &&
                igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                strncpy(s_current_dir, full_path, PathMaxLen - 1);
                s_current_dir[PathMaxLen - 1] = '\0';
            }

            // ---- Drag source: asset path payload ------------------------
            if (!dir_entry && igBeginDragDropSource(0)) {
                igSetDragDropPayload("ASSET_PATH", full_path, strlen(full_path) + 1, 0);
                igText("%s", entry->d_name);
                igEndDragDropSource();
            }
        }
        closedir(dir);
#endif
    }
    igEndChild();

    igEnd();
}

#endif // EDITOR_BUILD

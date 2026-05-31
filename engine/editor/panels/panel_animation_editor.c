#ifdef EDITOR_BUILD

#include "panel_animation_editor.h"

#include "../../core/asset_manager.h"
#include "../../core/animation.h"
#include "../../core/sprite_meta.h"
#include "../../renderer/renderer.h"
#include "panel_console.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Panel state (module-level statics)
// ---------------------------------------------------------------------------

/// Texture input path.
static char s_texture_path[AnimPathMaxLen] = {};

/// Loaded texture state.
static AssetHandle s_loaded_handle   = ASSET_HANDLE_INVALID;
static void       *s_imgui_tex_id   = nullptr;
static uint32_t    s_tex_width      = 0;
static uint32_t    s_tex_height     = 0;
static bool        s_texture_loaded = false;

/// Animation data (current editing session).
static AnimData s_anim = {};
static bool     s_anim_loaded = false;

/// Currently selected clip index (-1 = none).
static int s_selected_clip = -1;

/// Preview playback state.
static bool     s_preview_playing = false;
static uint32_t s_preview_frame   = 0;
static float    s_preview_elapsed = 0.0f;

/// Sprite meta for frame palette (optional).
static SpriteMeta s_sprite_meta = {};
static bool       s_sprite_meta_loaded = false;

// ---------------------------------------------------------------------------
// Internal — load texture and animation data.
// ---------------------------------------------------------------------------

static void load_anim_texture(AssetManager *am, Renderer *renderer) {
    // Release previous texture.
    if (s_imgui_tex_id != nullptr) {
        renderer_unregister_imgui_texture(renderer, s_imgui_tex_id);
        s_imgui_tex_id = nullptr;
    }
    if (s_loaded_handle != ASSET_HANDLE_INVALID) {
        asset_manager_release(am, s_loaded_handle);
        s_loaded_handle = ASSET_HANDLE_INVALID;
    }
    s_texture_loaded = false;
    s_anim_loaded = false;
    s_selected_clip = -1;
    s_preview_playing = false;

    // Clean up sprite meta.
    if (s_sprite_meta_loaded) {
        sprite_meta_destroy(&s_sprite_meta);
        s_sprite_meta_loaded = false;
    }

    // Load texture.
    s_loaded_handle = asset_manager_load_texture(am, s_texture_path);
    if (s_loaded_handle == ASSET_HANDLE_INVALID) {
        console_log("[anim_editor] Failed to load: %s", s_texture_path);
        return;
    }

    asset_manager_get_texture_size(am, s_loaded_handle,
                                   &s_tex_width, &s_tex_height);

    void *gpu_data = asset_manager_get_data(am, s_loaded_handle);
    s_imgui_tex_id = renderer_register_imgui_texture(renderer, gpu_data);
    if (s_imgui_tex_id == nullptr) {
        console_log("[anim_editor] Failed to register texture with ImGui");
        return;
    }

    s_texture_loaded = true;

    // Set up animation data.
    anim_data_init(&s_anim);
    strncpy(s_anim.texture_path, s_texture_path, AnimPathMaxLen - 1);
    s_anim.texture_path[AnimPathMaxLen - 1] = '\0';

    // Try loading existing .anim.meta.
    char meta_path[AnimPathMaxLen];
    anim_build_meta_path(s_texture_path, meta_path, AnimPathMaxLen);
    if (anim_meta_exists(s_texture_path)) {
        if (anim_data_load(meta_path, &s_anim)) {
            s_anim_loaded = true;
            if (s_anim.clip_count > 0) s_selected_clip = 0;
            console_log("[anim_editor] Loaded: %s (%u clips)",
                        meta_path, s_anim.clip_count);
        }
    }

    // Try loading sprite meta for frame palette.
    sprite_meta_init(&s_sprite_meta);
    if (sprite_meta_exists(s_texture_path)) {
        char spr_meta_path[SpritePathMaxLen];
        sprite_meta_build_path(s_texture_path, spr_meta_path, SpritePathMaxLen);
        if (sprite_meta_load(spr_meta_path, &s_sprite_meta)) {
            s_sprite_meta_loaded = true;
        }
    }

    s_anim_loaded = true;
    console_log("[anim_editor] Loaded texture: %s (%ux%u)",
                s_texture_path, s_tex_width, s_tex_height);
}

// ---------------------------------------------------------------------------
// Internal — draw the toolbar.
// ---------------------------------------------------------------------------

static void draw_toolbar(AssetManager *am, Renderer *renderer) {
    igText("Spritesheet:");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(280.0f);
    igInputText("##anim_tex_path", s_texture_path, sizeof(s_texture_path),
                0, nullptr, nullptr);

    // Drop target on the text input field.
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            strncpy(s_texture_path, dropped, sizeof(s_texture_path) - 1);
            s_texture_path[sizeof(s_texture_path) - 1] = '\0';
            load_anim_texture(am, renderer);
        }
        igEndDragDropTarget();
    }

    igSameLine(0.0f, 4.0f);
    if (igButton("Load##anim_load", (ImVec2){ 50, 0 })) {
        load_anim_texture(am, renderer);
    }

    // Drop target for texture paths.
    if (igBeginDragDropTarget()) {
        const ImGuiPayload *payload = igAcceptDragDropPayload("ASSET_PATH", 0);
        if (payload != nullptr) {
            const char *dropped = (const char *)payload->Data;
            strncpy(s_texture_path, dropped, sizeof(s_texture_path) - 1);
            s_texture_path[sizeof(s_texture_path) - 1] = '\0';
            load_anim_texture(am, renderer);
        }
        igEndDragDropTarget();
    }

    if (!s_texture_loaded) return;

    igSameLine(0.0f, 12.0f);
    if (igButton("Save .anim.meta", (ImVec2){ 130, 0 })) {
        char meta_path[AnimPathMaxLen];
        anim_build_meta_path(s_texture_path, meta_path, AnimPathMaxLen);
        if (anim_data_save(&s_anim, meta_path)) {
            console_log("[anim_editor] Saved: %s", meta_path);
        } else {
            console_log("[anim_editor] Failed to save: %s", meta_path);
        }
    }

    igSameLine(0.0f, 8.0f);
    igText("%ux%u  |  %u clips", s_tex_width, s_tex_height,
           s_anim.clip_count);
}

// ---------------------------------------------------------------------------
// Internal — draw the clip list sidebar.
// ---------------------------------------------------------------------------

static void draw_clip_list(void) {
    igText("Clips");
    igSeparator();

    // Add clip button.
    if (igButton("+ New Clip", (ImVec2){ -1, 0 })) {
        if (s_anim.clip_count < AnimMaxClips) {
            AnimClip *clip = &s_anim.clips[s_anim.clip_count];
            *clip = (AnimClip){};
            snprintf(clip->name, AnimNameMaxLen, "clip_%u",
                     s_anim.clip_count);
            clip->fps = 8.0f;
            clip->loop = true;
            s_selected_clip = (int)s_anim.clip_count;
            s_anim.clip_count++;
        }
    }

    igSeparator();

    for (uint32_t i = 0; i < s_anim.clip_count; ++i) {
        igPushID_Int((int)i);
        bool selected = ((int)i == s_selected_clip);

        char label[80];
        snprintf(label, sizeof(label), "%s (%u frames)",
                 s_anim.clips[i].name, s_anim.clips[i].frame_count);

        if (igSelectable_Bool(label, selected, 0, (ImVec2){ 0, 0 })) {
            s_selected_clip = (int)i;
            s_preview_playing = false;
            s_preview_frame = 0;
            s_preview_elapsed = 0.0f;
        }

        // Delete button on right-click context menu.
        if (igBeginPopupContextItem("##clip_ctx", ImGuiPopupFlags_MouseButtonRight)) {
            if (igMenuItem_Bool("Delete Clip", nullptr, false, true)) {
                // Shift remaining clips.
                for (uint32_t j = i; j < s_anim.clip_count - 1; ++j) {
                    s_anim.clips[j] = s_anim.clips[j + 1];
                }
                s_anim.clip_count--;
                if (s_selected_clip >= (int)s_anim.clip_count) {
                    s_selected_clip = (int)s_anim.clip_count - 1;
                }
                igEndPopup();
                igPopID();
                break;
            }
            igEndPopup();
        }

        igPopID();
    }
}

// ---------------------------------------------------------------------------
// Internal — draw the clip editor (timeline + properties).
// ---------------------------------------------------------------------------

static void draw_clip_editor(void) {
    if (s_selected_clip < 0 ||
        s_selected_clip >= (int)s_anim.clip_count) {
        igTextDisabled("Select or create a clip from the sidebar.");
        return;
    }

    AnimClip *clip = &s_anim.clips[s_selected_clip];

    // ---- Clip properties --------------------------------------------------
    igText("Clip: ");
    igSameLine(0.0f, 4.0f);
    igSetNextItemWidth(150.0f);
    igInputText("##clip_name", clip->name, AnimNameMaxLen, 0,
                nullptr, nullptr);

    igSameLine(0.0f, 12.0f);
    igSetNextItemWidth(100.0f);
    igDragFloat("FPS", &clip->fps, 0.5f, 1.0f, 60.0f, "%.1f", 0);
    if (clip->fps < 1.0f) clip->fps = 1.0f;

    igSameLine(0.0f, 12.0f);
    igCheckbox("Loop", &clip->loop);

    igSameLine(0.0f, 12.0f);
    igText("Frames: %u / %u", clip->frame_count, AnimMaxFrames);

    igSeparator();

    // ---- Frame timeline ---------------------------------------------------
    igText("Timeline:");
    igSameLine(0.0f, 8.0f);

    // Preview controls.
    if (s_preview_playing) {
        if (igButton("Stop##preview", (ImVec2){ 50, 0 })) {
            s_preview_playing = false;
        }
    } else {
        if (igButton("Play##preview", (ImVec2){ 50, 0 })) {
            s_preview_playing = true;
            s_preview_frame = 0;
            s_preview_elapsed = 0.0f;
        }
    }

    // Advance preview timer.
    if (s_preview_playing && clip->frame_count > 0) {
        ImGuiIO *io = igGetIO_Nil();
        s_preview_elapsed += io->DeltaTime;
        float frame_dur = 1.0f / clip->fps;
        while (s_preview_elapsed >= frame_dur) {
            s_preview_elapsed -= frame_dur;
            s_preview_frame++;
            if (s_preview_frame >= clip->frame_count) {
                if (clip->loop) {
                    s_preview_frame = 0;
                } else {
                    s_preview_frame = clip->frame_count - 1;
                    s_preview_playing = false;
                    break;
                }
            }
        }
    }

    // Draw frame thumbnails as a horizontal strip.
    if (s_imgui_tex_id != nullptr && clip->frame_count > 0) {
        float thumb_size = 48.0f;

        if (igBeginChild_Str("##timeline_strip", (ImVec2){ 0, thumb_size + 30 },
                             ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_HorizontalScrollbar)) {
            ImTextureRef_c tex_ref = {
                ._TexData = nullptr,
                ._TexID   = (ImTextureID)s_imgui_tex_id,
            };

            for (uint32_t f = 0; f < clip->frame_count; ++f) {
                igPushID_Int((int)f);

                AnimFrame *frame = &clip->frames[f];

                // Compute UVs from pixel rect.
                ImVec2 uv0 = {
                    frame->rect.x / (float)s_tex_width,
                    frame->rect.y / (float)s_tex_height
                };
                ImVec2 uv1 = {
                    (frame->rect.x + frame->rect.w) / (float)s_tex_width,
                    (frame->rect.y + frame->rect.h) / (float)s_tex_height
                };

                // Highlight current preview frame.
                if (f == s_preview_frame && s_preview_playing) {
                    ImVec2 cursor = igGetCursorScreenPos();
                    ImDrawList *dl = igGetWindowDrawList();
                    ImDrawList_AddRectFilled(dl,
                        (ImVec2){ cursor.x - 2, cursor.y - 2 },
                        (ImVec2){ cursor.x + thumb_size + 2,
                                  cursor.y + thumb_size + 2 },
                        igGetColorU32_Vec4(
                            (ImVec4){ 0.2f, 0.7f, 1.0f, 0.5f }),
                        4.0f, 0);
                }

                igImage(tex_ref, (ImVec2){ thumb_size, thumb_size },
                        uv0, uv1);

                // Frame index label.
                char flabel[16];
                snprintf(flabel, sizeof(flabel), "%u", f);
                igTextDisabled("%s", flabel);

                // Right-click to remove frame.
                if (igBeginPopupContextItem("##frame_ctx",
                                            ImGuiPopupFlags_MouseButtonRight)) {
                    if (igMenuItem_Bool("Remove Frame", nullptr, false, true)) {
                        for (uint32_t j = f; j < clip->frame_count - 1; ++j) {
                            clip->frames[j] = clip->frames[j + 1];
                        }
                        clip->frame_count--;
                        igEndPopup();
                        igPopID();
                        break;
                    }
                    igEndPopup();
                }

                igSameLine(0.0f, 4.0f);
                igPopID();
            }
        }
        igEndChild();
    } else {
        igTextDisabled("No frames. Add frames from the sprite palette below.");
    }

    // ---- Preview window ---------------------------------------------------
    if (s_imgui_tex_id != nullptr && clip->frame_count > 0) {
        igSeparator();
        igText("Preview:");

        uint32_t show_frame = s_preview_playing ? s_preview_frame : 0;
        if (show_frame >= clip->frame_count) show_frame = 0;

        AnimFrame *pf = &clip->frames[show_frame];
        ImVec2 uv0 = {
            pf->rect.x / (float)s_tex_width,
            pf->rect.y / (float)s_tex_height
        };
        ImVec2 uv1 = {
            (pf->rect.x + pf->rect.w) / (float)s_tex_width,
            (pf->rect.y + pf->rect.h) / (float)s_tex_height
        };

        float preview_size = 96.0f;
        ImTextureRef_c tex_ref = {
            ._TexData = nullptr,
            ._TexID   = (ImTextureID)s_imgui_tex_id,
        };
        igImage(tex_ref, (ImVec2){ preview_size, preview_size },
                uv0, uv1);
    }
}

// ---------------------------------------------------------------------------
// Internal — draw the sprite palette (from .sprite.meta regions).
// Clicking a region adds it as a frame to the current clip.
// ---------------------------------------------------------------------------

static void draw_sprite_palette(void) {
    if (!s_texture_loaded || s_imgui_tex_id == nullptr) return;

    igSeparator();
    igText("Sprite Palette — click to add frame");

    if (s_selected_clip < 0 ||
        s_selected_clip >= (int)s_anim.clip_count) {
        igTextDisabled("Select a clip first.");
        return;
    }

    AnimClip *clip = &s_anim.clips[s_selected_clip];

    if (s_sprite_meta_loaded && s_sprite_meta.region_count > 0) {
        // Show sprite regions from .sprite.meta as clickable thumbnails.
        float thumb = 40.0f;
        ImTextureRef_c tex_ref = {
            ._TexData = nullptr,
            ._TexID   = (ImTextureID)s_imgui_tex_id,
        };

        for (uint32_t i = 0; i < s_sprite_meta.region_count; ++i) {
            igPushID_Int((int)(i + 5000));
            const SpriteRegion *reg = &s_sprite_meta.regions[i];

            ImVec2 uv0 = {
                reg->rect.x / (float)s_tex_width,
                reg->rect.y / (float)s_tex_height
            };
            ImVec2 uv1 = {
                (reg->rect.x + reg->rect.w) / (float)s_tex_width,
                (reg->rect.y + reg->rect.h) / (float)s_tex_height
            };

            if (igImageButton("##spr_pal", tex_ref,
                              (ImVec2_c){ thumb, thumb },
                              (ImVec2_c){ uv0.x, uv0.y },
                              (ImVec2_c){ uv1.x, uv1.y },
                              (ImVec4_c){ 0, 0, 0, 0 },
                              (ImVec4_c){ 1, 1, 1, 1 })) {
                // Add this region as a frame.
                if (clip->frame_count < AnimMaxFrames) {
                    clip->frames[clip->frame_count].rect = reg->rect;
                    clip->frame_count++;
                }
            }

            if (igIsItemHovered(0)) {
                igSetTooltip("%s (%.0fx%.0f)", reg->name,
                            reg->rect.w, reg->rect.h);
            }

            igSameLine(0.0f, 4.0f);
            igPopID();
        }
        igNewLine();
    } else {
        igTextDisabled("No .sprite.meta found for this texture.\n"
                       "Use the Sprite Editor to slice the spritesheet first.");

        // Fallback: manual frame entry.
        igSeparator();
        igText("Manual frame entry:");

        static float s_manual_rect[4] = { 0, 0, 32, 32 };
        igDragFloat4("Rect (x,y,w,h)", s_manual_rect, 1.0f, 0.0f,
                     4096.0f, "%.0f", 0);
        if (igButton("Add Frame", (ImVec2){ 100, 0 })) {
            if (clip->frame_count < AnimMaxFrames) {
                clip->frames[clip->frame_count].rect = (Rect){
                    .x = s_manual_rect[0],
                    .y = s_manual_rect[1],
                    .w = s_manual_rect[2],
                    .h = s_manual_rect[3],
                };
                clip->frame_count++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void panel_animation_editor_render(bool *p_open,
                                   AssetManager *am,
                                   Renderer *renderer) {
    if (!igBegin("Animation Editor", p_open, ImGuiWindowFlags_MenuBar)) {
        igEnd();
        return;
    }

    // ---- Menu bar ---------------------------------------------------------
    if (igBeginMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem_Bool("Save", "Ctrl+S", false,
                                s_texture_loaded)) {
                char meta_path[AnimPathMaxLen];
                anim_build_meta_path(s_texture_path, meta_path,
                                     AnimPathMaxLen);
                if (anim_data_save(&s_anim, meta_path)) {
                    console_log("[anim_editor] Saved: %s", meta_path);
                }
            }
            igEndMenu();
        }
        igEndMenuBar();
    }

    // ---- Toolbar ----------------------------------------------------------
    draw_toolbar(am, renderer);

    if (!s_texture_loaded) {
        igTextDisabled("Load a spritesheet to begin authoring animations.");
        igEnd();
        return;
    }

    igSeparator();

    // ---- Layout: clip list (left) + clip editor (right) -------------------
    float avail_w = igGetContentRegionAvail().x;
    float sidebar_w = 180.0f;
    float editor_w = avail_w - sidebar_w - 8.0f;
    if (editor_w < 200.0f) editor_w = avail_w;

    // Clip list sidebar.
    if (igBeginChild_Str("##anim_clip_list",
                         (ImVec2){ sidebar_w, 0 },
                         ImGuiChildFlags_Borders, 0)) {
        draw_clip_list();
    }
    igEndChild();

    // Clip editor + timeline.
    if (editor_w < avail_w) {
        igSameLine(0.0f, 8.0f);
    }
    if (igBeginChild_Str("##anim_clip_editor",
                         (ImVec2){ editor_w, 0 },
                         ImGuiChildFlags_Borders, 0)) {
        draw_clip_editor();
        draw_sprite_palette();
    }
    igEndChild();

    igEnd();
}

void panel_animation_editor_shutdown(Renderer *renderer) {
    if (s_imgui_tex_id != nullptr && renderer != nullptr) {
        renderer_unregister_imgui_texture(renderer, s_imgui_tex_id);
        s_imgui_tex_id = nullptr;
    }
    if (s_sprite_meta_loaded) {
        sprite_meta_destroy(&s_sprite_meta);
        s_sprite_meta_loaded = false;
    }
    s_loaded_handle  = ASSET_HANDLE_INVALID;
    s_texture_loaded = false;
    s_anim_loaded    = false;
}

#endif // EDITOR_BUILD

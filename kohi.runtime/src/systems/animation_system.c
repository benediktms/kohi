#include "animation_system.h"

#include "core/engine.h"
#include "debug/kassert.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"

b8 animation_system_initialize(u64* memory_requirement, animation_system_state* memory, const animation_system_config* config) {
    *memory_requirement = sizeof(animation_system_state);
    if (!memory) {
        return true;
    }

    // NOTE: perform config/init here.

    animation_system_state* state = (animation_system_state*)memory;

    state->max_animations = config->max_animations;
    state->animations = KALLOC_TYPE_CARRAY(animation_data, state->max_animations);
    state->time_scale = 1.0f;

    // Global lighting storage buffer
    u64 buffer_size = sizeof(animation_shader_data) * state->max_animations;
    state->global_animation_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT);
    KASSERT(state->global_animation_ssbo != KRENDERBUFFER_INVALID);
    KDEBUG("Created animation global storage buffer.");

    return true;
}

void animation_system_shutdown(animation_system_state* state) {
    if (state) {
        renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, state->global_animation_ssbo);
    }
}

void animation_system_update(animation_system_state* state, frame_data* p_frame_data) {
    // LEFTOFF:
}
void animation_system_frame_prepare(animation_system_state* state, frame_data* p_frame_data) {
    // TODO: upload to the storage buffer
}

void animation_system_time_scale(animation_system_state* state, f32 time_scale) {
    state->time_scale = time_scale;
}

kanimation animation_create(animation_system_state* state, const animation_config* config) {
    kanimation anim = KANIMATION_INVALID;

    for (u8 i = 0; i < state->max_animations; ++i) {
        if (state->animations[i].track_count == 0) {
            anim = i;
            break;
        }
    }

    KASSERT_DEBUG(anim != KANIMATION_INVALID);

    // FIXME: init based on anim config, etc.

    return anim;
}
void animation_destroy(animation_system_state* state, kanimation animation) {
    if (animation != KANIMATION_INVALID) {
        animation_data* data = &state->animations[animation];

        KFREE_TYPE_CARRAY(data->bone_transforms, mat4, data->bone_count);
        KFREE_TYPE_CARRAY(data->skeleton.transforms, mat4, data->bone_count);
        KFREE_TYPE_CARRAY(data->skeleton.bones, animation_bone, data->bone_count);
        KFREE_TYPE_CARRAY(data->active_anim_states, animation_state, data->active_anim_count);
        KFREE_TYPE_CARRAY(data->tracks, animation_track, data->track_count);

        kzero_memory(data, sizeof(animation_data));
    }
}

void animation_track_play(animation_system_state* state, kanimation animation, u8 track, b8 loop) {
}
void animation_track_pause(animation_system_state* state, kanimation animation, u8 track) {
}
void animation_track_stop(animation_system_state* state, kanimation animation, u8 track) {
}
void animation_track_seek(animation_system_state* state, kanimation animation, u8 track, f32 time) {
}
void animation_track_seek_percent(animation_system_state* state, kanimation animation, u8 track, f32 percent) {
}
void animation_track_playback_speed(animation_system_state* state, kanimation animation, u8 track, f32 speed) {
}

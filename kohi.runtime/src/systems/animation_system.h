#pragma once

#include "core/frame_data.h"
#include "defines.h"
#include "renderer/renderer_types.h"

#define KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL "Kohi.StorageBuffer.AnimationsGlobal"

#define KANIMATION_MAX_BONES 64
#define KANIMATION_MAX_VERTEX_BONE_WEIGHTS 4

typedef struct animation_system_config {
    u8 max_animations;
} animation_system_config;

typedef struct animation_keyframe {
    f32 time; // the time offset from 0 where this keyframe should be applied.
    u32 bone_count;
    u32* bone_ids;
    mat4* transforms;
    // TODO: Setup a weighted system that propagates down the hierarchy and affects how much
    // each keyframe is applied per bone. This automatically blends them together as needed but
    // avoids branching. pose[i] = lerp(anim_a[i], anim_b[i], masks[i])
} animation_keyframe;

typedef struct animation_key_pos {
    vec3 position;
    f32 timestamp;
} animation_key_pos;

typedef struct animation_key_rot {
    quat rotation;
    f32 timestamp;
} animation_key_rot;

typedef struct animation_key_scale {
    vec3 scale;
    f32 timestamp;
} animation_key_scale;

typedef struct animation_bone {
    kname name;
    u32 position_count;
    animation_key_pos* positions;
    u32 rotation_count;
    animation_key_rot* rotations;
    u32 scale_count;
    animation_key_scale* scales;

    mat4 local_transform;

    u32 bone_id;
    u32 parent_bone_id; // NOTE: INVALID_ID == no parent
} animation_bone;

// Default transforms of the skeleton and its bones.
typedef struct animation_skeleton_data {
    // Counts are based on the animation_data bone_count
    mat4* transforms;
    animation_bone* bones;
} animation_skeleton_data;

typedef struct animation_track {
    b8 is_looping;
    kname name;
    // Time in seconds for the entire animation to play.
    f32 total_time;

    u32 keyframe_count;
    animation_keyframe* keyframes;
} animation_track;

typedef struct animation_state {
    f32 current_time;
} animation_state;

// FIXME: Rename this to be something more descriptive of holding multiple animations as well as skeletal data.
// NOT storing mesh data - that will be handled in the skinned_mesh system.
typedef struct animation_data {
    // 0 = free slot because we cannot have animations with no tracks
    u32 track_count;
    animation_track* tracks;
    animation_skeleton_data skeleton;

    // States for active animations
    u32 active_anim_count;
    animation_state* active_anim_states;

    // Active state of all the bone transforms.
    u32 bone_count;
    // This is what gets sent to the storage buffer and used in the shader.
    mat4* bone_transforms;
} animation_data;

typedef struct animation_system_state {
    f32 time_scale;
    u8 max_animations;
    animation_data* animations;
    krenderbuffer global_animation_ssbo;
} animation_system_state;

typedef struct animation_shader_data {
    mat4 transforms[KANIMATION_MAX_BONES];
} animation_shader_data;

typedef struct animation_global_ssbo_data {
    animation_shader_data* animations;
} animation_global_ssbo_data;

// References an animation_data in the system
typedef u8 kanimation;
#define KANIMATION_INVALID INVALID_ID_U8

typedef struct animation_config {
    // TODO: implement this
    u32 dummy;
} animation_config;

b8 animation_system_initialize(u64* memory_requirement, animation_system_state* memory, const animation_system_config* config);
void animation_system_shutdown(animation_system_state* state);

void animation_system_update(animation_system_state* state, frame_data* p_frame_data);
void animation_system_frame_prepare(animation_system_state* state, frame_data* p_frame_data);

KAPI void animation_system_time_scale(animation_system_state* state, f32 time_scale); // 1.0 - normal

KAPI kanimation animation_create(animation_system_state* state, const animation_config* config);
KAPI void animation_destroy(animation_system_state* state, kanimation animation);

KAPI void animation_track_play(animation_system_state* state, kanimation animation, u8 track, b8 loop);
KAPI void animation_track_pause(animation_system_state* state, kanimation animation, u8 track);
KAPI void animation_track_stop(animation_system_state* state, kanimation animation, u8 track);
KAPI void animation_track_seek(animation_system_state* state, kanimation animation, u8 track, f32 time);            // 0-total animation track time
KAPI void animation_track_seek_percent(animation_system_state* state, kanimation animation, u8 track, f32 percent); // 0-1
KAPI void animation_track_playback_speed(animation_system_state* state, kanimation animation, u8 track, f32 speed); // 1.0 is normal, 2.0 is double, etc.

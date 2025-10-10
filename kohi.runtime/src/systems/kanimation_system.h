#pragma once

#include "core/frame_data.h"
#include "core_render_types.h"
#include "defines.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "memory/allocators/pool_allocator.h"
#include "renderer/renderer_types.h"
#include "strings/kname.h"

#define KANIMATION_MAX_BONES 64
#define KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL "Kohi.StorageBuffer.AnimationsGlobal"

typedef struct anim_key_vec3 {
    vec3 value;
    f32 time;
} anim_key_vec3;

typedef struct anim_key_quat {
    quat value;
    f32 time;
} anim_key_quat;

// Animation channel for a node.
typedef struct kanimated_mesh_channel {
    kname name;
    u32 pos_count;
    anim_key_vec3* positions;
    u32 scale_count;
    anim_key_vec3* scales;
    u32 rot_count;
    anim_key_quat* rotations;
} kanimated_mesh_channel;

// Animation that contains channels.
typedef struct kanimated_mesh_animation {
    kname name;
    f32 duration;
    f32 ticks_per_second;
    u32 channel_count;
    kanimated_mesh_channel* channels;
} kanimated_mesh_animation;

// Bone data
typedef struct kanimated_mesh_bone {
    kname name;
    // Transformation from mesh space to bone space.
    mat4 offset;
    // Index into bone array.
    u32 id;
} kanimated_mesh_bone;

typedef struct kanimated_mesh_node {
    kname name;
    mat4 local_transform;
    u32 parent_index; // INVALID_ID = root
    u32 child_count;
    u32* children;
} kanimated_mesh_node;

typedef struct kanimated_mesh {
    kname name;
    kgeometry geo;
    kname material_name;
} kanimated_mesh;

// This is the "base" animated mesh, queried by all animators/instances
typedef struct kanimated_mesh_base {
    u16 id;
    kname asset_name;
    kname package_name;
    u32 animation_count;
    kanimated_mesh_animation* animations;
    u32 bone_count;
    kanimated_mesh_bone* bones;
    u32 node_count;
    kanimated_mesh_node* nodes;

    mat4 global_inverse_transform;

    u32 mesh_count;
    kanimated_mesh* meshes;
} kanimated_mesh_base;

typedef struct kanimated_mesh_animation_shader_data {
    mat4 final_bone_matrices[KANIMATION_MAX_BONES];
} kanimated_mesh_animation_shader_data;

typedef enum kanimated_mesh_animator_state {
    KANIMATED_MESH_ANIMATOR_STATE_STOPPED,
    KANIMATED_MESH_ANIMATOR_STATE_PLAYING,
    KANIMATED_MESH_ANIMATOR_STATE_PAUSED
} kanimated_mesh_animator_state;

// One animator = one animated mesh instance state
typedef struct kanimated_mesh_animator {
    kname name;
    // Index of the base mesh
    u16 base;
    // Index into the animation array. INVALID_ID_U16 = no current animation.
    u16 current_animation;
    f32 time_in_ticks;
    f32 time_scale;
    b8 loop;
    kanimated_mesh_animator_state state;
    // Pointer to shader_data array where data is stored.
    kanimated_mesh_animation_shader_data* shader_data;
    u32 max_bones;
} kanimated_mesh_animator;

typedef struct kanimated_mesh_instance_data {
    kanimated_mesh_animator animator;
    // NOTE: Size aligns with base mesh submesh count.
    kmaterial_instance* materials;
} kanimated_mesh_instance_data;

typedef struct kanimated_mesh_instance {
    u16 base_mesh;
    u16 instance;
} kanimated_mesh_instance;

typedef struct kanimated_mesh_system_config {
    kname default_application_package_name;
    // Max number of instances shared across all meshes.
    u16 max_instance_count;
} kanimated_mesh_system_config;

typedef struct kanimated_mesh_system_state {
    kname default_application_package_name;
    // Max number of instances shared across all meshes.
    u16 max_instance_count;

    f32 global_time_scale;

    // darray Base meshes.
    kanimated_mesh_base* base_meshes;

    // darray First dimension matches base_meshes (indexed by base mesh id).
    // Second dimension is a darray indexed by instance id.
    kanimated_mesh_instance_data** instances;

    krenderbuffer global_animation_ssbo;

    // Element count = max_instance_count
    pool_allocator shader_data_pool;
    kanimated_mesh_animation_shader_data* shader_data;
} kanimated_mesh_system_state;

typedef void (*PFN_animated_mesh_loaded)(kanimated_mesh_instance instance, void* context);

b8 kanimated_mesh_system_initialize(u64* memory_requirement, kanimated_mesh_system_state* memory, const kanimated_mesh_system_config* config);
void kanimated_mesh_system_shutdown(kanimated_mesh_system_state* state);

void kanimated_mesh_system_update(kanimated_mesh_system_state* state, f32 delta_time, frame_data* p_frame_data);
void kanimated_mesh_system_frame_prepare(kanimated_mesh_system_state* state, frame_data* p_frame_data);

KAPI void kanimated_mesh_system_time_scale(kanimated_mesh_system_state* state, f32 time_scale); // 1.0 - normal

KAPI kanimated_mesh_instance kanimated_mesh_instance_acquire(struct kanimated_mesh_system_state* state, kname asset_name, PFN_animated_mesh_loaded callback, void* context);
KAPI kanimated_mesh_instance kanimated_mesh_instance_acquire_from_package(struct kanimated_mesh_system_state* state, kname asset_name, kname package_name, PFN_animated_mesh_loaded callback, void* context);
// NOTE: Also releases held material instances.
KAPI void kanimated_mesh_instance_release(struct kanimated_mesh_system_state* state, kanimated_mesh_instance* instance);

// NOTE: Returns dynamic array, needs to be freed by caller.
KAPI kname* kanimated_mesh_query_animations(struct kanimated_mesh_system_state* state, u16 base_mesh, u32* out_count);

KAPI void kanimated_mesh_instance_animation_set(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, kname animation_name);

KAPI void kanimated_mesh_instance_time_scale_set(kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 time_scale); // 1.0 - normal
KAPI void kanimated_mesh_instance_loop_set(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, b8 loop);
KAPI void kanimated_mesh_instance_play(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance);
KAPI void kanimated_mesh_instance_pause(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance);
KAPI void kanimated_mesh_instance_stop(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance);
KAPI void kanimated_mesh_instance_seek(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 time);            // 0-total animation track time
KAPI void kanimated_mesh_instance_seek_percent(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 percent); // 0-1

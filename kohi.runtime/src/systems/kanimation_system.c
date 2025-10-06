#include "kanimation_system.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/allocators/pool_allocator.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/asset_system.h"
#include "systems/kmaterial_system.h"

b8 kanimated_mesh_system_initialize(u64* memory_requirement, kanimated_mesh_system_state* memory, const kanimated_mesh_system_config* config) {
    u32 max_instance_count = config->max_instance_count ? config->max_instance_count : 100;
    *memory_requirement = sizeof(kanimated_mesh_system_state);

    if (!memory) {
        return true;
    }

    kanimated_mesh_system_state* state = memory;

    state->default_application_package_name = config->default_application_package_name;
    state->max_instance_count = max_instance_count;

    state->base_meshes = darray_create(kanimated_mesh_base);
    state->instances = darray_create(kanimated_mesh_animator*);

    state->global_time_scale = 1.0f;

    // Global lighting storage buffer
    u64 buffer_size = sizeof(mat4) * state->max_instance_count;
    state->global_animation_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT);
    KASSERT(state->global_animation_ssbo != KRENDERBUFFER_INVALID);
    KDEBUG("Created kanimation global storage buffer.");

    // The free states of instances here are managed by a pool allocator.
    state->shader_data_pool = pool_allocator_create(sizeof(kanimated_mesh_animation_shader_data), state->max_instance_count);
    state->shader_data = state->shader_data_pool.memory;

    return true;
}

void kanimated_mesh_system_shutdown(kanimated_mesh_system_state* state) {
    // TODO: release all instances.
    // TODO: release all base meshes. (including unload from GPU)

    if (state) {
        renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, state->global_animation_ssbo);
    }
}

void kanimated_mesh_system_update(kanimated_mesh_system_state* state, frame_data* p_frame_data) {
    // TODO: Iterate all mesh instances and update thier final_bone_matrices.
    // TODO: Active flag??
}

void kanimated_mesh_system_frame_prepare(kanimated_mesh_system_state* state, frame_data* p_frame_data) {
    // TODO: Upload all of the mesh instance final_bone_matrices to the SSBO.
}

void kanimated_mesh_system_time_scale(kanimated_mesh_system_state* state, f32 time_scale) {
    KASSERT_DEBUG(state);
    state->global_time_scale = time_scale;
}

kanimated_mesh_instance kanimated_mesh_instance_acquire(struct kanimated_mesh_system_state* state, kname asset_name, PFN_animated_mesh_loaded callback, void* context) {
    return kanimated_mesh_instance_acquire_from_package(state, asset_name, state->default_application_package_name, callback, context);
}

// Returns true if already exists; otherwise false.
static b8 get_base_id(struct kanimated_mesh_system_state* state, kname asset_name, kname package_name, u16* out_id) {
    // Search for currently loaded/existing assets for a match first.
    u32 len = darray_length(state->base_meshes);
    for (u32 i = 0; i < len; ++i) {
        kanimated_mesh_base* base = &state->base_meshes[i];
        if (base->asset_name == asset_name && base->package_name == package_name) {
            *out_id = (u16)i;
            return true;
        }
    }

    u16 id = INVALID_ID_U16;
    // If one does not exist, create a new one.
    // First look for an empty slot.
    for (u32 i = 0; i < len; ++i) {
        kanimated_mesh_base* base = &state->base_meshes[i];
        if (base->id == INVALID_ID_U16) {
            // Free slot found, use it.
            id = (u16)i;
            break;
        }
    }

    // If no empty slot, it's an error as there is no more room.
    if (id == INVALID_ID_U16) {
        kanimated_mesh_base dummy = {0};
        darray_push(state->base_meshes, dummy);
        id = (u16)len;
    }

    kanimated_mesh_base* new_base = &state->base_meshes[id];
    new_base->asset_name = asset_name;
    new_base->package_name = package_name;
    new_base->id = id;

    // Also push a new entry into the instance array for this base.
    kanimated_mesh_animator* new_inst_array = darray_create(kanimated_mesh_animator);
    darray_push(state->instances, new_inst_array);

    *out_id = id;
    return false;
}

static u16 get_new_instance_id(struct kanimated_mesh_system_state* state, u16 base_id) {
    kanimated_mesh_base* base = &state->base_meshes[base_id];

    u16 len = darray_length(state->instances[base_id]);
    for (u16 i = 0; i < len; ++i) {
        if (state->instances[base_id][i].base == INVALID_ID_U16) {
            // Free slot found, use it.
            state->instances[base_id][i].base = base_id;
            state->instances[base_id][i].current_animation = INVALID_ID_U16;
            state->instances[base_id][i].shader_data = pool_allocator_allocate(&state->shader_data_pool);
            return i;
        }
    }

    kanimated_mesh_animator new_inst = {0};
    new_inst.base = base_id;
    new_inst.current_animation = INVALID_ID_U16;
    new_inst.shader_data = pool_allocator_allocate(&state->shader_data_pool);
    darray_push(state->instances[base_id], new_inst);

    return len;
}

typedef struct animated_mesh_asset_request_listener {
    kanimated_mesh_system_state* state;
    u16 base_id;
    u16 instance_id;
} animated_mesh_asset_request_listener;

static void kasset_animated_mesh_loaded(void* listener, kasset_animated_mesh* asset) {
    animated_mesh_asset_request_listener* typed_listener = (animated_mesh_asset_request_listener*)listener;

    kanimated_mesh_system_state* state = typed_listener->state;
    u16 base_id = typed_listener->base_id;
    u16 instance_id = typed_listener->instance_id;

    // Base mesh setup.
    kanimated_mesh_base* base = &state->base_meshes[base_id];
    base->global_inverse_transform = asset->global_inverse_transform;

    // NOTE: All these copies are here because the asset and state types here might diverge at some point.
    // If this doesn't wind up happening, the types could be unified and this could be copied all at once instead.
    base->bone_count = asset->bone_count;
    base->bones = KALLOC_TYPE_CARRAY(kanimated_mesh_bone, base->bone_count);
    for (u32 i = 0; i < base->bone_count; ++i) {
        kasset_animated_mesh_bone* source = &asset->bones[i];
        kanimated_mesh_bone* target = &base->bones[i];
        target->id = source->id;
        target->name = source->name;
        target->offset = source->offset;
    }

    base->node_count = asset->node_count;
    base->nodes = KALLOC_TYPE_CARRAY(kanimated_mesh_node, base->node_count);
    for (u32 i = 0; i < base->node_count; ++i) {
        kasset_animated_mesh_node* source = &asset->nodes[i];
        kanimated_mesh_node* target = &base->nodes[i];

        target->name = source->name;
        target->parent_index = source->parent_index;
        target->local_transform = source->local_transform;
        target->child_count = source->child_count;
        if (target->child_count) {
            target->children = KALLOC_TYPE_CARRAY(u32, target->child_count);
            KCOPY_TYPE_CARRAY(target->children, source->children, u32, target->child_count);
        }
    }

    base->animation_count = asset->animation_count;
    base->animations = KALLOC_TYPE_CARRAY(kanimated_mesh_animation, base->animation_count);
    for (u32 i = 0; i < base->animation_count; ++i) {
        kasset_animated_mesh_animation* source = &asset->animations[i];
        kanimated_mesh_animation* target = &base->animations[i];

        target->name = source->name;
        target->ticks_per_second = source->ticks_per_second;
        target->duration = source->duration;
        target->channel_count = source->channel_count;
        if (target->channel_count) {
            target->channels = KALLOC_TYPE_CARRAY(kanimated_mesh_channel, target->channel_count);

            for (u32 c = 0; c < target->channel_count; c++) {
                kasset_animated_mesh_channel* sc = &source->channels[c];
                kanimated_mesh_channel* tc = &target->channels[c];

                tc->name = sc->name;
                tc->pos_count = sc->pos_count;
                if (tc->pos_count) {
                    tc->positions = KALLOC_TYPE_CARRAY(anim_key_vec3, tc->pos_count);
                    for (u32 k = 0; k < tc->pos_count; ++k) {
                        tc->positions[k].time = sc->positions[k].time;
                        tc->positions[k].value = sc->positions[k].value;
                    }
                }
                tc->rot_count = sc->rot_count;
                if (tc->rot_count) {
                    tc->rotations = KALLOC_TYPE_CARRAY(anim_key_quat, tc->rot_count);
                    for (u32 k = 0; k < tc->rot_count; ++k) {
                        tc->rotations[k].time = sc->rotations[k].time;
                        tc->rotations[k].value = sc->rotations[k].value;
                    }
                }
                tc->scale_count = sc->scale_count;
                if (tc->scale_count) {
                    tc->scales = KALLOC_TYPE_CARRAY(anim_key_vec3, tc->scale_count);
                    for (u32 k = 0; k < tc->scale_count; ++k) {
                        tc->scales[k].time = sc->scales[k].time;
                        tc->scales[k].value = sc->scales[k].value;
                    }
                }
            }
        }
    }

    struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system;
    krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_GLOBAL_VERTEX));
    krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_GLOBAL_INDEX));

    // Finally, the meshes
    base->mesh_count = asset->submesh_count;
    base->meshes = KALLOC_TYPE_CARRAY(kanimated_mesh, base->mesh_count);
    for (u32 i = 0; i < base->mesh_count; ++i) {
        kanimated_mesh* target = &base->meshes[i];
        kasset_animated_mesh_submesh_data* source = &asset->submeshes[i];

        target->name = source->name;
        target->geo.name = source->name;
        target->geo.generation = INVALID_ID_U16;
        target->geo.type = KGEOMETRY_TYPE_3D_SKINNED;

        target->geo.vertex_element_size = sizeof(skinned_vertex_3d);
        target->geo.vertex_count = source->vertex_count;
        target->geo.vertices = KALLOC_TYPE_CARRAY(skinned_vertex_3d, source->vertex_count);
        KCOPY_TYPE_CARRAY(target->geo.vertices, source->vertices, skinned_vertex_3d, source->vertex_count);

        target->geo.index_element_size = sizeof(u32);
        target->geo.index_count = source->index_count;
        target->geo.indices = KALLOC_TYPE_CARRAY(skinned_vertex_3d, source->index_count);
        KCOPY_TYPE_CARRAY(target->geo.indices, source->indices, u32, source->index_count);

        // Extract the extents.
        vec3 min_pos = vec3_zero();
        vec3 max_pos = vec3_zero();
        for (u32 v = 0; v < source->vertex_count; ++v) {
            min_pos = vec3_min(min_pos, source->vertices[v].position);
            max_pos = vec3_max(max_pos, source->vertices[v].position);
        }
        target->geo.extents.min = min_pos;
        target->geo.extents.max = max_pos;
        target->geo.center = extents_3d_center(target->geo.extents);

        // Acquire material instance.
        if (!kmaterial_system_acquire(engine_systems_get()->material_system, source->material_name, &target->material)) {
            KERROR("Failed to get material '%s' for animated mesh submesh '%s'.", kname_string_get(source->material_name), kname_string_get(source->name));
            // TODO: Should this just use the default material instead?
        }

        // Upload the geometry.
        u64 vertex_size = (u64)(sizeof(skinned_vertex_3d) * source->vertex_count);
        u64 vertex_offset = 0;
        u64 index_size = (u64)(sizeof(u32) * source->index_count);
        u64 index_offset = 0;

        // Vertex data.
        if (!renderer_renderbuffer_allocate(renderer_system, vertex_buffer, vertex_size, &target->geo.vertex_buffer_offset)) {
            KERROR("animated mesh system failed to allocate from the renderer's vertex buffer! Submesh geometry won't be uploaded (skipped)");
            continue;
        }

        // Load the data.
        // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
        if (!renderer_renderbuffer_load_range(renderer_system, vertex_buffer, target->geo.vertex_buffer_offset + vertex_offset, vertex_size, target->geo.vertices + vertex_offset, false)) {
            KERROR("skinned mesh system failed to upload to the renderer vertex buffer!");
            if (!renderer_renderbuffer_free(renderer_system, vertex_buffer, vertex_size, target->geo.vertex_buffer_offset)) {
                KERROR("Failed to recover from vertex write failure while freeing vertex buffer range.");
            }
            continue;
        }

        // Index data, if applicable
        if (index_size) {
            // Allocate space in the buffer.
            if (!renderer_renderbuffer_allocate(renderer_system, index_buffer, index_size, &target->geo.index_buffer_offset)) {
                KERROR("skinned mesh system failed to allocate from the renderer index buffer!");
                // Free vertex data
                if (!renderer_renderbuffer_free(renderer_system, vertex_buffer, vertex_size, target->geo.vertex_buffer_offset)) {
                    KERROR("Failed to recover from index allocation failure while freeing vertex buffer range.");
                }
                continue;
            }

            // Load the data.
            // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
            if (!renderer_renderbuffer_load_range(renderer_system, index_buffer, target->geo.index_buffer_offset + index_offset, index_size, target->geo.indices + index_offset, false)) {
                KERROR("skinned mesh system failed to upload to the renderer index buffer!");
                // Free vertex data
                if (!renderer_renderbuffer_free(renderer_system, vertex_buffer, vertex_size, target->geo.vertex_buffer_offset)) {
                    KERROR("Failed to recover from index write failure while freeing vertex buffer range.");
                }
                // Free index data
                if (!renderer_renderbuffer_free(renderer_system, index_buffer, index_size, target->geo.index_buffer_offset)) {
                    KERROR("Failed to recover from index write failure while freeing index buffer range.");
                }
                continue;
            }
        }

        target->geo.generation++;
    }

    // Instance setup.
    kanimated_mesh_animator* instance = &state->instances[base_id][instance_id];
    instance->max_bones = asset->bone_count;
    instance->time_in_ticks = 0.0f;

    // After copying over all properties, release the asset.
    asset_system_release_animated_mesh(engine_systems_get()->asset_state, asset);

    // Cleanup the listener.
    KFREE_TYPE(listener, animated_mesh_asset_request_listener, MEMORY_TAG_ASSET);
}

kanimated_mesh_instance kanimated_mesh_instance_acquire_from_package(struct kanimated_mesh_system_state* state, kname asset_name, kname package_name, PFN_animated_mesh_loaded callback, void* context) {
    // Obtain a unique id for lookup into the resource arrays.
    u16 base_id = INVALID_ID_U16;
    b8 exists = get_base_id(state, asset_name, package_name, &base_id);

    // Always get a new instance.
    u16 instance_id = get_new_instance_id(state, base_id);

    // If the base didn't exist, will need to kick off an asset load.
    if (!exists) {
        animated_mesh_asset_request_listener* listener = KALLOC_TYPE(animated_mesh_asset_request_listener, MEMORY_TAG_ASSET);
        listener->state = state;
        listener->base_id = base_id;
        listener->instance_id = instance_id;

        // Kick off async asset load via the asset system.
        kasset_animated_mesh* asset = asset_system_request_animated_mesh_from_package(engine_systems_get()->asset_state, kname_string_get(package_name), kname_string_get(asset_name), listener, kasset_animated_mesh_loaded);
        KASSERT_DEBUG(asset);
    }

    return (kanimated_mesh_instance){
        .base_mesh = base_id,
        .instance = instance_id};
}

static u16 get_active_instance_count(struct kanimated_mesh_system_state* state, u16 base_id) {
    kanimated_mesh_base* base = &state->base_meshes[base_id];

    u32 len = darray_length(state->instances);
    u16 count = 0;
    for (u32 i = 0; i < len; ++i) {
        kanimated_mesh_animator* instance = &state->instances[base_id][i];
        if (instance->base == base_id) {
            count++;
        }
    }
    return count;
}

// NOTE: Also releases held material instances.
void kanimated_mesh_instance_release(struct kanimated_mesh_system_state* state, kanimated_mesh_instance* instance) {
    kanimated_mesh_animator* inst = &state->instances[instance->base_mesh][instance->instance];

    kzero_memory(inst, sizeof(kanimated_mesh_animator));
    inst->base = INVALID_ID_U16;
    inst->current_animation = INVALID_ID_U16;
    if (inst->shader_data) {
        pool_allocator_free(&state->shader_data_pool, inst->shader_data);
        inst->shader_data = 0;
    }

    u16 active_count = get_active_instance_count(state, instance->base_mesh);
    if (!active_count) {

        struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system;
        krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_GLOBAL_VERTEX));
        krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_GLOBAL_INDEX));

        // Unload submeshes from GPU.
        kanimated_mesh_base* base = &state->base_meshes[instance->base_mesh];
        for (u32 i = 0; i < base->mesh_count; ++i) {
            kanimated_mesh* m = &base->meshes[i];

            u64 vert_buf_size = m->geo.vertex_element_size * m->geo.vertex_count;
            if (!renderer_renderbuffer_free(renderer_system, vertex_buffer, vert_buf_size, m->geo.vertex_buffer_offset)) {
                KWARN("Failed to release vertex data for animated mesh. See logs for details.");
            }

            u64 index_buf_size = m->geo.index_element_size * m->geo.index_count;
            if (!renderer_renderbuffer_free(renderer_system, index_buffer, vert_buf_size, m->geo.index_buffer_offset)) {
                KWARN("Failed to release index data for animated mesh. See logs for details.");
            }

            kfree(m->geo.vertices, vert_buf_size, MEMORY_TAG_ARRAY);
            kfree(m->geo.indices, index_buf_size, MEMORY_TAG_ARRAY);

            kzero_memory(&m->geo, sizeof(kgeometry));
        }

        KFREE_TYPE_CARRAY(base->meshes, kanimated_mesh, base->mesh_count);

        // Cleanup animations.
        if (base->animation_count && base->animations) {
            for (u32 i = 0; i < base->animation_count; ++i) {
                kanimated_mesh_animation* anim = &base->animations[i];

                if (anim->channels && anim->channel_count) {
                    for (u32 c = 0; c < anim->channel_count; c++) {
                        kanimated_mesh_channel* ch = &anim->channels[c];

                        if (ch->pos_count && ch->positions) {
                            KFREE_TYPE_CARRAY(ch->positions, anim_key_vec3, ch->pos_count);
                        }

                        if (ch->scale_count && ch->scales) {
                            KFREE_TYPE_CARRAY(ch->scales, anim_key_vec3, ch->scale_count);
                        }

                        if (ch->rot_count && ch->rotations) {
                            KFREE_TYPE_CARRAY(ch->rotations, anim_key_quat, ch->rot_count);
                        }
                    }

                    KFREE_TYPE_CARRAY(anim->channels, kanimated_mesh_channel, anim->channel_count);
                }
            }

            KFREE_TYPE_CARRAY(base->animations, kanimated_mesh_animation, base->animation_count);
        }

        if (base->node_count && base->nodes) {
            for (u32 i = 0; i < base->node_count; ++i) {
                kanimated_mesh_node* node = &base->nodes[i];

                if (node->child_count && node->children) {
                    KFREE_TYPE_CARRAY(node->children, u32, node->child_count);
                }
            }

            KFREE_TYPE_CARRAY(base->nodes, kanimated_mesh_node, base->node_count);
        }

        if (base->bone_count && base->bones) {
            KFREE_TYPE_CARRAY(base->bones, kanimated_mesh_bone, base->bone_count);
        }

        kzero_memory(base, sizeof(kanimated_mesh_base));

        base->id = INVALID_ID_U16;
    }
}

// NOTE: Returns dynamic array, needs to be freed by caller.
kname* kanimated_mesh_query_animations(struct kanimated_mesh_system_state* state, u16 base_mesh, u32* out_count) {
    u32 count = state->base_meshes[base_mesh].animation_count;

    kname* anim_names = KALLOC_TYPE_CARRAY(kname, count);
    for (u32 i = 0; i < count; ++i) {
        anim_names[i] = state->base_meshes[base_mesh].animations[i].name;
    }

    *out_count = count;
    return anim_names;
}

void kanimated_mesh_instance_animation_set(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, kname animation_name) {
    kanimated_mesh_base* base = &state->base_meshes[instance.base_mesh];
    kanimated_mesh_animator* inst = &state->instances[instance.base_mesh][instance.instance];

    u32 count = base->animation_count;
    for (u32 i = 0; i < count; ++i) {
        if (base->animations[i].name == animation_name) {
            KTRACE("Animation '%s' now active on base mesh '%s'.", kname_string_get(base->animations[i].name), kname_string_get(base->asset_name));
            inst->current_animation = i;
            break;
        }
    }

    KWARN("Animation '%s' not found on base mesh '%s'.", kname_string_get(animation_name), kname_string_get(base->asset_name));
    if (inst->current_animation == INVALID_ID_U16) {
        if (base->animation_count > 0) {
            inst->current_animation = 0;
            KWARN("Set animation to default of the first entry, '%s'.", kname_string_get(base->animations[0].name));
        } else {
            KWARN("No animations exist, thus there is nothing to set.");
        }
    }
}

void kanimated_mesh_instance_time_scale_set(kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 time_scale) {
    /* kanimated_mesh_base* base = &state->base_meshes[instance.base_mesh]; */
    /* kanimated_mesh_animator* inst = &state->instances[instance.base_mesh][instance.instance]; */
    // TODO: set instance timescale
}

void kanimated_mesh_instance_loop_set(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, b8 loop) {
    // TODO: enable/disable looping.
}
void kanimated_mesh_instance_play(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance) {
    // TODO: set instance state to play
}
void kanimated_mesh_instance_pause(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance) {
    // TODO: set instance state to pause
}
void kanimated_mesh_instance_stop(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance) {
    // TODO: set instance state to stopped, reset time to 0.
}
void kanimated_mesh_instance_seek(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 time) {
    // TODO: Set instance time, but clamp within range (maybe mod?). Loop if enabled?
}
void kanimated_mesh_instance_seek_percent(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 percent) {
    // TODO: clamp 0-1, set to percent of duration.
}
void kanimated_mesh_instance_playback_speed(struct kanimated_mesh_system_state* state, kanimated_mesh_instance instance, f32 speed) {
    // TODO: Same as time scale.
}

static kanimated_mesh_channel* kanimation_find_channel(kanimated_mesh_animation* animation, kname node_name) {
    for (u32 i = 0; i < animation->channel_count; ++i) {
        if (animation->channels[i].name == node_name) {
            return &animation->channels[i];
        }
    }
    return 0;
}

static u32 base_find_node_index(kanimated_mesh_base* base, kname name) {
    for (u32 i = 0; i < base->node_count; ++i) {
        if (base->nodes[i].name == name) {
            return i;
        }
    }

    return INVALID_ID;
}

static u32 base_find_bone_index(kanimated_mesh_base* base, kname name) {
    for (u32 i = 0; i < base->bone_count; ++i) {
        if (base->bones[i].name == name) {
            return i;
        }
    }

    return INVALID_ID;
}

static vec3 interpolate_position(const kanimated_mesh_channel* channel, f32 time) {
    if (!channel->pos_count) {
        return vec3_zero();
    }
    if (channel->pos_count == 1) {
        return channel->positions[0].value;
    }

    u32 idx = 0;
    while (idx + 1 < channel->pos_count && time >= channel->positions[idx + 1].time) {
        idx++;
    }
    if (idx + 1 == channel->pos_count) {
        return channel->positions[channel->pos_count - 1].value;
    }

    f32 t0 = channel->positions[idx + 0].time;
    f32 t1 = channel->positions[idx + 1].time;
    f32 factor = (f32)((time - t0) / (t1 - t0));
    return vec3_lerp(channel->positions[idx].value, channel->positions[idx + 1].value, factor);
}

static quat interpolate_rotation(const kanimated_mesh_channel* channel, f32 time) {
    if (!channel->rot_count) {
        return quat_identity();
    }
    if (channel->rot_count == 1) {
        return channel->rotations[0].value;
    }

    u32 idx = 0;
    while (idx + 1 < channel->rot_count && time >= channel->rotations[idx + 1].time) {
        idx++;
    }
    if (idx + 1 == channel->rot_count) {
        return channel->rotations[channel->rot_count - 1].value;
    }

    f32 t0 = channel->rotations[idx + 0].time;
    f32 t1 = channel->rotations[idx + 1].time;
    f32 factor = (f32)((time - t0) / (t1 - t0));
    return quat_slerp(channel->rotations[idx].value, channel->rotations[idx + 1].value, factor);
}

static vec3 interpolate_scale(const kanimated_mesh_channel* channel, f32 time) {
    if (!channel->scale_count) {
        return vec3_zero();
    }
    if (channel->scale_count == 1) {
        return channel->scales[0].value;
    }

    u32 idx = 0;
    while (idx + 1 < channel->scale_count && time >= channel->scales[idx + 1].time) {
        idx++;
    }
    if (idx + 1 == channel->scale_count) {
        return channel->scales[channel->scale_count - 1].value;
    }

    f32 t0 = channel->scales[idx + 0].time;
    f32 t1 = channel->scales[idx + 1].time;
    f32 factor = (f32)((time - t0) / (t1 - t0));
    return vec3_lerp(channel->scales[idx].value, channel->scales[idx + 1].value, factor);
}

static void process_animator(kanimated_mesh_system_state* state, kanimated_mesh_animator* animator, kanimated_mesh_animation* animation, u32 node_index, const mat4 parent_transform) {
    kanimated_mesh_base* asset = &state->base_meshes[animator->base];
    kanimated_mesh_node* node = &asset->nodes[node_index];
    mat4 node_transform = node->local_transform;

    kanimated_mesh_channel* channel = kanimation_find_channel(animation, node->name);
    if (channel) {
        vec3 translation = interpolate_position(channel, animator->time_in_ticks);
        quat rotation = interpolate_rotation(channel, animator->time_in_ticks);
        vec3 scale = interpolate_scale(channel, animator->time_in_ticks);
        node_transform = mat4_from_translation_rotation_scale(translation, rotation, scale);
    }

    mat4 world_transform = mat4_mul(parent_transform, node_transform);

    u32 bone_index = base_find_bone_index(asset, node->name);
    if (bone_index != INVALID_ID) {
        mat4 final_matrix = mat4_mul(asset->global_inverse_transform, world_transform);
        final_matrix = mat4_mul(final_matrix, asset->bones[bone_index].offset);
        if (bone_index < animator->max_bones) {
            animator->shader_data->final_bone_matrices[bone_index] = final_matrix;
        }
    }

    // Recurse children.
    for (u32 i = 0; i < node->child_count; ++i) {
        u32 ci = node->children[i];
        process_animator(state, animator, animation, ci, world_transform);
    }
}

static void animator_create(kanimated_mesh_base* asset, kanimated_mesh_animator* out_animator) {
    out_animator->base = asset->id;
    out_animator->current_animation = (asset->animation_count > 0) ? 0 : INVALID_ID_U16;
    out_animator->time_in_ticks = 0.0f;
    out_animator->max_bones = asset->bone_count;
    for (u32 i = 0; i < KANIMATION_MAX_BONES; ++i) {
        out_animator->shader_data->final_bone_matrices[i] = mat4_identity();
    }
}

static void animator_set_animation(kanimated_mesh_system_state* state, kanimated_mesh_animator* animator, u16 index) {
    kanimated_mesh_base* base = &state->base_meshes[animator->base];
    if (index >= base->animation_count) {
        return;
    }

    animator->current_animation = index;
    animator->time_in_ticks = 0.0f;
}

static void animator_update(kanimated_mesh_system_state* state, kanimated_mesh_animator* animator, f32 delta_time) {
    if (animator->current_animation == INVALID_ID_U16) {
        return;
    }
    kanimated_mesh_base* base = &state->base_meshes[animator->base];
    kanimated_mesh_animation* current = &base->animations[animator->current_animation];
    f32 ticks_per_second = current->ticks_per_second;
    f32 delta_ticks = delta_time * ticks_per_second;
    animator->time_in_ticks += delta_ticks;

    // Wrap around.
    f32 duration = current->duration;
    if (duration > 0.0f) {
        animator->time_in_ticks += kmod(animator->time_in_ticks, duration);
        if (animator->time_in_ticks < 0.0f) {
            animator->time_in_ticks += duration;
        }
    }

    // Process the hierarchy starting at the root.
    for (u32 i = 0; i < base->node_count; ++i) {
        if (base->nodes[i].parent_index == INVALID_ID) {
            process_animator(state, animator, current, i, mat4_identity());
        }
    }
}

static void animator_get_bone_transforms(kanimated_mesh_system_state* state, kanimated_mesh_animator* animator, u32 count, mat4* out_transforms) {
    kanimated_mesh_base* base = &state->base_meshes[animator->base];
    u32 n = base->bone_count;
    if (count < n) {
        n = count;
    }

    for (u32 i = 0; i < n; ++i) {
        out_transforms[i] = animator->shader_data->final_bone_matrices[i];
    }
}

#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "systems/kmaterial_system.h"

#define KMATERIAL_UBO_MAX_VIEWS 16
#define KMATERIAL_UBO_MAX_PROJECTIONS 4
#define KMATERIAL_UBO_MAX_SHADOW_CASCADES 4

#define KRENDERBUFFER_NAME_MATERIALS_GLOBAL "Kohi.StorageBuffer.MaterialsGlobal"

/**
 * The uniform data for a light. 32 bytes.
 * Can be used for point or directional lights.
 */
typedef struct klight_shader_data {
    // Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear
    vec4 colour;
    // Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
    vec4 position;
} klight_shader_data;

typedef struct base_material_shader_data {
    u32 metallic_texture_channel;
    u32 roughness_texture_channel;
    u32 ao_texture_channel;
    /** @brief The material lighting model. */
    u32 lighting_model;

    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    // Texture use flags
    u32 tex_flags;
    f32 refraction_scale;
    u32 material_type;

    vec4 base_colour;
    vec4 emissive;

    vec3 normal;
    f32 metallic;

    vec3 mra;
    f32 roughness;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    f32 ao;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;
    f32 emissive_texture_intensity;
} base_material_shader_data;

// LEFTOFF: Integrate the above into a "global material SSBO" and use with the
// renderer. Then combine all material types into a single shader. Then use that
// shader, with this SSBO, to render.
// Note that the vertex shader for animated/skinned meshes will have to differ though.
//

typedef struct kmaterial_settings_ubo {
    f32 delta_time;
    f32 game_time;
    u32 render_mode;
    u32 use_pcf;

    // Shadow settings
    f32 shadow_bias;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;

    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
    vec4 cascade_splits;                                              // 16 bytes

    vec4 view_positions[KMATERIAL_UBO_MAX_VIEWS];    // indexed by immediate.view_index
    mat4 views[KMATERIAL_UBO_MAX_VIEWS];             // indexed by immediate.view_index
    mat4 projections[KMATERIAL_UBO_MAX_PROJECTIONS]; // indexed by immediate.projection_index
} kmaterial_settings_ubo;

typedef struct kmaterial_render_immediate_data {
    // bytes 0-15
    // Index into global ubo views
    u32 view_index;
    // Index into global ubo projections
    u32 projection_index;
    // Handle to transform.
    u32 transform_index;
    // Handle to base material
    u32 base_material_index;

    // bytes 16-31
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
    uvec2 packed_point_light_indices; // 8 bytes
    u32 num_p_lights;
    // Index into global irradiance cubemap texture array
    u32 irradiance_cubemap_index;

    // bytes 32-47
    vec4 clipping_plane;

    // bytes 48-63
    u32 dir_light_index; // probably zero
    f32 tiling;          // only used for water materials
    f32 wave_strength;   // only used for water materials
    f32 wave_speed;      // only used for water materials

    // 64-127 available
} kmaterial_render_immediate_data;

/** @brief State for the material renderer. */
typedef struct kmaterial_renderer {
    // Global storage buffer used for rendering materials.
    krenderbuffer material_global_ssbo;

    ktexture shadow_map_texture;
    u8 ibl_cubemap_texture_count;
    ktexture ibl_cubemap_textures[KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];

    // Pointer to use for material texture inputs _not_ using a texture map (because something has to be bound).
    ktexture default_texture;
    ktexture default_base_colour_texture;
    ktexture default_spec_texture;
    ktexture default_normal_texture;
    // Pointer to a default cubemap to fall back on if no IBL cubemaps are present.
    ktexture default_ibl_cubemap;
    ktexture default_mra_texture;
    ktexture default_water_normal_texture;
    ktexture default_water_dudv_texture;

    kshader material_standard_shader;
    u32 material_standard_shader_bs_0_instance_id;
    kshader material_standard_skinned_shader;
    u32 material_standard_skinned_shader_bs_0_instance_id;
    /* kshader material_water_shader; */
    /* u32 material_water_shader_bs_0_instance_id; */
    // FIXME: implement this
    kshader material_blended_shader;

    // Keep a pointer to various system states for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct kmaterial_system_state* material_state;

    u32 max_material_count;

    // Renderer state settings.
    kmaterial_settings_ubo settings;

    // Runtime package name pre-hashed and kept here for convenience.
    kname runtime_package_name;
} kmaterial_renderer;

KAPI b8 kmaterial_renderer_initialize(kmaterial_renderer* out_state, u32 max_material_count, u32 max_material_instance_count);
KAPI void kmaterial_renderer_shutdown(kmaterial_renderer* state);

KAPI void kmaterial_renderer_update(kmaterial_renderer* state);

// Sets material_data->group_id;
KAPI void kmaterial_renderer_register_base(kmaterial_renderer* state, kmaterial_data* material_data);
KAPI void kmaterial_renderer_unregister_base(kmaterial_renderer* state, kmaterial_data* material_data);

KAPI void kmaterial_renderer_register_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance);
KAPI void kmaterial_renderer_unregister_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance);

/* KINLINE void kmaterial_renderer_set_point_lights(kmaterial_renderer* state, u8 count, kpoint_light_render_data* lights) {
    u8 num_p_lights = KMIN(count, KMATERIAL_MAX_GLOBAL_POINT_LIGHTS);
    for (u8 i = 0; i < num_p_lights; ++i) {
        kpoint_light_render_data* pls = &lights[i];
        kpoint_light_shader_data* plt = &state->global_ubo_data.global_point_lights[i];
        plt->colour = vec4_from_vec3(pls->colour, pls->linear);
        plt->position = vec4_from_vec3(pls->position, pls->quadratic);
    }
} */

KAPI void kmaterial_renderer_set_irradiance_cubemap_textures(kmaterial_renderer* state, u8 count, ktexture* irradiance_cubemap_textures);

KAPI void kmaterial_renderer_apply_globals(kmaterial_renderer* state);

// Updates and binds base material.
KAPI void kmaterial_renderer_bind_base(kmaterial_renderer* state, kmaterial base);

// Updates material instance immediates using the provided data.
KAPI void kmaterial_renderer_apply_immediates(kmaterial_renderer* state, kmaterial_instance instance, const kmaterial_render_immediate_data* immediates);

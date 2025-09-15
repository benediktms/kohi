#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;
const uint KMATERIAL_UBO_MAX_VIEWS = 16;
const uint KMATERIAL_UBO_MAX_PROJECTIONS = 4;

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

const uint MATERIAL_WATER_TEXTURE_COUNT = 5;
const uint MATERIAL_WATER_SAMPLER_COUNT = 5;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;

struct light_data {
    // Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear 
    vec4 colour;
    // Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
    vec4 position;
};

struct base_material_data {
    uint metallic_texture_channel;
    uint roughness_texture_channel;
    uint ao_texture_channel;
    /** @brief The material lighting model. */
    uint lighting_model;

    // Base set of flags for the material. Copied to the material instance when created.
    uint flags;
    // Texture use flags
    uint tex_flags;
    float refraction_scale;
    float padding;

    vec4 base_colour;
    vec4 emissive;
    vec3 normal;
    float metallic;
    vec3 mra;
    float roughness;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    float ao;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;
    float emissive_texture_intensity;
};

struct animation_skin_data {
    mat4 bones[KANIMATION_SSBO_MAX_BONES_PER_MESH];
};

/** 
 * Used to convert from NDC -> UVW by taking the x/y components and transforming them:
 * 
 *   xy *= 0.5 + 0.5
 */
const mat4 ndc_to_uvw = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

// =========================================================
// Inputs
// =========================================================

// Vertex inputs
layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

// Binding Set 0

// Global settings for the scene.
layout(std140, set = 0, binding = 0) uniform kmaterial_settings_ubo {
    float delta_time;
    float game_time;
    uint render_mode;
    uint use_pcf;

    // Shadow settings
    float shadow_bias;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;

    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
    vec4 cascade_splits;                                         // 16 bytes

    vec4 view_positions[KMATERIAL_UBO_MAX_VIEWS]; // indexed by immediate.view_index
    mat4 views[KMATERIAL_UBO_MAX_VIEWS]; // indexed by immediate.view_index
    mat4 projections[KMATERIAL_UBO_MAX_PROJECTIONS]; // indexed by immediate.projection_index
} global_settings;

// All transforms
layout(std430, set = 0, binding = 1) readonly buffer global_transforms_ssbo {
    mat4 transforms[]; // indexed by immediate.transform_index
} global_transforms;

// All lighting
layout(std430, set = 0, binding = 2) readonly buffer global_lighting_ssbo {
    light_data lights[]; // indexed by immediate.packed_point_light_indices (needs unpacking to 16x u8s)
} global_lighting;

// All materials
layout(std430, set = 0, binding = 3) readonly buffer global_materials_ssbo {
    base_material_data base_materials[]; // indexed by immediate.transform_index
} global_materials;

// All animation data
layout(std430, set = 0, binding = 4) readonly buffer global_animations_ssbo {
    animation_skin_data animations[]; // indexed by immediate.animation_index;
} global_animations;


// Immediate data
layout(push_constant) uniform immediate_data {
    // bytes 0-15
    uint view_index;
    uint projection_index;
    uint transform_index;
    uint base_material_index;

    // bytes 16-31
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    uint irradiance_cubemap_index;

    // bytes 32-47
    vec4 clipping_plane;

    // bytes 48-63
    uint dir_light_index;
    float tiling;
    float wave_strength;
    float wave_speed;

    // 64-127 available
} immediate;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec4 frag_position;
	vec4 clip_space;
	vec4 light_space_frag_pos[KMATERIAL_UBO_MAX_SHADOW_CASCADES];
    vec4 vertex_colour;
	vec3 normal;
    float padding;
	vec3 tangent;
    float padding2;
	vec2 tex_coord;
	vec2 texcoord;
    vec3 world_to_camera;
    float padding3;
} out_dto;

void main() {
    mat4 model = global_transforms.transforms[immediate.transform_index];
    mat4 view = global_settings.views[immediate.view_index];
    mat4 projection = global_settings.projections[immediate.projection_index];

	vec4 world_position = model * in_position;
	out_dto.frag_position = world_position;
	out_dto.clip_space = projection * view * world_position;
	gl_Position = out_dto.clip_space;
	out_dto.texcoord = vec2((in_position.x * 0.5) + 0.5, (in_position.z * 0.5) + 0.5) * immediate.tiling;

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (ndc_to_uvw * global_settings.directional_light_spaces[i]) * world_position;
    }
	
	out_dto.world_to_camera = global_settings.view_positions[immediate.view_index].xyz - world_position.xyz;
}

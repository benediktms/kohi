#version 450

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

struct animation_skin_data {
    mat4 bones[KANIMATION_SSBO_MAX_BONES_PER_MESH];
};

// =========================================================
// Inputs
// =========================================================

// Vertex inputs

// Standard vertex data
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

// Binding Set 0

// Global settings for the scene.
layout(std140, set = 0, binding = 0) uniform depth_prepass_settings_ubo {
    mat4 projection;
    mat4 view;
} global_settings;

// All transforms
layout(std430, set = 0, binding = 1) readonly buffer global_transforms_ssbo {
    mat4 transforms[]; // indexed by immediate.transform_index
} global_transforms;

// All animation data
layout(std430, set = 0, binding = 2) readonly buffer global_animations_ssbo {
    animation_skin_data animations[]; // indexed by immediate.animation_index;
} global_animations;


// Immediate data
layout(push_constant) uniform immediate_data {
    uint transform_index;
} immediate;

void main() {
    mat4 model = global_transforms.transforms[immediate.transform_index];
    mat4 view = global_settings.view;
    mat4 projection = global_settings.projection;

	// Fragment position in world space.
	vec4 frag_position = model * vec4(in_position, 1.0);
    gl_Position = projection * view * frag_position;
}


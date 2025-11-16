#version 450

#define MAX_CASCADES 4

// =========================================================
// Inputs
// =========================================================

layout(set = 0, binding = 0) uniform global_ubo_data {
    mat4 view_projections[MAX_CASCADES];
} global_ubo;

// All transforms
layout(std430, set = 0, binding = 1) readonly buffer global_transforms_ssbo {
    mat4 transforms[]; // indexed by immediate.transform_index
} global_transforms;

layout (set = 1, binding = 0) uniform texture2D base_colour_texture;
layout (set = 1, binding = 1) uniform sampler base_colour_sampler;

layout(push_constant) uniform immediate_data {
	uint transform_index;
    uint cascade_index;
} immediate;

// Data Transfer Object from vertex shader
layout(location = 1) in struct dto {
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================

void main() {
    float alpha = texture(sampler2D(base_colour_texture, base_colour_sampler), in_dto.tex_coord).a;
    if(alpha < 0.5) { // TODO: This should probably be configurable.
        discard;
    }
}

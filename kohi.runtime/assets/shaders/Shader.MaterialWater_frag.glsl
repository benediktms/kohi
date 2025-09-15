#version 450

layout(location = 0) out vec4 out_colour;

// TODO: All these types should be defined in some #include file when #includes are implemented.

const float PI = 3.14159265359;

const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;
const uint KMATERIAL_UBO_MAX_VIEWS = 16;
const uint KMATERIAL_UBO_MAX_PROJECTIONS = 4;

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

const uint MATERIAL_WATER_TEXTURE_COUNT = 5;
const uint MATERIAL_WATER_SAMPLER_COUNT = 5;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;

const uint MAT_WATER_IDX_REFLECTION = 0;
const uint MAT_WATER_IDX_REFRACTION = 1;
const uint MAT_WATER_IDX_REFRACTION_DEPTH = 2;
const uint MAT_WATER_IDX_DUDV = 3;
const uint MAT_WATER_IDX_NORMAL = 4;

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

// =========================================================
// Inputs
// =========================================================

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

layout(set = 0, binding = 5) uniform texture2DArray shadow_texture;
layout(set = 0, binding = 6) uniform sampler shadow_sampler;
layout(set = 0, binding = 7) uniform textureCube irradiance_textures[MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];
layout(set = 0, binding = 8) uniform sampler irradiance_sampler;

layout(set = 1, binding = 0) uniform texture2D material_textures[MATERIAL_WATER_TEXTURE_COUNT];
layout(set = 1, binding = 1) uniform sampler material_samplers[MATERIAL_WATER_SAMPLER_COUNT];

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

// Data Transfer Object from vertex shader.
layout(location = 0) in dto {
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
} in_dto;


vec4 do_lighting(mat4 view, light_data directional_light, vec3 frag_position, vec3 albedo, vec3 normal, uint render_mode);
vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance);
vec3 calculate_point_light_radiance(light_data point_light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(vec3 colour, vec3 view_direction);
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias);
float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias);
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, int cascade_index);
float geometry_schlick_ggx(float normal_dot_direction, float roughness);
void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w);

// Entry point
void main() {
    uint render_mode = global_settings.render_mode;
    float game_time = global_settings.game_time;
    float move_factor = immediate.wave_speed * game_time;
    // Calculate surface distortion and bring it into [-1.0 - 1.0] range
    vec2 distorted_texcoords = texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), vec2(in_dto.texcoord.x + move_factor, in_dto.texcoord.y)).rg * 0.1;
    distorted_texcoords = in_dto.texcoord + vec2(distorted_texcoords.x, distorted_texcoords.y + move_factor);

    // Compute the surface normal using the normal map.
    vec4 normal_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_NORMAL], material_samplers[MAT_WATER_IDX_NORMAL]), distorted_texcoords);
    // Extract the normal, shifting to a range of [-1 - 1]
    vec3 normal = vec3(normal_colour.r * 2.0 - 1.0, normal_colour.g * 2.5, normal_colour.b * 2.0 - 1.0);
    
    vec3 original_tangent = normalize(vec3(1, 0, -0));
    vec3 tangent = original_tangent; // TODO: take from actual plane
    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = cross(vec3(0,1,0), original_tangent);// TODO: take from actual plane
    mat3 TBN = mat3(tangent, bitangent, normal);

    normal = normalize(TBN*normal);

    light_data directional_light = global_lighting.lights[immediate.dir_light_index];

    float water_depth = 0;

    if(render_mode == 0) {
        // Perspective division to NDC for texture projection, then to screen space.
        vec2 ndc = (in_dto.clip_space.xy / in_dto.clip_space.w) / 2.0 + 0.5;
        vec2 reflect_texcoord = vec2(ndc.x, ndc.y);
        vec2 refract_texcoord = vec2(ndc.x, -ndc.y);

        // TODO: Should come as uniforms from the viewport's near/far.
        float near = 0.1;
        float far = 1000.0;
        float depth = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION_DEPTH], material_samplers[MAT_WATER_IDX_REFRACTION_DEPTH]), refract_texcoord).r;
        // Convert depth to linear distance.
        float floor_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
        depth = gl_FragCoord.z;
        float water_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
        water_depth = floor_distance - water_distance;

        // Distort further
        vec2 distortion_total = (texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), distorted_texcoords).rg * 2.0 - 1.0) * immediate.wave_strength;

        reflect_texcoord += distortion_total;
        // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
        reflect_texcoord = clamp(reflect_texcoord, 0.001, 0.999);

        refract_texcoord += distortion_total;
        // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
        refract_texcoord.x = clamp(refract_texcoord.x, 0.001, 0.999);
        refract_texcoord.y = clamp(refract_texcoord.y, -0.999, -0.001); // Account for flipped y-axis

        vec4 reflect_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFLECTION], material_samplers[MAT_WATER_IDX_REFLECTION]), reflect_texcoord);
        vec4 refract_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION], material_samplers[MAT_WATER_IDX_REFRACTION]), refract_texcoord);
        // Refract should be slightly darker since it's wet.
        refract_colour.rgb = clamp(refract_colour.rgb - vec3(0.2), vec3(0.0), vec3(1.0));

        // Calculate the fresnel effect.
        float fresnel_factor = dot(normalize(in_dto.world_to_camera), normal);
        fresnel_factor = clamp(fresnel_factor, 0.0, 1.0);

        out_colour = mix(reflect_colour, refract_colour, fresnel_factor);
        vec4 tint = vec4(0.0, 0.3, 0.5, 1.0); // TODO: configurable.
        float tint_strength = 0.5; // TODO: configurable.
        out_colour = mix(out_colour, tint, tint_strength);
    } else {
        // Other modes should just use white.
        out_colour = vec4(1.0);
    }

    // Apply lighting
    vec4 lighting = do_lighting(global_settings.views[immediate.view_index], directional_light, in_dto.frag_position.xyz, out_colour.rgb, normal, render_mode);
    out_colour = lighting;

    if(render_mode == 0) {
        // Falloff depth of the water at the edge.
        float edge_depth_falloff = 0.5; // TODO: configurable
        out_colour.a = clamp(water_depth / edge_depth_falloff, 0.0, 1.0);
    }
}

vec4 do_lighting(mat4 view, light_data directional_light, vec3 frag_position, vec3 albedo, vec3 normal, uint render_mode) {
    vec4 light_colour;

    // These can be hardcoded for water surfaces.
    float metallic = 0.9;
    float roughness = 1.0 - metallic;
    float ao = 1.0;

    // Generate shadow value based on current fragment position vs shadow map.
    // Light and normal are also taken in the case that a bias is to be used.
    vec4 frag_position_view_space = view * vec4(frag_position, 1.0);
    float depth = abs(frag_position_view_space).z;
    // Get the cascade index from the current fragment's position.
    int cascade_index = -1;
    for(int i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
        if(depth < global_settings.cascade_splits[i]) {
            cascade_index = i;
            break;
        }
    }
    if(cascade_index == -1) {
        cascade_index = int(KMATERIAL_UBO_MAX_SHADOW_CASCADES);
    }
    float shadow = calculate_shadow(in_dto.light_space_frag_pos[cascade_index], normal, cascade_index);

    // Fade out the shadow map past a certain distance.
    float fade_start = global_settings.shadow_distance;
    float fade_distance = global_settings.shadow_fade_distance;

    // The end of the fade-out range.
    float fade_end = fade_start + fade_distance;

    float zclamp = clamp(length(global_settings.view_positions[immediate.view_index].xyz - frag_position), fade_start, fade_end);
    float fade_factor = (fade_end - zclamp) / (fade_end - fade_start + 0.00001); // Avoid divide by 0

    shadow = clamp(shadow + (1.0 - fade_factor), 0.0, 1.0);

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use base_reflectivity 
    // of 0.04 and if it's a metal, use the albedo color as base_reflectivity (metallic workflow)    
    vec3 base_reflectivity = vec3(0.04); 
    base_reflectivity = mix(base_reflectivity, albedo, metallic);

    if(render_mode == 0 || render_mode == 1 || render_mode == 3) {
        vec3 view_direction = normalize(global_settings.view_positions[immediate.view_index].xyz - frag_position);

        // Don't include albedo in mode 1 (lighting-only). Do this by using white 
        // multiplied by mode (mode 1 will result in white, mode 0 will be black),
        // then add this colour to albedo and clamp it. This will result in pure 
        // white for the albedo in mode 1, and normal albedo in mode 0, all without
        // branching.
        albedo += (vec3(1.0) * render_mode);         
        albedo = clamp(albedo, vec3(0.0), vec3(1.0));

        // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).
        // This uses a micro-facet model to use roughness and metallic properties of materials to produce
        // physically accurate representation of material reflectance.

        // Overall reflectance.
        vec3 total_reflectance = vec3(0.0);

        // Directional light radiance.
        {
            vec3 light_direction = normalize(-directional_light.position.xyz); // position = direction for directional light
            vec3 radiance = calculate_directional_light_radiance(directional_light.colour.rgb, view_direction);

            // Only directional light should be affected by shadow map.
            total_reflectance += (shadow * calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance));
        }

        // Point light radiance
        // Get point light indices by unpacking each element of immediate.packed_point_light_indices
        uint plights_rendered = 0;
        for(uint ppli = 0; ppli < 2 && plights_rendered < immediate.num_p_lights; ++ppli) {
            uint packed = immediate.packed_point_light_indices[ppli];
            uint unpacked[4];
            unpack_u32(packed, unpacked[0], unpacked[1], unpacked[2], unpacked[3]);
            for(uint upi = 0; upi < 4 && plights_rendered < immediate.num_p_lights; ++upi) {
                light_data light = global_lighting.lights[unpacked[upi]];
                vec3 light_direction = normalize(light.position.xyz - in_dto.frag_position.xyz);
                vec3 radiance = calculate_point_light_radiance(light, view_direction, in_dto.frag_position.xyz);

                total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
            }
        }

        // Irradiance holds all the scene's indirect diffuse light. Use the surface normal to sample from it.
        vec3 irradiance = texture(samplerCube(irradiance_textures[immediate.irradiance_cubemap_index], irradiance_sampler), normal).rgb;

        // Combine irradiance with albedo and ambient occlusion. 
        // Also add in total accumulated reflectance.
        vec3 ambient = irradiance * albedo * ao;
        // Modify total reflectance by the ambient colour.
        vec3 colour = ambient + total_reflectance;

        // HDR tonemapping
        colour = colour / (colour + vec3(1.0));
        // Gamma correction
        colour = pow(colour, vec3(1.0 / 2.2));

        if(render_mode == 3) {
            switch(cascade_index) {
                case 0:
                    colour *= vec3(1.0, 0.25, 0.25);
                    break;
                case 1:
                    colour *= vec3(0.25, 1.0, 0.25);
                    break;
                case 2:
                    colour *= vec3(0.25, 0.25, 1.0);
                    break;
                case 3:
                    colour *= vec3(1.0, 1.0, 0.25);
                    break;
            }
        }

        // Don't add alpha, that will be taken from the water itself.
        light_colour = vec4(colour, 1.0);
    } else if(render_mode == 2) {
        light_colour = vec4(abs(normal), 1.0);
    } else if(render_mode == 4) {
        // wireframe, just render a solid colour.
        light_colour = vec4(0.0, 0.0, 1.0, 1.0); // blue
    }

    return light_colour;
}

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance) {
    vec3 halfway = normalize(view_direction + light_direction);

    // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).

    // Normal distribution - approximates the amount of the surface's micro-facets that are aligned
    // to the halfway vector. This is directly influenced by the roughness of the surface. More aligned 
    // micro-facets = shiny, less = dull surface/less reflection.
    float roughness_sq = roughness*roughness;
    float roughness_sq_sq = roughness_sq * roughness_sq;
    float normal_dot_halfway = max(dot(normal, halfway), 0.0);
    float normal_dot_halfway_squared = normal_dot_halfway * normal_dot_halfway;
    float denom = (normal_dot_halfway_squared * (roughness_sq_sq - 1.0) + 1.0);
    denom = PI * denom * denom;
    float normal_distribution = (roughness_sq_sq / denom);

    // Geometry function which calculates self-shadowing on micro-facets (more pronounced on rough surfaces).
    float normal_dot_view_direction = max(dot(normal, view_direction), 0.0);
    // Scale the light by the dot product of normal and light_direction.
    float normal_dot_light_direction = max(dot(normal, light_direction), 0.0);
    float ggx_0 = geometry_schlick_ggx(normal_dot_view_direction, roughness);
    float ggx_1 = geometry_schlick_ggx(normal_dot_light_direction, roughness);
    float geometry = ggx_1 * ggx_0;

    // Fresnel-Schlick approximation for the fresnel. This generates a ratio of surface reflection 
    // at different surface angles. In many cases, reflectivity can be higher at more extreme angles.
    float cos_theta = max(dot(halfway, view_direction), 0.0);
    vec3 fresnel = base_reflectivity + (1.0 - base_reflectivity) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);

    // Take Normal distribution * geometry * fresnel and calculate specular reflection.
    vec3 numerator = normal_distribution * geometry * fresnel;
    float denominator = 4.0 * max(dot(normal, view_direction), 0.0) + 0.0001; // prevent div by 0 
    vec3 specular = numerator / denominator;

    // For energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component should equal 1.0 - fresnel.
    vec3 refraction_diffuse = vec3(1.0) - fresnel;
    // multiply diffuse by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    refraction_diffuse *= 1.0 - metallic;	  

    // The end result is the reflectance to be added to the overall, which is tracked by the caller.
    return (refraction_diffuse * albedo / PI + specular) * radiance * normal_dot_light_direction;  
}

vec3 calculate_point_light_radiance(light_data light, vec3 view_direction, vec3 frag_position_xyz) {
    float constant_f = 1.0f;
    // Per-light radiance based on the point light's attenuation.
    float distance = length(light.position.xyz - frag_position_xyz);
    // NOTE: linear = colour.a, quadratic = position.w
    float attenuation = 1.0 / (constant_f + light.colour.a * distance + light.position.w * (distance * distance));
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 100.0;
    return (light.colour.rgb * energy_multiplier) * attenuation;
}

vec3 calculate_directional_light_radiance(vec3 colour, vec3 view_direction) {
    // For directional lights, radiance is just the same as the light colour itself.
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 100.0;
    return colour * energy_multiplier;
}

// Percentage-Closer Filtering
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias) {
    float shadow = 0.0;
    vec2 texel_size = 1.0 / textureSize(sampler2DArray(shadow_texture, shadow_sampler), 0).xy;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcf_depth = texture(sampler2DArray(shadow_texture, shadow_sampler), vec3(projected.xy + vec2(x, y) * texel_size, cascade_index)).r;
            shadow += projected.z - shadow_bias > pcf_depth ? 1.0 : 0.0;
        }
    }
    shadow /= 9;
    return 1.0 - shadow;
}

float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias) {
    // Sample the shadow map.
    float map_depth = texture(sampler2DArray(shadow_texture, shadow_sampler), vec3(projected.xy, cascade_index)).r;

    // TODO: cast/get rid of branch.
    float shadow = projected.z - shadow_bias > map_depth ? 0.0 : 1.0;
    return shadow;
}

// Compare the fragment position against the depth buffer, and if it is further 
// back than the shadow map, it's in shadow. 0.0 = in shadow, 1.0 = not
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, int cascade_index) {
    // Perspective divide - note that while this is pointless for ortho projection,
    // perspective will require this.
    vec3 projected = light_space_frag_pos.xyz / light_space_frag_pos.w;
    // Need to reverse y
    projected.y = 1.0 - projected.y;

    // NOTE: Transform to NDC not needed for Vulkan, but would be for OpenGL.
    // projected.xy = projected.xy * 0.5 + 0.5;

    uint use_pcf = global_settings.use_pcf;
    float shadow_bias = global_settings.shadow_bias;
    if(use_pcf == 1) {
        return calculate_pcf(projected, cascade_index, shadow_bias);
    } 

    return calculate_unfiltered(projected, cascade_index, shadow_bias);
}

// Based on a combination of GGX and Schlick-Beckmann approximation to calculate probability
// of overshadowing micro-facets.
float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    roughness += 1.0;
    float k = (roughness * roughness) / 8.0;
    return normal_dot_direction / (normal_dot_direction * (1.0 - k) + k);
}

void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w) {
    x = (n >> 24) & 0xFF;
    y = (n >> 16) & 0xFF;
    z = (n >> 8) & 0xFF;
    w = n & 0xFF;
}

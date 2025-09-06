/**
 * LEFTOFF:
 * - Unify all material shaders - fragment. Vertex will have in_bone_ids and in_weights additional.
 * - Any get_shader_for_material_type call should include whether or not it's for a static/skinned mesh and
 *   return accordingly.
 */

#include "kmaterial_renderer.h"
#include "assets/kasset_types.h"
#include "core/engine.h"
#include "core/kvar.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "runtime_defines.h"
#include "serializers/kasset_shader_serializer.h"
#include "strings/kname.h"
#include "systems/kmaterial_system.h"
#include "systems/kshader_system.h"
#include "systems/ktransform_system.h"
#include "systems/light_system.h"
#include "systems/texture_system.h"

#define MATERIAL_BINDING_SET_GLOBAL 0
#define MATERIAL_BINDING_SET_INSTANCE 1

#define MATERIAL_STANDARD_NAME_FRAG "Shader.MaterialStandard_frag"
#define MATERIAL_STANDARD_NAME_VERT "Shader.MaterialStandard_vert"
#define MATERIAL_STANDARD_SKINNED_NAME_VERT "Shader.MaterialStandardSkinned_vert"
// Use the same fragment shader for skinned materials.
#define MATERIAL_STANDARD_SKINNED_NAME_FRAG MATERIAL_STANDARD_NAME_FRAG
#define MATERIAL_WATER_NAME_FRAG "Shader.MaterialWater_frag"
#define MATERIAL_WATER_NAME_VERT "Shader.MaterialWater_vert"
#define MATERIAL_BLENDED_NAME_FRAG "Shader.MaterialBlended_frag"
#define MATERIAL_BLENDED_NAME_VERT "Shader.MaterialBlended_vert"

#define MATERIAL_STANDARD_TEXTURE_COUNT 7
#define MATERIAL_STANDARD_SAMPLER_COUNT 7

#define MATERIAL_WATER_TEXTURE_COUNT 5
#define MATERIAL_WATER_SAMPLER_COUNT 5

// Standard material texture indices
const u32 MAT_STANDARD_IDX_BASE_COLOUR = 0;
const u32 MAT_STANDARD_IDX_NORMAL = 1;
const u32 MAT_STANDARD_IDX_METALLIC = 2;
const u32 MAT_STANDARD_IDX_ROUGHNESS = 3;
const u32 MAT_STANDARD_IDX_AO = 4;
const u32 MAT_STANDARD_IDX_MRA = 5;
const u32 MAT_STANDARD_IDX_EMISSIVE = 6;

// Water material texture indices
const u32 MAT_WATER_IDX_REFLECTION = 0;
const u32 MAT_WATER_IDX_REFRACTION = 1;
const u32 MAT_WATER_IDX_REFRACTION_DEPTH = 2;
const u32 MAT_WATER_IDX_DUDV = 3;
const u32 MAT_WATER_IDX_NORMAL = 4;

typedef enum kmaterial_standard_flag_bits {
    MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX = 0x0001,
    MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX = 0x0002,
    MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX = 0x0004,
    MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX = 0x0008,
    MATERIAL_STANDARD_FLAG_USE_AO_TEX = 0x0010,
    MATERIAL_STANDARD_FLAG_USE_MRA_TEX = 0x0020,
    MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX = 0x0040
} kmaterial_standard_flag_bits;

typedef u32 kmaterial_standard_flags;

b8 kmaterial_renderer_initialize(kmaterial_renderer* out_state, u32 max_material_count, u32 max_material_instance_count) {

    out_state->max_material_count = max_material_count;
    out_state->renderer = engine_systems_get()->renderer_system;
    out_state->material_state = engine_systems_get()->material_system;

    out_state->default_texture = texture_acquire_sync(kname_create(DEFAULT_TEXTURE_NAME));
    out_state->default_base_colour_texture = texture_acquire_sync(kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME));
    out_state->default_spec_texture = texture_acquire_sync(kname_create(DEFAULT_SPECULAR_TEXTURE_NAME));
    out_state->default_normal_texture = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
    out_state->default_mra_texture = texture_acquire_sync(kname_create(DEFAULT_MRA_TEXTURE_NAME));
    out_state->default_ibl_cubemap = texture_cubemap_acquire_sync(kname_create(DEFAULT_CUBE_TEXTURE_NAME));
    out_state->default_water_normal_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME));
    out_state->default_water_dudv_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME));

    // Global material storage buffer
    u64 buffer_size = sizeof(base_material_shader_data) * max_material_count;
    out_state->material_global_ssbo = renderer_renderbuffer_create(out_state->renderer, kname_create(KRENDERBUFFER_NAME_MATERIALS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT);
    KASSERT(out_state->material_global_ssbo != KRENDERBUFFER_INVALID);
    KDEBUG("Created material global storage buffer.");

    // Get default material shaders.

    // Standard material shader (static meshes).
    {
        kname mat_std_shader_name = kname_create(SHADER_NAME_RUNTIME_MATERIAL_STANDARD);
        kasset_shader mat_std_shader = {0};
        mat_std_shader.name = mat_std_shader_name;
        mat_std_shader.depth_test = true;
        mat_std_shader.depth_write = true;
        mat_std_shader.stencil_test = false;
        mat_std_shader.stencil_write = false;
        mat_std_shader.colour_write = true;
        mat_std_shader.colour_read = false;
        mat_std_shader.supports_wireframe = true;
        mat_std_shader.topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        mat_std_shader.stage_count = 2;
        mat_std_shader.stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, mat_std_shader.stage_count);
        mat_std_shader.stages[0].type = SHADER_STAGE_VERTEX;
        mat_std_shader.stages[0].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_shader.stages[0].source_asset_name = MATERIAL_STANDARD_NAME_VERT;
        mat_std_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_std_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_shader.stages[1].source_asset_name = MATERIAL_STANDARD_NAME_FRAG;

        mat_std_shader.attribute_count = 5;
        mat_std_shader.attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, mat_std_shader.attribute_count);
        mat_std_shader.attributes[0].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_shader.attributes[0].name = "in_position";
        mat_std_shader.attributes[1].name = "in_normal";
        mat_std_shader.attributes[1].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_shader.attributes[2].name = "in_texcoord";
        mat_std_shader.attributes[2].type = SHADER_ATTRIB_TYPE_FLOAT32_2;
        mat_std_shader.attributes[3].name = "in_colour";
        mat_std_shader.attributes[3].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_std_shader.attributes[4].name = "in_tangent";
        mat_std_shader.attributes[4].type = SHADER_ATTRIB_TYPE_FLOAT32_4;

        mat_std_shader.binding_set_count = 2;
        mat_std_shader.binding_sets = KALLOC_TYPE_CARRAY(shader_binding_set_config, mat_std_shader.binding_set_count);

        shader_binding_set_config* set_0 = &mat_std_shader.binding_sets[0];
        set_0->max_instance_count = 1;
        set_0->binding_count = 9;
        set_0->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_0->binding_count);

        u8 bidx = 0;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_UBO;
        set_0->bindings[bidx].name = kname_create("material global_ubo_data");
        set_0->ubo_index = 0;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_TRANSFORMS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_LIGHTING_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_MATERIALS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade maps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade map samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe cubemaps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        // Set 1
        shader_binding_set_config* set_1 = &mat_std_shader.binding_sets[1];
        set_1->max_instance_count = max_material_count;
        set_1->binding_count = 2;
        set_1->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_1->binding_count);

        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_1->bindings[bidx].name = kname_create("material texture maps");
        set_1->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->texture_count++;
        bidx++;

        bidx = 0;
        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_1->bindings[bidx].name = kname_create("material texture samplers");
        set_1->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->sampler_count++;
        bidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize(&mat_std_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_std_shader.stages, kasset_shader_stage, mat_std_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_std_shader.attributes, kasset_shader_attribute, mat_std_shader.attribute_count);
        for (u8 bs = 0; bs < mat_std_shader.binding_set_count; ++bs) {
            KFREE_TYPE_CARRAY(mat_std_shader.binding_sets[bs].bindings, shader_binding_config, mat_std_shader.binding_sets[bs].binding_count);
        }
        KFREE_TYPE_CARRAY(mat_std_shader.binding_sets, shader_binding_set_config, mat_std_shader.binding_set_count);
        kzero_memory(&mat_std_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        out_state->material_standard_shader = kshader_system_get_from_source(mat_std_shader_name, config_source);
    }

    // Standard Skinned material shader (skinned meshes).
    {
        kname mat_std_skinned_shader_name = kname_create(SHADER_NAME_RUNTIME_MATERIAL_STANDARD_SKINNED);
        kasset_shader mat_std_skinned_shader = {0};
        mat_std_skinned_shader.name = mat_std_skinned_shader_name;
        mat_std_skinned_shader.depth_test = true;
        mat_std_skinned_shader.depth_write = true;
        mat_std_skinned_shader.stencil_test = false;
        mat_std_skinned_shader.stencil_write = false;
        mat_std_skinned_shader.colour_write = true;
        mat_std_skinned_shader.colour_read = false;
        mat_std_skinned_shader.supports_wireframe = true;
        mat_std_skinned_shader.topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        mat_std_skinned_shader.stage_count = 2;
        mat_std_skinned_shader.stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, mat_std_skinned_shader.stage_count);
        mat_std_skinned_shader.stages[0].type = SHADER_STAGE_VERTEX;
        mat_std_skinned_shader.stages[0].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_skinned_shader.stages[0].source_asset_name = MATERIAL_STANDARD_SKINNED_NAME_VERT;
        mat_std_skinned_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_std_skinned_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_skinned_shader.stages[1].source_asset_name = MATERIAL_STANDARD_SKINNED_NAME_FRAG;

        mat_std_skinned_shader.attribute_count = 7;
        mat_std_skinned_shader.attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, mat_std_skinned_shader.attribute_count);
        mat_std_skinned_shader.attributes[0].name = "in_position";
        mat_std_skinned_shader.attributes[0].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_skinned_shader.attributes[1].name = "in_normal";
        mat_std_skinned_shader.attributes[1].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_skinned_shader.attributes[2].name = "in_texcoord";
        mat_std_skinned_shader.attributes[2].type = SHADER_ATTRIB_TYPE_FLOAT32_2;
        mat_std_skinned_shader.attributes[3].name = "in_colour";
        mat_std_skinned_shader.attributes[3].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_std_skinned_shader.attributes[4].name = "in_tangent";
        mat_std_skinned_shader.attributes[4].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_std_skinned_shader.attributes[5].name = "in_bone_ids";
        mat_std_skinned_shader.attributes[5].type = SHADER_ATTRIB_TYPE_INT32_4;
        mat_std_skinned_shader.attributes[6].name = "in_weights";
        mat_std_skinned_shader.attributes[6].type = SHADER_ATTRIB_TYPE_FLOAT32_4;

        mat_std_skinned_shader.binding_set_count = 2;
        mat_std_skinned_shader.binding_sets = KALLOC_TYPE_CARRAY(shader_binding_set_config, mat_std_skinned_shader.binding_set_count);

        shader_binding_set_config* set_0 = &mat_std_skinned_shader.binding_sets[0];
        set_0->max_instance_count = 1;
        set_0->binding_count = 9;
        set_0->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_0->binding_count);

        u8 bidx = 0;
        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_UBO;
        set_0->bindings[bidx].name = kname_create("material global_ubo_data");
        set_0->ubo_index = 0;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_TRANSFORMS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_LIGHTING_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_MATERIALS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade maps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade map samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe cubemaps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        // Set 1
        shader_binding_set_config* set_1 = &mat_std_skinned_shader.binding_sets[1];
        set_1->max_instance_count = max_material_count;
        set_1->binding_count = 2;
        set_1->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_1->binding_count);

        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_1->bindings[bidx].name = kname_create("material texture maps");
        set_1->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->texture_count++;
        bidx++;

        bidx = 0;
        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_1->bindings[bidx].name = kname_create("material texture samplers");
        set_1->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->sampler_count++;
        bidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize(&mat_std_skinned_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_std_skinned_shader.stages, kasset_shader_stage, mat_std_skinned_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_std_skinned_shader.attributes, kasset_shader_attribute, mat_std_skinned_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_std_skinned_shader.binding_sets, shader_binding_set_config, mat_std_skinned_shader.binding_set_count);
        kzero_memory(&mat_std_skinned_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        out_state->material_standard_skinned_shader = kshader_system_get_from_source(mat_std_skinned_shader_name, config_source);
    }

    // Water material shader.
    {
        kname mat_water_shader_name = kname_create(SHADER_NAME_RUNTIME_MATERIAL_WATER);
        kasset_shader mat_water_shader = {0};
        mat_water_shader.name = mat_water_shader_name;
        mat_water_shader.depth_test = true;
        mat_water_shader.depth_write = true;
        mat_water_shader.stencil_test = false;
        mat_water_shader.stencil_write = false;
        mat_water_shader.colour_write = true;
        mat_water_shader.colour_read = false;
        mat_water_shader.supports_wireframe = true;
        mat_water_shader.topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        mat_water_shader.stage_count = 2;
        mat_water_shader.stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, mat_water_shader.stage_count);
        mat_water_shader.stages[0].type = SHADER_STAGE_VERTEX;
        mat_water_shader.stages[0].package_name = PACKAGE_NAME_RUNTIME;
        mat_water_shader.stages[0].source_asset_name = MATERIAL_WATER_NAME_VERT;
        mat_water_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_water_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_water_shader.stages[1].source_asset_name = MATERIAL_WATER_NAME_FRAG;

        mat_water_shader.attribute_count = 1;
        mat_water_shader.attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, mat_water_shader.attribute_count);
        mat_water_shader.attributes[0].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_water_shader.attributes[0].name = "in_position";

        mat_water_shader.binding_set_count = 2;
        mat_water_shader.binding_sets = KALLOC_TYPE_CARRAY(shader_binding_set_config, mat_water_shader.binding_set_count);

        shader_binding_set_config* set_0 = &mat_water_shader.binding_sets[0];
        set_0->max_instance_count = 1;
        set_0->binding_count = 5;
        set_0->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_0->binding_count);

        u8 bidx = 0;
        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_UBO;
        set_0->bindings[bidx].name = kname_create("material global_ubo_data");
        set_0->ubo_index = 0;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_TRANSFORMS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_LIGHTING_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_MATERIALS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SSBO;
        set_0->bindings[bidx].name = kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL);
        set_0->ssbo_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade maps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard shadow cascade map samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D_ARRAY;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe cubemaps");
        set_0->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->texture_count++;
        bidx++;

        set_0->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_0->bindings[bidx].name = kname_create("material standard IBL probe samplers");
        set_0->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_CUBE;
        set_0->bindings[bidx].array_size = 4;
        set_0->sampler_count++;
        bidx++;

        // Set 1
        shader_binding_set_config* set_1 = &mat_water_shader.binding_sets[1];
        set_1->max_instance_count = max_material_count;
        set_1->binding_count = 2;
        set_1->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, set_1->binding_count);

        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_TEXTURE;
        set_1->bindings[bidx].name = kname_create("material texture maps");
        set_1->bindings[bidx].texture_type = SHADER_TEXTURE_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->texture_count++;
        bidx++;

        bidx = 0;
        set_1->bindings[bidx].binding_type = SHADER_BINDING_TYPE_SAMPLER;
        set_1->bindings[bidx].name = kname_create("material texture samplers");
        set_1->bindings[bidx].sampler_type = SHADER_SAMPLER_TYPE_2D;
        set_1->bindings[bidx].array_size = 7;
        set_1->sampler_count++;
        bidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize(&mat_water_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_water_shader.stages, kasset_shader_stage, mat_water_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_water_shader.attributes, kasset_shader_attribute, mat_water_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_water_shader.binding_sets, shader_binding_set_config, mat_water_shader.binding_set_count);
        kzero_memory(&mat_water_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        out_state->material_water_shader = kshader_system_get_from_source(mat_water_shader_name, config_source);
    }

    // Blended material shader.
    {
        // TODO: blended materials.
        // state->material_blended_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_MATERIAL_BLENDED));
    }

    // Get the binding 0 set instances for the shaders.
    out_state->material_standard_shader_bs_0_instance_id = kshader_acquire_binding_set_instance(out_state->material_standard_shader, 0);
    out_state->material_standard_skinned_shader_bs_0_instance_id = kshader_acquire_binding_set_instance(out_state->material_standard_skinned_shader, 0);
    out_state->material_water_shader_bs_0_instance_id = kshader_acquire_binding_set_instance(out_state->material_water_shader, 0);

    return true;
}

void kmaterial_renderer_shutdown(kmaterial_renderer* state) {
    if (state) {
        // TODO: Free resources, etc.
        renderer_renderbuffer_destroy(state->renderer, state->material_global_ssbo);
    }
}

void kmaterial_renderer_update(kmaterial_renderer* state) {
    if (state) {

        // Get "use pcf" option
        // TODO: optimization - hook up to events that fire when the value changes.
        i32 iuse_pcf = 0;
        kvar_i32_get("use_pcf", &iuse_pcf);
        state->settings.use_pcf = (b8)iuse_pcf;
    }
}

static kshader get_shader_for_material_type(kmaterial_renderer* state, kmaterial_type type) {
    switch (type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        return KSHADER_INVALID;
    case KMATERIAL_TYPE_STANDARD:
        return state->material_standard_shader;
    case KMATERIAL_TYPE_WATER:
        return state->material_water_shader;
    }
}

void kmaterial_renderer_register_base(kmaterial_renderer* state, kmaterial_data* base_material) {
    if (state) {
        kshader shader = get_shader_for_material_type(state, base_material->type);
        if (shader != KSHADER_INVALID) {
            // Create a shader instance for the material.
            base_material->binding_set_id = kshader_acquire_binding_set_instance(shader, MATERIAL_BINDING_SET_INSTANCE);
            KASSERT_MSG(base_material->binding_set_id != INVALID_ID_U32,
                        "Failed to acquire shader group (base material). See logs for details.");
        }
    }
}

void kmaterial_renderer_unregister_base(kmaterial_renderer* state, kmaterial_data* base_material) {
    if (state) {
        kshader shader = get_shader_for_material_type(state, base_material->type);
        if (shader != KSHADER_INVALID) {
            // Release the group for the material.
            kshader_release_binding_set_instance(shader, MATERIAL_BINDING_SET_INSTANCE, base_material->binding_set_id);
            base_material->binding_set_id = INVALID_ID_U32;
        }
    }
}

void kmaterial_renderer_register_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance) {
    if (state) {
        kshader shader = get_shader_for_material_type(state, base_material->type);
        if (shader != KSHADER_INVALID) {
            // TODO: May eliminate this as it's no longer needed since immediates are used instead.
            //
            /* // Create a group for the material.
            KASSERT_MSG(
                kshader_system_shader_per_draw_acquire(shader, &instance->per_draw_id),
                "Failed to acquire shader per-draw (material instance). See logs for details."); */
        }
    }
}

void kmaterial_renderer_unregister_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance) {
    if (state) {
        kshader shader = get_shader_for_material_type(state, base_material->type);
        if (shader != KSHADER_INVALID) {
            // TODO: May eliminate this as it's no longer needed since immediates are used instead.
            //
            /* // Release the group for the material.
            if (!kshader_system_shader_per_draw_release(shader, instance->per_draw_id)) {
                KWARN("Failed to release shader per-draw (material instance). See logs for details.");
            }
            base_material->group_id = INVALID_ID_U32; */
        }
    }
}

// Sets global point light data for the entire scene.
// NOTE: count exceeding KMATERIAL_MAX_GLOBAL_POINT_LIGHTS will be ignored

void kmaterial_renderer_set_shadow_map_texture(kmaterial_renderer* state, ktexture shadow_map_texture) {
    state->shadow_map_texture = shadow_map_texture;
}

void kmaterial_renderer_set_irradiance_cubemap_textures(kmaterial_renderer* state, u8 count, ktexture* irradiance_cubemap_textures) {
    // Ignore anything over KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT
    count = KMIN(count, KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT);

    KZERO_TYPE_CARRAY(state->ibl_cubemap_textures, ktexture, KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT);

    state->ibl_cubemap_texture_count = count;
    KCOPY_TYPE_CARRAY(state->ibl_cubemap_textures, irradiance_cubemap_textures, ktexture, count);
}

void kmaterial_renderer_apply_globals(kmaterial_renderer* state) {

    // Setup material Storage buffer data.
    void* mapped_memory = renderer_renderbuffer_get_mapped_memory(state->renderer, state->material_global_ssbo);

    base_material_shader_data* mapped_materials = (base_material_shader_data*)mapped_memory;
    const kmaterial_data* materials = kmaterial_system_get_all_base_materials(state->material_state);
    // FIXME: Find a way to unify these types to avoid all the copying.
    for (u32 i = 0; i < state->max_material_count; ++i) {
        const kmaterial_data* src = &materials[i];
        base_material_shader_data* dest = &mapped_materials[i];

        dest->base_colour = src->base_colour;
        dest->normal = src->normal;
        dest->flags = src->flags;
        dest->metallic = src->metallic;
        dest->roughness = src->roughness;
        dest->ao = src->ao;
        dest->metallic_texture_channel = src->metallic_texture_channel;
        dest->roughness_texture_channel = src->roughness_texture_channel;
        dest->ao_texture_channel = src->ao_texture_channel;
        dest->mra = src->mra;
        dest->emissive = src->emissive;
        dest->emissive_texture_intensity = src->emissive_texture_intensity;
        dest->base_colour = src->base_colour;
        dest->uv_offset = src->uv_offset;
        dest->uv_scale = src->uv_scale;
        dest->refraction_scale = src->refraction_scale;
        dest->lighting_model = src->model;
        // NOTE: texture flags get set during binding phase below.
        dest->tex_flags = 0;
    }

    b8 is_wireframe = (state->settings.render_mode == RENDERER_VIEW_MODE_WIREFRAME);

    // Set standard shader UBO globals
    {
        kshader shader = state->material_standard_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // Ensure wireframe mode is (un)set.
        KASSERT_DEBUG(kshader_system_set_wireframe(shader, is_wireframe));

        // Upload the global UBO
        kshader_set_binding_data(shader, 0, state->material_standard_shader_bs_0_instance_id, 0, 0, &state->settings, sizeof(kmaterial_settings_ubo));

        // Texture maps
        // Shadow map - arrayed texture.
        // FIXME: Probably only need to set this once, when the scene is initially loaded?
        if (state->shadow_map_texture) {
            kshader_set_binding_texture(shader, 0, state->material_standard_shader_bs_0_instance_id, 5, 0, state->shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = state->ibl_cubemap_textures[i] != INVALID_KTEXTURE ? state->ibl_cubemap_textures[i] : state->default_ibl_cubemap;
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            kshader_set_binding_texture(shader, 0, state->material_standard_shader_bs_0_instance_id, 7, i, t);
        }
    }
    // Set water shader globals
    {
        kshader shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // Ensure wireframe mode is (un)set.
        KASSERT_DEBUG(kshader_system_set_wireframe(shader, is_wireframe));

        // Upload the global UBO
        kshader_set_binding_data(shader, 0, state->material_water_shader_bs_0_instance_id, 0, 0, &state->settings, sizeof(kmaterial_settings_ubo));

        // Texture maps
        // Shadow map - arrayed texture.
        // FIXME: Probably only need to set this once, when the scene is initially loaded?
        if (state->shadow_map_texture) {
            kshader_set_binding_texture(shader, 0, state->material_water_shader_bs_0_instance_id, 5, 0, state->shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = state->ibl_cubemap_textures[i] != INVALID_KTEXTURE ? state->ibl_cubemap_textures[i] : state->default_ibl_cubemap;
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            kshader_set_binding_texture(shader, 0, state->material_water_shader_bs_0_instance_id, 7, i, t);
        }
    }

    // TODO: Set skinned material shader globals
    // TODO: Set blended shader globals
}

// Updates and binds base material.
void kmaterial_renderer_bind_base(kmaterial_renderer* state, kmaterial base_material) {
    KASSERT_DEBUG(state);

    const kmaterial_data* material = kmaterial_get_base_material_data(engine_systems_get()->material_system, base_material);
    KASSERT_DEBUG(material);

    void* mapped_memory = renderer_renderbuffer_get_mapped_memory(state->renderer, state->material_global_ssbo);
    base_material_shader_data* mapped_materials = (base_material_shader_data*)mapped_memory;
    base_material_shader_data* mapped_mat = &mapped_materials[base_material];

    mapped_mat->tex_flags = 0;

    kshader shader = KSHADER_INVALID;

    switch (material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        break;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        kshader_system_use(shader);

        // --------------------------------------------
        // Texture inputs - bind each texture if used.
        // --------------------------------------------

        // Base colour
        ktexture base_colour_tex = state->default_base_colour_texture;
        if (texture_is_loaded(material->base_colour_texture)) {
            FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX, true);
            base_colour_tex = material->base_colour_texture;
        }
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_BASE_COLOUR, base_colour_tex);

        // Normal, if used
        ktexture normal_tex = state->default_normal_texture;
        if (FLAG_GET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)) {
            if (texture_is_loaded(material->normal_texture)) {
                FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX, true);
                normal_tex = material->normal_texture;
            }
        } else {
            mapped_mat->normal = KMATERIAL_DEFAULT_NORMAL_VALUE;
        }
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_NORMAL, normal_tex);

        // MRA (Metallic/Roughness/AO)
        b8 mra_enabled = FLAG_GET(material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT);
        ktexture mra_texture = state->default_mra_texture;
        ktexture metallic_texture = state->default_base_colour_texture;
        ktexture roughness_texture = state->default_base_colour_texture;
        ktexture ao_texture = state->default_base_colour_texture;
        if (mra_enabled) {
            // Use the MRA texture or fallback to the MRA value on the material.
            if (texture_is_loaded(material->mra_texture)) {
                FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX, true);
                mra_texture = material->mra_texture;
            }
        } else {
            // If not using MRA, then do these:

            // Metallic texture or value
            if (texture_is_loaded(material->metallic_texture)) {
                FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX, true);
                metallic_texture = material->metallic_texture;
            }

            // Roughness texture or value
            if (texture_is_loaded(material->roughness_texture)) {
                FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX, true);
                roughness_texture = material->roughness_texture;
            }

            // AO texture or value (if enabled)
            if (FLAG_GET(material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) {
                if (texture_is_loaded(material->ao_texture)) {
                    FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX, true);
                    ao_texture = material->ao_texture;
                }
            } else {
                mapped_mat->ao = 1.0f;
            }
        }

        // Apply textures
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_MRA, mra_texture);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_METALLIC, metallic_texture);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_ROUGHNESS, roughness_texture);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_AO, ao_texture);

        // Emissive
        ktexture emissive_texture = state->default_base_colour_texture;
        if (FLAG_GET(material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) {
            if (texture_is_loaded(material->emissive_texture)) {
                FLAG_SET(mapped_mat->tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX, true);
                emissive_texture = material->emissive_texture;
            }
        } else {
            mapped_mat->emissive = vec4_zero();
        }
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_STANDARD_IDX_EMISSIVE, emissive_texture);
    } break;
    case KMATERIAL_TYPE_WATER: {

        shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        ktexture reflection_colour_tex = material->reflection_texture;
        ktexture refraction_colour_tex = material->refraction_texture;
        ktexture refraction_depth_tex = material->refraction_depth_texture;
        ktexture dudv_texture = texture_is_loaded(material->dudv_texture) ? material->dudv_texture : state->default_texture;
        ktexture normal_texture = texture_is_loaded(material->normal_texture) ? material->normal_texture : state->default_normal_texture;

        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_WATER_IDX_REFLECTION, reflection_colour_tex);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_WATER_IDX_REFRACTION, refraction_colour_tex);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_WATER_IDX_REFRACTION_DEPTH, refraction_depth_tex);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_WATER_IDX_DUDV, dudv_texture);
        kshader_set_binding_texture(shader, 1, base_material, 0, MAT_WATER_IDX_NORMAL, normal_texture);

    } break;
    case KMATERIAL_TYPE_BLENDED: {
        shader = state->material_blended_shader;
        KASSERT_MSG(false, "Blended materials not yet supported.");
    } break;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Custom materials not yet supported.");
        break;
    }
}

// Updates and binds material instance using the provided lighting information.
void kmaterial_renderer_apply_immediates(kmaterial_renderer* state, kmaterial_instance instance, const kmaterial_render_immediate_data* immediates) {
    KASSERT_DEBUG(state);

    const kmaterial_instance_data* instance_data = kmaterial_get_material_instance_data(engine_systems_get()->material_system, instance);
    KASSERT_DEBUG(instance_data);

    const kmaterial_data* base_material = kmaterial_get_base_material_data(engine_systems_get()->material_system, instance.base_material);
    KASSERT_DEBUG(base_material);

    /* // Pack point light indices
    uvec2 packed_point_light_indices = {0};
    u8 written = 0;
    for (u8 i = 0; i < 2 && written < point_light_count; ++i) {
        u32 vi = 0;

        for (u8 p = 0; p < 4 && written < point_light_count; ++p) {

            // Pack the u8 into the given u32
            vi |= ((u32)point_light_indices[written] << ((3 - p) * 8));
            ++written;
        }

        // Store the packed u32
        packed_point_light_indices.elements[i] = vi;
    } */

    kshader shader = KSHADER_INVALID;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown material type cannot be applied.");
        break;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        kshader_set_immediate_data(shader, immediates, sizeof(kmaterial_render_immediate_data));
    } break;
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        kshader_set_immediate_data(shader, immediates, sizeof(kmaterial_render_immediate_data));
    } break;
    case KMATERIAL_TYPE_BLENDED: {
        shader = state->material_blended_shader;
        KASSERT_MSG(false, "Blended materials not yet supported.");
    } break;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Custom materials not yet supported.");
        break;
    }
}

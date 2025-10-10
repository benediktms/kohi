#include "kasset_importer_animated_mesh_assimp.h"

#include <assimp/cimport.h>
#include <assimp/color4.h>
#include <assimp/defs.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <assimp/vector3.h>

#include "assets/kasset_types.h"
#include "assimp/anim.h"
#include "assimp/matrix4x4.h"

#include "debug/kassert.h"

#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"

#include "platform/filesystem.h"
#include "serializers/kasset_material_serializer.h"

// TODO: If this is needed, perhaps move types to separate header file.
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kanimation_system.h"

static void skinned_vertex_3d_defaults(skinned_vertex_3d* vert);
static void get_material_texture_data_by_type(kname package_name, const struct aiMaterial* material, enum aiTextureType texture_type, kasset_material* new_material, kmaterial_texture_input_config* input);
static mat4 mat4_from_ai(const struct aiMatrix4x4* source);
static b8 materials_from_assimp(const struct aiScene* scene, kname package_name, const char* output_directory);
static const struct aiScene* assimp_open_file(const char* source_path);
static b8 anim_asset_from_assimp(const struct aiScene* scene, kname package_name, kasset_animated_mesh* out_asset);
static void anim_asset_destroy(kasset_animated_mesh* asset);

b8 kasset_animated_mesh_assimp_import(const char* source_path, const char* target_path, const char* material_target_dir, const char* package_name) {
    kasset_animated_mesh new_asset = {0};

    const struct aiScene* scene = assimp_open_file(source_path);

    kname pkg_name = kname_create(package_name);

    // Import mesh, animations, bones, etc.
    if (!anim_asset_from_assimp(scene, pkg_name, &new_asset)) {
        KERROR("Failed to import assimp asset '%s'.", source_path);
        return false;
    }

    // Import materials
    if (!materials_from_assimp(scene, pkg_name, material_target_dir)) {
        KERROR("Failed to import materials from assimp asset '%s'.", source_path);
        return false;
    }

    return true;
}

static void skinned_vertex_3d_defaults(skinned_vertex_3d* vert) {
    for (u8 i = 0; i < 4; ++i) {
        vert->bone_ids.elements[i] = -1;
        vert->weights.elements[i] = 0.0f;
    }
}

static void get_material_texture_data_by_type(kname package_name, const struct aiMaterial* material, enum aiTextureType texture_type, kasset_material* new_material, kmaterial_texture_input_config* input) {
    if (aiGetMaterialTextureCount(material, texture_type)) {
        const u32 index = 0; // NOTE: Only get the first one.
        struct aiString path;
        enum aiTextureMapping mapping;
        u32 uvindex;
        ai_real blend;
        enum aiTextureOp op;
        enum aiTextureMapMode mapmode;
        u32 flags;
        enum aiReturn result = aiGetMaterialTexture(material, texture_type, index, &path, &mapping, &uvindex, &blend, &op, &mapmode, &flags);
        if (result != aiReturn_SUCCESS) {
            KWARN("Failed reading base colour texture.");
        } else {
            const char* asset_name = string_filename_no_extension_from_path(path.data);
            input->resource_name = kname_create(asset_name);
            string_free(asset_name);
            input->package_name = package_name;

            texture_repeat repeat = TEXTURE_REPEAT_REPEAT;
            switch (mapmode) {
            case aiTextureMapMode_Wrap:
                repeat = TEXTURE_REPEAT_REPEAT;
                break;
            case aiTextureMapMode_Clamp:
                repeat = TEXTURE_REPEAT_CLAMP_TO_EDGE;
                break;
            case aiTextureMapMode_Mirror:
                repeat = TEXTURE_REPEAT_MIRRORED_REPEAT;
                break;
            default:
            case aiTextureMapMode_Decal:
                KWARN("Unsupported texture map mode found, defaulting to repeat.");
                break;
            }

            input->sampler.repeat_u = input->sampler.repeat_v = input->sampler.repeat_w = repeat;
            // NOTE: Since there is no way to obtain this, all maps will use linear min/mag.
            input->sampler.filter_min = input->sampler.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
            // NOTE: Don't name the sampler here. Properties can be analyzed by the engine and a default sampler can be chosen based on it.
            input->sampler.name = INVALID_KNAME;
        }
    }
}

static mat4 mat4_from_ai(const struct aiMatrix4x4* source) {
    // NOTE: Kohi expects column-major, and assimp uses row-major.
    // Therefore, transpose the matrix here before returning.
    mat4 m = {0};
    m.data[0] = source->a1;
    m.data[1] = source->b1;
    m.data[2] = source->c1;
    m.data[3] = source->d1;
    m.data[4] = source->a2;
    m.data[5] = source->b2;
    m.data[6] = source->c2;
    m.data[7] = source->d2;
    m.data[8] = source->a3;
    m.data[9] = source->b3;
    m.data[10] = source->c3;
    m.data[11] = source->d3;
    m.data[12] = source->a4;
    m.data[13] = source->b4;
    m.data[14] = source->c4;
    m.data[15] = source->d4;

    return m;
}

// Import all materials from an assimp scene.
static b8 materials_from_assimp(const struct aiScene* scene, kname package_name, const char* output_directory) {

    for (u32 i = 0; i < scene->mNumMaterials; ++i) {
        struct aiMaterial* material = scene->mMaterials[i];

        kasset_material new_material = {0};

        // Extract the shading model. Use this to determine what maps and properties to extract.
        i32 shading_model_int = 0;
        new_material.model = KMATERIAL_MODEL_PBR;
        enum aiReturn result = aiGetMaterialInteger(material, AI_MATKEY_SHADING_MODEL, &shading_model_int);
        if (result == aiReturn_SUCCESS) {
            switch (((enum aiShadingMode)shading_model_int)) {
            case aiShadingMode_PBR_BRDF:
            case aiShadingMode_CookTorrance:
                new_material.model = KMATERIAL_MODEL_PBR;
                break;
            case aiShadingMode_Phong:
            case aiShadingMode_Blinn:
                new_material.model = KMATERIAL_MODEL_PHONG;
                break;
            case aiShadingMode_NoShading:
                new_material.model = KMATERIAL_MODEL_UNLIT;
                break;
            case aiShadingMode_Gouraud:
            case aiShadingMode_Flat:
            case aiShadingMode_Toon:
            case aiShadingMode_OrenNayar:
            case aiShadingMode_Minnaert:
            case aiShadingMode_Fresnel:
            default:
                KWARN("Shading model not supported, defaulting to PBR.");
                new_material.model = KMATERIAL_MODEL_PBR;
                break;
            }
        }

        switch (new_material.model) {
        default:
        case KMATERIAL_MODEL_PBR:
            get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_NORMALS, &new_material, &new_material.normal_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_METALNESS, &new_material, &new_material.metallic_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE_ROUGHNESS, &new_material, &new_material.roughness_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_AMBIENT_OCCLUSION, &new_material, &new_material.ambient_occlusion_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_GLTF_METALLIC_ROUGHNESS, &new_material, &new_material.mra_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_EMISSIVE, &new_material, &new_material.emissive_map);
            break;
        case KMATERIAL_MODEL_UNLIT: {
            get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
            if (!new_material.base_colour_map.resource_name) {
                get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE, &new_material, &new_material.base_colour_map);
            }

            // Also get diffuse colour, which might be defined.
            struct aiColor4D diffuse_c4d;
            new_material.base_colour = vec4_one();
            result = aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse_c4d);
            if (result == aiReturn_SUCCESS) {
                new_material.base_colour = (colour4){diffuse_c4d.r, diffuse_c4d.g, diffuse_c4d.b, diffuse_c4d.a};
            }
        } break;
        case KMATERIAL_MODEL_PHONG: {
            get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
            if (!new_material.base_colour_map.resource_name) {
                get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE, &new_material, &new_material.base_colour_map);
            }
            get_material_texture_data_by_type(package_name, material, aiTextureType_NORMALS, &new_material, &new_material.normal_map);
            get_material_texture_data_by_type(package_name, material, aiTextureType_SPECULAR, &new_material, &new_material.specular_colour_map);

            // Phong-specific properties.

            /* // TODO: Phong Shininess
            ai_real shininess = 0.0f;
            result = aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess);
            if (result == aiReturn_SUCCESS) {
                new_material.shininess = (f32)shininess;
            } */

            struct aiColor4D ambient_c4d;
            result = aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &ambient_c4d);
            if (result == aiReturn_SUCCESS) {
                new_material.base_colour = (colour4){ambient_c4d.r, ambient_c4d.g, ambient_c4d.b, ambient_c4d.a};
            } else {
                new_material.base_colour = vec4_zero();
            }

            struct aiColor4D diffuse_c4d;
            colour4 diffuse = vec4_one();
            result = aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse_c4d);
            if (result == aiReturn_SUCCESS) {
                diffuse = (colour4){diffuse_c4d.r, diffuse_c4d.g, diffuse_c4d.b, diffuse_c4d.a};
            }

            // For Phong, base colour is ambient + diffuse.
            new_material.base_colour = vec4_normalized(vec4_add(new_material.base_colour, diffuse));

            struct aiColor4D specular_c4d;
            result = aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular_c4d);
            if (result == aiReturn_SUCCESS) {
                new_material.specular_colour = (colour4){specular_c4d.r, specular_c4d.g, specular_c4d.b, specular_c4d.a};
            } else {
                new_material.specular_colour = vec4_zero();
            }
        } break;
        }

        // Serialize the material.
        const char* serialized_text = kasset_material_serialize(&new_material);
        if (!serialized_text) {
            KWARN("Failed to serialize material '%s'. See logs for details.", kname_string_get(new_material.name));
            string_free(serialized_text);
            continue;
        }

        // Write out kmt file.
        const char* out_path = string_format("%s/%s.%s", output_directory, kname_string_get(new_material.name), "kmt");
        if (!filesystem_write_entire_text_file(out_path, serialized_text)) {
            KERROR("Failed to write serialized material to disk. See logs for details.");
        }
        string_free(out_path);
        string_free(serialized_text);
    }

    return true;
}

static const struct aiScene* assimp_open_file(const char* source_path) {
    const struct aiScene* scene = aiImportFile(source_path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        KERROR("Error importing via assimp: %s", aiGetErrorString());
        return 0;
    }

    return scene;
}

static b8 anim_asset_from_assimp(const struct aiScene* scene, kname package_name, kasset_animated_mesh* out_asset) {

    kzero_memory(out_asset, sizeof(kasset_animated_mesh));
    out_asset->global_inverse_transform = mat4_from_ai(&scene->mRootNode->mTransformation);
    // TODO: Does this need to be the inverse?

    // Get all unique bones across all meshes.
    kasset_animated_mesh_bone bones[KANIMATION_MAX_BONES] = {0};
    u32 bone_count = 0;
    for (u32 m = 0; m < scene->mNumMeshes; ++m) {
        struct aiMesh* mesh = scene->mMeshes[m];
        for (u32 b = 0; b < mesh->mNumBones; ++b) {
            struct aiBone* ai_bone = mesh->mBones[b];
            kasset_animated_mesh_bone* bone = &bones[bone_count];
            kname ai_bone_name = kname_create(ai_bone->mName.data);

            b8 found = false;
            for (u32 i = 0; i < bone_count; ++i) {
                if (bones[i].name == ai_bone_name) {
                    found = true;
                    break;
                }
            }
            if (found) {
                // Bone already exists, skip it.
                continue;
            }
            KASSERT(bone_count < KANIMATION_MAX_BONES);
            bone->name = ai_bone_name;
            bone->offset = mat4_from_ai(&ai_bone->mOffsetMatrix);
            bone->id = bone_count;

            bone_count++;
        }
    }

    out_asset->bones = KALLOC_TYPE_CARRAY(kasset_animated_mesh_bone, bone_count);
    out_asset->bone_count = bone_count;
    KCOPY_TYPE_CARRAY(out_asset->bones, bones, kasset_animated_mesh_bone, bone_count);

    // Flatten the node structure into a single array and reference by index instead.
    u32 node_capacity = 256;
    u32 node_count = 0;
    kasset_animated_mesh_node* nodes = KALLOC_TYPE_CARRAY(kasset_animated_mesh_node, node_capacity);

    typedef struct node_map_entry {
        const struct aiNode* node;
        u32 index;
    } node_map_entry;

    u32 node_map_count = 0;
    u32 node_map_capacity = 0;
    node_map_entry* node_map = KNULL;

    // Push the root first
    const struct aiNode* root = scene->mRootNode;
    const struct aiNode* stack_nodes[1024];
    u32 stack_top = 0;
    stack_nodes[stack_top++] = root;
    while (stack_top) {
        const struct aiNode* current = stack_nodes[--stack_top];
        if (node_count == node_capacity) {
            nodes = KREALLOC_TYPE_CARRAY(nodes, kasset_animated_mesh_node, node_capacity, node_capacity * 2);
            node_capacity *= 2;
        }
        kasset_animated_mesh_node* node = &nodes[node_count];
        kzero_memory(node, sizeof(kasset_animated_mesh_node));
        node->name = kname_create(current->mName.data);
        node->parent_index = INVALID_ID;
        node->children = KNULL;
        node->child_count = 0;
        // Add it to the map
        if (node_map_count == node_map_capacity) {
            node_map = KREALLOC_TYPE_CARRAY(node_map, node_map_entry, node_map_capacity, node_map_capacity ? node_map_capacity * 2 : 64);
            node_map_capacity = node_map_capacity ? node_map_capacity * 2 : 64;
        }
        node_map_entry* entry = &node_map[node_map_count];
        entry->node = current;
        entry->index = node_count;
        node_map_count++;
        node_count++;
        // Push children.
        for (u32 i = 0; i < current->mNumChildren; ++i) {
            stack_nodes[stack_top++] = current->mChildren[i];
        }
    }

    // Set parent/child by re-iterating the map.
    for (u32 i = 0; i < node_map_count; ++i) {
        const struct aiNode* current = node_map[i].node;
        u32 index = node_map[i].index;
        for (u32 c = 0; c < current->mNumChildren; c++) {
            const struct aiNode* child = current->mChildren[c];
            u32 child_index = INVALID_ID;
            for (u32 s = 0; s < node_map_count; ++s) {
                if (node_map[s].node == child) {
                    child_index = node_map[s].index;
                    break;
                }
            }
            if (child_index != INVALID_ID) {
                kasset_animated_mesh_node* cn = &nodes[index];
                cn->children = KREALLOC_TYPE_CARRAY(cn->children, u32, cn->child_count, cn->child_count + 1);
                cn->children[cn->child_count++] = child_index;
                nodes[child_index].parent_index = index;
            }
        }
    }

    out_asset->nodes = KALLOC_TYPE_CARRAY(kasset_animated_mesh_node, node_count);
    KCOPY_TYPE_CARRAY(out_asset->nodes, nodes, kasset_animated_mesh_node, node_count);
    out_asset->node_count = node_count;
    KFREE_TYPE_CARRAY(nodes, kasset_animated_mesh_node, node_capacity);

    // Copy channels and keys
    u32 animation_count = out_asset->animation_count;
    out_asset->animation_count = animation_count;
    out_asset->animations = KALLOC_TYPE_CARRAY(kasset_animated_mesh_animation, out_asset->animation_count);
    for (u32 a = 0; a < animation_count; ++a) {
        struct aiAnimation* anim = scene->mAnimations[a];
        kasset_animated_mesh_animation* out = &out_asset->animations[a];
        out->name = kname_create(anim->mName.data);
        out->duration = anim->mDuration;
        out->ticks_per_second = anim->mTicksPerSecond;
        out->channel_count = anim->mNumChannels;
        out->channels = KALLOC_TYPE_CARRAY(kasset_animated_mesh_channel, out->channel_count);
        for (u32 c = 0; c < out->channel_count; c++) {
            struct aiNodeAnim* chn = anim->mChannels[c];
            kasset_animated_mesh_channel* oc = &out->channels[c];
            kzero_memory(oc, sizeof(kanimated_mesh_channel));
            oc->name = kname_create(chn->mNodeName.data);

            // Positions
            oc->pos_count = chn->mNumPositionKeys;
            if (oc->pos_count) {
                oc->positions = KALLOC_TYPE_CARRAY(kasset_animated_mesh_key_vec3, oc->pos_count);
                for (u32 k = 0; k < oc->pos_count; ++k) {
                    struct aiVectorKey* vk = &chn->mPositionKeys[k];
                    oc->positions[k].time = vk->mTime;
                    oc->positions[k].value = (vec3){vk->mValue.x, vk->mValue.y, vk->mValue.z};
                }
            } else {
                oc->positions = KNULL;
            }

            // Rotations
            oc->rot_count = chn->mNumRotationKeys;
            if (oc->rot_count) {
                oc->rotations = KALLOC_TYPE_CARRAY(kasset_animated_mesh_key_quat, oc->rot_count);
                for (u32 k = 0; k < oc->rot_count; ++k) {
                    struct aiQuatKey* vk = &chn->mRotationKeys[k];
                    oc->rotations[k].time = vk->mTime;
                    oc->rotations[k].value = (quat){vk->mValue.x, vk->mValue.y, vk->mValue.z, vk->mValue.w};
                }
            } else {
                oc->rotations = KNULL;
            }

            // Scales
            oc->scale_count = chn->mNumScalingKeys;
            if (oc->scale_count) {
                oc->scales = KALLOC_TYPE_CARRAY(kasset_animated_mesh_key_vec3, oc->scale_count);
                for (u32 k = 0; k < oc->scale_count; ++k) {
                    struct aiVectorKey* vk = &chn->mScalingKeys[k];
                    oc->scales[k].time = vk->mTime;
                    oc->scales[k].value = (vec3){vk->mValue.x, vk->mValue.y, vk->mValue.z};
                }
            } else {
                oc->scales = KNULL;
            }
        }
    }

    // Extract materials.

    // Release all assimp resources.
    aiReleaseImport(scene);

    return true;
}

static void anim_asset_destroy(kasset_animated_mesh* asset) {
    if (!asset) {
        return;
    }

    for (u32 i = 0; i < asset->animation_count; ++i) {
        kasset_animated_mesh_animation* a = &asset->animations[i];
        for (u32 c = 0; c < a->channel_count; c++) {
            kasset_animated_mesh_channel* ch = &a->channels[c];
            KFREE_TYPE_CARRAY(ch->positions, anim_key_vec3, ch->pos_count);
            KFREE_TYPE_CARRAY(ch->rotations, anim_key_quat, ch->rot_count);
            KFREE_TYPE_CARRAY(ch->scales, anim_key_vec3, ch->scale_count);
        }
        KFREE_TYPE_CARRAY(a->channels, kanimated_mesh_channel, a->channel_count);
    }
    KFREE_TYPE_CARRAY(asset->animations, kanimated_mesh_animation, asset->animation_count);
    KFREE_TYPE_CARRAY(asset->bones, kasset_animated_mesh_bone, asset->bone_count);
    for (u32 i = 0; i < asset->node_count; ++i) {
        KFREE_TYPE_CARRAY(asset->nodes[i].children, u32, asset->nodes[i].child_count);
    }
    KFREE_TYPE_CARRAY(asset->nodes, kasset_animated_mesh_node, asset->node_count);
}

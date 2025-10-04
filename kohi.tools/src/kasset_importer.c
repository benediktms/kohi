#include "kasset_importer.h"

#include <assimp/cimport.h>
#include <assimp/color4.h>
#include <assimp/defs.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <assimp/vector3.h>
#include <bits/types/stack_t.h>

#include "assets/kasset_types.h"
#include "assimp/anim.h"
#include "assimp/matrix4x4.h"
#include "containers/darray.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "importers/kasset_importer_audio.h"
#include "importers/kasset_importer_bitmap_font_fnt.h"
#include "importers/kasset_importer_image.h"
#include "importers/kasset_importer_material_obj_mtl.h"
#include "importers/kasset_importer_static_mesh_obj.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "platform/filesystem.h"
#include "platform/kpackage.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kanimation_system.h"
#include "utils/render_type_utils.h"

/*
 *
 *
NOTE: Need to add required/optional options (lul) to import processes. Can vary by type/importer
kohi.tools -t "./assets/models/Tree.ksm" -s "./assets/models/source/Tree.obj" -mtl_target_path="./assets/materials/" -package_name="Testbed"
kohi.tools -t "./assets/models/Tree.ksm" -s "./assets/models/source/Tree.gltf" -mtl_target_path="./assets/materials/" -package_name="Testbed"
kohi.tools -t "./assets/images/orange_lines_512.kbi" -s "./assets/images/source/orange_lines_512.png" -flip_y=no
*/

// Returns the index of the option. -1 if not found.
static i16 get_option_index(const char* name, u8 option_count, const import_option* options);
static const char* get_option_value(const char* name, u8 option_count, const import_option* options);
static b8 extension_is_audio(const char* extension);
static b8 extension_is_image(const char* extension);

b8 obj_2_ksm(const char* source_path, const char* target_path, const char* mtl_target_dir, const char* package_name) {
    KDEBUG("Executing %s...", __FUNCTION__);
    // OBJ import
    const char* content = filesystem_read_entire_text_file(source_path);
    if (!content) {
        KERROR("Failed to read file content for path '%s'. Import failed.", source_path);
        return false;
    }

    u32 material_file_count = 0;
    const char** material_file_names = 0;
    // Parses source file, imports and writes asset to disk.
    if (!kasset_static_mesh_obj_import(target_path, content, &material_file_count, &material_file_names)) {
        KERROR("Failed to import obj file '%s'. See logs for details.", source_path);
        return false;
    }

    const char* source_folder = string_directory_from_path(source_path);

    // Secondary import of materials. If these fail, should not count as a static mesh import failure.
    for (u32 i = 0; i < material_file_count; ++i) {
        const char* mtl_file_name_no_extension = string_filename_no_extension_from_path(material_file_names[i]);
        const char* src_mtl_file_path = string_format("%s/%s", source_folder, material_file_names[i]);
        const char* data = filesystem_read_entire_text_file(src_mtl_file_path);
        b8 mtl_result = kasset_material_obj_mtl_import(mtl_target_dir, mtl_file_name_no_extension, package_name, data);
        string_free(mtl_file_name_no_extension);
        string_free(src_mtl_file_path);
        string_free(data);
        if (!mtl_result) {
            KWARN("Material file import failed (%s). See logs for details.", source_path);
        }
    }

    string_free(source_folder);

    return true;
}

b8 mtl_2_kmt(const char* source_path, const char* target_filename, const char* mtl_target_dir, const char* package_name) {
    KDEBUG("Executing %s...", __FUNCTION__);
    // MTL import
    /* const char* mtl_file_name = string_filename_from_path(source_path); */
    const char* data = filesystem_read_entire_text_file(source_path);
    b8 success = kasset_material_obj_mtl_import(mtl_target_dir, target_filename, package_name, data);
    /* string_free(mtl_file_name); */
    string_free(data);
    if (!success) {
        KERROR("Material file import failed (%s). See logs for details.", source_path);
        return false;
    }

    return true;
}

b8 source_audio_2_kaf(const char* source_path, const char* target_path) {
    KDEBUG("Executing %s...", __FUNCTION__);
    return kasset_audio_import(source_path, target_path);
}

// if output_format is set, force that format. Otherwise use source file format.
b8 source_image_2_kbi(const char* source_path, const char* target_path, b8 flip_y, kpixel_format output_format) {
    KDEBUG("Executing %s... (flip_y=%s)", __FUNCTION__, flip_y ? "yes" : "no");
    return kasset_image_import(source_path, target_path, flip_y, output_format);
}

b8 fnt_2_kbf(const char* source_path, const char* target_path) {
    KDEBUG("Executing %s...", __FUNCTION__);
    return kasset_bitmap_font_fnt_import(source_path, target_path);
}

typedef struct skinned_mesh {
    kgeometry geo;

} skinned_mesh;

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

static skinned_mesh process_mesh(kname package_name, struct aiMesh* mesh, const struct aiScene* scene) {
    skinned_vertex_3d* vertices = KALLOC_TYPE_CARRAY(skinned_vertex_3d, mesh->mNumVertices);
    u8* vertex_bone_counts = KALLOC_TYPE_CARRAY(u8, mesh->mNumVertices);

    u32 index_count = 0;
    for (u32 i = 0; i < mesh->mNumFaces; ++i) {
        index_count += mesh->mFaces[i].mNumIndices;
    }
    u32* indices = KALLOC_TYPE_CARRAY(u32, index_count);

    for (u32 i = 0; i < mesh->mNumVertices; ++i) {
        skinned_vertex_3d* vert = &vertices[i];
        skinned_vertex_3d_defaults(vert);
        struct aiVector3D* sv = &mesh->mVertices[i];
        struct aiVector3D* sn = &mesh->mNormals[i];
        struct aiVector3D* st = &mesh->mTangents[i];
        struct aiVector3D* sb = &mesh->mBitangents[i];
        vert->position = (vec3){sv->x, sv->y, sv->z};
        vert->normal = (vec3){sn->x, sn->y, sn->z};
        vec3 t = (vec3){st->x, st->y, st->z};
        vec3 b = (vec3){sb->x, sb->y, sb->z};
        f32 w = vec3_dot(vec3_cross(t, vert->normal), b) < 0.0f ? -1.0f : 1.0f;
        vert->tangent = vec4_from_vec3(t, w);

        if (mesh->mTextureCoords[0]) {
            vert->texcoord = (vec2){
                .x = mesh->mTextureCoords[0][i].x,
                .y = mesh->mTextureCoords[0][i].y};
        } else {
            vert->texcoord = (vec2){0, 0};
        }

        // NOTE: Use vertex colour if it exists, otherwise just use white.
        // TODO: Find a cleaner way to do this?
        if (mesh->mColors[0]) {
            struct aiColor4D* colour = &mesh->mColors[0][i];
            vert->colour = (vec4){colour->r, colour->g, colour->b, colour->a};
        } else {
            vert->colour = vec4_one();
        }
    }

    u32 ii = 0;
    for (u32 i = 0; i < mesh->mNumFaces; ++i) {
        struct aiFace face = mesh->mFaces[i];
        for (u32 j = 0; j < face.mNumIndices; ++j) {
            indices[ii] = face.mIndices[j];
        }
    }

    for (u32 i = 0; i < mesh->mNumBones; ++i) {
        struct aiBone* bone = mesh->mBones[i];

        // Apply bone indices and weights to vertices
        for (u32 j = 0; j < bone->mNumWeights; ++j) {
            struct aiVertexWeight* weight = &bone->mWeights[j];
            skinned_vertex_3d* v = &vertices[weight->mVertexId];
            u8 count = vertex_bone_counts[weight->mVertexId];

            // Each vertex can only be affected by 4 bones.
            // FIXME: Need some sort of fix for if this overflows.
            if (count < KANIMATION_MAX_VERTEX_BONE_WEIGHTS) {
                v->bone_ids.elements[count] = i;
                v->weights.elements[count] = weight->mWeight;
                vertex_bone_counts[weight->mVertexId]++;
            } else {
                KWARN("Vertex id %u already has the max number of bone_ids and weights that can influence it.");
            }
        }

        // LEFTOFF: Figure out how to query bone hierarchy

        // TODO: transpose offset matrix
    }

    for (u32 i = 0; i < mesh->mNumFaces; ++i) {
        struct aiFace face = mesh->mFaces[i];
        for (u32 j = 0; j < face.mNumIndices; ++j) {
            darray_push(indices, face.mIndices[j]);
        }
    }

    struct aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
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
        colour4 diffuse = vec4_one();
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
}

static void process_node(kname package_name, struct aiNode* node, const struct aiScene* scene) {
    // Process each mesh in the current node.
    for (u32 i = 0; i < node->mNumMeshes; ++i) {
        struct aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        skinned_mesh m = process_mesh(package_name, mesh, scene);
        // TODO: push to mesh list
    }

    for (u32 i = 0; i < node->mNumChildren; ++i) {
        process_node(package_name, node->mChildren[i], scene);
    }
}

b8 load_assimp_model(const char* source_path, kname package_name) {
    const struct aiScene* scene = aiImportFile(source_path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        KERROR("Error importing via assimp: %s", aiGetErrorString());
        return false;
    }

    process_node(package_name, scene->mRootNode, scene);

    // TODO:
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

// NOTE: This is the new version.
b8 anim_asset_from_assimp(const char* source_path, kname package_name, kanimation_asset* out_asset) {
    const struct aiScene* scene = aiImportFile(source_path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        KERROR("Error importing via assimp: %s", aiGetErrorString());
        return false;
    }

    kzero_memory(out_asset, sizeof(kanimation_asset));
    out_asset->global_inverse_transform = mat4_from_ai(&scene->mRootNode->mTransformation);
    // TODO: Does this need to be the inverse?

    // Get all unique bones across all meshes.
    kanimated_mesh_bone bones[KANIMATION_MAX_BONES] = {0};
    u32 bone_count = 0;
    for (u32 m = 0; m < scene->mNumMeshes; ++m) {
        struct aiMesh* mesh = scene->mMeshes[m];
        for (u32 b = 0; b < mesh->mNumBones; ++b) {
            struct aiBone* ai_bone = mesh->mBones[b];
            kanimated_mesh_bone* bone = &bones[bone_count];
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

    out_asset->bones = KALLOC_TYPE_CARRAY(kanimated_mesh_bone, bone_count);
    out_asset->bone_count = bone_count;
    KCOPY_TYPE_CARRAY(out_asset->bones, bones, kanimated_mesh_bone, bone_count);

    // Flatten the node structure into a single array and reference by index instead.
    u32 node_capacity = 256;
    u32 node_count = 0;
    kanimated_mesh_node* nodes = KALLOC_TYPE_CARRAY(kanimated_mesh_node, node_capacity);

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
            nodes = KREALLOC_TYPE_CARRAY(nodes, kanimated_mesh_node, node_capacity, node_capacity * 2);
            node_capacity *= 2;
        }
        kanimated_mesh_node* node = &nodes[node_count];
        kzero_memory(node, sizeof(kanimated_mesh_node));
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
                kanimated_mesh_node* cn = &nodes[index];
                cn->children = KREALLOC_TYPE_CARRAY(cn->children, u32, cn->child_count, cn->child_count + 1);
                cn->children[cn->child_count++] = child_index;
                nodes[child_index].parent_index = index;
            }
        }
    }

    out_asset->nodes = KALLOC_TYPE_CARRAY(kanimated_mesh_node, node_count);
    KCOPY_TYPE_CARRAY(out_asset->nodes, nodes, kanimated_mesh_node, node_count);
    out_asset->node_count = node_count;
    KFREE_TYPE_CARRAY(nodes, kanimated_mesh_node, node_capacity);

    // Copy channels and keys
    u32 animation_count = out_asset->animation_count;
    out_asset->animation_count = animation_count;
    out_asset->animations = KALLOC_TYPE_CARRAY(kanimated_mesh_animation, out_asset->animation_count);
    for (u32 a = 0; a < animation_count; ++a) {
        struct aiAnimation* anim = scene->mAnimations[a];
        kanimated_mesh_animation* out = &out_asset->animations[a];
        out->name = kname_create(anim->mName.data);
        out->duration = anim->mDuration;
        out->ticks_per_second = anim->mTicksPerSecond;
        out->channel_count = anim->mNumChannels;
        out->channels = KALLOC_TYPE_CARRAY(kanimated_mesh_channel, out->channel_count);
        for (u32 c = 0; c < out->channel_count; c++) {
            struct aiNodeAnim* chn = anim->mChannels[c];
            kanimated_mesh_channel* oc = &out->channels[c];
            kzero_memory(oc, sizeof(kanimated_mesh_channel));
            oc->name = kname_create(chn->mNodeName.data);

            // Positions
            oc->pos_count = chn->mNumPositionKeys;
            if (oc->pos_count) {
                oc->positions = KALLOC_TYPE_CARRAY(anim_key_vec3, oc->pos_count);
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
                oc->rotations = KALLOC_TYPE_CARRAY(anim_key_quat, oc->rot_count);
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
                oc->scales = KALLOC_TYPE_CARRAY(anim_key_vec3, oc->scale_count);
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

    // Release all assimp resources.
    aiReleaseImport(scene);

    return true;
}

void anim_asset_destroy(kanimation_asset* asset) {
    if (!asset) {
        return;
    }

    for (u32 i = 0; i < asset->animation_count; ++i) {
        kanimated_mesh_animation* a = &asset->animations[i];
        for (u32 c = 0; c < a->channel_count; c++) {
            kanimated_mesh_channel* ch = &a->channels[c];
            KFREE_TYPE_CARRAY(ch->positions, anim_key_vec3, ch->pos_count);
            KFREE_TYPE_CARRAY(ch->rotations, anim_key_quat, ch->rot_count);
            KFREE_TYPE_CARRAY(ch->scales, anim_key_vec3, ch->scale_count);
        }
        KFREE_TYPE_CARRAY(a->channels, kanimated_mesh_channel, a->channel_count);
    }
    KFREE_TYPE_CARRAY(asset->animations, kanimated_mesh_animation, asset->animation_count);
    KFREE_TYPE_CARRAY(asset->bones, kanimated_mesh_bone, asset->bone_count);
    for (u32 i = 0; i < asset->node_count; ++i) {
        KFREE_TYPE_CARRAY(asset->nodes[i].children, u32, asset->nodes[i].child_count);
    }
    KFREE_TYPE_CARRAY(asset->nodes, kanimated_mesh_node, asset->node_count);
}

b8 import_from_path(const char* source_path, const char* target_path, u8 option_count, const import_option* options) {
    if (!source_path || !string_length(source_path)) {
        KERROR("Path is required. Import failed.");
        return false;
    }

    // The source file extension dictates what importer is used.
    const char* source_extension = string_extension_from_path(source_path, true);
    if (!source_extension) {
        return false;
    }

    /* const char* target_folder = string_directory_from_path(target_path);
    if (!target_folder) {
        return false;
    } */

    const char* target_filename = string_filename_no_extension_from_path(target_path);
    if (!target_filename) {
        return false;
    }

    b8 success = false;

    // NOTE: No VFS state available here. Use raw filesystem instead here.

    if (strings_equali(source_extension, ".obj")) {
        // optional
        const char* mtl_target_dir = get_option_value("mtl_target_path", option_count, options);
        // optional
        const char* package_name = get_option_value("package_name", option_count, options);

        if (!obj_2_ksm(source_path, target_path, mtl_target_dir, package_name)) {
            goto import_from_path_cleanup;
        }

    } else if (strings_equali(source_extension, ".mtl")) {

        // required
        const char* mtl_target_dir = get_option_value("mtl_target_path", option_count, options);
        if (!mtl_target_dir) {
            KERROR("mtl_2_kmt requires property 'mtl_target_path' to be set.");
            goto import_from_path_cleanup;
        }

        // required
        const char* package_name = get_option_value("package_name", option_count, options);
        if (!package_name) {
            KERROR("mtl_2_kmt requires property 'package_name' to be set.");
            goto import_from_path_cleanup;
        }

        if (!mtl_2_kmt(source_path, target_filename, mtl_target_dir, package_name)) {
            goto import_from_path_cleanup;
        }
    } else if (extension_is_audio(source_extension)) {
        if (!source_audio_2_kaf(source_path, target_path)) {
            goto import_from_path_cleanup;
        }
    } else if (extension_is_image(source_extension)) {
        b8 flip_y = true;
        kpixel_format output_format = KPIXEL_FORMAT_UNKNOWN;

        // Extract optional properties.
        const char* flip_y_str = get_option_value("flip_y", option_count, options);
        if (flip_y_str) {
            string_to_bool(flip_y_str, &flip_y);
        }

        const char* output_format_str = get_option_value("output_format", option_count, options);
        if (output_format_str) {
            output_format = string_to_kpixel_format(output_format_str);
        }

        if (!source_image_2_kbi(source_path, target_path, flip_y, output_format)) {
            goto import_from_path_cleanup;
        }
    } else if (strings_equali(source_extension, ".fnt")) {
        if (!fnt_2_kbf(source_path, target_path)) {
            goto import_from_path_cleanup;
        }
    } else {
        KERROR("Unknown file extension (%s) provided in import path '%s'", source_extension, source_path);
        goto import_from_path_cleanup;
    }

    success = true;
import_from_path_cleanup:

    if (source_extension) {
        string_free(source_extension);
    }

    return success;
}

b8 import_all_from_manifest(const char* manifest_path) {
    if (!manifest_path) {
        return false;
    }

    const char* asset_base_directory = string_directory_from_path(manifest_path);
    if (!asset_base_directory) {
        KERROR("Failed to obtain base directory of manifest file. See logs for details.");
        return false;
    }

    // Read and deserialize the manifest first.
    const char* manifest_content = filesystem_read_entire_text_file(manifest_path);
    if (!manifest_content) {
        KERROR("Failed to read manifest file. See logs for details.");
        return false;
    }

    asset_manifest manifest = {0};
    if (!kpackage_parse_manifest_file_content(manifest_path, &manifest)) {
        KERROR("Failed to parse asset manifest. See logs for details.");
        return false;
    }

    u32 asset_count = darray_length(manifest.assets);

    KINFO("Asset manifest '%s' has a total listing of %u assets.", manifest_path, asset_count);

    for (u32 i = 0; i < asset_count; ++i) {
        asset_manifest_asset* asset = &manifest.assets[i];
        if (!asset->source_path) {
            KTRACE("Asset '%s' (%s) does NOT have a source_path. Nothing to import.", kname_string_get(asset->name), asset->path);
        } else {
            KINFO("Asset '%s' (%s) DOES have a source_path of '%s'. Importing...", kname_string_get(asset->name), asset->path, asset->source_path);

            // The source file extension dictates what importer is used.
            const char* source_extension = string_extension_from_path(asset->source_path, true);
            if (!source_extension) {
                KWARN("Unable to determine source extension for path '%s'. Skipping import.", asset->source_path);
                continue;
            }

            if (strings_equali(source_extension, ".obj")) {
                // NOTE: Using defaults for this.
                const char* mtl_target_dir = string_format("%s/%s", manifest.path, "assets/materials/");
                const char* package_name = kname_string_get(manifest.name);

                if (!obj_2_ksm(asset->source_path, asset->path, mtl_target_dir, package_name)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (strings_equali(source_extension, ".mtl")) {
                const char* mtl_target_dir = string_directory_from_path(asset->path);
                if (!mtl_target_dir) {
                    KERROR("mtl_2_kmt requires property 'mtl_target_path' to be set.");
                    goto import_all_from_manifest_cleanup;
                }

                const char* package_name = kname_string_get(manifest.name);
                if (!mtl_2_kmt(asset->source_path, asset->path, mtl_target_dir, package_name)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (extension_is_audio(source_extension)) {
                if (!source_audio_2_kaf(asset->source_path, asset->path)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (extension_is_image(source_extension)) {
                // Always assume y should be flipped on import.
                b8 flip_y = true;
                // NOTE: When importing this way, always use the pixel format as provided by the asset.
                kpixel_format output_format = KPIXEL_FORMAT_UNKNOWN;

                if (!source_image_2_kbi(asset->source_path, asset->path, flip_y, output_format)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (strings_equali(source_extension, ".fnt")) {
                if (!fnt_2_kbf(asset->source_path, asset->path)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else {
                KERROR("Unknown file extension (%s) provided in import path '%s'", source_extension, asset->source_path);
                goto import_all_from_manifest_cleanup;
            }

        import_all_from_manifest_cleanup:

            if (source_extension) {
                string_free(source_extension);
            }
        }
    }

    return true;
}

// Returns the index of the option. -1 if not found.
static i16 get_option_index(const char* name, u8 option_count, const import_option* options) {
    if (!name || !option_count || !options) {
        return -1;
    }

    for (u8 i = 0; i < option_count; ++i) {
        if (strings_equali(name, options[i].name)) {
            return (i16)i;
        }
    }

    return -1;
}

static const char* get_option_value(const char* name, u8 option_count, const import_option* options) {
    i16 index = get_option_index(name, option_count, options);
    if (index < 0) {
        return 0;
    }

    return options[index].value;
}

static b8 extension_is_audio(const char* extension) {
    const char* extensions[3] = {".mp3", ".ogg", ".wav"};
    for (u8 i = 0; i < 3; ++i) {
        if (strings_equali(extension, extensions[i])) {
            return true;
        }
    }

    return false;
}

static b8 extension_is_image(const char* extension) {
    const char* extensions[5] = {".jpg", ".jpeg", ".png", ".tga", ".bmp"};
    for (u8 i = 0; i < 5; ++i) {
        if (strings_equali(extension, extensions[i])) {
            return true;
        }
    }

    return false;
}

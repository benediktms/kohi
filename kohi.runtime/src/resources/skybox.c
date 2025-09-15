#include "skybox.h"

#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/kshader_system.h"
#include "systems/texture_system.h"
#include <runtime_defines.h>

b8 skybox_create(skybox_config config, skybox* out_skybox) {
    if (!out_skybox) {
        KERROR("skybox_create requires a valid pointer to out_skybox!");
        return false;
    }

    out_skybox->cubemap_name = config.cubemap_name;
    out_skybox->state = SKYBOX_STATE_CREATED;
    out_skybox->cubemap = 0;

    return true;
}

b8 skybox_initialize(skybox* sb) {
    if (!sb) {
        KERROR("skybox_initialize requires a valid pointer to sb!");
        return false;
    }

    sb->shader_set0_instance_id = INVALID_ID;

    sb->state = SKYBOX_STATE_INITIALIZED;

    return true;
}

b8 skybox_load(skybox* sb) {
    if (!sb) {
        KERROR("skybox_load requires a valid pointer to sb!");
        return false;
    }
    sb->state = SKYBOX_STATE_LOADING;

    sb->geometry = geometry_generate_cube(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, sb->cubemap_name);
    if (!renderer_geometry_upload(&sb->geometry)) {
        KERROR("Failed to upload skybox geometry.");
    }

    sb->cubemap = texture_cubemap_acquire_sync(sb->cubemap_name);

    /* struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system; */

    /* kshader skybox_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SKYBOX), kname_create(PACKAGE_NAME_RUNTIME)); // TODO: allow configurable shader.
    sb->shader_set0_instance_id = renderer_shader_acquire_binding_set_instance(renderer_system, skybox_shader, 0); */
    /* KASSERT_DEBUG(sb->shader_set0_instance_id != INVALID_ID_U32); */
    sb->state = SKYBOX_STATE_LOADED;

    return true;
}

b8 skybox_unload(skybox* sb) {
    if (!sb) {
        KERROR("skybox_unload requires a valid pointer to sb!");
        return false;
    }
    sb->state = SKYBOX_STATE_UNDEFINED;

    struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system;

    kshader skybox_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SKYBOX), kname_create(PACKAGE_NAME_RUNTIME)); // TODO: allow configurable shader.
    renderer_shader_release_binding_set_instance(renderer_system, skybox_shader, 0, sb->shader_set0_instance_id);
    sb->shader_set0_instance_id = INVALID_ID;

    renderer_geometry_destroy(&sb->geometry);
    geometry_destroy(&sb->geometry);

    if (sb->cubemap_name) {
        if (sb->cubemap) {
            texture_release(sb->cubemap);
            sb->cubemap = 0;
        }

        sb->cubemap_name = 0;
    }

    return true;
}

/**
 * @brief Destroys the provided skybox.
 *
 * @param sb A pointer to the skybox to be destroyed.
 */
void skybox_destroy(skybox* sb) {
    if (!sb) {
        KERROR("skybox_destroy requires a valid pointer to sb!");
        return;
    }
    sb->state = SKYBOX_STATE_UNDEFINED;

    // If loaded, unload first, then destroy.
    if (sb->shader_set0_instance_id != INVALID_ID) {
        b8 result = skybox_unload(sb);
        if (!result) {
            KERROR("skybox_destroy() - Failed to successfully unload skybox before destruction.");
        }
    }
}

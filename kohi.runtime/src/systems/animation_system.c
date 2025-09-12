#include "animation_system.h"

#include "core/engine.h"
#include "debug/kassert.h"
#include "logger.h"
#include "renderer/renderer_frontend.h"

b8 animation_system_initialize(u64* memory_requirement, animation_system_state* memory, const animation_system_config* config) {
    *memory_requirement = sizeof(animation_system_state);
    if (!memory) {
        return true;
    }

    // NOTE: perform config/init here.

    animation_system_state* state = (animation_system_state*)memory;
    // Global lighting storage buffer
    u64 buffer_size = sizeof(animation_global_ssbo_data);
    state->global_animation_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT);
    KASSERT(state->global_animation_ssbo != KRENDERBUFFER_INVALID);
    KDEBUG("Created animation global storage buffer.");

    return true;
}

void animation_system_shutdown(animation_system_state* state) {
    if (state) {
        renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, state->global_animation_ssbo);
    }
}

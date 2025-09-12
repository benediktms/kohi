#pragma once

#include "defines.h"
#include "renderer/renderer_types.h"

#define KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL "Kohi.StorageBuffer.AnimationsGlobal"

typedef struct animation_system_config {
    u32 dummy;
} animation_system_config;

typedef struct animation_system_state {
    krenderbuffer global_animation_ssbo;
} animation_system_state;

typedef struct animation_global_ssbo_data {
    mat4 stuff[100];
} animation_global_ssbo_data;

b8 animation_system_initialize(u64* memory_requirement, animation_system_state* memory, const animation_system_config* config);
void animation_system_shutdown(animation_system_state* state);

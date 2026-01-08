#pragma once

#include "renderer/nine_slice.h"
#include "standard_ui_system.h"

typedef struct sui_button_internal_data {
	vec4 colour;
	nine_slice nslice;
	u32 binding_instance_id;
} sui_button_internal_data;

KAPI b8 sui_button_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control);
KAPI void sui_button_control_destroy(standard_ui_state* state, struct sui_control* self);
KAPI b8 sui_button_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height);
KAPI b8 sui_button_control_width_set(standard_ui_state* state, struct sui_control* self, i32 width);

KAPI b8 sui_button_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_button_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data);

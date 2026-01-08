#pragma once

#include "standard_ui_system.h"

struct geometry;

typedef struct sui_panel_internal_data {
	vec4 colour;
	kgeometry g;
	u32 binding_instance_id;
	b8 is_dirty;
} sui_panel_internal_data;

KAPI b8 sui_panel_control_create(standard_ui_state* state, const char* name, vec2 size, vec4 colour, struct sui_control* out_control);
KAPI void sui_panel_control_destroy(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_panel_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_panel_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data);

KAPI vec2 sui_panel_size(standard_ui_state* state, struct sui_control* self);
KAPI b8 sui_panel_control_resize(standard_ui_state* state, struct sui_control* self, vec2 new_size);

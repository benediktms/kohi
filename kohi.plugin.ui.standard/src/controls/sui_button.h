#pragma once

#include "renderer/nine_slice.h"
#include "standard_ui_system.h"
#include "systems/font_system.h"

typedef enum sui_button_type {
	// Just a regular button - no content like text or image.
	SUI_BUTTON_TYPE_BASIC,
	SUI_BUTTON_TYPE_TEXT,
} sui_button_type;

typedef struct sui_button_internal_data {
	sui_button_type button_type;

	vec4 colour;
	nine_slice nslice;
	u32 binding_instance_id;

	sui_control label;
} sui_button_internal_data;

KAPI b8 sui_button_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control);
KAPI b8 sui_button_control_create_with_text(standard_ui_state* state, const char* name, font_type type, kname font_name, u16 font_size, const char* text_content, struct sui_control* out_control);
KAPI void sui_button_control_destroy(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_button_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height);
KAPI b8 sui_button_control_width_set(standard_ui_state* state, struct sui_control* self, i32 width);
KAPI b8 sui_button_control_text_set(standard_ui_state* state, struct sui_control* self, const char* text);

KAPI b8 sui_button_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_button_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data);

#pragma once

#include "standard_ui_system.h"

#include <systems/font_system.h>

typedef struct sui_tree_item_internal_data {
	vec2i size;
	vec4 colour;
	u32 binding_instance_id;

	sui_control toggle_button;
	sui_control label;

	sui_control child_container;

	u64 context;

	// darray
	struct sui_control* children;
} sui_tree_item_internal_data;

KAPI b8 sui_tree_item_control_create(
	standard_ui_state* state,
	const char* name,
	u16 initial_width,
	font_type type,
	kname font_name,
	u16 font_size,
	const char* text,
	u64 context,
	struct sui_control* out_control);
KAPI void sui_tree_item_control_destroy(standard_ui_state* state, struct sui_control* self);
KAPI b8 sui_tree_item_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_tree_item_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data);

KAPI void sui_tree_item_control_width_set(standard_ui_state* state, struct sui_control* self, u16 width);

KAPI void sui_tree_item_text_set(standard_ui_state* state, struct sui_control* self, const char* text);
KAPI const char* sui_tree_item_text_get(standard_ui_state* state, struct sui_control* self);

KAPI u64 sui_tree_item_context_get(standard_ui_state* state, struct sui_control* self);
KAPI void sui_tree_item_context_set(standard_ui_state* state, struct sui_control* self, u64 context);

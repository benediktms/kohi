#pragma once

#include "standard_ui_system.h"

#include <defines.h>
#include <renderer/nine_slice.h>
#include <renderer/renderer_types.h>
#include <systems/font_system.h>

struct standard_ui_render_data;

typedef enum sui_textbox_type {
	SUI_TEXTBOX_TYPE_STRING,
	SUI_TEXTBOX_TYPE_INT,
	SUI_TEXTBOX_TYPE_FLOAT
} sui_textbox_type;

typedef struct sui_textbox_internal_data {
	vec2i size;
	vec4 colour;
	sui_textbox_type type;
	nine_slice nslice;
	nine_slice focused_nslice;
	u32 binding_instance_id;
	sui_control content_label;
	sui_control cursor;
	sui_control highlight_box;
	range32 highlight_range;
	u32 cursor_position;
	f32 text_view_offset;
	sui_clip_mask clip_mask;

	// Cached copy of the internal label's line height (taken in turn from its font.)
	f32 label_line_height;

	// HACK: Need to store a pointer to the standard ui state here because
	// the event system can only pass a single pointer, which is already occupied
	// by "self". Should probably re-think this before adding too many more controls.
	struct standard_ui_state* state;
} sui_textbox_internal_data;

KAPI b8 sui_textbox_control_create(standard_ui_state* state, const char* name, font_type font_type, kname font_name, u16 font_size, const char* text, sui_textbox_type type, struct sui_control* out_control);

KAPI void sui_textbox_control_destroy(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_textbox_control_size_set(standard_ui_state* state, struct sui_control* self, i32 width, i32 height);
KAPI b8 sui_textbox_control_width_set(standard_ui_state* state, struct sui_control* self, i32 width);
KAPI b8 sui_textbox_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height);

KAPI b8 sui_textbox_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);

KAPI b8 sui_textbox_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data);

KAPI const char* sui_textbox_text_get(standard_ui_state* state, struct sui_control* self);
KAPI void sui_textbox_text_set(standard_ui_state* state, struct sui_control* self, const char* text);

// Deletes text at cursor position. If a highlight range exists, the entire range is deleted.
// Updates cursor position and highlight range accordingly.
KAPI void sui_textbox_delete_at_cursor(standard_ui_state* state, struct sui_control* self);

// Select all and set cursor to the end.
KAPI void sui_textbox_select_all(standard_ui_state* state, struct sui_control* self);
// Select none and set cursor to the beginning.
KAPI void sui_textbox_select_none(standard_ui_state* state, struct sui_control* self);

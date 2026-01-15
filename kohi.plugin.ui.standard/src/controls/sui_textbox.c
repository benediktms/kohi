#include "sui_textbox.h"

#include <containers/darray.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/input.h>
#include <defines.h>
#include <input_types.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <renderer/nine_slice.h>
#include <renderer/renderer_frontend.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/kshader_system.h>
#include <systems/ktransform_system.h>

#include "controls/sui_label.h"
#include "controls/sui_panel.h"
#include "math/math_types.h"
#include "renderer/standard_ui_renderer.h"
#include "standard_ui_defines.h"
#include "standard_ui_system.h"
#include "systems/texture_system.h"

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context);
static b8 sui_textbox_on_paste(u16 code, void* sender, void* listener_inst, event_context context);
static void sui_textbox_on_focus(struct standard_ui_state* state, sui_control* self);
static void sui_textbox_on_unfocus(struct standard_ui_state* state, sui_control* self);

static f32 sui_textbox_calculate_cursor_offset(standard_ui_state* state, u32 string_pos, const char* full_string, sui_textbox_internal_data* internal_data) {
	if (string_pos == 0) {
		return 0;
	}

	char* copy = string_duplicate(full_string);
	u32 len = string_length(copy);
	char* mid_target = copy;
	string_mid(mid_target, full_string, 0, string_pos);

	vec2 size = vec2_zero();
	sui_label_internal_data* label_data = ((sui_label_internal_data*)internal_data->content_label.internal_data);
	if (label_data->type == FONT_TYPE_BITMAP) {
		font_system_bitmap_font_measure_string(state->font_system, label_data->bitmap_font, mid_target, &size);
	} else if (label_data->type == FONT_TYPE_SYSTEM) {
		font_system_system_font_measure_string(state->font_system, label_data->system_font, mid_target, &size);
	} else {
		KFATAL("hwhat");
	}

	// Make sure to cleanup the string.
	// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
	kfree((char*)copy, len + 1, MEMORY_TAG_STRING);

	// Use the x-axis of the mesurement to place the cursor.
	return size.x;
}

static void sui_textbox_update_highlight_box(standard_ui_state* state, sui_control* self) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	sui_label_internal_data* label_data = typed_data->content_label.internal_data;

	if (typed_data->highlight_range.size == 0) {
		typed_data->highlight_box.is_visible = false;
		return;
	}

	typed_data->highlight_box.is_visible = true;

	// Offset from the start of the string.
	f32 offset_start = sui_textbox_calculate_cursor_offset(state, typed_data->highlight_range.offset, label_data->text, self->internal_data);
	f32 offset_end = sui_textbox_calculate_cursor_offset(state, typed_data->highlight_range.offset + typed_data->highlight_range.size, label_data->text, self->internal_data);
	f32 width = offset_end - offset_start;
	f32 padding = typed_data->nslice.corner_size.x;
	f32 padding_y = typed_data->nslice.corner_size.y;

	vec3 initial_pos = ktransform_position_get(typed_data->highlight_box.ktransform);
	/* initial_pos.y = -typed_data->label_line_height + 10.0f; */
	// positive line height is below
	// negative should be above?
	initial_pos.y = padding_y * 0.5f; //-typed_data->label_line_height; // * -0.5f;
	ktransform_position_set(typed_data->highlight_box.ktransform, (vec3){padding + offset_start, initial_pos.y, initial_pos.z});
	ktransform_scale_set(typed_data->highlight_box.ktransform, (vec3){width, 1.0f, 1.0f});
}

static void sui_textbox_update_cursor_position(standard_ui_state* state, sui_control* self) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	sui_label_internal_data* label_data = typed_data->content_label.internal_data;

	// Offset from the start of the string.
	f32 offset = sui_textbox_calculate_cursor_offset(state, typed_data->cursor_position, label_data->text, self->internal_data);
	f32 padding = typed_data->nslice.corner_size.x;

	// The would-be cursor position, not yet taking padding into account.
	vec3 cursor_pos = {0};
	cursor_pos.x = offset + typed_data->text_view_offset;
	cursor_pos.y = 6.0f; // TODO: configurable

	// Ensure the cursor is within the bounds of the textbox.
	// Don't take the padding into account just yet.
	f32 clip_width = typed_data->size.x - (padding * 2);
	f32 clip_x_min = padding;
	f32 clip_x_max = clip_x_min + clip_width;
	f32 diff = 0;
	if (cursor_pos.x > clip_width) {
		diff = clip_width - cursor_pos.x;
		// Set the cursor right up against the edge, taking padding into account.
		cursor_pos.x = clip_x_max;
	} else if (cursor_pos.x < 0) {
		diff = 0 - cursor_pos.x;
		// Set the cursor right up against the edge, taking padding into account.
		cursor_pos.x = clip_x_min;
	} else {
		// Use the position as-is, but add padding.
		cursor_pos.x += padding;
	}
	// Save the view offset.
	typed_data->text_view_offset += diff;
	// Translate the label forward/backward to line up with the cursor, taking padding into account.
	vec3 label_pos = ktransform_position_get(typed_data->content_label.ktransform);
	ktransform_position_set(typed_data->content_label.ktransform, (vec3){padding + typed_data->text_view_offset, label_pos.y, label_pos.z});

	// Translate the cursor to it's new position.
	ktransform_position_set(typed_data->cursor.ktransform, cursor_pos);
}

b8 sui_textbox_control_create(standard_ui_state* state, const char* name, font_type font_type, kname font_name, u16 font_size, const char* text, sui_textbox_type type, struct sui_control* out_control) {
	if (!sui_base_control_create(state, name, out_control)) {
		return false;
	}

	out_control->internal_data_size = sizeof(sui_textbox_internal_data);
	out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
	sui_textbox_internal_data* typed_data = out_control->internal_data;

	// Reasonable defaults.
	typed_data->size = (vec2i){200, font_size + 10}; // add padding
	typed_data->colour = vec4_one();
	typed_data->type = type;

	out_control->is_focusable = true;

	// Assign function pointers.
	out_control->destroy = sui_textbox_control_destroy;
	out_control->update = sui_textbox_control_update;
	out_control->render = sui_textbox_control_render;
	out_control->on_focus = sui_textbox_on_focus;
	out_control->on_unfocus = sui_textbox_on_unfocus;

	out_control->name = string_duplicate(name);

	char* buffer = string_format("%s_textbox_internal_label", name);

	if (type == SUI_TEXTBOX_TYPE_FLOAT) {
		// Verify the text content. If numeric is required and it isn't numeric, blank it out.
		f32 f = 0;
		if (!string_to_f32(text, &f)) {
			text = "";
		}
	} else if (type == SUI_TEXTBOX_TYPE_INT) {
		i64 i = 0;
		if (!string_to_i64(text, &i)) {
			text = "";
		}
	}

	if (!sui_label_control_create(state, buffer, font_type, font_name, font_size, text, &typed_data->content_label)) {
		KERROR("Failed to create internal label control for textbox. Textbox creation failed.");
		string_free(buffer);
		return false;
	}
	string_free(buffer);
	typed_data->label_line_height = sui_label_line_height_get(state, &typed_data->content_label);

	// Use a panel as the cursor.
	buffer = string_format("%s_textbox_cursor_panel", name);
	if (!sui_panel_control_create(state, buffer, (vec2){1.0f, font_size - 4.0f}, (vec4){1.0f, 1.0f, 1.0f, 1.0f}, &typed_data->cursor)) {
		KERROR("Failed to create internal cursor control for textbox. Textbox creation failed.");
		string_free(buffer);
		return false;
	}
	string_free(buffer);

	// Highlight box.
	buffer = string_format("%s_textbox_highlight_panel", name);
	if (!sui_panel_control_create(state, buffer, (vec2){1.0f, font_size}, (vec4){0.0f, 0.5f, 0.9f, 0.5f}, &typed_data->highlight_box)) {
		KERROR("Failed to create internal highlight box control for textbox. Textbox creation failed.");
		string_free(buffer);
		return false;
	}
	string_free(buffer);

	// HACK: Storing a pointer to the system state here, since the UI system can only pass a
	// single pointer which is already occupied by "self". This needs to be rethought.
	typed_data->state = state;

	// load

	// HACK: TODO: remove hardcoded stuff.
	u32 atlas_x, atlas_y;
	texture_dimensions_get(state->atlas_texture, &atlas_x, &atlas_y);
	vec2i atlas_size = (vec2i){atlas_x, atlas_y};

	vec2i corner_px_size = (vec2i){3, 3};
	vec2i corner_size = (vec2i){10, 10};
	{
		vec2i atlas_min = (vec2i){180, 31};
		vec2i atlas_max = (vec2i){193, 43};
		if (!nine_slice_create(out_control->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice)) {
			KERROR("Failed to generate nine slice.");
			return false;
		}
	}

	{
		vec2i atlas_min = (vec2i){180, 31 + 13};
		vec2i atlas_max = (vec2i){193, 43 + 13};
		if (!nine_slice_create(out_control->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->focused_nslice)) {
			KERROR("Failed to generate nine slice.");
			return false;
		}
	}

	out_control->bounds.x = 0.0f;
	out_control->bounds.y = 0.0f;
	out_control->bounds.width = typed_data->size.x;
	out_control->bounds.height = typed_data->size.y;
	// Setup textbox clipping mask geometry.
	typed_data->clip_mask.reference_id = 1; // TODO: move creation/reference_id assignment.

	kgeometry quad = geometry_generate_quad(typed_data->size.x - (corner_size.x * 2), typed_data->size.y, 0, 0, 0, 0, kname_create("textbox_clipping_box"));
	if (!renderer_geometry_upload(&quad)) {
		KERROR("sui_textbox_control_load - Failed to upload geometry quad");
		return false;
	}

	typed_data->clip_mask.clip_geometry = quad;

	typed_data->clip_mask.render_data.model = mat4_identity();
	// FIXME: Convert this to generate just verts/indices, and upload via the new
	// renderer api functions instead of deprecated geometry functions.
	typed_data->clip_mask.render_data.unique_id = typed_data->clip_mask.reference_id;

	typed_data->clip_mask.render_data.vertex_count = typed_data->clip_mask.clip_geometry.vertex_count;
	typed_data->clip_mask.render_data.vertex_element_size = typed_data->clip_mask.clip_geometry.vertex_element_size;
	typed_data->clip_mask.render_data.vertex_buffer_offset = typed_data->clip_mask.clip_geometry.vertex_buffer_offset;

	typed_data->clip_mask.render_data.index_count = typed_data->clip_mask.clip_geometry.index_count;
	typed_data->clip_mask.render_data.index_element_size = typed_data->clip_mask.clip_geometry.index_element_size;
	typed_data->clip_mask.render_data.index_buffer_offset = typed_data->clip_mask.clip_geometry.index_buffer_offset;

	typed_data->clip_mask.render_data.diffuse_colour = vec4_zero(); // transparent;

	typed_data->clip_mask.clip_ktransform = ktransform_from_position((vec3){corner_size.x, 0.0f, 0.0f}, 0);

	ktransform_parent_set(typed_data->clip_mask.clip_ktransform, out_control->ktransform);

	// Acquire group resources for this control.
	kshader sui_shader = kshader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));

	// Acquire binding set resources for this control.
	typed_data->binding_instance_id = INVALID_ID;
	typed_data->binding_instance_id = kshader_acquire_binding_set_instance(sui_shader, 1);
	if (typed_data->binding_instance_id == INVALID_ID) {
		KFATAL("Unable to acquire shader group resources for button.");
		return false;
	}

	// NOTE: Only parenting the transform, the control. This is to have control over how the
	// clipping mask is attached and drawn. See the render function for the other half of this.
	// TODO: Adjustable padding
	typed_data->content_label.parent = out_control;
	ktransform_parent_set(typed_data->content_label.ktransform, out_control->ktransform);
	ktransform_position_set(typed_data->content_label.ktransform, (vec3){typed_data->nslice.corner_size.x, -2.0f, 0.0f}); // padding/2 for y
	typed_data->content_label.is_active = true;
	if (!standard_ui_system_update_active(state, &typed_data->content_label)) {
		KERROR("Unable to update active state for textbox system text.");
	}

	// Create the cursor and attach it as a child.
	if (!standard_ui_system_control_add_child(state, out_control, &typed_data->cursor)) {
		KERROR("Failed to parent textbox system text.");
	} else {
		// Set an initial position.
		ktransform_position_set(typed_data->cursor.ktransform, (vec3){typed_data->nslice.corner_size.x, typed_data->label_line_height - 4.0f, 0.0f});
		typed_data->cursor.is_active = true;
		if (!standard_ui_system_update_active(state, &typed_data->cursor)) {
			KERROR("Unable to update active state for textbox cursor.");
		}
	}

	// Ensure the cursor position is correct.
	sui_textbox_update_cursor_position(state, out_control);

	// Create the highlight box and attach it as a child.
	// NOTE: Only parenting the transform, the control. This is to have control over how the
	// clipping mask is attached and drawn. See the render function for the other half of this.

	// Set an initial position.
	typed_data->highlight_box.is_active = true;
	typed_data->highlight_box.is_visible = false;
	/* typed_data->highlight_box.parent = self; */
	ktransform_parent_set(typed_data->highlight_box.ktransform, out_control->ktransform);
	/* ktransform_position_set(typed_data->highlight_box.ktransform, (vec3){typed_data->nslice.corner_size.x, typed_data->label_line_height - 4.0f, 0.0f}); */
	if (!standard_ui_system_update_active(state, &typed_data->highlight_box)) {
		KERROR("Unable to update active state for textbox highlight box.");
	}

	// Ensure the highlight box size and position is correct.
	sui_textbox_update_highlight_box(state, out_control);

	event_register(EVENT_CODE_KEY_PRESSED, out_control, sui_textbox_on_key);
	event_register(EVENT_CODE_KEY_RELEASED, out_control, sui_textbox_on_key);

	return true;
}

void sui_textbox_control_destroy(standard_ui_state* state, struct sui_control* self) {
	// unload
	// TODO: unload sub-controls that aren't children (i.e content_label and highlight_box)
	event_unregister(EVENT_CODE_KEY_PRESSED, self, sui_textbox_on_key);
	event_unregister(EVENT_CODE_KEY_RELEASED, self, sui_textbox_on_key);

	sui_base_control_destroy(state, self);
}

b8 sui_textbox_control_size_set(standard_ui_state* state, struct sui_control* self, i32 width, i32 height) {
	if (!self) {
		return false;
	}

	sui_textbox_internal_data* typed_data = self->internal_data;
	typed_data->size.x = width;
	typed_data->size.y = height;
	typed_data->nslice.size.x = width;
	typed_data->nslice.size.y = height;
	typed_data->focused_nslice.size.x = width;
	typed_data->focused_nslice.size.y = height;

	self->bounds.height = height;
	self->bounds.width = width;

	nine_slice_update(&typed_data->nslice, 0);
	nine_slice_update(&typed_data->focused_nslice, 0);

	kgeometry* vg = &typed_data->clip_mask.clip_geometry;
	// HACK: TODO: remove hardcoded stuff.
	vec2i corner_size = (vec2i){10, 10};

	kgeometry quad = geometry_generate_quad(typed_data->size.x - (corner_size.x * 2), typed_data->size.y, 0, 0, 0, 0, 0);
	kfree(quad.indices, quad.index_element_size * quad.index_count, MEMORY_TAG_ARRAY);

	kfree(vg->vertices, vg->vertex_element_size * vg->vertex_count, MEMORY_TAG_ARRAY);
	vg->vertices = quad.vertices;
	vg->extents = quad.extents;

	renderer_geometry_vertex_update(&typed_data->clip_mask.clip_geometry, 0, vg->vertex_count, vg->vertices, false);

	return true;
}
b8 sui_textbox_control_width_set(standard_ui_state* state, struct sui_control* self, i32 width) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	return sui_textbox_control_size_set(state, self, width, typed_data->size.y);
}
b8 sui_textbox_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	return sui_textbox_control_size_set(state, self, typed_data->size.x, height);
}

b8 sui_textbox_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
	if (!sui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	sui_textbox_internal_data* typed_data = self->internal_data;
	nine_slice_render_frame_prepare(&typed_data->nslice, p_frame_data);
	nine_slice_render_frame_prepare(&typed_data->focused_nslice, p_frame_data);

	return true;
}

b8 sui_textbox_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
	if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	b8 is_focused = standard_ui_system_is_control_focused(state, self);

	// Render the nine-slice.
	sui_textbox_internal_data* typed_data = self->internal_data;
	nine_slice* ns = 0;
	if (state->focused == self) {
		ns = &typed_data->focused_nslice;
	} else {
		ns = &typed_data->nslice;
	}

	if (ns->vertex_data.elements) {
		standard_ui_renderable nineslice_renderable = {0};
		nineslice_renderable.render_data.unique_id = self->id.uniqueid;
		nineslice_renderable.render_data.vertex_count = ns->vertex_data.element_count;
		nineslice_renderable.render_data.vertex_element_size = ns->vertex_data.element_size;
		nineslice_renderable.render_data.vertex_buffer_offset = ns->vertex_data.buffer_offset;
		nineslice_renderable.render_data.index_count = ns->index_data.element_count;
		nineslice_renderable.render_data.index_element_size = ns->index_data.element_size;
		nineslice_renderable.render_data.index_buffer_offset = ns->index_data.buffer_offset;
		nineslice_renderable.render_data.model = ktransform_world_get(self->ktransform);
		nineslice_renderable.render_data.diffuse_colour = vec4_mul(is_focused ? state->focused_base_colour : state->unfocused_base_colour, typed_data->colour);

		nineslice_renderable.binding_instance_id = typed_data->binding_instance_id;
		nineslice_renderable.atlas_override = INVALID_KTEXTURE;

		darray_push(render_data->renderables, nineslice_renderable);
	}

	typed_data->cursor.is_visible = is_focused;

	typed_data->clip_mask.render_data.model = ktransform_world_get(typed_data->clip_mask.clip_ktransform);

	// Only perform highlight_box logic if it is visible.
	if (typed_data->highlight_box.is_visible) {
		// Render the highlight box manually so the clip mask can be attached to it.
		// This ensures the highlight boxis rendered and clipped before the cursor or other
		// children are drawn.
		if (!typed_data->highlight_box.render(state, &typed_data->highlight_box, p_frame_data, render_data)) {
			KERROR("Failed to render highlight box for textbox '%s'", self->name);
			return false;
		}

		// Attach clipping mask to highlight box, which would be the last element added.
		u32 renderable_count = darray_length(render_data->renderables);
		render_data->renderables[renderable_count - 1].clip_mask_render_data = &typed_data->clip_mask.render_data;
	}

	// Render the content label manually so the clip mask can be attached to it.
	// This ensures the content label is rendered and clipped before the cursor or other
	// children are drawn. Also render it last so it appears over the highlight box for clarity.
	if (!typed_data->content_label.render(state, &typed_data->content_label, p_frame_data, render_data)) {
		KERROR("Failed to render content label for textbox '%s'", self->name);
		return false;
	}

	return true;
}

const char* sui_textbox_text_get(standard_ui_state* state, struct sui_control* self) {
	if (!self) {
		return 0;
	}

	sui_textbox_internal_data* typed_data = self->internal_data;
	return sui_label_text_get(state, &typed_data->content_label);
}

void sui_textbox_text_set(standard_ui_state* state, struct sui_control* self, const char* text) {
	if (self) {
		sui_textbox_internal_data* typed_data = self->internal_data;

		if (string_length(text)) {
			if (typed_data->type == SUI_TEXTBOX_TYPE_FLOAT) {
				// Verify the text content. If numeric is required and it isn't numeric, blank it out.
				f32 f = 0;
				if (!string_to_f32(text, &f)) {
					text = "";
					KWARN("%s - Textbox '%s' is of type float, but input does not parse to float. Blanking out.", __FUNCTION__, self->name);
				}
			} else if (typed_data->type == SUI_TEXTBOX_TYPE_INT) {
				i64 i = 0;
				if (!string_to_i64(text, &i)) {
					KWARN("%s - Textbox '%s' is of type int, but input does not parse to int. Blanking out.", __FUNCTION__, self->name);
					text = "";
				}
			}
		}

		sui_label_text_set(state, &typed_data->content_label, text);

		// Reset the cursor position when the text is set.
		typed_data->cursor_position = 0;
		sui_textbox_update_cursor_position(state, self);
	}
}

void sui_textbox_delete_at_cursor(standard_ui_state* state, struct sui_control* self) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
	u32 len = string_length(entry_control_text);

	if (!len) {
		sui_label_text_set(state, &typed_data->content_label, "");
		typed_data->cursor_position = 0;
		return;
	}

	char* str = string_duplicate(entry_control_text);
	if (typed_data->highlight_range.size == (i32)len) {
		// Whole range is selected, delete and reset cursor position.
		str = string_empty(str);
		typed_data->cursor_position = 0;
	} else {
		if (typed_data->highlight_range.size > 0) {
			// If there is a selection, delete it.
			string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
			typed_data->cursor_position = typed_data->highlight_range.offset;
		} else if (typed_data->cursor_position < len) {
			// Otherwise delete one character at the selection point.
			string_remove_at(str, entry_control_text, typed_data->cursor_position, 1);
		}
	}

	// Clear the highlight range.
	typed_data->highlight_range.offset = 0;
	typed_data->highlight_range.size = 0;
	sui_textbox_update_highlight_box(state, self);

	sui_label_text_set(state, &typed_data->content_label, str);
	// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
	kfree((char*)str, len + 1, MEMORY_TAG_STRING);
	sui_textbox_update_cursor_position(state, self);
}

void sui_textbox_select_all(standard_ui_state* state, struct sui_control* self) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
	u32 len = string_length(entry_control_text);
	typed_data->highlight_range.size = len;
	typed_data->highlight_range.offset = 0;
	typed_data->cursor_position = len;
	sui_textbox_update_highlight_box(state, self);
	sui_textbox_update_cursor_position(state, self);
}

void sui_textbox_select_none(standard_ui_state* state, struct sui_control* self) {
	sui_textbox_internal_data* typed_data = self->internal_data;
	typed_data->highlight_range.size = 0;
	typed_data->highlight_range.offset = 0;
	typed_data->cursor_position = 0;
	sui_textbox_update_highlight_box(state, self);
	sui_textbox_update_cursor_position(state, self);
}

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
	sui_control* self = listener_inst;
	sui_textbox_internal_data* typed_data = self->internal_data;
	standard_ui_state* state = typed_data->state;
	if (state->focused != self) {
		return false;
	}

	u16 key_code = context.data.u16[0];
	if (code == EVENT_CODE_KEY_PRESSED) {
		b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);
#if KPLATFORM_APPLE
		// On Apple, consider "command" as control.
		// TODO: Need to think this through better for the future to not have platform code here.
		b8 ctrl_held = input_is_key_down(KEY_LSUPER) || input_is_key_down(KEY_RSUPER);
#else
		b8 ctrl_held = input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL) || input_is_key_down(KEY_CONTROL);
#endif

		const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
		u32 len = string_length(entry_control_text);
		if (key_code == KEY_BACKSPACE) {
			if (len == 0) {
				sui_label_text_set(state, &typed_data->content_label, "");
			} else if ((typed_data->cursor_position > 0 || typed_data->highlight_range.size > 0)) {
				char* str = string_duplicate(entry_control_text);
				if (typed_data->highlight_range.size > 0) {
					if (typed_data->highlight_range.size == (i32)len) {
						str = string_empty(str);
						typed_data->cursor_position = 0;
					} else {
						string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
						typed_data->cursor_position = typed_data->highlight_range.offset;
					}
					// Clear the highlight range.
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
					sui_textbox_update_highlight_box(state, self);
				} else {
					string_remove_at(str, entry_control_text, typed_data->cursor_position - 1, 1);
					typed_data->cursor_position--;
				}
				sui_label_text_set(state, &typed_data->content_label, str);
				// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
				kfree((char*)str, len + 1, MEMORY_TAG_STRING);
				sui_textbox_update_cursor_position(state, self);
			}
		} else if (key_code == KEY_DELETE) {
			sui_textbox_delete_at_cursor(state, self);
		} else if (key_code == KEY_LEFT) {
			if (typed_data->cursor_position > 0) {
				if (shift_held) {
					if (typed_data->highlight_range.size == 0) {
						typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
					}
					if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset) {
						typed_data->highlight_range.offset--;
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
					} else {
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
					}
					typed_data->cursor_position--;
				} else {
					if (typed_data->highlight_range.size > 0) {
						typed_data->cursor_position = typed_data->highlight_range.offset;
					} else {
						typed_data->cursor_position--;
					}
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
				}
				sui_textbox_update_highlight_box(state, self);
				sui_textbox_update_cursor_position(state, self);
			}
		} else if (key_code == KEY_RIGHT) {
			// NOTE: cursor position can go past the end of the str so backspacing works right.
			if (typed_data->cursor_position < len) {
				if (shift_held) {
					if (typed_data->highlight_range.size == 0) {
						typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
					}
					if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset + typed_data->highlight_range.size) {
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
					} else {
						typed_data->highlight_range.offset = KCLAMP(typed_data->highlight_range.offset + 1, 0, (i32)len);
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
					}
					typed_data->cursor_position++;
				} else {
					if (typed_data->highlight_range.size > 0) {
						typed_data->cursor_position = typed_data->highlight_range.offset + typed_data->highlight_range.size;
					} else {
						typed_data->cursor_position++;
					}
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
				}

				sui_textbox_update_highlight_box(state, self);
				sui_textbox_update_cursor_position(state, self);
			}
		} else if (key_code == KEY_HOME) {
			if (shift_held) {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = typed_data->cursor_position;
			} else {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = 0;
			}
			typed_data->cursor_position = 0;
			sui_textbox_update_highlight_box(state, self);
			sui_textbox_update_cursor_position(state, self);
		} else if (key_code == KEY_END) {
			if (shift_held) {
				typed_data->highlight_range.offset = typed_data->cursor_position;
				typed_data->highlight_range.size = len - typed_data->cursor_position;
			} else {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = 0;
			}
			typed_data->cursor_position = len;
			sui_textbox_update_highlight_box(state, self);
			sui_textbox_update_cursor_position(state, self);
		} else {
			// Use A-Z and 0-9 as-is.
			char char_code = key_code;
			if ((key_code >= KEY_A && key_code <= KEY_Z)) {
				if (ctrl_held) {
					if (key_code == KEY_A) {
						char_code = 0;
						sui_textbox_select_all(state, self);
					}

					// Paste
					if (key_code == KEY_V) {
						// Request a paste. This will consume the event.
						event_register_single(EVENT_CODE_CLIPBOARD_PASTE, self, sui_textbox_on_paste);
						platform_request_clipboard_content(engine_active_window_get());
						return true;
					}

					// Copy/cut
					if (key_code == KEY_C || key_code == KEY_X) {
						if (typed_data->highlight_range.size > 0) {

							// Copy the selected text to the clipboard, if any is selected.
							const range32* hl = &typed_data->highlight_range;
							char* buf = kallocate(hl->size + 1, MEMORY_TAG_STRING);

							const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
							for (i32 i = 0; i < hl->size; ++i) {
								buf[i] = entry_control_text[hl->offset + i];
							}
							buf[hl->size + 1] = 0;

							platform_clipboard_content_set(engine_active_window_get(), KCLIPBOARD_CONTENT_TYPE_STRING, hl->size + 1, buf);

							// If cutting, remove the selected text from the textbox.
							if (key_code == KEY_X) {
								sui_textbox_delete_at_cursor(state, self);
							}
						}
						return true;
					}
				}
				// TODO: check caps lock.
				if (!shift_held && !ctrl_held) {
					char_code = key_code + 32;
				}
			} else if ((key_code >= KEY_0 && key_code <= KEY_9)) {
				if (shift_held) {
					// NOTE: this handles US standard keyboard layouts.
					// Will need to handle other layouts as well.
					switch (key_code) {
					case KEY_0:
						char_code = ')';
						break;
					case KEY_1:
						char_code = '!';
						break;
					case KEY_2:
						char_code = '@';
						break;
					case KEY_3:
						char_code = '#';
						break;
					case KEY_4:
						char_code = '$';
						break;
					case KEY_5:
						char_code = '%';
						break;
					case KEY_6:
						char_code = '^';
						break;
					case KEY_7:
						char_code = '&';
						break;
					case KEY_8:
						char_code = '*';
						break;
					case KEY_9:
						char_code = '(';
						break;
					}
				}
			} else {
				switch (key_code) {
				case KEY_SPACE:
					char_code = key_code;
					break;
				case KEY_MINUS:
					char_code = shift_held ? '_' : '-';
					break;
				case KEY_EQUAL:
					char_code = shift_held ? '+' : '=';
					break;
				case KEY_PERIOD:
					char_code = shift_held ? '>' : '.';
					break;
				case KEY_COMMA:
					char_code = shift_held ? '<' : ',';
					break;
				case KEY_SLASH:
					char_code = shift_held ? '?' : '/';
					break;
				case KEY_QUOTE:
					char_code = shift_held ? '"' : '\'';
					break;
				case KEY_SEMICOLON:
					char_code = shift_held ? ':' : ';';
					break;
				case KEY_LBRACKET:
					char_code = shift_held ? '{' : '[';
					break;
				case KEY_RBRACKET:
					char_code = shift_held ? '}' : ']';
					break;
				case KEY_BACKSLASH:
					char_code = shift_held ? '|' : '\\';
					break;

				default:
					// Not valid for entry, use 0
					char_code = 0;
					break;
				}
			}

			if (char_code != 0) {
				const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
				u32 len = string_length(entry_control_text);

				// Need to verify that the input is valid before doing it. Otherwise boot.
				if (typed_data->type == SUI_TEXTBOX_TYPE_INT || typed_data->type == SUI_TEXTBOX_TYPE_FLOAT) {
					if (!codepoint_is_numeric(char_code) && (char_code != '.' && char_code != '-' && char_code != '+')) {
						KWARN("not numeric or .-+");
						return true;
					}

					// Each of these is only allowed once.
					if (char_code == '.' || char_code == '-' || char_code == '+') {
						i32 index = string_index_of(entry_control_text, char_code);
						if (index != -1) {
							// Only if not about to be replaced.
							if (typed_data->highlight_range.size == 0 || !IS_IN_RANGE((u32)index, typed_data->cursor_position, (u32)typed_data->highlight_range.offset)) {
								KWARN("duplicate found: '%c'", char_code);
								return true;
							}
						}
					}

					// Decimals are only allowed for float types.
					if (char_code == '.' && typed_data->type == SUI_TEXTBOX_TYPE_INT) {
						KWARN("Decimal not allowed on int textboxes.");
						return true;
					}
				}
				char* str = kallocate(sizeof(char) * (len + 2), MEMORY_TAG_STRING);

				// If text is highlighted, delete highlighted text, then insert at cursor position.
				if (typed_data->highlight_range.size > 0) {
					if (typed_data->highlight_range.size == (i32)len) {
						str = string_empty(str);
						typed_data->cursor_position = 0;
					} else {
						string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
						typed_data->cursor_position = typed_data->highlight_range.offset;
					}
				} else {
					string_copy(str, entry_control_text);
				}

				string_insert_char_at(str, str, typed_data->cursor_position, char_code);
				/* string_format_unsafe(str, "%s%c", entry_control_text, char_code); */

				sui_label_text_set(state, &typed_data->content_label, str);
				kfree(str, len + 2, MEMORY_TAG_STRING);
				if (typed_data->highlight_range.size > 0) {
					// Clear the highlight range.
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
					sui_textbox_update_highlight_box(state, self);
				}

				typed_data->cursor_position++;
				sui_textbox_update_cursor_position(state, self);
			}
		}
	}
	if (self->on_key) {
		sui_keyboard_event evt = {0};
		evt.key = key_code;
		evt.type = code == EVENT_CODE_KEY_PRESSED ? SUI_KEYBOARD_EVENT_TYPE_PRESS : SUI_KEYBOARD_EVENT_TYPE_RELEASE;
		self->on_key(state, self, evt);
		return true;
	}

	return false;
}

static b8 sui_textbox_on_paste(u16 code, void* sender, void* listener_inst, event_context context) {

	kclipboard_context* clip = context.data.custom_data.data;

	// Only handle string data.
	if (clip->content_type == KCLIPBOARD_CONTENT_TYPE_STRING) {

		sui_control* self = listener_inst;
		sui_textbox_internal_data* typed_data = self->internal_data;
		standard_ui_state* state = typed_data->state;

		const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
		u32 insert_length = string_length(clip->content);

		// Verify the text content. If numeric is required and it isn't numeric, cancel the paste.
		if (typed_data->type == SUI_TEXTBOX_TYPE_FLOAT) {
			f32 f = 0;
			if (!string_to_f32(clip->content, &f)) {
				// Consider the event handled, don't let anything else have it.
				return true;
			}
		} else if (typed_data->type == SUI_TEXTBOX_TYPE_INT) {
			i64 i = 0;
			if (!string_to_i64(clip->content, &i)) {
				// Consider the event handled, don't let anything else have it.
				return true;
			}
		}

		u32 len = string_length(entry_control_text);
		char* str = kallocate(sizeof(char) * (len + insert_length + 1), MEMORY_TAG_STRING);

		// If text is highlighted, delete highlighted text, then insert at cursor position.
		if (typed_data->highlight_range.size > 0) {
			if (typed_data->highlight_range.size == (i32)len) {
				str = string_empty(str);
				typed_data->cursor_position = 0;
			} else {
				string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
				typed_data->cursor_position = typed_data->highlight_range.offset;
			}
		} else {
			string_copy(str, entry_control_text);
		}

		string_insert_str_at(str, str, typed_data->cursor_position, clip->content);

		sui_label_text_set(state, &typed_data->content_label, str);
		kfree(str, len + insert_length + 1, MEMORY_TAG_STRING);
		if (typed_data->highlight_range.size > 0) {
			// Clear the highlight range.
			typed_data->highlight_range.offset = 0;
			typed_data->highlight_range.size = 0;
			sui_textbox_update_highlight_box(state, self);
		}

		typed_data->cursor_position += insert_length;
		sui_textbox_update_cursor_position(state, self);
	}

	// Consider the event handled, don't let anything else have it.
	return true;
}

static void sui_textbox_on_focus(struct standard_ui_state* state, sui_control* self) {
	sui_textbox_select_all(state, self);
}

static void sui_textbox_on_unfocus(struct standard_ui_state* state, sui_control* self) {
	sui_textbox_select_none(state, self);
}

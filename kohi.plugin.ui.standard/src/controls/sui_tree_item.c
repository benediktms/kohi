#include "sui_tree_item.h"
#include "controls/sui_button.h"
#include "controls/sui_label.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "standard_ui_system.h"
#include "strings/kstring.h"
#include "systems/ktransform_system.h"

static b8 on_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 toggle_on_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

b8 sui_tree_item_control_create(
	standard_ui_state* state,
	const char* name,
	u16 initial_width,
	font_type type,
	kname font_name,
	u16 font_size,
	const char* text,
	u64 context,
	struct sui_control* out_control) {
	if (!sui_base_control_create(state, name, out_control)) {
		return false;
	}
	const char* toggle_button_name = KNULL;
	const char* label_name = KNULL;
	b8 success = false;

	sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI);

	toggle_button_name = string_format("%s_toggle_button", name);
	if (!sui_button_control_create_with_text(state, toggle_button_name, type, font_name, font_size, ">", &typed_data->toggle_button)) {
		KERROR("Failed to create toggle button for tree item.");
		goto tree_item_create_cleanup;
	}
	standard_ui_system_control_add_child(state, out_control, &typed_data->toggle_button);
	ktransform_position_set(typed_data->toggle_button.ktransform, (vec3){-42.0f, 2.0f, 0});
	sui_button_control_width_set(state, &typed_data->toggle_button, 40);  // FIXME: hardcoded
	sui_button_control_height_set(state, &typed_data->toggle_button, 40); // FIXME: hardcoded
	typed_data->toggle_button.can_mouse_interact = true;
	typed_data->toggle_button.on_click = toggle_on_clicked;

	label_name = string_format("%s_label", name);
	if (!sui_label_control_create(state, label_name, type, font_name, font_size, text, &typed_data->label)) {
		KERROR("Failed to create label for tree item.");
		goto tree_item_create_cleanup;
	}
	standard_ui_system_control_add_child(state, out_control, &typed_data->label);
	ktransform_position_set(typed_data->label.ktransform, (vec3){0.0f, -2.0f, 0}); // FIXME: hardcoded
	typed_data->label.can_mouse_interact = false;

	out_control->bounds.width = initial_width;
	out_control->bounds.height = 4; // FIXME: hardcoded crap

	out_control->internal_click = on_clicked;

	success = true;
tree_item_create_cleanup:
	string_free(toggle_button_name);
	string_free(label_name);

	return success;
}
void sui_tree_item_control_destroy(standard_ui_state* state, struct sui_control* self) {
	if (self) {
		// TODO: the thing
	}
}
b8 sui_tree_item_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
	//
	return true;
}
b8 sui_tree_item_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, struct standard_ui_render_data* render_data) {
	if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	/* sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI); */

	/* if (!typed_data->label.render(state, &typed_data->label, p_frame_data, render_data)) {
		KERROR("Failed to render content label for button '%s'", self->name);
		return false;
	} */

	return true;
}

void sui_tree_item_control_width_set(standard_ui_state* state, struct sui_control* self, u16 width) {
	self->bounds.width = width;
}

void sui_tree_item_text_set(standard_ui_state* state, struct sui_control* self, const char* text) {
	sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI);
	sui_label_text_set(state, &typed_data->label, text);
}

const char* sui_tree_item_text_get(standard_ui_state* state, struct sui_control* self) {
	sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI);
	return sui_label_text_get(state, &typed_data->label);
}

u64 sui_tree_item_context_get(standard_ui_state* state, struct sui_control* self) {
	sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI);
	return typed_data->context;
}
void sui_tree_item_context_set(standard_ui_state* state, struct sui_control* self, u64 context) {
	sui_tree_item_internal_data* typed_data = kallocate(sizeof(sui_tree_item_internal_data), MEMORY_TAG_UI);
	typed_data->context = context;
}

static b8 on_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KDEBUG("outer control clicked");
	if (self->on_click) {
		self->on_click(state, self, event);
	}
	return true;
}

static b8 toggle_on_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KDEBUG("toggle button clicked");
	return false;
}

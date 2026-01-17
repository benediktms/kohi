#include "kui_tree_item.h"
#include "containers/darray.h"
#include "controls/kui_button.h"
#include "controls/kui_label.h"
#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "systems/ktransform_system.h"

static b8 label_on_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 toggle_on_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);

kui_control kui_tree_item_control_create(
	kui_state* state,
	const char* name,
	u16 initial_width,
	font_type type,
	kname font_name,
	u16 font_size,
	const char* text,
	b8 show_toggle_button) {

	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_TREE_ITEM);
	kui_base_control* base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_tree_item_control* typed_control = (kui_tree_item_control*)base;

	const char* toggle_button_name = KNULL;
	const char* label_name = KNULL;
	const char* child_container_name = KNULL;
	b8 success = false;

	// Toggle button
	toggle_button_name = string_format("%s_toggle_button", name);
	typed_control->toggle_button = kui_button_control_create_with_text(state, toggle_button_name, type, font_name, font_size, ">");
	kui_base_control* toggle_base = kui_system_get_base(state, typed_control->toggle_button);
	KASSERT(toggle_base);
	kui_system_control_add_child(state, handle, typed_control->toggle_button);
	ktransform_position_set(toggle_base->ktransform, (vec3){-42.0f, 2.0f, 0});
	kui_button_control_width_set(state, typed_control->toggle_button, 40);	// FIXME: hardcoded
	kui_button_control_height_set(state, typed_control->toggle_button, 40); // FIXME: hardcoded
	toggle_base->on_click = toggle_on_clicked;
	FLAG_SET(toggle_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, true);
	FLAG_SET(toggle_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, show_toggle_button);

	// Label
	label_name = string_format("%s_label", name);
	typed_control->label = kui_label_control_create(state, label_name, type, font_name, font_size, text);
	kui_base_control* label_base = kui_system_get_base(state, typed_control->label);
	KASSERT(label_base);
	kui_system_control_add_child(state, handle, typed_control->label);
	ktransform_position_set(label_base->ktransform, (vec3){0.0f, -2.0f, 0}); // FIXME: hardcoded
	FLAG_SET(label_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, true);
	label_base->internal_click = label_on_clicked;

	base->bounds.width = initial_width;
	base->bounds.height = 40; // FIXME: hardcoded crap

	FLAG_SET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	// Child container.
	child_container_name = string_format("%s_child_container", name);
	typed_control->child_container = kui_base_control_create(state, child_container_name, KUI_CONTROL_TYPE_BASE);
	kui_base_control* container_base = kui_system_get_base(state, typed_control->label);
	KASSERT(container_base);
	kui_system_control_add_child(state, handle, typed_control->child_container);
	ktransform_position_set(container_base->ktransform, (vec3){40.0f, 40.0f, 0});

	success = true;
tree_item_create_cleanup:
	string_free(toggle_button_name);
	string_free(label_name);
	string_free(child_container_name);

	// TODO: control cleanup on failure

	return handle;
}

void kui_tree_item_control_destroy(kui_state* state, kui_control* self) {
	kui_base_control* base = kui_system_get_base(state, *self);
	KASSERT(base);

	if (base->user_data_size && base->user_data) {
		// FIXME: memory tag!
		kfree(base->user_data, base->user_data_size, MEMORY_TAG_ENGINE);
	}

	kui_base_control_destroy(state, self);
}
b8 kui_tree_item_control_update(kui_state* state, kui_control self, struct frame_data* p_frame_data) {
	//
	return true;
}
b8 kui_tree_item_control_render(kui_state* state, kui_control self, struct frame_data* p_frame_data, struct kui_render_data* render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	/* kui_tree_item_internal_data* typed_data = kallocate(sizeof(kui_tree_item_internal_data), MEMORY_TAG_UI); */

	/* if (!typed_data->label.render(state, &typed_data->label, p_frame_data, render_data)) {
		KERROR("Failed to render content label for button '%s'", self->name);
		return false;
	} */

	return true;
}

void kui_tree_item_control_add_child_tree_item(kui_state* state, kui_control self, kui_control child_tree_item) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_tree_item_control* typed_data = (kui_tree_item_control*)base;
	if (!typed_data->children) {
		darray_create(kui_control);
	}

	u32 count = darray_length(typed_data->children);
	darray_push(typed_data->children, child_tree_item);

	kui_system_control_add_child(state, typed_data->child_container, typed_data->children[count].base.handle);
	ktransform_position_set(typed_data->children[count].base.ktransform, (vec3){0.0f, 40.0f * count, 0});
}

void kui_tree_item_control_width_set(kui_state* state, kui_control self, u16 width) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	base->bounds.width = width;
}

void kui_tree_item_text_set(kui_state* state, kui_control self, const char* text) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_tree_item_control* typed_data = (kui_tree_item_control*)base;
	kui_label_text_set(state, typed_data->label, text);
}

const char* kui_tree_item_text_get(kui_state* state, kui_control self) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_tree_item_control* typed_data = (kui_tree_item_control*)base;
	return kui_label_text_get(state, typed_data->label);
}

u64 kui_tree_item_context_get(kui_state* state, kui_control self) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_tree_item_control* typed_data = (kui_tree_item_control*)base;
	return typed_data->context;
}
void kui_tree_item_context_set(kui_state* state, kui_control self, u64 context) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_tree_item_control* typed_data = (kui_tree_item_control*)base;
	typed_data->context = context;
}

static b8 label_on_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KDEBUG("inner label clicked");
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_base_control* parent_base = kui_system_get_base(state, base->parent);
	KASSERT(parent_base);

	if (parent_base->on_click) {
		parent_base->on_click(state, base->parent, event);
	}
	return true;
}

static b8 toggle_on_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	/* kui_button_internal_data* typed_data = self->internal_data; */

	const char* text = kui_button_control_text_get(state, self);
	b8 expanded = text[0] == 'v';
	expanded = !expanded;
	kui_button_control_text_set(state, self, expanded ? "v" : ">");

	return false;
}

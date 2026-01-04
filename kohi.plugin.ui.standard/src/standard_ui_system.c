#include "standard_ui_system.h"

#include <containers/darray.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/input.h>
#include <defines.h>
#include <identifiers/identifier.h>
#include <identifiers/khandle.h>
#include <input_types.h>
#include <kresources/kresource_types.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/resource_types.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/ktransform_system.h>
#include <systems/texture_system.h>

#include "core_resource_types.h"
#include "debug/kassert.h"
#include "kohi.plugin.ui.standard_version.h"
#include "standard_ui_defines.h"
#include "sui_defines.h"
#include "systems/kshader_system.h"

static b8 sui_base_internal_mouse_down(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_up(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_click(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_over(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_out(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_move(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_drag_begin(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_drag(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 sui_base_internal_mouse_drag_end(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

static b8 control_and_ancestors_active_r(const struct sui_control* control) {
	if (!control->is_active) {
		return false;
	}

	if (control->parent) {
		return control_and_ancestors_active_r(control->parent);
	}

	return true;
}

static b8 control_and_ancestors_visible_r(const struct sui_control* control) {
	if (!control->is_visible) {
		return false;
	}

	if (control->parent) {
		return control_and_ancestors_visible_r(control->parent);
	}

	return true;
}

static b8 control_and_ancestors_active_and_visible_r(const struct sui_control* control) {
	return control_and_ancestors_active_r(control) && control_and_ancestors_visible_r(control);
}

static b8 control_process_mouse_event(standard_ui_state* typed_state, sui_control* control, event_context evt_context, u32 inside_count, PFN_mouse_event_callback* inside_callbacks, u32 outside_count, PFN_mouse_event_callback* outside_callbacks, b8 affect_hover_state) {

	// Check if control is active and visible. This should check recursively upward to make sure any
	// disabled/invisible parent controls are taken into account.
	if (!control_and_ancestors_active_and_visible_r(control)) {
		// Skip ones that aren't.
		return false;
	}

	sui_mouse_event evt = {
		.mouse_button = (mouse_buttons)evt_context.data.i16[2],
		.x = evt_context.data.i16[0],
		.y = evt_context.data.i16[1],
	};

	b8 block_propagation = false;
	mat4 model = ktransform_world_get(control->ktransform);
	mat4 inv = mat4_inverse(model);
	vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);

	// If any callback returns false, block propagation of the event.
	if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
		if (affect_hover_state) {
			control->is_hovered = true;
		}
		for (u32 i = 0; i < inside_count; ++i) {
			if (!inside_callbacks[i](typed_state, control, evt)) {
				block_propagation = true;
			}
		}
	} else {
		if (affect_hover_state) {
			control->is_hovered = false;
		}
		for (u32 i = 0; i < outside_count; ++i) {
			if (!outside_callbacks[i](typed_state, control, evt)) {
				block_propagation = true;
			}
		}
	}

	return block_propagation;
}

static b8 standard_ui_system_mouse_down(u16 code, void* sender, void* listener_inst, event_context context) {
	standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

	b8 block_propagation = false;
	for (u32 i = 0; i < typed_state->active_control_count; ++i) {
		sui_control* control = typed_state->active_controls[i];

		PFN_mouse_event_callback inside_callbacks[1] = {
			control->internal_mouse_down,
		};
		if (control_process_mouse_event(typed_state, control, context, 1, inside_callbacks, 0, 0, false)) {
			block_propagation = true;
		}
	}

	KTRACE("ui mouse down, block_propagation = %s", block_propagation ? "yes" : "no");

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 standard_ui_system_mouse_up(u16 code, void* sender, void* listener_inst, event_context context) {
	standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

	b8 block_propagation = false;
	for (u32 i = 0; i < typed_state->active_control_count; ++i) {
		sui_control* control = typed_state->active_controls[i];
		control->is_pressed = false;

		PFN_mouse_event_callback inside_callbacks[1] = {
			control->internal_mouse_up,
		};
		if (control_process_mouse_event(typed_state, control, context, 1, inside_callbacks, 0, 0, false)) {
			block_propagation = true;
		}
	}

	KTRACE("ui mouse up, block_propagation = %s", block_propagation ? "yes" : "no");

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}
static b8 standard_ui_system_click(u16 code, void* sender, void* listener_inst, event_context context) {
	standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

	b8 block_propagation = false;
	for (u32 i = 0; i < typed_state->active_control_count; ++i) {
		sui_control* control = typed_state->active_controls[i];

		PFN_mouse_event_callback inside_callbacks[1] = {
			control->internal_click,
		};
		if (control_process_mouse_event(typed_state, control, context, 1, inside_callbacks, 0, 0, false)) {
			block_propagation = true;
		}
	}

	KTRACE("ui mouse click, block_propagation = %s", block_propagation ? "yes" : "no");

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 standard_ui_system_move(u16 code, void* sender, void* listener_inst, event_context context) {
	standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

	b8 block_propagation = false;
	for (u32 i = 0; i < typed_state->active_control_count; ++i) {
		sui_control* control = typed_state->active_controls[i];

		PFN_mouse_event_callback inside_callbacks[2] = {
			control->internal_mouse_over,
			control->internal_mouse_move,
		};
		PFN_mouse_event_callback outside_callbacks[1] = {
			control->internal_mouse_out,
		};
		if (control_process_mouse_event(typed_state, control, context, 2, inside_callbacks, 1, outside_callbacks, true)) {
			block_propagation = true;
		}
	}

	/* KTRACE("ui mouse move, block_propagation = %s", block_propagation ? "yes" : "no"); */

	return block_propagation;
}

static b8 standard_ui_system_drag(u16 code, void* sender, void* listener_inst, event_context context) {
	standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

	b8 block_propagation = false;
	for (u32 i = 0; i < typed_state->active_control_count; ++i) {
		sui_control* control = typed_state->active_controls[i];

		PFN_mouse_event_callback inside_callback, outside_callback;
		u32 inside_count, outside_count;
		if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
			// Drag begin must start within the control.
			inside_callback = control->internal_mouse_drag_begin;
			inside_count = 1;
			outside_callback = KNULL;
			outside_count = 0;
		} else if (code == EVENT_CODE_MOUSE_DRAGGED) {
			// Drag event can occur inside or outside the control.
			inside_callback = control->internal_mouse_drag;
			inside_count = 1;
			outside_callback = control->internal_mouse_drag;
			outside_count = 1;
		} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
			// Drag end event can occur inside or outside the control.
			inside_callback = control->internal_mouse_drag_end;
			inside_count = 1;
			outside_callback = control->internal_mouse_drag_end;
			outside_count = 1;
		} else {
			KTRACE("Other event...");
			return false;
		}

		if (control_process_mouse_event(typed_state, control, context, inside_count, &inside_callback, outside_count, &outside_callback, true)) {
			block_propagation = true;
		}
	}

	/* KTRACE("ui mouse drag, block_propagation = %s", block_propagation ? "yes" : "no"); */

	return block_propagation;
}

b8 standard_ui_system_initialize(u64* memory_requirement, standard_ui_state* state, standard_ui_system_config* config) {
	if (!memory_requirement) {
		KERROR("standard_ui_system_initialize requires a valid pointer to memory_requirement.");
		return false;
	}
	if (config->max_control_count == 0) {
		KFATAL("standard_ui_system_initialize - config.max_control_count must be > 0.");
		return false;
	}

	// Memory layout: struct + active control array + inactive_control_array
	u64 struct_requirement = sizeof(standard_ui_state);
	u64 active_array_requirement = sizeof(sui_control) * config->max_control_count;
	u64 inactive_array_requirement = sizeof(sui_control) * config->max_control_count;
	*memory_requirement = struct_requirement + active_array_requirement + inactive_array_requirement;

	if (!state) {
		return true;
	}

	state->renderer = engine_systems_get()->renderer_system;
	state->font_system = engine_systems_get()->font_system;

	// Get the shader and the global binding id.
	state->shader = kshader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));
	// Acquire binding set resources for this control.
	state->shader_set0_binding_instance_id = INVALID_ID;
	state->shader_set0_binding_instance_id = kshader_acquire_binding_set_instance(state->shader, 0);
	KASSERT(state->shader_set0_binding_instance_id != INVALID_ID);

	state->config = *config;
	state->active_controls = (void*)((u8*)state + struct_requirement);
	kzero_memory(state->active_controls, sizeof(sui_control) * config->max_control_count);
	state->inactive_controls = (void*)((u8*)state->active_controls + active_array_requirement);
	kzero_memory(state->inactive_controls, sizeof(sui_control) * config->max_control_count);

	sui_base_control_create(state, "__ROOT__", &state->root);
	state->root.is_active = true;
	standard_ui_system_update_active(state, &state->root);

	// Atlas texture.
	state->atlas_texture = texture_acquire_from_package_sync(
		kname_create(STANDARD_UI_DEFAULT_ATLAS_NAME),
		kname_create(PACKAGE_NAME_STANDARD_UI));
	if (state->atlas_texture == INVALID_KTEXTURE) {
		KERROR("Failed to request atlas texture for standard UI.");
		state->atlas_texture = texture_acquire_sync(kname_create(DEFAULT_TEXTURE_NAME));
	}

	// Listen for input events.
	event_register(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
	event_register(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
	event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, state, standard_ui_system_drag);
	event_register(EVENT_CODE_MOUSE_DRAGGED, state, standard_ui_system_drag);
	event_register(EVENT_CODE_MOUSE_DRAG_END, state, standard_ui_system_drag);
	event_register(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
	event_register(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

	state->vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	state->index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	KTRACE("Initialized standard UI system (%s).", KVERSION);

	return true;
}

void standard_ui_system_shutdown(standard_ui_state* state) {
	if (state) {
		// Stop listening for input events.
		event_unregister(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
		event_unregister(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
		event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, state, standard_ui_system_drag);
		event_unregister(EVENT_CODE_MOUSE_DRAGGED, state, standard_ui_system_drag);
		event_unregister(EVENT_CODE_MOUSE_DRAG_END, state, standard_ui_system_drag);
		event_unregister(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
		event_unregister(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

		// Unload and destroy inactive controls.
		for (u32 i = 0; i < state->inactive_control_count; ++i) {
			sui_control* c = state->inactive_controls[i];
			c->destroy(state, c);
		}
		// Unload and destroy active controls.
		for (u32 i = 0; i < state->active_control_count; ++i) {
			sui_control* c = state->active_controls[i];
			c->destroy(state, c);
		}

		// Release texture for UI atlas.
		if (state->atlas_texture) {
			texture_release(state->atlas_texture);
			state->atlas_texture = INVALID_KTEXTURE;
		}
	}
}

b8 standard_ui_system_update(standard_ui_state* state, struct frame_data* p_frame_data) {
	if (!state) {
		return false;
	}

	for (u32 i = 0; i < state->active_control_count; ++i) {
		sui_control* c = state->active_controls[i];
		c->update(state, c, p_frame_data);
	}

	return true;
}

b8 standard_ui_system_render(standard_ui_state* state, sui_control* root, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
	if (!state) {
		return false;
	}

	render_data->ui_atlas = state->atlas_texture;
	render_data->shader_set0_binding_instance_id = state->shader_set0_binding_instance_id;

	if (!root) {
		root = &state->root;
	}

	if (root->render) {
		if (!root->render(state, root, p_frame_data, render_data)) {
			KERROR("Root element failed to render. See logs for more details");
			return false;
		}
	}

	if (root->children) {
		u32 length = darray_length(root->children);
		for (u32 i = 0; i < length; ++i) {
			sui_control* c = root->children[i];
			if (!c->is_visible) {
				continue;
			}
			if (!standard_ui_system_render(state, c, p_frame_data, render_data)) {
				KERROR("Child element failed to render. See logs for more details");
				return false;
			}
		}
	}

	return true;
}

b8 standard_ui_system_update_active(standard_ui_state* state, sui_control* control) {
	if (!state) {
		return false;
	}

	u32* src_limit = control->is_active ? &state->inactive_control_count : &state->active_control_count;
	u32* dst_limit = control->is_active ? &state->active_control_count : &state->inactive_control_count;
	sui_control** src_array = control->is_active ? state->inactive_controls : state->active_controls;
	sui_control** dst_array = control->is_active ? state->active_controls : state->inactive_controls;
	for (u32 i = 0; i < *src_limit; ++i) {
		if (src_array[i] == control) {
			sui_control* c = src_array[i];
			dst_array[*dst_limit] = c;
			(*dst_limit)++;

			// Copy the rest of the array inward.
			kcopy_memory(((u8*)src_array) + (i * sizeof(sui_control*)), ((u8*)src_array) + ((i + 1) * sizeof(sui_control*)), sizeof(sui_control*) * ((*src_limit) - i));
			src_array[*src_limit] = 0;
			(*src_limit)--;
			return true;
		}
	}

	KERROR("Unable to find control to update active on, maybe control is not registered?");
	return false;
}

b8 standard_ui_system_register_control(standard_ui_state* state, sui_control* control) {
	if (!state) {
		return false;
	}

	if (state->total_control_count >= state->config.max_control_count) {
		KERROR("Unable to find free space to register sui control. Registration failed.");
		return false;
	}

	state->total_control_count++;
	// Found available space, put it there.
	state->inactive_controls[state->inactive_control_count] = control;
	state->inactive_control_count++;
	return true;
}

b8 standard_ui_system_control_add_child(standard_ui_state* state, sui_control* parent, sui_control* child) {
	if (!child) {
		return false;
	}

	if (!parent) {
		parent = &state->root;
	}

	if (!parent->children) {
		parent->children = darray_create(sui_control*);
	}

	if (child->parent) {
		if (!standard_ui_system_control_remove_child(state, child->parent, child)) {
			KERROR("Failed to remove child from parent before reparenting.");
			return false;
		}
	}

	darray_push(parent->children, child);
	child->parent = parent;
	ktransform_parent_set(child->ktransform, parent->ktransform);

	return true;
}

b8 standard_ui_system_control_remove_child(standard_ui_state* state, sui_control* parent, sui_control* child) {
	if (!parent || !child) {
		return false;
	}

	if (!parent->children) {
		KERROR("Cannot remove a child from a parent which has no children.");
		false;
	}

	u32 child_count = darray_length(parent->children);
	for (u32 i = 0; i < child_count; ++i) {
		if (parent->children[i] == child) {
			sui_control* popped;
			darray_pop_at(parent->children, i, &popped);
			ktransform_parent_set(parent->ktransform, KTRANSFORM_INVALID);
			child->parent = 0;

			return true;
		}
	}

	KERROR("Unable to remove child which is not a child of given parent.");
	return false;
}

void standard_ui_system_focus_control(standard_ui_state* state, sui_control* control) {
	if (!control) {
		if (state->focused && state->focused->on_unfocus) {
			state->focused->on_unfocus(state, state->focused);
		}
		state->focused = KNULL;
	} else if (control->is_focusable) {
		if (state->focused && state->focused->on_unfocus) {
			state->focused->on_unfocus(state, state->focused);
		}
		state->focused = control;
		if (state->focused && state->focused->on_focus) {
			state->focused->on_focus(state, state->focused);
		}
	}
}

b8 sui_base_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control) {
	if (!out_control) {
		return false;
	}

	// Set all controls to visible by default.
	out_control->is_visible = true;

	// Assign function pointers.
	out_control->destroy = sui_base_control_destroy;
	out_control->update = sui_base_control_update;
	out_control->render = sui_base_control_render;

	out_control->name = string_duplicate(name);
	out_control->id = identifier_create();

	out_control->ktransform = ktransform_create(0);

	// Hook up default internal events. These can be overridden as needed by specialized controls.
	out_control->internal_mouse_down = sui_base_internal_mouse_down;
	out_control->internal_mouse_up = sui_base_internal_mouse_up;
	out_control->internal_click = sui_base_internal_click;
	out_control->internal_mouse_over = sui_base_internal_mouse_over;
	out_control->internal_mouse_out = sui_base_internal_mouse_out;
	out_control->internal_mouse_move = sui_base_internal_mouse_move;
	out_control->internal_mouse_drag_begin = sui_base_internal_mouse_drag_begin;
	out_control->internal_mouse_drag = sui_base_internal_mouse_drag;
	out_control->internal_mouse_drag_end = sui_base_internal_mouse_drag_end;

	return true;
}
void sui_base_control_destroy(standard_ui_state* state, struct sui_control* self) {
	if (self) {
		// TODO: recurse children/unparent?

		if (self->internal_data && self->internal_data_size) {
			kfree(self->internal_data, self->internal_data_size, MEMORY_TAG_UI);
		}
		if (self->name) {
			string_free(self->name);
		}
		kzero_memory(self, sizeof(sui_control));
	}
}

static void sui_recalculate_world_ktransform(standard_ui_state* state, struct sui_control* self) {
	/* ktransform_calculate_local(self->ktransform);
	mat4 local = ktransform_local_get(self->ktransform);

	if (self->parent) {
		sui_recalculate_world_ktransform(state, self->parent);
		mat4 parent_world = ktransform_world_get(self->parent->ktransform);
		mat4 self_world = mat4_mul(local, parent_world);
		ktransform_world_set(self->ktransform, self_world);
	} else {
		ktransform_world_set(self->ktransform, local);
	} */
}

b8 sui_base_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
	if (!self) {
		return false;
	}

	/* sui_recalculate_world_ktransform(state, self); */

	return true;
}
b8 sui_base_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
	if (!self) {
		return false;
	}

	return true;
}

void sui_control_position_set(standard_ui_state* state, struct sui_control* self, vec3 position) {
	ktransform_position_set(self->ktransform, position);
}

vec3 sui_control_position_get(standard_ui_state* state, struct sui_control* self) {
	return ktransform_position_get(self->ktransform);
}

static b8 sui_base_internal_mouse_down(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_down ? self->on_mouse_down(state, self, event) : false;
}

static b8 sui_base_internal_mouse_up(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_up ? self->on_mouse_up(state, self, event) : false;
}

static b8 sui_base_internal_click(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	if (self->is_focusable) {
		if (state->focused != self) {
			standard_ui_system_focus_control(state, self);
		}
	} else {
		// Something else was clicked, unfocus.
		standard_ui_system_focus_control(state, KNULL);
	}

	// Block event propagation by default. User events can override this.
	return self->on_click ? self->on_click(state, self, event) : false;
}

static b8 sui_base_internal_mouse_over(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_over ? self->on_mouse_over(state, self, event) : false;
}

static b8 sui_base_internal_mouse_out(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_out ? self->on_mouse_out(state, self, event) : false;
}

static b8 sui_base_internal_mouse_move(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_move ? self->on_mouse_move(state, self, event) : false;
}

static b8 sui_base_internal_mouse_drag_begin(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_drag_begin ? self->on_mouse_drag_begin(state, self, event) : false;
}

static b8 sui_base_internal_mouse_drag(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_drag ? self->on_mouse_drag(state, self, event) : false;
}

static b8 sui_base_internal_mouse_drag_end(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	if (!self) {
		return true;
	}

	// Block event propagation by default. User events can override this.
	return self->on_mouse_drag_end ? self->on_mouse_drag_end(state, self, event) : false;
}

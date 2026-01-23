#include "kui_scrollable.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/kshader_system.h>

#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "renderer/kui_renderer.h"
#include "systems/ktransform_system.h"

kui_control kui_scrollable_control_create(kui_state* state, const char* name, vec2 size, b8 scroll_x, b8 scroll_y) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_SCROLLABLE);

	kui_base_control* base = kui_system_get_base(state, handle);
	KASSERT(base);

	kui_scrollable_control* typed_control = (kui_scrollable_control*)base;

	base->bounds = vec4_create(0, 0, size.x, size.y);

	// Reasonable defaults.
	typed_control->scroll_x = true;
	typed_control->scroll_y = true;

	// Assign function pointers.
	base->destroy = kui_scrollable_control_destroy;
	base->update = kui_scrollable_control_update;
	base->render = kui_scrollable_control_render;

	// Setup clipping mask geometry.
	base->clip_mask.reference_id = 1; // TODO: move creation/reference_id assignment.

	// FIXME: Use unit position and scale instead?
	base->clip_mask.clip_geometry = geometry_generate_quad(size.x, size.y, 0, 0, 0, 0, kname_create("scrollable_clipping_box"));
	KASSERT(renderer_geometry_upload(&base->clip_mask.clip_geometry));

	base->clip_mask.render_data.model = mat4_identity();
	// FIXME: Convert this to generate just verts/indices, and upload via the new
	// renderer api functions instead of deprecated geometry functions.
	base->clip_mask.render_data.unique_id = base->clip_mask.reference_id;

	base->clip_mask.render_data.vertex_count = base->clip_mask.clip_geometry.vertex_count;
	base->clip_mask.render_data.vertex_element_size = base->clip_mask.clip_geometry.vertex_element_size;
	base->clip_mask.render_data.vertex_buffer_offset = base->clip_mask.clip_geometry.vertex_buffer_offset;

	base->clip_mask.render_data.index_count = base->clip_mask.clip_geometry.index_count;
	base->clip_mask.render_data.index_element_size = base->clip_mask.clip_geometry.index_element_size;
	base->clip_mask.render_data.index_buffer_offset = base->clip_mask.clip_geometry.index_buffer_offset;

	base->clip_mask.render_data.diffuse_colour = vec4_zero(); // transparent;

	base->clip_mask.clip_ktransform = ktransform_create(0);

	ktransform_parent_set(base->clip_mask.clip_ktransform, base->ktransform);

	const char* content_name = string_format("%s_content", name);
	typed_control->content_wrapper = kui_base_control_create(state, content_name, KUI_CONTROL_TYPE_BASE);
	string_free(content_name);

	kui_system_control_add_child(state, handle, typed_control->content_wrapper);

	/* kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	typed_control->binding_instance_id = INVALID_ID;
	typed_control->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	if (typed_control->binding_instance_id == INVALID_ID) {
		KFATAL("Unable to acquire shader binding set resources for label.");
		kui_base_control_destroy(state, &handle);
	} */

	return handle;
}

void kui_scrollable_control_destroy(kui_state* state, kui_control* self) {
	kui_base_control* base = kui_system_get_base(state, *self);
	KASSERT(base);
	/* kui_scrollable_control* typed_control = (kui_scrollable_control*)base; */

	// destroy clipping mask
	renderer_geometry_destroy(&base->clip_mask.clip_geometry);

	kui_base_control_destroy(state, self);
}

b8 kui_scrollable_control_update(kui_state* state, kui_control self, struct frame_data* p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 kui_scrollable_control_render(kui_state* state, kui_control self, struct frame_data* p_frame_data, kui_render_data* render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control* typed_control = (kui_scrollable_control*)base;

	if (typed_control->is_dirty) {
		// TODO: reupload vertex geometry for clipping mask?
		/* renderer_geometry_vertex_update(&typed_control->g, 0, typed_control->g.vertex_count, typed_control->g.vertices, true); */
		typed_control->is_dirty = false;
	}

	base->clip_mask.render_data.model = ktransform_world_get(base->clip_mask.clip_ktransform);

	return true;
}
vec2 kui_scrollable_size(kui_state* state, kui_control self) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);

	return (vec2){base->bounds.width, base->bounds.height};
}

void kui_scrollable_set_height(kui_state* state, kui_control self, f32 height) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control_resize(state, self, (vec2){base->bounds.width, height});
}
void kui_scrollable_set_width(kui_state* state, kui_control self, f32 width) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control_resize(state, self, (vec2){width, base->bounds.height});
}

b8 kui_scrollable_control_resize(kui_state* state, kui_control self, vec2 new_size) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);

	/* kui_scrollable_control* typed_control = (kui_scrollable_control*)base; */

	base->bounds.width = new_size.x;
	base->bounds.height = new_size.y;
	vertex_2d* vertices = base->clip_mask.clip_geometry.vertices;
	vertices[1].position.y = new_size.y;
	vertices[1].position.x = new_size.x;
	vertices[2].position.y = new_size.y;
	vertices[3].position.x = new_size.x;
	renderer_geometry_vertex_update(&base->clip_mask.clip_geometry, 0, 4, vertices, false);

	return true;
}

kui_control kui_scrollable_control_get_content_container(kui_state* state, kui_control self) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control* typed_control = (kui_scrollable_control*)base;
	return typed_control->content_wrapper;
}

void kui_scrollable_control_scroll_y(kui_state* state, kui_control self, f32 amount) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control* typed_control = (kui_scrollable_control*)base;
	kui_base_control* container_base = kui_system_get_base(state, typed_control->content_wrapper);
	ktransform_translate(container_base->ktransform, (vec3){0, amount, 0});
}
void kui_scrollable_control_scroll_x(kui_state* state, kui_control self, f32 amount) {
	kui_base_control* base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control* typed_control = (kui_scrollable_control*)base;
	kui_base_control* container_base = kui_system_get_base(state, typed_control->content_wrapper);
	ktransform_translate(container_base->ktransform, (vec3){amount, 0, 0});
}

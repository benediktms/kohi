#include "sui_panel.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/kshader_system.h>

#include "renderer/standard_ui_renderer.h"
#include "standard_ui_defines.h"
#include "standard_ui_system.h"

b8 sui_panel_control_create(standard_ui_state* state, const char* name, vec2 size, vec4 colour, struct sui_control* out_control) {
	if (!sui_base_control_create(state, name, out_control)) {
		return false;
	}

	out_control->internal_data_size = sizeof(sui_panel_internal_data);
	out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
	sui_panel_internal_data* typed_data = out_control->internal_data;

	out_control->bounds = vec4_create(0, 0, size.x, size.y);

	// Reasonable defaults.
	typed_data->colour = colour;
	typed_data->is_dirty = true;

	// Assign function pointers.
	out_control->destroy = sui_panel_control_destroy;
	out_control->update = sui_panel_control_update;
	out_control->render = sui_panel_control_render;

	out_control->name = string_duplicate(name);

	// load

	// Generate UVs.
	f32 xmin, ymin, xmax, ymax;
	generate_uvs_from_image_coords(512, 512, 44, 7, &xmin, &ymin);
	generate_uvs_from_image_coords(512, 512, 73, 36, &xmax, &ymax);

	// Create a simple plane.
	typed_data->g = geometry_generate_quad(out_control->bounds.width, out_control->bounds.height, xmin, xmax, ymin, ymax, kname_create(out_control->name));
	if (!renderer_geometry_upload(&typed_data->g)) {
		KERROR("sui_panel_control_load - Failed to upload geometry quad");
		return false;
	}

	kshader sui_shader = kshader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));
	// Acquire binding set resources for this control.
	typed_data->binding_instance_id = INVALID_ID;
	typed_data->binding_instance_id = kshader_acquire_binding_set_instance(sui_shader, 1);
	if (typed_data->binding_instance_id == INVALID_ID) {
		KFATAL("Unable to acquire shader binding set resources for label.");
		return false;
	}
	return true;
}

void sui_panel_control_destroy(standard_ui_state* state, struct sui_control* self) {
	sui_base_control_destroy(state, self);
}

b8 sui_panel_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
	if (!sui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 sui_panel_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
	if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	sui_panel_internal_data* typed_data = self->internal_data;
	if (typed_data->is_dirty) {
		renderer_geometry_vertex_update(&typed_data->g, 0, typed_data->g.vertex_count, typed_data->g.vertices, true);
		typed_data->is_dirty = false;
	}

	if (typed_data->g.vertices) {
		standard_ui_renderable renderable = {0};
		renderable.render_data.unique_id = self->id.uniqueid;
		renderable.render_data.vertex_count = typed_data->g.vertex_count;
		renderable.render_data.vertex_element_size = typed_data->g.vertex_element_size;
		renderable.render_data.vertex_buffer_offset = typed_data->g.vertex_buffer_offset;
		renderable.render_data.index_count = typed_data->g.index_count;
		renderable.render_data.index_element_size = typed_data->g.index_element_size;
		renderable.render_data.index_buffer_offset = typed_data->g.index_buffer_offset;
		renderable.render_data.model = ktransform_world_get(self->ktransform);
		renderable.render_data.diffuse_colour = typed_data->colour;

		renderable.binding_instance_id = typed_data->binding_instance_id;
		renderable.atlas_override = INVALID_KTEXTURE;

		darray_push(render_data->renderables, renderable);
	}

	return true;
}
vec2 sui_panel_size(standard_ui_state* state, struct sui_control* self) {
	if (!self) {
		return vec2_zero();
	}

	return (vec2){self->bounds.width, self->bounds.height};
}

void sui_panel_set_height(standard_ui_state* state, struct sui_control* self, f32 height) {
	sui_panel_control_resize(state, self, (vec2){self->bounds.width, height});
}
void sui_panel_set_width(standard_ui_state* state, struct sui_control* self, f32 width) {
	sui_panel_control_resize(state, self, (vec2){width, self->bounds.height});
}

b8 sui_panel_control_resize(standard_ui_state* state, struct sui_control* self, vec2 new_size) {
	if (!self) {
		return false;
	}

	sui_panel_internal_data* typed_data = self->internal_data;

	self->bounds.width = new_size.x;
	self->bounds.height = new_size.y;
	vertex_2d* vertices = typed_data->g.vertices;
	vertices[1].position.y = new_size.y;
	vertices[1].position.x = new_size.x;
	vertices[2].position.y = new_size.y;
	vertices[3].position.x = new_size.x;
	typed_data->is_dirty = true;

	return true;
}

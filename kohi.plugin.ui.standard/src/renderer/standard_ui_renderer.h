#pragma once

#include <core_render_types.h>
#include <renderer/renderer_types.h>

struct frame_data;

typedef struct sui_pass_data {
	kshader sui_shader;
} sui_pass_data;

typedef struct standard_ui_render_data {
	ktexture colour_buffer;
	ktexture depth_stencil_buffer;
	mat4 view;
	mat4 projection;

	ktexture ui_atlas;
	u32 shader_set0_binding_instance_id;

	u32 renderable_count;
	struct standard_ui_renderable* renderables;
} standard_ui_render_data;

/**
 * @brief Represents the state of the Standard UI renderer.
 */
typedef struct sui_renderer {

	struct renderer_system_state* renderer_state;
	struct texture_system_state* texture_system;

	krenderbuffer standard_vertex_buffer;
	krenderbuffer extended_vertex_buffer;
	krenderbuffer index_buffer;

	sui_pass_data sui_pass;

} sui_renderer;

KAPI b8 sui_renderer_create(sui_renderer* out_renderer);

KAPI void sui_renderer_destroy(sui_renderer* renderer);

KAPI b8 sui_renderer_render_frame(sui_renderer* renderer, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

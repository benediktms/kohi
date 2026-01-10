#pragma once

#include <core/frame_data.h>
#include <core/keymap.h>
#include <platform/platform.h>
#include <renderer/kforward_renderer.h>
#include <standard_ui_system.h>
#include <world/world_types.h>

#include "editor/editor_gizmo.h"

typedef struct keditor_gizmo_pass_render_data {
	mat4 projection;
	mat4 view;

	b8 visible;

	kdebug_geometry_render_data geometry;

	mat4 gizmo_transform;

	b8 do_pass;
} keditor_gizmo_pass_render_data;

typedef enum editor_mode {
	EDITOR_MODE_SCENE,
	EDITOR_MODE_ENTITY,
	EDITOR_MODE_TREE,
	EDITOR_MODE_ASSETS
} editor_mode;

typedef struct keditor_gizmo_pass_data {
	kshader gizmo_shader;
	u32 set0_instance_id;
} keditor_gizmo_pass_data;

typedef struct editor_state {
	// Editor camera
	kcamera editor_camera;
	f32 editor_camera_forward_move_speed;
	f32 editor_camera_backward_move_speed;
	editor_gizmo gizmo;
	b8 using_gizmo;
	// Editor state
	// Darray of selected entities.
	kentity* selection_list;
	keymap editor_keymap;

	b8 is_running;

	// Pointer to the scene currently owned by the editor (NOT necessarily the scene owned by the game code currently!)
	struct kscene* edit_scene;
	kname scene_asset_name;
	kname scene_package_name;

	keditor_gizmo_pass_data editor_gizmo_pass;
	struct renderer_system_state* renderer;
	krenderbuffer standard_vertex_buffer;
	krenderbuffer index_buffer;

	keditor_gizmo_pass_render_data* editor_gizmo_render_data;

	editor_mode mode;

	u16 font_size;
	kname font_name;
	u16 textbox_font_size;
	kname textbox_font_name;

	// UI elements
	standard_ui_state* sui_state;
	sui_control editor_root;

	// Main window
	sui_control main_bg_panel;
	sui_control save_button;
	sui_control save_button_label;
	sui_control mode_entity_button;
	sui_control mode_entity_label;
	sui_control mode_scene_button;
	sui_control mode_scene_label;
	sui_control mode_tree_button;
	sui_control mode_tree_label;

	// Scene Inspector window
	f32 scene_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 scene_inspector_right_col_x;
	sui_control scene_inspector_bg_panel;
	sui_control scene_inspector_title;
	sui_control scene_name_label;
	sui_control scene_name_textbox;

	// Entity Inspector window
	f32 entity_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 entity_inspector_right_col_x;
	sui_control entity_inspector_bg_panel;
	sui_control entity_inspector_title;
	sui_control entity_name_label;
	sui_control entity_name_textbox;
	sui_control entity_position_label;
	sui_control entity_position_x_textbox;
	sui_control entity_position_y_textbox;
	sui_control entity_position_z_textbox;

	sui_control entity_orientation_label;
	sui_control entity_orientation_x_textbox;
	sui_control entity_orientation_y_textbox;
	sui_control entity_orientation_z_textbox;
	sui_control entity_orientation_w_textbox;

	sui_control entity_scale_label;
	sui_control entity_scale_x_textbox;
	sui_control entity_scale_y_textbox;
	sui_control entity_scale_z_textbox;

	// Tree window
	f32 tree_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 tree_inspector_right_col_x;
	sui_control tree_inspector_bg_panel;
	sui_control tree_inspector_title;

	u32 tree_element_count;
	sui_control* tree_elements;

} editor_state;

KAPI b8 editor_initialize(u64* memory_requirement, struct editor_state* state);
KAPI void editor_shutdown(struct editor_state* state);

KAPI b8 editor_open(struct editor_state* state, kname scene_name, kname scene_package_name);
KAPI b8 editor_close(struct editor_state* state);
KAPI void editor_set_mode(struct editor_state* state, editor_mode mode);

KAPI void editor_clear_selected_entities(struct editor_state* state);
KAPI void editor_select_entities(struct editor_state* state, u32 count, kentity* entities);
KAPI void editor_add_to_selected_entities(struct editor_state* state, u32 count, kentity* entities);
KAPI void editor_select_parent(struct editor_state* state);
KAPI b8 editor_selection_contains(struct editor_state* state, kentity entity);

KAPI void editor_update(struct editor_state* state, frame_data* p_frame_data);
KAPI void editor_frame_prepare(struct editor_state* state, frame_data* p_frame_data, b8 draw_gizmo, keditor_gizmo_pass_render_data* gizmo_pass_render_data);
KAPI b8 editor_render(struct editor_state* state, frame_data* p_frame_data, ktexture colour_buffer_target, b8 draw_gizmo, keditor_gizmo_pass_render_data* gizmo_pass_render_data);

KAPI void editor_on_window_resize(struct editor_state* state, const struct kwindow* window);

KAPI void editor_setup_keymaps(struct editor_state* state);

KAPI void editor_on_lib_load(struct editor_state* state);
KAPI void editor_on_lib_unload(struct editor_state* state);

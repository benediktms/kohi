#include "editor.h"

#include "assets/kasset_types.h"
#include "audio/audio_frontend.h"
#include "core/event.h"
#include "core_resource_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "editor/editor_gizmo.h"
#include "input_types.h"
#include "math/geometry_2d.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "plugins/plugin_types.h"
#include "renderer/renderer_frontend.h"
#include "standard_ui_system.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kcamera_system.h"
#include "systems/kshader_system.h"
#include "systems/ktransform_system.h"
#include "systems/plugin_system.h"
#include "systems/texture_system.h"
#include "utils_plugin_defines.h"
#include "world/kscene.h"
#include "world/world_types.h"
#include "world/world_utils.h"

#include <containers/darray.h>
#include <controls/sui_button.h>
#include <controls/sui_label.h>
#include <controls/sui_panel.h>
#include <controls/sui_textbox.h>
#include <core/console.h>
#include <core/engine.h>
#include <math/kmath.h>
#include <platform/platform.h>
#include <standard_ui_plugin_main.h>
#include <systems/ktimeline_system.h>
#include <utils/ksort.h>

#include <logger.h>

typedef struct editor_gizmo_global_ubo {
	mat4 projection;
	mat4 view;
} editor_gizmo_global_ubo;

typedef struct editor_gizmo_immediate_data {
	mat4 model;
} editor_gizmo_immediate_data;

static f32 get_engine_delta_time(void);
static f32 get_engine_total_time(void);

static void editor_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_cascades(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_wireframe(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_gizmo_mode(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_gizmo_orientation_set(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_sprint_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_save_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_zoom_extents(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static b8 editor_on_mouse_move(u16 code, void* sender, void* listener_inst, event_context context);
static b8 editor_on_drag(u16 code, void* sender, void* listener_inst, event_context context);
static b8 editor_on_button(u16 code, void* sender, void* listener_inst, event_context context);

static void editor_command_execute(console_command_context context);

static void editor_register_events(struct editor_state* state);
static void editor_unregister_events(struct editor_state* state);
static void editor_register_commands(struct editor_state* state);
static void editor_unregister_commands(struct editor_state* state);

static b8 save_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 mode_scene_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 mode_entity_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
static b8 mode_tree_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

static void scene_name_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);

static void entity_name_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_position_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_position_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_position_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_orientation_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_orientation_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_orientation_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_orientation_w_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_scale_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_scale_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);
static void entity_scale_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);

static void tree_refresh(editor_state* state);
static b8 tree_label_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

b8 editor_initialize(u64* memory_requirement, struct editor_state* state) {
	*memory_requirement = sizeof(editor_state);
	if (!state) {
		return true;
	}

	// Setup gizmo.
	if (!editor_gizmo_create(&state->gizmo)) {
		KERROR("Failed to create editor gizmo!");
		return false;
	}
	if (!editor_gizmo_initialize(&state->gizmo)) {
		KERROR("Failed to initialize editor gizmo!");
		return false;
	}
	if (!editor_gizmo_load(&state->gizmo)) {
		KERROR("Failed to load editor gizmo!");
		return false;
	}

	state->renderer = engine_systems_get()->renderer_system;

	state->standard_vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	state->index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// Editor gizmo pass state
	{
		state->editor_gizmo_pass.gizmo_shader = kshader_system_get(kname_create(SHADER_NAME_PLUGIN_UTILS_EDITOR_GIZMO), kname_create(PACKAGE_NAME_PLUGIN_UTILS));
		KASSERT_DEBUG(state->editor_gizmo_pass.gizmo_shader != KSHADER_INVALID);

		state->editor_gizmo_pass.set0_instance_id = kshader_acquire_binding_set_instance(state->editor_gizmo_pass.gizmo_shader, 0);
	}

	// Editor camera. Use the same view properties of the world camera, but different starting position/rotation.
	vec3 editor_cam_pos = (vec3){-10.0f, 10.0f, -10.0f};
	vec3 editor_cam_rot_euler_radians = (vec3){deg_to_rad(-35.0f), deg_to_rad(225.0f), deg_to_rad(0.0f)};
	rect_2di world_vp_rect = {0, 0, 1280 - 40, 720 - 40};
	state->editor_camera = kcamera_create(KCAMERA_TYPE_3D, world_vp_rect, editor_cam_pos, editor_cam_rot_euler_radians, deg_to_rad(45.0f), 0.1f, 1000.0f);

	state->editor_camera_forward_move_speed = 5.0f * 5.0f;
	state->editor_camera_backward_move_speed = 2.5f * 5.0f;

	state->selection_list = darray_create(kentity);

	kruntime_plugin* sui_plugin = plugin_system_get(engine_systems_get()->plugin_system, "kohi.plugin.ui.standard");
	standard_ui_state* sui_state = ((standard_ui_plugin_state*)sui_plugin->plugin_state)->state;
	state->sui_state = sui_state;

	// UI elements. Create/load them all up here.
	state->font_name = kname_create("Noto Sans CJK JP");
	state->font_size = 32;
	state->textbox_font_name = kname_create("Noto Sans Mono CJK JP");
	state->textbox_font_size = 30;

	// Main root control for everything else to belong to.
	{
		KASSERT(sui_base_control_create(sui_state, "editor_root", &state->editor_root));
		KASSERT(standard_ui_system_register_control(sui_state, &state->editor_root));
		KASSERT(standard_ui_system_control_add_child(sui_state, KNULL, &state->editor_root));
		state->editor_root.is_active = true;
		standard_ui_system_update_active(sui_state, &state->editor_root);
		state->editor_root.is_visible = false;
	}

	// Main window
	{
		// Main background panel.
		KASSERT(sui_panel_control_create(sui_state, "main_bg_panel", (vec2){200.0f, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f}, &state->main_bg_panel));
		KASSERT(standard_ui_system_register_control(sui_state, &state->main_bg_panel));
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->editor_root, &state->main_bg_panel));
		ktransform_position_set(state->main_bg_panel.ktransform, (vec3){10, 10, 0});
		state->main_bg_panel.is_active = true;
		standard_ui_system_update_active(sui_state, &state->main_bg_panel);
		state->main_bg_panel.is_visible = true;

		// Save button.
		{
			KASSERT(sui_button_control_create(sui_state, "save_button", &state->save_button));
			KASSERT(standard_ui_system_register_control(sui_state, &state->save_button));

			KASSERT(standard_ui_system_control_add_child(sui_state, &state->main_bg_panel, &state->save_button));
			sui_button_control_width_set(sui_state, &state->save_button, 200);
			ktransform_position_set(state->save_button.ktransform, (vec3){0, 50, 0});
			state->save_button.is_active = true;
			standard_ui_system_update_active(sui_state, &state->save_button);
			state->save_button.is_visible = true;

			// Label
			KASSERT(sui_label_control_create(sui_state, "save_button_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Save", &state->save_button_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->save_button_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->save_button, &state->save_button_label));
			ktransform_position_set(state->save_button_label.ktransform, (vec3){10, -5.0f, 0});
			state->save_button_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->save_button_label);
			state->save_button_label.is_visible = true;

			state->save_button.on_click = save_button_clicked;
		}

		// Scene mode button.
		{
			KASSERT(sui_button_control_create(sui_state, "mode_scene_button", &state->mode_scene_button));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_scene_button));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->main_bg_panel, &state->mode_scene_button));
			sui_button_control_width_set(sui_state, &state->mode_scene_button, 100);
			ktransform_position_set(state->mode_scene_button.ktransform, (vec3){0, 100, 0});
			state->mode_scene_button.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_scene_button);
			state->mode_scene_button.is_visible = true;

			// Label
			KASSERT(sui_label_control_create(sui_state, "mode_scene_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene", &state->mode_scene_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_scene_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->mode_scene_button, &state->mode_scene_label));
			ktransform_position_set(state->mode_scene_label.ktransform, (vec3){10, -5.0f, 0});
			state->mode_scene_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_scene_label);
			state->mode_scene_label.is_visible = true;

			state->mode_scene_button.user_data = state;
			state->mode_scene_button.user_data_size = sizeof(*state);
			state->mode_scene_button.on_click = mode_scene_button_clicked;
		}

		// Entity mode button.
		{
			KASSERT(sui_button_control_create(sui_state, "mode_entity_button", &state->mode_entity_button));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_entity_button));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->main_bg_panel, &state->mode_entity_button));
			sui_button_control_width_set(sui_state, &state->mode_entity_button, 100);
			ktransform_position_set(state->mode_entity_button.ktransform, (vec3){100, 100, 0});
			state->mode_entity_button.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_entity_button);
			state->mode_entity_button.is_visible = true;

			// Label
			KASSERT(sui_label_control_create(sui_state, "mode_entity_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity", &state->mode_entity_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_entity_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->mode_entity_button, &state->mode_entity_label));
			ktransform_position_set(state->mode_entity_label.ktransform, (vec3){10, -5.0f, 0});
			state->mode_entity_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_entity_label);
			state->mode_entity_label.is_visible = true;

			state->mode_entity_button.user_data = state;
			state->mode_entity_button.user_data_size = sizeof(*state);
			state->mode_entity_button.on_click = mode_entity_button_clicked;
		}

		// Tree mode button.
		{
			KASSERT(sui_button_control_create(sui_state, "mode_tree_button", &state->mode_tree_button));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_tree_button));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->main_bg_panel, &state->mode_tree_button));
			sui_button_control_width_set(sui_state, &state->mode_tree_button, 100);
			ktransform_position_set(state->mode_tree_button.ktransform, (vec3){0, 150, 0});
			state->mode_tree_button.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_tree_button);
			state->mode_tree_button.is_visible = true;

			// Label
			KASSERT(sui_label_control_create(sui_state, "mode_tree_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree", &state->mode_tree_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->mode_tree_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->mode_tree_button, &state->mode_tree_label));
			ktransform_position_set(state->mode_tree_label.ktransform, (vec3){10, -5.0f, 0});
			state->mode_tree_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->mode_tree_label);
			state->mode_tree_label.is_visible = true;

			state->mode_tree_button.user_data = state;
			state->mode_tree_button.user_data_size = sizeof(*state);
			state->mode_tree_button.on_click = mode_tree_button_clicked;
		}
	}

	// Scene inspector window panel.
	{
		state->scene_inspector_width = 500.0f;
		state->scene_inspector_right_col_x = 150.0f;
		KASSERT(sui_panel_control_create(sui_state, "scene_inspector_bg_panel", (vec2){state->scene_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f}, &state->scene_inspector_bg_panel));
		KASSERT(standard_ui_system_register_control(sui_state, &state->scene_inspector_bg_panel));
		ktransform_translate(state->scene_inspector_bg_panel.ktransform, (vec3){1280 - (state->scene_inspector_width + 10)});
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->editor_root, &state->scene_inspector_bg_panel));
		state->scene_inspector_bg_panel.is_active = false;
		standard_ui_system_update_active(sui_state, &state->scene_inspector_bg_panel);
		state->scene_inspector_bg_panel.is_visible = false;

		// Window Label
		KASSERT(sui_label_control_create(sui_state, "scene_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene", &state->scene_inspector_title));
		KASSERT(standard_ui_system_register_control(sui_state, &state->scene_inspector_title));
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->scene_inspector_bg_panel, &state->scene_inspector_title));
		ktransform_position_set(state->scene_inspector_title.ktransform, (vec3){10, -5.0f, 0});
		state->scene_inspector_title.is_active = true;
		standard_ui_system_update_active(sui_state, &state->scene_inspector_title);
		state->scene_inspector_title.is_visible = true;

		// scene name
		{
			// Name label.
			KASSERT(sui_label_control_create(sui_state, "scene_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name:", &state->scene_name_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->scene_name_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->scene_inspector_bg_panel, &state->scene_name_label));
			ktransform_position_set(state->scene_name_label.ktransform, (vec3){10, 50 + -5.0f, 0});
			state->scene_name_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->scene_name_label);
			state->scene_name_label.is_visible = true;

			// Name textbox.
			KASSERT(sui_textbox_control_create(sui_state, "scene_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_STRING, &state->scene_name_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->scene_name_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->scene_inspector_bg_panel, &state->scene_name_textbox));
			KASSERT(sui_textbox_control_width_set(sui_state, &state->scene_name_textbox, 380));
			ktransform_position_set(state->scene_name_textbox.ktransform, (vec3){state->scene_inspector_right_col_x, 50, 0});
			state->scene_name_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->scene_name_textbox);
			state->scene_name_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->scene_name_textbox.user_data = state;
			state->scene_name_textbox.user_data_size = sizeof(*state);
			state->scene_name_textbox.on_key = scene_name_textbox_on_key;
		}

		// TODO: more controls
	}

	// Entity inspector window panel.
	{
		state->entity_inspector_width = 650.0f;
		state->entity_inspector_right_col_x = 130.0f;
		KASSERT(sui_panel_control_create(sui_state, "entity_inspector_bg_panel", (vec2){state->entity_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f}, &state->entity_inspector_bg_panel));
		KASSERT(standard_ui_system_register_control(sui_state, &state->entity_inspector_bg_panel));
		ktransform_translate(state->entity_inspector_bg_panel.ktransform, (vec3){1280 - (state->entity_inspector_width + 10)});
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->editor_root, &state->entity_inspector_bg_panel));
		state->entity_inspector_bg_panel.is_active = false;
		standard_ui_system_update_active(sui_state, &state->entity_inspector_bg_panel);
		state->entity_inspector_bg_panel.is_visible = false;

		// Window Label
		KASSERT(sui_label_control_create(sui_state, "entity_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity (no selection)", &state->entity_inspector_title));
		KASSERT(standard_ui_system_register_control(sui_state, &state->entity_inspector_title));
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_inspector_title));
		ktransform_position_set(state->entity_inspector_title.ktransform, (vec3){10, -5.0f, 0});
		state->entity_inspector_title.is_active = true;
		standard_ui_system_update_active(sui_state, &state->entity_inspector_title);
		state->entity_inspector_title.is_visible = true;

		// Entity name
		{
			// Name label.
			KASSERT(sui_label_control_create(sui_state, "entity_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name:", &state->entity_name_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_name_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_name_label));
			ktransform_position_set(state->entity_name_label.ktransform, (vec3){10, 50 + -5.0f, 0});
			state->entity_name_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_name_label);
			state->entity_name_label.is_visible = true;

			// Name textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_STRING, &state->entity_name_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_name_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_name_textbox));
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_name_textbox, 380));
			ktransform_position_set(state->entity_name_textbox.ktransform, (vec3){state->entity_inspector_right_col_x, 50, 0});
			state->entity_name_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_name_textbox);
			state->entity_name_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_name_textbox.user_data = state;
			state->entity_name_textbox.user_data_size = sizeof(*state);
			state->entity_name_textbox.on_key = entity_name_textbox_on_key;
		}

		// Entity position
		{
			// Position label
			KASSERT(sui_label_control_create(sui_state, "entity_position_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Position", &state->entity_position_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_position_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_position_label));
			ktransform_position_set(state->entity_position_label.ktransform, (vec3){10, 100 + -5.0f, 0});
			state->entity_position_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_position_label);
			state->entity_position_label.is_visible = true;

			// Position x textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_position_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_position_x_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_position_x_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_position_x_textbox));
			ktransform_position_set(state->entity_position_x_textbox.ktransform, (vec3){state->entity_inspector_right_col_x, 100, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_position_x_textbox, 120));
			((sui_textbox_internal_data*)state->entity_position_x_textbox.internal_data)->colour = (colour4){1.0f, 0.5f, 0.5f, 1.0f};
			state->entity_position_x_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_position_x_textbox);
			state->entity_position_x_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_position_x_textbox.user_data = state;
			state->entity_position_x_textbox.user_data_size = sizeof(*state);
			state->entity_position_x_textbox.on_key = entity_position_x_textbox_on_key;

			// Position y textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_position_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_position_y_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_position_y_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_position_y_textbox));
			ktransform_position_set(state->entity_position_y_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 130, 100, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_position_y_textbox, 120));
			((sui_textbox_internal_data*)state->entity_position_y_textbox.internal_data)->colour = (colour4){0.5f, 1.0f, 0.5f, 1.0f};
			state->entity_position_y_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_position_y_textbox);
			state->entity_position_y_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_position_y_textbox.user_data = state;
			state->entity_position_y_textbox.user_data_size = sizeof(*state);
			state->entity_position_y_textbox.on_key = entity_position_y_textbox_on_key;

			// Position z textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_position_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_position_z_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_position_z_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_position_z_textbox));
			ktransform_position_set(state->entity_position_z_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 260, 100, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_position_z_textbox, 120));
			((sui_textbox_internal_data*)state->entity_position_z_textbox.internal_data)->colour = (colour4){0.5f, 0.5f, 1.0f, 1.0f};
			state->entity_position_z_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_position_z_textbox);
			state->entity_position_z_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_position_z_textbox.user_data = state;
			state->entity_position_z_textbox.user_data_size = sizeof(*state);
			state->entity_position_z_textbox.on_key = entity_position_z_textbox_on_key;
		}

		// Entity rotation
		{
			// Position label
			KASSERT(sui_label_control_create(sui_state, "entity_orientation_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Orientation", &state->entity_orientation_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_orientation_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_orientation_label));
			ktransform_position_set(state->entity_orientation_label.ktransform, (vec3){10, 150 + -5.0f, 0});
			state->entity_orientation_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_orientation_label);
			state->entity_orientation_label.is_visible = true;

			// Orientation x textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_orientation_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_orientation_x_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_orientation_x_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_orientation_x_textbox));
			ktransform_position_set(state->entity_orientation_x_textbox.ktransform, (vec3){state->entity_inspector_right_col_x, 150, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_orientation_x_textbox, 120));
			state->entity_orientation_x_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_orientation_x_textbox);
			state->entity_orientation_x_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_orientation_x_textbox.user_data = state;
			state->entity_orientation_x_textbox.user_data_size = sizeof(*state);
			state->entity_orientation_x_textbox.on_key = entity_orientation_x_textbox_on_key;

			// Orientation y textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_orientation_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_orientation_y_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_orientation_y_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_orientation_y_textbox));
			ktransform_position_set(state->entity_orientation_y_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 130, 150, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_orientation_y_textbox, 120));
			state->entity_orientation_y_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_orientation_y_textbox);
			state->entity_orientation_y_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_orientation_y_textbox.user_data = state;
			state->entity_orientation_y_textbox.user_data_size = sizeof(*state);
			state->entity_orientation_y_textbox.on_key = entity_orientation_y_textbox_on_key;

			// Orientation z textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_orientation_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_orientation_z_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_orientation_z_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_orientation_z_textbox));
			ktransform_position_set(state->entity_orientation_z_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 260, 150, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_orientation_z_textbox, 120));
			state->entity_orientation_z_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_orientation_z_textbox);
			state->entity_orientation_z_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_orientation_z_textbox.user_data = state;
			state->entity_orientation_z_textbox.user_data_size = sizeof(*state);
			state->entity_orientation_z_textbox.on_key = entity_orientation_z_textbox_on_key;

			// Orientation z textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_orientation_w_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_orientation_w_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_orientation_w_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_orientation_w_textbox));
			ktransform_position_set(state->entity_orientation_w_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 390, 150, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_orientation_w_textbox, 120));
			state->entity_orientation_w_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_orientation_w_textbox);
			state->entity_orientation_w_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_orientation_w_textbox.user_data = state;
			state->entity_orientation_w_textbox.user_data_size = sizeof(*state);
			state->entity_orientation_w_textbox.on_key = entity_orientation_w_textbox_on_key;
		}

		// Entity scale
		{
			// Scale label
			KASSERT(sui_label_control_create(sui_state, "entity_scale_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scale", &state->entity_scale_label));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_scale_label));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_scale_label));
			ktransform_position_set(state->entity_scale_label.ktransform, (vec3){10, 200 + -5.0f, 0});
			state->entity_scale_label.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_scale_label);
			state->entity_scale_label.is_visible = true;

			// Scale x textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_scale_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_scale_x_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_scale_x_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_scale_x_textbox));
			ktransform_position_set(state->entity_scale_x_textbox.ktransform, (vec3){state->entity_inspector_right_col_x, 200, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_scale_x_textbox, 120));
			((sui_textbox_internal_data*)state->entity_scale_x_textbox.internal_data)->colour = (colour4){1.0f, 0.5f, 0.5f, 1.0f};
			state->entity_scale_x_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_scale_x_textbox);
			state->entity_scale_x_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_scale_x_textbox.user_data = state;
			state->entity_scale_x_textbox.user_data_size = sizeof(*state);
			state->entity_scale_x_textbox.on_key = entity_scale_x_textbox_on_key;

			// Scale y textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_scale_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_scale_y_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_scale_y_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_scale_y_textbox));
			ktransform_position_set(state->entity_scale_y_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 130, 200, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_scale_y_textbox, 120));
			((sui_textbox_internal_data*)state->entity_scale_y_textbox.internal_data)->colour = (colour4){0.5f, 1.0f, 0.5f, 1.0f};
			state->entity_scale_y_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_scale_y_textbox);
			state->entity_scale_y_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_scale_y_textbox.user_data = state;
			state->entity_scale_y_textbox.user_data_size = sizeof(*state);
			state->entity_scale_y_textbox.on_key = entity_scale_y_textbox_on_key;

			// Scale z textbox.
			KASSERT(sui_textbox_control_create(sui_state, "entity_scale_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", SUI_TEXTBOX_TYPE_FLOAT, &state->entity_scale_z_textbox));
			KASSERT(standard_ui_system_register_control(sui_state, &state->entity_scale_z_textbox));
			KASSERT(standard_ui_system_control_add_child(sui_state, &state->entity_inspector_bg_panel, &state->entity_scale_z_textbox));
			ktransform_position_set(state->entity_scale_z_textbox.ktransform, (vec3){state->entity_inspector_right_col_x + 260, 200, 0});
			KASSERT(sui_textbox_control_width_set(sui_state, &state->entity_scale_z_textbox, 120));
			((sui_textbox_internal_data*)state->entity_scale_z_textbox.internal_data)->colour = (colour4){0.5f, 0.5f, 1.0f, 1.0f};
			state->entity_scale_z_textbox.is_active = true;
			standard_ui_system_update_active(sui_state, &state->entity_scale_z_textbox);
			state->entity_scale_z_textbox.is_visible = true;
			// Store a pointer to the editor state for use in the handler.
			state->entity_scale_z_textbox.user_data = state;
			state->entity_scale_z_textbox.user_data_size = sizeof(*state);
			state->entity_scale_z_textbox.on_key = entity_scale_z_textbox_on_key;
		}
	}

	// Tree window panel.
	{
		state->tree_inspector_width = 500.0f;
		state->tree_inspector_right_col_x = 150.0f;
		KASSERT(sui_panel_control_create(sui_state, "tree_inspector_bg_panel", (vec2){state->tree_inspector_width, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f}, &state->tree_inspector_bg_panel));
		KASSERT(standard_ui_system_register_control(sui_state, &state->tree_inspector_bg_panel));
		ktransform_translate(state->tree_inspector_bg_panel.ktransform, (vec3){1280 - (state->tree_inspector_width + 10)});
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->editor_root, &state->tree_inspector_bg_panel));
		state->tree_inspector_bg_panel.is_active = false;
		standard_ui_system_update_active(sui_state, &state->tree_inspector_bg_panel);
		state->tree_inspector_bg_panel.is_visible = false;

		// Window Label
		KASSERT(sui_label_control_create(sui_state, "tree_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree", &state->tree_inspector_title));
		KASSERT(standard_ui_system_register_control(sui_state, &state->tree_inspector_title));
		KASSERT(standard_ui_system_control_add_child(sui_state, &state->tree_inspector_bg_panel, &state->tree_inspector_title));
		ktransform_position_set(state->tree_inspector_title.ktransform, (vec3){10, -5.0f, 0});
		state->tree_inspector_title.is_active = true;
		standard_ui_system_update_active(sui_state, &state->tree_inspector_title);
		state->tree_inspector_title.is_visible = true;

		// TODO: more controls
	}

	state->is_running = true;

	return true;
}
void editor_shutdown(struct editor_state* state) {
}

b8 editor_open(struct editor_state* state, kname scene_name, kname scene_package_name) {
	kasset_text* scene_asset = asset_system_request_text_from_package_sync(
		engine_systems_get()->asset_state,
		kname_string_get(scene_package_name),
		kname_string_get(scene_name));
	if (!scene_asset) {
		KERROR("%s - Failed to request scene asset. See logs for details.", __FUNCTION__);
		return false;
	}

	// TODO: callback for zone load?
	/* zone_loaded_context* c = KALLOC_TYPE(zone_loaded_context, MEMORY_TAG_GAME);
	c->z = z;
	c->state = state;
	c->zone_index = index;
	c->spawn_point_id = spawn_point_id; */

	KINFO("Opening editor scene...");

	// Creates scene and triggers load.
	state->edit_scene = kscene_create(scene_asset->content, 0, 0);
	state->scene_asset_name = scene_name;
	state->scene_package_name = scene_package_name;

	asset_system_release_text(engine_systems_get()->asset_state, scene_asset);
	if (!state->edit_scene) {
		KERROR("%s - Failed to create and load scene. See logs for details.", __FUNCTION__);
		return false;
	}

	const char* scene_name_str = kscene_get_name(state->edit_scene);
	sui_textbox_text_set(state->sui_state, &state->scene_name_textbox, scene_name_str ? scene_name_str : "");

	// If opened successfully, change keymaps.
	if (!input_keymap_pop()) {
		KERROR("No keymap was popped during world->editor");
	}
	input_keymap_push(&state->editor_keymap);

	state->is_running = true;

	// Events and console commands for the editor should only be available when it is running.
	editor_register_events(state);
	editor_register_commands(state);

	// Enable UI elements.
	state->editor_root.is_visible = true;

	// Set the default mode.
	editor_set_mode(state, EDITOR_MODE_SCENE);

	return true;
}

b8 editor_close(struct editor_state* state) {
	// TODO: dirty check. If dirty, return false here. May need some sort of callback to
	// allow a "this is saved, now we can close" function.

	KTRACE("Destroying editor scene...");
	// Unload the current zone's scene from the world.
	kscene_destroy(state->edit_scene);
	state->edit_scene = KNULL;

	state->scene_asset_name = INVALID_KNAME;
	state->scene_package_name = INVALID_KNAME;

	KTRACE("Editor scene destroyed.");

	// Events and console commands for the editor should only be available when it is running.
	editor_unregister_events(state);
	editor_unregister_commands(state);

	state->is_running = false;

	// Disable UI elements.
	state->editor_root.is_visible = false;

	return true;
}

sui_control* get_inspector_base_for_mode(struct editor_state* state, editor_mode mode) {
	switch (mode) {
	case EDITOR_MODE_SCENE:
		return &state->scene_inspector_bg_panel;
	case EDITOR_MODE_ENTITY:
		return &state->entity_inspector_bg_panel;
	case EDITOR_MODE_TREE:
		return &state->tree_inspector_bg_panel;
	case EDITOR_MODE_ASSETS:
		// TODO: other types
		return KNULL;
	}
}

void editor_set_mode(struct editor_state* state, editor_mode mode) {
	// Disable current window
	sui_control* window = get_inspector_base_for_mode(state, state->mode);
	window->is_active = false;
	window->is_visible = false;
	standard_ui_system_update_active(state->sui_state, window);

	// Set mode an enable the new.
	state->mode = mode;
	window = get_inspector_base_for_mode(state, state->mode);
	window->is_active = true;
	window->is_visible = true;
	standard_ui_system_update_active(state->sui_state, window);
}

void editor_clear_selected_entities(struct editor_state* state) {
	darray_clear(state->selection_list);
	state->gizmo.selected_transform = KTRANSFORM_INVALID;
	KTRACE("Selection cleared.");

	// No selection, turn stuff off.
	sui_label_text_set(state->sui_state, &state->entity_inspector_title, "Entity (no selection)");
	sui_textbox_text_set(state->sui_state, &state->entity_name_textbox, "");

	// Update inspector position controls.
	sui_textbox_text_set(state->sui_state, &state->entity_position_x_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_position_y_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_position_z_textbox, "");

	// Update inspector orientation controls.
	sui_textbox_text_set(state->sui_state, &state->entity_orientation_x_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_orientation_y_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_orientation_z_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_orientation_w_textbox, "");

	// Update inspector scale controls.
	sui_textbox_text_set(state->sui_state, &state->entity_scale_x_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_scale_y_textbox, "");
	sui_textbox_text_set(state->sui_state, &state->entity_scale_z_textbox, "");
}

void editor_select_entities(struct editor_state* state, u32 count, kentity* entities) {
	editor_clear_selected_entities(state);

	editor_add_to_selected_entities(state, count, entities);
}

void editor_add_to_selected_entities(struct editor_state* state, u32 count, kentity* entities) {

	for (u32 s = 0; s < count; ++s) {
		kentity entity = entities[s];
		kname name = kscene_get_entity_name(state->edit_scene, entity);
		KINFO("Selection [%u]: '%s'", s, kname_string_get(name));
		if (!editor_selection_contains(state, entity)) {
			darray_push(state->selection_list, entity);
		}
	}

	// Set the gizmo to the selection.
	// HACK: force single-select for now.
	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
	// TODO: Set the gizmo to an average position of all selected entity transforms,
	// and apply the modifications to transforms individually, but together.

	// Update inspector controls.
	const char* type_str = kentity_type_to_string(kentity_unpack_type(state->selection_list[0]));
	char* title_str = string_format("Entity (%s)", type_str);
	sui_label_text_set(state->sui_state, &state->entity_inspector_title, title_str);
	string_free(title_str);

	kname name = kscene_get_entity_name(state->edit_scene, state->selection_list[0]);
	const char* name_str = kname_string_get(name);
	sui_textbox_text_set(state->sui_state, &state->entity_name_textbox, name_str ? name_str : "");

	// Update inspector position controls.
	{
		vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(position.x);
		sui_textbox_text_set(state->sui_state, &state->entity_position_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(position.y);
		sui_textbox_text_set(state->sui_state, &state->entity_position_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(position.z);
		sui_textbox_text_set(state->sui_state, &state->entity_position_z_textbox, z);
		string_free(z);
	}
	// Update inspector orientation controls.
	{
		quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(rotation.x);
		sui_textbox_text_set(state->sui_state, &state->entity_orientation_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(rotation.y);
		sui_textbox_text_set(state->sui_state, &state->entity_orientation_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(rotation.z);
		sui_textbox_text_set(state->sui_state, &state->entity_orientation_z_textbox, z);
		string_free(z);
		const char* w = f32_to_string(rotation.w);
		sui_textbox_text_set(state->sui_state, &state->entity_orientation_w_textbox, w);
		string_free(w);
	}
	// Update inspector scale controls.
	{
		vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(scale.x);
		sui_textbox_text_set(state->sui_state, &state->entity_scale_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(scale.y);
		sui_textbox_text_set(state->sui_state, &state->entity_scale_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(scale.z);
		sui_textbox_text_set(state->sui_state, &state->entity_scale_z_textbox, z);
		string_free(z);
	}
}

void editor_select_parent(struct editor_state* state) {
	u32 count = darray_length(state->selection_list);
	if (count != 1) {
		KWARN("%s - cannot select parent unless exactly one entity is selected.", __FUNCTION__);
		return;
	}

	kentity parent = kscene_get_entity_parent(state->edit_scene, state->selection_list[0]);
	if (parent == KENTITY_INVALID) {
		KINFO("Selected object has no parent.");
		return;
	}

	state->selection_list[0] = parent;

	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
}

b8 editor_selection_contains(struct editor_state* state, kentity entity) {
	u32 selection_count = darray_length(state->selection_list);
	for (u32 s = 0; s < selection_count; ++s) {
		if (state->selection_list[s] == entity) {
			return true;
		}
	}

	return false;
}

void editor_update(struct editor_state* state, frame_data* p_frame_data) {
	editor_gizmo_update(&state->gizmo, state->editor_camera);

	// Update the listener orientation. In editor mode, the sound follows the camera.
	vec3 cam_pos = kcamera_get_position(state->editor_camera);
	vec3 cam_forward = kcamera_forward(state->editor_camera);
	vec3 cam_up = kcamera_up(state->editor_camera);
	kaudio_system_listener_orientation_set(engine_systems_get()->audio_system, cam_pos, cam_forward, cam_up);

	if (!kscene_update(state->edit_scene, p_frame_data)) {
		KWARN("Failed to update editor scene.");
	}
}

void editor_frame_prepare(struct editor_state* state, frame_data* p_frame_data, b8 draw_gizmo, keditor_gizmo_pass_render_data* gizmo_pass_render_data) {
	// Setup data required for the editor gizmo pass

	editor_gizmo_render_frame_prepare(&state->gizmo, p_frame_data);
	b8 has_selection = state->selection_list && darray_length(state->selection_list);

	gizmo_pass_render_data->do_pass = has_selection && draw_gizmo;
	if (gizmo_pass_render_data->do_pass) {

		gizmo_pass_render_data->projection = state->gizmo.render_projection;
		gizmo_pass_render_data->view = kcamera_get_view(state->editor_camera);
		gizmo_pass_render_data->visible = has_selection;
		gizmo_pass_render_data->gizmo_transform = state->gizmo.render_model;

		kgeometry g = state->gizmo.mode_data[state->gizmo.mode].geo;
		kdebug_geometry_render_data* geo_rd = &gizmo_pass_render_data->geometry;
		geo_rd->geo.index_count = g.index_count;
		geo_rd->geo.index_offset = g.index_buffer_offset;
		geo_rd->geo.vertex_count = g.vertex_count;
		geo_rd->geo.vertex_offset = g.vertex_buffer_offset;
		geo_rd->geo.transform = KTRANSFORM_INVALID; // NOTE: transform isn't directly used here. app->state->gizmo.ktransform_handle;

		// Inverted winding not supported for debug geometries.
		geo_rd->geo.flags = FLAG_SET(geo_rd->geo.flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT, false);
	}
}

static void set_render_state_defaults(rect_2di vp_rect) {
	renderer_begin_debug_label("frame defaults", vec3_zero());

	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_set_stencil_test_enabled(false);
	renderer_set_stencil_compare_mask(0);

	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
	// Default winding is counter clockwise
	renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

	rect_2di viewport_rect = {vp_rect.x, vp_rect.y + vp_rect.height, vp_rect.width, -(f32)vp_rect.height};
	renderer_viewport_set(viewport_rect);

	rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
	renderer_scissor_set(scissor_rect);

	renderer_end_debug_label();
}

b8 editor_render(struct editor_state* state, frame_data* p_frame_data, ktexture colour_buffer_target, b8 draw_gizmo, keditor_gizmo_pass_render_data* render_data) {

#if KOHI_DEBUG
	// NOTE: Editor gizmo only included in non-release builds
	if (render_data->do_pass) {

		if (render_data->visible) {

			renderer_begin_debug_label("editor gizmo", (vec3){0.5f, 1.0f, 0.5});

			rect_2di vp_rect = {0};
			if (!texture_dimensions_get(colour_buffer_target, (u32*)&vp_rect.width, (u32*)&vp_rect.height)) {
				return false;
			}
			// Editor gizmo begin render
			renderer_begin_rendering(state->renderer, p_frame_data, vp_rect, 1, &colour_buffer_target, INVALID_KTEXTURE, 0);
			set_render_state_defaults(vp_rect);

			// Disable depth test/write so the gizmo is always on top.
			renderer_set_depth_test_enabled(false);
			renderer_set_depth_write_enabled(false);
			renderer_set_stencil_test_enabled(false);

			kshader_system_use_with_topology(state->editor_gizmo_pass.gizmo_shader, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT, 0);
			renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

			// Global UBO data
			editor_gizmo_global_ubo global_ubo_data = {
				.view = render_data->view,
				.projection = render_data->projection};
			kshader_set_binding_data(state->editor_gizmo_pass.gizmo_shader, 0, state->editor_gizmo_pass.set0_instance_id, 0, 0, &global_ubo_data, sizeof(editor_gizmo_global_ubo));
			kshader_apply_binding_set(state->editor_gizmo_pass.gizmo_shader, 0, state->editor_gizmo_pass.set0_instance_id);

			kdebug_geometry_render_data* g = &render_data->geometry;

			editor_gizmo_immediate_data immediate_data = {.model = render_data->gizmo_transform};
			kshader_set_immediate_data(state->editor_gizmo_pass.gizmo_shader, &immediate_data, sizeof(editor_gizmo_immediate_data));

			// Draw it.
			b8 includes_index_data = g->geo.index_count > 0;

			if (!renderer_renderbuffer_draw(state->renderer, state->standard_vertex_buffer, g->geo.vertex_offset, g->geo.vertex_count, 0, includes_index_data)) {
				KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
				return false;
			}
			if (includes_index_data) {
				if (!renderer_renderbuffer_draw(state->renderer, state->index_buffer, g->geo.index_offset, g->geo.index_count, 0, !includes_index_data)) {
					KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
					return false;
				}
			}

			// Editor gizmo end render
			renderer_end_rendering(state->renderer, p_frame_data);
			renderer_end_debug_label();
		}
	}
#endif
	return true;
}

void editor_on_window_resize(struct editor_state* state, const struct kwindow* window) {
	if (!window->width || !window->height) {
		return;
	}

	// Resize cameras.
	rect_2di world_vp_rect = {0, 0, window->width, window->height};

	kcamera_set_vp_rect(state->editor_camera, world_vp_rect);

	// Send the resize off to the scene, if it exists.
	kscene_on_window_resize(state->edit_scene, window);

	// UI elements
	ktransform_position_set(state->scene_inspector_bg_panel.ktransform, (vec3){window->width - (state->scene_inspector_width + 10), 10});
	ktransform_position_set(state->entity_inspector_bg_panel.ktransform, (vec3){window->width - (state->entity_inspector_width + 10), 10});

	ktransform_position_set(state->tree_inspector_bg_panel.ktransform, (vec3){window->width - (state->tree_inspector_width + 10), 10});
	sui_panel_set_height(state->sui_state, &state->tree_inspector_bg_panel, window->height - 120.0f);
}

void editor_setup_keymaps(struct editor_state* state) {
	state->editor_keymap = keymap_create();
	/* state->editor_keymap.overrides_all = true; */

	keymap_binding_add(&state->editor_keymap, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);
	keymap_binding_add(&state->editor_keymap, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);

	keymap_binding_add(&state->editor_keymap, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);
	keymap_binding_add(&state->editor_keymap, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);

	keymap_binding_add(&state->editor_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_pitch);
	keymap_binding_add(&state->editor_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_pitch);

	keymap_binding_add(&state->editor_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_forward);
	keymap_binding_add(&state->editor_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, state, editor_on_sprint_forward);
	keymap_binding_add(&state->editor_keymap, KEY_S, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_backward);
	keymap_binding_add(&state->editor_keymap, KEY_Q, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_left);
	keymap_binding_add(&state->editor_keymap, KEY_E, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_right);
	keymap_binding_add(&state->editor_keymap, KEY_SPACE, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_up);
	keymap_binding_add(&state->editor_keymap, KEY_X, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_down);

	keymap_binding_add(&state->editor_keymap, KEY_0, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_default);
	keymap_binding_add(&state->editor_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_lighting);
	keymap_binding_add(&state->editor_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_normals);
	keymap_binding_add(&state->editor_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_cascades);
	keymap_binding_add(&state->editor_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_wireframe);

	keymap_binding_add(&state->editor_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_G, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_gizmo_orientation_set);

	// ctrl s
	keymap_binding_add(&state->editor_keymap, KEY_S, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_save_scene);

	keymap_binding_add(&state->editor_keymap, KEY_Z, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_zoom_extents);
}

static f32 get_engine_delta_time(void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_delta_get(engine);
}

static f32 get_engine_total_time(void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_total_get(engine);
}

static b8 editor_has_focused_control(editor_state* editor) {
	return editor->sui_state->focused != KNULL;
}

static void editor_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 f = 0.0f;
	if (key == KEY_LEFT || key == KEY_A) {
		f = 1.0f;
	} else if (key == KEY_RIGHT || key == KEY_D) {
		f = -1.0f;
	}
	kcamera_yaw(state->editor_camera, f * get_engine_delta_time());
}

static void editor_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 f = 0.0f;
	if (key == KEY_UP) {
		f = 1.0f;
	} else if (key == KEY_DOWN) {
		f = -1.0f;
	}

	kcamera_pitch(state->editor_camera, f * get_engine_delta_time());
}

static void editor_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 0");
}

static void editor_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 1");
}

static void editor_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 2");
}

static void editor_on_set_render_mode_cascades(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 3");
}

static void editor_on_set_render_mode_wireframe(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 4");
}

static void editor_on_set_gizmo_mode(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	editor_gizmo_mode mode;
	switch (key) {
	case KEY_1:
	default:
		mode = EDITOR_GIZMO_MODE_NONE;
		break;
	case KEY_2:
		mode = EDITOR_GIZMO_MODE_MOVE;
		break;
	case KEY_3:
		mode = EDITOR_GIZMO_MODE_ROTATE;
		break;
	case KEY_4:
		mode = EDITOR_GIZMO_MODE_SCALE;
		break;
	}
	editor_gizmo_mode_set(&state->gizmo, mode);
}

static void editor_on_gizmo_orientation_set(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	editor_gizmo_orientation orientation = editor_gizmo_orientation_get(&state->gizmo);
	orientation++;
	if (orientation > EDITOR_GIZMO_ORIENTATION_MAX) {
		orientation = 0;
	}
	editor_gizmo_orientation_set(&state->gizmo, orientation);
}

static void editor_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_sprint_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * 2 * delta);
}

static void editor_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_backward(state->editor_camera, state->editor_camera_backward_move_speed * delta);
}

static void editor_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_left(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_right(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	kcamera_move_up(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
}

static void editor_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	kcamera_move_down(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
}

static void save_scene(const struct kscene* scene, kname package_name, kname asset_name) {
	if (scene) {
		kscene_state scene_state = kscene_state_get(scene);
		if (scene_state == KSCENE_STATE_LOADED) {
			KDEBUG("Saving current scene...");
			const char* serialized = kscene_serialize(scene);
			if (!serialized) {
				KERROR("Scene serialization failed! Scene save thus fails. Check logs.");
				return;
			}

			// Write the text asset to disk
			if (!asset_system_write_text(engine_systems_get()->asset_state, package_name, asset_name, serialized)) {
				KERROR("Failed to save scene asset.");
			}
		} else {
			KERROR("Current scene is not in a loaded state, and cannot be saved.");
		}
	} else {
		KERROR("No scene is open to be saved.");
	}
}

static void editor_on_save_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
}

static void editor_on_zoom_extents(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	KTRACE("Zoom extents");

	editor_state* state = (editor_state*)user_data;

	if (darray_length(state->selection_list)) {

		/* ktransform t = kscene_get_entity_transform(state->edit_scene, state->selection_list[0]);
		vec3 center = ktransform_world_position_get(t); */

		mat4 view = kcamera_get_view(state->editor_camera);

		f32 fov = kcamera_get_fov(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		f32 aspect = (f32)vp_rect.width / vp_rect.height;
		f32 tan_half_fov_y = ktan(fov * 0.5f);
		f32 tan_half_fov_x = tan_half_fov_y * aspect;

		f32 required_distance = 0.0f;

		aabb box = kscene_get_aabb(state->edit_scene, state->selection_list[0]);
		vec3 min = box.min;
		vec3 max = box.max;

		vec3 corners[8] = {
			{min.x, min.y, min.z},
			{max.x, min.y, min.z},
			{min.x, max.y, min.z},
			{max.x, max.y, min.z},
			{min.x, min.y, max.z},
			{max.x, min.y, max.z},
			{min.x, max.y, max.z},
			{max.x, max.y, max.z},
		};

		vec3 center = vec3_zero();
		for (u32 i = 0; i < 8; ++i) {
			// Center is the average of all points.
			center = vec3_add(center, corners[i]);

			// Move the corner to camera space.
			corners[i] = vec3_transform(corners[i], 1.0f, view);

			f32 x = kabs(corners[i].x);
			f32 y = kabs(corners[i].y);
			f32 z = -corners[i].z; // camera looks -z

			// Ignore corners behind camera .
			if (z <= 0.0f) {
				continue;
			}

			f32 d_x = x / tan_half_fov_x;
			f32 d_y = y / tan_half_fov_y;

			f32 d = KMAX(d_x, d_y);
			if (d > required_distance) {
				required_distance = d;
			}
		}
		center = vec3_div_scalar(center, 8.0f);

		// Pad it a bit.
		required_distance *= 1.05f;

		vec3 position = vec3_mul_scalar(kcamera_forward(state->editor_camera), required_distance);
		position = vec3_sub(center, position);

		kcamera_set_position(state->editor_camera, position);
	}
}

static void editor_command_execute(console_command_context context) {
	editor_state* state = (editor_state*)context.listener;
	if (strings_equal(context.command_name, "editor_save_scene")) {
		save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
	} else if (strings_equal(context.command_name, "editor_select_parent")) {
		editor_select_parent(state);
	} else if (strings_equal(context.command_name, "editor_dump_hierarchy")) {
		kscene_dump_hierarchy(state->edit_scene);
	} else if (strings_equal(context.command_name, "editor_set_selected_rotation")) {
		if (context.argument_count != 4) {
			KERROR("editor_set_selected_rotation requires 4 arguments (quaternion x, y, z, w)");
			return;
		} else {
			quat q = {0};
			for (u8 i = 0; i < 4; ++i) {
				string_to_f32(context.arguments[i].value, &q.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_rotation requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_rotation(state->edit_scene, state->selection_list[0], q);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_position")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_position requires 3 arguments (position x, y, z)");
			return;
		} else {
			vec3 p = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &p.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_position requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_position(state->edit_scene, state->selection_list[0], p);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_scale")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_scale requires 3 arguments (scale x, y, z)");
			return;
		} else {
			vec3 scale = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &scale.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_scale requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_scale(state->edit_scene, state->selection_list[0], scale);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_add_model")) {
		// editor_add_model "name with spaces" "asset name with spaces" "package name with spaces"
		// editor_add_model "barrels entity" "barrels model" Testbed
		kname name = kname_create(context.arguments[0].value);
		kname asset_name = kname_create(context.arguments[1].value);
		// Third property is optional and defaults to application package name.
		kname package_name = context.argument_count == 3 ? kname_create(context.arguments[2].value) : INVALID_KNAME;
		// Assign as a child of the first currently selected entity, if it exists.
		kentity parent = darray_length(state->selection_list) ? state->selection_list[0] : KENTITY_INVALID;

		kentity new_entity = kscene_add_model(state->edit_scene, name, KTRANSFORM_INVALID, parent, asset_name, package_name, 0, 0);

		// Select it
		editor_select_entities(state, 1, &new_entity);
	}
}

static void editor_register_events(struct editor_state* state) {
	KASSERT(event_register(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button));
	KASSERT(event_register(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag));
}

static void editor_unregister_events(struct editor_state* state) {
	event_unregister(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button);
	event_unregister(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move);
	event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag);
}

static void editor_register_commands(struct editor_state* state) {
	KASSERT(console_command_register("editor_save_scene", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_select_parent", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_dump_hierarchy", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_position", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_rotation", 4, 4, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_scale", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_add_model", 2, 3, state, editor_command_execute));
}

static void editor_unregister_commands(struct editor_state* state) {
	console_command_unregister("editor_save_scene");
	console_command_unregister("editor_select_parent");
	console_command_unregister("editor_dump_hierarchy");
	console_command_unregister("editor_set_selected_position");
	console_command_unregister("editor_set_selected_rotation");
	console_command_unregister("editor_set_selected_scale");
	console_command_unregister("editor_add_model");
}

void editor_on_lib_load(struct editor_state* state) {
	if (state->is_running) {
		editor_register_events(state);
		editor_register_commands(state);
	}
}
void editor_on_lib_unload(struct editor_state* state) {
	editor_unregister_events(state);
	editor_unregister_commands(state);
}

static b8 save_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KTRACE("Save button clicked.");

	console_command_execute("editor_save_scene");

	// Don't allow the event to popagate.
	return false;
}
static b8 mode_scene_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KTRACE("Editor mode SCENE button clicked.");
	editor_set_mode(self->user_data, EDITOR_MODE_SCENE);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_entity_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KTRACE("Editor mode ENTITY button clicked.");
	editor_set_mode(self->user_data, EDITOR_MODE_ENTITY);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_tree_button_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	KTRACE("Editor mode TREE button clicked.");
	editor_set_mode(self->user_data, EDITOR_MODE_TREE);

	tree_refresh(self->user_data);
	// Don't allow the event to popagate.
	return false;
}

static b8 editor_on_mouse_move(u16 code, void* sender, void* listener_inst, event_context context) {
	editor_state* state = (editor_state*)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(MOUSE_BUTTON_LEFT)) {
		b8 has_selection = state->selection_list && darray_length(state->selection_list);
		if (has_selection) {
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];

			mat4 view = kcamera_get_view(state->editor_camera);
			vec3 origin = kcamera_get_position(state->editor_camera);
			rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
			mat4 projection = kcamera_get_projection(state->editor_camera);

			ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

			editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER);
		}
	}

	return false; // Allow other event handlers to process this event.
}

static b8 editor_on_drag(u16 code, void* sender, void* listener_inst, event_context context) {
	editor_state* state = (editor_state*)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	i16 x = context.data.i16[0];
	i16 y = context.data.i16[1];
	u16 drag_button = context.data.u16[2];

	// Only care about left button drags.
	if (drag_button == MOUSE_BUTTON_LEFT) {
		mat4 view = kcamera_get_view(state->editor_camera);
		vec3 origin = kcamera_get_position(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		mat4 projection = kcamera_get_projection(state->editor_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

		if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
			state->using_gizmo = true;
			// Drag start -- change the interaction mode to "dragging".
			editor_gizmo_interaction_begin(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
		} else if (code == EVENT_CODE_MOUSE_DRAGGED) {
			editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
		} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
			editor_gizmo_interaction_end(&state->gizmo);
			state->using_gizmo = false;
		}

		// TODO: update function
		// Update inspector position controls.
		{
			vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
			const char* xt = f32_to_string(position.x);
			sui_textbox_text_set(state->sui_state, &state->entity_position_x_textbox, xt);
			string_free(xt);
			const char* yt = f32_to_string(position.y);
			sui_textbox_text_set(state->sui_state, &state->entity_position_y_textbox, yt);
			string_free(yt);
			const char* zt = f32_to_string(position.z);
			sui_textbox_text_set(state->sui_state, &state->entity_position_z_textbox, zt);
			string_free(zt);
		}

		// Update inspector orientation controls.
		{
			quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
			const char* x = f32_to_string(rotation.x);
			sui_textbox_text_set(state->sui_state, &state->entity_orientation_x_textbox, x);
			string_free(x);
			const char* y = f32_to_string(rotation.y);
			sui_textbox_text_set(state->sui_state, &state->entity_orientation_y_textbox, y);
			string_free(y);
			const char* z = f32_to_string(rotation.z);
			sui_textbox_text_set(state->sui_state, &state->entity_orientation_z_textbox, z);
			string_free(z);
			const char* w = f32_to_string(rotation.w);
			sui_textbox_text_set(state->sui_state, &state->entity_orientation_w_textbox, w);
			string_free(w);
		}

		// Update inspector scale controls.
		{
			vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
			const char* xt = f32_to_string(scale.x);
			sui_textbox_text_set(state->sui_state, &state->entity_scale_x_textbox, xt);
			string_free(xt);
			const char* yt = f32_to_string(scale.y);
			sui_textbox_text_set(state->sui_state, &state->entity_scale_y_textbox, yt);
			string_free(yt);
			const char* zt = f32_to_string(scale.z);
			sui_textbox_text_set(state->sui_state, &state->entity_scale_z_textbox, zt);
			string_free(zt);
		}
	}

	return false; // Let other handlers handle.
}

i32 raycast_hit_kquicksort_compare(void* a, void* b) {
	raycast_hit* a_typed = a;
	raycast_hit* b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return -1;
	} else if (a_typed->distance < b_typed->distance) {
		return 1;
	}
	return 0;
}
i32 raycast_hit_kquicksort_compare_desc(void* a, void* b) {
	raycast_hit* a_typed = a;
	raycast_hit* b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return 1;
	} else if (a_typed->distance < b_typed->distance) {
		return -1;
	}
	return 0;
}

/* KAPI void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn); */

static b8 editor_on_button(u16 code, void* sender, void* listener_inst, event_context context) {
	if (code == EVENT_CODE_BUTTON_PRESSED) {
		//
	} else if (code == EVENT_CODE_BUTTON_RELEASED) {
		u16 button = context.data.u16[2];
		switch (button) {
		case MOUSE_BUTTON_LEFT: {
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];
			editor_state* state = (editor_state*)listener_inst;

			if (state->edit_scene) {

				if (state->using_gizmo) {
					return false;
				}
				kscene_state scene_state = kscene_state_get(state->edit_scene);
				if (scene_state == KSCENE_STATE_LOADED) {
					mat4 view = kcamera_get_view(state->editor_camera);
					mat4 projection = kcamera_get_projection(state->editor_camera);
					vec3 origin = kcamera_get_position(state->editor_camera);
					rect_2di current_vp_rect = kcamera_get_vp_rect(state->editor_camera);

					// Multi-select
					b8 multiselect = (input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL));

					struct kscene* current_scene = state->edit_scene;
					// Cast a ray into the scene and see if anything can be selected.
					if (point_in_rect_2di((vec2i){x, y}, current_vp_rect)) {
						ray r = ray_from_screen((vec2i){x, y}, current_vp_rect, origin, view, projection);
						r.max_distance = 2000.0f;
						// Ignore collisions occurring where the ray's origin is inside a BVH node.
						FLAG_SET(r.flags, RAY_FLAG_IGNORE_IF_INSIDE_BIT, true);
						raycast_result result = {0};
						if (kscene_raycast(current_scene, &r, &result)) {

							u32 hit_count = result.hits ? darray_length(result.hits) : 0;
							if (!hit_count) {
								KINFO("Nothing hit from raycast.");
								editor_clear_selected_entities(state);
							} else {
								if (!multiselect) {
									KINFO("Not multiselecting, clearing selection...");
									editor_clear_selected_entities(state);
								}

								// Sort hits by distance.
								kquick_sort(sizeof(raycast_hit), result.hits, 0, hit_count - 1, raycast_hit_kquicksort_compare);

								for (u32 i = 0; i < hit_count; ++i) {
									// Each thing. Use this to make selections, etc.
									raycast_hit* hit = &result.hits[i];

									kentity entity = (kentity)hit->user;

									// Skip BVH-only hits.
									if (hit->type == RAYCAST_HIT_TYPE_BVH_AABB) {
										KTRACE("Skipping BVH AABB hit (name='%k')", kscene_get_entity_name(state->edit_scene, entity));
										continue;
									}

									// Add to selection.
									editor_add_to_selected_entities(state, 1, &entity);
									// NOTE: only taking the first thing from the list.
									break;
								}
							}
						}
					}
				}
			}
		} break;
		}
	}

	// Allow other handlers to process the event.
	return false;
}

static void scene_name_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_name(editor->edit_scene, entry_control_text);
			}
		}
	}
}

static void entity_name_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_entity_name(editor->edit_scene, editor->selection_list[0], kname_create(entry_control_text));
			}
		}
	}
}

static void entity_position_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					position.x = x;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_position_y_textbox);
		}
	}
}
static void entity_position_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					position.y = y;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_position_z_textbox);
		}
	}
}
static void entity_position_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					position.z = z;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_position_x_textbox);
		}
	}
}

static void entity_orientation_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					rotation.x = x;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_orientation_y_textbox);
		}
	}
}
static void entity_orientation_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					rotation.y = y;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_orientation_z_textbox);
		}
	}
}

static void entity_orientation_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					rotation.z = z;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_orientation_w_textbox);
		}
	}
}

static void entity_orientation_w_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 w;
				if (string_to_f32(val, &w)) {
					rotation.w = w;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_orientation_x_textbox);
		}
	}
}

static void entity_scale_x_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					scale.x = x;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_scale_y_textbox);
		}
	}
}
static void entity_scale_y_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					scale.y = y;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_scale_z_textbox);
		}
	}
}
static void entity_scale_z_textbox_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
	if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = self->user_data;

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = sui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = sui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					scale.z = z;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			standard_ui_system_focus_control(state, &editor->entity_scale_x_textbox);
		}
	}
}

typedef struct hierarchy_node_context {
	editor_state* editor;
	kentity entity;
} hierarchy_node_context;

static void tree_refresh(editor_state* state) {
	if (state->edit_scene) {
		// TODO: destroy recursively when done.
		u32 count = 0;
		kscene_hierarchy_node* nodes = kscene_get_hierarchy(state->edit_scene, &count);

		// Destroy all the current controls.
		if (state->tree_element_count) {
			for (u32 i = 0; i < count; ++i) {
				KFREE_TYPE(state->tree_elements[i].user_data, hierarchy_node_context, MEMORY_TAG_ENGINE);
				sui_label_control_destroy(state->sui_state, &state->tree_elements[i]);
			}
			KFREE_TYPE_CARRAY(state->tree_elements, sui_control, count);
			state->tree_elements = KNULL;
			state->tree_element_count = 0;
		}

		if (count) {

			state->tree_element_count = count;
			state->tree_elements = KALLOC_TYPE_CARRAY(sui_control, count);
			standard_ui_state* sui_state = state->sui_state;

			// Create all the new labels.
			for (u32 i = 0; i < count; ++i) {
				kscene_hierarchy_node* node = &nodes[i];
				sui_control* label = &state->tree_elements[i];

				kname name = kscene_get_entity_name(state->edit_scene, node->entity);

				// TODO: make label names unique

				KASSERT(sui_label_control_create(sui_state, "entity", FONT_TYPE_SYSTEM, state->font_name, state->font_size, kname_string_get(name), label));
				KASSERT(standard_ui_system_register_control(sui_state, label));
				KASSERT(standard_ui_system_control_add_child(sui_state, &state->tree_inspector_bg_panel, label));
				ktransform_position_set(label->ktransform, (vec3){10, (35 * (i + 1)) + (-5.0f), 0});
				label->is_active = true;
				standard_ui_system_update_active(sui_state, label);
				label->is_visible = true;

				hierarchy_node_context* context = KALLOC_TYPE(hierarchy_node_context, MEMORY_TAG_ENGINE);
				context->editor = state;
				context->entity = node->entity;

				label->user_data = (void*)context;
				label->on_click = tree_label_clicked;
			}
		}
	}
}

static b8 tree_label_clicked(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
	hierarchy_node_context* context = (hierarchy_node_context*)self->user_data;

	editor_clear_selected_entities(context->editor);
	editor_add_to_selected_entities(context->editor, 1, &context->entity);

	return true;
}

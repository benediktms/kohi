#pragma once

#include "core/keymap.h"
#include "debug_console.h"
#include "defines.h"
#include "editor/editor.h"
#include "kui_system.h"
#include "renderer/kforward_renderer.h"
#include "renderer/kui_renderer.h"
#include "systems/kcamera_system.h"
#include "time/kclock.h"

#include <resources/debug/debug_grid.h>
#include <utils_plugin_defines.h>

#if KOHI_DEBUG
#	define testbed_EDITOR 1
#endif

#define PACKAGE_NAME_TESTBED "Testbed"

typedef enum testbed_app_mode {
	TESTBED_APP_MODE_WORLD,
#ifdef testbed_EDITOR
	TESTBED_APP_MODE_EDITOR,
#endif
	TESTBED_APP_MODE_MAIN_MENU,
	TESTBED_APP_MODE_PAUSE_MENU
} testbed_application_mode;

KINLINE const char* testbed_application_mode_to_string(testbed_application_mode mode) {
	switch (mode) {
	default:
	case TESTBED_APP_MODE_WORLD:
		return "WORLD";
	case TESTBED_APP_MODE_EDITOR:
		return "EDITOR";
	case TESTBED_APP_MODE_MAIN_MENU:
		return "MAIN_MENU";
	case TESTBED_APP_MODE_PAUSE_MENU:
		return "PAUSE";
	}
}

// User-defined codes to be used with the event system.
typedef enum game_event_code {
	// Start of the User-defined code range. Not an actual used code.
	GAME_EVENT_CODE_START = 0x00FF,
	GAME_EVENT_CODE_UNUSED = 0x0100,

	/**
	 * @brief An event fired when context sensitivity text should be displayed.
	 *
	 * Context usage:
	 * const char* display_text = context.data.s
	 */
	GAME_EVENT_CODE_SHOW_CONTEXT_DISPLAY = 0x0101,

	/*
	 * @brief An event fired when context sensitivity text should be displayed.
	 *
	 * Context usage: N/A
	 */
	GAME_EVENT_CODE_HIDE_CONTEXT_DISPLAY = 0x0102,

} game_event_code;

typedef struct game_state_serializable {
	u64 time_played_seconds;
} game_state_serializable;

typedef struct game_constants {
	f32 base_movement_speed;
	// TODO: Get rid of this in favour of mouse-pos-based rotation/gamepad right stick rotations.
	f32 turn_speed;
} game_constants;

// Represents the actual state of the game; serialized to disk on game save.
typedef struct game_state {

	game_state_serializable serializable;
	game_constants constants;

} game_state;

#ifdef testbed_EDITOR
struct editor_state;
#endif

typedef struct application_state {
	b8 running;
	// Used as the default for rendering the world.
	kcamera world_camera;
	kcamera ui_camera;

	keymap global_keymap;
	keymap world_keymap;

	// The current mode of the game, which controls input, etc.
	testbed_application_mode mode;

	u16 width, height;

	kname scene_name;
	kname scene_package_name;
	struct kscene* current_scene;

	// Pointers to engine systems.
	struct kaudio_system_state* audio_system;
	struct kruntime_plugin* kui_plugin;
	struct kui_plugin_state* kui_plugin_state;
	struct kui_state* kui_state;

	kclock update_clock;
	kclock prepare_clock;
	kclock render_clock;
	f64 last_update_elapsed;

	// The forward game renderer.
	kforward_renderer game_renderer;
	kui_renderer kui_renderer;

	mat4 world_projection;
	mat4 ui_projection;

	u32 render_mode;

	// Previous frame allocator memory allocated
	u64 prev_framealloc_allocated;
	// Previous frame allocator total memory (in case it changes)
	u64 prev_framealloc_total;

	// NOTE: Debug stuff to eventually be excluded on release builds.
#ifdef KOHI_DEBUG
	kui_control debug_text;
	kui_control debug_text_shadow;
	debug_console_state debug_console;
	keymap console_keymap;
#endif

	game_state game;

	struct item_db* db;

	// UI state
	kui_control context_sensitive_text;

#ifdef KOHI_EDITOR
	struct editor_state* editor;
#endif
} application_state;

struct testbed_render_data;

typedef struct application_frame_data {
	struct testbed_render_data* render_data;
} application_frame_data;

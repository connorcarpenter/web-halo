/*
MAIN_FATAL.C

Fatal halt implementation, separated from MAIN.C so the real production
definition links without the executable-entry translation unit. The body
is unchanged from the MAIN.C original; native and HALO_WEB behavior and
dependencies are identical.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries/profile.h"
#include "cseries/cseries_windows.h"
#include "main.h"
#include "real_math.h"
#include "game.h"
#include "game_engine.h"
#include "game/campaign_coop.h"
#include "game/cheats.h"
#include "game/player_control.h"
#include "game/player_control_runtime.h"
#include "game/players.h"
#include "game/local_players.h"
#include "game/player_queues_new.h"
#include "integer_math.h"
#include "main/main_runtime.h"
#include "input.h"
#include "input/input_abstraction.h"
#include "shell.h"
#include "event_manager.h"
#include "telnet_console.h"
#include "ui_widget.h"
#include "editor_stubs.h"
#include "console.h"
#include "debug_keys.h"
#include "scenario.h"
#include "scenario/scenario_definitions.h"
#include "cache/cache_files.h"
#include "cache/predicted_resources.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/tiff_file.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "interface/attract_mode.h"
#include "interface/interface.h"
#include "interface/marketing_and_strategic_business_development.h"
#include "interface/terminal.h"
#include "saved games/player_profile.h"
#include "saved games/game_state.h"
#include "sound/sound_manager.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_debug.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "bink/bink_playback.h"
#include "main/d3d_intimacy.h"
#include "networking/network_game_globals.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "effects/player_effects.h"
#include "physics/collision_usage.h"
#include "physics/collision_debug.h"
#include "hs/hs.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "tag_files/files.h"

#ifdef HALO_WEB
#include <emscripten/emscripten.h>
#include "../../port/web/src/web_online_ui.h"
#endif

#include "main_screenshot.h"

void halt_and_catch_fire(
	void)
{
	short gamepad_index;
	long font_tag_index;
	struct scenario *scenario;
	struct rasterizer_frame_begin_parameters frame_parameters;
	struct rasterizer_window_begin_parameters window_parameters;

#ifdef HALO_WEB
	platform_log("web halt: %s", error_get());

	// A fatal error can arrive inside Emscripten's proxied main-loop callback.
	// The native halt loop below never returns, so its presents cannot complete
	// in that environment and instead become an unbounded CPU/swap loop.
	// Stop future callbacks and terminate this web runtime; native builds retain
	// the original on-screen halt behavior.
	emscripten_cancel_main_loop();
	emscripten_force_exit(EXIT_FAILURE);
#endif

	if (!global_screenshot_count.halt_recursion_lock)
	{
		scenario = global_scenario_try_and_get();
		global_screenshot_count.halt_recursion_lock = TRUE;
		for (gamepad_index = 0; gamepad_index < MAXIMUM_GAMEPADS; gamepad_index++)
		{
			if (input_has_gamepad(gamepad_index))
			{
				input_set_gamepad_rumbler_state(
					gamepad_index,
					0,
					0);
			}
		}

		if (scenario)
		{
			font_tag_index = interface_get_tag_index(_interface_font_terminal);
		}
		if (!scenario || font_tag_index == NONE)
		{
			font_tag_index = tag_loaded(
				FONT_GROUP_TAG,
				"old tags\\internal system plain");
		}

		while (TRUE)
		{
			rasterizer_reset_state();
			csmemset(&frame_parameters, 0, sizeof(frame_parameters));
			rasterizer_frame_begin(&frame_parameters);
			rasterizer_windows_begin();

			csmemset(&window_parameters, 0, sizeof(window_parameters));
			window_parameters.camera.position = *global_origin3d;
			window_parameters.camera.forward = *global_forward3d;
			window_parameters.camera.up = *global_up3d;
			window_parameters.camera.mirrored = FALSE;
			window_parameters.camera.vertical_field_of_view =
				2.0f * arctangent(
					0.75f * render_camera_get_adjusted_field_of_view_tangent(
						DEGREES_TO_RADIANS(80.0f)),
					1.0f);
			window_parameters.camera.z_near = rasterizer_globals.near_clip_distance;
			window_parameters.camera.viewport_bounds.x0 = 0;
#ifdef HALO_LINUX
			window_parameters.camera.viewport_bounds.x1 = (short)halo_screen_width();
#else
			window_parameters.camera.viewport_bounds.x1 = 640;
#endif
			window_parameters.camera.viewport_bounds.y0 = 0;
			window_parameters.camera.viewport_bounds.y1 = 480;
			window_parameters.camera.z_far = rasterizer_globals.far_clip_distance;
			render_camera_build_frustum(
				&window_parameters.camera,
				NULL,
				&window_parameters.frustum,
				TRUE);
			window_parameters.rasterizer_target = 0;
			window_parameters.fog.atmospheric_color = *global_real_rgb_blue;
			window_parameters.fog.atmospheric_maximum_distance = 0.0f;
			window_parameters.fog.atmospheric_minimum_distance = 0.0f;
			window_parameters.fog.planar_mode = 0;
			render.camera = window_parameters.camera;
			rasterizer_window_begin(&window_parameters);

			if (font_tag_index != NONE)
			{
				point2d cursor = { 0, 0 };
				rectangle2d bounds = rasterizer_globals.reserved04.frame_bounds;

				draw_string_set_draw_mode(
					font_tag_index,
					NONE,
					0,
					0,
					global_real_argb_white);
				draw_string_set_tab_stops(NULL, 0);
				draw_string_set_color(global_real_argb_white);
				rasterizer_draw_string(
					&bounds,
					NULL,
					&cursor,
					-4,
					"halobeta xbox 01.01.14.2342 built at: Jan 14 2002 12:49:20");
				bounds.y0 = cursor.y - 1;
				rasterizer_draw_string(
					&bounds,
					NULL,
					&cursor,
					-4,
					error_get());
			}

			rasterizer_transparent_geometry_draw(TRUE);
			rasterizer_transparent_geometry_draw(FALSE);
			rasterizer_debug_draw();
			rasterizer_window_end();
			rasterizer_windows_end();
			rasterizer_frame_end();
			rasterizer_present(NULL, NULL);
			input_update();
		}
	}

	exit(0);
	return;
}

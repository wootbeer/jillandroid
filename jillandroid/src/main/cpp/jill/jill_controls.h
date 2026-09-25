/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code ("Jill of the Jungle Reconstructed").

Jill of the Jungle Reconstructed is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Jill of the Jungle Reconstructed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Jill of the Jungle Reconstructed.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#ifndef JILL_CONTROLS_H
#define JILL_CONTROLS_H

#include <stdbool.h>

/* On-screen touch D-pad + JUMP/THROW/PAUSE buttons for Jill, plus the gamepad-connected flag that
 * hides them while a real controller is attached -- Android port addition, not upstream (real DOS
 * Jill only ever had keyboard/joystick input, see GAMECTRL.c's own header comment). Ported from the
 * God of Thunder Android port's own got_controls.c/.h (wootbeer's own explicit ask: "do we even have
 * our basic support from descore? ... let's also get our 'touch options' sub-menu going, the same
 * one we had in god of thunder"), scoped down to Jill's own much smaller action set -- see
 * jill_controls.c's own top-of-file comment for the full rationale and exactly what's the same/
 * different from got_controls.c. This header is only what HOSTANDROID.c/JUNGLE.c need to call into
 * jill_controls.c -- the JNI entry points Java calls (jillTouchHandler(), jillSetGamepadConnected(),
 * jillSetDisplayDensity()) live in jill_controls.c itself, not here, same as got_controls.c's own
 * split. */

/* Sizes and positions the on-screen D-pad/JUMP/THROW/PAUSE buttons' hit-test rectangles from the
 * real screen dimensions -- called every time HOSTANDROID.c's jillhost_set_surface_size() runs (both
 * the very first call, from jillMain(), and every later jillSurfaceResized(), unlike
 * got_controls_init()'s own GoT-side "once at startup only" precedent -- Jill's own hook already
 * fires on resize, so there's no reason not to re-lay-out then too). */
void jill_controls_init(int screen_w, int screen_h);

/* Draws the on-screen touch buttons (translucent rectangles, screen-pixel space) on top of whatever
 * this frame already drew -- called every frame from HOSTANDROID.c's host_present(), right before
 * its own eglSwapBuffers() call. No-ops (draws nothing) while a real gamepad is connected -- see
 * jill_controls.c's own gamepad_connected comment. */
void jill_draw_touch_buttons(void);

/* New for the "Touch Options" pause-menu submenu (JUNGLE.c's pausemenu()) -- mirrors
 * got_controls_touch_enabled() exactly. True once jill_controls_init() has run and no gamepad is
 * currently connected. */
bool jill_controls_touch_enabled(void);

/* Selects one of the shared descore/touch/descore_touch.c module's own kDescoreTouchScaleValues[]
 * table entries (0-based index, same 9-entry table got_controls.c originally established -- see
 * that shared module's own comment) and immediately re-lays-out every on-screen button at the new
 * size. */
void jill_controls_set_scale_step(int step);

/* Pure lookup into the same kDescoreTouchScaleValues[] table jill_controls_set_scale_step()
 * indexes -- JUNGLE.c's own Touch Options label formatter uses this to show the actual multiplier
 * text (e.g. "1.00x") next to the Scaling slider's bar. */
float jill_controls_touch_scale_value(int step);

/* Sets the translucent alpha jill_draw_touch_buttons() renders every button with -- clamped
 * internally to the same [previous-default, fully-opaque] floor JUNGLE.c's own Touch Options
 * submenu already enforces on the step value before converting it to a float here (a second,
 * defensive clamp, not the only one -- same shape got_controls_set_opacity() already established). */
void jill_controls_set_opacity(float alpha);

/* True whenever a real gamepad is currently connected -- the same live gamepad_connected flag
 * jill_controls_touch_enabled() already exposes the inverse of, just under its own name so
 * JUNGLE.c's pausemenu() top-level row (which shows EITHER "TOUCH OPTIONS" OR "REMAP GAMEPAD"
 * depending on this) doesn't have to read !jill_controls_touch_enabled() and reason through the
 * double negative -- exact same precedent got_controls_gamepad_connected() already established. */
bool jill_controls_gamepad_connected(void);

/* One-time real display density (Android's `density` -- 1.0 at the historical 160dpi baseline, the
 * same scale factor Android's own dp unit is defined against), set once from
 * Java_wootbeer_jillandroid_JillView_jillSetDisplayDensity() before the render thread (and so this
 * module's own jill_controls_init()) ever runs. Used only for the TOUCH_MIN_CELL_DP absolute-size
 * floor (jill_controls.c's own layout_buttons() comment) -- simpler than the God of Thunder Android
 * port's own got_px_to_dp()/got_dp_to_px() JNI round-trip (got_main.c), since Java already has this
 * value directly (Resources.getDisplayMetrics().density) with no native call-back needed to get it.
 * Defaults to 1.0f if never called (harmless -- simply the historical-baseline floor, not a crash or
 * an uninitialized read), though JillView.java's constructor always calls this before the render
 * thread ever starts. */
void jill_controls_set_display_density(float density);

/* New for the Noisemaker screen (JUNGLE.c's own noisemaker()) -- wootbeer's own explicit ask, once
 * touch controls were confirmed working: a small on-screen KEYBOARD TOGGLE button (see
 * jill_controls.c's own BTN_KEYBOARD_TOGGLE comment) that shows/hides the real Android soft
 * keyboard, so a touch-only player can actually type the letter/number keys noisemaker() reads to
 * play notes -- something no other screen in the game needs, so this button is only drawn/hit-
 * tested while `active` is true. Call with true right when noisemaker() is entered (immediately
 * followed by one gr_present_page() call so the button actually appears on screen -- see that
 * function's own comment for why a fresh present is needed there specifically) and with false right
 * before it returns; going true -> false also hides the real keyboard automatically if the button
 * was left toggled on, and resets that toggle's own remembered on/off state back to off, so the
 * player never has to remember to close the keyboard themselves before leaving -- same "release
 * anything left held on a state transition" hygiene jillSetGamepadConnected() already established
 * for the D-pad/JUMP/THROW buttons. */
void jill_controls_set_noisemaker_active(bool active);

/* Flips BTN_KEYBOARD_TOGGLE's own on/off state and asks Android to show or hide the real soft
 * keyboard accordingly -- the exact same action a tap on that button performs (jill_controls.c's own
 * handle_down() calls this too), exposed here so JUNGLE.c's noisemaker() can trigger it directly from
 * the gamepad Select button as well. wootbeer's own follow-up ask, after Start/Select were first wired to
 * exit Noisemaker: "with gamepad enabled can we also make select open and close the android keyboard
 * instead of closing from noisemaker?" -- Select no longer exits (Start/key_pause alone does that
 * now, still via the on-screen PAUSE button too), it toggles the keyboard, mirroring
 * BTN_KEYBOARD_TOGGLE's own touch-only equivalent so gamepad players get the same capability without
 * ever needing to touch the screen. Safe to call regardless of Noisemaker's own active state (though
 * in practice JUNGLE.c only ever calls this from within noisemaker()'s own loop, the one place
 * key_select is read at all) -- it just toggles s_keyboard_shown and calls host_show_keyboard()/
 * host_hide_keyboard() (HOSTSDL.H), the same fire-and-forget pair every other call site already uses,
 * with no dependency on whether the on-screen button itself happens to be visible right now. */
void jill_controls_toggle_keyboard(void);

#endif

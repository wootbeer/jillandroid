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

/* Real on-screen touch D-pad for Jill, plus the gamepad-connected flag that hides it -- Android port
 * addition, not upstream (real DOS Jill only ever had keyboard/joystick input, see GAMECTRL.c's own
 * header comment on why its joystick reads are permanently stubbed "absent"). wootbeer's own explicit
 * ask: "now to work on our on-screen touch control support. do we even have our basic support from
 * descore? and if so let's also get our 'touch options' sub-menu going, the same one we had in god
 * of thunder... if the gamepad is disconnected then the touch controls will show, and the 'remap
 * gamepad' sub-menu listing will be replaced with 'touch options'... all the 'buttons' will be
 * mapped to the jill of the jungle equivalent actions." Answer to that first question: no, Jill had
 * no touch-control support of its own at all before this file -- ported here from the God of Thunder
 * Android port's own got_controls.c (itself adapted from Descore's own controls.c, see that file's
 * own top comment), not copied verbatim, for two real differences:
 *
 * 1. Jill's own gameplay action space is genuinely tiny -- a full JPLAYER.c read confirms exactly
 *    two real actions, JUMP (fire1/key_shift) and THROW (fire2/key_alt), contextually reinterpreted
 *    across Jill's player-transform states (normal/fish/frog/bird) but never any MORE than those two
 *    at once -- versus GoT's three (Fire/Magic/Select). So this file only needs a JUMP and a THROW
 *    button (plus PAUSE, same as GoT's own BTN_PAUSE) alongside the D-pad, not GoT's four-button
 *    FIRE/MAGIC/SELECT/PAUSE stack. wootbeer's own suggestion to use the Descent Android port as a
 *    model for "more buttons, since it has quite a few more functions than god of thunder" turned out
 *    to have nothing to actually port from -- that project's own Android tree has no touch-control
 *    implementation at all (only unrelated digitized-audio code) -- but it's also simply unnecessary
 *    here: even GoT's own already-larger 4-button scheme is more than Jill's real 2-action gameplay
 *    needs, so GoT's own precedent alone is already sufficient to model this on, exactly as wootbeer's
 *    own request led with ("the same one we had in god of thunder").
 *
 * 2. Key delivery is a direct jillhost_notify_key() (HOSTANDROID.H) call per button, not a write into
 *    a GoT-style `key_flag[100]` scancode array -- Jill has no such array; its own real input
 *    primitive is exactly this same jillhost_notify_key(key_code, down) call, the identical one
 *    JillView.java's own physical D-pad/gamepad path already makes (via jillKeyEvent(), see that
 *    file's own class comment) for the exact same key codes this file uses (KEYBOARD.H's own
 *    key_up/key_down/key_left/key_right/key_shift/key_alt/key_pause) -- confirmed correct by reading
 *    both ends of that path: JUNGLE.c's own play() calls checkctrl(1) every gameplay tick (pollflag=1
 *    live during real gameplay, not just menus), and GAMECTRL.c's checkctrl() reads key_shift/key_alt
 *    straight into fire1/fire2 while its own `dx1==0 && dy1==0 && pollflag` fallback reads
 *    keydown[1][scan_cursor*], itself populated every tick by KEYBOARD.c's k_status() from
 *    host_key_down(k_up)/host_key_down(k_down)/etc -- the exact same continuous-held state
 *    jillhost_notify_key() feeds. So a touch button calling jillhost_notify_key() directly drives
 *    movement/actions (in gameplay AND every domenu()-based menu, which reads the same dx1/dy1/
 *    fire1/fire2) exactly as if the same physical key were held on a real keyboard -- no separate
 *    "touch key_flag equivalent" array of its own was ever needed, which is actually simpler than
 *    GoT's own architecture, not just a scaled-down copy of it.
 *
 * Pre-release cleanup, this round -- wootbeer's own follow-up: "I do like the idea of having the
 * same base of touch support and gamepad support, is there any bit of it that makes sense to
 * include in descore, since we keep reusing it anyway?" Everything that WASN'T Jill's own button
 * geometry/action set -- the per-pointer touch dispatch state machine (including the "drag across
 * adjacent buttons without a spurious release/press stutter" logic), the gamepad-connected flag and
 * its "release anything held" transition guard, the Scaling/Opacity Touch Options infrastructure,
 * and the translucent-quad GL overlay draw -- has moved to descore/touch/descore_touch.c/.h (see
 * that file's own header comment for the full story). This file is now a thin adapter: Jill's own
 * button layout (layout_buttons() below) and JNI entry points, wired to that shared module through
 * jill_touch_key_fn()/jill_touch_tap_fn() and descore_touch_set_button()'s plain geometry+keys
 * arguments. What's NOT ported from got_controls.c at all: its own "Gamepad remapping" section
 * (Jill already has its own separate, earlier-built equivalent in jill_jni_bridge.c's own "Gamepad
 * remapping" section, unrelated to touch controls) and its demo-swallow branch in touchHandler()
 * (Jill has no comparable "abort a running demo on any touch" feature this needs to gate). */

#include "jill_controls.h"

#include "HOSTANDROID.H"
#include "HOSTSDL.H" /* host_show_keyboard()/host_hide_keyboard() -- BTN_KEYBOARD_TOGGLE below */
#include "KEYBOARD.H"
#include "../descore/touch/descore_touch.h"

#include <android/log.h>
#include <jni.h>
#include <stdbool.h>
#include <string.h>

#define LOG_TAG "JillControls"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#if defined(JILL_EP1)
#define NUM_BUTTONS 13
#else
#define NUM_BUTTONS 12
#endif

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_LEFT 2
#define BTN_RIGHT 3
#define BTN_UP_LEFT 4
#define BTN_UP_RIGHT 5
#define BTN_DOWN_LEFT 6
#define BTN_DOWN_RIGHT 7
#define BTN_JUMP 8   /* fire1/key_shift -- see this file's own top comment on why JUMP+THROW (not
                      * GoT's own FIRE/MAGIC/SELECT trio) is Jill's real full action-button set. */
#define BTN_THROW 9  /* fire2/key_alt */
#define BTN_PAUSE 10 /* key_pause -- opens/closes JUNGLE.c's pausemenu(), same role as GoT's own
                      * BTN_PAUSE (KEY_ESC there; Jill's own Start-button pause already uses
                      * key_pause specifically, not key_escape, see JillView.java's own K_PAUSE
                      * comment -- this button matches that, not GoT's ESC-based one). */
#if defined(JILL_EP1)
#define BTN_COIN_TOSS 11 /* key_space -- Android port addition, not upstream. wootbeer's own
                          * follow-up ask, once the D-pad/JUMP/THROW/PAUSE buttons above were in:
                          * "Episode 1's spacebar-bound coin-toss action, yes we need to add this
                          * to a touch button if we can't do it otherwise." Episode-1-only, same
                          * `#if defined(JILL_EP1)` gate real upstream's own coin-toss trigger
                          * (JPLAYER.c's `key == ' '` check) already has -- Episodes 2/3 have no
                          * coin-toss mechanic to give a button to, so this button (and the whole
                          * NUM_BUTTONS==12 layout below) simply doesn't exist in those two builds'
                          * own compiled jillhost2/jillhost3 .so. */
#define BTN_KEYBOARD_TOGGLE 12
#else
#define BTN_KEYBOARD_TOGGLE 11
#endif
/* Android port addition, not upstream, all episodes -- wootbeer's own follow-up ask, once the
 * Noisemaker screen (JUNGLE.c's own noisemaker()) was reachable with touch controls: "is there a
 * way to add a small opaque button ... in order to show/hide an android keyboard?" noisemaker()
 * plays a note for every one of ~50 QWERTY letter/number keys it reads directly (see that
 * function's own noise_keys[] table) -- there's no way to type any of them without a real or soft
 * keyboard, and Android never shows one for this SurfaceView on its own (same reason JUNGLE.c's
 * savegame() already needs host_show_keyboard()/host_hide_keyboard(), HOSTSDL.H's own comment has
 * that whole story). This button just calls those same two functions, toggling between them (see
 * jill_controls_toggle_keyboard() below) -- asked wootbeer whether Android has a built-in system
 * "keyboard" icon we could draw instead of a plain shape, specifically to avoid pulling in any new
 * image asset or font/icon library and whatever licensing that might carry; there isn't one this
 * project could rely on without doing exactly that (no keyboard glyph exists in the plain GLES1
 * quad-only renderer every other button already uses, and the modern Material "keyboard" icon is
 * an AndroidX/Material dependency, not anything bundled with the platform), so this stays a plain
 * translucent quad, same as every other button here -- position alone (learned once, same as the
 * D-pad/JUMP/THROW/PAUSE cluster already is) is how the player finds it, not a picture. Only
 * relevant during Noisemaker, so unlike every other button in this array it's a
 * descore_touch_set_button() `custom_visibility` button, driven by
 * jill_controls_set_noisemaker_active() below rather than the shared module's own ordinary
 * "hidden while a gamepad is connected" rule -- so it can never be mistaken for a real gameplay
 * button (or accidentally tapped) on any other screen, gamepad connected or not. It's also set up
 * with zero keys (a descore_touch tap-toggle button, not a held one) -- handle_down() over in
 * descore_touch.c special-cases this via jill_touch_tap_fn() below instead of pressing any key. */

static int screen_w = 0, screen_h = 0;
static bool controls_initialized = false;

bool jill_controls_gamepad_connected(void) {
    return descore_touch_gamepad_connected();
}

/* Lays out the 8 D-pad buttons as a 3x3 grid (corners = diagonals, edges = cardinals, center empty)
 * in the bottom-left corner of the real screen, plus JUMP (bottom-right corner, mirroring the D-pad's
 * size/bottom margin, exactly like got_controls.c's own FIRE) and THROW (directly above JUMP, same
 * column -- mirroring got_controls.c's own MAGIC-above-FIRE placement), plus PAUSE (top-right
 * corner, identical placement to got_controls.c's own BTN_PAUSE). Same cell-sizing formula
 * (fraction of the shorter screen dimension times the Scaling slider, floored at the shared
 * module's own descore_touch_min_cell_px()) and the same belt-and-suspenders on-screen clamp
 * got_controls.c's own layout_buttons() already established -- see that function's own comment
 * for the full geometry rationale, unchanged here. This function is Jill's own -- it's the one
 * piece of the touch system that genuinely differs per game (see this file's own top comment on
 * why that stays out of descore/touch/) -- everything it computes here is handed to the shared
 * module one button at a time via descore_touch_set_button() at the very end. */
static void layout_buttons(void) {
    typedef struct {
        float x, y, w, h;
        unsigned char keys[2];
        int num_keys;
        bool custom_visibility;
    } LocalButton;
    LocalButton local[NUM_BUTTONS];
    float cell, gap, origin_x, origin_y, min_cell;
    int row, col, i;
    int w = screen_w, h = screen_h;

    memset(local, 0, sizeof(local));

    cell = 0.11f * (float) (w < h ? w : h) * descore_touch_scale_value(descore_touch_scale_step());
    min_cell = descore_touch_min_cell_px();
    if (cell < min_cell) {
        cell = min_cell;
    }
    gap = cell * 0.08f;
    origin_x = cell * 0.6f; /* left margin */
    origin_y = (float) h - cell * 3.6f; /* bottom margin */

    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            int idx = -1;
            unsigned char k0 = 0, k1 = 0;
            int nk = 0;

            if (row == 0 && col == 0) { idx = BTN_UP_LEFT;    k0 = key_up;   k1 = key_left;  nk = 2; }
            else if (row == 0 && col == 1) { idx = BTN_UP;    k0 = key_up;                   nk = 1; }
            else if (row == 0 && col == 2) { idx = BTN_UP_RIGHT; k0 = key_up;  k1 = key_right; nk = 2; }
            else if (row == 1 && col == 0) { idx = BTN_LEFT;  k0 = key_left;                  nk = 1; }
            else if (row == 1 && col == 2) { idx = BTN_RIGHT; k0 = key_right;                 nk = 1; }
            else if (row == 2 && col == 0) { idx = BTN_DOWN_LEFT; k0 = key_down; k1 = key_left; nk = 2; }
            else if (row == 2 && col == 1) { idx = BTN_DOWN;  k0 = key_down;                  nk = 1; }
            else if (row == 2 && col == 2) { idx = BTN_DOWN_RIGHT; k0 = key_down; k1 = key_right; nk = 2; }
            else { continue; } /* center cell -- dead zone, no button */

            local[idx].x = origin_x + (float) col * cell + gap * 0.5f;
            local[idx].y = origin_y + (float) row * cell + gap * 0.5f;
            local[idx].w = cell - gap;
            local[idx].h = cell - gap;
            local[idx].keys[0] = k0;
            if (nk > 1) local[idx].keys[1] = k1;
            local[idx].num_keys = nk;
        }
    }

    /* JUMP: one cell, bottom-right corner, same size and bottom margin as the D-pad's own cells --
     * mirrors got_controls.c's own BTN_FIRE placement exactly (fire1/key_shift is Jill's own hammer-
     * throw-equivalent "primary action", same role FIRE plays for GoT's own Thor). */
    local[BTN_JUMP].x = (float) w - cell * 1.6f - (cell - gap);
    local[BTN_JUMP].y = origin_y + cell * 2.0f + gap * 0.5f;
    local[BTN_JUMP].w = cell - gap;
    local[BTN_JUMP].h = cell - gap;
    local[BTN_JUMP].keys[0] = key_shift;
    local[BTN_JUMP].num_keys = 1;

    /* THROW: one more cell directly above JUMP, same column -- a vertical pair in the bottom-right
     * corner mirroring the D-pad's own bottom-left cluster, same reasoning got_controls.c's own
     * BTN_MAGIC-above-BTN_FIRE placement already gives (a matched pair of action buttons, not a
     * hand-tuned final layout -- same starting-guess caveat this file's own top comment already
     * makes about JUMP's placement). */
    local[BTN_THROW].x = local[BTN_JUMP].x;
    local[BTN_THROW].y = local[BTN_JUMP].y - cell;
    local[BTN_THROW].w = cell - gap;
    local[BTN_THROW].h = cell - gap;
    local[BTN_THROW].keys[0] = key_alt;
    local[BTN_THROW].num_keys = 1;

#if defined(JILL_EP1)
    /* COIN TOSS (Episode 1 only): one more cell directly above THROW, same column -- extends the
     * JUMP/THROW vertical pair into a 3-cell stack, same reasoning got_controls.c's own
     * BTN_SELECT-above-BTN_MAGIC placement already gives for its own third action button. See
     * BTN_COIN_TOSS's own #define comment for the feature this drives. */
    local[BTN_COIN_TOSS].x = local[BTN_THROW].x;
    local[BTN_COIN_TOSS].y = local[BTN_THROW].y - cell;
    local[BTN_COIN_TOSS].w = cell - gap;
    local[BTN_COIN_TOSS].h = cell - gap;
    local[BTN_COIN_TOSS].keys[0] = key_space;
    local[BTN_COIN_TOSS].num_keys = 1;
#endif

    /* PAUSE: top-right corner, well clear of the D-pad/JUMP/THROW cluster down in the bottom two
     * corners -- identical placement to got_controls.c's own BTN_PAUSE (smaller cell than the
     * movement buttons since it's tapped far less often). */
    local[BTN_PAUSE].w = cell * 0.7f;
    local[BTN_PAUSE].h = cell * 0.7f;
    local[BTN_PAUSE].x = (float) w - local[BTN_PAUSE].w - cell * 0.4f;
    local[BTN_PAUSE].y = cell * 0.4f;
    local[BTN_PAUSE].keys[0] = key_pause;
    local[BTN_PAUSE].num_keys = 1;

    /* KEYBOARD TOGGLE: top-left corner, mirroring PAUSE's own top-right size/margin exactly (same
     * cell*0.7 size, same cell*0.4 margin from both screen edges) -- keeps the two visually balanced
     * and, since PAUSE already claims the top-right, guarantees this can never overlap it. Always
     * kept up to date here regardless of episode/Noisemaker state (cheap, and simpler than a special
     * partial-layout path) -- see BTN_KEYBOARD_TOGGLE's own #define comment for why it's only ever
     * actually drawn/hit-tested while Noisemaker is active, so having real coordinates ready here the
     * moment jill_controls_set_noisemaker_active(true) flips it on costs nothing. Zero keys (a
     * descore_touch tap-toggle button) and custom_visibility=true -- see this file's own top-of-file
     * #define comment for BTN_KEYBOARD_TOGGLE. */
    local[BTN_KEYBOARD_TOGGLE].w = cell * 0.7f;
    local[BTN_KEYBOARD_TOGGLE].h = cell * 0.7f;
    local[BTN_KEYBOARD_TOGGLE].x = cell * 0.4f;
    local[BTN_KEYBOARD_TOGGLE].y = cell * 0.4f;
    local[BTN_KEYBOARD_TOGGLE].num_keys = 0;
    local[BTN_KEYBOARD_TOGGLE].custom_visibility = true;

    /* Belt-and-suspenders safety clamp, same precedent got_controls.c's own layout_buttons()
     * already established -- every button above is already anchored with a margin comfortably
     * inside the screen at any scale from 0.90x-1.80x, so this shouldn't ever actually trigger, but
     * cheap insurance against a button silently clipping off-screen on an unusual aspect ratio. */
    for (i = 0; i < NUM_BUTTONS; ++i) {
        if (local[i].x < 0.0f) {
            local[i].x = 0.0f;
        }
        if (local[i].y < 0.0f) {
            local[i].y = 0.0f;
        }
        if (local[i].x + local[i].w > (float) w) {
            local[i].x = (float) w - local[i].w;
        }
        if (local[i].y + local[i].h > (float) h) {
            local[i].y = (float) h - local[i].h;
        }
    }

    for (i = 0; i < NUM_BUTTONS; ++i) {
        descore_touch_set_button(i, local[i].x, local[i].y, local[i].w, local[i].h,
                                  local[i].keys, local[i].num_keys, local[i].custom_visibility);
    }

    LOGI("layout_buttons: %dx%d screen, scale=%.2fx, cell=%.1fpx, pad origin (%.1f,%.1f)", w, h,
         descore_touch_scale_value(descore_touch_scale_step()), cell, origin_x, origin_y);
}

/* Key-delivery callback handed to descore_touch_init() below -- see this file's own top comment,
 * point 2, for why a direct jillhost_notify_key() call is Jill's own real input primitive. */
static void jill_touch_key_fn(unsigned char key, int down) {
    jillhost_notify_key(key, down);
}

/* Tap-toggle callback handed to descore_touch_init() below -- the only zero-key button Jill has
 * today is BTN_KEYBOARD_TOGGLE (see that #define's own comment), so this has just the one case,
 * but stays a switch (not an `if`) so a second tap-toggle button, if this project ever adds one,
 * is an obvious place to extend rather than a new special case elsewhere. */
static void jill_touch_tap_fn(int button_index) {
    switch (button_index) {
        case BTN_KEYBOARD_TOGGLE:
            jill_controls_toggle_keyboard();
            break;
        default:
            break;
    }
}

void jill_controls_init(int w, int h) {
    screen_w = w;
    screen_h = h;

    descore_touch_init(NUM_BUTTONS, jill_touch_key_fn, jill_touch_tap_fn);
    descore_touch_set_screen_size(w, h);
    layout_buttons();

    controls_initialized = true;
}

bool jill_controls_touch_enabled(void) {
    return controls_initialized && !descore_touch_gamepad_connected();
}

void jill_controls_set_scale_step(int step) {
    descore_touch_set_scale_step(step);
    if (controls_initialized) {
        layout_buttons(); /* re-lay-out immediately, same "moves the slider, sees it move" shape
                            * every other live-adjusted menu slider in this project already has. */
    }
}

float jill_controls_touch_scale_value(int step) {
    return descore_touch_scale_value(step);
}

void jill_controls_set_opacity(float alpha) {
    descore_touch_set_opacity(alpha); /* clamped to [DESCORE_TOUCH_OPACITY_DEFAULT, 1.0] there --
                                        * same floor got_controls_set_opacity() established against
                                        * got_menu.c's own TOUCH_OPACITY_MIN_ALPHA, now shared. */
}

void jill_controls_set_display_density(float density) {
    descore_touch_set_display_density(density);
}

/* BTN_KEYBOARD_TOGGLE's own on/off state -- native has no way to ask Android whether the real soft
 * keyboard is currently on screen, so this is the single source of truth for what this feature
 * thinks it last asked for. Kept in sync with reality by construction: the only two places that ever
 * change it are toggle_keyboard() (a real tap) and jill_controls_set_noisemaker_active() forcing it
 * back to false -- and therefore the real keyboard hidden -- on the way out of Noisemaker, so a
 * player can never leave the screen with the keyboard silently still up. */
static bool s_keyboard_shown = false;

/* True only while JUNGLE.c's own noisemaker() is actually running -- see
 * jill_controls_set_noisemaker_active() below. */
static bool s_noisemaker_active = false;

/* jill_touch_tap_fn()'s own special case for BTN_KEYBOARD_TOGGLE above -- a single tap flips
 * s_keyboard_shown and asks Android to show or hide the real soft keyboard accordingly (HOSTSDL.H's
 * own host_show_keyboard()/host_hide_keyboard(), the exact same pair JUNGLE.c's savegame() already
 * brackets its own winput() call with). Deliberately fires once per tap-down, not a held/continuous
 * action like the movement or JUMP/THROW buttons -- there's nothing to "release" for a toggle. Not
 * static (declared in jill_controls.h) -- JUNGLE.c's own noisemaker() calls this same function
 * directly for the gamepad Select button now too (see jill_controls.h's own comment for that whole
 * story), so touch and gamepad drive the exact same toggle, never two copies of this logic to keep
 * in sync. */
void jill_controls_toggle_keyboard(void) {
    s_keyboard_shown = !s_keyboard_shown;
    if (s_keyboard_shown) {
        host_show_keyboard();
    } else {
        host_hide_keyboard();
    }
}

/* Called from JUNGLE.c's own noisemaker(): true right when it's entered, false right before it
 * returns -- see jill_controls.h's own comment for the full call-site contract (including why the
 * caller also needs one gr_present_page() call right after the true call, to actually get
 * BTN_KEYBOARD_TOGGLE on screen). Going true -> false also force-hides the real keyboard and resets
 * s_keyboard_shown back to false if the player left it toggled on -- same "clean up anything left
 * held/shown on the way out" transition guard descore_touch_set_gamepad_connected() gives the
 * D-pad/JUMP/THROW buttons, just for the soft keyboard instead of a jillhost_notify_key(). Pushes
 * the new state straight through to the shared module's own descore_touch_set_button_visible() --
 * BTN_KEYBOARD_TOGGLE is a custom_visibility button precisely so this is the only thing that ever
 * controls whether it's drawn/hit-tested, gamepad connected or not (see that button's own #define
 * comment). */
void jill_controls_set_noisemaker_active(bool active) {
    if (!active && s_keyboard_shown) {
        host_hide_keyboard();
        s_keyboard_shown = false;
    }
    s_noisemaker_active = active;
    descore_touch_set_button_visible(BTN_KEYBOARD_TOGGLE, active);
}

/* Forwards raw multi-touch pointer events from JillView.java's own onTouchEvent() into the shared
 * descore_touch module -- same per-pointer-index dispatch got_controls.c's own touchHandler()
 * already established, minus that function's own demo-swallow branch (see this file's own top
 * comment for why Jill has no comparable gate to apply here). No renderScale conversion needed,
 * same reasoning got_controls.c's own comment already gives: JillView (like GotView) draws its
 * touch buttons directly in real screen pixels (see descore_touch_draw(), called from
 * jill_draw_touch_buttons() below), so raw event coordinates already line up with what
 * descore_touch_handle() hit-tests against. */
JNIEXPORT jboolean JNICALL
Java_wootbeer_jillandroid_JillView_jillTouchHandler(JNIEnv *env, jclass type, jint action,
        jint pointer_id, jfloat x, jfloat y, jfloat prev_x, jfloat prev_y) {
    (void) env;
    (void) type;
    (void) prev_x;
    (void) prev_y; /* unlike Descore's own touchHandler(), nothing here needs the previous position */

    if (!controls_initialized) {
        return JNI_FALSE;
    }
    return descore_touch_handle((int) action, (int) pointer_id, (float) x, (float) y)
               ? JNI_TRUE : JNI_FALSE;
}

/* Called from JillView.java's InputManager.InputDeviceListener (mirroring GotView.java's own
 * identical wiring -- see that class's own isGamepad()/onInputDeviceAdded()/onInputDeviceRemoved()/
 * onInputDeviceChanged() comments) whenever a gamepad-class InputDevice connects/disconnects. Just
 * relays into the shared module -- see descore_touch_set_gamepad_connected()'s own comment for the
 * "release anything the touch D-pad was holding down" transition-guard it applies. */
JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillSetGamepadConnected(JNIEnv *env, jclass type, jboolean connected) {
    (void) env;
    (void) type;
    descore_touch_set_gamepad_connected((bool) connected);
}

/* One-time real display density, forwarded from JillView.java's constructor -- see
 * jill_controls_set_display_density()'s own header comment for why this (not a got_dp_to_px()-style
 * JNI round-trip) is this port's own equivalent. */
JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillSetDisplayDensity(JNIEnv *env, jclass type, jfloat density) {
    (void) env;
    (void) type;
    jill_controls_set_display_density(density);
}

/* Drawn as a separate screen-space GL overlay on top of whatever this frame already drew -- see
 * HOSTANDROID.c's own host_present() for exactly where this is called from (right before its own
 * eglSwapBuffers()). Just relays into the shared module -- see descore_touch_draw()'s own comment
 * for the actual drawing and the black-screen bug fix it carries. */
void jill_draw_touch_buttons(void) {
    if (!controls_initialized) {
        return;
    }
    descore_touch_draw();
}

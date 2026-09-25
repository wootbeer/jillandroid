/* descore_touch.h -- generic on-screen touch-button input + gamepad-connected tracking, shared
 * across any "descore"-shell Android port (Jill of the Jungle, God of Thunder, Descent, and
 * whatever comes after). Pulled out of Jill's own jill_controls.c during this project's
 * pre-release cleanup, at wootbeer's own request: "I do like the idea of having the same base of
 * touch support and gamepad support, is there any bit of it that makes sense to include in
 * descore, since we keep reusing it anyway?" jill_controls.c itself was ported from the God of
 * Thunder Android port's own got_controls.c (see that file's own header comment) -- this is the
 * generalization of the mechanism the two already shared by hand-copying, not a new design.
 *
 * What lives here (mechanism, action-set-agnostic): per-pointer touch dispatch (press/release/
 * drag-across-buttons without a spurious release-then-press stutter), the gamepad-connected flag
 * and its "release anything the touch UI was holding" transition guard, the Scaling/Opacity Touch
 * Options slider infrastructure (including the shared 9-step scale table and opacity floor -- the
 * exact numbers this project already settled on twice, see kDescoreTouchScaleValues's own comment
 * below), and the translucent-quad GL draw routine.
 *
 * What deliberately does NOT live here, per wootbeer's own explicit ask ("don't unify the
 * controls action sets, since every game could be vastly different"): button geometry/layout,
 * how many buttons exist, which key(s) each one sends, and the game's own key-delivery primitive.
 * Every one of those stays in each game's own controls file (jill_controls.c's own
 * layout_buttons(), Jill's JUMP/THROW/PAUSE/COIN_TOSS/KEYBOARD_TOGGLE button set) and is wired to
 * this module only through descore_touch_init()'s callbacks and descore_touch_set_button()'s
 * plain geometry+keys arguments -- this module never assumes any particular action set.
 *
 * A note for the OTHER descore-shell ports (God of Thunder, Descent): this file only updates
 * Jill's own local copy of descore/. For the touch mechanism to actually become shared in
 * practice, this same file needs to be carried over into those projects' own descore/ trees too
 * (and their own got_controls.c/equivalent thinned down to a layout-only adapter, the same way
 * jill_controls.c was here) -- that's real work in each of those repos, not automatic just because
 * the file exists here now. */

#ifndef DESCORE_TOUCH_H
#define DESCORE_TOUCH_H

#include <stdbool.h>

#define DESCORE_TOUCH_MAX_BUTTONS 32
#define DESCORE_TOUCH_MAX_KEYS_PER_BUTTON 2
#define DESCORE_TOUCH_MAX_POINTERS 16

/* Android MotionEvent action constants -- same subset every descore-shell port's own touch
 * dispatcher already hardcoded by hand (avoids pulling in android/input.h just for these 5
 * values). Passed straight through from the game's own JNI touch-event entry point. */
#define DESCORE_TOUCH_ACTION_DOWN 0
#define DESCORE_TOUCH_ACTION_UP 1
#define DESCORE_TOUCH_ACTION_MOVE 2
#define DESCORE_TOUCH_ACTION_POINTER_DOWN 5
#define DESCORE_TOUCH_ACTION_POINTER_UP 6

/* Called once per key for a button with num_keys > 0 -- `down` is 1 on press, 0 on release. The
 * game supplies whatever its own real input primitive is (Jill: a direct jillhost_notify_key()
 * call; a GoT-style port: a key_flag[] array write) -- this module has no opinion on what a "key"
 * actually is beyond the unsigned char it was handed in descore_touch_set_button(). */
typedef void (*DescoreTouchKeyFn)(unsigned char key, int down);

/* Called instead of DescoreTouchKeyFn for a button set up with num_keys == 0 -- a discrete
 * tap-to-toggle button (Jill's own on-screen soft-keyboard toggle during the Noisemaker screen is
 * the first real example) rather than a held movement/action button. Fires once per tap-down;
 * there is nothing to "release" for a toggle, so this is never called again for the matching
 * touch-up. May be NULL if a port has no such buttons. */
typedef void (*DescoreTouchTapFn)(int button_index);

/* One-time setup -- `num_buttons` must be <= DESCORE_TOUCH_MAX_BUTTONS. `key_fn` must not be
 * NULL; `tap_fn` may be NULL if this port has no discrete tap-toggle buttons. */
void descore_touch_init(int num_buttons, DescoreTouchKeyFn key_fn, DescoreTouchTapFn tap_fn);

/* Real screen size in pixels -- needed for descore_touch_draw()'s own orthographic projection.
 * The game's own layout function (Jill: layout_buttons()) computes button geometry against these
 * same dimensions itself; this module just needs them again for the draw call's glOrthof(). */
void descore_touch_set_screen_size(int width, int height);

/* Real display density (Android's `density` -- 1.0 at the historical 160dpi baseline), forwarded
 * once from Java before the render thread ever runs. Backs descore_touch_min_cell_px() below;
 * 1.0f is the harmless default if this is ever somehow skipped (same reasoning jill_controls.c's
 * own previous s_display_density comment gave: simply the historical-baseline floor size, not a
 * crash or a divide-by-zero). */
void descore_touch_set_display_density(float density);

/* Minimum absolute touch-target size, in real pixels (TOUCH_MIN_CELL_DP * density) -- the floor a
 * game's own layout function should clamp its computed cell size to, so a fraction-of-screen cell
 * size can never shrink below a comfortable physical touch target on an unusually small/dense
 * screen combined with a low Scaling setting. Same 44dp floor and rationale got_controls.c's own
 * TOUCH_MIN_CELL_DP already established, ported here verbatim rather than re-derived. */
float descore_touch_min_cell_px(void);

/* Defines (or redefines) button `index`'s geometry and the key(s) it sends. `keys`/`num_keys` may
 * be NULL/0 for a discrete tap-toggle button (see DescoreTouchTapFn above) -- handle_down() will
 * call the tap callback instead of pressing any key for it. `custom_visibility` marks a button
 * whose visible/hidden state the game manages itself via descore_touch_set_button_visible()
 * (Jill's keyboard toggle: visible only while its own Noisemaker screen is active, gamepad
 * connected or not) rather than following the ordinary "hidden while a gamepad is connected" rule
 * every other button gets automatically from descore_touch_set_gamepad_connected() below. */
void descore_touch_set_button(int index, float x, float y, float w, float h,
                               const unsigned char *keys, int num_keys, bool custom_visibility);

/* For a custom_visibility button only (see descore_touch_set_button() above) -- sets whether it's
 * currently drawn/hit-tested. Harmless to call on an ordinary button too, but pointless: the next
 * descore_touch_set_gamepad_connected() call will overwrite it back to the standard rule. */
void descore_touch_set_button_visible(int index, bool visible);

/* Forwards one raw multi-touch pointer event (from the game's own JNI touch-event entry point).
 * Returns true if this module consumed it (there could possibly be a visible button right now),
 * matching the boolean every descore-shell port's own jillTouchHandler()-equivalent already
 * returns to Java. */
bool descore_touch_handle(int action, int pointer_id, float x, float y);

/* Whenever a gamepad-class InputDevice connects/disconnects (the game's own Java InputManager
 * listener calls through to this). Releases any key(s) the touch UI was currently holding down
 * through DescoreTouchKeyFn (a finger left resting on screen when a gamepad takes over shouldn't
 * leave a direction/action stuck held), then updates every button's own visible flag: hidden while
 * connected, shown while not -- except any button marked custom_visibility above, which this call
 * never touches. */
void descore_touch_set_gamepad_connected(bool connected);
bool descore_touch_gamepad_connected(void);

/* Touch Options "Scaling" slider -- 9 fixed steps, same asymmetric range got_controls.c's own
 * kTouchScaleValues[] already established: touch targets are already comfortable at the default,
 * so only 2 steps go down from 1.00x but 6 go up, to 1.80x. Values shared verbatim, not retuned --
 * this project has already settled on these numbers twice (Jill and GoT both), no reason for a
 * third set. Index 2 (1.00x) is the default step -- a fresh install, or an existing config file
 * with no saved scale yet, looks identical to before this feature shipped. */
#define DESCORE_TOUCH_SCALE_NUM_STEPS 9
#define DESCORE_TOUCH_SCALE_DEFAULT_STEP 2
extern const float kDescoreTouchScaleValues[DESCORE_TOUCH_SCALE_NUM_STEPS];

void descore_touch_set_scale_step(int step); /* clamped to [0, DESCORE_TOUCH_SCALE_NUM_STEPS-1] */
int descore_touch_scale_step(void);
float descore_touch_scale_value(int step); /* clamped the same way; kDescoreTouchScaleValues[step] */

/* Touch Options "Opacity" slider -- same default/floor value got_controls.c's own
 * TOUCH_OPACITY_DEFAULT already established (that file's own comment on making sure the minimum
 * opacity is what the previous, fixed-opacity default already was, so a player adjusting this for
 * the first time never loses track of the controls entirely). */
#define DESCORE_TOUCH_OPACITY_DEFAULT 0.30f
void descore_touch_set_opacity(float alpha); /* clamped to [DESCORE_TOUCH_OPACITY_DEFAULT, 1.0] */
float descore_touch_opacity(void);

/* Draws every currently-visible button as a simple flat translucent gray quad (no textures) --
 * same level of visual polish got_draw_touch_buttons() already established. Called as a separate
 * screen-space GL overlay on top of whatever the game's own frame already drew, right before the
 * game's own eglSwapBuffers(). Restores GL_PROJECTION/GL_MODELVIEW to identity before returning --
 * see descore_touch.c's own draw function comment for the black-screen bug this fixes, still
 * present and unfixed in got_draw_touch_buttons()'s own original copy as of this writing. */
void descore_touch_draw(void);

#endif

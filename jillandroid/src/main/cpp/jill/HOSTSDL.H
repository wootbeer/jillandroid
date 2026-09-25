/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code ("Jill of the Jungle Reconstructed").

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#ifndef JILL_HOSTSDL_H
#define JILL_HOSTSDL_H

#include <stdint.h>
#include "RECOVERY.H"

int host_open(const char *title, int scale);
void host_close(void);
int host_is_open(void);
int host_pump(void);
void host_present(const byte *pixels, const byte *palette);
void host_set_title(const char *title);
int host_key_pressed(void);
int host_peek_key(void);
int host_read_key(void);
int host_key_down(int key_code);
void host_clear_keys(void);
void host_console_clear(void);
void host_sleep(unsigned milliseconds);
longword host_coreleft(void);
uint32_t host_ticks(void);
void host_start_clock(void);
void host_stop_clock(void);

/* Android port addition, not upstream -- this header is otherwise vendored unchanged (the real
 * decomp's own host abstraction). Added for real save-name text entry: WIN.c's winput()/wgetkey()
 * (unchanged, see that file's own header comment) read typed characters through this same host_*
 * boundary (KEYBOARD.c's k_read() -> host_read_key(), fed by whatever key events actually arrive),
 * which works fine for a real DOS keyboard or a desktop SDL window, but Android has no on-screen
 * keyboard visible by default -- nothing was ever telling the platform one was wanted, so no
 * keystrokes ever arrived and JUNGLE.c's savegame() (the only real caller, see its own call site)
 * would just wait forever. host_show_keyboard()/host_hide_keyboard() bracket that one winput()
 * call so whichever host backend needs to do something for this (Android: pop/dismiss the real
 * soft keyboard, see HOSTANDROID.c's own implementation; a hypothetical desktop SDL build could
 * safely no-op both, since a real keyboard is already always "shown") gets a clear, narrow signal
 * for exactly when text entry starts and ends, without WIN.c's own byte-identical winput()/
 * wgetkey() needing to know anything about it. */
void host_show_keyboard(void);
void host_hide_keyboard(void);

/* Android port addition, not upstream -- same "this header is otherwise vendored unchanged"
 * exception host_show_keyboard()/host_hide_keyboard() above already are. Called from JUNGLE.c's
 * own jill_apply_display_mode() (see that function's and jill_display_mode's own comments)
 * whenever the pause menu's DISPLAY MODE row cycles: mode is one of JUNGLE.c's own
 * JILL_DISPLAY_DEFAULT/JILL_DISPLAY_FORCE43/JILL_DISPLAY_MINIMAL values (0/1/2). Only
 * HOSTANDROID.c's own implementation does anything with it (pillarboxing host_present()'s output
 * for Force 4:3, see that function's own comment) -- Default and Minimal UI don't need any
 * presentation-layer change, JUNGLE.c already draws everything they need into the same fixed
 * 320x200 buffer this file already knows how to present. A hypothetical desktop SDL build could
 * safely no-op this, the same way it could host_show_keyboard()/host_hide_keyboard() above. */
void host_set_display_mode(int mode);

/* Android port addition, not upstream -- see JILL.H's own jill_mirror_enabled comment for the whole
 * Mirror Mode feature. Three separate calls, not one, mirroring host_set_display_mode()'s own
 * "just latch a value for host_present() to read on its next call" shape:
 *
 * host_set_mirror_enabled(enabled) -- the player's own ON/OFF choice (pausemenu()'s own MIRROR row,
 * JUNGLE.c), 1=flip, 0=don't.
 *
 * host_set_mirror_rect(x1, y1, x2, y2) -- the screen-space rectangle host_present() flips pixels
 * within, in the same fixed 320x200 logical coordinates every other viewport in this codebase
 * already uses (vptype's own vpx/vpy/vpxl/vpyl -- x1/y1 inclusive, x2/y2 exclusive, i.e. vpx/vpy and
 * vpx+vpxl/vpy+vpyl straight from *gamevp). Pushed from JUNGLE.c's own jill_activate_display_mode()
 * every time it finishes establishing *gamevp, so this rect can never drift out of sync with
 * whichever display mode (Default/Force 4:3/Minimal UI) is actually active -- deliberately NOT
 * CONTROLS/INVENTORY's own side panel or botvp's own bottom message strip, so text/menus/GUI are
 * never mirrored, exactly wootbeer's own ask.
 *
 * host_set_mirror_suspended(suspended) -- a full, temporary override (1=treat as OFF regardless of
 * host_set_mirror_enabled()'s own value). Originally set/cleared around askquit()/pausemenu() (and
 * anything reachable from the latter) for as long as either was on screen, since both draw their
 * own text directly into the same shared page buffer gamevp's own world pixels live in -- simply
 * not mirroring anything at all for the single frozen frame either blocking call was on screen was
 * meant as a simplification, avoiding the need for a separate "menu layer" to exclude the way GoT's
 * own per-frame exclude-rect can. wootbeer's own bug report found the real cost of that simplification:
 * "when entering pause menu while mirror mode is on, everything flips back while the menu is open.
 * and then once it closes there's some mirrored artifacts and such leftover" -- suspending
 * un-mirrors the WHOLE screen, not just the menu's own small box, so the frozen world visible
 * around/behind it visibly flips back to its true orientation for as long as it's up. Every real
 * call site has since been replaced with domenu_at()/dotextmsg()/loadsavewin()/
 * jill_gamepad_capture_button()/savegame() each pushing their own transient host_set_mirror_
 * exclude_rect() instead (that function's own comment above has the whole per-window-exclusion
 * story; see play()'s own key_escape/'Q'/key_pause cases, JUNGLE.c, for the removed suspend calls),
 * so nothing in this codebase calls host_set_mirror_suspended() any more -- kept here, not removed,
 * as working, tested infrastructure in case some future screen genuinely can't be given its own
 * exclude rect (e.g. something that isn't a simple rectangular window). Only HOSTANDROID.c's own
 * host_present() implementation does anything with any of these three -- a hypothetical desktop SDL
 * build could safely no-op all of them, the same way it could host_set_display_mode() above. */
void host_set_mirror_enabled(int enabled);
void host_set_mirror_rect(int x1, int y1, int x2, int y2);
void host_set_mirror_suspended(int suspended);

/* Android port history, not upstream -- a SECOND, smaller exclude rect nested inside the main
 * mirror rect above, currently used by the pause-menu-family functions in JUNGLE.c (domenu_at(),
 * dotextmsg(), loadsavewin(), jill_gamepad_capture_button(), savegame()) and by putlevelmsg()'s own
 * levelwin, each scoped to its own window's bounds for as long as that window is on screen. This
 * mechanism has a known, still-open limitation for large exclude rects: excluding a destination
 * region stops THOSE columns from mirroring, but does nothing to stop OTHER, still-mirrored columns
 * elsewhere in gamevp from reading FROM that same region as THEIR OWN mirror source, reproducing
 * whatever's there a second time, backwards -- wootbeer's own report for the pause menu specifically:
 * "when you open the pause menu with mirror mode turned on, and in the default... display mode
 * setting, a fourth of the menu is 'duplicated' and mirrored on the right side." Not yet fixed.
 *
 * Originally also used by JOBJ2.c's own msg_mapdemo() (the "MAP"/"DEMO" banner sign pinned near the
 * top-left of gamevp), pushed fresh every tick it drew, for the exact same reason -- and it hit the
 * exact same shadow-duplication bug once Minimal UI's own HUD strip started sharing that corner and
 * made it obvious ("the large blue text at the top of the screen that says MAP, is duplicated in
 * both corners of the screen"). A pixel pre-mirror fix (the same technique that fixed the Minimal UI
 * HUD strip, jill_draw_minimal_hud(), JUNGLE.c) was tried there and reverted -- it relies on nothing
 * else drawing over either position for the rest of the tick, a guarantee jill_draw_minimal_hud()
 * gets for free (deliberately the LAST thing drawn into its own strip every tick) but msg_mapdemo()
 * does not (just one object among many in the normal per-tick draw loop), and the attempt visibly
 * broke ("that whole strip of the screen is messed up again, and now the MAP world banner is just
 * completely gone"). Rather than keep fighting that, wootbeer's own call: "can we just remove the map
 * banner entirely while mirror mode is on?... that way that whole strip of the screen can look
 * properly mirrored and unaffected by any bugs." msg_mapdemo() now simply doesn't draw that banner
 * at all while Mirror Mode is on (see its own comment, JOBJ2.c) -- so it no longer calls this
 * function except when Mirror Mode is off (where mirroring, and therefore this exclude rect, is
 * moot anyway). If the pause-menu duplication bug above is ever tackled for real, doing that same
 * pre-mirror transform INSIDE host_present() itself, at actual flip time, reading straight from
 * whatever's already in the buffer for this exclude rect, would sidestep msg_mapdemo()'s own
 * draw-order problem entirely (host_present() runs strictly after all per-tick drawing is done) --
 * worth remembering if that's ever attempted, though simply hiding the pause menu's own overflowing
 * quarter the way msg_mapdemo() now hides itself is not an option there (unlike a background banner,
 * the menu is what the player is actively trying to read).
 *
 * x1/y1/x2/y2 use the exact same absolute-screen-space convention host_set_mirror_rect() does
 * (x1/y1 inclusive, x2/y2 exclusive). active=0 disables it (the common case -- most of gamevp has
 * nothing to exclude), and JUNGLE.c resets it to inactive at every point it also pushes a fresh
 * host_set_mirror_rect() (jill_activate_display_mode()). Only HOSTANDROID.c's own host_present()
 * implementation does anything with it -- a hypothetical desktop SDL build could safely no-op it,
 * the same way it could host_set_display_mode() above. */
void host_set_mirror_exclude_rect(int active, int x1, int y1, int x2, int y2);

/* Android port addition, not upstream -- shared between JUNGLE.c (jill_hudvp's own pixel height,
 * see that viewport's own comment) and HOSTANDROID.c (the translucent backing bar host_present()
 * draws behind it in Minimal UI mode, see that function's own comment) so the two can't drift out
 * of sync -- both already include this header. In pixel rows, out of the fixed 320x200 buffer
 * both files already agree on. */
#define JILL_MINIMAL_HUD_HEIGHT 30

/* Android port history, not upstream -- a THIRD, fully independent exclude rect, separate from
 * host_set_mirror_exclude_rect() above. Originally dedicated to jill_hudvp (the Minimal UI HUD
 * strip, JILL_MINIMAL_HUD_HEIGHT rows tall), pushed active for as long as JILL_DISPLAY_MINIMAL mode
 * itself was active, since that mode expands *gamevp to cover the entire screen (so level art
 * visibly scrolls underneath the HUD strip, per wootbeer's own request), putting jill_hudvp's own HUD
 * content inside the main mirror rect along with it. That approach -- excluding the HUD's own
 * destination columns from being mirrored -- turned out to be fundamentally unable to fix the
 * problem it was for: excluding a destination region stops THOSE columns from mirroring, but does
 * nothing to stop OTHER, still-mirrored columns elsewhere in the strip from reading FROM that same
 * source region as THEIR OWN mirror source, reproducing the HUD a second time, backwards, wherever
 * that lookup landed (wootbeer's own report, after the width was tightened to fix an earlier over-
 * exclusion bug: "now the minimal ui is mirrored on both sides of the screen"). JUNGLE.c's
 * jill_draw_minimal_hud() now fixes the HUD's own mirror behavior a different way entirely --
 * pre-writing an already-flipped copy of itself directly into the pixel buffer so host_present()'s
 * own uniform, unexclusion whole-strip mirror pass undoes it back to normal (that function's own
 * comment has the mechanics) -- so nothing in this codebase calls this function any more; JUNGLE.c's
 * jill_activate_display_mode() explicitly pushes it inactive on every display-mode change instead.
 * Kept here, not removed, as working, tested infrastructure for a future screen that might still
 * need a real destination-side mirror exclusion (the same reasoning host_set_mirror_suspended()'s
 * own comment gives for staying in place unused). x1/y1/x2/y2 use the same absolute-screen-space
 * convention (x1/y1 inclusive, x2/y2 exclusive) as the other two; active=0 disables it. */
void host_set_mirror_hud_exclude_rect(int active, int x1, int y1, int x2, int y2);

#endif

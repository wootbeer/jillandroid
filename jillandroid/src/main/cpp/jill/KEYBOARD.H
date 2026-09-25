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

#ifndef JILL_KEYBOARD_H
#define JILL_KEYBOARD_H

#include "RECOVERY.H"

/* Values returned by the original BIOS-backed k_read(). */
enum {
    enter = 13,
    esc = 27,
    escape = 27,
    k_tab = 9,
    k_bs = 8,
    k_up = 200,
    k_down = 208,
    k_left = 203,
    k_right = 205,
    k_pgup = 201,
    k_pgdown = 209,
    k_f1 = 187,
    k_f2 = 188,
    k_f3 = 189,
    k_f4 = 190,
    k_f5 = 191,
    k_f6 = 192,
    k_f7 = 193,
    k_f8 = 194,
    k_f9 = 195,
    k_f10 = 196
};

/* Existing recovered modules use these descriptive aliases. */
#define key_escape escape
#define key_enter enter
#define key_space 32
#define key_up k_up
#define key_down k_down
#define key_left k_left
#define key_right k_right
#define key_pgup k_pgup
#define key_pgdown k_pgdown
#define key_ctrl 0x11
#define key_alt 0x12
#define key_shift 0x10
/* Android port addition, not upstream -- same style as key_shift/key_alt above (an Android
 * gamepad signal with no DOS keyboard equivalent, given the next free code in that same 0x10-
 * 0x12 block). JillView.java forwards the gamepad Start button to this (see that file's own
 * comment); JUNGLE.c's play() reads it in its own key switch to open pausemenu() -- see that
 * function's own header comment for the whole feature. */
#define key_pause 0x13
/* Android port addition, not upstream -- next free code after key_pause above, same reasoning.
 * domenu() (JUNGLE.c) returns this from its own choice-acceptance loop when the player backs out
 * with the gamepad B button instead of picking a listed choice -- see that function's own comment
 * for why B means "back" there and not "confirm" (wootbeer's own explicit ask: on menus, A and Start
 * both confirm, B backs out one layer -- gameplay's own B-jumps/A-throws mapping is unchanged,
 * this only affects domenu()'s own menu-choice logic). Every domenu() caller that cares about the
 * difference between "the player picked something" and "the player backed out without picking
 * anything" checks for this against whatever key_table it passed in (which never contains it,
 * since key_table entries are always real, visible menu letters) -- see askquit()/pausemenu()'s
 * own comments for how each one handles it. */
#define key_back 0x14
/* Android port addition, not upstream -- "Remap Gamepad" submenu (JUNGLE.c's pausemenu(), JILL.H's
 * own JILL_GAMEPAD_ACTION_JUMP/_THROW comment has the whole feature). JUMP (fire1/key_shift) and
 * THROW (fire2/key_alt) become fully remappable to whichever physical gamepad button the player
 * picks -- but domenu_at()'s own confirm/back detection (JUNGLE.c) has ALWAYS read fire1/fire2
 * directly for "B backs out"/"A confirms" (see that function's own comment on the A-confirms/
 * B-backs-out convention), so a player who remaps BOTH JUMP and THROW away from A/B would
 * otherwise lose all ability to confirm or back out of any menu -- including the very Remap
 * Gamepad screen needed to undo the mistake. These two codes are a real, always-on menu-confirm/
 * menu-cancel signal, completely decoupled from the remap table -- JillView.java's own handleKey()
 * forwards physical A/B to these unconditionally, in ADDITION to whatever JUMP/THROW binding they
 * may (or may not) also currently hold (see that file's own comment), so menu navigation can never
 * be lost no matter how JUMP/THROW get remapped. Mirrors the God of Thunder Android port's own
 * KEY_CONFIRM/KEY_CANCEL (got_controls.c/got_menu.c) -- the same architecture wootbeer's own ask
 * ("model after the one we made for god of thunder") means this whole feature is built on.
 * GAMECTRL.c's own checkctrl()/checkctrl0() populate the continuous-held menu_confirm/menu_cancel
 * globals (GAMECTRL.H) from these exactly the same way fire1/fire2 are populated from key_shift/
 * key_alt just above; domenu_at() (JUNGLE.c) ORs them into its own existing fire1/fire2 checks.
 * Next free codes after key_back above. */
#define key_menu_confirm 0x15
#define key_menu_cancel 0x16
/* Android port addition, not upstream -- next free code after key_menu_cancel above, same
 * reasoning as key_pause/key_back/key_menu_confirm/key_menu_cancel all had for their own additions.
 * wootbeer's own explicit ask, after finding the Noisemaker screen (JUNGLE.c's own noisemaker()) had no
 * way at all to exit once entered without a physical keyboard: "can we bind 'start' and 'select' on
 * gamepad to exit back to the main menu." Start already reaches noisemaker() once that function's
 * own exit condition also checks key_pause (see that function's own comment) -- key_select is a new,
 * dedicated code purely for Select, forwarded unconditionally by JillView.java's own handleKey()
 * (KEYCODE_BUTTON_SELECT), the same "always-on, single clear job" shape every code in this block
 * already has. Deliberately its own code rather than also being funneled into key_pause: Select was
 * never bound to anything before this, and giving it a dedicated code means it only ever does
 * exactly what a caller explicitly checks it for instead of silently also opening the real pause
 * menu (JUNGLE.c's play(), which checks key_pause) anywhere real gameplay happens to read key_pause.
 * Select's own meaning changed once, right after it first shipped -- wootbeer's own immediate follow-up:
 * "with gamepad enabled can we also make select open and close the android keyboard instead of
 * closing from noisemaker?" noisemaker() no longer treats key_select as an exit at all; it now calls
 * jill_controls_toggle_keyboard() instead (see that function's own comment), so Select opens/closes
 * the real Android soft keyboard for a gamepad player, mirroring the on-screen KEYBOARD TOGGLE touch
 * button's own action -- Start (key_pause) remains the only gamepad exit. key_select stays the
 * dedicated code either way, still checked only in that one call site. */
#define key_select 0x17
#define key_f1 k_f1
#define key_f2 k_f2
#define key_f3 k_f3
#define key_f4 k_f4
#define key_f5 k_f5
#define key_f6 k_f6
#define key_f7 k_f7
#define key_f8 k_f8
#define key_f9 k_f9
#define key_f10 k_f10

enum {
    scan_esc = 0x01,
    scan_ctrl = 0x1d,
    scan_lshift = 0x2a,
    scan_rshift = 0x36,
    scan_alt = 0x38,
    scan_space = 0x39,
    scan_f1 = 0x3b,
    scan_cursorup = 0x48,
    scan_cursorleft = 0x4b,
    scan_cursorright = 0x4d,
    scan_cursordown = 0x50
};

extern sbyte k_rshift, k_lshift, k_shift, k_ctrl, k_alt, k_numlock;
/* Android port addition, not upstream -- see key_menu_confirm/key_menu_cancel's own comment just
 * above for the whole feature. Populated by k_status() (KEYBOARD.c) exactly like k_shift/k_alt
 * just above, from host_key_down(key_menu_confirm)/host_key_down(key_menu_cancel). */
extern sbyte k_menu_confirm, k_menu_cancel;
extern volatile byte keydown[2][256];

void k_status(void);
int k_pressed(void);
int k_read(void);
void handler(void);
void installhandler(byte status);
void removehandler(void);
void disablebios(void);
void enablebios(void);
int biosstatus(void);

#endif

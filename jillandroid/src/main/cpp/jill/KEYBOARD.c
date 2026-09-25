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

/*
 * Android port note (this file, not upstream): already built entirely on HOSTSDL.H's own
 * host_* abstraction (host_is_open()/host_pump()/host_peek_key()/host_key_pressed()/
 * host_read_key()/host_key_down()/host_sleep()/host_clear_keys()) -- the exact same
 * abstraction HOSTANDROID.c already implements for Android, so this is a direct drop-in port,
 * byte-identical to upstream aside from this note. Renamed KEYBOARD.C -> KEYBOARD.c for the
 * same CMake gotcha as jill/GR.c (see that file's own header comment).
 *
 * This retires HOSTANDROID.c's own temporary k_read() stand-in (see that function's own
 * comment) -- the real k_read() here (and k_pressed()/k_status() etc.) supersede it. DELETE
 * the HOSTANDROID.c stand-in now that this exists -- a duplicate-symbol link error is what
 * that stand-in's own comment said to watch for.
 */

#include "KEYBOARD.H"
#include "HOSTSDL.H"

#include <string.h>

sbyte k_rshift, k_lshift, k_shift, k_ctrl, k_alt, k_numlock;
/* Android port addition, not upstream -- see KEYBOARD.H's own key_menu_confirm/key_menu_cancel/
 * k_menu_confirm/k_menu_cancel comments for the whole "Remap Gamepad" menu-lockout-prevention
 * feature these two feed. */
sbyte k_menu_confirm, k_menu_cancel;
volatile byte keydown[2][256];
static byte bioscall;

int k_pressed(void)
{
    if (host_is_open()) {
        (void)host_pump();
        return host_peek_key();
    }
    /* SDL window close is the host equivalent of cancelling a BIOS key wait. */
    return 1;
}

int k_read(void)
{
    if (host_is_open()) {
        while (host_is_open() && !host_key_pressed()) {
            (void)host_pump();
            host_sleep(1);
        }
        return host_read_key();
    }
    return key_escape;
}

void k_status(void)
{
    memset((void *)keydown, 0, sizeof(keydown));
    k_lshift = (sbyte)(host_is_open() && host_key_down(key_shift));
    k_rshift = 0;
    k_shift = (sbyte)(k_rshift | k_lshift);
    k_ctrl = (sbyte)(host_is_open() && host_key_down(key_ctrl));
    k_alt = (sbyte)(host_is_open() && host_key_down(key_alt));
    k_numlock = 0;
    /* Android port addition, not upstream -- see key_menu_confirm/key_menu_cancel's own KEYBOARD.H
     * comment for the whole always-on menu-confirm/menu-cancel feature. Same shape as k_shift/
     * k_alt just above -- these two are never remapped, so no gamepad-remap table lookup belongs
     * here, just a direct host_key_down() read like every other fixed key. */
    k_menu_confirm = (sbyte)(host_is_open() && host_key_down(key_menu_confirm));
    k_menu_cancel = (sbyte)(host_is_open() && host_key_down(key_menu_cancel));

    keydown[0][scan_ctrl] = (byte)k_ctrl;
    keydown[0][scan_lshift] = (byte)k_lshift;
    keydown[0][scan_rshift] = (byte)k_rshift;
    keydown[0][scan_alt] = (byte)k_alt;
    keydown[0][scan_space] = (byte)(host_is_open() && host_key_down(key_space));
    keydown[1][scan_cursorup] = (byte)(host_is_open() && host_key_down(k_up));
    keydown[1][scan_cursorleft] = (byte)(host_is_open() && host_key_down(k_left));
    keydown[1][scan_cursorright] = (byte)(host_is_open() && host_key_down(k_right));
    keydown[1][scan_cursordown] = (byte)(host_is_open() && host_key_down(k_down));
}

void installhandler(byte status)
{
    memset((void *)keydown, sizeof(keydown), 0);
    bioscall = status;
}

void removehandler(void)
{
}

void disablebios(void) { bioscall = 0; }
void enablebios(void) { host_clear_keys(); bioscall = 1; }
int biosstatus(void) { return bioscall != 0; }

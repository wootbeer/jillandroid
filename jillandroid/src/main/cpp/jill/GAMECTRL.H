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

#ifndef JILL_GAMECTRL_H
#define JILL_GAMECTRL_H

#include "RECOVERY.H"

extern word dx1, dy1, fire1, fire2, fire1off, fire2off;
/* Android port addition, not upstream -- see KEYBOARD.H's own key_menu_confirm/key_menu_cancel
 * comment for the whole "Remap Gamepad" menu-lockout-prevention feature. Populated by checkctrl()
 * (GAMECTRL.c) from k_menu_confirm/k_menu_cancel (KEYBOARD.c), with the exact same
 * continuous-held-suppressed-until-release shape fire1/fire1off already has -- domenu_at()
 * (JUNGLE.c) ORs menu_confirm/menu_cancel into its own existing fire2/fire1 checks. */
extern word menu_confirm, menu_cancel, menu_confirmoff, menu_canceloff;
extern word joyxl, joyxc, joyxr, joyyu, joyyc, joyyd;
extern word joyflag;
extern word key;
extern word dx1hold, dy1hold, flow1;
extern word dx1old, dy1old;
extern word joyxsense, joyysense;
extern longword systime;

extern word macrecord, macplay, macabort, macaborted, mactime;

int buttona1(void);
int buttona2(void);
void readspeed(void);
void readjoy(word *x, word *y);
int caldir(char *text, word *jx, word *jy);
int joypresent(void);
int calibratejoy(void);
void checkctrl(int pollflag);
void checkctrl0(int pollflag);
void sensectrlmode(void);
int gc_config(void);
void getkey(void);
void stopmac(void);
void playmac(char *filename);
void recordmac(char *filename);
void macrecend(void);
void recmac(void);
void getmac(void);
void gc_init(void);
void gc_exit(void);

#endif

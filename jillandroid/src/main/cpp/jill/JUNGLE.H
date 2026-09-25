/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code (?Jill of the Jungle Reconstructed?).

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

#ifndef JILL_JUNGLE_H
#define JILL_JUNGLE_H

#include "JILL.H"

void fin(void);
void fout(void);
void drawkeys(void);
void drawcmds(void);
void loadcfg(void);
void savecfg(void);
int numlines(void);
/* Renamed from upstream's own "getline" -- Android/bionic's <stdio.h> already declares a real
   POSIX getline(char **, size_t *, FILE *) with a completely different signature; see
   jill/JUNGLE.c's own header comment. */
int jill_getline(int number, char *line, int add_spaces);
void printline(vptype *viewport, int y, int number);
void ourdelay(void);
void initboard(void);
void putlevelmsg(int number);
void donelevelmsg(void);
void play(int demo_flag);
void pleasewait(void);
void printhi(int new_high_score);
int loadsavewin(const char *message, const char *blank_message);
int loadgame(void);
void savegame(void);
void drawgamewin(void);
void noisemaker(void);
void pageview(int page);
/* Android port addition, not upstream -- window_y is a new 9th parameter (wootbeer's own ask: the
 * pause menu's added DISPLAY MODE row pushed it from 11 lines to 12, one line taller than the
 * fixed y=64 top edge this function used to hardcode for every caller left room for -- see
 * domenu()'s own header comment, JUNGLE.c). Every pre-existing call site now passes 64 explicitly,
 * so nothing else changes; only pausemenu()'s own outer call passes something smaller, to fit its
 * own now-taller window without moving its bottom edge. */
int domenu(const char *message, const char *key_table, int first_choice_line,
           int choices, int timeout, int indent, int window_x, int window_width, int window_y);
int askquit(void);
/* Android port addition, not upstream -- see this function's own header comment (JUNGLE.c) for
 * the whole feature. Declared here for the same reason askquit() above is: play() (defined
 * earlier in JUNGLE.c) calls it, but it's itself defined later in that same file. */
int pausemenu(void);
void dodemo(void);
void jmenu(void);
void rexit(int result);

#endif

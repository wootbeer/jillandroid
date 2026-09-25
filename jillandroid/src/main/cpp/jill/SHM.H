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

#ifndef JILL_SHM_H
#define JILL_SHM_H

#include "RECOVERY.H"

#ifndef SHM_MAX_TABLES
#define SHM_MAX_TABLES 64
#endif

#define shm_fontf  1
#define shm_xxxxx  2
#define shm_blflag 4
#define st_byte    0
#define st_plain   1
#define st_rle     2

extern word shm_want[SHM_MAX_TABLES];
extern byte *shm_tbladdr[SHM_MAX_TABLES];
extern word shm_tbllen[SHM_MAX_TABLES];
extern word shm_flags[SHM_MAX_TABLES];

void shm_init(char *filename);
void init8bit(void);
void xlate_table(int table_number, byte *source, byte *work_buffer);
void shm_do(void);
void shm_exit(void);

/* Android port addition, not upstream -- see JILL.H's own jill_color_setting comment for the whole
 * "Jill Color" Enhancements feature this backs. Opens filename (expected to be another episode's
 * own jillN.sha, a true sibling directory reachable by relative path -- see that comment for why)
 * as a completely independent, one-shot read: its own local offset/length header, its own malloc'd
 * decode buffers, its own fd, closed again before this returns. Deliberately does NOT touch the
 * primary shafile/shm_offset/shm_length/shm_want/shm_tbladdr/shm_tbllen/shm_flags globals at all --
 * those stay exactly as this episode's own live game state has them, mid-session, the whole time;
 * this is a side read of a second file, not a second open on the primary one. Decodes exactly one
 * table (table_number, 0-127, matching this file's own on-disk 128-entry offset/length header --
 * NOT bounded to SHM_MAX_TABLES the way shm_tbladdr[]/the live table registry are, since nothing
 * here ever writes into that array) via the same xlate_table_core() decode logic xlate_table()
 * itself uses, so the output is in the exact same in-memory format as shm_tbladdr[]/shm_flags[] --
 * safe to hand straight to drawshape_from_table() (GR.H). On success, *out_addr is a malloc'd
 * buffer the caller now owns (free() it when done) and returns 1; on ANY failure (file doesn't
 * exist -- the expected shape of "that episode's own asset isn't installed on this device", a
 * short/corrupt header, or a zero-length table) leaves *out_addr NULL, *out_len/*out_flags 0, and
 * returns 0 -- the caller's own fallback-to-Default behavior (jill_refresh_color_override(),
 * JUNGLE.c) is built entirely around that 0 return, not a separate error code. */
int shm_load_single_table(const char *filename, int table_number, byte **out_addr, word *out_len,
                          word *out_flags);

#endif

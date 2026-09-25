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

#ifndef JILL_GR_H
#define JILL_GR_H

#include "RECOVERY.H"

#define JILL_SCREEN_WIDTH  320
#define JILL_SCREEN_HEIGHT 200
#define JILL_PAGE_COUNT    2
#define JILL_PALETTE_SIZE  256
#ifndef SHM_MAX_TABLES
#define SHM_MAX_TABLES     64
#endif

enum {
    x_cga = 0,
    x_cgagrey = 1,
    x_ega = 2,
    x_egagrey = 3,
    x_vga = 4,
    x_mcga = 6
};

typedef struct vptype {
    word vpx, vpy;
    word vpxl, vpyl;
    word vpox, vpoy;
    word vphi, vpback;
} vptype;

extern byte x_ourmode;
extern vptype mainvp;
extern uword cmtab[4][256];
extern word pagemode, pageshow, pagedraw;
extern word showofs, drawofs, pagelen;
extern word pixelsperbyte, origmode;
extern byte pixvalue;
extern byte *LOST;
extern byte vgapal[JILL_PALETTE_SIZE * 3];
extern byte jill_video[JILL_PAGE_COUNT][JILL_SCREEN_HEIGHT][JILL_SCREEN_WIDTH];

void pixaddr_cga(int x, int y, byte **vidbuf, byte *bitc);
void pixaddr_ega(int x, int y, byte **vidbuf, byte *bitc);
void pixaddr_vga(int x, int y, byte **vidbuf, byte *bitc);
void drawshape(vptype *vp, int n, int x, int y);
/* Android port addition, not upstream -- see JILL.H's own jill_color_setting comment for the whole
 * feature (wootbeer's own cross-episode "Jill Color" Enhancements option). A reusable, minimal
 * primitive: draws shape_number (0-255, a raw index into table_addr -- NOT drawshape()'s own
 * encoded table<<8|shape_number scheme, since there's no shm_tbladdr[]-style table registry to
 * resolve a table number against here) directly from an already-decoded table buffer
 * (table_addr/table_flags, in the exact same in-memory format xlate_table() produces for
 * shm_tbladdr[]/shm_flags[]) rather than resolving shm_tbladdr[n>>8] the way drawshape() itself
 * does. Deliberately a SEPARATE function, not a drawshape() refactor -- drawshape() is this whole
 * engine's single most heavily-used draw primitive (every object, every UI element, every menu
 * icon), so this avoids touching its own well-tested code path at all; the two share their actual
 * pixel-blit primitives (ldrawsh_cga()/ldrawsh_ega()/ldrawsh_vga(), below) and clip the exact same
 * way, so visuals match byte-for-byte once a real table_addr is supplied. Silently does nothing if
 * table_addr is NULL. */
void drawshape_from_table(vptype *vp, const byte *table_addr, word table_flags, int shape_number,
                          int x, int y);
void plot(vptype *vp, int x, int y, int color);
void linex(int x0, int y0, int x1, int y1, int color);
void waitsafe(void);
void setcm_cga(int table, int lo0, int lo1, int lo2, int lo3,
               int hi0, int hi1, int hi2, int hi3);
void fontcolor_cga(int hi, int lo, int back);
void fontcolor_ega(int hi, int lo, int back);
void fontcolor_vga(int hi, int lo, int back);
void fntcolor(int hi, int lo, int back);
void initcolortabs_cga(void);
void initcolortabs_ega(void);
void initcolortabs_vga(void);
void setpages(void);
void setpagemode(int mode);
int getportnum(void);
void pageflip(void);
void gr_present_page(void);
void wait_vbi(void);
void vga_setpal(void);
void readpal(byte *palette);
void clrpal(void);
void fadein(void);
void setcolor(int color, int red, int green, int blue);
void fadeout(void);
int gr_config(void);
void gr_init(void);
void gr_exit(void);

void ldrawsh_cga(vptype *vp, int x, int y, int width_bytes, int height,
                 const byte *shape, int color_table);
void ldrawsh_ega(vptype *vp, int x, int y, int width_bytes, int height,
                 const byte *shape, int color_table);
void ldrawsh_vga(vptype *vp, int x, int y, int width_bytes, int height,
                 const byte *shape, int color_table);
void ldrawsh_mcga(vptype *vp, int x, int y, int width_bytes, int height,
                  const byte *shape, int color_table);
void scroll(vptype *vp, int x0, int y0, int x1, int y1, int xd, int yd);
void lcopypage(void);
void scrollvp(vptype *vp, int xd, int yd);
void clrvp(vptype *vp, byte color);

void plot_cga(int x, int y, byte color);
void plot_ega(int x, int y, byte color);
void plot_vga(int x, int y, byte color);
void readpix_vga(int x, int y);
void line_cga(int x0, int y0, int x1, int y1, byte color);

#endif

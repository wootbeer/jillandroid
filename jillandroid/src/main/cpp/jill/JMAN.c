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
 * Android port note (this file, not upstream): pure game logic (the object manager --
 * addobj(), killobj(), the movement/collision helpers trymove()/justmove()/crawl()/fishdo()
 * etc., msg_tiny, scoring, board updates). The only real change from upstream is swapping the
 * one _itoa() call (an MSVC-only int-to-string helper, not available on Android/bionic) for
 * the portable snprintf() equivalent, and adding <stdio.h> for it. Renamed JMAN.C -> JMAN.c
 * for the same CMake gotcha as jill/GR.c (see that file's own header comment).
 */

#include "JILL.H"
#include "EPISODE.H"
#include "MUSIC.H"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BREAKWALL_TILE 0x00a7

objtype objs[maxobjs + 2];
word numobjs, numscrnobjs;
word scrnobjs[maxscrnobjs + 1];
pltype pl;
word scrollxd, scrollyd, oldscrollxd, oldscrollyd, oldx0, oldy0;
word gameover, gamecount, statmodflg, designflag, peeky;

/* Android port addition, not upstream -- Cheat Codes (JILL.H's own jill_cheat_unlimited_gems
 * comment has the whole feature). The four player-facing toggles pausemenu()'s own CHEATS submenu
 * (JUNGLE.c) flips; jill_cheat_all_items_added[] just below is private bookkeeping for the fourth
 * one, never touched directly outside this file. */
int jill_cheat_unlimited_gems;
int jill_cheat_unlimited_keys;
int jill_cheat_god_mode;
int jill_cheat_all_items;

/* Android port addition, not upstream -- one flag per jill_inventory_kind (JOBJ.H), indexed
 * directly by the inv_* enum value itself (0..10 -- comfortably under this array's own size, no
 * separate lookup table needed). Nonzero means "the All Items cheat granted exactly one of this
 * kind, and it's still outstanding" -- set by jill_cheat_set_all_items(1) below, cleared either by
 * jill_cheat_set_all_items(0) (which also takes the real item back) or by jill_cheats_reset().
 * encode_player() (JUNGLE.c) reads this through jill_cheat_all_items_owed() below to keep exactly
 * these grants out of every on-disk save, without this file needing to know anything about save
 * file layout. */
static int jill_cheat_all_items_added[11];

/* Android port addition, not upstream -- see JILL.H's own jill_cheat_all_items comment for why
 * these four kinds specifically (not gems/keys, already their own dedicated cheats above; not the
 * frog/bird/fish transformation potions, a form change rather than passive inventory). Tops each
 * one up to at least 1 -- never strips or duplicates a copy the player already, legitimately holds
 * -- and remembers exactly which kinds it had to grant so toggling back off (or jill_cheats_reset())
 * can take back only those specific grants. */
void jill_cheat_set_all_items(int enabled)
{
    static const int kinds[4] = { inv_knife, inv_blade, inv_jump, inv_invin };
    int index, kind;

    if (enabled) {
        for (index = 0; index < 4; ++index) {
            kind = kinds[index];
            if (!invcount(kind)) {
                addinv(kind);
                jill_cheat_all_items_added[kind] = 1;
            }
        }
    } else {
        for (index = 0; index < 4; ++index) {
            kind = kinds[index];
            if (jill_cheat_all_items_added[kind]) {
                (void)takeinv(kind);
                jill_cheat_all_items_added[kind] = 0;
            }
        }
    }
    jill_cheat_all_items = enabled;
    statmodflg |= mod_screen;
}

/* Android port addition, not upstream -- see jill_cheat_all_items_added[] above; JUNGLE.c's own
 * encode_player() is the only real caller, deliberately kept out of this file so JMAN.c doesn't
 * need to know anything about the save-file format. */
int jill_cheat_all_items_owed(int invthing)
{
    if (invthing < 0 || invthing >= 11) return 0;
    return jill_cheat_all_items_added[invthing];
}

/* Android port addition, not upstream -- see JILL.H's own jill_cheat_unlimited_gems comment for
 * the whole "session-only, resets at a real session boundary" story and exactly which two call
 * sites (JUNGLE.c) call this. jill_cheat_set_all_items(0) both flips the flag back off and takes
 * back exactly whatever it granted -- reused here rather than duplicating that logic, and harmless
 * to call even when All Items was already off (nothing in jill_cheat_all_items_added[] to take
 * back, so it's a no-op past the first two lines). */
void jill_cheats_reset(void)
{
    jill_cheat_unlimited_gems = 0;
    jill_cheat_unlimited_keys = 0;
    jill_cheat_god_mode = 0;
    jill_cheat_set_all_items(0);
}

void setboard(int x, int y, int value)
{
    if (x >= 0 && x < boardxs && y >= 0 && y < boardys)
        bd[x][y] = (uword)value;
}

void modboard(int x, int y)
{
    if (x >= 0 && x < boardxs && y >= 0 && y < boardys)
        bd[x][y] |= mod_screen;
}

void initobjs(void)
{
    numobjs = 1;
    objs[0].objkind = obj_player;
    objs[0].x = 32;
    objs[0].y = 32;
    objs[0].xd = 0;
    objs[0].yd = 0;
    objs[0].state = st_stand;
    objs[0].substate = 0;
    objs[0].statecount = 0;
    objs[0].counter = 0;
    objs[0].xl = kindxl[obj_player];
    objs[0].yl = kindyl[obj_player];
    objs[0].inside = NULL;
    objs[0].info1 = 0;
    objs[0].zaphold = 0;
    pl.numinv = 0;
    pl.level = 1;
    initinv();
    memset(pl.pad, 0, sizeof(pl.pad));
}

void playerkill(int n)
{
    addscore(kindscore[(byte)objs[n].objkind], objs[n].x, objs[n].y);
    notemod(n);
    killobj(n);
}

int countobj(int objkind)
{
    int count = 0, n;
    for (n = 0; n < numobjs; ++n) count += objs[n].objkind == objkind;
    return count;
}

void notemod(int n)
{
    int x, y, startx, starty, endx, endy;
    startx = objs[n].x / 16; starty = objs[n].y / 16;
    endx = (objs[n].x + objs[n].xl + 15) / 16;
    endy = (objs[n].y + objs[n].yl + 15) / 16;
    for (y = starty; y < endy; ++y)
        for (x = startx; x < endx; ++x) modboard(x, y);
}

void setobjsize(int n)
{
    int kind, string_length = 0;
    char number[8];
    kind = (byte)objs[n].objkind;
    objs[n].xl = kindxl[kind]; objs[n].yl = kindyl[kind];
    if (objs[n].inside != NULL) string_length = (int)strlen(objs[n].inside);
    if (kind == obj_text6) objs[n].xl = (word)(string_length * 6);
    else if (kind == obj_text8) objs[n].xl = (word)(string_length * 8);
    else if (kind == obj_score) {
        snprintf(number, sizeof(number), "%d", objs[n].state);
        objs[n].xl = (word)((strlen(number) + 2) * kindxl[obj_score]);
    }
}

int findcheckpt(int level)
{
    int n;
    for (n = 0; n < numobjs; ++n)
        if (objs[n].objkind == obj_checkpt && objs[n].counter == level) return n;
    return 0;
}

void dolevelsong(void)
{
    int n = findcheckpt(pl.level);
    int c;
    int d;

    if (n > 0 && objs[n].inside != NULL &&
        (objs[n].inside[0] == '*' || objs[n].inside[0] == '#' ||
         objs[n].inside[0] == '&')) {
        /* Android port change, not upstream -- was a plain, unbounded strcpy(); see JOBJ.c's own
         * msg_checkpt() comment (the identical fix, applied there for the same crash) for the whole
         * "level-file-sourced string into a fixed 32-byte buffer" story. Bounded here too, for the
         * exact same reason -- objs[n].inside comes from the same unchecked level-file load either
         * way. */
        strncpy(newlevel, objs[n].inside, sizeof(newlevel) - 1);
        newlevel[sizeof(newlevel) - 1] = '\0';
    } else {
        c = findcheckpt(0);
        /* Android port addition, not upstream -- wootbeer's own crash report: "jill fell down a hole in
         * the terrain in episode 2 and then the game crashed." Root cause: findcheckpt() (just
         * above) returns 0 both for "match found at index 0" and "no match found at all" -- a
         * sentinel collision -- and index 0 is ALWAYS the player object (initobjs()'s own
         * convention, this file, cited elsewhere here too), whose own `inside` field is always
         * NULL. The `n > 0` check just above already treats a 0 result correctly as "not found";
         * this fallback branch never got the same guard before dereferencing objs[c].inside[0] --
         * so on ANY board with no CHECKPT object at counter==0 (whatever pit/path Jill fell into on
         * that Episode 2 board apparently leads to exactly one), c stays 0 and this unconditionally
         * read NULL[0]. Real DOS memory at segment 0 (the interrupt vector table) made that merely
         * read harmless-looking garbage instead of faulting, so upstream's own original game never
         * visibly crashed from it -- Android's flat address space has no mapped page at NULL, so the
         * exact same logic bug is fatal here. Guarded the same way `n` already is, just above: no
         * counter==0 checkpoint on this board simply means no song override, exactly like the
         * primary branch's own "not found" case treats it. objs[c].inside itself is checked too, not
         * just c>0 -- a real counter==0 checkpoint with no `inside` string at all is equally
         * legitimate (the primary branch's own objs[n].inside != NULL check, just above, already
         * allows for that same case on `n`). */
        if (c > 0 && objs[c].inside != NULL) {
            d = objs[c].inside[0];
            if (d == '*' || d == '#' || d == '&') {
                /* Android port change, not upstream -- same unbounded-strcpy fix as the primary
                 * branch's own, just above. */
                strncpy(newlevel, objs[c].inside, sizeof(newlevel) - 1);
                newlevel[sizeof(newlevel) - 1] = '\0';
            }
        }
    }
}

void p_reenter(int died)
{
    int n;
    int destination_x;
    int destination_y;
    int x;
    int y;
    word saved_level;
    ulongword saved_old_score;

    statmodflg |= mod_screen;
    n = findcheckpt(pl.level);
    destination_x = objs[n].x;
    destination_y = objs[n].y;
    if (objs[0].objkind != obj_tiny) destination_y -= 16;
    if (n > 0 && died && objs[n].state == 1) {
        saved_old_score = pl.oldscore;
        saved_level = pl.level;
        loadboard(curlevel);
        pl.level = saved_level;
        pl.score = saved_old_score;
        pl.health = 6;
        n = findcheckpt(pl.level);
    }
    pl.oldscore = pl.score;
    dolevelsong();
    objs[0].x = (word)(destination_x & ~7);
    objs[0].y = (word)destination_y;
    setorigin();
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y)
            setboard(x, y, bd[x][y] | mod_screen);
    objs[0].state = st_begin;
    objs[0].statecount = 0;
}

void p_ouch(int healthtake, int diemode)
{
    if (objs[0].objkind == obj_tiny) return;
    if (objs[0].objkind == obj_player &&
        (stateinfo[objs[0].state] & sti_invincible)) return;
    healthtake -= invcount(inv_invin);
    if (healthtake <= 0) return;
    statmodflg |= mod_screen;
    pl.health = (word)(pl.health - healthtake);
    pl.ouched = 1;
    /* Android port addition, not upstream -- God Mode cheat code (JILL.H's own
     * jill_cheat_unlimited_gems comment has the whole Cheat Codes feature). This is the one and
     * only place this port ever reduces pl.health toward a lethal value or arms the death state
     * below (confirmed by grep across every p_ouch() caller in the codebase), so a single clamp
     * here is enough -- mirrors GoT Android's own "health floor of 1" God Mode exactly (wootbeer's own
     * ask: "function the same way as in god of thunder"): health can still visibly drop all the way
     * to 1 and the ordinary non-fatal hit sound just below still plays, this only stops a hit from
     * ever actually reaching the value (0) the death branch below checks for. */
    if (jill_cheat_god_mode && pl.health == 0) pl.health = 1;
    if (pl.health > 0) {
        snd_play(4, 19);
        return;
    }
    pl.health = 0;
    objs[0].objkind = obj_player;
    objs[0].xl = 16; objs[0].yl = 32;
    objs[0].state = st_die;
    objs[0].statecount = 0;
    objs[0].substate = (word)diemode;
    if (diemode == die_bird)
        objs[0].y = (word)((objs[0].y - 1) & ~15);
    objs[0].yd = -12;
    snd_play(4, 39 + diemode);
    explode1(objs[0].x, objs[0].y, 10);
}

void seekplayer(int n, int *dx, int *dy)
{
    *dx = JILL_SIGN(objs[0].x - objs[n].x);
    *dy = JILL_SIGN(objs[0].y - objs[n].y);
}

void modjunglescroll(int xd, int yd, int modcode)
{
    int x, y, new_x, new_y, viewport_end_y, end_y, end_x, viewport_end_x;

    if (xd > 0) {
        viewport_end_x = (gamevp->vpox + gamevp->vpxl - xd) / 16;
        end_x = JILL_MIN((gamevp->vpox + gamevp->vpxl - 1) / 16,
                         boardxs - 1);
        for (x = viewport_end_x; x <= end_x; ++x) {
            for (y = 0; y < scrnys + 1; ++y) {
                new_y = JILL_MIN(gamevp->vpoy / 16 + y, boardys - 1);
                setboard(x, new_y, bd[x][new_y] | modcode);
                if (modcode == mod_virtual) drawcell(x, new_y);
            }
        }
    } else if (xd < 0) {
        new_x = gamevp->vpox / 16;
        for (y = 0; y < scrnys + 1; ++y) {
            new_y = gamevp->vpoy / 16 + y;
            setboard(new_x, new_y, bd[new_x][new_y] | modcode);
            if (modcode == mod_virtual) drawcell(new_x, new_y);
        }
    }

    if (yd > 0) {
        viewport_end_y = gamevp->vpoy + gamevp->vpyl;
        end_y = JILL_MIN((viewport_end_y - 1) / 16, boardys - 1);
        for (new_y = (viewport_end_y - yd) / 16;
             new_y <= end_y; ++new_y) {
            for (x = 0; x < scrnxs + 1; ++x) {
                new_x = JILL_MIN(gamevp->vpox / 16 + x, boardxs - 1);
                setboard(new_x, new_y, bd[new_x][new_y] | modcode);
                if (modcode == mod_virtual) drawcell(new_x, new_y);
            }
        }
    } else if (yd < 0) {
        for (new_y = gamevp->vpoy / 16;
             new_y <= (gamevp->vpoy - yd - 1) / 16; ++new_y) {
            for (x = 0; x < scrnxs + 1; ++x) {
                new_x = JILL_MIN(gamevp->vpox / 16 + x, boardxs - 1);
                setboard(new_x, new_y, bd[new_x][new_y] | modcode);
                if (modcode == mod_virtual) drawcell(new_x, new_y);
            }
        }
    }
}

void junglescroll(int xd, int yd)
{
    int x, y;
    int cut0, cut1, cut2, cut3;
    int start_x, start_y, start_x2, start_y2;
    int end_x, end_y, end_x2, end_y2;

    start_x = oldx0 / 16;
    start_y = oldy0 / 16;
    end_x = (oldx0 + objs[0].xl + 15) / 16;
    end_y = (oldy0 + objs[0].yl + 15) / 16;
    start_x2 = objs[0].x / 16;
    start_y2 = objs[0].y / 16;
    end_x2 = (objs[0].x + objs[0].xl + 15) / 16;
    end_y2 = (objs[0].y + objs[0].yl + 15) / 16;

    if (xd == 0) {
        cut0 = 0;
        cut1 = JILL_MIN(start_x, start_x2) * 16 - gamevp->vpox;
        cut2 = JILL_MAX(end_x, end_x2) * 16 - gamevp->vpox;
        cut3 = gamevp->vpxl;
        scroll(gamevp, cut0, 0, cut1, gamevp->vpyl, 0, -yd);
    } else if (yd == 0) {
        cut0 = 0;
        cut1 = JILL_MIN(start_y, start_y2) * 16 - gamevp->vpoy;
        cut2 = JILL_MAX(end_y, end_y2) * 16 - gamevp->vpoy;
        cut3 = gamevp->vpyl;
        scroll(gamevp, 0, cut0, gamevp->vpxl, cut1, -xd, 0);
    }

    for (x = start_x; x < end_x; ++x) {
        for (y = start_y; y < end_y; ++y) {
            drawcell(x, y);
            setboard(x, y, bd[x][y] & ~mod_screenonly);
        }
    }

    if (xd == 0) {
        scroll(gamevp, cut1, 0, cut2, gamevp->vpyl, 0, -yd);
        gamevp->vpoy = (word)(gamevp->vpoy + yd);
        (void)kindmsg[(byte)objs[0].objkind](0, msg_draw, 0);
        scroll(gamevp, cut2, 0, cut3, gamevp->vpyl, 0, -yd);
    } else if (yd == 0) {
        scroll(gamevp, 0, cut1, gamevp->vpxl, cut2, -xd, 0);
        gamevp->vpox = (word)(gamevp->vpox + xd);
        (void)kindmsg[(byte)objs[0].objkind](0, msg_draw, 0);
        scroll(gamevp, 0, cut2, gamevp->vpxl, cut3, -xd, 0);
    } else {
        scrollvp(gamevp, -xd, -yd);
        gamevp->vpox = (word)(gamevp->vpox + xd);
        gamevp->vpoy = (word)(gamevp->vpoy + yd);
        (void)kindmsg[(byte)objs[0].objkind](0, msg_draw, 0);
    }
    modjunglescroll(xd, yd, mod_virtual);
}

#if defined(JILL_EP3)
void pagedjunglescroll(int xd, int yd)
{
    scrollvp(gamevp, -xd, -yd);
    gamevp->vpox = (word)(gamevp->vpox + xd);
    gamevp->vpoy = (word)(gamevp->vpoy + yd);
    modjunglescroll(xd, yd, mod_screen);
}
#endif

void refresh(int page_mode)
{
    byte update_table[boardxs][20];
    int x, y, c, n;
    int start_x, start_y, end_x, end_y;
    int scroll_x, scroll_y;

    if (page_mode) {
        if (statmodflg) {
            drawstats();
            statmodflg &= (pagedraw + 1) * mod_page0;
        }
        if (scrollxd + oldscrollxd != 0 || scrollyd + oldscrollyd != 0) {
            gamevp->vpox = (word)(gamevp->vpox - oldscrollxd);
            gamevp->vpoy = (word)(gamevp->vpoy - oldscrollyd);
            scroll_x = scrollxd + oldscrollxd;
            scroll_y = scrollyd + oldscrollyd;
#if defined(JILL_EP3)
            pagedjunglescroll(scroll_x, scroll_y);
#else
            scrollvp(gamevp, -scroll_x, -scroll_y);
            gamevp->vpox = (word)(gamevp->vpox + scroll_x);
            gamevp->vpoy = (word)(gamevp->vpoy + scroll_y);
            modjunglescroll(scroll_x, scroll_y, mod_screen);
#endif
        }
        oldscrollxd = scrollxd;
        oldscrollyd = scrollyd;

        start_x = JILL_MIN(gamevp->vpox / 16 + scrnxs, boardxs - 1);
        start_y = JILL_MIN(gamevp->vpoy / 16 + scrnys - 1, boardys - 1);
        end_x = JILL_MAX(gamevp->vpox / 16 - 2, 0);
        end_y = JILL_MAX(gamevp->vpoy / 16 - 2, 0);
        for (x = start_x; x >= end_x; --x) {
            for (y = start_y; y >= end_y; --y) {
                if (bd[x][y] & mod_screen) {
                    drawcell(x, y);
                    setboard(x, y, bd[x][y] &
                             ~((pagedraw + 1) * mod_page0));
                }
            }
        }
        for (n = numobjs - 1; n >= 0; --n) {
            if (objs[n].objflags & mod_screen) {
                (void)kindmsg[(byte)objs[n].objkind](n, msg_draw, 0);
                objs[n].objflags &= (uword)~((pagedraw + 1) * mod_page0);
            }
        }
        /* Android port fix, not upstream -- MUST run before pageflip(), not after (see the tail
         * comment below, at this function's own end, for why it has to run this late in the first
         * place; this comment is about exactly how late). pageflip() (GR.c) doesn't just present
         * the page that was just drawn -- it also flips which of the two video pages *pagedraw*
         * itself points at, to the OTHER (stale, not-yet-redrawn-this-tick) one, in the same call.
         * drawcell()/jill_draw_minimal_hud() (both reached from jill_minimal_hud_tick(), JUNGLE.c)
         * ultimately write through pixaddr_vga() (GR.c), which always targets *pagedraw* -- so a
         * call placed after pageflip() (an earlier round of this whole feature had it as this
         * function's own last line, unconditionally, after both this branch and the one below)
         * draws into the page that was just hidden, not the one that was just shown, missing the
         * frame that's actually on screen and instead landing one whole tick early into a page
         * nobody's looking at yet. Every OTHER draw call in this same branch already runs before
         * this same pageflip() for exactly that reason; this one just has to join them, moved up
         * from its own original spot at the very end of this function -- wootbeer's own screenshot
         * caught the result: half the HUD strip's own text missing letters (a wprint() call is
         * more than one drawshape() call, and each one only fully lands in whichever page was
         * *pagedraw* at the moment it ran -- landing a tick late on a page that gets shown a tick
         * later reads as a letter or two flickering in and out, not a clean miss). */
        if (jill_active_display_mode == JILL_DISPLAY_MINIMAL) jill_minimal_hud_tick();
        pageflip();
    } else {
        if (statmodflg) {
            drawstats();
            statmodflg = 0;
        }
        for (c = 0; c < boardxs; ++c) update_table[c][0] = 255;
        if (scrollxd != 0 || scrollyd != 0)
            junglescroll(scrollxd, scrollyd);

        start_x = JILL_MIN(gamevp->vpox / 16 + scrnxs - 1, boardxs - 1);
        start_y = JILL_MIN(gamevp->vpoy / 16 + scrnys - 1, boardys - 1);
        end_x = JILL_MAX(gamevp->vpox / 16 - 2, 0);
        end_y = JILL_MAX(gamevp->vpoy / 16 - 2, 0);

        for (n = 0; n < numobjs; ++n) {
            if (objs[n].objflags & mod_screen) {
                x = objs[n].x / 16;
                if (x < end_x) x = end_x;
                c = 0;
                while (update_table[x][c] != 255) ++c;
                update_table[x][c] = (byte)n;
                update_table[x][c + 1] = 255;
                objs[n].objflags &= ~mod_screen;
            }
        }

        for (x = start_x; x >= end_x; --x) {
            for (y = start_y; y >= end_y; --y) {
                if (bd[x][y] & mod_screenonly) {
                    drawcell(x, y);
                    setboard(x, y, bd[x][y] & ~mod_screen);
                }
            }
            for (c = 0; update_table[x][c] != 255 && c < 20; ++c) {
                n = update_table[x][c];
                (void)kindmsg[(byte)objs[n].objkind](n, msg_draw, 0);
            }
        }
    }

    if (pl.ouched != 0) {
        pl.ouched = 0;
        statmodflg |= mod_screen;
    }

    /* Android port addition, not upstream -- Minimal UI's own compact HUD (JUNGLE.c) has to be
     * drawn dead last, strictly after this function's own tile/object redraw and scrolling above,
     * not from the statmodflg-gated drawstats() call near the top of each branch. Drawing it there
     * instead would get it immediately overdrawn by this same tick's own terrain redraw, and --
     * worse -- the page_mode branch's own scrollvp() call would drag stale HUD pixels sideways
     * along with the world on every scrolling tick, since it blits gamevp's whole rectangle with no
     * idea any of it was ever HUD content rather than level art (gamevp now covers the HUD's own
     * rows too, see jill_activate_display_mode()'s own comment, JUNGLE.c). Unconditional (not gated
     * on statmodflg) because gamevp's own redraw above can dirty the HUD's pixel footprint on any
     * tick that scrolls or reveals new terrain, whether or not a STAT actually changed this same
     * tick -- see jill_minimal_hud_tick()'s own comment for the rest of the story, including its
     * one known limitation.
     *
     * Only for the non-page_mode branch here -- the page_mode branch (real gameplay's own only
     * path in practice, see that branch's own matching call for the whole story) already made its
     * own call further up, before ITS OWN pageflip(), since calling it after pageflip() draws into
     * the wrong one of the two video pages. This branch never calls pageflip() itself at all (its
     * own callers, e.g. drawboard(), do that separately once the whole board's been redrawn), so
     * there's no equivalent ordering hazard here -- this position was always correct for this
     * branch, only ever wrong for the other one.
     *
     * Reads jill_active_display_mode, not jill_display_mode -- wootbeer's own follow-up ask, "I want
     * to keep the default look for the main menu" (see JILL.H's own comment on the two) -- refresh()
     * itself only ever runs from real gameplay drawing (drawboard()/updbkgnd() etc.), but
     * jill_active_display_mode is still the correct one to read here since it's what actually
     * reflects whichever geometry gamevp is really in at this instant, the same reasoning
     * drawgamewin()/drawcmds()/drawstats() (JUNGLE.c) already apply to their own checks. */
    if (!page_mode && jill_active_display_mode == JILL_DISPLAY_MINIMAL) jill_minimal_hud_tick();
}

void updbkgnd(void)
{
    int x, y, tile;
    int start_x = gamevp->vpox / 16;
    int start_y = gamevp->vpoy / 16;
    int end_x = JILL_MIN(start_x + scrnxs, boardxs - 1);
    int end_y = JILL_MIN(start_y + scrnys, boardys - 1);

    for (x = start_x; x <= end_x; ++x) {
        for (y = start_y; y <= end_y; ++y) {
            tile = board(x, y);
            if (info[tile].flags & f_msgupdate)
                setboard(x, y, bd[x][y] |
                         ((msg_block(x, y, msg_update) != 0) * mod_screen));
        }
    }
}

/* Android port addition, not upstream -- see JILL.H's own jill_enemyspeed_setting comment for the
 * whole "Enemy Speed" Enhancements feature. Toggled once per updobjs() call below (not once per
 * object), so every non-player object slowed by JILL_SPEED_SLOW skips the exact same ticks as every
 * other one -- a single shared phase, deliberately, the simplest thing that still reads as "enemies
 * move at half pace" rather than needing per-object state. */
static int jill_enemyspeed_slow_phase;

void updobjs(int doflag)
{
    int count, count2;
    int n, n2;
    int flag, old_x, old_y, old_width, old_height;
    int x, y, start_x, end_x, start_y, end_y;

    if (doflag) jill_enemyspeed_slow_phase ^= 1;

    numscrnobjs = 1;
    scrnobjs[0] = 0;
    start_x = gamevp->vpox - 96;
    end_x = gamevp->vpox + gamevp->vpxl + 96;
    start_y = gamevp->vpoy - 48;
    end_y = gamevp->vpoy + gamevp->vpyl + 48;

    for (n = 1; n < numobjs && numscrnobjs < maxscrnobjs; ++n) {
        if (objs[n].x + objs[n].xl >= start_x && objs[n].x <= end_x &&
            objs[n].y + objs[n].yl >= start_y && objs[n].y <= end_y) {
            scrnobjs[numscrnobjs++] = (word)n;
            objs[n].objflags &= (uword)~(pagedraw * mod_page0);
        }
    }

    scrollxd = 0;
    scrollyd = 0;
    oldx0 = objs[0].x;
    oldy0 = objs[0].y;

    for (count = 0; count < numscrnobjs; ++count) {
        n = scrnobjs[count];
        old_x = objs[n].x;
        old_y = objs[n].y;
        old_width = objs[n].xl;
        old_height = objs[n].yl;

        if (doflag) {
            if (objs[n].zaphold > 0) --objs[n].zaphold;
            /* Android port addition, not upstream -- see jill_enemyspeed_slow_phase's own comment
             * above and JILL.H's own jill_enemyspeed_setting comment for the whole feature. n==0 is
             * always the player (initobjs()'s own convention -- see JILL.H's own
             * JILL_PLAYER_SHAPE_TABLE comment for the same fact cited elsewhere), deliberately never
             * scaled here: wootbeer's own ask was specifically "enemy speed", and every OTHER object on
             * screen (pickups, thrown weapons, doors, effects) also runs through this same
             * dispatch -- there's no per-kind "is this actually an enemy" flag anywhere in this
             * codebase to narrow it further (checked; none exists), so this uniformly covers every
             * non-player object, not just hostile ones. FAST calls kindmsg[...] a second time in the
             * same tick -- each call is already a fully self-contained, collision-tested single-tick
             * step (exactly what every enemy AI handler in JOBJ.c/JOBJ2.c/JOBJ3.c already does once
             * per call), so two calls back-to-back is "two safely-validated small steps", not one
             * larger raw-distance jump that could tunnel through geometry. SLOW instead skips the
             * call outright on alternate ticks (jill_enemyspeed_slow_phase), the same "do nothing
             * this tick" a paused/off-screen object already experiences via doflag==0 just below,
             * just gated on the shared phase instead. mod_screen only gets set on a tick this
             * object's own msg_update actually ran (or every tick when there's nothing to gate, at
             * JILL_SPEED_DEFAULT/FAST) -- skipping it outright on a skipped SLOW tick is correct:
             * the object's own screen position hasn't changed since the last tick, so there's
             * nothing new to redraw. */
            if (n == 0 || jill_enemyspeed_setting != JILL_SPEED_SLOW || jill_enemyspeed_slow_phase) {
                if (kindmsg[(byte)objs[n].objkind](n, msg_update, 0))
                    objs[n].objflags |= mod_screen;
                if (n != 0 && jill_enemyspeed_setting == JILL_SPEED_FAST) {
                    if (kindmsg[(byte)objs[n].objkind](n, msg_update, 0))
                        objs[n].objflags |= mod_screen;
                }
            }
        } else {
            objs[n].objflags |= mod_screen;
        }

        if (kindflags[(byte)objs[n].objkind] & f_msgtouch) {
            for (count2 = 0; count2 <= numscrnobjs; ++count2) {
                n2 = scrnobjs[count2];
                if (n2 != n &&
                    objs[n2].x < objs[n].x + objs[n].xl &&
                    objs[n].x < objs[n2].x + objs[n2].xl &&
                    objs[n2].y < objs[n].y + objs[n].yl &&
                    objs[n].y < objs[n2].y + objs[n2].yl) {
                    (void)kindmsg[(byte)objs[n].objkind](n, msg_touch, n2);
                    objs[n].objflags |= mod_screen;
                    (void)kindmsg[(byte)objs[n2].objkind](n2, msg_touch, n);
                    objs[n2].objflags |= mod_screen;
                }
            }
        }

        if (objs[n].objflags & mod_screen) {
            start_x = old_x / 16;
            start_y = old_y / 16;
            end_x = (old_x + old_width + 15) / 16;
            end_y = (old_y + old_height + 15) / 16;
            for (y = start_y; y < end_y; ++y)
                for (x = start_x; x < end_x; ++x)
                    setboard(x, y, bd[x][y] | mod_screen);
        }
    }

    for (count = 0; count < numscrnobjs; ++count) {
        n = scrnobjs[count];
        x = objs[n].x;
        y = objs[n].y;
        start_x = x / 16;
        start_y = y / 16;
        end_x = (x + objs[n].xl + 15) / 16;
        end_y = (y + objs[n].yl + 15) / 16;
        flag = 0;
        for (y = start_y; y < end_y; ++y)
            for (x = start_x; x < end_x; ++x)
                flag = flag || (bd[x][y] & mod_screen);
        if (flag) objs[n].objflags |= mod_screen;
    }
}

void killobj(int n)
{
    objs[n].objflags |= mod_screen;
    objs[n].objkind = obj_killme;
}

int addobj(int kind, int x, int y)
{
    objs[numobjs].objkind = (sbyte)kind;
    objs[numobjs].x = (word)x;
    objs[numobjs].y = (word)y;
    objs[numobjs].state = 0;
    objs[numobjs].xd = 0;
    objs[numobjs].yd = 0;
    objs[numobjs].xl = kindxl[kind];
    objs[numobjs].yl = kindyl[kind];
    objs[numobjs].substate = 0;
    objs[numobjs].statecount = 0;
    objs[numobjs].objflags = 0;
    objs[numobjs].inside = NULL;
    objs[numobjs].info1 = 0;
    objs[numobjs].zaphold = 0;
    objs[numobjs].counter = 0;
    if (numobjs < maxobjs) ++numobjs;
    return numobjs - 1;
}

void addinv(int invthing)
{
    if (pl.numinv >= maxinventory - 1) return;
    pl.inv[pl.numinv++] = (word)invthing;
    statmodflg |= mod_screen;
}

int takeinv(int invthing)
{
    int index, following;

    /* Android port addition, not upstream -- Unlimited Gems/Unlimited Keys cheat codes (JILL.H's
     * own jill_cheat_unlimited_gems comment has the whole Cheat Codes feature). Checked BEFORE the
     * search loop below, not just guarding the removal at the end of it -- inv_crystal/inv_redkey
     * are each spent by exactly one real gate (a map door/a locked door, JOBJ.c), and that gate's
     * own takeinv() call doubles as both the "do I have one" check AND the spend in a single call,
     * so a player sitting at exactly zero would otherwise still fail this call and never get
     * through even with the cheat on -- returning success here unconditionally (without touching
     * pl.inv[] at all) covers both cases at once, the same "opens from zero" fix GoT Android's own
     * Unlimited Keys needed. Every other invthing (the knife you throw, the internal
     * inv_jill/transform-clearing loop) falls straight through to the real search below,
     * completely untouched. */
    if (invthing == inv_redkey && jill_cheat_unlimited_keys) return 1;
    if (invthing == inv_crystal && jill_cheat_unlimited_gems) return 1;

    for (index = 0; index < pl.numinv; ++index) {
        if (pl.inv[index] == invthing) {
            for (following = index + 1; following < pl.numinv; ++following)
                pl.inv[following - 1] = pl.inv[following];
            --pl.numinv;
            statmodflg |= mod_screen;
            return 1;
        }
    }
    return 0;
}

int invcount(int invthing)
{
    int count = 0, index;
    for (index = 0; index < pl.numinv; ++index) count += pl.inv[index] == invthing;
    return count;
}

void initinv(void)
{
    pl.health = 6;
    while (takeinv(inv_jill)) { }
}

void moveobj(int n, int x, int y)
{
    if (y < 0) y = 0;
    else if (y > boardys * 16 - 16 - objs[n].yl)
        y = boardys * 16 - 16 - objs[n].yl;
    if (x < 0) x = 0;
    else if (x > boardxs * 16 - 16 - objs[n].xl)
        x = boardxs * 16 - 16 - objs[n].xl;
    objs[n].x = (word)x;
    objs[n].y = (word)y;
}

int standfloor(int n, int dx, int dy)
{
    int x, startx, endx, tile_y, newx, newy;
    newx = objs[n].x + dx; newy = objs[n].y + dy;
    if (((newy + objs[n].yl) & 15) != 0) return 0;
    tile_y = (newy + objs[n].yl - 1) / 16 + 1;
    startx = newx / 16; endx = (newx + objs[n].xl + 15) / 16;
    for (x = startx; x < endx; ++x) {
        int tile = board(x, tile_y);
        int flags = info[tile].flags;
        if ((flags & (f_playerthru | f_notstair)) == (f_playerthru | f_notstair)) return 0;
    }
    return 1;
}

int trymove(int n, int x, int y)
{
    int flags = f_playerthru;
    if (y > objs[n].y) flags |= f_notstair;
    if (cando(n, x, y, flags) == flags) { moveobj(n, x, y); return 1; }
    if (cando(n, objs[n].x, y, flags) == flags) { moveobj(n, objs[n].x, y); return 2; }
    if (cando(n, x, objs[n].y, flags) == flags) { moveobj(n, x, objs[n].y); return 4; }
    return 0;
}

int justmove(int n, int x, int y)
{
    if (cando(n, x, y, f_playerthru) != 0) { moveobj(n, x, y); return 1; }
    return 0;
}

int onscreen(int n)
{
    return objs[n].x + objs[n].xl >= gamevp->vpox && objs[n].y + objs[n].yl >= gamevp->vpoy &&
           objs[n].x <= gamevp->vpox + gamevp->vpxl && objs[n].y <= gamevp->vpoy + gamevp->vpyl;
}

int trymovey(int n, int x, int y)
{
    const int flags = f_playerthru | f_notstair;
    if (cando(n, x, y, flags) == flags) { moveobj(n, x, y); return 1; }
    if (cando(n, objs[n].x, y, flags) == flags) { moveobj(n, objs[n].x, y); return 1; }
    objs[n].xd = 0;
    return 0;
}

int crawl(int n, int dx, int dy)
{
    if (standfloor(n, dx, dy) &&
        cando(n, objs[n].x + dx, objs[n].y + dy, f_playerthru) == f_playerthru) {
        moveobj(n, objs[n].x + dx, objs[n].y + dy);
        return 1;
    }
    return 0;
}

void addscore(int score, int x, int y)
{
    int n = addobj(obj_score, x, y);
    if (n != 0) {
        objs[n].state = (word)score;
        objs[n].counter = 16;
        objs[n].xd = (word)(JILL_SIGN(x - objs[0].x) * 2);
        objs[n].yd = 3;
        setobjsize(n);
    }
    statmodflg |= mod_screen;
    pl.score = (ulongword)((longword)pl.score + score);
}

void addtext(const char *text, int objkind, int x, int y)
{
    int n = addobj(objkind, x, y);
    if (n != 0) {
        objs[n].counter = 64;
        objs[n].inside = jill_strdup(text);
        objs[n].xd = 2;
        objs[n].yd = -1;
        setobjsize(n);
    }
}

void sendtrig(int counter, int msg, int fromobj)
{
    int n;
    for (n = 0; n < numobjs; ++n) {
        int kind = (byte)objs[n].objkind;
        if ((kindflags[kind] & f_trigger) && objs[n].counter == counter)
            (void)kindmsg[kind](n, msg, fromobj);
    }
}

void setorigin(void)
{
    gamevp->vpox = (word)((objs[0].x - scrnxs * 8) & ~7);
    gamevp->vpox = (word)JILL_MAX(0, JILL_MIN((boardxs - scrnxs) * 16, gamevp->vpox));
    gamevp->vpoy = (word)(objs[0].y + 16 - scrnys * 8);
    gamevp->vpoy = (word)JILL_MAX(0, JILL_MIN((boardys + 1 - scrnys) * 16, gamevp->vpoy));
    oldscrollxd = oldscrollyd = 0;
}

int cando(int n, int x, int y, int flags)
{
    int cell_x, cell_y;
    int start_x = x / 16;
    int start_y = y / 16;
    int end_x = (x + objs[n].xl + 15) / 16;
    int end_y = (y + objs[n].yl + 15) / 16;
    int split_y = (objs[n].y + kindyl[(byte)objs[n].objkind] + 15) / 16;
    int stair_flags = f_notstair;
    int result = 0xffff;

    for (cell_y = start_y; cell_y < end_y; ++cell_y) {
        if (cell_y >= split_y) stair_flags = 0;
        for (cell_x = start_x; cell_x < end_x; ++cell_x)
            result &= (info[board(cell_x, cell_y)].flags | stair_flags) & flags;
    }
    return result;
}

int objdo(int n, int x, int y, int flags)
{
    int cell_x, cell_y;
    int result = 0xffff;
    int start_x = x / 16;
    int start_y = y / 16;
    int end_x = (x + objs[n].xl + 15) / 16;
    int end_y = (y + objs[n].yl + 15) / 16;

    for (cell_y = start_y; cell_y < end_y; ++cell_y)
        for (cell_x = start_x; cell_x < end_x; ++cell_x)
            result &= info[board(cell_x, cell_y)].flags & flags;
    return result;
}

void touchbkgnd(int n)
{
    int x, y, startx, starty, endx, endy;
    if (stateinfo[objs[n].state] & sti_invincible) return;
    startx = objs[n].x / 16; starty = objs[n].y / 16;
    endx = (objs[n].x + objs[n].xl + 15) / 16;
    endy = (objs[n].y + objs[n].yl + 15) / 16;
    for (y = starty; y < endy; ++y)
        for (x = startx; x < endx; ++x)
            if (info[board(x, y)].flags & f_msgtouch)
                (void)msg_block(x, y, msg_touch);
}

void purgeobjs(void)
{
    int source, destination = 0;
    for (source = 0; source < numobjs; ++source) {
        if (objs[source].objkind == obj_killme) free(objs[source].inside);
        else {
            if (source != destination) objs[destination] = objs[source];
            ++destination;
        }
    }
    numobjs = (word)destination;
}

void updbotmsg(void)
{
    if (botmsg[0] != '\0') {
        --bottime;
        if (bottime < 0) {
            botmsg[0] = '\0';
            statmodflg |= mod_screen;
        }
    }
}

void hitplayer(int n)
{
    if (objs[n].zaphold == 0 && !(stateinfo[objs[0].state] & sti_invincible))
        p_ouch(1, die_ash);
    objs[n].zaphold = 3;
}

int fishdo(int n, int x, int y)
{
    const int flags = f_playerthru | f_water;
    if (objdo(n, x, y, flags) == flags) { moveobj(n, x, y); return 1; }
    return 0;
}

void pointvect(int n1, int n2, int *xout, int *yout, int length)
{
    int x, y;
    x = objs[n1].x - objs[n2].x; y = objs[n1].y - objs[n2].y;
    if (x == 0) y = length * JILL_SIGN(y);
    else if (y == 0) x = length * JILL_SIGN(x);
    else if (abs(x) > abs(y)) { y = y * length / abs(x); x = length * JILL_SIGN(x); }
    else { x = x * length / abs(y); y = length * JILL_SIGN(y); }
    *xout = x;
    *yout = y;
}

int vectdist(int n1, int n2)
{
    return abs(objs[n1].x - objs[n2].x) + abs(objs[n1].y - objs[n2].y);
}

int trybreakwall(int n, int x, int y)
{
    int cell_x, cell_y, broken = 0;
    for (cell_x = x / 16; cell_x <= (x + objs[n].xl) / 16; ++cell_x) {
        for (cell_y = y / 16; cell_y <= (y + objs[n].yl) / 16; ++cell_y) {
            if (board(cell_x, cell_y) == BREAKWALL_TILE) {
                setboard(cell_x, cell_y, 0);
                if (broken++ == 0) {
                    explode1(cell_x * 16, cell_y * 16, 5);
                    snd_play(2, 49);
                }
            }
        }
    }
    return broken;
}

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
 * Android port note (this file, not upstream): pure game logic (the real player object --
 * input handling via GAMECTRL.H's dx1/dy1/fire1/fire2, the full state machine, drawing,
 * inventory, scrolling), no DOS or hardware dependency of any kind -- byte-identical to
 * upstream aside from this note. Renamed JPLAYER.C -> JPLAYER.c for the same CMake gotcha as
 * jill/GR.c (see that file's own header comment).
 */

#include "JILL.H"
#include "EPISODE.H"
#include "GAMECTRL.H"
#include "KEYBOARD.H"
#include "MUSIC.H"

#include <stdlib.h>

#if defined(JILL_EP3)
word inv_xfm[11] = { 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0 };
const char *inv_getmsg[11] = {
    "FOOF!",
    "USE KEYS TO OPEN DOORS",
    "YOU FOUND A KNIFE",
    "YOU FOUND A GEM",
    "POOF!",
    "ZZZZZZZT!",
    "WHOOSH!",
    "KABOOM!",
    "YOU FIND A THROWING STAR",
    "EXTRA JUMPING POWER",
    "SHIELD OF INVINCIBILITY"
};
#else
word inv_xfm[11] = { 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0 };
const char *inv_getmsg[11] = {
    "FOOF!",
    "USE KEYS TO OPEN DOORS",
    "YOU FOUND A KNIFE",
    "YOU FOUND A GEM",
    "POOF!",
    "ZZZZZZZT!",
    "A BAG OF COINS!",
    "KABOOM!",
    "YOU FIND A SPINNING BLADE",
    "EXTRA JUMPING POWER",
    "SHIELD OF INVINCIBILITY"
};
#endif

void calc_scroll(int vertical_delta)
{
    int horizontal_step = (objs[0].xl == 4 && (x_ourmode & 0xfe) != x_ega) ? 4 : 8;
    int top, bottom, wanted;

    if (objs[0].x < gamevp->vpox + 88 && gamevp->vpox >= 8)
        scrollxd = (word)-horizontal_step;
    if (objs[0].x > gamevp->vpox + scrnxs * 16 - 104 &&
        gamevp->vpox < (boardxs - scrnxs - 1) * 16)
        scrollxd = (word)horizontal_step;

    top = JILL_MAX(0, JILL_MIN((boardys - scrnys + 1) * 16,
                              objs[0].y - scrnys * 16 + 96));
    bottom = JILL_MAX(0, JILL_MIN((boardys - scrnys + 1) * 16,
                                 objs[0].y - 32));
    wanted = gamevp->vpoy + vertical_delta;
    if (wanted >= top && wanted <= bottom) scrollyd = (word)vertical_delta;
    else if (gamevp->vpoy > bottom) scrollyd = (word)(bottom - gamevp->vpoy);
    else if (gamevp->vpoy < top) scrollyd = (word)(top - gamevp->vpoy);
}

int msg_player(int n, int msg, int z)
{
    static const signed char climb_shape[7] = {
        0x18, 0x18, 0x19, 0x1a, 0x1a, 0x19, 0x19
    };
    static const signed char climb_y[7] = { 4, 0, 0, 6, 4, 4, 0 };
    static const signed char fire_sequence[21] = {
        0x48, 0x49, 0x48, 0x49, 0x48, 0x48, 0x49,
        0x48, 0x49, 0x49, 0x48, 0x48, 0x48, 0x49,
        0x49, 0x49, 0x4a, 0x4a, 0x4a, 0x4a, 0x4a
    };
    static const word fidget_sequence[4][4] = {
        { 0x13, 0x13, 0x13, 0x13 },
        { 0x13, 0x10, 0x12, 0x10 },
        { 0x13, 0x10, 0x12, 0x10 },
        { 0x12, 0x12, 0x12, 0x12 }
    };
    static const char *const fidget_message[4] = {
        "Look, an airplane!",
        "Are you just gonna sit there?",
        "Have you seen Jill anywhere?",
        "Hey,  your shoes are untied."
    };
    static word fidget_number;
    int shape = kindtable[obj_player] * 256;
    int c;
    int destination_y;
    int modified = 0;
    int peek_y = 0;
    int weapon;
    objtype *player = &objs[n];
    (void)z;

    if (msg == msg_draw) {
        switch (player->state) {
        case st_stand:
            if (player->statecount < 0) {
                if (player->info1 == 0)
                    shape += 0x3c;
                else
                    shape += 0x24 + (player->info1 > 0 ? 8 : 0);
                if (player->statecount == -4 || player->statecount == -5)
                    ++shape;
            } else if (player->yd < 0) {
                shape += 0x13;
            } else if (player->yd == 3) {
                shape += 0x3d;
            } else if (player->yd > 0) {
                shape += 0x12;
            } else if (player->info1 == 0 ||
                       (player->xd == 0 && player->statecount >= 3)) {
                if (player->statecount < 20 && player->info1 != 0)
                    shape += 0x14 + (player->info1 > 0);
                else if (player->statecount > 268)
                    shape += fidget_sequence[fidget_number][(player->statecount / 2) & 3];
                else if (player->statecount > 150)
                    shape += 0x11;
                else
                    shape += 0x10;
            } else if (player->substate == 8) {
                shape += 0x14 + (player->xd > 0);
            } else {
                shape += (player->substate & 7) + (player->xd > 0 ? 8 : 0);
            }
            break;

        case st_jumping:
            if (player->xd == 0) {
                if (player->substate == 0)
                    shape += 0x38;
                else if (player->substate == 1)
                    shape += 0x39;
                else if (player->yd <= 0)
                    shape += 0x3a;
                else
                    shape += 0x3c;
            } else {
                if (player->substate == 0)
                    shape += 0x20;
                else if (player->substate == 1)
                    shape += 0x21;
                else if (player->yd <= 0)
                    shape += 0x22;
                else
                    shape += 0x24;
                if (player->xd > 0) shape += 8;
            }
            break;

        case st_climbing:
            shape += climb_shape[player->substate];
            break;

        case st_begin:
            if (player->statecount < 16)
                shape += 0x13;
            else if (player->statecount < 24)
                shape += 0x10;
            else
                shape += 0x12;
            tempvp = *gamevp;
            tempvp.vpyl = (word)(player->y + player->yl - gamevp->vpoy);
            /* Android port change, not upstream -- jill_draw_player_shape() (JILL.H/JUNGLE.c)
             * transparently substitutes the cross-loaded "Jill Color" Enhancements override in
             * place of a plain drawshape() call, for every one of this function's OWN shape
             * (table 8, JILL_PLAYER_SHAPE_TABLE) draws -- see that declaration's own comment. The
             * one drawshape() call left untouched below (the invincibility-glow overlay, a
             * completely different table/shape number) is deliberately NOT one of Jill's own
             * body-color frames, so it stays a plain drawshape() call. */
            jill_draw_player_shape(&tempvp, shape, player->x,
                                    player->y + 33 - player->statecount);
            return 1;

        case st_die:
            if (player->statecount == 0) {
                shape += 0x42;
                jill_draw_player_shape(gamevp, shape, player->x, player->y);
                return 1;
            }
            switch (player->substate) {
            case die_ash:
                shape += 0x30 + player->statecount / 4;
                break;
            case die_bird:
                shape += 0x40 + player->statecount / 4;
                break;
            case die_fish:
                shape += fire_sequence[player->statecount];
                jill_draw_player_shape(gamevp, shape, player->x, player->y + 16);
                return 1;
            }
            break;
        }
        jill_draw_player_shape(gamevp, shape, player->x, player->y);
        if (player->counter > 0)
            drawshape(gamevp, 0x0e29 - player->counter / 2,
                      player->x, player->y + 26);
        return 0;
    }

    if (msg != msg_update)
        return 0;

    /* Android port addition, not upstream -- wootbeer's own report: "the left and right controls are
     * backwards when [mirror mode is] enabled". Mirror Mode (JILL.H's own jill_mirror_enabled
     * comment) is a pure presentation-layer horizontal pixel flip -- game LOGIC still sees dx1
     * exactly as the physical D-pad/touchscreen reports it, so pressing what now READS as visually
     * "right" on the mirrored screen was still moving Jill in the ORIGINAL, un-mirrored +x
     * direction, backwards from what the player sees. move_dx1 is dx1's own value, sign-flipped
     * only while Mirror Mode is actually active, and every DIRECTION-sensitive horizontal-input
     * read below (starting to walk, which way she faces on the jump-trigger, air control while
     * jumping, stepping off a vine) uses it in dx1's own place. Deliberately NOT the raw global
     * dx1 itself -- that stays untouched for every other reader this same tick (calc_scroll(),
     * msg_tiny()'s own separate movement logic further down this file, and any other object's own
     * msg_update), and only dx1 (horizontal) is touched -- dy1 (vertical) is left alone throughout,
     * matching wootbeer's own report ("left and right"), since Mirror Mode only ever flips the
     * horizontal axis. */
    {
        int move_dx1 = jill_mirror_enabled ? (word)-dx1 : dx1;

    switch (player->state) {
    case st_stand:
        if (!cando(n, player->x, player->y, f_playerthru)) {
            player->substate = -1;
            player->info1 = 0;
        }
        if (player->yd != 0) {
            player->yd = (word)(player->yd - JILL_SIGN(player->yd));
            modified = 1;
        }
        if (player->statecount < 0) {
            modified = 1;
            if (player->statecount == -1) player->statecount = 3;
        } else if (player->xd == 0) {
            if (move_dx1 != 0) {
                modified = 1;
                player->substate = 7;
                player->xd = move_dx1;
                player->statecount = 0;
                break;
            } else if (player->statecount > 300) {
                player->info1 = 0;
                player->statecount = 20;
                modified = 1;
            } else if (player->statecount >= 268) {
                modified = 1;
            } else if (player->statecount == 258) {
                fidget_number = (word)(rand() % 4);
                putbotmsg(fidget_message[fidget_number], 2);
                bottime = 25;
            } else if (player->statecount == 3) {
                modified = 1;
            }
            if (cando(n, player->x, (player->y & ~15) + 16,
                      f_playerthru | f_notstair) ==
                (f_playerthru | f_notstair)) {
                modified = 1;
                player->state = st_jumping;
                player->yd = 0;
                player->substate = 2;
            }
        } else if (move_dx1 != 0) {
            modified = 1;
            if (move_dx1 == player->xd) {
                /* Android port change, not upstream -- wootbeer's own "Mov Speed" Enhancements ask:
                 * scales Jill's own per-tick walking step by jill_movespeed_setting via
                 * jill_scale_by_speed() (JILL.H) -- both the cando() reach-check and the actual
                 * move have to use the SAME scaled distance, so it's computed once into a local
                 * rather than calling jill_scale_by_speed() twice (which would be harmless here
                 * since it's a pure function of two already-stable values, but a local reads
                 * clearer). Deliberately not touching the vertical peek/jump-trigger/climb-entry
                 * deltas elsewhere in this function -- see this file's own header note on what
                 * Movement Speed does and doesn't cover. move_dx1 (Mirror Mode's own
                 * sign-corrected dx1, see this function's own top), not raw dx1 -- see that
                 * comment for why. */
                int move_step = move_dx1 * jill_scale_by_speed(8, jill_movespeed_setting);
                if (cando(n, player->x + move_step, player->y,
                          f_playerthru)) {
                    player->x = (word)(player->x + move_step);
                    player->yd = 0;
                }
                player->substate = (word)((player->substate + 1) & 7);
                player->statecount = 0;
            } else {
                player->info1 = player->xd;
                player->xd = 0;
                player->statecount = 4;
            }
        } else {
            if (player->statecount >= 2 && player->substate != 0) {
                modified = 1;
                player->xd = 0;
                player->substate = 0;
                player->statecount = 0;
            }
        }
        if (cando(n, player->x, (player->y & ~15) + 16,
                  f_playerthru | f_notstair) == (f_playerthru | f_notstair)) {
            modified = 1;
            player->state = st_jumping;
            player->yd = 0;
            player->substate = 0;
        }

#if defined(JILL_EP1)
        if (key == ' ' && invcount(inv_coins)) {
            addobj(obj_token, player->x + player->info1 * 16,
                   player->y + 16);
            objs[numobjs - 1].counter = 6;
            if (trymove(numobjs - 1, objs[numobjs - 1].x,
                        objs[numobjs - 1].y)) {
                for (c = 0; c < numobjs - 1; ++c)
                    if (objs[c].objkind == obj_token && objs[c].counter == 6)
                        killobj(c);
            } else {
                killobj(numobjs - 1);
            }
        }
#endif

        if (fire1) {
            fire1off = 1;
            modified = 1;
            player->state = st_jumping;
            player->yd = (word)(-(16 + 4 * invcount(inv_jump)));
            player->substate = 0;
            /* Android port change, not upstream -- move_dx1 (Mirror Mode's own sign-corrected
             * dx1), not raw dx1 -- see this function's own top. Which way Jill initially faces
             * off a standing jump needs to match the mirrored screen too. */
            player->xd = move_dx1;
            snd_play(1, 1);
        } else if (dy1 != 0) {
            if ((player->x & 15) == 0 &&
                cando(n, player->x, player->y + dy1 * 4, f_playerthru) &&
                !cando(n, player->x, player->y + dy1 * 4, f_notvine)) {
                modified = 1;
                moveobj(n, player->x, player->y + dy1 * 4);
                player->state = st_climbing;
                player->substate = 3;
            }
            if (player->state == st_stand && player->xd == 0) {
                player->yd = (word)JILL_MIN(JILL_MAX(-3,
                    player->yd + dy1 * 2), 3);
                player->xd = 0;
                player->info1 = 0;
                player->statecount = 3;
                if (player->yd > 1) peek_y = 2;
                else if (player->yd < -1) peek_y = -2;
            }
        }
        break;

    case st_begin:
        modified = 1;
        player->xl = kindxl[obj_player];
        player->yl = kindyl[obj_player];
        if (player->statecount >= 32) {
            player->state = st_stand;
            player->xd = 0;
            player->info1 = 0;
            player->yd = 0;
            modified = 1;
        }
        break;

    case st_die:
        if (player->statecount == 0 && player->substate == die_fish) {
            player->yd = (word)JILL_MIN(player->yd + 2, 16);
            if (justmove(n, player->x, player->y + player->yd))
                player->statecount = -1;
        } else if (player->statecount == 1 && player->substate == die_fish) {
            player->xl = 32;
            player->yl = 32;
            moveobj(n, player->x, player->y + 16);
        } else if (player->statecount >= 20) {
            pl.health = 6;
            p_reenter(1);
        }
        ++player->statecount;
        return 1;

    case st_jumping:
        modified = 1;
        player->counter = 0;
        if ((player->x & 15) == 0 &&
            !cando(n, player->x, player->y, f_notvine)) {
            player->state = st_climbing;
            player->substate = 6;
            break;
        }
        if (++player->substate > 2) {
            /* Android port change, not upstream -- same jill_scale_by_speed()-based "Mov Speed"
             * scaling as st_stand's own move_step above, applied here to Jill's own horizontal air
             * control while jumping (reused across all three trymovey() calls below, so a jump's
             * own horizontal drift stays consistent throughout, not just its first tick).
             * Deliberately NOT scaling the vertical gravity accumulator two lines up (player->yd)
             * or the jump-trigger velocity in st_stand above -- see this file's own header note. */
            int move_step = move_dx1 * jill_scale_by_speed(8, jill_movespeed_setting);
            player->yd = (word)(player->yd + 2);
            if (player->yd > 16) player->yd = 16;
            if (move_dx1 != 0) {
                player->substate = 2;
                player->xd = move_dx1;
            }
            if (player->substate > 8) player->xd = 0;
            if (!trymovey(n, player->x + move_step,
                          player->y + player->yd)) {
                if (player->yd >= 0) {
                    c = 0;
                    if (((player->y + player->yl) & 15) == 0)
                        c = 1;
                    else if (!trymovey(n, player->x + move_step,
                                      ((player->y + player->yl) & ~15) +
                                      16 - player->yl))
                        c = 1;
                    if (c == 1) {
                        player->state = st_stand;
                        player->counter = 6;
                        player->statecount = (word)((player->yd >= 16 ? -7 : -4) +
                                                    (dx1 != 0));
                        player->xd = 0;
                        player->yd = 0;
                        snd_play(1, 2);
                    }
                } else {
                    destination_y = (player->y - 1) & ~15;
                    if (destination_y == player->y)
                        player->yd = 0;
                    else if (!trymovey(n, player->x + move_step,
                                      destination_y))
                        player->yd = 0;
                }
            }
        }
        break;

    case st_climbing:
        if (move_dx1 != 0) {
            /* Android port change, not upstream -- same jill_scale_by_speed()-based "Mov Speed"
             * scaling as st_stand's/st_jumping's own move_step above, applied here to Jill's own
             * horizontal step off a vine/ladder back onto solid ground. move_dx1 (Mirror Mode's
             * own sign-corrected dx1), not raw dx1 -- see this function's own top. */
            int move_step = move_dx1 * jill_scale_by_speed(8, jill_movespeed_setting);
            if (player->substate != 6 &&
                cando(n, player->x + move_step, player->y, f_playerthru)) {
                player->counter = 0;
                modified = 1;
                moveobj(n, player->x + move_step, player->y);
                player->state = st_jumping;
                player->substate = 2;
                player->xd = move_dx1;
                if (fire1) {
                    fire1off = 1;
                    player->yd = -12;
                    snd_play(1, 1);
                } else {
                    player->yd = -4;
                }
            }
        } else {
            player->xd = 0;
            if (player->substate == 6) player->substate = 0;
            if (dy1 != 0) {
                /* Android port change, not upstream -- same "Mov Speed" scaling as this state's own
                 * horizontal move_step above, applied here to Jill's own per-tick vertical climb
                 * distance. climb_y[]'s own values (JPLAYER.c's own static array, top of this
                 * function) are keyed to specific climb-animation substates, not a single flat
                 * constant the way the horizontal step is -- jill_scale_by_speed() is still safe to
                 * apply per-element here since it's a pure "half/double/unchanged" scale of
                 * whatever distance that substate already moves, same as scaling the flat +4
                 * climbing-down step just below. */
                destination_y = player->y;
                if (dy1 < 0)
                    destination_y -= jill_scale_by_speed(climb_y[player->substate], jill_movespeed_setting);
                else
                    destination_y += jill_scale_by_speed(4, jill_movespeed_setting);
                if (!cando(n, player->x, destination_y, f_playerthru)) {
                    if (dy1 > 0)
                        destination_y = (player->y & ~15) + 16;
                    else
                        destination_y = (player->y - 1) & ~15;
                    if (!cando(n, player->x, destination_y, f_playerthru))
                        destination_y = 0;
                }
                if (destination_y != 0) {
                    if (dy1 < 0) {
                        player->substate = (word)(player->substate + dy1);
                        if (player->substate >= 6) player->substate = 0;
                    } else {
                        player->substate = 2;
                    }
                    modified = 1;
                    moveobj(n, player->x, destination_y);
                    if (cando(n, player->x, player->y, f_notvine)) {
                        player->state = st_stand;
                        player->xd = 0;
                        player->info1 = 0;
                    }
                }
            }
            if (player->substate < 0)
                player->substate = 5;
            else if (player->substate > 6)
                player->substate = 6;
        }
        break;
    }
    } /* closes the move_dx1-scoping block opened just above this function's own switch */

    if (player->xd != 0) player->info1 = player->xd;
    if (fire2 && (stateinfo[player->state] & sti_canfire)) {
        fire2off = 1;
        if (player->info1 != 0) {
            c = 0;
            for (weapon = 0; weapon < numscrnobjs; ++weapon)
                c += objs[scrnobjs[weapon]].objkind == obj_spinblad;
            if (invcount(inv_blade) > c) {
                addobj(obj_spinblad, player->x + player->info1 * 12 + 4,
                       player->y + 2);
                /* Android port change, not upstream -- wootbeer's own "Attack Speed" Enhancements ask:
                 * scales the spinning blade's own raw throw velocity by jill_attackspeed_setting,
                 * via the same jill_scale_by_speed() helper Movement Speed uses below (see
                 * JILL.H's own comment). Deliberately just this one literal, not anything about the
                 * throw itself (its own spawn position, cooldown, or the invcount()/takeinv() logic
                 * choosing which weapon to throw) -- "speed" here means how fast the thrown object
                 * itself travels, not how often Jill can throw. */
                objs[numobjs - 1].xd = (word)(player->info1 * jill_scale_by_speed(6, jill_attackspeed_setting));
                snd_play(2, 34);
            } else if (takeinv(inv_knife)) {
                weapon = addobj(obj_knife,
                                player->x + player->info1 * 12 + 4,
                                player->y + 2);
                if (weapon != 0) {
                    if (cando(weapon, objs[weapon].x, objs[weapon].y,
                              f_playerthru) == f_playerthru) {
                        /* Android port change, not upstream -- same Attack Speed scaling as the
                         * spinning blade above, applied to the knife's own throw velocity. */
                        objs[numobjs - 1].xd = (word)(player->info1 * jill_scale_by_speed(8, jill_attackspeed_setting));
                        objs[numobjs - 1].statecount = 3;
                        snd_play(2, 3);
                    } else {
                        killobj(weapon);
                        addinv(inv_knife);
                        snd_play(2, 8);
                    }
                }
            } else {
                snd_play(2, 8);
            }
        }
    }
    ++player->statecount;
    if (player->counter != 0) {
        modified = 1;
        --player->counter;
    }
    touchbkgnd(0);
    calc_scroll(peek_y);
    return modified;
}

int msg_tiny(int n, int msg, int z)
{
    int shape;
    int tiny_count = ((x_ourmode & 0xfe) == x_ega) ? 3 : 1;
    objtype *player = &objs[n];
    (void)z;

    switch (msg) {
    case msg_draw:
        shape = 0x1000;
        if (player->xd != 0)
            shape += (player->statecount & 3) + (player->xd < 0 ? 8 : 0);
        else
            shape += (player->statecount & 1) + (player->yd < 0 ? 6 : 4);
        drawshape(gamevp, shape, player->x, player->y + 2);
        break;

    case msg_update:
        /* Android port addition, not upstream -- same Mirror Mode control-direction fix as
         * msg_player()'s own move_dx1 (see that function's own comment for the whole story) --
         * Tiny Jill reads dx1 directly for her own movement/facing too, so she'd have the exact
         * same "controls backwards" bug under Mirror Mode without this. Reassigned in place of dx1
         * below (not just read) since this function's own counter-suppression logic already
         * mutates dx1 itself as part of its "face the direction but don't actually step yet"
         * mechanic -- doing the same to this local instead keeps that mechanic byte-identical
         * while never touching the real global dx1. */
        {
        int move_dx1 = jill_mirror_enabled ? (word)-dx1 : dx1;
        if (move_dx1 != 0 || dy1 != 0) {
            player->statecount = (word)((player->statecount + 1) & 3);
            player->xd = move_dx1;
            if (move_dx1 != 0 && player->counter != 0) {
                if (move_dx1 > 0 && player->counter > 16) {
                    --player->counter;
                    move_dx1 = 0;
                } else if (move_dx1 < 0 && player->counter < 16) {
                    ++player->counter;
                    move_dx1 = 0;
                } else if (player->counter == 16) {
                    move_dx1 = (word)(move_dx1 * (tiny_count + 1));
                    player->counter = 0;
                }
            }
            player->yd = dy1;
            if (cando(n, player->x + move_dx1 * 2, player->y + dy1 * 2, f_back)) {
                player->x = (word)(player->x + move_dx1 * 2);
                player->y = (word)(player->y + dy1 * 2);
            }
        }
        }
        calc_scroll(0);
        if (scrollxd > 0) player->counter = (word)(tiny_count + 16);
        else if (scrollxd < 0) player->counter = (word)(16 - tiny_count);
        touchbkgnd(0);
        break;
    }
    return 1;
}

int msg_jillfish(int n, int msg, int z)
{
    static const word fish_shape[4] = { 0, 1, 2, 1 };
    int destination_x;
    int destination_y;
    int temporary_y;
    int on_water;
    int direction;
    int bullet;
    int shape;
    objtype *player = &objs[n];
    (void)z;

    switch (msg) {
    case msg_draw:
        shape = kindtable[obj_jillfish] * 256 + fish_shape[player->counter];
        if (player->substate > 0) shape += 3;
        drawshape(gamevp, shape, player->x, player->y);
        break;

    case msg_update:
        player->counter = (word)((player->counter + 1) & 3);
        on_water = fishdo(n, player->x, player->y);
        if (player->xd != 0) player->substate = player->xd;
        /* Android port change, not upstream -- same Mirror Mode control-direction fix as
         * msg_player()'s own move_dx1 (see that function's own comment). */
        player->xd = (word)((jill_mirror_enabled ? (word)-dx1 : dx1) * 8);
        if (on_water) {
            player->yd = (word)(player->yd + dy1 * 2 + (player->yd < 2));
            if (rand() % 4 == 0)
                addobj(obj_bubble, player->x + 6, player->y - 2);
            if (player->yd < -8) player->yd = -8;
            if (player->yd > 8) player->yd = 8;
        } else {
            player->yd = (word)(player->yd + 2);
            if (player->yd < -16) player->yd = -16;
            if (player->yd > 16) player->yd = 16;
        }

        if (fire1 && on_water) {
            fire1off = 1;
            snd_play(1, 37);
            player->yd = -2;
        }
        if (fire2) {
            fire2off = 1;
            snd_play(1, 38);
            bullet = addobj(obj_fishbullet,
                            player->x - 8 + (player->substate < 0 ? 32 : 0),
                            player->y);
            direction = ((player->substate > 0) - (player->substate < 0)) * 8;
            direction += ((player->xd > 0) - (player->xd < 0)) * 4;
            objs[bullet].xd = (word)direction;
        }

        if (player->yd > 8) player->yd = 8;
        else if (player->yd < -8) player->yd = -8;
        destination_x = player->x + player->xd;
        destination_y = player->y + player->yd;
        if (!justmove(n, player->x, destination_y)) {
            if (player->yd < 0) temporary_y = (destination_y + 16) & ~15;
            else if (player->yd > 0)
                temporary_y = (destination_y & ~15) + 16 - player->yl;
            if (player->yd != 0 && !justmove(n, player->x, temporary_y))
                player->yd = 0;
        }
        fishdo(n, destination_x, player->y);
        calc_scroll(0);
        break;
    }
    return 1;
}

int msg_jillspider(int n, int msg, int z)
{
    (void)n;
    (void)z;
    return msg == msg_update ? 0 : msg;
}

int msg_jillfrog(int n, int msg, int z)
{
    int destination_x;
    int destination_y;
    int shape;
    objtype *player = &objs[n];
    (void)z;

    switch (msg) {
    case msg_draw:
        shape = kindtable[obj_jillfrog] * 256 + (player->info1 > 0 ? 0 : 3);
        if (player->state == 1) shape += player->yd > 0 ? 2 : 1;
        drawshape(gamevp, shape, player->x, player->y);
        break;

    case msg_update:
        if (player->xd != 0) player->info1 = player->xd;
        /* Android port change, not upstream -- same Mirror Mode control-direction fix as
         * msg_player()'s own move_dx1 (see that function's own comment). */
        if ((player->x & 7) != 0 || player->xd == 0)
            player->xd = (word)((jill_mirror_enabled ? (word)-dx1 : dx1) * 4);
        else
            player->xd = (word)((jill_mirror_enabled ? (word)-dx1 : dx1) * 8);

        if (cando(n, player->x, player->y + 1, 3) != 3) {
            if (fire1 || fire2) {
                player->yd = -12;
                snd_play(1, 43);
            } else {
                player->yd = -8;
                snd_play(1, 18);
            }
        } else {
            if (++player->yd > 12) player->yd = 12;
        }

        destination_x = player->x + player->xd;
        destination_y = player->y + player->yd;
        if ((trymove(n, destination_x, destination_y) & 3) == 0) {
            if (player->yd >= 0) {
                destination_y = (destination_y & ~15) + 16 - player->yl;
                (void)trymove(n, destination_x, destination_y);
                player->counter = 0;
            }
            player->yd = 0;
        }
        calc_scroll(0);
        break;
    }
    return 1;
}

int msg_jillbird(int n, int msg, int z)
{
    static const word bird_shape[6] = { 0, 1, 2, 3, 2, 1 };
    int destination_x;
    int destination_y;
    int temporary_y;
    int direction;
    int shot;
    int shape;
    objtype *player = &objs[n];
    (void)z;

    switch (msg) {
    case msg_draw:
        shape = kindtable[obj_phoenix] * 256 + bird_shape[player->counter];
        if (player->substate < 0) shape += 4;
        drawshape(gamevp, shape, player->x, player->y);
        break;

    case msg_update:
        if (player->xd != 0) player->substate = player->xd;
        if (++player->counter >= 6) player->counter = 0;
        /* Android port change, not upstream -- same Mirror Mode control-direction fix as
         * msg_player()'s own move_dx1 (see that function's own comment). */
        if ((player->x & 7) != 0 || player->xd == 0)
            player->xd = (word)((jill_mirror_enabled ? (word)-dx1 : dx1) * 4);
        else
            player->xd = (word)((jill_mirror_enabled ? (word)-dx1 : dx1) * 8);
        ++player->yd;

        if (fire1 || fire2) {
            fire1off = 1;
            snd_play(1, 15);
            player->yd = -6;
        }
        if (fire2) {
            fire2off = 1;
            snd_play(1, 34);
            direction = (player->substate > 0) - (player->substate < 0);
            shot = addobj(obj_firebullet, player->x + direction * 24, player->y);
            objs[shot].xd = (word)(direction * 8);
            objs[shot].yd = (word)(dy1 * 2);
        }

        if (player->yd > 8) player->yd = 8;
        else if (player->yd < -8) player->yd = -8;
        destination_x = player->x + player->xd;
        if (!justmove(n, destination_x, player->y)) destination_x = player->x;
        if (player->yd != 0) {
            destination_y = player->y + player->yd;
            if (!justmove(n, destination_x, destination_y)) {
                if (player->yd < 0) temporary_y = (destination_y + 16) & ~15;
                else temporary_y = (destination_y & ~15) + 16 - player->yl;
                if (temporary_y == destination_y || !justmove(n, destination_x, temporary_y))
                    player->yd = 0;
            }
        }
        if (objdo(n, player->x, player->y, f_weapon) != f_weapon)
            p_ouch(0x100, 1);
        calc_scroll(0);
        break;
    }
    return 1;
}

int playerxfm(int transform)
{
    int index;
    int result = 0;
    int new_kind = obj_player;
    int old_kind = objs[0].objkind;
    int old_width = objs[0].xl;
    int old_height = objs[0].yl;
    int new_x;
    int new_y;

    if (transform == inv_frog) new_kind = obj_jillfrog;
    else if (transform == inv_bird) new_kind = obj_jillbird;
    else if (transform == inv_fish) new_kind = obj_jillfish;
    if (objs[0].objkind == new_kind) return 0;

    objs[0].objkind = (sbyte)new_kind;
    objs[0].xl = kindxl[new_kind];
    objs[0].yl = kindyl[new_kind];
    new_x = objs[0].x & ~7;
    new_y = objs[0].y + old_height - objs[0].yl;
    if (cando(0, new_x, new_y, f_playerthru)) result = 1;
    else {
        new_y = objs[0].y;
        if (cando(0, new_x, new_y, f_playerthru)) result = 1;
    }
    if (result) {
        addinv(transform);
        objs[0].y = (word)new_y;
        objs[0].x = (word)new_x;
        objs[0].state = 0;
        objs[0].substate = 0;
        objs[0].counter = 0;
        objs[0].xd = 0;
        objs[0].yd = 0;
        for (index = 0; index < 11; ++index)
            if (inv_xfm[index])
                while (takeinv(index)) { }
        explode1(objs[0].x, objs[0].y, 10);
        putbotmsg(inv_getmsg[transform], 7);
    } else {
        objs[0].objkind = (sbyte)old_kind;
        objs[0].xl = (word)old_width;
        objs[0].yl = (word)old_height;
    }
    return 0;
}

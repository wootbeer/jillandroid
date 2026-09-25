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

#ifndef JILL_H
#define JILL_H

#include <stdio.h>
#include "RECOVERY.H"
#include "GR.H"
#include "JOBJ.H"

#define boardxs 128
#define boardys 64
#define normxs 15
#define normys 11
#define fullxs 20
#define fullys 13
#define numinfotypes 600
#define maxobjs 256
#define maxscrnobjs 192
#define maxinventory 16

#define mod_screenonly ((uword)0x8000)
#define mod_screen     ((uword)0xc000)
#define mod_virtual    ((uword)0x4000)
#define mod_page0      ((uword)0x4000)
#define mod_page1      ((uword)0x8000)

#define f_playerthru 0x0001
#define f_notstair   0x0002
#define f_notvine    0x0004
#define f_msgtouch   0x0008
#define f_msgdraw    0x0010
#define f_msgupdate  0x0020
#define f_inside     0x0040
#define f_front      0x0080
#define f_trigger    0x0100
#define f_back       0x0200
#define f_killable   0x0400
#define f_fireball   0x0800
#define f_always     0x1000
#define f_water      0x2000
#define f_weapon     0x4000

enum { msg_draw, msg_touch, msg_update, msg_trigger, msg_trigoff, msg_trigon };
enum { st_stand, st_jumping, st_climbing, st_die, st_begin };
enum { die_ash, die_bird, die_fish };
enum { sti_canfire = 1, sti_invincible = 2 };

typedef struct infotype {
    uword sh;
    uword flags;
    char *name;
} infotype;

/* Exact 31-byte DOS level record.  The far pointer is retained as an opaque
   serialized value; loadboard resolves it through the following string stack. */
#pragma pack(push, 1)
typedef struct JillDiskObject {
    byte objkind;
    word x, y, xd, yd, xl, yl;
    word state, substate, statecount, counter, objflags;
    ulongword string_pointer;
    word info1, zaphold;
} JillDiskObject;

typedef struct JillDiskPlayer {
    word level;
    word health;
    word numinv;
    word inv[maxinventory];
    ulongword score;
    word ouched;
    ulongword oldscore;
    byte pad[22];
} JillDiskPlayer;
#pragma pack(pop)

typedef struct objtype {
    sbyte objkind;
    word x, y, xd, yd, xl, yl;
    word state, substate, statecount, counter;
    uword objflags;
    char *inside;
    word info1, zaphold;
} objtype;

typedef struct pltype {
    word level;
    word health;
    word numinv;
    word inv[maxinventory];
    ulongword score;
    word ouched;
    ulongword oldscore;
    byte pad[22];
} pltype;

typedef int (*object_message_fn)(int n, int msg, int z);

extern vptype *gamevp, *statvp, *cmdvp;
extern vptype botvp, tempvp;
extern word scrnxs, scrnys;
extern uword bd[boardxs][boardys];
extern infotype info[numinfotypes];
extern object_message_fn kindmsg[numobjkinds];
extern word kindxl[numobjkinds], kindyl[numobjkinds];
extern const char *kindname[numobjkinds];
extern uword kindflags[numobjkinds];
extern word kindtable[numobjkinds], kindscore[numobjkinds];
extern word inv_shape[11];
extern word inv_xfm[11];
extern const char *inv_getmsg[11];
extern objtype objs[maxobjs + 2];
extern word numobjs, numscrnobjs;
extern word scrnobjs[maxscrnobjs + 1];
extern pltype pl;

extern word scrollxd, scrollyd, oldscrollxd, oldscrollyd, oldx0, oldy0;
extern word gameover, gamecount, statmodflg, designflag, peeky;
extern word stateinfo[6];
extern char newlevel[32], curlevel[32];
extern const char *leveltxt[32];
extern char botmsg[60];
extern word botcol, bottime;

/* Android port addition, not upstream -- jill_display_mode/jill_active_display_mode/JILL_DISPLAY_*
 * are JUNGLE.c's own (see that file's own header comment on the whole Display Mode feature);
 * declared here, not just in JUNGLE.c, because JMAN.c's own refresh() needs to check
 * jill_active_display_mode and call jill_minimal_hud_tick() (also JUNGLE.c, see its own comment)
 * at exactly the right point in its own per-tick draw order -- see refresh()'s own new call site
 * for why it has to be there.
 *
 * Two separate variables, not one, since wootbeer's own follow-up ask: "this option persists to the
 * main menu, I want to keep the default look for the main menu if possible" -- jill_display_mode
 * is the player's own persisted preference (config.dat, the pause menu's own DISPLAY MODE row
 * text, and what jmenu()/dodemo() restore Default from), while jill_active_display_mode is
 * whatever's actually driving rendering *right now* -- forced to JILL_DISPLAY_DEFAULT outside real
 * gameplay (jmenu()'s own menu loop and dodemo()'s own attract-mode playback, see each one's own
 * comment) regardless of jill_display_mode, then restored to jill_display_mode the moment play()
 * actually starts. Every render-time check that used to read jill_display_mode directly
 * (drawgamewin()/drawcmds()/drawstats() here, refresh()'s own call site below) reads
 * jill_active_display_mode instead -- jill_display_mode itself is now only ever read for its
 * *persisted-preference* meaning (the pause menu's own status text, and case 'V' cycling it). */
extern int jill_display_mode;
extern int jill_active_display_mode;
#define JILL_DISPLAY_DEFAULT 0
#define JILL_DISPLAY_FORCE43 1
#define JILL_DISPLAY_MINIMAL 2
void jill_minimal_hud_tick(void);

/* Android port addition, not upstream -- wootbeer's own ask, mirroring the God of Thunder Android
 * port's own Hammer/Armor Color Enhancements option: "add an enhancement like we did in god of
 * thunder for his armor/hammer color, except now it will be jill's color, we can choose her color
 * from episode 1 2 or 3, or the default for what that episode is supposed to be." Unlike GoT's own
 * Hammer/Armor Color (a handful of sprite tiers already sitting in the ONE shared GOTRES.DAT every
 * episode has fully loaded), Jill's three episodes are three entirely separate .sha files, each
 * holding only its OWN look for Jill -- so "Episode 2's color" while playing Episode 1 means
 * briefly opening jill2.sha's own file (a true sibling directory, "../ep2/jill2.sha" -- see
 * jill_jni_bridge.c's own jill_run_game()/jill_episode_dir_from_sha_path() comment for why that
 * relative path always resolves) and decoding just ONE table out of it -- JILL_PLAYER_SHAPE_TABLE,
 * Jill's own player-sprite animation table, the same table number in every episode's own .sha (see
 * that define's own comment for the citation). See jill_refresh_color_override()'s own comment
 * (JUNGLE.c) for the whole mechanism, and JPLAYER.c's own msg_player() for where the swap actually
 * happens on screen, through jill_draw_player_shape() below.
 *
 * 0 = DEFAULT (this episode's own normal look, no cross-loading at all); 1/2/3 = that episode
 * NUMBER's own look, deliberately reusing the episode number itself as the setting's own value
 * (not a separate enum) so both the pause menu's own cycling and jill_refresh_color_override()'s
 * own "../epN/jillN.sha" path lookup can use it directly. Persisted the same way/place as
 * jill_display_mode (config.dat, JillAppConfig) -- see that struct's own comment. */
extern int jill_color_setting;
#define JILL_COLOR_DEFAULT 0

/* The one shape table every episode's own player sprite lives at -- JOBJ.c's own initobjinfo()
 * header comment confirms it ("JILL2.EXE's stripped initobjinfo assigns the same... shape
 * tables... as Episode 1"), and its own recovered_table[obj_player] entry is 8 in every episode's
 * build (one shared, non-#if-branched function). Not read from kindtable[obj_player] itself here --
 * that array isn't populated until initobjinfo() runs, well after jill_refresh_color_override()
 * needs to run once at startup -- this is simply the same fixed value that lookup always resolves
 * to anyway, used directly so there's no ordering dependency on initobjinfo() at all. */
#define JILL_PLAYER_SHAPE_TABLE 8

/* The alternate-episode player-sprite table jill_refresh_color_override() (JUNGLE.c) decodes into
 * when jill_color_setting asks for a different episode's own look than this build's own -- NULL
 * whenever no override is active (Default, the setting matches this episode's own number, or that
 * episode's own asset simply isn't present on this device, see jill_refresh_color_override()'s own
 * comment on that fallback). Read by jill_draw_player_shape() below, in the exact same in-memory
 * format xlate_table() (SHM.c) produces for shm_tbladdr[]/shm_flags[]. */
extern byte *jill_altcolor_addr;
extern word jill_altcolor_flags;

/* Draws one of Jill's own player-sprite frames -- shape_number encoded exactly like drawshape()'s
 * own `n` (JILL_PLAYER_SHAPE_TABLE in the high byte, the frame index in the low byte, matching
 * msg_player()'s own `shape` variable, JPLAYER.c) -- transparently substituting
 * jill_altcolor_addr's own cross-loaded frame instead of the normal one whenever an override is
 * active. JPLAYER.c's own msg_player() calls this everywhere it used to call drawshape() directly
 * for Jill's own body; every OTHER drawshape() call in that function (the invincibility-glow
 * overlay, a completely different table) is untouched -- see that function's own call sites.
 * Implemented in JUNGLE.c, alongside jill_refresh_color_override() -- GR.c's own
 * drawshape_from_table() (GR.H) is the generic, reusable primitive this calls; this is the one
 * small piece of glue that's specifically about Jill's own color override, not a general graphics
 * primitive. */
void jill_draw_player_shape(vptype *vp, int shape_number, int x, int y);

/* Android port addition, not upstream -- three independent SLOW/DEFAULT/FAST speed dials, wootbeer's
 * own follow-up ask after Jill Color: "let's do one for 'enemy speed'... then do one for jill's
 * attack speed... then have one for jill's movement speed". DEFAULT is deliberately value 0, the
 * same "a freshly zero-initialized global, or an old/short display.dat that predates these fields,
 * both land on Default" reasoning jill_color_setting's own comment above already establishes.
 * jill_scale_by_speed() below is the shared helper JPLAYER.c uses to apply one of these to a raw
 * per-tick pixel-delta or velocity constant; JMAN.c's own updobjs() uses a different technique for
 * jill_enemyspeed_setting specifically (repeating or skipping whole per-object msg_update calls,
 * not scaling a constant -- there's no single "distance per tick" constant at that call site the
 * way there is here, since every enemy kind has its own AI handler with its own movement logic --
 * see updobjs()'s own comment for the whole reasoning). Persisted the same way/place as
 * jill_display_mode/jill_color_setting (config.dat, JillAppConfig, JUNGLE.c). */
#define JILL_SPEED_DEFAULT 0
#define JILL_SPEED_SLOW 1
#define JILL_SPEED_FAST 2
extern int jill_enemyspeed_setting;
extern int jill_attackspeed_setting;
extern int jill_movespeed_setting;

/* Halves base at JILL_SPEED_SLOW, doubles it at JILL_SPEED_FAST, leaves it unchanged at
 * JILL_SPEED_DEFAULT (or any other value). Implemented in JUNGLE.c, alongside the speed settings
 * themselves -- not static there, since JPLAYER.c needs it too. */
int jill_scale_by_speed(int base, int setting);

/* Android port addition, not upstream -- wootbeer's own ask: "would it be possible to add a mirror mode
 * option as well like we did in god of thunder? just flip everything on the axis except for text,
 * menus, and gui of course." A simple on/off toggle (OFF=0 for the same zero-init/backward-compat
 * reasoning as JILL_SPEED_DEFAULT above) -- unlike the three speed dials, wootbeer didn't ask for
 * multiple tiers here, just a flip. The actual per-frame horizontal flip is a presentation-layer
 * effect in HOSTANDROID.c's own host_present(), scoped to *gamevp's own current screen rect only
 * (see host_set_mirror_rect()'s own comment, HOSTSDL.H) -- text/menus/GUI live in the CONTROLS/
 * INVENTORY side panel and botvp's own bottom message strip, both outside that rect in Default/
 * Force 4:3 mode, so neither is ever touched there, exactly wootbeer's own ask. Minimal UI mode (which
 * deliberately expands *gamevp to cover the whole screen) and every text/menu/GUI screen that draws
 * directly into *gamevp's own shared pixels (the HUD strip, level-entry messages, askquit()/
 * pausemenu() and everything reachable from it) each instead push their own transient or persistent
 * mirror-exclude-rect (host_set_mirror_exclude_rect()/host_set_mirror_hud_exclude_rect(), HOSTSDL.H)
 * covering just their own on-screen window, so the world underneath stays mirrored while only the
 * UI itself is excluded -- see each of those exclude-rect push sites' own comments (JUNGLE.c) for
 * the individual stories. An earlier round of this feature instead suspended Mirror Mode outright
 * (host_set_mirror_suspended(), HOSTSDL.H) for as long as askquit()/pausemenu() was on screen, a
 * blunter fix wootbeer's own follow-up bug report replaced: "when entering pause menu while mirror mode
 * is on, everything flips back while the menu is open. and then once it closes there's some
 * mirrored artifacts and such leftover" -- suspending un-mirrored the WHOLE screen for that whole
 * duration, not just the menu's own small box. */
#define JILL_MIRROR_OFF 0
#define JILL_MIRROR_ON 1
extern int jill_mirror_enabled;

/* Android port addition, not upstream -- wootbeer's own ask, mirroring the God of Thunder Android
 * port's own Cheat Codes menu exactly: "let's flesh out the cheat sub-menu now, also modeled after
 * the god of thunder cheat menu... I want the cheats to function the same way as in god of thunder,
 * off by default, new game, reloads etc." A Cheat Codes submenu under the pause menu's own CHEATS
 * row (previously an inert placeholder, see pausemenu()'s own header comment), four independent
 * session-only toggles. All four default OFF (plain ints, no static initializer -- the same
 * zero-init convention every other Enhancements setting above already relies on) and are
 * deliberately NOT written to config.dat or any save file -- these are per-session testing aids,
 * not a persisted player preference or real game progress, exactly the "temporary, for testing"
 * framing GoT Android's own Cheat Codes row had from the start. jill_cheats_reset() (JMAN.c) forces
 * all four back off (and, for All Items, un-grants whatever it added) at every real session
 * boundary -- a brand new game (jmenu()'s own 'P' case, JUNGLE.c) and a loaded save (loadgame(),
 * JUNGLE.c, so both jmenu()'s own 'R' case and pausemenu()'s own 'L' case get it for free) both
 * call it -- deliberately NOT hooked into an ordinary level-to-level transition (loadboard()/
 * zapobjs()/initobjs(), which fire on every level, not just a fresh session), so a player relying
 * on these for testing doesn't have them silently switched back off just by walking through a door.
 *
 * jill_cheat_unlimited_gems/jill_cheat_unlimited_keys -- checked directly inside takeinv() (JMAN.c)
 * for exactly the two inventory kinds a real door/gate spends (inv_crystal/inv_redkey): while on,
 * takeinv() returns success for that kind without actually removing anything, and does so even at
 * zero held, mirroring GoT Android's own "the resource-check gate is separate from the resource-
 * spend" lesson (a locked door's own takeinv(inv_redkey) call doubles as both the have-I-got-one
 * check AND the spend, so a player sitting at exactly zero needs the cheat to short-circuit the
 * check too, not just protect a copy already held). Every OTHER inventory kind's own takeinv() call
 * (the knife you throw, the internal inv_jill/transform-clearing loop) is completely untouched.
 *
 * jill_cheat_god_mode -- checked at the very top of p_ouch() (JMAN.c), the one and only place this
 * port ever reduces pl.health toward a lethal value or arms the death state (confirmed by grep --
 * every p_ouch() caller in the whole codebase). Clamps health back up to 1 instead of ever letting
 * it reach 0, the same "health floor of 1, not a take-no-damage cheat" semantics GoT Android's own
 * God Mode uses (wootbeer's own ask: "function the same way as in god of thunder") -- health still
 * visibly drops and the ordinary non-fatal hit flash/sound still plays, this just stops a hit from
 * ever actually being fatal.
 *
 * jill_cheat_all_items -- a direct one-shot inventory mutation (jill_cheat_set_all_items(), JMAN.c)
 * applied the instant the row toggles, not a live per-tick gate like the other three: adds exactly
 * one each of a fixed "useful item" set -- knife, blade, jump boots, invincibility (deliberately
 * NOT gems/keys, already their own dedicated cheats above, and NOT the frog/bird/fish transformation
 * potions, which change Jill's own object kind outright rather than sitting passively in inventory,
 * see playerxfm(), JPLAYER.c) -- for every kind the player doesn't already hold at least one of, and
 * remembers exactly which kinds it granted so toggling back off removes only those specific grants.
 * Any copy the player already held, or picks up legitimately at any point (including a second one of
 * a kind the cheat already granted), is never at risk of being taken away -- the same precision GoT
 * Android's own "added mask" gets for its own bitmask inventory, just tracked per item-kind count
 * instead of per bit. Session-only like the other three: never written to a save file
 * (encode_player(), JUNGLE.c, excludes exactly the cheat's own still-outstanding grants from every
 * on-disk pl.inv[], via jill_cheat_all_items_owed() below), and jill_cheats_reset() clears both the
 * toggle and the tracking at every real session boundary. */
extern int jill_cheat_unlimited_gems;
extern int jill_cheat_unlimited_keys;
extern int jill_cheat_god_mode;
extern int jill_cheat_all_items;
void jill_cheat_set_all_items(int enabled);
int jill_cheat_all_items_owed(int invthing);
void jill_cheats_reset(void);

/* Android port addition, not upstream -- pausemenu()'s new "Remap Gamepad" submenu (JUNGLE.c's
 * case 'G', previously an inert placeholder -- see that function's own header comment). wootbeer's own
 * explicit ask: "let's again model after the one we made for god of thunder. have all the same
 * gamepad buttons available to be mapped that god of thunder has, and have them able to be mapped
 * to all of the controls jill of the jungle uses. of course the default controls will be what we
 * have now, and we will have the same apply, default, cancel options, etc as god of thunder. and
 * of course save it in our config.dat file." Jill only has two real remappable gameplay actions --
 * JUMP (fire1/key_shift) and THROW (fire2/key_alt), unlike the God of Thunder Android port's three
 * (Fire/Magic/Select Item) -- everything else about the design mirrors that port's own
 * got_controls.c/got_menu.c exactly, just at this smaller scale: see jill_jni_bridge.c's own
 * "Gamepad remapping" section for the native-side table/capture-state-machine (the module these
 * action indices and the shared JILL_GAMEPAD_UNBOUND_KEYCODE sentinel are for), and JUNGLE.c's own
 * pausemenu() case 'G' for the submenu UI (staged bindings, capture step, Reset/Cancel/Apply).
 *
 * These indices are the one thing JUNGLE.c and jill_jni_bridge.c's own remap module both need
 * to agree on (hand-synced, the same cross-file-constant policy this whole project already uses --
 * see JILL_SOUND_GAIN_STEPS's own precedent) -- 0/1/2, not an enum, to keep both sides' own switch/
 * array-index usage as plain as GoT's own kGamepadRemapActions[]/action-int convention already is. */
#define JILL_GAMEPAD_ACTION_JUMP 0
#define JILL_GAMEPAD_ACTION_THROW 1
/* Android port addition, not upstream -- wootbeer's own follow-up ask, once the on-screen touch
 * controls were in: "Episode 1's spacebar-bound coin-toss action, yes we need to add this to a
 * touch button if we can't do it otherwise. I'm assuming this action already works with gamepad
 * controls though?" It didn't -- JPLAYER.c's own real coin-toss check (`key == ' '`, itself already
 * `#if defined(JILL_EP1)` since real upstream only ever wired this into Episode 1) reads Jill's
 * one-shot keyboard queue directly; no physical gamepad button, capturable or otherwise, was ever
 * wired to send key_space (32) before this. A third remappable action closes that gap the same way
 * JUMP/THROW already work, `#if defined(JILL_EP1)`-gated for the same reason JPLAYER.c's own check
 * is -- Episodes 2/3 have no coin-toss mechanic at all (no inv_coins use outside Episode 1's own
 * jillN.dat content), so a "COIN TOSS" remap row or touch button would be a dead, confusing option
 * in those builds. Since this project builds one native library per episode already (CMakeLists.txt's
 * own JILLHOST_SOURCES comment), JILL_EP1 is a real compiler-defined macro in every translation unit
 * of the jillhost1 target specifically -- the exact same conditional-compilation idiom JPLAYER.c/
 * JOBJ.c/etc. already use throughout this codebase, not a new pattern. See jill_jni_bridge.c's own
 * kJillGamepadRemapActions[] for this action's own default binding (JILL_GP_BUTTON_X, previously
 * unbound to anything) and jill_controls.c's own BTN_COIN_TOSS for the new touch button (a third
 * cell in the same JUMP/THROW vertical stack, mirroring the God of Thunder Android port's own
 * SELECT-above-MAGIC-above-FIRE three-stack). */
#if defined(JILL_EP1)
#define JILL_GAMEPAD_ACTION_COIN_TOSS 2
#define JILL_GAMEPAD_NUM_ACTIONS 3
#else
#define JILL_GAMEPAD_NUM_ACTIONS 2
#endif
/* Sentinel for "no physical button bound to this action" -- matches got_controls.c's own
 * GAMEPAD_UNBOUND exactly (hand-synced constant, same policy as the indices above); never a real
 * Android KeyEvent keycode, which are always >= 0. */
#define JILL_GAMEPAD_UNBOUND_KEYCODE (-1)

/* jill_jni_bridge.c's own remap-table API -- see that file's own "Gamepad remapping" section for
 * what each of these actually does; JUNGLE.c's pausemenu() (case 'G') and jill_load_app_config()/
 * jill_save_app_config() are the only callers. */
int jill_gamepad_remap_bound_keycode(int action);
void jill_gamepad_remap_set_bound_keycode(int action, int keycode);
int jill_gamepad_remap_default_keycode(int action);
const char *jill_gamepad_remap_action_label(int action);
const char *jill_gamepad_remap_button_name(int keycode);
int jill_gamepad_remap_is_valid_keycode(int keycode);
void jill_gamepad_remap_start_capture(void);
void jill_gamepad_remap_cancel_capture(void);
int jill_gamepad_remap_poll_capture(void);

/* jill_controls.c's own on-screen touch controls API -- see that file's own header comment for the
 * whole "Touch Options" pause-menu submenu feature (wootbeer's own explicit ask: "now to work on our
 * on-screen touch control support... let's also get our 'touch options' sub-menu going, the same
 * one we had in god of thunder"). Declared here (not extern-declared directly in JUNGLE.c the way
 * the God of Thunder Android port's own got_menu.c/got_main.c extern-declare got_controls.c's API)
 * to match this project's own established pattern of every jill_jni_bridge.c-hosted API just above
 * -- one shared header, not a per-caller extern block. JUNGLE.c's pausemenu() (case 'G', now
 * branching on jill_controls_gamepad_connected()) and jill_load_app_config()/jill_save_app_config()
 * are the only callers on this side; HOSTANDROID.c calls jill_controls_init()/jill_draw_touch_buttons()
 * directly via jill_controls.h, not through here. */
#include "jill_controls.h"

/* Android port change, not upstream -- was a plain macro, `(bd[(x)][(y)] & 0x3fffU)`, with no bounds
 * checking at all (unlike setboard()/modboard() just below, which both explicitly clamp), matching
 * upstream exactly. wootbeer's own crash report: "jill fell down a hole in the terrain in episode 2 and
 * then the game crashed" -- a hole with open space extending below the board's own last row (boardys,
 * 64) lets a falling object's proposed landing y (JMAN.c's own cando()/objdo()/standfloor(), the
 * collision-test callers every fall/move routine goes through) grow past that edge BEFORE moveobj()
 * ever gets a chance to clamp it back into range -- moveobj() only clamps the position a move
 * actually LANDS on, after cando() has already approved it, so the out-of-bounds coordinate reaches
 * this macro first. Reading bd[][] out of bounds there returns whatever raw 16-bit value happens to
 * sit in nearby memory; masked to 14 bits (up to 16383) and used to index info[] (JILL.H, just below,
 * only numinfotypes=600 entries) it's very likely a far out-of-bounds read there too -- easily past
 * mapped memory. Real DOS memory (small, segment-relative, everything nearby actually belongs to the
 * same running program) made this land somewhere merely garbage-looking instead of unmapped, so
 * upstream's own original game apparently never crashed from it -- Android's flat virtual address
 * space has no such guarantee, so the identical logic bug is fatal here, exactly like the
 * findcheckpt()/dolevelsong() NULL-deref bug fixed just before this one. Converted from a macro to a
 * real function (every real call site already passes simple values/expressions, never anything
 * depending on being evaluated exactly once or not at all, so this is a drop-in replacement) so the
 * bounds check lives in exactly one place rather than needing to be repeated at every collision-test
 * call site in JMAN.c/JOBJ.c/JOBJ2.c/JOBJ3.c/JUNGLE.c. Returns 0 for an out-of-bounds cell -- always
 * a safe, in-bounds info[] index regardless of what tile kind 0 actually is, and this exact "walked
 * off the edge of the defined board" situation was never a real, reachable case upstream's own level
 * format was designed around in the first place, so there's no "authentic" behavior to preserve here,
 * only a crash to avoid. */
static inline uword board(int x, int y)
{
    if (x < 0 || x >= boardxs || y < 0 || y >= boardys) return 0;
    return bd[x][y] & 0x3fffU;
}

void setboard(int x, int y, int value);
void modboard(int x, int y);
void initobjs(void);
void playerkill(int n);
int countobj(int objkind);
void notemod(int n);
void setobjsize(int n);
int findcheckpt(int level);
void dolevelsong(void);
void p_reenter(int died);
void p_ouch(int healthtake, int diemode);
void seekplayer(int n, int *dx, int *dy);
void modjunglescroll(int xd, int yd, int modcode);
void junglescroll(int xd, int yd);
#if defined(JILL_EP3)
void pagedjunglescroll(int xd, int yd);
#endif
void refresh(int page_mode);
void updbkgnd(void);
void updobjs(int doflag);
void killobj(int n);
int addobj(int kind, int x, int y);
void addinv(int invthing);
int takeinv(int invthing);
int invcount(int invthing);
void initinv(void);
void moveobj(int n, int x, int y);
int standfloor(int n, int dx, int dy);
int trymove(int n, int x, int y);
int justmove(int n, int x, int y);
int onscreen(int n);
int trymovey(int n, int x, int y);
int crawl(int n, int dx, int dy);
void addscore(int score, int x, int y);
void addtext(const char *text, int objkind, int x, int y);
void sendtrig(int counter, int msg, int fromobj);
void setorigin(void);
int cando(int n, int x, int y, int flags);
int objdo(int n, int x, int y, int flags);
void touchbkgnd(int n);
void purgeobjs(void);
void updbotmsg(void);
void hitplayer(int n);
int fishdo(int n, int x, int y);
void pointvect(int n1, int n2, int *x, int *y, int length);
int vectdist(int n1, int n2);
int trybreakwall(int n, int x, int y);

void initinfo(void);
void calc_scroll(int vertical_delta);
int playerxfm(int n);

void drawstats(void);
void drawcell(int x, int y);
void drawboard(void);
void moddrawboard(void);
void zapobjs(void);
void loadboard(const char *filename);
void saveboard(const char *filename);
void putbotmsg(const char *message, int color);
void dotextmsg(int number);

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(JillDiskObject) == 31, "Jill object disk record must be 31 bytes");
_Static_assert(sizeof(JillDiskPlayer) == 70, "Jill player disk record must be 70 bytes");
#endif

#endif

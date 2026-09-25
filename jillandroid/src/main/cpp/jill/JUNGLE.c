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

/*
 * Android port note (this file, not upstream): the real ~1660-line game loop -- the biggest single
 * file in this port, but mechanically the smallest set of changes yet, because everything it calls
 * was already ported across the last several stages (GR.c/GRASM.c/GRL.c, WIN.c, SHM.c, the object
 * system, KEYBOARD.c/MUSIC.c, jill/GAMECTRL.c, jill/CONFIG.c, jill/COPYFILE.c, jill/PIXWRITE.c, and
 * HOSTANDROID.c's real myclock-driving timer thread). Seven real changes:
 *
 *   1. #include <SDL.h> is dropped. Upstream's own HOSTSDL.H (the desktop host's interface, shared
 *      as-is by this Android build -- see that file's own header) has no SDL.h dependency itself;
 *      nothing in this file calls an SDL_* function directly (everything goes through the host_*
 *      abstraction), so the #include was only ever needed for the desktop SDL host build this file
 *      also compiles against upstream, not for anything this file itself uses.
 *   2. #include "DOSIO.H" is dropped for plain POSIX, same as every other ported file in this
 *      project (PLATFORM.c/JINFO.c/SHM.c/MUSIC.c/jill/GAMECTRL.c/jill/COPYFILE.c/jill/PIXWRITE.c):
 *      loadcfg()/savecfg() are the only two functions here doing raw file I/O (everything else uses
 *      portable fopen/fread/fwrite/fclose already) -- _open/_read/_write/_close -> open/read/write/
 *      close, _O_BINARY|_O_RDONLY -> O_RDONLY, _creat(path, 0) -> creat(path, S_IRUSR|S_IWUSR), and
 *      _filelength(handle) -> fstat()+st_size (the same pattern jill/GAMECTRL.c's playmac() already
 *      established). <stdio.h> is added explicitly since DOSIO.H was silently providing it before.
 *   3. The file's eight _itoa()/_ltoa() calls (drawstats()/printhi()/loadsavewin()/loadgame()/
 *      savegame()) become snprintf(), the same swap already used throughout this project (see
 *      JOBJ.c/JMAN.c/jill/CONFIG.c's own header comments) -- each one keeps upstream's own cast
 *      exactly (including printhi()'s own truncation of a ulongword score down to word before
 *      formatting it -- that's upstream's own behavior, not something this port introduced).
 *   4. _stricmp()/_strnicmp() (MSVC <string.h>, used only in main()'s own --validate/--scale=
 *      argument parsing) become strcasecmp()/strncasecmp() (<strings.h>, POSIX) -- first use of
 *      this particular swap in the project, same idea as every other MSVC-only call this project
 *      has ported.
 *   5. design() (DESIGN.C's own level-editor entry point, reached only via jmenu()'s hidden dev key
 *      '5') has no definition yet -- DESIGN.C isn't ported (dev-only functionality, out of scope
 *      for the main game loop, same call made for it back when this stage was first scoped). A
 *      small project-owned stand-in, jill_design_stub.c, provides a no-op so this file links; see
 *      that file's own header comment for when to remove it.
 *   6. Upstream's own getline(int number, char *line, int add_spaces) -- extracts the given line
 *      number from a decoded text-message buffer, nothing to do with reading text from a FILE --
 *      collides with Android/bionic's real POSIX getline(char **lineptr, size_t *n, FILE *stream),
 *      declared in <stdio.h> since this project's added #include (see point 2 above) makes it
 *      visible here. Genuinely incompatible signatures, not just a name upstream happened to reuse
 *      for a similar idea -- this doesn't compile no matter what's included alongside it on a
 *      modern POSIX libc. Renamed to jill_getline() throughout this file and JUNGLE.H, matching the
 *      "jill_" prefix this project already uses for its own additions (jill_delay()/jill_ticks()/
 *      jill_strdup()/jill_random(), PLATFORM.c; jill_read_u16_le() etc., also PLATFORM.c) --
 *      cosmetic rename only, the function's own logic is untouched.
 *   7. rexit()'s own trailing exit(result) is replaced with longjmp(jill_quit_jmp, ...) -- added
 *      once jill_jni_bridge.c actually started calling main() (see that file's own header comment)
 *      and a real device crash showed why this one couldn't wait: rexit() is reached the moment
 *      anything goes wrong loading a file (loadboard()'s own rexit(1)/rexit(2)/rexit(3)/rexit(4) on
 *      a short/missing level file, among others), and upstream's own exit() call, on Android, tears
 *      down the WHOLE app process from a background native thread while EGL/Choreographer state on
 *      other threads is still alive -- not a graceful "this play session is over", a hard abort
 *      (observed as a FORTIFY "pthread_mutex_lock called on a destroyed mutex" SIGABRT on the
 *      AChoreographer thread, moments after a legitimate rexit(1) from a level file that didn't load).
 *      jill_quit_jmp (defined in jill_jni_bridge.c, set with setjmp() right before it calls main())
 *      is what rexit() below jumps back to instead -- every real cleanup call above it (savecfg()/
 *      snd_exit()/shm_exit()/gc_exit()/gr_exit()/host_close()) still runs first, in the same order,
 *      unchanged; only the final "how does this function's caller ever get control back" mechanism
 *      is swapped, from "it doesn't, the process ends" to "back to jill_run_game(), the way any other
 *      Android JNI call returns". See jill_jni_bridge.c's own header comment for the other half of
 *      this.
 *
 * Everything else -- level load/save, the save-game encode/decode helpers, text-message paging,
 * the demo/macro playback wiring, the menu system, printhi()'s high-score table, and main() itself
 * (now the real entry point jill_jni_bridge.c's jillMain() calls -- see that file's own header
 * comment for how) -- is byte-identical logic.
 *
 * This file is also what every one of jill_jungle_stubs.c's remaining placeholders was standing in
 * for: gamevp/statvp/cmdvp, botvp/tempvp, scrnxs/scrnys, bd, newlevel/curlevel, botmsg/botcol/
 * bottime, inv_shape, and putbotmsg()/dotextmsg()/drawcell()/drawstats()/loadboard() all get their
 * real definitions right here, matching that file's own long-documented removal trigger exactly.
 * jill_jungle_stubs.c is deleted in this same change (see CMakeLists.txt).
 */

#include "JUNGLE.H"

#include "CONFIG.H"
#include "COPYFILE.H"
#include "DESIGN.H"
#include "EPISODE.H"
#include "GAMECTRL.H"
#include "HOSTSDL.H"
#include "KEYBOARD.H"
#include "MUSIC.H"
#include "PIXWRITE.H"
#include "SHM.H"
#include "WINDOWS.H"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Android port addition, not upstream -- diagnostic-only logging for rexit() below (see that
 * function's own comment). Same LOG_TAG ("jillhost") and macro shape as HOSTANDROID.c/
 * jill_jni_bridge.c's own LOGE, kept local to this file (a bare #include <android/log.h> plus one
 * macro) rather than pulling in a host header, to keep this otherwise-upstream-faithful file's own
 * Android footprint as small as this one diagnostic actually needs. */
#include <android/log.h>
#define JILL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "jillhost", __VA_ARGS__)
/* Android port addition, not upstream -- diagnostic-only logging for jmenu()'s own quit check
 * below (see that call site's own comment). Same tag/shape as JILL_LOGE just above, ANDROID_LOG_INFO
 * instead of _ERROR since this isn't an error path by itself -- just visibility into a value that's
 * otherwise invisible from outside this file. */
#define JILL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "jillhost", __VA_ARGS__)

#define JILL_HIGH_COUNT      10
#define JILL_HIGH_STORAGE_COUNT 12
#define JILL_HIGH_NAME_LEN   10
#define JILL_SAVE_COUNT       6
#define JILL_SAVE_NAME_LEN   12
#define JILL_BOARD_BYTES (boardxs * boardys * 2)

wintype ourwin, levelwin;
vptype botvp, tempvp;
vptype *gamevp = &ourwin.inside;
vptype *cmdvp = &ourwin.topleft;
vptype *statvp = &ourwin.botleft;
word scrnxs = normxs, scrnys = normys;
uword bd[boardxs][boardys];
char newlevel[32], curlevel[32];
#if defined(JILL_EP1)
const char *leveltxt[32] = {
    "JILL ENTERS\rTHE\rJUNGLE MAP.\r",
    "JILL BOUNDS\rTHROUGH\rTHE BOULDERS\r",
    "JILL JOURNEYS\rINTO\rTHE FOREST\r",
    "JILL ENTERS\rTHE HUT\r",
    "\r", "\r",
    "JILL DASHES INTO\rTHE CASTLE\r",
    "JILL EXPLORES\rTHE FOREST\r",
    "JILL SNEAKS\rINTO\rARG'S DUNGEON\r",
    "JILL ENTERS\rTHE\rPHOENIX MAZE\r",
    "JILL VENTURES\rINTO THE\rKNIGHT'S PUZZLE\r",
    "JILL CREEPS INTO\rTHE DARK FOREST\r",
    "JILL SPLASHES INTO\rTHE UNDERGROUND\rRIVER\r",
    "JILL ENTERS\rYET\rANOTHER PUZZLE\r",
    "JILL ENTERS\rTHE PLATEAU\r",
    "JILL REACHES\rTHE ENDING\r\rNOW SIT BACK\rAND ENJOY\r",
    "\r", "17\r", "18\r", "19\r",
    "JILL BOUNDS INTO\rTHE BONUS LEVEL\r",
    "21\r", "22\r", "23\r", "24\r", "25\r", "26\r", "27\r", "28\r", "29\r",
    "", ""
};
#elif defined(JILL_EP2)
const char *leveltxt[32] = {
    "JILL GOES\rUNDERGROUND\r",
    "",
    "JILL ENTERS\rMONTEZUMA'S\rCASTLE\r",
    "\r", "\r", "\r", "\r", "\r",
    "JILL DESCENDS\rINTO THE\rDEPTHS OF\rHECK\r",
    "JILL RUNS INTO\rTHE RED PUZZLE\r",
    "JILL SWIMS TO\rTHE WATERWORLD\r",
    "\r",
    "JILL DRIFTS\rINTO THE\rDEMONIC MAZE\r",
    "JILL BOUNDS\rINTO\rTHE BONUS LEVEL\r",
    "JILL WANDERS\rINTO THE LAND\rOF ETERNAL\rWEIRDNESS\r",
    "JILL JUMPS\rINTO A STICKY\rSITUATION\r",
    "JILL BETTER\rTHINK FAST!\r",
    "\r", "\r",
    "JILL ESCAPES\rTHE UNDERGROUND\r",
    "21\r", "22\r", "23\r", "24\r", "25\r",
    "26\r", "27\r", "28\r", "29\r",
    "", "", ""
};
#elif defined(JILL_EP3)
const char *leveltxt[32] = {
    "JILL ENTERS\rTHE\rJUNGLE MAP\r",
    "JILL ENTERS\rTHE VALLEY\r",
    "JILL ROAMS\rTHROUGH THE\rVILLAGE\r",
    "JILL JOURNEYS\rTO THE DAM\r",
    "JILL DISCOVERS\rTHE SECRET FOREST\r",
    "JILL BOUNDS INTO\rTHE AERIE\r",
    "JILL EXPLORES\rTHE AQUEDUCT\r",
    "JILL BOARDS\rTHE SHIP OF\rTHE GIANT GREEN\rLIZARD MEN!\r",
    "JILL VENTURES\rINTO THE\rMEGA PUZZLE\r",
    "JILL JOURNEYS\rINTO THE JAIL\r",
    "JILL TRYS HER\rLUCK IN THE\rPYRAMID PUZZLE.\r",
    "JILL LEAPS INTO\rLEVEL ELEVEN\r",
    "IF YOU THINK THE\rNEXT LEVEL IS\rNUMBER TWELVE,\rYOU'RE RIGHT!\r",
    "JILL FINALLY\rDISCOVERS\rTHE CASTLE\r",
    "\r", "\r", "\r", "\r", "\r", "\r", "\r",
    "21\r", "22\r", "23\r", "24\r", "25\r", "26\r", "27\r", "28\r", "29\r",
    "", ""
};
#endif
char botmsg[60];
word botcol, bottime;
char oursong[32], tempname[64];
word oldlevelnum;
word facetable = 24;
#if defined(JILL_EP1)
word xbordercol = 1;
#elif defined(JILL_EP2) || defined(JILL_EP3)
word xbordercol = 7;
#endif
word xmsgdelay;
word debug, turtle, xdemoflag;
word levelmsgclock;
char xshafile[] = JILL_SHAPE_FILE;
word inv_shape[11] = {
    0x0026, 0x000c, 0x000d, 0x000b, 0x000e, 0x000f,
    0x0012, 0x0014, 0x0023, 0x0024, 0x0025
};

static char high_name[JILL_HIGH_STORAGE_COUNT][JILL_HIGH_NAME_LEN];
static ulongword high_score[JILL_HIGH_COUNT];
static char save_name[JILL_SAVE_COUNT][JILL_SAVE_NAME_LEN];
static int selected_save;

#if defined(JILL_EP1)
char *demoboard[10] = {
    "intro.jn1", "", "", "", "", "", "", "", "", ""
};
byte demolvl[10] = { 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char *demoname[10] = {
    "jn1demo.mac", "", "", "", "", "", "", "", "", ""
};
#elif defined(JILL_EP2)
char *demoboard[10] = {
    "3.jn2", "9.jn2", "17.jn2", "", "", "", "", "", "", ""
};
byte demolvl[10] = { 3, 9, 17, 0, 0, 0, 0, 0, 0, 0 };
char *demoname[10] = {
    "jn2dem1.mac", "jn2dem2.mac", "jn2dem3.mac", "",
    "", "", "", "", "", ""
};
#elif defined(JILL_EP3)
char *demoboard[10] = {
    "1.jn3", "5.jn3", "12.jn3", "", "", "", "", "", "", ""
};
byte demolvl[10] = { 1, 5, 12, 0, 0, 0, 0, 0, 0, 0 };
char *demoname[10] = {
    "jn3dem1.mac", "jn3dem2.mac", "jn3dem3.mac", "",
    "", "", "", "", "", ""
};
#endif
word demonum;

static void set_game_layout(void);

/* Android port addition, not upstream -- wootbeer's own explicit ask, mirroring the display-mode
 * option the God of Thunder Android port already has: a pause-menu-selectable presentation mode,
 * cycled by pausemenu()'s own new "DISPLAY MODE" row (case 'V'). 0=Default is exactly today's
 * existing behavior (stretch host_present()'s fixed 320x200 buffer to fill the Surface, standard
 * CONTROLS/INVENTORY panel layout); 1=Force 4:3 is Default's own JUNGLE.c-side layout completely
 * unchanged -- only HOSTANDROID.c's host_present() pillarboxes/letterboxes the presented picture,
 * see host_set_display_mode() there; 2=Minimal UI hides the CONTROLS panel and shrinks
 * INVENTORY/HEALTH/SCORE into a slim strip across the top (jill_draw_minimal_hud() below), and
 * uses the screen space that frees up to genuinely expand gamevp -- more real game world drawn
 * every frame, not just the same picture stretched bigger. Persisted across launches in display.dat
 * (jill_load_app_config()/jill_save_app_config() below) -- unlike turtle/soundf, which
 * really do just reset every relaunch, wootbeer's own explicit ask was for this one to stick. The
 * JILL_DISPLAY_* values and the jill_display_mode/jill_active_display_mode declarations themselves
 * now live in JILL.H, not here -- JMAN.c's own refresh() needs them too, see that header's own
 * comment for the full two-variable story (jill_display_mode is the persisted preference;
 * jill_active_display_mode is what's actually driving rendering this instant, forced to Default
 * outside real gameplay). */
int jill_display_mode;
int jill_active_display_mode;

/* Android port addition, not upstream -- see JILL.H's own jill_color_setting comment for the whole
 * "Jill Color" Enhancements feature. jill_altcolor_addr/jill_altcolor_flags are the actual
 * cross-loaded table (or NULL/0, no override active) -- jill_refresh_color_override() below is the
 * only thing that ever writes them. */
int jill_color_setting;
byte *jill_altcolor_addr;
word jill_altcolor_flags;

/* "../epN/jillN.sha" for each of the three possible cross-episode overrides, index by
 * jill_color_setting itself (1/2/3 -- index 0, DEFAULT, is never read as a path). Relative, not
 * absolute -- see JILL.H's own jill_color_setting comment for why that always resolves to the
 * right sibling directory regardless of which episode this build actually is. */
static const char *const jill_color_sha_path[4] = {
    NULL, "../ep1/jill1.sha", "../ep2/jill2.sha", "../ep3/jill3.sha"
};

/* Android port addition, not upstream -- called once at startup (main(), below) and again every
 * time pausemenu()'s own ENHANCEMENTS submenu cycles jill_color_setting (case 'J' there). Frees
 * whatever jill_altcolor_addr previously held (a no-op the very first time, when it's still NULL)
 * and, if the new setting asks for a DIFFERENT episode's own look than this build actually is,
 * cross-loads that episode's own JILL_PLAYER_SHAPE_TABLE via shm_load_single_table() (SHM.H) into
 * a fresh buffer. Leaves jill_altcolor_addr NULL (meaning "use this episode's own normal look,
 * exactly like Default") for three different reasons that all resolve the same way: the setting IS
 * Default, the setting already matches this build's own episode number (nothing to override), or
 * -- wootbeer's own follow-up ask -- that other episode's own jillN.sha simply isn't present on this
 * device ("if the user doesn't have that asset loaded we may just have to default it to the one
 * for that episode"): shm_load_single_table() returning 0 covers a missing file the same way it
 * covers a short/corrupt one, and either way this function's own fallback is identical, no special
 * casing needed. jill_draw_player_shape() below is what actually reads jill_altcolor_addr on every
 * draw. */
static void jill_refresh_color_override(void)
{
    byte *addr = NULL;
    word len = 0, flags = 0;

    if (jill_altcolor_addr != NULL) {
        free(jill_altcolor_addr);
        jill_altcolor_addr = NULL;
        jill_altcolor_flags = 0;
    }

    if (jill_color_setting == JILL_COLOR_DEFAULT) return;
#if defined(JILL_EP1)
    if (jill_color_setting == 1) return;
#elif defined(JILL_EP2)
    if (jill_color_setting == 2) return;
#elif defined(JILL_EP3)
    if (jill_color_setting == 3) return;
#endif

    if (shm_load_single_table(jill_color_sha_path[jill_color_setting], JILL_PLAYER_SHAPE_TABLE,
                              &addr, &len, &flags)) {
        jill_altcolor_addr = addr;
        jill_altcolor_flags = flags;
    }
}

/* Android port addition, not upstream -- see JILL.H's own declaration comment. */
void jill_draw_player_shape(vptype *vp, int shape_number, int x, int y)
{
    if (jill_altcolor_addr != NULL)
        drawshape_from_table(vp, jill_altcolor_addr, jill_altcolor_flags, shape_number, x, y);
    else
        drawshape(vp, shape_number, x, y);
}

/* Android port addition, not upstream -- see JILL.H's own comment for the whole "Enemy Speed"/
 * "Attack Speed"/"Mov Speed" Enhancements feature. Persisted the same way/place as
 * jill_color_setting (display.dat, JillAppConfig, below). */
int jill_enemyspeed_setting;
int jill_attackspeed_setting;
int jill_movespeed_setting;

/* Android port addition, not upstream -- see JILL.H's own comment for the whole Mirror Mode
 * feature. Persisted the same way/place as jill_color_setting (display.dat, JillAppConfig,
 * below). */
int jill_mirror_enabled;

/* Android port addition, not upstream -- see JILL.H's own declaration comment. Not static: JPLAYER.c
 * calls this too (Attack Speed/Movement Speed). */
int jill_scale_by_speed(int base, int setting)
{
    if (setting == JILL_SPEED_SLOW) return base / 2;
    if (setting == JILL_SPEED_FAST) return base * 2;
    return base;
}

/* set_game_layout()'s own gamevp/scrnxs/scrnys, cached once right after it establishes them (see
 * that function's own new lines below) so jill_apply_display_mode() always has a known-good
 * "Default/Force 4:3 layout" to restore *gamevp from -- the same struct-copy-restore technique
 * pageview()'s own local `saved_viewport` already uses for its temporary gamevp swap, just cached
 * at file scope instead of on the stack since this one has to survive across many display-mode
 * toggles, not just one pageview() call. */
static vptype jill_default_gamevp;
static word jill_default_scrnxs, jill_default_scrnys;

/* Minimal UI's own HUD strip -- a full-width viewport across the top rows, never overlapping
 * botvp's own bottom message line (y 188-200, untouched by any of this). Just used for its own
 * pixel geometry (jill_draw_minimal_hud() below draws into it directly) -- in this mode gamevp
 * itself now covers this same screen-space rectangle too (wootbeer's own explicit ask: "I wanted it
 * to fill in like the rest of the screen with more level", after an earlier round reserved this
 * strip as a dead, non-scrolling black bar), so the two deliberately overlap. Real level art
 * scrolls underneath the HUD's own text/icons every tick via jill_minimal_hud_tick() below, which
 * is why this needs its own per-tick redraw (called unconditionally from JMAN.c's refresh(), not
 * just when statmodflg fires the way the DOS panel's own drawstats() is) -- unlike the old
 * non-overlapping strip, gamevp's own scrolling blit (refresh()'s own scrollvp() call) would drag
 * yesterday's HUD pixels sideways with the world if this only redrew on a stat change. */
static vptype jill_hudvp;

static void jill_draw_minimal_hud(void);
static void jill_activate_display_mode(int mode);
static void jill_redraw_after_display_mode_change(void);
static void jill_apply_display_mode(int new_mode);

/* Android port addition, not upstream -- see jill_minimal_hud_tick()'s own definition (and
 * JILL.H's declaration of it) for the whole story. Kept as a separate, non-static entry point
 * from jill_draw_minimal_hud() (which stays file-local) specifically so JMAN.c's own refresh()
 * has one thing to call, not two, and so it can't accidentally call jill_draw_minimal_hud() alone
 * and skip the backdrop redraw that has to happen first. */
void jill_minimal_hud_tick(void);

static void data_path(char *destination, size_t capacity, const char *name)
{
    size_t length;
    if (destination == NULL || capacity == 0) return;
    destination[0] = '\0';
    if (cfg_path[0] != '\0') {
        strncpy(destination, cfg_path, capacity - 1);
        destination[capacity - 1] = '\0';
        length = strlen(destination);
        if (length != 0 && destination[length - 1] != '\\' && destination[length - 1] != '/' &&
            length + 1 < capacity) {
            destination[length++] = '\\';
            destination[length] = '\0';
        }
    }
    if (strlen(destination) + strlen(name) + 1 < capacity) strcat(destination, name);
}

static int read_exact(FILE *file, void *destination, size_t length)
{
    return file != NULL && fread(destination, 1, length, file) == length;
}

static int write_exact(FILE *file, const void *source, size_t length)
{
    return file != NULL && fwrite(source, 1, length, file) == length;
}

static int write_word_file(FILE *file, uword value)
{
    byte encoded[2];
    jill_write_u16_le(encoded, value);
    return write_exact(file, encoded, sizeof(encoded));
}

static void decode_object(objtype *object, const byte encoded[31])
{
    object->objkind = (sbyte)encoded[0];
    object->x = (word)jill_read_u16_le(encoded + 1);
    object->y = (word)jill_read_u16_le(encoded + 3);
    object->xd = (word)jill_read_u16_le(encoded + 5);
    object->yd = (word)jill_read_u16_le(encoded + 7);
    object->xl = (word)jill_read_u16_le(encoded + 9);
    object->yl = (word)jill_read_u16_le(encoded + 11);
    object->state = (word)jill_read_u16_le(encoded + 13);
    object->substate = (word)jill_read_u16_le(encoded + 15);
    object->statecount = (word)jill_read_u16_le(encoded + 17);
    object->counter = (word)jill_read_u16_le(encoded + 19);
    object->objflags = jill_read_u16_le(encoded + 21);
    object->inside = jill_read_u32_le(encoded + 23) != 0 ? (char *)(uintptr_t)1 : NULL;
    object->info1 = (word)jill_read_u16_le(encoded + 27);
    object->zaphold = (word)jill_read_u16_le(encoded + 29);
}

static void encode_object(byte encoded[31], const objtype *object)
{
    memset(encoded, 0, 31);
    encoded[0] = (byte)object->objkind;
    jill_write_u16_le(encoded + 1, (uword)object->x);
    jill_write_u16_le(encoded + 3, (uword)object->y);
    jill_write_u16_le(encoded + 5, (uword)object->xd);
    jill_write_u16_le(encoded + 7, (uword)object->yd);
    jill_write_u16_le(encoded + 9, (uword)object->xl);
    jill_write_u16_le(encoded + 11, (uword)object->yl);
    jill_write_u16_le(encoded + 13, (uword)object->state);
    jill_write_u16_le(encoded + 15, (uword)object->substate);
    jill_write_u16_le(encoded + 17, (uword)object->statecount);
    jill_write_u16_le(encoded + 19, (uword)object->counter);
    jill_write_u16_le(encoded + 21, object->objflags);
    jill_write_u32_le(encoded + 23, object->inside != NULL ? 1U : 0U);
    jill_write_u16_le(encoded + 27, (uword)object->info1);
    jill_write_u16_le(encoded + 29, (uword)object->zaphold);
}

static void decode_player(const byte encoded[70])
{
    int index;
    pl.level = (word)jill_read_u16_le(encoded);
    pl.health = (word)jill_read_u16_le(encoded + 2);
    pl.numinv = (word)jill_read_u16_le(encoded + 4);
    for (index = 0; index < maxinventory; ++index)
        pl.inv[index] = (word)jill_read_u16_le(encoded + 6 + index * 2);
    pl.score = jill_read_u32_le(encoded + 38);
    pl.ouched = (word)jill_read_u16_le(encoded + 42);
    pl.oldscore = jill_read_u32_le(encoded + 44);
    memcpy(pl.pad, encoded + 48, sizeof(pl.pad));
}

static void encode_player(byte encoded[70])
{
    /* Android port addition, not upstream -- Cheat Codes' own "All Items" cheat (JILL.H's own
     * jill_cheat_all_items comment has the whole feature). One "still outstanding" flag per
     * inventory kind, copied once up front from jill_cheat_all_items_owed() (JMAN.c) -- skip_owed[
     * kind] counts down as this loop below encounters real pl.inv[] entries of that kind, so
     * exactly the cheat's own single grant per kind is left out of the encoded buffer and nothing
     * else is -- a second, legitimately-picked-up copy of the same kind is written through
     * normally. This function is the one place EVERY on-disk pl.inv[] (every save slot, and the
     * level-transition continuity file, saveboard()'s own two call sites) gets written, so this is
     * also the one place that needs to guard against a save ever baking a cheat-granted item in as
     * real progress -- matching GoT Android's own got_save_write_slot() precedent exactly (wootbeer's
     * own ask: "take away the ones they aren't supposed to have when... saving, etc."). */
    int skip_owed[11];
    int index, kind, written = 0;

    for (kind = 0; kind < 11; ++kind) skip_owed[kind] = jill_cheat_all_items_owed(kind);

    memset(encoded, 0, 70);
    jill_write_u16_le(encoded, (uword)pl.level);
    jill_write_u16_le(encoded + 2, (uword)pl.health);
    for (index = 0; index < pl.numinv; ++index) {
        kind = pl.inv[index];
        if (kind >= 0 && kind < 11 && skip_owed[kind] > 0) {
            --skip_owed[kind];
            continue;
        }
        jill_write_u16_le(encoded + 6 + written * 2, (uword)kind);
        ++written;
    }
    jill_write_u16_le(encoded + 4, (uword)written);
    jill_write_u32_le(encoded + 38, pl.score);
    jill_write_u16_le(encoded + 42, (uword)pl.ouched);
    jill_write_u32_le(encoded + 44, pl.oldscore);
    memcpy(encoded + 48, pl.pad, sizeof(pl.pad));
}

void fin(void) { snd_play(1, 22); fadein(); }
void fout(void) { snd_play(1, 21); fadeout(); }

void drawkeys(void)
{
    const char *first;
    const char *second;

    switch (objs[0].objkind) {
    case obj_player:
        fontcolor(cmdvp, 7, 8);
        first = "JUMP   ";
        if (invcount(inv_blade)) second = "BLADE  ";
        else if (invcount(inv_knife)) second = "KNIFE  ";
        else second = "       ";
        break;
    case obj_tiny:
        fontcolor(cmdvp, 7, 8);
        first = "       ";
        second = "       ";
        break;
    case obj_jillbird:
        fontcolor(cmdvp, 7 - pagedraw * 4, 8);
        first = "FLAP   ";
        second = "FIRE   ";
        break;
    case obj_jillfrog:
        fontcolor(cmdvp, 7 - pagedraw * 4, 8);
        first = "HOP    ";
        second = "LEAP   ";
        break;
    case obj_jillfish:
        fontcolor(cmdvp, 7 - pagedraw * 6, 8);
        first = "SWIM   ";
        second = "SHOOT  ";
        break;
    case 55:
        fontcolor(cmdvp, 7, 8);
        first = "       ";
        second = "       ";
        break;
    default:
        return;
    }

    wprint(cmdvp, 37, 10, 2, first);
    wprint(cmdvp, 33, 19, 2, second);
}

void drawcmds(void)
{
    /* Android port addition, not upstream -- Minimal UI mode skips the whole CONTROLS legend
     * (it's not drawn anywhere on screen in that mode, see drawgamewin()'s own new branch) and
     * goes straight to drawstats(), which owns jill_draw_minimal_hud() in that mode. Reads
     * jill_active_display_mode, not jill_display_mode -- see JILL.H's own comment on the two --
     * so this stays Default-shaped in the main menu even when the player's persisted preference is
     * Minimal UI. */
    if (jill_active_display_mode != JILL_DISPLAY_MINIMAL) {
        fontcolor(cmdvp, 4, 8);
        clearvp(cmdvp);
        wprint(cmdvp, 0, 33, 1, "____________");
        wprint(cmdvp, 5, 2, 2, "Move Jill");
        fontcolor(cmdvp, 5, 8);
        drawshape(cmdvp, 0x0600, 2, 9);
        drawshape(cmdvp, 0x0601, 2, 18);
        drawshape(cmdvp, 0x0609, 2, 27);
        fontcolor(cmdvp, 7, 8);
        wprint(cmdvp, 30, 28, 2, "Help");
        fontcolor(cmdvp, 3, 8);
        wprint(cmdvp, 1, 43, 1, "N");
        wprint(cmdvp, 1, 51, 1, "Q");
        wprint(cmdvp, 1, 59, 1, "S");
        wprint(cmdvp, 1, 67, 1, "R");
        wprint(cmdvp, 1, 75, 1, "T");
        fontcolor(cmdvp, 2, 8);
        wprint(cmdvp, 14, 44, 2, "NOISE");
        wprint(cmdvp, 14, 52, 2, "QUIT");
        wprint(cmdvp, 14, 60, 2, "SAVE");
        wprint(cmdvp, 14, 68, 2, "RESTORE");
        wprint(cmdvp, 14, 76, 2, "TURTLE");
        drawkeys();
    }
    drawstats();
    statmodflg |= mod_screen;
}

void putbotmsg(const char *message, int color)
{
    strcpy(botmsg, message);
    botcol = (word)color;
    bottime = 80;
    statmodflg |= mod_screen;
}

/* Android port addition, not upstream -- Minimal UI mode's own compact replacement for the
 * CONTROLS panel's sound/turtle icons and the full INVENTORY panel's HEALTH/SCORE/LEVEL/inventory
 * drawing, all squeezed into jill_hudvp's own 320x30 strip (see that viewport's own comment)
 * instead of the DOS panel's tall 64x155-ish sidebar. Reuses the exact same shape IDs and color
 * indices as the real drawstats() below, just repositioned -- this is a layout change, not a new
 * HUD design. Row 1 (y=2): HEALTH pips, SCORE, LEVEL. Row 2 (y=15): every inventory icon, spread
 * across the full 320px width at 20px spacing -- comfortably fits all maxinventory (16) slots,
 * unlike the original panel's cramped 3-wide grid, so nothing needs to scroll or get cut off. */
static void jill_draw_minimal_hud(void)
{
    char message[32];
    int index;
    /* Android port addition, not upstream -- tracks the rightmost x this tick's own HUD content
     * (text or icons) actually reaches, in jill_hudvp's own local coordinates. An EARLIER round of
     * this fix used this to push a tight mirror-exclude rect (wootbeer's own "entire top row... all
     * unmirrored" report, fixed by measuring this instead of always excluding the full 320px strip)
     * -- that round's own real bug, wootbeer's very next report: "now the minimal ui is mirrored on both
     * sides of the screen". Root cause: EXCLUDING destination columns [0, hud_right) from being
     * mirrored stops THOSE columns from flipping, but does nothing to stop OTHER, still-mirrored
     * columns on the right from reading FROM that same source region as THEIR OWN mirror source --
     * host_present()'s own mirror_col = 319-x lands squarely back inside [0, hud_right) for every
     * destination x in [320-hud_right, 320), so the HUD gets reproduced a second time over there,
     * backwards. hud_right is still measured exactly as before (every wprint()/drawshape() call
     * below updates it via a local `right`, never shrinking it) -- just put to a completely
     * different use now, below. */
    int hud_right = 0;
    int right;
    int row, col;
    /* Android port addition, not upstream -- wootbeer's own fix idea for the "mirrored on both sides"
     * bug above: "have our original mirror mode code for the minimal ui as well. but then the fix
     * idea is to actually mirror the minimal ui itself first when mirror mode is on, and then when
     * the mirror mode code takes over, it's flipped to look 'normal'." Exactly right, and exactly
     * what these two snapshots (plus the real pixel-manipulation block at this function's own end)
     * implement -- see that block's own comment for the mechanics. static, not local/stack: this
     * function runs every single tick Minimal UI is active (jill_minimal_hud_tick()'s own comment),
     * and JILL_SCREEN_WIDTH*JILL_MINIMAL_HUD_HEIGHT bytes (9600) is real weight to keep re-allocating
     * on the stack 60 times a second for no reason. Sized to the strip's own real, fixed dimensions
     * (JILL_SCREEN_WIDTH, not just hud_right's own tick-to-tick-varying value) since hud_right isn't
     * known yet at this point in the function -- the actual copies below only ever touch the first
     * hud_right columns of either array, once it's known. */
    static byte clean_snapshot[JILL_MINIMAL_HUD_HEIGHT][JILL_SCREEN_WIDTH];
    static byte hud_snapshot[JILL_MINIMAL_HUD_HEIGHT][JILL_SCREEN_WIDTH];

    /* Android port addition, not upstream -- captures this tick's own fresh, HUD-free terrain
     * (jill_minimal_hud_tick(), this function's own caller, just redrew the whole strip above,
     * unconditionally, before ever calling this function) before any HUD glyph gets drawn over it --
     * see this function's own end for what this snapshot is actually for. Skipped outright when
     * Mirror Mode is off, since nothing below needs it then -- this function draws exactly as it
     * always has in that case, zero extra cost. */
    if (jill_mirror_enabled) {
        for (row = 0; row < (int)jill_hudvp.vpyl; ++row)
            for (col = 0; col < (int)jill_hudvp.vpxl; ++col)
                clean_snapshot[row][col] =
                    jill_video[pagedraw != 0][jill_hudvp.vpy + row][jill_hudvp.vpx + col];
    }

    /* Android port addition, not upstream -- every fontcolor() call below now uses back=-1, the
     * same "draw text transparently over whatever's already there, no solid box behind each
     * glyph" convention this file already uses elsewhere for text meant to sit over real
     * background art (JOBJ.c's own fontcolor(gamevp, 5, -1); JUNGLE.c's own "please wait" overlay,
     * fontcolor(&waitwin.inside, 15, -1)) -- fontcolor_vga()'s own back==-1 special case (GR.c)
     * is exactly this "no background fill" signal, not a real color index. Two rounds of wootbeer's
     * own feedback got this HUD here: first "can we get rid of all the gray at the top... is
     * there a point to that?" (there wasn't -- an earlier version filled this whole strip with a
     * flat color, gray then black, neither one meant to represent anything), then "I didn't want
     * black up there, I wanted it to fill in like the rest of the screen with more level" --
     * jill_apply_display_mode() now expands gamevp to genuinely cover this strip's rows too, and
     * jill_minimal_hud_tick() (below) redraws the real, scrolling level art underneath every tick
     * before this function ever runs, so there's finally something worth NOT covering up. No
     * whole-strip clearvp() call here any more either, for the same reason -- it would paint right
     * back over the level art jill_minimal_hud_tick() just drew. Readability still comes from a
     * real alpha-blended backing, just not from anything in this function: HOSTANDROID.c's
     * host_present() draws a translucent black bar behind this whole strip as its own
     * screen-space GL quad, after this buffer is already uploaded as a texture -- the same
     * technique GoT's own got_hud.c uses ("a wide low-alpha black backing bar sits behind all of
     * it so the HUD stays readable"), and the only way to get real translucency at all here: none
     * of JUNGLE.c's own indexed-palette drawing (this function included) has an alpha channel to
     * blend with.
     *
     * One real, deliberate loss from this round: pl.ouched's own red flash on a hit doesn't show
     * here any more. In the original panel (and this function's own first version) that flash was
     * the whole statvp rectangle's clearvp() background going solid red for a moment -- exactly
     * the kind of opaque fill this round is removing, so there's no equivalent left to trigger.
     * The health pips themselves still drop immediately when hit, which is most of the same
     * feedback; a lighter-weight substitute (e.g. flashing the native translucent bar's own alpha/
     * color briefly) would need new JUNGLE.c<->HOSTANDROID.c plumbing this round didn't add --
     * worth a follow-up if wootbeer asks for it back. */
    fontcolor(&jill_hudvp, -5, -1);
    wprint(&jill_hudvp, 2, 2, 2, "HP");
    right = 2 + 6 * 2; /* "HP", 2 chars at this font's own 6px-per-char cell (the same per-char
                         * width putlevelmsg()'s own centering math already uses: "6 *
                         * (int)strlen(line)"). */
    if (right > hud_right) hud_right = right;
    fontcolor(&jill_hudvp, -4, -1);
    for (index = 0; index < pl.health - 1; ++index)
        drawshape(&jill_hudvp, 0xe2a, index * 3 + 20, 2);
    drawshape(&jill_hudvp, 0xe2b, (pl.health - 1) * 3 + 18, 2);
    /* +8: a conservative real-glyph-width margin past the final (larger) pip's own drawshape() x --
     * cheaper than threading this shape's real pixel width down from the .DDT/.SHA art data just
     * for this measurement, and erring wide here only ever costs a few extra unmirrored pixels,
     * never a mirrored sliver of an actual pip glyph. */
    right = (pl.health - 1) * 3 + 18 + 8;
    if (right > hud_right) hud_right = right;

    /* Android port change, not upstream -- wootbeer's own ask, this round: "remove 'sc' and 'lv' from
     * minimal gui, players can tell what these are." Dropped the two label wprint() calls outright
     * (score and level are the only two bare numbers on this strip besides the HP pips, which keep
     * their own "HP" label since a row of pip icons alone wouldn't read as health at a glance) and
     * shifted each number left into the space its own label used to occupy, rather than leaving an
     * empty gap -- same x position the label itself used to start at (60/130), not the number's old
     * position (74/144). Font colors are untouched: the score number still inherits the HP pips' own
     * -4 (no separate fontcolor() call ever preceded "SC" here, so removing that label changes
     * nothing about it), and the level number still gets its own -2 via the fontcolor() call just
     * below, exactly as before. */
    snprintf(message, sizeof(message), "%d", (int)(longword)pl.score);
    wprint(&jill_hudvp, 60, 2, 2, message);
    right = 60 + 6 * (int)strlen(message);
    if (right > hud_right) hud_right = right;

    fontcolor(&jill_hudvp, -2, -1);
    if (pl.level == 127) strcpy(message, "MAP");
    else snprintf(message, sizeof(message), "%d", (int)(longword)pl.level);
    wprint(&jill_hudvp, 130, 2, 2, message);
    right = 130 + 6 * (int)strlen(message);
    if (right > hud_right) hud_right = right;

    fontcolor(&jill_hudvp, 7, -1);
    for (index = 0; index < pl.numinv; ++index)
        drawshape(&jill_hudvp, 0xe00 + inv_shape[pl.inv[index]], index * 20, 15);
    if (pl.numinv > 0) {
        /* Last icon's own drawshape() x is (numinv-1)*20; +20 covers that icon's own real width
         * (these shapes are drawn 20px apart, so each one comfortably fits inside its own 20px
         * cell) with the same small safety margin the HP pip measurement above uses. */
        right = (pl.numinv - 1) * 20 + 20;
        if (right > hud_right) hud_right = right;
    }

    /* Android port addition, not upstream -- wootbeer's own report, once the MAP/DEMO world banner (that
     * used to sit right on top of this exact spot) stopped drawing under Mirror Mode and exposed it:
     * "its not minor a whole chunk of screen is being mirrored and scrolling the way it's not
     * supposed to." Root cause: an earlier round of this fix (this function's own top comment has
     * the "mirrored on both sides" bug it replaced) treated the WHOLE [0, hud_right) rectangle as
     * "the HUD" -- reading it, restoring clean terrain over ALL of it, and writing the ENTIRE
     * rectangle (HUD glyphs AND whatever real level art was visible in the gaps around/behind them,
     * this HUD's own "no background fill" drawing convention -- back=-1, further up -- leaves plenty
     * of gaps) to the mirror-symmetric position. That correctly keeps the HUD's own text/icons
     * readable, but it ALSO shows any ordinary terrain caught inside that same rectangle in its
     * true, unmirrored orientation -- while the very next row down (outside jill_hudvp's own height)
     * mirrors normally. Whenever that terrain isn't symmetric with itself, the two don't line up: a
     * visible seam right at the strip's own bottom edge, and -- since it's the WORLD's own terrain,
     * not a fixed picture -- it scrolls in the wrong direction relative to everything below it as
     * the player moves, exactly wootbeer's report.
     *
     * Fixed by comparing hud_snapshot against clean_snapshot PIXEL BY PIXEL instead of treating the
     * whole rectangle uniformly: since nothing else runs between the two snapshots except this
     * function's own wprint()/drawshape() calls above (jill_minimal_hud_tick(), this function's own
     * caller, already finished redrawing terrain and objects before ever calling this function), any
     * pixel where the two snapshots differ is guaranteed to be a real HUD glyph pixel -- and any
     * pixel where they're EQUAL is ordinary terrain that never needed to be touched at all. Only
     * glyph pixels get the restore-and-premirror treatment now (host_present()'s own
     * mirror_col = 319-x lookup still needs the SOURCE buffer at column (319-x) to already hold each
     * glyph pixel's correct, readable value for it to land back at its true column x once flipped --
     * same mechanics as before, just scoped down); terrain pixels are left completely alone, exactly
     * as jill_minimal_hud_tick() already drew them, so host_present()'s own uniform mirror pass
     * treats them like any other ordinary terrain on screen -- scrolling the same direction as
     * everything below the strip, no seam. Reading hud_snapshot from a static array (captured in the
     * first loop below, never compared against jill_video directly mid-write) keeps this correct even
     * where a glyph's own mirror-symmetric column also falls inside [0, hud_right) -- doesn't
     * currently happen (hud_right stays under half the strip's own width) but costs nothing to keep
     * safe against. Skipped entirely when Mirror Mode is off, matching the skipped snapshot above --
     * this function's own normal, single, un-mirrored draw is left completely alone in that case. */
    if (jill_mirror_enabled) {
        for (row = 0; row < (int)jill_hudvp.vpyl; ++row)
            for (col = 0; col < hud_right; ++col)
                hud_snapshot[row][col] =
                    jill_video[pagedraw != 0][jill_hudvp.vpy + row][jill_hudvp.vpx + col];

        for (row = 0; row < (int)jill_hudvp.vpyl; ++row) {
            for (col = 0; col < hud_right; ++col) {
                if (hud_snapshot[row][col] != clean_snapshot[row][col]) {
                    int dst_col = (int)jill_hudvp.vpxl - 1 - col;
                    jill_video[pagedraw != 0][jill_hudvp.vpy + row][jill_hudvp.vpx + col] =
                        clean_snapshot[row][col];
                    jill_video[pagedraw != 0][jill_hudvp.vpy + row][jill_hudvp.vpx + dst_col] =
                        hud_snapshot[row][col];
                }
            }
        }
    }
}

/* Android port addition, not upstream -- called from JMAN.c's own refresh() (see JILL.H's
 * declaration and refresh()'s own new call site for exactly where/why) every tick Minimal UI is
 * active, unconditionally -- not gated on statmodflg the way the DOS panel's own drawstats() is,
 * because gamevp's own scrolling can dirty this strip's pixels on any tick, whether or not a stat
 * actually changed this same tick.
 *
 * Redraws the real level art under the HUD strip first, mirroring (a scoped-down copy of) the
 * exact tile-culling math refresh()'s own page_mode/non-page_mode branches use for their own
 * start_x/end_x/start_y/end_y -- necessary because jill_draw_minimal_hud() above no longer clears
 * this strip to any flat color at all, so without a fresh terrain redraw first, old HUD glyphs
 * would just accumulate on top of whatever was already there. drawcell() (JMAN.c) always draws
 * into gamevp specifically, not an arbitrary viewport -- which is exactly what's wanted now that
 * gamevp genuinely covers this strip's own rows (jill_apply_display_mode()), not a separate,
 * repointing trick the way pageview()'s own *gamevp swaps work.
 *
 * Also redraws any on-screen object (the player, enemies, etc.) whose own bounding box overlaps
 * this strip's rows, after the terrain above and before the HUD content below -- wootbeer's own report
 * once the buffer-ordering fix above stopped masking it: "jill can jump 'behind' the sky now".
 * That's exactly what this function's own terrain redraw does on its own, unpatched: it repaints
 * every tile under the strip unconditionally, which is necessary (see this comment's own first
 * paragraph) but also means anything the normal object-draw loop already drew there this same tick
 * (refresh(), JMAN.c, which always runs before this function -- see that function's own call site)
 * gets painted straight back over. scrnobjs[]/numscrnobjs (JMAN.c's own updobjs(), called once per
 * tick before refresh() ever runs) is already exactly the list of every object currently considered
 * on-screen, index 0 (the player) always first -- cheap to walk again here and redraw whichever of
 * them actually overlap this strip's own world-space rectangle, unconditionally (not gated on the
 * object's own mod_screen dirty flag, the same reasoning the terrain redraw above already uses:
 * this strip can need a fresh object redraw on any tick that touches it, whether or not that
 * particular object changed this same tick). Redrawing an object here that the normal loop already
 * drew this tick is harmless -- same sprite, same position, just drawn twice. */
void jill_minimal_hud_tick(void)
{
    int x, y, n, count;
    /* end_x/end_y's own "-2" margin matches refresh()'s own identical start_x/start_y/end_x/end_y
     * computation just above (both branches) -- not an arbitrary choice: this engine scrolls in
     * sub-tile pixel amounts, so a tile just outside the strictly-visible range can still have a
     * sliver on screen, and refresh() overscans by 2 tiles on the trailing edge for exactly that
     * reason. Matching it here avoids a thin strip of stale/never-redrawn terrain creeping in
     * during smooth scrolling that a tighter margin wouldn't catch. */
    int start_x = JILL_MIN(gamevp->vpox / 16 + scrnxs - 1, boardxs - 1);
    int end_x = JILL_MAX(gamevp->vpox / 16 - 2, 0);
    int start_y = JILL_MIN((gamevp->vpoy + jill_hudvp.vpyl + 15) / 16, boardys - 1);
    int end_y = JILL_MAX(gamevp->vpoy / 16 - 2, 0);
    /* Android port fix, not upstream -- the object-overlap check just below has to match this
     * terrain redraw's own ACTUAL coverage, in whole tiles, not the raw jill_hudvp.vpyl (30px) it
     * was first written against. start_y/end_y round OUT to whole tile boundaries (that's the
     * whole point of the "+15" and the "-2" overscan above), so the terrain loop can reach up to
     * 15 real pixels past jill_hudvp's own 30px height on the bottom edge, and a further 32px
     * (2 tiles) past gamevp->vpoy on the top edge. An object sitting in either of those two gaps --
     * covered by the terrain redraw above but outside jill_hudvp.vpyl's own raw pixel count --
     * still got silently painted over by that redraw with no matching redraw of its own: wootbeer's own
     * "jill can jump 'behind' the sky now" report, after the first attempt at this same fix only
     * checked against jill_hudvp.vpyl directly and missed this gap. Deriving these two bounds from
     * end_y/start_y themselves (the exact tile rows the redraw above actually touches), instead of
     * jill_hudvp.vpyl, keeps the two permanently in sync regardless of how either one's own math
     * changes later. */
    int redraw_top = end_y * 16;
    int redraw_bottom = (start_y + 1) * 16;

    for (x = start_x; x >= end_x; --x)
        for (y = start_y; y >= end_y; --y)
            drawcell(x, y);

    for (count = 0; count < numscrnobjs; ++count) {
        n = scrnobjs[count];
        if (objs[n].x + objs[n].xl > gamevp->vpox && objs[n].x < gamevp->vpox + gamevp->vpxl &&
            objs[n].y + objs[n].yl > redraw_top &&
            objs[n].y < redraw_bottom)
            (void)kindmsg[(byte)objs[n].objkind](n, msg_draw, 0);
    }

    jill_draw_minimal_hud();
}

void drawstats(void)
{
    char message[32];
    int index;

    /* Reads jill_active_display_mode, not jill_display_mode -- see JILL.H's own comment on the
     * two -- so any drawstats() call that happens while jill_active_display_mode is forced to
     * Default (jmenu()'s own menu loop, dodemo()'s own attract-mode playback) draws the normal
     * DOS CONTROLS/INVENTORY panel content, even when the player's persisted preference is
     * Minimal UI. */
    if (jill_active_display_mode == JILL_DISPLAY_MINIMAL) {
        jill_draw_minimal_hud();
    } else {
        fontcolor(cmdvp, -7, 8);
        drawshape(cmdvp, 0x060a + soundf, 53, 43);
        drawshape(cmdvp, 0x060a + turtle, 53, 75);
        fontcolor(statvp, -5, pl.ouched ? 4 : 8);
        clearvp(statvp);
        wprint(statvp, 2, 2, 2, "HEALTH");
        fontcolor(statvp, -4, 8);
        for (index = 0; index < pl.health - 1; ++index)
            drawshape(statvp, 0xe2a, index * 3 + 42, 2);
        drawshape(statvp, 0xe2b, (pl.health - 1) * 3 + 40, 2);
        wprint(statvp, 33, 10, 2, "SCORE");
        snprintf(message, sizeof(message), "%d", (int)(longword)pl.score);
        wprint(statvp, 64 - ((int)strlen(message) * 6 + 1), 16, 2, message);
        fontcolor(statvp, -2, 8);
        wprint(statvp, 1, 10, 2, "LEVEL");
        if (pl.level == 127) strcpy(message, "MAP");
        else snprintf(message, sizeof(message), "%d", (int)(longword)pl.level);
        wprint(statvp, 1, 16, 2, message);
        if (debug && !swrite) {
            snprintf(message, sizeof(message), "%d", (int)host_coreleft());
            strcat(message, "     ");
            wprint(statvp, 28, 64, 2, message);
        }
        for (index = 0; index < pl.numinv; ++index)
            drawshape(statvp, 0xe00 + inv_shape[pl.inv[index]],
                      (index / 3) * 14 + 1, (index % 3) * 14 + 26);
        drawkeys();
    }
    clearvp(&botvp);
    fontcolor(&botvp, botcol, 0);
    wprint(&botvp, 160 - (int)strlen(botmsg) * 3, 2, 2, botmsg);
}

void zapobjs(void)
{
    int index;
    for (index = 0; index < numobjs; ++index) {
        if (objs[index].inside != NULL && objs[index].inside != (char *)(uintptr_t)1)
            free(objs[index].inside);
        objs[index].inside = NULL;
    }
    initobjs();
}

void loadcfg(void)
{
    char path[64];
    int handle;
    int index;
    int empty;
    struct stat st;

    data_path(path, sizeof(path), JILL_CONFIG_FILE);
    handle = open(path, O_RDONLY);
    empty = handle < 0 || fstat(handle, &st) != 0 || st.st_size <= 0;
    if (empty) {
        for (index = 0; index < JILL_HIGH_COUNT; ++index) {
            high_name[index][0] = '\0';
            high_score[index] = 0;
        }
        for (index = 0; index < JILL_SAVE_COUNT; ++index)
            save_name[index][0] = '\0';
        cf.firstthru = 0;
        cf.joyflag0 = 0;
        cf.video_mode = x_vga;
        cf.musicflag0 = 1;
        cf.vocflag0 = 1;
    } else {
        (void)read(handle, high_name, 120);
        (void)read(handle, high_score, 40);
        (void)read(handle, save_name, 72);
        if (read(handle, &cf, 22) < 0) cf.firstthru = 1;
    }
    if (handle >= 0) close(handle);
}

void savecfg(void)
{
    char path[64];
    int handle;

    data_path(path, sizeof(path), JILL_CONFIG_FILE);
    handle = creat(path, S_IRUSR | S_IWUSR);
    if (handle >= 0) {
        (void)write(handle, high_name, 120);
        (void)write(handle, high_score, 40);
        (void)write(handle, save_name, 72);
        (void)write(handle, &cf, 22);
        close(handle);
    }
}

/* Android port addition, not upstream -- persists jill_display_mode across app launches. wootbeer's
 * own explicit ask, after trying the feature: "yes please add persist to those options. if we need
 * to add a new config file like we did for god of thunder we can" -- and a new file is exactly the
 * right call here, mirroring the God of Thunder Android port's own AUDIOSET.DAT precedent for its
 * sound/music gain sliders. Deliberately NOT a new field tacked onto upstream's own ConfigState/
 * jillN.cfg (loadcfg()/savecfg() just above): that pair reads/writes with hardcoded literal byte
 * counts (120/40/72/22, no backward-compatible size-tiering the way GoT's own GotSaveHeader grew
 * one for exactly this kind of addition) -- growing it risks misaligning every field for a player
 * who already has a jillN.cfg on disk from before this shipped. A brand new file carries none of
 * that risk, and -- just as important -- this needs to apply the moment the app opens (title/menu
 * screens included, not just once a save is actually continued), the same "account-wide, not
 * per-save-slot" reasoning that sent GoT's own gain settings to their own new file instead of
 * GotSaveHeader too. Read once at startup (jillMain(), right alongside loadcfg() below) rather than
 * from anywhere in the save-game path; written every time jill_apply_display_mode() actually
 * changes the mode (see that function's own call), same "write straight through on every change"
 * precedent AUDIOSET.DAT's own save calls use.
 *
 * Named "config.dat", not "display.dat" -- wootbeer's own ask, once this file was about to start
 * holding the new Sound/Music Options submenu's own settings too: "we will probably have to
 * actually rename the config file to something that makes more sense since it will now hold
 * settings for multiple options... make sure it's named config so it's generic." Exact same rename
 * the God of Thunder Android port already went through for the identical reason (its own CONFIG.DAT,
 * renamed up from AUDIOSET.DAT once Touch Options/Remap Gamepad also needed somewhere to live --
 * see got_main.c's own got_config_path() comment) -- "config" is this project's own now-established
 * name for "the one file every future app-wide, not-per-save-slot setting goes into", the same role
 * that comment already describes. jill_load_app_config() below migrates a real display.dat from
 * before this rename (wootbeer's own already-tested display-mode/color/speed/mirror settings) into the
 * new filename once, on the very next load, rather than silently losing them -- see that function's
 * own comment for how. Still not per-episode like jillN.cfg/jnNsave -- each episode already gets its
 * own chdir()'d-into private directory on Android (jill_jni_bridge.c's jill_run_game()), so there's
 * no cross-episode collision to avoid the way upstream's own single-directory DOS install needed
 * jillN.cfg's per-episode name for. */
typedef struct JillAppConfig {
    int display_mode;
    /* Android port addition, not upstream -- jill_color_setting's own persisted value (JILL.H's
     * own comment has the whole "Jill Color" Enhancements feature). Appended, not inserted --
     * see this struct's own load/save functions for what growing it means for a config.dat
     * written before this field existed. */
    int color_setting;
    /* Android port addition, not upstream -- jill_enemyspeed_setting/jill_attackspeed_setting/
     * jill_movespeed_setting/jill_mirror_enabled's own persisted values (JILL.H's own comments have
     * the whole Enemy Speed/Attack Speed/Mov Speed/Mirror Mode Enhancements features). Appended
     * again, same "growing the struct is safe" reasoning as color_setting just above -- a
     * config.dat written before any of these four existed is simply one fread() short again, and
     * every field here keeps its own static-initializer default (0 -- Default/Off -- for each). */
    int enemyspeed_setting;
    int attackspeed_setting;
    int movespeed_setting;
    int mirror_enabled;
    /* Android port addition, not upstream -- Sound/Music Options submenu (JUNGLE.c's pausemenu(),
     * MUSIC.H's own jill_sound_gain/jill_music_gain/jill_music_enabled comment has the whole
     * feature). Appended at the END, same "growing is safe, old file just leaves these at their own
     * static defaults" reasoning as every field above -- a config.dat (or a migrated legacy
     * display.dat, which never had these fields to begin with) written before this feature existed
     * simply comes back with sound/music gain at their own compiled-in defaults (100%, wootbeer's own
     * "have the current sound levels be the default" ask) and both toggles on. word, not int, only
     * because jill_sound_gain/jill_music_gain/jill_music_enabled themselves are word (MUSIC.H) --
     * every other field here is a plain jill_* int, these three are the exception since that's the
     * type their own live globals already use. */
    word sound_gain;
    word music_gain;
    word sound_enabled;
    word music_enabled;
    /* Android port addition, not upstream -- "Remap Gamepad" submenu (see JILL.H's own
     * JILL_GAMEPAD_ACTION_JUMP/_THROW comment for the whole feature). Appended at the END, same
     * "growing the struct is safe" reasoning as every field above -- a config.dat written before
     * this feature existed simply comes back one fread() short again, and jill_load_app_config()
     * below leaves these two at whatever the live remap table's own compiled-in defaults already
     * are (jill_gamepad_remap_bound_keycode(), seeded before either file open, same pattern
     * sound_gain/music_gain/sound_enabled/music_enabled already established). Plain int, matching
     * every jill_* setting field above except the four word ones right above this comment -- these
     * two aren't a jill_* global at all, they're read/written straight through the remap module's
     * own API (jill_gamepad_remap_bound_keycode()/_set_bound_keycode()), not a simple variable. */
    int jump_button_keycode;
    int throw_button_keycode;
    /* Android port addition, not upstream -- "Touch Options" submenu (JUNGLE.c's pausemenu(), case
     * 'G', now branching between REMAP GAMEPAD and TOUCH OPTIONS depending on
     * jill_controls_gamepad_connected() -- see that case's own comment for the whole feature and
     * jill_controls.h's own comment for the native touch-button module these two fields drive).
     * Appended at the END, same "growing the struct is safe" reasoning as every field above -- a
     * config.dat written before this feature existed simply comes back one fread() short again, and
     * jill_load_app_config() below leaves both at their own static defaults (s_jill_touch_scale_step/
     * s_jill_touch_opacity_step's own initializers, just below this struct), which look identical to
     * this feature's own just-shipped default appearance -- same "an existing install looks
     * unchanged until the user actually touches the new slider" precedent every prior Android-only
     * config field here already establishes. */
    int touch_scale_step;
    int touch_opacity_step;
#if defined(JILL_EP1)
    /* Android port addition, not upstream -- Episode 1's own COIN TOSS remap binding (JILL.H's own
     * JILL_GAMEPAD_ACTION_COIN_TOSS comment has the whole feature). Appended at the END, same
     * "growing the struct is safe" reasoning as every field above. `#if defined(JILL_EP1)`, not
     * unconditional, because this whole struct is itself compiled three separate times (once per
     * jillhostN target, JILL_EPn already selects the rest of this file's own per-episode behavior
     * throughout) -- Episodes 2/3 have no coin-toss action to persist a binding for.
     *
     * Android port note (this comment, not the field, changed) -- config.dat now lives in one
     * shared file across all three episodes (wootbeer's own ask: "is there a way we can make settings
     * persist between episodes? like the gamepad mappings, touch settings, sound settings, etc." --
     * see jill_jni_bridge.c's own jill_link_shared_config_file()), so this IS now cross-episode
     * compatibility that has to keep working: whichever of the three builds saves config.dat LAST
     * decides its on-disk byte length, and Episode 1's own build is the only one of the three
     * that's ever one int longer, for this field alone. C struct layout guarantees don't let a
     * field affect the byte OFFSET of anything declared before it, so this trailing field being
     * present or absent never shifts any other field's position -- every field above this one lands
     * at the identical offset whichever of the three builds compiled the struct, which is exactly
     * what makes jill_load_app_config()'s own now-tolerant partial fread() (see its own comment)
     * safe to just apply whatever prefix of the file it actually got. The one real, accepted
     * consequence: if Episode 2 or 3 saves config.dat AFTER Episode 1 has, Episode 1's own
     * coin_toss_button_keycode value is gone from the file (fopen(path, "wb") truncates), so it
     * reverts to its own compiled-in default the next time Episode 1 loads -- not data loss beyond
     * that one binding, not a crash, and not the "every setting silently resets" failure a shared
     * file would otherwise risk (again, see jill_load_app_config()'s own fread() comment for why
     * that specific failure mode is what actually got fixed here). */
    int coin_toss_button_keycode;
#endif
} JillAppConfig;

/* Android port addition, not upstream -- "Touch Options" submenu's own persisted state (this
 * struct's own touch_scale_step/touch_opacity_step comment above). Live here, in JUNGLE.c, rather
 * than inside jill_controls.c the way the God of Thunder Android port splits this (got_menu.c's own
 * s_touch_scale_step/s_touch_opacity_step statics vs. got_controls.c's own derived s_scale_step/
 * s_touch_opacity) -- Jill's own Touch Options submenu UI lives in this same file (pausemenu(), case
 * 'G'), so there's no cross-file getter/setter pair needed the way got_menu_touch_scale_step()/
 * got_menu_set_touch_scale_step() exist purely to bridge that split; jill_controls.c itself only
 * ever tracks the DERIVED live values (its own s_scale_step/s_touch_opacity), pushed here via
 * jill_controls_set_scale_step()/jill_controls_set_opacity() exactly the way got_controls.c's own
 * setters are pushed to from got_menu.c. TOUCH_SCALE_DEFAULT_STEP must stay numerically in sync with
 * jill_controls.c's own copy (hand-audited, same cross-file-constant policy this whole project
 * already uses -- see JILL_SOUND_GAIN_STEPS's own precedent); TOUCH_OPACITY_DEFAULT_STEP (5) is
 * Touch Options' own Opacity row default -- wootbeer's own follow-up ask, once touch controls were
 * confirmed working: "can we make the default opacity 65%? of course my config file won't reflect
 * that but that is fine" (an existing config.dat keeps whatever step it already saved -- this only
 * changes what a fresh install, with no config.dat yet, seeds s_jill_touch_opacity_step to). Step 5
 * lands exactly on 65% via jill_touch_opacity_alpha()'s own floored formula just below:
 * JILL_TOUCH_OPACITY_MIN_ALPHA (0.30) + (1.0 - 0.30) * 5 / JILL_TOUCH_OPACITY_NUM_STEPS(10) = 0.65,
 * no rounding needed. The floor itself (step 0, still jill_controls.c's own TOUCH_OPACITY_DEFAULT,
 * 30% -- wootbeer's original God of Thunder Android port ask carried over, see
 * JILL_TOUCH_OPACITY_MIN_ALPHA's own comment below) is unchanged and still reachable by sliding all
 * the way down -- only the fresh-install starting point moved. */
#define JILL_TOUCH_SCALE_DEFAULT_STEP 2
/* Hand-synced with jill_controls.c's own TOUCH_SCALE_NUM_STEPS -- same cross-file-constant policy
 * this whole project already uses (see JILL_SOUND_GAIN_STEPS's own precedent). Not #included from
 * there directly since jill_controls.h deliberately exposes only the scale-VALUE lookup
 * (jill_controls_touch_scale_value()), not the step COUNT itself -- this file needs the count to
 * bound its own Left/Right clamp, the same reason got_menu.c keeps its own hand-synced copy of
 * got_controls.c's TOUCH_SCALE_NUM_STEPS rather than sharing one #define across both files. */
#define TOUCH_SCALE_NUM_STEPS 9
#define JILL_TOUCH_OPACITY_DEFAULT_STEP 5
#define JILL_TOUCH_OPACITY_NUM_STEPS JILL_SOUND_GAIN_STEPS /* reuses the same 10-step granularity
                                                             * Sound/Music's own gain sliders already
                                                             * established, same precedent
                                                             * got_menu.c's own s_touch_opacity_step
                                                             * comment gives for reusing
                                                             * SOUND_MUSIC_GAIN_STEPS there. */
/* Hand-synced with jill_controls.c's own TOUCH_OPACITY_DEFAULT -- same policy as
 * TOUCH_SCALE_NUM_STEPS just above. Step 0 always maps to this floor alpha, not to 0.0f --
 * wootbeer's own God of Thunder Android port ask, carried over verbatim: "make sure the minimum
 * opacity is what our previous default was so the user doesn't lose complete track of the
 * controls." This is still the slider's own bottom step (0%'s stand-in), independent of whatever
 * JILL_TOUCH_OPACITY_DEFAULT_STEP a fresh install actually starts at now -- see that #define's own
 * comment just above. */
#define JILL_TOUCH_OPACITY_MIN_ALPHA 0.30f
static int s_jill_touch_scale_step = JILL_TOUCH_SCALE_DEFAULT_STEP;
static int s_jill_touch_opacity_step = JILL_TOUCH_OPACITY_DEFAULT_STEP;

/* Android port addition, not upstream -- converts a 0..JILL_TOUCH_OPACITY_NUM_STEPS Opacity-slider
 * step into the actual alpha jill_controls_set_opacity() (and so jill_draw_touch_buttons()) will
 * render, floored at JILL_TOUCH_OPACITY_MIN_ALPHA rather than 0 -- same formula the God of Thunder
 * Android port's own got_menu_set_touch_opacity_step()/format_opacity_label() both already use (that
 * file's own comment on why step 0 can't just mean "invisible"). Shared by both the load path
 * (jill_load_app_config() above) and the Touch Options submenu UI itself (pausemenu(), case 'G',
 * below) so the two can never compute a different alpha for the same step. */
static float jill_touch_opacity_alpha(int step)
{
    return JILL_TOUCH_OPACITY_MIN_ALPHA +
           (1.0f - JILL_TOUCH_OPACITY_MIN_ALPHA) * (float)step / (float)JILL_TOUCH_OPACITY_NUM_STEPS;
}

/* Pre-rename layout (display_mode through mirror_enabled only, six ints, no sound_gain/music_gain/
 * sound_enabled/music_enabled) -- what a real display.dat on disk from before this round's rename
 * actually contains. offsetof() rather than a second hand-maintained struct: this always tracks
 * JillAppConfig's own first six fields exactly, even if one of THEM ever grows a comment or shifts
 * (fields are never reordered in this codebase, only appended -- see every field's own comment
 * above), so there's only ever one struct definition to keep in sync, not two. */
#define JILL_LEGACY_DISPLAY_CONFIG_SIZE offsetof(JillAppConfig, sound_gain)

/* Forward declaration -- jill_load_app_config() below calls this directly, mid-function, to migrate
 * a real legacy display.dat straight to the new config.dat the moment it's found (see that call's
 * own comment), but jill_save_app_config() itself is defined afterward, right below. */
static void jill_save_app_config(void);

static void jill_load_app_config(void)
{
    char path[64];
    FILE *file;
    JillAppConfig settings;
    int loaded = 0;

    /* A missing/wrong-size config.dat falls back to a real, already-tested display.dat from before
     * this round's filename rename -- wootbeer's own existing display-mode/color/speed/mirror settings
     * shouldn't silently vanish just because the file they live in got renamed out from under them.
     * Tries the pre-rename SIX-int size specifically (JILL_LEGACY_DISPLAY_CONFIG_SIZE) -- the exact
     * size a display.dat written by any previous round of this feature would be -- rather than the
     * full current (now ten-int) struct size, which a real legacy file could never match. Reads
     * into the front of `settings` (matching every field's own on-disk order, since only fields
     * were ever appended, never reordered) and leaves sound_gain/music_gain/sound_enabled/
     * music_enabled at whatever jill_load_app_config()'s own caller-supplied `settings` already
     * held -- overwritten with real static defaults just below before any of the four range checks
     * that would otherwise read them uninitialized. Both paths converge on the exact same "validate
     * every field, keep whichever ones are in range" logic afterward -- a legacy load just starts
     * from a config.dat-shaped `settings` that's missing its own newest four fields, not a
     * fundamentally different code path. */
    settings.sound_gain = jill_sound_gain;
    settings.music_gain = jill_music_gain;
    settings.sound_enabled = soundf;
    settings.music_enabled = jill_music_enabled;
    /* Android port addition, not upstream -- "Remap Gamepad" submenu's own two persisted fields
     * (this struct's own comment above). Same "seed with the live/default value before either file
     * open, so a legacy or short-one-fread() file just leaves it there" shape as every field above. */
    settings.jump_button_keycode = jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_JUMP);
    settings.throw_button_keycode = jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_THROW);
    /* Android port addition, not upstream -- "Touch Options" submenu's own two persisted fields
     * (this struct's own comment above). Same "seed with the live/default value before either file
     * open" shape as every field above. */
    settings.touch_scale_step = s_jill_touch_scale_step;
    settings.touch_opacity_step = s_jill_touch_opacity_step;
#if defined(JILL_EP1)
    /* Android port addition, not upstream -- Episode 1's own COIN TOSS remap binding (this struct's
     * own comment above). Same "seed with the live value before either file open" shape. */
    settings.coin_toss_button_keycode =
        jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_COIN_TOSS);
#endif

    data_path(path, sizeof(path), "config.dat");
    file = fopen(path, "rb");
    if (file != NULL) {
        /* Android port change, not upstream -- was `fread(&settings, sizeof(settings), 1, file) ==
         * 1`, an all-or-nothing read: fread()'s (size, nmemb) form only ever reports whole elements
         * completed, so a config.dat even ONE byte shorter than the CURRENT build's struct made
         * `loaded` false, skipping the whole `if (loaded)` block below and leaving every setting --
         * not just whichever field the size mismatch was actually about -- at its live/default
         * value. Harmless back when the only way a config.dat could be short was "written by an
         * older build, before some field existed" (this function's own display.dat fallback below
         * covers the one real case of that so far), but jill_link_shared_config_file()
         * (jill_jni_bridge.c) now makes config.dat a single file shared across all three episode
         * builds, and Episode 1's own build of this struct is permanently one int longer than
         * Episodes 2/3's (see coin_toss_button_keycode's own comment above) -- so Episode 1 loading
         * a config.dat that Episode 2 or 3 saved most recently would hit this exact short-read case
         * on EVERY SINGLE LAUNCH, not just once on an old install, silently resetting every display/
         * sound/touch/remap setting back to default instead of just leaving coin_toss_button_keycode
         * unset. Swapping to the (1, sizeof(settings)) form instead reports the real BYTE COUNT
         * actually read, so `loaded` is now true for any non-empty read, however short -- `settings`
         * was already fully pre-seeded with live/default values for every field above (see this
         * whole function's own comment), so a short read just leaves whatever trailing bytes it
         * didn't reach at that same pre-seeded value, exactly like the display.dat legacy-migration
         * path already relies on. The `file == NULL` case (config.dat doesn't exist at all) is
         * unchanged -- `loaded` stays false and the display.dat fallback below still runs. */
        loaded = fread(&settings, 1, sizeof(settings), file) > 0;
        fclose(file);
    }
    if (!loaded) {
        data_path(path, sizeof(path), "display.dat");
        file = fopen(path, "rb");
        if (file != NULL) {
            loaded = fread(&settings, JILL_LEGACY_DISPLAY_CONFIG_SIZE, 1, file) == 1;
            fclose(file);
            /* Migrated -- write it straight to the new config.dat so this fallback only ever runs
             * once per install, matching every other "write straight through on every change"
             * precedent this whole file already follows (jill_apply_display_mode() etc.). The old
             * display.dat itself is simply left in place, unused from here on -- harmless, and
             * removing a file the user's own device owns isn't this function's job. */
            if (loaded) jill_save_app_config();
        }
    }
    if (loaded) {
        if (settings.display_mode >= JILL_DISPLAY_DEFAULT &&
            settings.display_mode <= JILL_DISPLAY_MINIMAL) {
            jill_display_mode = settings.display_mode;
        }
        if (settings.color_setting >= JILL_COLOR_DEFAULT && settings.color_setting <= 3) {
            jill_color_setting = settings.color_setting;
        }
        /* Android port addition, not upstream -- same range-validated load as color_setting just
         * above, one field per new Enhancement. */
        if (settings.enemyspeed_setting >= JILL_SPEED_DEFAULT &&
            settings.enemyspeed_setting <= JILL_SPEED_FAST) {
            jill_enemyspeed_setting = settings.enemyspeed_setting;
        }
        if (settings.attackspeed_setting >= JILL_SPEED_DEFAULT &&
            settings.attackspeed_setting <= JILL_SPEED_FAST) {
            jill_attackspeed_setting = settings.attackspeed_setting;
        }
        if (settings.movespeed_setting >= JILL_SPEED_DEFAULT &&
            settings.movespeed_setting <= JILL_SPEED_FAST) {
            jill_movespeed_setting = settings.movespeed_setting;
        }
        if (settings.mirror_enabled >= JILL_MIRROR_OFF && settings.mirror_enabled <= JILL_MIRROR_ON) {
            jill_mirror_enabled = settings.mirror_enabled;
        }
        /* Android port addition, not upstream -- Sound/Music Options submenu's own four fields.
         * Left at their own static defaults (set just above, before either file open) by a legacy
         * display.dat load or a config.dat still short one fread() -- same "range check fails,
         * default already in place" shape every other field above already uses. */
        if (settings.sound_gain <= JILL_SOUND_GAIN_STEPS) {
            jill_sound_gain = settings.sound_gain;
        }
        if (settings.music_gain <= JILL_MUSIC_GAIN_MAX_STEP) {
            jill_music_gain = settings.music_gain;
        }
        if (settings.sound_enabled <= 1) soundf = settings.sound_enabled;
        if (settings.music_enabled <= 1) jill_music_enabled = settings.music_enabled;
        /* Android port addition, not upstream -- "Remap Gamepad" submenu's own two persisted
         * fields. Same range-validated load as every field above, except the "valid range" here is
         * jill_gamepad_remap_is_valid_keycode() (JILL.H) -- either the unbound sentinel or a real
         * member of the 7-button capturable pool -- rather than a plain numeric bound, since a
         * garbage int is otherwise harmless (jill_gamepad_dispatch_raw()'s own live-table lookup
         * would simply never match any real physical keycode) but still worth rejecting outright so
         * a corrupt/foreign config.dat can't leave an action silently, permanently unbindable from
         * this screen's own Reset row until a real capture happens to overwrite it. Written straight
         * through the remap module's own live-table setter, not a jill_* global -- see this struct's
         * own jump_button_keycode/throw_button_keycode comment above. */
        if (jill_gamepad_remap_is_valid_keycode(settings.jump_button_keycode)) {
            jill_gamepad_remap_set_bound_keycode(JILL_GAMEPAD_ACTION_JUMP,
                                                  settings.jump_button_keycode);
        }
        if (jill_gamepad_remap_is_valid_keycode(settings.throw_button_keycode)) {
            jill_gamepad_remap_set_bound_keycode(JILL_GAMEPAD_ACTION_THROW,
                                                  settings.throw_button_keycode);
        }
#if defined(JILL_EP1)
        /* Android port addition, not upstream -- Episode 1's own COIN TOSS remap binding. Same
         * range-validated load as JUMP/THROW just above. */
        if (jill_gamepad_remap_is_valid_keycode(settings.coin_toss_button_keycode)) {
            jill_gamepad_remap_set_bound_keycode(JILL_GAMEPAD_ACTION_COIN_TOSS,
                                                  settings.coin_toss_button_keycode);
        }
#endif
        /* Android port addition, not upstream -- "Touch Options" submenu's own two persisted
         * fields. Same range-validated load as every field above; pushed straight through
         * jill_controls.c's own live setters (not just assigned to the static step variables
         * directly) so the on-screen touch buttons' own layout/opacity are already correct the very
         * first frame, the same "load applies immediately, not just next time the menu happens to
         * open" precedent every other Enhancement/Display/Sound field above already establishes. */
        if (settings.touch_scale_step >= 0 && settings.touch_scale_step < TOUCH_SCALE_NUM_STEPS) {
            s_jill_touch_scale_step = settings.touch_scale_step;
            jill_controls_set_scale_step(s_jill_touch_scale_step);
        }
        if (settings.touch_opacity_step >= 0 &&
            settings.touch_opacity_step <= JILL_TOUCH_OPACITY_NUM_STEPS) {
            s_jill_touch_opacity_step = settings.touch_opacity_step;
            jill_controls_set_opacity(jill_touch_opacity_alpha(s_jill_touch_opacity_step));
        }
    }
    /* Android port addition, not upstream -- temporary diagnostic for wootbeer's own bug report, see
     * the REMAP GAMEPAD APPLY branch's own new diagnostic log comment (pausemenu(), case 'G')
     * for the whole story. `loaded` here says whether config.dat (or a legacy display.dat) was
     * found at all this launch; the two keycodes are the LIVE table's own final values after
     * every check above -- on a normal launch (config.dat found, jump/throw both in range) these
     * should already match whatever REMAP GAMEPAD's own APPLY log last reported saving. */
    JILL_LOGI("jill_load_app_config: loaded=%d jump_keycode=%d throw_keycode=%d", loaded,
              jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_JUMP),
              jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_THROW));
}

static void jill_save_app_config(void)
{
    char path[64];
    FILE *file;
    JillAppConfig settings;

    settings.display_mode = jill_display_mode;
    settings.color_setting = jill_color_setting;
    settings.enemyspeed_setting = jill_enemyspeed_setting;
    settings.attackspeed_setting = jill_attackspeed_setting;
    settings.movespeed_setting = jill_movespeed_setting;
    settings.mirror_enabled = jill_mirror_enabled;
    settings.sound_gain = jill_sound_gain;
    settings.music_gain = jill_music_gain;
    settings.sound_enabled = soundf;
    settings.music_enabled = jill_music_enabled;
    /* Android port addition, not upstream -- "Remap Gamepad" submenu's own two persisted fields,
     * read straight from the remap module's own live table (see this struct's own comment). */
    settings.jump_button_keycode = jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_JUMP);
    settings.throw_button_keycode = jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_THROW);
#if defined(JILL_EP1)
    /* Android port addition, not upstream -- Episode 1's own COIN TOSS remap binding, read straight
     * from the remap module's own live table, same as JUMP/THROW just above. */
    settings.coin_toss_button_keycode =
        jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_COIN_TOSS);
#endif
    /* Android port addition, not upstream -- "Touch Options" submenu's own two persisted fields,
     * read straight from this file's own live step statics (this struct's own comment). */
    settings.touch_scale_step = s_jill_touch_scale_step;
    settings.touch_opacity_step = s_jill_touch_opacity_step;
    data_path(path, sizeof(path), "config.dat");
    file = fopen(path, "wb");
    if (file == NULL) return;
    (void)fwrite(&settings, sizeof(settings), 1, file);
    fclose(file);
}

void loadboard(const char *filename)
{
    byte board_image[JILL_BOARD_BYTES];
    byte player_image[70];
    byte object_image[31];
    byte string_marker[maxobjs];
    FILE *file;
    uword disk_object_count;
    int index, x, y;
    /* Android port addition, not upstream -- wootbeer's own crash report, now with a logcat line to
     * confirm it: "rexit(1) -- curlevel=\"1.JN2\" errno=2 (No such file or directory)". Root cause:
     * upstream's own level-transition strings (JOBJ.c's msg_checkpt()/JMAN.c's dolevelsong(), both
     * ultimately sourced from a checkpoint object's `inside` field, embedded straight in the level
     * file exactly as the original DOS game wrote it) are upper case, "1.JN2" -- harmless on DOS/
     * Windows' own case-insensitive filesystems, the exact same story MUSIC.c's own snd_do() header
     * comment and jill_jni_bridge.c's own header comment (point 2) already tell for AUDIO.EPC and
     * every other asset extension, but not here: JillActivity.java's own copier writes every real
     * asset file lowercase (both of those comments again), and Android's real filesystem is case-
     * sensitive, so a plain fopen("1.JN2") never finds the "1.jn2" actually sitting on disk. Those
     * two existing fixes could just edit the literal in the C source directly, since upstream wrote
     * it there in the first place -- this string comes from the level FILE's own binary data at
     * runtime instead, so there's no source-code literal to fix; lower-cased right here, the one
     * place every real board load (a normal level transition, a checkpoint's own teleport, a death
     * respawn via curlevel) funnels through, rather than needing the same fix repeated at every
     * caller. Matches this project's own established "assets are lowercase" convention exactly --
     * every filename already written lowercase by hand (loadboard(), demoboard[] literals elsewhere
     * in this file) passes through this unchanged. curlevel is set from the ALREADY-lowercased copy
     * below, not the original mixed-case `filename` argument, so a later respawn's own
     * loadboard(curlevel) reopens the same working path without needing to re-normalize anything. */
    char lowercase_filename[64];

    for (index = 0; filename[index] != '\0' && index < (int)sizeof(lowercase_filename) - 1;
         ++index)
        lowercase_filename[index] = (char)tolower((unsigned char)filename[index]);
    lowercase_filename[index] = '\0';
    filename = lowercase_filename;

    for (index = 9; index < SHM_MAX_TABLES; ++index) shm_want[index] = 0;
    shm_want[14] = 1;
    shm_want[46] = 1;
    /* Android port change, not upstream -- was a plain, unbounded strcpy(curlevel, filename),
     * matching upstream exactly. curlevel is a fixed 32-byte buffer (JILL.H) but this function is
     * also called with tempname (64 bytes, the '!' secret-room branch, JUNGLE.c) -- harmless in
     * practice today (this project's own generated temp names are always short), but no different
     * in kind from the unbounded newlevel writes just fixed in JOBJ.c/JMAN.c, so bounded the same
     * way here too rather than leaving one more unchecked copy from a longer buffer into a shorter
     * one. */
    strncpy(curlevel, filename, sizeof(curlevel) - 1);
    curlevel[sizeof(curlevel) - 1] = '\0';
    zapobjs();
    file = fopen(filename, "rb");
    if (file == NULL || fread(board_image, 1, sizeof(board_image), file) == 0) rexit(1);
    if (fread(&disk_object_count, 1, 2, file) == 0) rexit(2);
    if (disk_object_count > maxobjs) rexit(3);

    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y)
            bd[x][y] = jill_read_u16_le(board_image + (x * boardys + y) * 2);

    memset(string_marker, 0, sizeof(string_marker));
    numobjs = (word)disk_object_count;
    for (index = 0; index < numobjs; ++index) {
        if (fread(object_image, 1, sizeof(object_image), file) == 0) rexit(3);
        decode_object(&objs[index], object_image);
        string_marker[index] = objs[index].inside != NULL;
        objs[index].inside = NULL;
    }
    if (fread(player_image, 1, sizeof(player_image), file) == 0) rexit(4);
    decode_player(player_image);
    /* Android port addition, not upstream -- wootbeer's own report: "the cheat text stayed to 'on' this
     * time, but the items were still removed when switching levels." An earlier round of this fix
     * put the re-grant inside initobjs() (JMAN.c), called a few lines up through this function's own
     * zapobjs() call -- but decode_player() just above ALWAYS runs after that and overwrites
     * pl.numinv/pl.inv[] wholesale from whatever this level's own file has embedded for them
     * (upstream's own real per-level player record, not a port addition), silently undoing that
     * earlier re-grant every single time. This is the real last place pl.inv[] changes before
     * loadboard() returns, so it's the only place a re-grant can actually stick. Same reasoning as
     * before otherwise (see jill_cheat_set_all_items()'s own comment, JMAN.c): safe to call
     * unconditionally whenever the cheat is on, since it only ever tops up whatever this decode just
     * wiped, and the two real session-boundary callers (a new game, a loaded save) still end up
     * correctly OFF because they each call jill_cheats_reset() right after their own loadboard()
     * call returns, stripping back out whatever this re-granted. */
    if (jill_cheat_all_items) jill_cheat_set_all_items(1);

    for (index = 0; index < numobjs; ++index) {
        uword length;
        if (!string_marker[index]) continue;
        (void)fread(&length, 1, 2, file);
        objs[index].inside = (char *)malloc((size_t)length + 1);
        (void)fread(objs[index].inside, 1, (size_t)length + 1, file);
    }
    fclose(file);
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y) {
            unsigned cell = board(x, y);
            shm_want[(info[cell].sh >> 8) & 0x3f] = 1;
        }
    for (index = 0; index < numobjs; ++index) {
        int kind = objs[index].objkind;
        shm_want[kindtable[kind]] = 1;
    }
    shm_do();
}

void saveboard(const char *filename)
{
    byte board_image[JILL_BOARD_BYTES];
    byte player_image[70];
    byte object_image[31];
    FILE *file;
    int index, x, y;

    file = fopen(filename, "wb");
    if (file == NULL) rexit(201);
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y)
            jill_write_u16_le(board_image + (x * boardys + y) * 2, bd[x][y]);
    if (!write_exact(file, board_image, sizeof(board_image))) rexit(202);
    (void)write_word_file(file, (uword)numobjs);
    for (index = 0; index < numobjs; ++index) {
        encode_object(object_image, &objs[index]);
        (void)write_exact(file, object_image, sizeof(object_image));
    }
    encode_player(player_image);
    (void)write_exact(file, player_image, sizeof(player_image));
    for (index = 0; index < numobjs; ++index) {
        size_t length;
        if (objs[index].inside == NULL) continue;
        length = strlen(objs[index].inside);
        (void)write_word_file(file, (uword)length);
        (void)write_exact(file, objs[index].inside, length + 1);
    }
    fclose(file);
}

int numlines(void)
{
    int lines = 0, index;
    for (index = 0; index < textmsglen; ++index) lines += textmsg[index] == 13;
    return lines;
}

int jill_getline(int number, char *line, int add_spaces)
{
    int color = 7, current = 0, source = 0, output = 0;
    line[0] = '\0';
    while (source < textmsglen && current < number)
        if (textmsg[source++] == 13) ++current;
    while (source < textmsglen && (unsigned char)textmsg[source] < 32 && textmsg[source] != 13) ++source;
    if (source < textmsglen && textmsg[source] >= '0' && textmsg[source] <= '7')
        color = textmsg[source++] - '0';
    while (source < textmsglen && textmsg[source] != 13 && output < 77) {
        unsigned char character = (unsigned char)textmsg[source++];
        line[output++] = add_spaces ? (char)toupper(character) : (char)character;
    }
    line[output] = '\0';
    return color;
}

void printline(vptype *viewport, int y, int number)
{
    char line[80];
    fontcolor(viewport, jill_getline(number, line, 1), 1);
    wprint(viewport, 0, y, 2, "                                    ");
    wprint(viewport, (viewport->vpxl - (int)strlen(line) * 6) / 2,
           y, 2, line);
}

void ourdelay(void)
{
    uword start = *myclock;
    while ((word)(*myclock - start) < xmsgdelay) { }
}

void dotextmsg(int number)
{
    wintype textwin;
    int c, linecount, y, y0, textys;
    char line[80];
    /* Android port addition, not upstream -- wootbeer's own bug report, continued: the drawboard() fix
     * just added to pausemenu()'s own case 'H' didn't actually fix it ("the pause menu text is
     * drawing over the help screen, the cursor is now showing too and can 'repaint' those areas
     * when I move it"). Real root cause is HERE, not there: this function always calls
     * setpagemode(1) below, unconditionally, to double-buffer its own drawing -- but neither
     * return path below ever restored whatever pagemode this function was actually CALLED with,
     * both just left it hardcoded at 1 (or, for the short-message branch, didn't touch it again at
     * all after the setpagemode(1) up top). That's harmless from play()'s own key_f1 case, which
     * already runs at pagemode 1 anyway (play()'s own top sets it once for the whole session) --
     * but pausemenu() runs at pagemode 0 (immediate, no double-buffering, set by play()'s own
     * key_pause case right before calling it) and calls THIS function from its own case 'H'.
     * Immediately after dotextmsg() returns with pagemode stuck at 1, pausemenu()'s own new
     * drawboard() call draws into pagedraw -- the HIDDEN back buffer setpagemode(1) just switched
     * to (GR.c's own setpagemode(): "pagedraw = 1 - pageshow"), not the buffer actually on screen,
     * so that repaint is invisible. Worse, pausemenu()'s own very next domenu_at() call starts with
     * its OWN setpagemode(1), which calls lcopypage() (GR.c) to seed ITS OWN back buffer as a COPY
     * of whatever is CURRENTLY SHOWN -- which is still Help's own last frame at that point -- so the
     * pause menu's own (smaller) text gets drawn on top of a fresh copy of the help screen, then
     * pageflip()ped to visible: exactly "the menu draws on top of the help screen". domenu_at()'s
     * own setpagemode(0) at ITS OWN end then puts everything back in immediate mode for its per-tick
     * cursor blink, which draws straight to the now-visible buffer and locally overwrites whatever
     * stale pixels the cursor glyph itself touches as it moves -- exactly "the cursor... can repaint
     * those areas when I move it". Saving/restoring pagemode across this whole function (whatever it
     * actually was on entry, not a hardcoded guess) fixes this at the source, for every caller. */
    int saved_pagemode = pagemode;
    dx1hold = 1;
    dy1hold = 1;
    text_get(number);
    if (textmsg != NULL) {
        setpagemode(1);
        defwin(&textwin,
               gamevp->vpx / 8 + 2,
               gamevp->vpy + 16,
               gamevp->vpxl / 16 - 3,
               gamevp->vpyl / 16 - 4,
               0, 0, 0);
        /* Android port addition, not upstream -- same transient mirror-exclude-rect fix as
         * domenu_at()'s own matching comment just above (that comment has the whole "everything
         * flips back while the menu is open" story) -- dotextmsg() draws its OWN window directly
         * into gamevp too (reachable from pausemenu()'s own case 'H', HELP), so it needs the exact
         * same per-window exclusion, not just domenu_at()'s screens. Cleared below, right before
         * moddrawboard() -- the one point both the short-message and long/paged branches below
         * already converge on, so a single clear covers both exit paths. Pushed here, BEFORE
         * drawwin(&textwin) just below (not after, the way this used to be ordered) -- see
         * domenu_at()'s own matching comment, and host_set_mirror_exclude_rect()'s own comment
         * (HOSTANDROID.c), for why this window's own shadow-duplicate fix needs a clean, pre-window
         * snapshot of this rect, which only exists before anything is drawn into it. */
        host_set_mirror_exclude_rect(1, textwin.border.vpx, textwin.border.vpy,
                                     textwin.border.vpx + textwin.border.vpxl,
                                     textwin.border.vpy + textwin.border.vpyl);
        drawwin(&textwin);
        fontcolor(&textwin.inside, 7, 1);
        clearvp(&textwin.inside);
        fontcolor(&textwin.border, jill_getline(0, line, 0), -1);
        titlewin(&textwin, line);
        fontcolor(&textwin.inside, 7, 0);

        textys = textwin.inside.vpyl / 6;
        linecount = numlines();
        if (linecount <= textys) {
            y = (textwin.inside.vpyl - 6 * (linecount - 1)) / 2;
            for (c = 1; c < linecount; ++c) {
                printline(&textwin.inside, y, c);
                y += 6;
            }
            pageflip();
            ourdelay();
            /* Android port change, not upstream -- wootbeer's own "A/B/Start always confirm/cancel,
             * regardless of remap" convention (domenu_at()'s own comment has the whole story) now
             * applies here too: menu_confirm/menu_cancel (A/B, never remapped) and key_pause
             * (Start) join fire1/key_enter/key_space so dismissing Help can never collide with
             * wherever JUMP/THROW currently happen to be bound, the exact same collision that used
             * to make A back out of a menu instead of confirming once JUMP was rebound onto it.
             * Drained here (not just off-flags) same as the entry-side dx1/dy1/key/fire1 wait
             * already was -- whatever button opened Help (often the very A/Start press that picked
             * "HELP" off the pause menu) needs to be fully released before a fresh press counts as
             * dismissing it, or Help would close itself the instant it opened. */
            do checkctrl0(1);
            while (dx1 != 0 || dy1 != 0 || key != 0 || fire1 != 0 || menu_confirm != 0 ||
                   menu_cancel != 0);
            do checkctrl0(1);
            while (key != key_space && key != key_enter && key != key_pause && fire1 == 0 &&
                   menu_confirm == 0 && menu_cancel == 0);
            setpagemode(saved_pagemode);
        } else {
            y0 = 0;
            y = 0;
            for (c = 1; c <= textys; ++c) {
                printline(&textwin.inside, y, c);
                y += 6;
            }
            pageflip();
            setpagemode(0);
            fire1off = 1;
            /* Android port addition, not upstream -- same menu_confirm/menu_cancel carry-over
             * suppression as this function's own short-message branch just above (see that
             * branch's own new comment for the whole story), applied here as off-flags (matching
             * this existing fire1off's own shape) rather than a drain loop, since this path's own
             * very next do-while below already treats them purely as "dismiss", with no separate
             * drain step of its own to fold them into. */
            menu_confirmoff = 1;
            menu_canceloff = 1;
            do checkctrl0(1);
            while (dx1 != 0 || dy1 != 0 || key != 0);
            ourdelay();
            do {
                checkctrl0(0);
                dx1 += (key == key_pgdown) - (key == key_pgup);
                if (dx1 + dy1 < 0 && y0 > 0) {
                    --y0;
                    scrollvp(&textwin.inside, 0, 6);
                    printline(&textwin.inside, 0, y0 + 1);
                } else if (dx1 + dy1 > 0 && y0 + textys < linecount) {
                    ++y0;
                    scrollvp(&textwin.inside, 0, -6);
                    printline(&textwin.inside, 6 * (textys - 1),
                              y0 + textys);
                }
                /* Android port change, not upstream -- see this function's own short-message
                 * branch above for the whole "A/B/Start always dismiss Help" story; same signals
                 * added here. */
            } while (key != key_enter && key != key_escape && key != key_pause && fire1 == 0 &&
                     menu_confirm == 0 && menu_cancel == 0);
            setpagemode(saved_pagemode);
        }
        /* Android port addition, not upstream -- clears the exclude rect pushed above, right as
         * this window closes for good (both branches above converge here). */
        host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
        moddrawboard();
        free(textmsg);
        key = 0;
    }
}

void initboard(void)
{
    int x, y;
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y)
            setboard(x, y, 0);
    gamevp->vpox = 0;
    gamevp->vpoy = 0;
}

void putlevelmsg(int number)
{
    int c, linecount, y;
    char line[80];

    levelmsgclock = *myclock;
    if (number >= 32) return;
    textmsg = (char *)leveltxt[number];
    textmsglen = (int)strlen(textmsg);
    if (textmsg != NULL) {
        setpagemode(1);
        drawwin(&levelwin);
        fontcolor(&levelwin.inside, 7, 1);
        clearvp(&levelwin.inside);
        if (x_ourmode == x_vga && facetable != 0) {
            for (c = 0; c < 16; ++c)
                drawshape(&levelwin.topleft,
                          0x4000 + facetable * 0x100 + c,
                          16 * (c & 3), 16 * (c / 4));
        }
        linecount = numlines();
        y = (levelwin.inside.vpyl - 6 * (linecount - 1)) / 2;
        for (c = 0; c < linecount; ++c) {
            fontcolor(&levelwin.inside, jill_getline(c, line, 0), 1);
            wprint(&levelwin.inside,
                   (levelwin.inside.vpxl - 6 * (int)strlen(line)) / 2,
                   y, 2, line);
            y += 6;
        }
        pageflip();
        moddrawboard();
        /* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_exclude_rect()
         * comment. wootbeer's own report: "the text that pop-ups when you enter a new level/area is
         * flipped as well." levelwin sits at fixed absolute screen coordinates inside *gamevp's own
         * bounds in every display mode (not just Minimal UI), so without this it's caught by the
         * main mirror rect exactly like JOBJ2.c's own msg_mapdemo() banner is -- reusing that same
         * transient exclude slot here rather than jill_hudvp's persistent one, since this window is
         * only actually on screen for as long as donelevelmsg() below is waiting, not for a whole
         * display mode's duration. levelwin.border already holds this window's own absolute
         * on-screen rect (see WINDOWS.H's own wintype/defwin()), so no vpox/vpoy scroll-offset
         * conversion is needed the way msg_mapdemo() needs for its own scrolling world position. */
        host_set_mirror_exclude_rect(1, levelwin.border.vpx, levelwin.border.vpy,
                                     levelwin.border.vpx + levelwin.border.vpxl,
                                     levelwin.border.vpy + levelwin.border.vpyl);
    }
}

void donelevelmsg(void)
{
    int dt, done = 0;
    do checkctrl0(0); while (key != 0);
    do {
        checkctrl0(0);
        dt = (word)(*myclock - levelmsgclock) / 18;
        if (key == key_escape || key == key_enter) done = 1;
        else if (dt >= 2 && (key != 0 || fire1 != 0)) done = 1;
        else if (dt >= 4) done = 1;
    } while (!done);
    /* Android port addition, not upstream -- clears the exclude rect putlevelmsg() pushed above,
     * once this message is actually dismissed. Necessary here (unlike msg_mapdemo()'s own
     * self-renewing per-tick re-claim, JOBJ2.c) because putlevelmsg() only pushes this ONCE per
     * level entry, not every tick, so nothing else would ever clear it -- leaving a permanent stale
     * unmirrored patch on screen for the rest of the level otherwise. */
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
}

void drawcell(int x, int y)
{
    unsigned cell;
    if (x < 0 || x >= boardxs || y < 0 || y >= boardys) return;
    cell = board(x, y);
    if ((info[cell].flags & f_msgdraw) == 0) drawshape(gamevp, info[cell].sh, x * 16, y * 16);
    else (void)msg_block(x, y, msg_draw);
}

void drawboard(void)
{
    int x, y;
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y) modboard(x, y);
    updobjs(0);
    statmodflg = 0;
    refresh(0);
}

void moddrawboard(void)
{
    int x, y;
    for (x = 0; x < boardxs; ++x)
        for (y = 0; y < boardys; ++y) modboard(x, y);
    statmodflg |= mod_screen;
}

static void set_game_layout(void)
{
    scrnxs = normxs;
    scrnys = normys;
    defwin(&ourwin, 0, 0, 19, 10, 4, 5, dialog);
    gamevp = &ourwin.inside;
    cmdvp = &ourwin.topleft;
    statvp = &ourwin.botleft;
    botvp.vpx = 0; botvp.vpy = 188;
    botvp.vpxl = 320; botvp.vpyl = 12;
    botvp.vpox = botvp.vpoy = 0;
    botvp.vphi = botvp.vpback = 0;
    if (facetable != 0 && x_ourmode == x_vga) {
        defwin(&levelwin, 12, 48, 11, 4, 4, 0, 0);
        levelwin.topleft.vpy = levelwin.inside.vpy;
        levelwin.topleft.vpyl = levelwin.inside.vpyl;
    } else {
        defwin(&levelwin, 13, 48, 8, 4, 0, 0, 0);
    }

    /* Android port addition, not upstream -- cache the Default/Force-4:3 layout defwin() just
     * established, and lay out Minimal UI's own HUD strip, both used by jill_apply_display_mode()
     * (see that function's and jill_display_mode's own comments). set_game_layout() itself is
     * only ever called once per play session (jillMain(), before the very first drawgamewin()),
     * so this always captures gamevp's true starting geometry before any display-mode toggle
     * could have touched it. */
    jill_default_gamevp = *gamevp;
    jill_default_scrnxs = scrnxs;
    jill_default_scrnys = scrnys;
    jill_hudvp.vpx = 0; jill_hudvp.vpy = 0;
    jill_hudvp.vpxl = 320; jill_hudvp.vpyl = JILL_MINIMAL_HUD_HEIGHT;
    jill_hudvp.vpox = jill_hudvp.vpoy = 0;
    jill_hudvp.vphi = jill_hudvp.vpback = 0;
}

void play(int demo_flag)
{
    int c, begclock, temppage;
    int cheatchar = 0;
    int cheatcount = 0;
    word saved_score;

    /* Android port fix, not upstream -- wootbeer's own follow-up ask: keep the main menu's own look
     * Default regardless of the player's persisted DISPLAY MODE preference (see JILL.H's own
     * comment on jill_display_mode vs jill_active_display_mode for the whole story). jmenu()'s own
     * loop top and dodemo()'s own top both force jill_active_display_mode to Default before
     * getting here -- this is the one place it gets restored to whatever the player actually chose,
     * since play() (called from all three of jmenu()'s own 'P'/0x10/'R' branches, plus once per
     * demo segment from dodemo()) is the only place real gameplay itself ever runs. Guarded so a
     * mode that was already active (the common case: no forcing happened, or the player's own
     * preference already IS Default) does nothing extra here at all -- this function's own next few
     * lines already redraw everything they need to unconditionally.
     *
     * Goes through jill_redraw_after_display_mode_change() (its own comment has the story), not
     * just an inline drawgamewin() call -- an earlier round of this fix did exactly that (activate,
     * then drawgamewin() alone) and that's what caused wootbeer's own "gray strip roughly in the center
     * of the screen until something animates over it" report: whichever caller drew the level into
     * the OLD geometry just before calling play() (loadboard()+drawboard(), every one of jmenu()'s
     * own 'P'/0x10/'R' branches) already cleared that board's own per-cell dirty flags on the way
     * in, so drawgamewin() alone -- which only clears gamevp's own pixels, never repaints the level
     * -- left the newly-resized area with nothing left to tell refresh()'s own per-tick redraw
     * (JMAN.c) it needed touching. jill_redraw_after_display_mode_change()'s own drawboard() call
     * is what actually repaints the level into the new geometry, and running through its own
     * fout()/pageflip()/fin() bracket here costs nothing extra -- gameplay hasn't presented a frame
     * of its own yet at this point in play(), so there's no stale frame it could ever flash. This
     * function's own setorigin()/drawcmds()/drawstats() a few lines below still run afterward
     * regardless (whether or not this block did anything) -- harmless, cheap repeats when the mode
     * was already correct, and exactly what's needed to also refresh the HUD/CONTROLS content one
     * more time when it wasn't. */
    if (jill_active_display_mode != jill_display_mode) {
        jill_activate_display_mode(jill_display_mode);
        jill_redraw_after_display_mode_change();
    }

    /* Android port addition, not upstream -- wootbeer's own report: starting a new game, the pause
     * menu's own MIRROR row reads ON and controls come out backwards, while the screen itself
     * isn't visibly mirrored at all -- "I think we had a similar bug to this earlier." He's right:
     * this is the same "mirror mode does not apply instantly" staleness this function's own
     * key_escape/'Q' and key_pause cases below already guard against, by re-pushing both
     * host_set_mirror_rect() and host_set_mirror_enabled(jill_mirror_enabled) fresh right at that
     * transition rather than trusting whatever HOSTANDROID.c's own static copies were last left at.
     * Those two cases cover RETURNING to an already-running game; this was the one gap -- STARTING
     * one fresh, jmenu()'s own 'P'/0x10/'R' branches into this function, has no equivalent push of
     * its own. The block just above only pushes it as a side effect of jill_activate_display_mode(),
     * which is itself gated on the display mode actually changing -- true most of the time for a
     * Default-mode player, since jill_active_display_mode and jill_display_mode already agree, so
     * that block (and its own mirror push) never runs at all on an ordinary new game. This one is
     * unconditional -- every real gameplay session passes through here regardless -- so it's the
     * one guaranteed place left to resync both pieces of state fresh from *gamevp's own
     * just-established geometry the instant real control is about to start. Cheap, idempotent
     * insurance if everything was already in sync; a real fix if it wasn't. */
    host_set_mirror_rect(gamevp->vpx, gamevp->vpy, gamevp->vpx + gamevp->vpxl,
                         gamevp->vpy + gamevp->vpyl);
    host_set_mirror_enabled(jill_mirror_enabled);

    putbotmsg("PRESS F1 FOR HELP!", 4);
    initinv();
    setorigin();
    drawcmds();
    drawstats();
    newlevel[0] = '\0';
    setpagemode(1);
    dolevelsong();
    gameover = 0;

    do {
        if (newlevel[0] != '\0') {
            if (newlevel[0] == '*') {
                memmove(newlevel, newlevel + 1, strlen(newlevel));
                strcpy(oursong, newlevel);
                sb_playtune(newlevel);
                newlevel[0] = '\0';
            } else if (newlevel[0] == '#') {
                memmove(newlevel, newlevel + 1, strlen(newlevel));
                strcpy(oursong, newlevel);
                if (!sb_playing()) sb_playtune(newlevel);
                newlevel[0] = '\0';
            } else if (newlevel[0] == '&') {
                memmove(newlevel, newlevel + 1, strlen(newlevel));
                macabort = 2;
                playmac(newlevel);
                strcpy(oursong, newlevel);
                if (!sb_playing()) sb_playtune(newlevel);
                newlevel[0] = '\0';
            } else if (newlevel[0] == '!') {
                int saved_count;
                putlevelmsg(0);
                c = invcount(3);
                saved_count = c;
                saved_score = (word)pl.score;
                loadboard(tempname);
                pl.score = (ulongword)(longword)(word)saved_score;
                (void)remove(tempname);
                while (c-- > 0) addinv(3);
                newlevel[0] = '\0';
                p_reenter(0);
                c = findcheckpt(pl.level);
                if (objs[c].state != 1 || saved_count > 0) {
                    killobj(c);
                } else {
                    putbotmsg("YOU LEFT WITHOUT FINDING A GEM!", 4);
                    moveobj(0, objs[c].x, objs[c].y);
                    pl.level = 0;
                }
                donelevelmsg();
            } else {
                oldlevelnum = pl.level;
                putlevelmsg(pl.level);
                saveboard(tempname);
                saved_score = (word)pl.score;
                loadboard(newlevel);
                pl.score = (ulongword)(longword)(word)saved_score;
                newlevel[0] = '\0';
                pl.level = oldlevelnum;
                p_reenter(0);
                donelevelmsg();
            }
        }

        begclock = *myclock;
        sb_update();
        ++gamecount;
        checkctrl(1);
        key = (word)toupper(key);

        if (key != 0) {
            if (cheatchar == '/') {
                if (cheatcount == 2) {
                    cheatchar = 0;
                    if (key == '0') {
                        if (macrecord) macrecend();
                    } else if (key == 'R') {
                        recordmac("temp.mac");
                    } else if (key == 'C') {
                        playmac("temp.mac");
                    }
                    key = 0;
                }
            }

            if (key == cheatchar) ++cheatcount;
            else {
                cheatcount = 1;
                cheatchar = key;
            }

            if (cheatchar == 'X' && cheatcount == 3) {
                cheatchar = 0;
                pl.health = 8;
                if (!invcount(10)) addinv(10);
                if (!invcount(1)) addinv(1);
                statmodflg |= mod_screen;
            } else if (cheatchar == 'Z' && cheatcount == 3) {
                cheatchar = 0;
                debug = !debug;
                statmodflg |= mod_screen;
            } else if (cheatchar == 'W' && cheatcount == 3) {
                getkey();
                pixwrite(key - '0');
                swrite = 1;
                cheatchar = 0;
            }
        }

        switch (key) {
        case 'N':
            soundf = !soundf;
            statmodflg |= mod_screen;
            break;
        case 'T':
            turtle = !turtle;
            statmodflg |= mod_screen;
            break;
        case 'P':
            do {
                checkctrl0(0);
                sb_update();
            } while (key == 0 && fire1 == 0 && fire2 == 0 && dx1 == 0 && dy1 == 0);
            break;
        default:
            break;
        }

        if (demo_flag && !xdemoflag && countobj(0x43) == 0)
            (void)addobj(0x43, objs[0].x, objs[0].y);

        updbkgnd();
        updobjs(1);
        updbotmsg();
        refresh(pagemode);
        purgeobjs();

        switch (toupper(key)) {
        case 'S':
            temppage = pagedraw;
            pagedraw = pageshow;
            setpages();
            savegame();
            drawcmds();
            pagedraw = (word)temppage;
            setpages();
            key = key_space;
            break;
        case 'R':
            temppage = pagedraw;
            pagedraw = pageshow;
            setpages();
            (void)loadgame();
            dolevelsong();
            drawcmds();
            pagedraw = (word)temppage;
            setpages();
            setorigin();
            moddrawboard();
            key = key_space;
            break;
        case key_f1:
            dotextmsg(1);
            break;
        case key_escape:
        case 'Q':
            /* Android port change, not upstream -- this case (and pausemenu()'s own, just below)
             * used to fully suspend Mirror Mode (host_set_mirror_suspended(), HOSTSDL.H) for as
             * long as askquit()'s own dialog was on screen, since it draws directly into gamevp's
             * own shared page buffer. wootbeer's own bug report: "when entering pause menu while mirror
             * mode is on, everything flips back while the menu is open. and then once it closes
             * there's some mirrored artifacts and such leftover" -- suspending un-mirrored the
             * WHOLE screen, not just this dialog's own small box, so the frozen world visible
             * around/behind it visibly flipped back to its true orientation for as long as it was
             * up. Replaced with domenu_at()'s own new per-window mirror-exclude-rect push (that
             * function's own comment has the whole story, including the "leftover artifacts" half
             * of this same report) -- askquit() is built entirely out of domenu_at(), so it already
             * gets this for free, no call needed here at all any more. */
            setpagemode(0);
            gameover = (word)askquit();
            setpagemode(1);
            /* Android port addition, not upstream, this round -- pausemenu()'s own QUIT TO MAIN
             * MENU case ('M', above) has the whole "session ends at the main menu boundary, so
             * Mirror Mode shouldn't survive it" story -- this is the OTHER path back to the main
             * menu, upstream's own original "REALLY QUIT?" prompt (askquit(), Escape/Q), which
             * predates the pause menu's own QUIT submenu but lands in the exact same place on a YES
             * (gameover=1, straight back to the main menu) for the exact same reason, so it needs
             * the identical reset. Checked against gameover, not unconditionally -- a NO answer
             * keeps playing the current session, where Mirror should obviously be left alone. */
            if (gameover) jill_mirror_enabled = JILL_MIRROR_OFF;
            /* Android port addition, not upstream -- re-pushes the CURRENT *gamevp rect and
             * jill_mirror_enabled right here, not just relying on whatever jill_activate_display_
             * mode() last pushed (that call is itself gated on the display mode actually changing --
             * see this function's own top -- so it may not have run again since this level started).
             * wootbeer's own report: "mirror mode does not apply instantly" -- this closes the only
             * staleness window left in the mirror rect/enabled state, cheap insurance on top of
             * case 'N' (JUNGLE.c's own pausemenu()) already pushing host_set_mirror_enabled()
             * directly the moment the player toggles it. */
            host_set_mirror_rect(gamevp->vpx, gamevp->vpy, gamevp->vpx + gamevp->vpxl,
                                 gamevp->vpy + gamevp->vpyl);
            host_set_mirror_enabled(jill_mirror_enabled);
            /* Android port addition, not upstream -- see jill_activate_display_mode()'s own
             * matching reset for why (HOSTSDL.H's own host_set_mirror_exclude_rect() comment has
             * the whole "MAP"/"DEMO" banner feature). Also cheap insurance here: askquit() already
             * clears its own exclude rect on the way out (domenu_at()'s own comment), so this is
             * normally a no-op, not a load-bearing reset. */
            host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
            moddrawboard();
            break;
        /* Android port addition, not upstream -- key_pause (KEYBOARD.H) is the gamepad Start
         * button (JillView.java), wired to a new real pause menu rather than reusing askquit()'s
         * own "REALLY QUIT?" prompt -- see pausemenu()'s own header comment for the whole
         * feature. Same wrapping askquit() already gets right above: setpagemode(0) so the menu
         * draws over a still frame, setpagemode(1)+moddrawboard() to clean up and force a full
         * redraw of the game screen underneath once it closes, and the same gameover assignment
         * convention (pausemenu() returns 1 for exactly "quit to main menu", matching askquit()'s
         * own YES=1). */
        case key_pause:
            /* Android port change, not upstream -- see the key_escape/'Q' case just above for the
             * whole "everything flips back while the menu is open" story: this case used to
             * suspend Mirror Mode outright for as long as pausemenu() (and everything reachable
             * from it -- every submenu, Sound/Music, Cheats, Enhancements, Remap Gamepad, Touch
             * Options, Display Mode, Help, Save, Restore) was on screen. Every one of those screens
             * now excludes its own on-screen window from the mirror instead (domenu_at()/
             * dotextmsg()/loadsavewin()/jill_gamepad_capture_button()/savegame()'s own comments each
             * have their own piece of the story), so no suspend call is needed here any more. */
            setpagemode(0);
            gameover = (word)pausemenu();
            setpagemode(1);
            /* Android port addition, not upstream -- see the key_escape/'Q' case just above for why
             * this re-push belongs here too. */
            host_set_mirror_rect(gamevp->vpx, gamevp->vpy, gamevp->vpx + gamevp->vpxl,
                                 gamevp->vpy + gamevp->vpyl);
            host_set_mirror_enabled(jill_mirror_enabled);
            /* Android port addition, not upstream -- see jill_activate_display_mode()'s own
             * matching reset for why (HOSTSDL.H's own host_set_mirror_exclude_rect() comment has
             * the whole "MAP"/"DEMO" banner feature). Also cheap insurance here, same as the
             * key_escape/'Q' case just above -- pausemenu()'s own screens already clear their own
             * exclude rect on the way out. */
            host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
            moddrawboard();
            break;
        default:
            break;
        }

        if (demo_flag && !macplay) gameover = 1;
        if (!host_is_open()) gameover = 1; /* SDL transport boundary. */
        while ((word)(*myclock - begclock) < turtle + 1) { }
    } while (!gameover);

    key = 0;
    if (gameover == 2) pageview(200);
    setpagemode(0);
    if (!demo_flag) printhi(1);
}

void pleasewait(void)
{
    wintype waitwin;
    uword start_clock;
    int x;
    int y;

    clrpal();
    setpagemode(1);
    if (xshafile[0] != 'o' || x_ourmode != x_vga) {
        defwin(&waitwin, 6, 56, 11, 3, 0, 0, textbox);
        drawwin(&waitwin);
        fontcolor(&waitwin.inside, 15, -1);
        wprint(&waitwin.inside, 32, 3, 2, "A NEW RELEASE FROM");
        wprint(&waitwin.inside, 30, 10, 1, "Epic MegaGames");
        wprint(&waitwin.inside, 48, 32, 2, "PRODUCED BY");
        wprint(&waitwin.inside, 48, 39, 2, "Tim Sweeney");
        fontcolor(&waitwin.inside, 2, -1);
        wprint(&waitwin.inside,
               (waitwin.inside.vpxl - (int)strlen(JILL_GAME_TITLE) * 8) / 2,
               21, 1, JILL_GAME_TITLE);
        fontcolor(&mainvp, 1, 0);
        wprint(&mainvp, 64, 160, 2, "NOW LOADING, PLEASE WAIT...");

        if (xshafile[0] == 'o') {
            for (x = 0; x <= 5; ++x) {
                drawshape(&mainvp, 0x0c06 + x, x * 32, 0);
                drawshape(&mainvp, 0x0c06 + x, 304 - x * 32, 0);
                drawshape(&mainvp, 0x0c00 + x, x * 32, 168);
                drawshape(&mainvp, 0x0c00 + x, 304 - x * 32, 168);
            }
        }
        pageflip();
        setpagemode(0);
        fadein();
    } else {
        shm_want[34] = 1;
        shm_do();
        clrvp(&mainvp, 0);
        for (x = 0; x < 19; ++x)
            for (y = 0; y < 12; ++y)
                drawshape(&mainvp, 0x6201 + y * 19 + x,
                          x * 16, y * 16);
        clrpal();
        pageflip();
        fadein();
        start_clock = *myclock;
        while ((word)(*myclock - start_clock) < 80 && host_is_open()) { }
        fadeout();
        clrvp(&mainvp, 0);
        pageflip();
        clrvp(&mainvp, 0);
        shm_want[47] = 1;
        shm_want[34] = 0;
        shm_do();
        for (x = 0; x < 19; ++x)
            for (y = 0; y < 12; ++y)
                drawshape(&mainvp, 0x6f01 + y * 19 + x,
                          x * 16, y * 16);
        setpagemode(0);
        clrpal();
        pageflip();
        fadein();
        start_clock = *myclock;
        while ((word)(*myclock - start_clock) < 60 && host_is_open()) { }
        shm_want[47] = 0;
        shm_do();
    }
}

void printhi(int new_high_score)
{
    char string[20];
    int background;
    int position = JILL_HIGH_COUNT;
    int index;
    int character;
    int max_length;

    background = new_high_score ? 4 : 8;
    if (new_high_score) {
        while (position > 0 && high_score[position - 1] < pl.score)
            --position;
        if (position >= JILL_HIGH_COUNT) {
            new_high_score = 0;
        } else {
            for (index = JILL_HIGH_COUNT - 2; index >= position; --index) {
                high_score[index + 1] = high_score[index];
                strcpy(high_name[index + 1], high_name[index]);
            }
            high_score[position] = pl.score;
            high_name[position][0] = '\0';
        }
    }

    fontcolor(cmdvp, 5, background);
    clearvp(cmdvp);
    wprint(cmdvp, 0, 4, 1, "____________");
    fontcolor(cmdvp, 4, background);
    wprint(cmdvp, 4, 2, 2, "HI SCORES");
    fontcolor(cmdvp, 2, background);
    for (index = 0; index < JILL_HIGH_COUNT; ++index)
        wprint(cmdvp, 2, index * 7 + 15, 2, high_name[index]);

    fontcolor(cmdvp, 6, background);
    for (index = 0; index < JILL_HIGH_COUNT; ++index) {
        snprintf(string, sizeof(string), "%d", (int)(longword)high_score[index]);
        for (character = 0; string[character] != '\0'; ++character)
            drawshape(cmdvp, 0x03d0 + (byte)string[character],
                      62 - (int)strlen(string) * 4 + character * 4,
                      index * 7 + 15);
    }

    if (new_high_score) {
        fontcolor(cmdvp, 7, background);
        snprintf(string, sizeof(string), "%d", (int)(word)high_score[position]);
        max_length = (54 - (int)strlen(string) * 4) / 6;
        if (max_length >= 12) max_length = 12;
        /* Android port addition, not upstream -- wootbeer's own report: "when quitting a game and the
         * hi score screen pops up, I need an android keyboard to enter my hi score name". Same bug
         * savegame()'s own winput() call already had fixed (see that call's own comment) -- without
         * this, nothing ever tells Android a soft keyboard is wanted here, so winput()'s own real,
         * unchanged keystroke loop (WIN.c) just waits forever with no way to type. This is the only
         * OTHER winput() call site in the whole codebase (grepped for it) -- savegame()'s own bracket
         * was the sole precedent, this one was simply missed the first time around. */
        host_show_keyboard();
        winput(cmdvp, 2, position * 7 + 15, 2,
               high_name[position], max_length);
        host_hide_keyboard();
        if (high_name[position][0] == '\0')
            (void)loadcfg();
        else
            (void)savecfg();
        printhi(0);
    }
}

int loadsavewin(const char *message, const char *blank_message)
{
    char string[2] = { 0, 0 };
    word oldclock;
    int c;
    int cursor_frame;

    dx1hold = 1;
    dy1hold = 1;
    fire1off = 1;
    /* Android port addition, not upstream -- matching fix to domenu()'s own new fire1off/
     * fire2off lines (see that function's own comment for the full explanation): this loop's own
     * exit condition below now reads fire2 (A) as confirm, same as domenu(), but only fire1off
     * was ever set here (from upstream's original B-confirms behavior). Without also suppressing
     * fire2, a carried-over held A press from whatever screen opened this picker (e.g. confirming
     * "Restore" in the pause menu with A) instantly re-confirmed slot 0 here -- exactly what wootbeer
     * reported. */
    fire2off = 1;
    /* Android port addition, not upstream -- wootbeer's own bug report: rebound JUMP (fire1) onto the
     * gamepad A button, then found this exact screen's own accept/abort logic below firing "abort"
     * on an A press instead of confirming a slot -- A's dedicated always-on menu_confirm signal
     * (KEYBOARD.H's own comment) WAS asserting correctly, but this loop below still read raw
     * fire1/fire2 directly (matching domenu_at()'s OWN pre-fix logic, and the exact same bug for
     * the exact same reason -- see that function's own fix comment for the whole story), so
     * whichever gamepad button JUMP/THROW happened to be bound to at the moment could silently
     * fight the dedicated A-confirms/B-aborts convention this screen's own on-screen prompt
     * promises. Same entry-side carried-over-held-press suppression as domenu_at()'s own
     * menu_confirmoff/menu_canceloff, now that this loop reads those two signals instead. */
    menu_confirmoff = 1;
    menu_canceloff = 1;
    /* Android port addition, not upstream -- same transient mirror-exclude-rect fix as domenu_at()'s
     * own matching comment (that comment has the whole "everything flips back while the menu is
     * open" story), applied here since this screen draws directly into cmdvp (the CONTROLS side
     * panel viewport) rather than a defwin()'d window of its own. cmdvp sits OUTSIDE the mirror rect
     * in Default/Force 4:3 mode (mirroring only ever covers *gamevp's own bounds, HOSTSDL.H's own
     * host_set_mirror_rect() comment), so this would be a no-op there -- but Minimal UI mode
     * deliberately expands *gamevp to cover the ENTIRE screen (jill_activate_display_mode()'s own
     * comment), which puts cmdvp's own fixed panel coordinates inside the mirror rect too. Excluding
     * cmdvp's own bounds here closes that gap for Save/Restore, reachable from the exact same
     * pausemenu() this whole fix is about. Cleared below, right before each of this function's own
     * two return points. */
    host_set_mirror_exclude_rect(1, cmdvp->vpx, cmdvp->vpy, cmdvp->vpx + cmdvp->vpxl,
                                 cmdvp->vpy + cmdvp->vpyl);
    fontcolor(cmdvp, 5, 1);
    clearvp(cmdvp);
    wprint(cmdvp, 0, 4, 1, "____________");
    wprint(cmdvp, 0, 56, 1, "____________");
    fontcolor(cmdvp, 4, 1);
    wprint(cmdvp, 6, 2, 2, message);
    fontcolor(cmdvp, 3, 1);
    for (c = 0; c < JILL_SAVE_COUNT; ++c) {
        snprintf(string, sizeof(string), "%d", c + 1);
        wprint(cmdvp, 8, c * 8 + 13, 2, string);
    }
    for (c = 0; c < JILL_SAVE_COUNT; ++c)
        wprint(cmdvp, 20, c * 8 + 13, 2,
               save_name[c][0] != '\0' ? save_name[c] : blank_message);
    fontcolor(cmdvp, 2, 1);
    wprint(cmdvp, 14, 65, 2, "PRESS");
    wprint(cmdvp, 6, 77, 2, "TO ABORT");
    fontcolor(cmdvp, 4, 1);
    wprint(cmdvp, 12, 71, 2, "ESCAPE");
    fontcolor(cmdvp, 7, 1);

    cursor_frame = 6;
    do {
        string[1] = '\0';
        checkctrl0(0);
        cursor_frame = (cursor_frame & 7) + 1;
        string[0] = (char)cursor_frame;
        wprint(cmdvp, 1, selected_save * 8 + 13, 2, string);
        oldclock = *myclock;
        while (*myclock == oldclock && host_is_open()) { }
        wprint(cmdvp, 1, selected_save * 8 + 13, 2, " ");
        selected_save += dx1 + dy1;
        /* Android port change, not upstream -- same wrap-around fix as domenu_at()'s own cursor
         * movement (see that function's own comment for wootbeer's own ask/reasoning); this screen has
         * its own separate, older cursor-movement code rather than going through domenu(), so it
         * needs the identical fix applied here too instead of inheriting it for free. */
        selected_save = ((selected_save % JILL_SAVE_COUNT) + JILL_SAVE_COUNT) % JILL_SAVE_COUNT;
        /* Android port addition, not upstream -- same A-confirms/B-backs-out convention
         * domenu()'s own accept logic uses now (see that function's own comment): fire1 (B) used
         * to accept the highlighted slot here, same as upstream's own logic; it's dropped from
         * this loop's own exit condition below (fire2/key_pause added instead) and instead
         * treated as an abort, right alongside key_escape, just below -- fitting, since this
         * screen's own on-screen prompt already reads "PRESS ESCAPE TO ABORT".
         *
         * Android port change, not upstream -- wootbeer's own bug report, see this function's own
         * menu_confirmoff/menu_canceloff comment above for the whole story. fire2/fire1 (whatever
         * THROW/JUMP happen to be bound to right now) swapped for menu_confirm/menu_cancel (the
         * dedicated, never-remapped A/B signals, KEYBOARD.H's own comment) -- same fix domenu_at()
         * itself just got, applied here since this screen has always had its own separate
         * accept/abort logic rather than going through domenu_at(). key_enter/key_escape/key_pause
         * keep meaning exactly what they always did. */
    } while (host_is_open() && menu_confirm == 0 && key != key_enter && key != key_escape &&
             key != key_pause && menu_cancel == 0);

    /* Android port addition, not upstream -- clears the exclude rect pushed above, right as this
     * screen closes for good (either return path below). */
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
    if (!host_is_open() || key == key_escape || menu_cancel) return -1;
    return selected_save;
}

int loadgame(void)
{
    char savefile[520];
    char mapfile[520];
    char suffix[8];
    FILE *file;
    int slot = loadsavewin("LOAD GAME", "<empty>");
    if (slot < 0 || save_name[slot][0] == '\0') return 0;

    snprintf(suffix, sizeof(suffix), "%d", slot);
    data_path(savefile, sizeof(savefile), JILL_SAVE_PREFIX);
    strcat(savefile, ".");
    strcat(savefile, suffix);
    data_path(mapfile, sizeof(mapfile), JILL_SAVE_PREFIX);
    strcat(mapfile, "m.");
    strcat(mapfile, suffix);

    file = fopen(mapfile, "rb");
    if (file != NULL) {
        fclose(file);
        (void)remove(tempname);
        (void)copyfile(mapfile, tempname);
    }
    loadboard(savefile);
    /* Android port addition, not upstream -- same session-boundary reset a new game gets (jmenu()'s
     * own 'P' case above has the whole comment); this one call site covers both real callers of
     * loadgame() (jmenu()'s own 'R' case and pausemenu()'s own 'L' case) for free. Also un-sticks
     * the CHEATS submenu's own "ALL ITEMS: ON" label from whatever session was running before this
     * load -- encode_player() already guarantees a save can never actually contain a cheat-granted
     * item (see that function's own comment), so without this the label could read ON against an
     * inventory that no longer has anything to show for it, the exact same stale-label bug GoT
     * Android's own Cheat Codes menu hit for the identical reason. */
    jill_cheats_reset();
    return 1;
}

void savegame(void)
{
    char savefile[520];
    char mapfile[520];
    char suffix[8];
    char name[JILL_SAVE_NAME_LEN];
    FILE *file;
    int slot = loadsavewin("SAVE GAME", "");
    if (slot < 0) return;

    strcpy(name, save_name[slot]);
    /* Android port addition, not upstream -- see HOSTSDL.H's own host_show_keyboard()/
     * host_hide_keyboard() comment for why: without this, nothing ever tells Android a soft
     * keyboard is wanted here, so winput()'s own real, unchanged keystroke loop (WIN.c) just
     * waits forever and the player can never actually type a save name. Bracketing only this one
     * call (not loadsavewin() above, which picks a slot with the D-pad, no typing involved) is
     * deliberate -- the keyboard should only be up while there's somewhere to type. */
    /* Android port addition, not upstream -- same cmdvp mirror-exclusion loadsavewin() itself just
     * pushed and cleared above (that call's own comment has the whole story) -- loadsavewin() only
     * covers ITS OWN drawing into cmdvp (picking a slot), not this separate winput() call right here
     * (typing the actual save name), which draws into cmdvp too, after loadsavewin() already
     * returned and cleared its own exclude rect. */
    host_set_mirror_exclude_rect(1, cmdvp->vpx, cmdvp->vpy, cmdvp->vpx + cmdvp->vpxl,
                                 cmdvp->vpy + cmdvp->vpyl);
    host_show_keyboard();
    winput(cmdvp, 20, slot * 8 + 13, 2, name, 7);
    host_hide_keyboard();
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
    if (key == key_escape || name[0] == '\0') return;
    strcpy(save_name[slot], name);

    snprintf(suffix, sizeof(suffix), "%d", slot);
    data_path(savefile, sizeof(savefile), JILL_SAVE_PREFIX);
    strcat(savefile, ".");
    strcat(savefile, suffix);
    data_path(mapfile, sizeof(mapfile), JILL_SAVE_PREFIX);
    strcat(mapfile, "m.");
    strcat(mapfile, suffix);

    saveboard(savefile);
    file = fopen(tempname, "rb");
    if (file != NULL) {
        fclose(file);
        (void)copyfile(tempname, mapfile);
    }
    savecfg();
}

void drawgamewin(void)
{
    clearvp(&mainvp);
    /* Android port addition, not upstream -- Minimal UI mode: gamevp already covers the screen
     * space the CONTROLS/INVENTORY border+panels would normally occupy (see
     * jill_activate_display_mode()), so none of that gets drawn -- just clear gamevp itself and
     * let jill_draw_minimal_hud() (called from drawstats(), see that function's own new branch)
     * own the strip above it. Reads jill_active_display_mode, not jill_display_mode -- see JILL.H's
     * own comment on the two -- so the main menu keeps its normal bordered CONTROLS/INVENTORY look
     * even when the player's persisted preference is Minimal UI. */
    if (jill_active_display_mode == JILL_DISPLAY_MINIMAL) {
        fontcolor(gamevp, 7, 8);
        clearvp(gamevp);
        return;
    }
    drawwin(&ourwin);
    fontcolor(cmdvp, 7, 8);
    clearvp(cmdvp);
    fontcolor(statvp, 7, 8);
    clearvp(statvp);
    fontcolor(gamevp, 7, 8);
    clearvp(gamevp);
    fontcolor(&ourwin.border, xbordercol, -1);
    titlewin(&ourwin, JILL_GAME_TITLE);
    titlebot(&ourwin, "INVENTORY");
    titletop(&ourwin, "CONTROLS");
}
void noisemaker(void)
{
    static const char noise_keys[] =
        "1234567890-=QWERTYUIOP[]ASDFGHJKL;'ZXCVBNM,./\1\2\3\4\5\6";
    int c;

    /* Android port addition, not upstream -- wootbeer's own explicit ask, after finding this screen
     * unreachable-exit on touch controls and gamepad both: "is there a way to add a small opaque
     * button ... in order to show/hide an android keyboard?" and "can we bind 'start' and 'select'
     * on gamepad to exit back to the main menu." jill_controls_set_noisemaker_active()'s own comment
     * (jill_controls.h) has the whole BTN_KEYBOARD_TOGGLE story; the gr_present_page() call right
     * after it is required, not optional -- this function's own loop below never calls pageflip()/
     * gr_present_page() again on its own (checkctrl0() only polls input, it never redraws), so
     * without one extra present here the new button would set its "active" flag but never actually
     * appear on screen until something else happened to trigger a redraw. gr_present_page() (GR.H),
     * deliberately not pageflip() (GR.H) -- pageflip() also toggles which page buffer is the current
     * front one, which would show stale content from the other buffer instead of a clean re-draw of
     * what's already on screen; gr_present_page() just re-presents the current front buffer
     * unchanged, so this really only adds the touch overlay's own new button, nothing else moves. */
    jill_controls_set_noisemaker_active(1);
    gr_present_page();

    fire1off = 0;
    do {
        sb_update();
        checkctrl0(0);
        key = (word)toupper(key);
        /* Android port addition, not upstream -- wootbeer's own follow-up ask, after Start/Select were
         * first wired to exit this screen: "with gamepad enabled can we also make select open and
         * close the android keyboard instead of closing from noisemaker?" Select no longer exits at
         * all (jill_controls_toggle_keyboard()'s own comment has the reasoning) -- it now drives the
         * exact same show/hide toggle BTN_KEYBOARD_TOGGLE's own touch button does, so a gamepad
         * player can pop the real keyboard open without ever touching the screen. `key` is cleared
         * right after so a stray key_select (0x17, not one of noise_keys[]'s own byte values below
         * regardless, so this is defensive rather than strictly load-bearing) can never be mistaken
         * for anything else this loop checks afterward. Confirmed safe against the real soft
         * keyboard's own operation: a physical gamepad button press is an ordinary hardware KeyEvent
         * delivered straight to JillView's own focused SurfaceView by the platform, completely
         * separate from the IME's own InputConnection/text-composition path (the one this screen's
         * typed QWERTY-key notes actually use) -- showing or hiding the soft keyboard never steals
         * that focus away, so Select keeps reaching this exact same code whether the keyboard is
         * currently up or down, with nothing to interfere with on either side. */
        if (key == key_select) {
            jill_controls_toggle_keyboard();
            key = 0;
        }
        for (c = 0; noise_keys[c] != '\0'; ++c)
            if ((byte)noise_keys[c] == key)
                snd_play(1, c + 1);
        /* Android port note, not upstream -- key_pause below is what actually lets a player leave
         * this screen at all without a physical Enter/Escape key: it's already sent by both the
         * on-screen PAUSE touch button and the gamepad Start button (see BTN_PAUSE's/K_PAUSE's own
         * comments), so checking it here is what makes "there's already a way with touch controls to
         * exit" true -- before this line there wasn't one. Select (key_select) used to also exit
         * here; it doesn't anymore -- see the keyboard-toggle check just above for why. */
    } while (host_is_open() && key != key_enter && key != key_escape && key != key_pause);

    /* Android port addition, not upstream -- see jill_controls_set_noisemaker_active()'s own comment
     * for the full story: this also force-hides the real Android soft keyboard and resets
     * BTN_KEYBOARD_TOGGLE's own remembered on/off state back to off if the player left it toggled
     * on, so leaving Noisemaker (by any of the three exits above -- Enter, Escape, or key_pause)
     * never leaves the keyboard silently still open over the main menu behind it. */
    jill_controls_set_noisemaker_active(0);
}

void pageview(int page)
{
    vptype saved_viewport;
    int n;
    int c;
    uword start_clock;
    /* Android port addition, not upstream -- wootbeer's own ask: a small "ANDROID PORT BY WOOTBEER"
     * credit line, in the same cyan (fontcolor() hi=3 -> VGA palette index 11, "light cyan" --
     * see vgapal[] just above in GR.c) that the real CREDITS page's own "please share this game"
     * line uses. That real text is NOT drawn by this function, or anywhere else in this engine --
     * it traced back to genuine 1992 Epic MegaGames level art: this whole function just jumps the
     * camera to whichever checkpoint object has counter==page and calls drawboard() on it (below),
     * so the CREDITS screen's own text is baked into that board's own tile data, the same way any
     * other level's scenery is. There's no string constant anywhere to edit to add a matching
     * line next to it. Instead, this overlays real engine-drawn text on top of that baked art,
     * captured once here (not re-checked against the mutating `page` local below, which
     * pageview()'s own do-while loop reassigns to objs[n].xd for a chained follow-up screen) so it
     * keeps showing on every board the real CREDITS entry (page 12) chains through, not just the
     * first. back=-1 (transparent, glyphs only) is this same file's own established idiom for
     * text drawn over already-rendered art rather than a fresh cleared viewport -- see e.g. this
     * function's neighboring "please wait" overlay's own fontcolor(&waitwin.inside, 15, -1) call --
     * and wprintc() (WIN.c) is a real, already-ported upstream helper that centers text in a
     * viewport by its own vpxl width; nothing else in this codebase happens to call it yet, but it
     * needs no changes to do exactly what's wanted here against mainvp's real 320px width. */
    int credits_page = (page == 12);

    scrnxs = fullxs;
    scrnys = fullys;
    do {
        n = -1;
        setpagemode(1);
        fout();
        for (c = 0; c < numobjs; ++c)
            if (objs[c].objkind == obj_checkpt &&
                objs[c].counter == page)
                n = c;

        if (n > 0) {
            saved_viewport = *gamevp;
            *gamevp = mainvp;
            gamevp->vpox = objs[n].x;
            gamevp->vpoy = objs[n].y;
            drawboard();
            if (credits_page) {
                fontcolor(gamevp, 3, -1);
                wprintc(gamevp, 4, 2, "ANDROID PORT BY WOOTBEER");
            }
            pageflip();
            setpagemode(0);
            fin();
            *gamevp = saved_viewport;

            if (page == 99) {
                noisemaker();
            } else {
                start_clock = *myclock;
                fire1off = 1;
                do {
                    checkctrl0(0);
                    sb_update();
                } while (host_is_open() &&
                         ((key == 0 && fire1 == 0) ||
                          (word)(*myclock - start_clock) < 18));
            }

            if (objs[n].xd != 0)
                page = objs[n].xd;
            else
                n = -1;
        }
    } while (n > 0 && host_is_open());

    scrnxs = normxs;
    scrnys = normys;
    setpagemode(1);
    fout();
    drawgamewin();
    drawboard();
    pageflip();
    setpagemode(0);
    fin();
}

/* Android port addition, not upstream -- sets *gamevp/scrnxs/scrnys/host_set_display_mode() for
 * whichever mode is passed, and nothing else: no persistence, no redraw, no fade bracket. Split out
 * of jill_apply_display_mode() below (which still does all three of those, for the pause menu's own
 * "DISPLAY MODE" row) so jmenu()/dodemo()/play() can each flip jill_active_display_mode (JILL.H's
 * own comment has the full two-variable story) synchronously, back-to-back with their own existing
 * redraw calls, without also re-triggering a persistence write or a duplicate fade for a mode the
 * player never actually chose from the menu -- see each call site's own comment for why every one
 * of them is already followed immediately by a real redraw of its own, so nothing here needs to
 * draw anything itself to avoid a visibly stale frame.
 *
 * Default and Force 4:3 share the same JUNGLE.c-side layout (Force 4:3 is purely a HOSTANDROID.c
 * host_present() presentation change, see host_set_display_mode() there) -- only Minimal UI changes
 * anything here, expanding gamevp into the screen space the CONTROLS/INVENTORY panels would
 * otherwise occupy.
 *
 * *gamevp is overwritten in place (never repointed) the same way pageview()'s own `saved_viewport`
 * swap works above -- gamevp always points at &ourwin.inside; only its *contents* ever change.
 * Callers that go on to call setorigin() (JMAN.c) afterward get vpox/vpoy recentered on the player
 * from scratch using the new scrnxs/scrnys/gamevp size, so there's no stale-scroll-position glitch
 * even though this unconditionally resets *gamevp from the cached baseline first -- exactly like
 * the startup init sequence (jillMain(), above) already does: set_game_layout() -> drawgamewin() ->
 * drawcmds() -> setorigin() -> drawboard(). */
static void jill_activate_display_mode(int mode)
{
    jill_active_display_mode = mode;

    *gamevp = jill_default_gamevp;
    if (mode == JILL_DISPLAY_MINIMAL) {
        /* Full screen width AND height, from y=0 down to the top of botvp's own message line
         * (y 188) -- deliberately including the HUD strip's own rows now, not stopping just below
         * them: wootbeer's own explicit ask, after seeing an earlier round leave that strip a dead,
         * non-scrolling black bar: "I wanted it to fill in like the rest of the screen with more
         * level". jill_minimal_hud_tick() (JMAN.c's refresh() calls it every tick, see JILL.H's
         * declaration) keeps real level art scrolling under the HUD's own text/icons the same way
         * it does everywhere else in gamevp -- see that function's own comment for how, and its
         * one known limitation. fullxs/fullys (JILL.H) is the same "wider than normal" tile-count
         * pageview() itself already uses for its own oversized viewport -- pixaddr_vga() (GR.c)
         * clips all pixel writes to the physical 320x200 screen regardless, so sizing gamevp
         * beyond it is safe. */
        gamevp->vpx = 0;
        gamevp->vpy = 0;
        gamevp->vpxl = 320;
        gamevp->vpyl = 188;
        scrnxs = fullxs;
        /* Android port note, not upstream -- fullys (13 tiles = 208px), not something tighter --
         * an earlier round of this fix tried scrnys=12 (192px, the closest tile count that still
         * covers 188 real pixels with no slack) on the theory that fullys overshot gamevp's own
         * real 188px height by too much. That theory had the direction backwards. drawshape()
         * (GR.c) already clips every shape against the *viewport's own* vp->vpyl/vp->vpxl before
         * ever drawing it ("if (y < vp->vpyl && ... )"), on top of pixaddr_vga()'s own physical-
         * screen clip -- so scrnys being a few tiles taller than gamevp's own real pixel height is
         * always harmless, nothing ever actually lands outside gamevp's own rectangle because of
         * it. undershooting scrnys is the real danger: JMAN.c's refresh() uses vpoy/16+scrnys-1 as
         * the FAR edge of its own per-tick dirty-cell redraw range, computed from vpoy's own
         * *floored* tile position -- when vpoy isn't itself a multiple of 16 (i.e. mid-scroll,
         * which is most of the time), that floor can leave up to 15 real pixels short of what
         * scrnys*16 alone would suggest, so scrnys has to cover a full tile of slack beyond
         * gamevp's own real height to guarantee the bottommost dirty row is never skipped in the
         * worst case. 188px needs 12 tiles minimum (192px) just to cover it at all, so scrnys=12
         * left zero slack -- exactly wrong, not "safely tight": it silently dropped the bottommost
         * tile row's redraw for most sub-tile scroll phases, a real, screen-visible "some tiles
         * near the bottom of gamevp just don't get redrawn until something else happens to touch
         * them" bug (stale/gray patches, popping back in whenever a scroll phase or a nearby
         * update happens to catch them -- exactly the kind of intermittent glitch that reads as
         * flicker). fullys (13 tiles) is 12 (the tile-exact minimum) plus one full tile of slack,
         * covering the worst case with room to spare -- and it's not an arbitrary choice: it's the
         * exact same "+1 tile of scroll slack beyond the tile-exact visible size" relationship
         * set_game_layout()'s own Default-mode scrnys (normys=11) already has to its own real
         * 160px/10-tile gamevp height, an upstream convention this just matches instead of
         * fighting. */
        scrnys = fullys;
    } else {
        scrnxs = jill_default_scrnxs;
        scrnys = jill_default_scrnys;
    }

    host_set_display_mode(mode);

    /* Android port addition, not upstream -- see JILL.H's own jill_mirror_enabled comment and
     * HOSTSDL.H's own host_set_mirror_rect() comment for the whole Mirror Mode feature. Pushed here,
     * right alongside host_set_display_mode() just above, so Mirror Mode's own screen-space rect
     * always matches whichever gamevp this function just finished establishing -- every caller that
     * needs *gamevp's geometry refreshed already calls this function (jmenu()'s/play()'s own
     * loop-top hooks, jill_apply_display_mode()), so there's no separate call site to keep in sync.
     * Exclusive x2/y2 (vpx+vpxl, vpy+vpyl), matching host_set_mirror_rect()'s own contract.
     * host_set_mirror_enabled() is pushed here too rather than only from pausemenu()'s own MIRROR
     * row, so a fresh jill_mirror_enabled loaded from display.dat (jill_load_app_config(),
     * near main()'s own top) actually reaches HOSTANDROID.c the first time this function runs at
     * startup, not just after the player next toggles it. */
    host_set_mirror_rect(gamevp->vpx, gamevp->vpy, gamevp->vpx + gamevp->vpxl,
                         gamevp->vpy + gamevp->vpyl);
    host_set_mirror_enabled(jill_mirror_enabled);
    /* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_exclude_rect()
     * comment for the full history and current users (the pause-menu-family functions and
     * putlevelmsg()). Reset to inactive every time *gamevp's geometry is (re)established here --
     * each of those current users pushes it active only transiently, for as long as its own window
     * is on screen, and clears it again before returning, so this is only ever the "nothing to
     * exclude right now" default, cheap insurance against a display-mode change landing mid-window
     * somehow, not a fight against any per-tick push. */
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
    /* Android port history, not upstream -- this call used to push host_set_mirror_hud_exclude_rect()
     * active for Minimal UI mode (see that function's own comment, HOSTSDL.H, for the full original
     * story: wootbeer's own "the minimal ui text is flipped" report, then a follow-up round that instead
     * over-excluded and got "the entire top row... is all unmirrored", then a THIRD round that
     * tightened the excluded width to the HUD's own real content -- which fixed that, but broke in a
     * new way wootbeer caught next: "now the minimal ui is mirrored on both sides of the screen", since
     * excluding a destination region can't stop OTHER destination columns from mirroring FROM it.
     * jill_draw_minimal_hud() (below) now fixes the HUD's own mirror behavior a completely different
     * way instead -- pre-writing an already-flipped copy of itself directly into jill_video so
     * host_present()'s own uniform, unexclusion whole-strip mirror pass undoes it back to normal
     * (that function's own comment has the mechanics, and wootbeer's own fix idea this implements
     * literally: "mirror the minimal ui itself first when mirror mode is on, and then when the
     * mirror mode code takes over, it's flipped to look 'normal'") -- so no exclude rect is needed
     * here for the HUD at all any more. Pushed inactive explicitly, not just left alone, so this
     * function's own state is always a complete, self-contained reset regardless of what a future
     * caller might otherwise assume was still active from some earlier round. host_set_mirror_hud_
     * exclude_rect() itself is left in place, unused but callable, the same "kept as working,
     * tested infrastructure" reasoning HOSTSDL.H's own host_set_mirror_suspended() comment gives for
     * its own now-unused function. */
    host_set_mirror_hud_exclude_rect(0, 0, 0, 0, 0);
}

/* Android port addition, not upstream -- the redraw half of what jill_apply_display_mode() used to
 * do inline, split out so every caller that forces jill_active_display_mode to something other
 * than what it already was (jmenu()'s own loop-top hook, play()'s own restore-on-entry hook, both
 * below) gets the exact same known-correct sequence pageview()'s own camera jumps already use,
 * instead of a hand-rolled subset. Order matters here, not just presence: drawgamewin()/drawcmds()
 * redraw the CONTROLS/INVENTORY border+panels (or their deliberate absence in Minimal UI) for
 * whichever mode jill_activate_display_mode() just switched *gamevp to, then setorigin() recenters
 * vpox/vpoy on the player using that same new geometry, and only THEN does drawboard() actually
 * repaint the level -- drawboard() draws into gamevp at whatever vpox/vpoy currently holds, so
 * calling it before setorigin() would paint the old, stale scroll position into the new-sized
 * viewport. This exact ordering bug is what caused wootbeer's own "gray strip roughly in the center of
 * the screen until something animates over it" report: an earlier round's play()-entry hook called
 * drawgamewin() alone with no follow-up drawboard() at all, which clears gamevp's own pixels but
 * leaves the board's own per-cell dirty flags untouched (they'd already been cleared by whichever
 * drawboard() call the caller made *before* switching modes, into the old, now-abandoned geometry)
 * -- so refresh()'s own per-tick dirty-cell redraw (JMAN.c) never had a reason to repaint any of
 * it, and the freshly-cleared area just stayed gray until some unrelated update (an object moving
 * through it) happened to dirty a cell there directly. */
static void jill_redraw_after_display_mode_change(void)
{
    setpagemode(1);
    fout();
    drawgamewin();
    drawcmds();
    setorigin();
    drawboard();
    pageflip();
    setpagemode(0);
    fin();
}

/* Android port addition, not upstream -- applies whichever display mode pausemenu()'s own new
 * "DISPLAY MODE" row (case 'V') just cycled to: this is the ONE place a fresh choice from that
 * menu goes through, so it's the one place that persists it (jill_display_mode, display.dat) --
 * jill_activate_display_mode() (above) does the actual *gamevp/scrnxs/scrnys/host_set_display_mode()
 * work, jill_redraw_after_display_mode_change() (just above) gives it the same smooth transition
 * pageview()'s own camera jumps already get. */
static void jill_apply_display_mode(int new_mode)
{
    jill_display_mode = new_mode;
    /* Android port addition, not upstream -- write straight through on every change, same
     * precedent the God of Thunder Android port's own AUDIOSET.DAT save calls use for its gain
     * sliders (see jill_save_app_config()'s own comment). This is the single place
     * jill_display_mode ever actually changes (pausemenu()'s own case 'V' only ever calls this),
     * so there's exactly one path that needs to persist it, not two slightly different copies. */
    jill_save_app_config();

    jill_activate_display_mode(new_mode);
    jill_redraw_after_display_mode_change();
}

/* Android port addition, not upstream -- window_y is a new 9th parameter, always 64 (the exact
 * literal this function itself used to hardcode) at every pre-existing call site, so nothing about
 * any of those changes. wootbeer's own report: adding pausemenu()'s own "DISPLAY MODE" row pushed that
 * one menu from 11 lines (title + 10 choices) to 12 -- one line too many for the fixed y=64/
 * bottom-edge-188 budget every domenu() screen used to share (11 lines is exactly the tallest that
 * still fits: yl16 = numlines()/2+1 = 6, border.vpyl = 6*16+28 = 124, 64+124 = 188, precisely
 * botvp's own top edge (JILL.H's own screen layout, set_game_layout() -- deliberately never
 * overlapping the bottom message line). 12 lines rounds yl16 up to 7 (140px tall), pushing the
 * window's own bottom edge to 204 -- past botvp AND past the physical 200px screen bottom, exactly
 * the "text doesn't fit" wootbeer reported. Only pausemenu()'s own outer call (JUNGLE.c, below) passes
 * a smaller window_y, moved up just enough to keep that same bottom-edge-188 convention with its
 * own now-taller window -- every other domenu() screen keeps the exact window_y=64 it always had.
 * window_width/window_x (the window's own horizontal size/position) are untouched by any of this:
 * they already size the window well within the fixed 320px-wide logical framebuffer every display
 * mode renders into (Force 4:3/Minimal UI only change how that same 320x200 picture gets presented
 * on screen, never its own internal layout, see jill_activate_display_mode()'s own comment) -- so
 * there's no real risk of this ever overflowing sideways on a narrower-aspect device like wootbeer's
 * own Retroid Pocket Nova example, only the vertical budget this parameter now fixes. */
/* Android port addition, not upstream -- domenu() itself (just below) is now a thin wrapper around
 * this; every pre-existing call site is completely untouched (domenu() still takes the exact same
 * nine arguments, still always starts highlighted on the first row). choice_ptr is the new part:
 * NULL for every one of those pre-existing callers (domenu()'s own wrapper passes it), or a pointer
 * to a caller-owned int for a menu that redraws itself in its own for(;;) loop and wants the cursor
 * to survive from one redraw to the next -- pausemenu()'s own CHEATS/ENHANCEMENTS submenus (wootbeer's
 * own report: "when toggling the cheats on and off in the menu the cursor's position resets after
 * each item. I'd like it to stay on that item.") are the only two callers that pass a real pointer,
 * each keeping its own small `cursor` local alive across its whole loop -- see either submenu's own
 * call site for the pattern. When choice_ptr is non-NULL, its value on entry seeds this call's own
 * starting `choice` (clamped defensively in case a stale value from a differently-sized menu ever
 * got left in it) and its final value is written back out before returning, so the next call in the
 * same loop picks up exactly where this one left off. */
/* slider_mask/slider_dx are new, Android-addition-only parameters -- wootbeer's own follow-up ask,
 * after the Sound/Music Options submenu (case 'N' below) already shipped with its two gain rows
 * only cycling one step per Fire-confirm: "can the left and right controls, d pad, and sticks, be
 * used to move those sliders left and right instead of going up and down to other menu options?"
 * Every pre-existing call (domenu()'s own wrapper just below, and the CHEATS/ENHANCEMENTS submenus)
 * passes slider_mask=0/slider_dx=NULL and is completely unaffected -- see the dx1/dy1 handling
 * just below for exactly what changes, and only when a real mask bit is set.
 *
 * slider_mask: one bit per `choice` row (bit N == row N) that should treat LEFT/RIGHT (dx1) as an
 * in-place value adjustment instead of the normal "move the cursor to another row" meaning dx1
 * already has for every row. Only the Sound/Music submenu's own two gain rows (SOUND, MUSIC) set
 * any bits here -- every other row in that SAME submenu (SOUND EFFECTS/MUSIC toggles, BACK) is
 * untouched by this at all, dx1 still moves the cursor off of them exactly like before.
 *
 * slider_dx: 0 on every return except one -- LEFT/RIGHT was pressed while highlighting a
 * slider_mask row, in which case this is set to -1 (LEFT) or +1 (RIGHT) and the return value
 * (`key`) is key_table[choice], the exact same value a Fire-confirm on that same row would already
 * produce. The caller (case 'N' below) tells the two apart by checking slider_dx itself: nonzero
 * means "adjust by this amount", zero means "this was a real Fire/Start confirm, do whatever that
 * row's own confirm action already does" -- so a slider row keeps its existing Fire-cycles-one-
 * step-and-wraps behavior for a controller/keyboard with no separate analog left/right (or a
 * player who just prefers pressing Fire), while LEFT/RIGHT now ALSO works, without needing a
 * confirm press first. May be NULL when slider_mask is 0 (every non-slider caller passes NULL). */
static int domenu_at(const char *message, const char *key_table, int first_choice_line,
                      int choices, int timeout, int indent, int window_x, int window_width,
                      int window_y, int *choice_ptr, unsigned slider_mask, int *slider_dx)
{
    wintype menu;
    char line[80];
    word timeout_clock;
    word move_clock = 0;
    int accept;
    int table_index;
    int choice = (choice_ptr != NULL && *choice_ptr >= 0 && *choice_ptr < choices)
                      ? *choice_ptr : 0;
    /* Different from `choice` on purpose (matching this function's own original choice=0/
     * previous_choice=1 pair exactly when choice_ptr is NULL) so the very first iteration below
     * always draws the cursor at `choice`'s own real starting row instead of assuming it's 0. */
    int previous_choice = (choice == 0) ? 1 : 0;
    int flash = 0;
    int line_number;
    int cursor_x;
    /* Android port addition, not upstream -- temporary diagnostic for wootbeer's own bug report: "I
     * want 'a' to always confirm on menus, 'b' to always cancel... but the bug is after I've
     * rebound or unbound those buttons, the menu functionality of those two doesn't work as
     * described anymore." Every signal this function's own accept/back logic below reads
     * (fire1/fire2/menu_confirm/menu_cancel/key) is logged, but only on an actual CHANGE from the
     * previous poll -- edge-triggered, not once-per-tick, so this stays silent while nothing is
     * happening instead of flooding logcat at ~60Hz -- so a real repro (rebind Jump/Throw away
     * from B/A, open any domenu_at()-based menu, press physical A/B) produces a short, readable
     * trace of exactly which signal did or didn't assert. Safe to leave in permanently (cheap,
     * only fires on a real transition) but flagged here as a debugging aid in case it's worth
     * pulling once this bug is confirmed fixed. */
    int log_prev_fire1 = -1, log_prev_fire2 = -1, log_prev_menu_confirm = -1,
        log_prev_menu_cancel = -1, log_prev_key = -1;

    /* Android port addition, not upstream -- wootbeer's own report after testing the A-confirms/
     * B-backs-out change above: picking "Restore" from the pause menu (itself a domenu() choice,
     * confirmed by pressing A) immediately re-confirmed the very first slot in the save/load
     * picker with no chance to choose. Root cause: fire1/fire2 (GAMECTRL.c's checkctrl()) are
     * CONTINUOUS held-button state, not one-shot presses -- if A is still physically held when
     * this new domenu() (or loadsavewin(), see that function's own matching fix) starts polling,
     * its very first checkctrl0() call sees fire2 already 1 and instantly accepts. GAMECTRL.c's
     * own fire1off/fire2off is the established fix for exactly this (already used by loadsavewin()
     * below and pageview() above for the same reason, just never applied here or extended to
     * fire2): checkctrl()'s "if (fire2) fire2 ^= fire2off;" suppresses fire2 for as long as the
     * button stays continuously held, and only clears once it's actually released -- so setting
     * both flags here, before the very first checkctrl0() call below, makes a carried-over A or B
     * press from whatever screen opened this menu register as "still held from before" instead of
     * "a fresh press", requiring a real release-and-press-again to confirm or back out. key_pause
     * (Start) doesn't need this: it comes from k_read()'s one-shot key queue, not a held-state
     * flag, so it can never falsely re-trigger this way. */
    fire1off = 1;
    fire2off = 1;
    /* Android port addition, not upstream -- same carried-over-held-press suppression as fire1off/
     * fire2off just above, for the new always-on menu_confirm/menu_cancel signals (see KEYBOARD.H's
     * own key_menu_confirm/key_menu_cancel comment for the whole "Remap Gamepad" menu-lockout-
     * prevention feature these are part of). */
    menu_confirmoff = 1;
    menu_canceloff = 1;

    /* Android port addition, not upstream -- reset every entry (including the non-slider-row/NULL
     * case, harmlessly) so a caller that checks this after the loop below never sees a stale value
     * left over from this same function's own PREVIOUS call in the same outer redraw loop -- see
     * this parameter's own comment above for the whole feature. */
    if (slider_dx != NULL) *slider_dx = 0;

    textmsg = (char *)message;
    textmsglen = (int)strlen(textmsg);
    defwin(&menu, window_x, window_y, window_width, numlines() / 2 + 1, 0, 0, 2);
    /* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_exclude_rect()
     * comment. wootbeer's own bug report: "when entering pause menu while mirror mode is on, everything
     * flips back while the menu is open. and then once it closes there's some mirrored artifacts
     * and such leftover." Every domenu_at() screen (the top-level pause menu itself, askquit(),
     * jmenu(), and every submenu here -- SOUND/MUSIC, CHEATS, ENHANCEMENTS, DISPLAY MODE's own
     * cycling row, REMAP GAMEPAD/TOUCH OPTIONS' row list, the QUIT submenu) used to rely on
     * play()'s own key_escape/'Q'/key_pause cases fully suspending Mirror Mode (host_set_mirror_
     * suspended(), HOSTSDL.H) for as long as any of them was on screen, since this window's own
     * `menu` box draws directly into the same shared gamevp pixels the (frozen) game world does.
     * That was a blunter fix than it needed to be: suspending un-mirrors the ENTIRE screen, not just
     * this window's own small box, so the frozen world visible around/behind the menu visibly
     * "flips back" to its true orientation for as long as the menu is up, then flips again on close
     * -- exactly wootbeer's first symptom. Excluding just `menu.border`'s own rect here instead (the
     * same transient-exclude-rect pattern putlevelmsg()/msg_mapdemo() already established for their
     * own on-screen windows) leaves the world mirrored the whole time, correctly, and only ever
     * un-mirrors the menu's own box. The suspend/resume dance is also very likely the real source of
     * wootbeer's second symptom (leftover mirrored artifacts after closing): host_present() re-mirrors
     * the SAME underlying pixel buffer completely fresh every frame from whatever's actually in it,
     * so simply suspending and resuming can't itself corrupt anything permanently -- but the
     * unmirrored menu pixels this window drew were baked directly into that shared buffer, and nothing
     * forces a full board redraw again until moddrawboard()'s own next real refresh() actually gets
     * around to it, leaving a brief window where stale, never-meant-to-be-mirrored menu pixels get
     * mirrored anyway once suspension lifts. Excluding this window's own rect sidesteps that
     * entirely, the same as level-entry messages and the MAP/DEMO banner already do -- see
     * play()'s own key_escape/'Q'/key_pause cases for the matching removal of the old suspend calls.
     * Cleared right before this function returns, below -- single entry/exit, no risk of leaking an
     * active exclude rect out to whatever screen (or real gameplay) redraws next.
     *
     * Android port addition, not upstream, this round -- wootbeer's own follow-up report, after the
     * top-level-menu-showing-through fixes above (pausemenu()'s own case 'N'/'G' comments have that
     * whole story): "with mirror mode on gamepad and sound sound menus actually mirror under
     * themselves to the left side now when I open them." Different bug, same family: excluding
     * `menu.border` from the mirror stops THIS window from being mirrored, but every OTHER, still-
     * mirrored destination column whose own mirror SOURCE happens to fall inside `menu.border` still
     * reads its real pixels and reproduces them, backwards, wherever that lookup lands -- this
     * window's own mirror-symmetric "shadow". Barely visible for the narrower (window_width=9)
     * screens, glaring for SOUND/MUSIC and REMAP GAMEPAD/TOUCH OPTIONS (window_width=13, almost as
     * wide as *gamevp itself in Default mode), which is exactly the "gamepad and sound" wootbeer named.
     * HOSTANDROID.c's own host_set_mirror_exclude_rect() now handles this itself: given this rect
     * BEFORE anything is drawn into it, it snapshots the clean (pre-window) pixels there, writes a
     * correctly-mirrored-looking copy of them into this window's own shadow rect, and excludes that
     * shadow rect from the ordinary mirror pass too -- so the shadow shows ordinary mirrored
     * background instead of a backwards copy of this menu. That "BEFORE anything is drawn" part is
     * exactly why this call moved up here, above drawwin(&menu) just below, instead of staying right
     * after it the way it used to -- see that function's own comment (HOSTANDROID.c) for the whole
     * mechanism and why every other caller (dotextmsg(), jill_gamepad_capture_button()) needed the
     * same reordering. */
    host_set_mirror_exclude_rect(1, menu.border.vpx, menu.border.vpy,
                                 menu.border.vpx + menu.border.vpxl,
                                 menu.border.vpy + menu.border.vpyl);
    drawwin(&menu);
    setpagemode(1);
    timeout_clock = *myclock;
    for (line_number = numlines() - 1; line_number >= 0; --line_number) {
        int color = jill_getline(line_number, line, 0);
        fontcolor(&menu.inside, color, -1);
        wprint(&menu.inside,
               4 + (line_number >= first_choice_line) * indent,
               line_number * 8 + 8, 2, line);
    }
    pageflip();
    setpagemode(0);
    cursor_x = (indent - 12) & ~7;

    for (;;) {
        if (!host_is_open()) {
            key = 'Q';
            break;
        }
        sb_update();
        if (++flash >= 12) flash = 0;
        if ((flash & 1) != 0 || previous_choice != choice) {
            drawshape(&menu.inside, 0x4709, cursor_x,
                      (first_choice_line + previous_choice) * 8 + 8);
            drawshape(&menu.inside, 0x0201 + (flash >> 1), cursor_x,
                      (first_choice_line + choice) * 8 + 8);
        }
        previous_choice = choice;
        checkctrl0(0);
        key = (word)toupper(key);

        /* Android port addition, not upstream -- see this function's own log_prev_fire1 comment
         * above for the whole story. */
        if ((int)fire1 != log_prev_fire1 || (int)fire2 != log_prev_fire2 ||
            (int)menu_confirm != log_prev_menu_confirm ||
            (int)menu_cancel != log_prev_menu_cancel || (int)key != log_prev_key) {
            JILL_LOGI("domenu_at: fire1=%d fire2=%d menu_confirm=%d menu_cancel=%d key=%d ('%c')"
                      " choice=%d",
                      fire1, fire2, menu_confirm, menu_cancel, key,
                      (key >= 32 && key < 127) ? (char)key : '?', choice);
            log_prev_fire1 = fire1;
            log_prev_fire2 = fire2;
            log_prev_menu_confirm = menu_confirm;
            log_prev_menu_cancel = menu_cancel;
            log_prev_key = key;
        }

        /* Android port addition, not upstream -- see this function's own slider_mask/slider_dx
         * comment above. Checked BEFORE the ordinary cursor-move branch just below, and using the
         * exact same move_clock repeat-rate gate, so a slider row "steals" LEFT/RIGHT for itself
         * (in-place value adjust) while every other row keeps dx1's old meaning (move the cursor)
         * completely unchanged. Breaks out of this whole loop immediately, the same way the
         * timeout branch below already does without going through the ordinary accept/key_table
         * dispatch further down -- necessary, not just convenient: this function draws `message`
         * exactly once, before this loop even starts, so the only way the caller's own new gain
         * value ever actually appears on screen is for THIS call to end and the caller's own outer
         * redraw loop (case 'N' below) to regenerate `message` from scratch and call this function
         * again, the identical "redraw the whole submenu on every value change" pattern a Fire-
         * confirmed cycle on the same row already relies on today. */
        if (slider_dx != NULL && dx1 != 0 && (slider_mask & (1u << choice)) != 0 &&
            abs((word)(*myclock - move_clock)) > 1) {
            move_clock = *myclock;
            *slider_dx = (dx1 > 0) ? 1 : -1;
            key = (byte)key_table[choice];
            break;
        }
        if (dx1 + dy1 != 0 && abs((word)(*myclock - move_clock)) > 1) {
            move_clock = *myclock;
            choice += dx1 + dy1;
            /* Android port change, not upstream -- wootbeer's own ask: "when the cursor is at the very
             * top of the menu, it cannot wrap around to the bottom item after the next press, I
             * would like to add that, and of course being able to move the cursor to the bottom,
             * then back to top item." Wraps both directions in one shot instead of the two old
             * separate clamps (which just stopped dead at row 0/choices-1) -- the double modulo
             * handles C's own implementation-defined-looking-but-actually-well-defined negative
             * remainder (a plain `% choices` on a negative choice can itself come back negative),
             * and also covers choice overshooting past either end by more than one row in a single
             * press (dx1+dy1 can be +-2 on a diagonal d-pad read), not just the +-1 case a simple
             * if/else wrap would only handle correctly. choices is always >=1 for every real menu
             * here, so this can never divide by zero. */
            choice = ((choice % choices) + choices) % choices;
            timeout_clock = *myclock;
        }
        if ((word)(*myclock - timeout_clock) > 300 && timeout) {
            key = 'D';
            break;
        }

        accept = 0;
        if (key == key_escape) key = 'Q';
        /* Android port addition, not upstream -- wootbeer's own explicit ask: on every menu, A and
         * Start both confirm the highlighted choice, and B backs out one layer instead of
         * confirming, REGARDLESS of whatever JUMP/THROW currently happen to be remapped to.
         *
         * This used to also OR in fire1/fire2 (whatever JUMP/THROW are CURRENTLY bound to) as an
         * additional confirm/back trigger, on top of the dedicated menu_confirm/menu_cancel
         * signals below -- the idea being "fire1/fire2 still ALSO confirm/back, so a menu never
         * loses BOTH ways to navigate it". wootbeer's own bug report (rebinding JUMP onto the gamepad
         * A button, then finding A silently backed OUT of every menu instead of confirming) is
         * exactly the failure that additive design could never actually prevent: the fire1 check
         * doesn't care WHICH physical button JUMP is bound to, so once a player rebinds JUMP onto
         * A itself, pressing A asserts fire1 (read here as "back") at the exact same instant it
         * asserts menu_confirm (read here as "confirm") -- and since fire1 is checked FIRST, back
         * always won, no matter what the player actually meant. Same failure mode, mirrored, for
         * THROW rebound onto B (fire2 would need to assert alongside menu_cancel, but fire2 was
         * only ever checked as a CONFIRM trigger, not back, so that particular combination was
         * safe -- JUMP-onto-A was the one real collision, confirmed via this function's own new
         * diagnostic log against wootbeer's actual test).
         *
         * Fixed by dropping fire1/fire2 from this check entirely -- menu_confirm/menu_cancel
         * (dedicated, never-remapped, KEYBOARD.H's own key_menu_confirm/key_menu_cancel comment)
         * are now the ONLY gamepad-button-driven signals this function reads, so A/B can never
         * collide with wherever JUMP/THROW happen to live. key_pause (Start) and key_enter/
         * key_space (keyboard) round out confirm; key_escape already becomes 'Q' just above,
         * which only ever matches a real key_table entry, not a universal back -- menu_cancel (B)
         * is the only gamepad back trigger now. JUMP/THROW keep meaning jump/throw everywhere
         * real gameplay (JPLAYER.c) reads fire1/fire2 directly -- completely unchanged, this was
         * always only about what THIS function treats as confirm/back. */
        if (menu_cancel) {
            key = key_back;
            accept = 1;
        } else if (key == key_enter || key == key_space || key == key_pause || menu_confirm) {
            key = (byte)key_table[choice];
            accept = 1;
        } else {
            for (table_index = 0; table_index < (int)strlen(key_table); ++table_index)
                if ((byte)key_table[table_index] == key) accept = 1;
        }
        if (accept) break;
    }
    if (choice_ptr != NULL) *choice_ptr = choice;
    /* Android port addition, not upstream -- clears the exclude rect pushed above, right as this
     * window closes for good (this is domenu_at()'s own single return point). Necessary here
     * (unlike msg_mapdemo()'s own self-renewing per-tick re-claim, JOBJ2.c) since it's only ever
     * pushed once per call, not every tick. */
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
    return key;
}

/* Android port addition, not upstream -- see domenu_at()'s own comment just above for the whole
 * cursor-persistence story. Every pre-existing call site keeps calling this exact function with
 * its exact original nine arguments, completely unaware anything changed underneath it. */
int domenu(const char *message, const char *key_table, int first_choice_line,
           int choices, int timeout, int indent, int window_x, int window_width, int window_y)
{
    return domenu_at(message, key_table, first_choice_line, choices, timeout, indent,
                      window_x, window_width, window_y, NULL, 0, NULL);
}

/* Android port addition, not upstream -- the "Remap Gamepad" submenu's own "press a button for
 * this action" capture step (pausemenu()'s case 'G', below). wootbeer's own explicit ask: "let's again
 * model after the one we made for god of thunder... same apply, default, cancel options" -- see
 * JILL.H's own JILL_GAMEPAD_ACTION_JUMP/_THROW comment for the whole feature.
 *
 * Deliberately NOT built on domenu_at() the way every other menu screen in this file is -- there's
 * no key_table of choices to confirm here, only jill_gamepad_remap_poll_capture() (jill_jni_bridge.c)
 * actually reporting a real captured Android keyCode, one raw gamepad button forwarded straight
 * from JillView.java's own jillGamepadButtonRaw() (that file's own comment has the whole native-side
 * design). Draws its own small window directly with the exact same defwin()/drawwin()/wprint()/
 * pageflip() primitives domenu_at() itself uses just above, then polls once per tick (checkctrl0(0),
 * the same per-tick pacing every other menu screen here already relies on) until either a button is
 * captured or the player backs out.
 *
 * Start (key_pause) or ESC (key_escape) cancels -- deliberately NOT B/fire1/menu_cancel, unlike
 * domenu_at()'s own confirm/back checks just above: B is one of the 7 capturable buttons a player
 * might legitimately want to bind THIS action to, and checking menu_cancel here would make that
 * impossible (bind JUMP to B, and the same press that's supposed to capture B would also read as
 * "cancel" and back out before the capture ever resolves) -- exactly the God of Thunder Android
 * port's own "press B to bind B" bug (got_menu.c's own resync_menu_edge_state() comment) that this
 * sidesteps by construction rather than needing a fix after the fact: Start/ESC simply aren't in
 * the capturable pool (JillView.java never raw-forwards them), so they can never collide with a
 * real capture the way B could.
 *
 * Returns the captured Android keyCode, or JILL_GAMEPAD_UNBOUND_KEYCODE (JILL.H) if the player
 * backed out via Start/ESC instead -- pausemenu()'s own case 'G' leaves the staged binding
 * untouched on that sentinel, exactly like picking no new button at all. */
static int jill_gamepad_capture_button(int action)
{
    wintype capwin;
    char message[128];
    char line[80];
    int line_number;
    int captured;

    snprintf(message, sizeof(message),
             "7REBIND: %s\r"
             "2Press a button for\r"
             "2this action...\r"
             "3(Start/ESC cancels)",
             action == JILL_GAMEPAD_ACTION_JUMP ? "JUMP" : "THROW");
    textmsg = message;
    textmsglen = (int)strlen(textmsg);
    defwin(&capwin, 10, 48, 13, numlines() / 2 + 1, 0, 0, 2);
    /* Android port addition, not upstream -- same transient mirror-exclude-rect fix as domenu_at()'s
     * own matching comment (that comment has the whole "everything flips back while the menu is
     * open" story) -- this screen draws its own window directly rather than going through
     * domenu_at() (see this function's own header comment on why), so it needs the same exclusion
     * applied here too. Cleared below, right before this function's own single return. Pushed here,
     * BEFORE drawwin(&capwin) just below (not after) -- same reordering, and the same reason
     * (host_set_mirror_exclude_rect()'s own comment, HOSTANDROID.c, needs a clean pre-window
     * snapshot of this rect), as domenu_at()'s own matching call. */
    host_set_mirror_exclude_rect(1, capwin.border.vpx, capwin.border.vpy,
                                 capwin.border.vpx + capwin.border.vpxl,
                                 capwin.border.vpy + capwin.border.vpyl);
    drawwin(&capwin);
    setpagemode(1);
    for (line_number = numlines() - 1; line_number >= 0; --line_number) {
        int color = jill_getline(line_number, line, 0);
        fontcolor(&capwin.inside, color, -1);
        wprint(&capwin.inside, 4, line_number * 8 + 8, 2, line);
    }
    pageflip();
    setpagemode(0);

    jill_gamepad_remap_start_capture();
    captured = JILL_GAMEPAD_UNBOUND_KEYCODE;
    for (;;) {
        if (!host_is_open()) break;
        checkctrl0(0);
        if (key == key_escape || key == key_pause) break; /* backed out -- see this function's own
                                                             * header comment for why not B/fire1/
                                                             * menu_cancel. */
        captured = jill_gamepad_remap_poll_capture();
        if (captured != JILL_GAMEPAD_UNBOUND_KEYCODE) break;
    }
    jill_gamepad_remap_cancel_capture();
    /* Android port addition, not upstream -- wootbeer's own report: "when in the remap gamepad
     * sub-menu, and pushing 'b' to remap it to something, it exits the sub-menu." A and B are the
     * only two of the 7 capturable buttons that ALSO drive an always-on menu signal
     * (key_menu_confirm/key_menu_cancel, KEYBOARD.H) that this function deliberately does NOT
     * check above (see this function's own header comment on why not). But that signal is still
     * live and true for as long as the physical button stays down -- if the player is still
     * physically holding A/B the instant this function returns (the normal case: the very button
     * that was just captured is still down), the caller's own next domenu_at() call for the row
     * list would otherwise see menu_confirm/menu_cancel already asserted and read it as a brand
     * new confirm/cancel press before the player ever consciously pressed it there, immediately
     * backing (or confirming) its way back out -- exactly the God of Thunder Android port's own
     * "press B to bind B" bug class (that port's own resync_menu_edge_state() comment), just
     * arrived at through this port's different (toggle-suppressed, not edge-detected) confirm/
     * back mechanism. Waiting HERE for a real release, rather than trusting the next domenu_at()
     * call's own entry-side fire1off/menu_canceloff suppression to win a race against a still-held
     * button, removes the timing dependency entirely -- a no-op for X/Y/L1/R1/THUMBL, which never
     * touch either signal. */
    while (host_is_open() && (menu_confirm || menu_cancel)) {
        checkctrl0(0);
    }
    /* Android port addition, not upstream -- clears the exclude rect pushed above, right as this
     * window closes for good (this is this function's own single return point). */
    host_set_mirror_exclude_rect(0, 0, 0, 0, 0);
    return captured;
}

/* Android port addition, not upstream -- wootbeer's own report: "after mapping jump to b, when
 * pushing b to exit the pause menu, jill jumps at the same time." Root cause: B (bound to Jump/
 * fire1 by default, or whatever the player has remapped Jump to via the new Remap Gamepad
 * submenu) is ALSO how domenu_at() detects "back out of this menu" (fire1/menu_cancel, see that
 * function's own header comment) -- the exact same physical press that backs OUT of the pause
 * menu is still being held the instant play()'s own game loop resumes and reads fire1 directly
 * (JPLAYER.c) for a jump. domenu_at()'s own fire1off/fire2off suppression only protects a menu
 * FROM a press carried INTO it from before (see that function's own entry-side comment) -- there
 * was never a matching guard for a press carried the OTHER way, out of the last menu screen and
 * into live gameplay. This closes that gap generically for every pausemenu() exit path a
 * still-held Jump/Throw button could leak out of: waits for both fire1 and fire2 to read false (a
 * real release) before returning control to gameplay, so the very next checkctrl() tick in play()
 * always starts from a clean, unheld state, whatever button the player used to leave. */
static void jill_drain_menu_exit_buttons(void)
{
    while (host_is_open() && (fire1 || fire2)) {
        checkctrl0(0);
    }
}

int askquit(void)
{
    snd_play(1, 22);
    (void)domenu("7REALLY QUIT?\r4YES\r2NO\r", "YN", 1, 2, 0, 20, 13, 6, 64);
    return key == 'Y';
}

/* Android port addition, not upstream -- builds one Sound/Music Options submenu gain row's own
 * "PREFIX: [bar] NN%" label text (pausemenu()'s own 'N' case, below) -- shared between the SOUND
 * and MUSIC rows there, which differ only in their own prefix and max_step (JILL_SOUND_GAIN_STEPS
 * for Sound's own plain 0-100% range, JILL_MUSIC_GAIN_MAX_STEP for Music's own extra headroom past
 * that -- see MUSIC.H's own jill_sound_gain/jill_music_gain comment). Real precedent for the bar-
 * of-'='/'-' shape itself: the God of Thunder Android port's own got_menu.c format_gain_label().
 * `bar` is sized for the larger of the two callers' own max_step (JILL_MUSIC_GAIN_MAX_STEP) always,
 * not whichever one this particular call needs -- one fixed-size stack buffer, cheap and simple,
 * rather than a VLA or a caller-sized one. Every real percentage here is a clean multiple of 10
 * (step*10) since both sliders only ever move in whole 10%-of-JILL_SOUND_GAIN_STEPS steps -- no
 * rounding to account for, unlike a raw float gain would need. */
static void jill_format_gain_row(char *out, size_t out_size, const char *prefix, int step,
                                  int max_step)
{
    char bar[JILL_MUSIC_GAIN_MAX_STEP + 1];
    int i;
    int filled = (step < 0) ? 0 : (step > max_step) ? max_step : step;
    for (i = 0; i < max_step; ++i) bar[i] = (i < filled) ? '=' : '-';
    bar[max_step] = '\0';
    snprintf(out, out_size, "%s: [%s] %d%%", prefix, bar, step * 10);
}

/* Android port addition, not upstream -- Jill's own reconstructed source never had an in-game
 * pause menu at all (real DOS play had nothing beyond askquit()'s own "REALLY QUIT?" prompt,
 * reached from Escape/Q). wootbeer asked for one modeled on the God of Thunder Android port's own
 * pause menu: Start opens it (play()'s own key_pause case, just above), Resume/Sound-Music/Remap
 * Gamepad/Display Mode/Speed/Save/Restore/Help/Cheats/Enhancements/Quit in that order, with Quit
 * opening its own Continue Game/Quit to Main Menu/Quit to DOS submenu.
 *
 * Built entirely out of domenu()/defwin()/drawwin() -- the exact same real menu machinery every
 * other screen in this file (jmenu(), askquit(), dotextmsg()) already uses, including the window-
 * border shapes (0x43xx, drawwin() above) that come straight out of whichever episode's own
 * jillN.sha is currently loaded. That's the entire reason wootbeer's purple/red/orange per-episode
 * border request needed no code here at all: SHM.c's shm_init() already loaded the right shape
 * table for whichever of the three jillhostN binaries is running (see CMakeLists.txt's own
 * comment), and drawwin() was already drawing this same "dialog" style border in each episode's
 * own real color for the game's other menus -- this menu just inherits that for free by using the
 * same primitives, the same way it inherits the font/cursor/flash-highlight look of every other
 * domenu() screen.
 *
 * Speed is a real, working toggle today (turtle, the exact same global the pre-existing 'T' hotkey
 * in play()'s own switch above already flips) -- its current ON/OFF state is baked into the message
 * string fresh every time this menu (re)draws, via the snprintf() below, so flipping it and staying
 * in the menu shows the new state immediately. Sound/Music used to be this same shape (a single
 * ON/OFF toggle, soundf -- still the exact global play()'s own separate, still-live 'N' hotkey
 * flips for a quick sound-effects-only mute mid-game) but is now its own submenu -- see that case's
 * own comment below for the whole Sound/Music Options feature (wootbeer's own ask, modeled on the God
 * of Thunder Android port's own Sound/Music submenu). Remap Gamepad is still a deliberately inert
 * placeholder (wootbeer's own call -- real behavior is
 * later work): selecting it just redraws this same menu rather than doing nothing visibly, so a
 * stray confirm press doesn't look like a dead button. Display Mode, Cheats, and Enhancements are
 * all real -- see each case below (and JILL.H's own jill_display_mode/jill_cheat_unlimited_gems/
 * jill_color_setting comments) for what each one actually does. Save/Restore/Help reuse the exact
 * same real calls play()'s own 'S'/'R'/key_f1 cases already make (savegame()/loadgame()/
 * dotextmsg()) -- see each case below for why.
 *
 * Return value: exactly 1 when QUIT TO MAIN MENU was chosen from the Quit submenu, 0 for every
 * other way this menu closes (including RESUME and the submenu's own CONTINUE GAME, which are the
 * same "close and keep playing" outcome) -- play()'s own key_pause case assigns this straight to
 * gameover, the identical convention askquit() already established for its own YES/NO return
 * value, so "quit to main menu" behaves exactly like the pre-existing Escape/Q path already did.
 * QUIT TO DOS doesn't return at all -- see that branch's own comment. */
int pausemenu(void)
{
    char message[400];
    int temppage;
    /* Android port addition, not upstream -- see domenu_at()'s own slider_mask/slider_dx comment,
     * and case 'N''s own SOUND/MUSIC comment, for the whole feature; this is this top-level menu's
     * own copy of the same pattern CHEATS/ENHANCEMENTS below now also use, for DISPLAY (case 'V')
     * and SPEED (case 'T') -- the only two rows here with a value of their own to adjust, out of
     * this menu's full eleven.
     *
     * `cursor` -- Android port addition, not upstream, this round. An EARLIER version of this same
     * function left choice_ptr NULL here (on the theory that this menu never persisted its own
     * highlighted row across a redraw to begin with, unlike CHEATS/ENHANCEMENTS, so there was
     * nothing new to preserve). wootbeer found the real problem with that: "on the main menu after
     * toggling any of the options, the cursor moves back to the top of the menu" -- exactly the
     * same "cursor resets after every toggle" report that already got CHEATS/ENHANCEMENTS their own
     * persistent `cursor` a round before this one, just never extended up to this top-level menu
     * too. Same fix, same shape: a real `cursor` local, seeded at row 0 (RESUME) once before this
     * function's own for(;;) loop starts, then threaded through choice_ptr on every domenu_at()
     * call below so it survives from one redraw to the next -- toggling DISPLAY or SPEED (or
     * anything else here) now leaves the highlight sitting right where the player left it, not
     * bounced back to the top every time. */
    int cursor = 0;
    int slider_dx;

    snd_play(1, 22);
    for (;;) {
        snprintf(message, sizeof(message),
                 "7PAUSED\r"
                 "2RESUME\r"
                 "2SOUND/MUSIC\r"
                 "2%s\r"
                 "2DISPLAY: %s\r"
                 "2SPEED: %s\r"
                 "2SAVE\r"
                 "2RESTORE\r"
                 "2HELP\r"
                 "2CHEATS\r"
                 "2ENHANCEMENTS\r"
                 "4QUIT\r",
                 /* Android port addition, not upstream -- wootbeer's own explicit ask for the new Touch
                  * Options feature: "if the gamepad is disconnected then the touch controls will
                  * show, and the 'remap gamepad' sub-menu listing will be replaced with 'touch
                  * options'." Same row, same key ('G' below), same slot -- only the label (and case
                  * 'G''s own dispatch) changes with jill_controls_gamepad_connected(), mirroring the
                  * God of Thunder Android port's own TOP_TOUCH_OPTIONS row exactly (that row's own
                  * comment: it shows "Touch Options" or "Remap Gamepad" text depending on
                  * got_controls_gamepad_connected(), always visible either way). */
                 jill_controls_gamepad_connected() ? "REMAP GAMEPAD" : "TOUCH OPTIONS",
                 /* Android port addition, not upstream -- names jill_display_mode's own current
                  * value in the pause menu, same idiom as SOUND/MUSIC's ON/OFF and SPEED's
                  * TURTLE/NORMAL just above/below. wootbeer's own follow-up ask shortened both the row
                  * label ("DISPLAY MODE" -> "DISPLAY" above) and this Minimal UI value's own label
                  * ("MINIMAL UI" -> "FULL", since Minimal UI's whole point is maximizing the
                  * onscreen game/map area by shrinking the HUD chrome down to a thin strip -- so
                  * "FULL" reads as "full-screen game view" to a player, not as a statement about
                  * how much HUD is showing) so the row fits pausemenu()'s own fixed 9-line-wide
                  * window (window_width=9, the domenu() call just below) without wrapping/
                  * truncating -- jill_display_mode's own enum values (JILL_DISPLAY_MINIMAL etc,
                  * JILL.H) are untouched; only this display string changed. */
                 jill_display_mode == JILL_DISPLAY_FORCE43 ? "FORCE 4:3" :
                 jill_display_mode == JILL_DISPLAY_MINIMAL ? "FULL" : "DEFAULT",
                 turtle ? "TURTLE" : "NORMAL");
        /* Android port fix, not upstream -- window_y=48, not domenu()'s own usual 64 (see that
         * function's own comment on its new 9th parameter for the whole story). This menu is
         * always exactly 12 lines (title + 11 choices, both fixed at compile time just above), so
         * its own window is always exactly 7*16+28 = 140px tall -- y=48 keeps its bottom edge at
         * 48+140 = 188, the same never-overlap-botvp convention every domenu() screen originally
         * got for free from the shared y=64 default back when 11 lines (124px tall) was the
         * tallest any of them got; y=64 would instead push this one's own bottom edge to 204,
         * wootbeer's own reported "text doesn't fit the screen". */
        /* Android port change, not upstream -- domenu_at() directly, not the domenu() wrapper, for
         * two reasons together now: passing DISPLAY (index 3) and SPEED (index 4)'s own
         * slider_mask bits and getting slider_dx back, and threading &cursor through so the
         * highlighted row survives across this function's own for(;;) redraws -- see this
         * function's own `cursor`/`slider_dx` comments above for both stories. */
        (void)domenu_at(message, "RNGVTSLHCEQ", 1, 11, 0, 24, 10, 9, 48, &cursor,
                         (1u << 3) | (1u << 4), &slider_dx);

        switch (key) {
        case 'R': /* RESUME */
        case key_back: /* B backing out of this top-level menu is the same as picking RESUME --
                         * "one layer back" from here is the game itself. See domenu()'s own
                         * comment on key_back for the whole B-backs-out convention; the Quit
                         * submenu below relies on the SAME convention but doesn't need an explicit
                         * case for it -- key_back there simply matches none of 'C'/'M'/'D' and
                         * falls out of that inner switch, which is already "redraw this (outer)
                         * menu" -- exactly back-one-layer-from-the-submenu, for free. */
            jill_drain_menu_exit_buttons(); /* see that function's own comment -- B (fire1/
                                              * menu_cancel) is still held right here. */
            return 0;
        case 'N': /* SOUND/MUSIC -- Android port addition, not upstream. wootbeer's own ask: "let's
                   * flesh out the cheat sub-menu... replace the current sound/music toggle with
                   * just a sub-menu... modeled after the god of thunder cheat menu... the same
                   * kind of toggle and sliders we have in god of thunder for sound fx and music.
                   * have the current sound levels be the default... the music does seem a little
                   * quiet so maybe there is some headroom for some gain on it." Same in-place-
                   * toggle-row/cursor-persistence shape as CHEATS/ENHANCEMENTS above (that case's
                   * own header comment on `cursor` has the whole "why a local, not a domenu_at()
                   * change" story). Real precedent for the GAIN rows' own Left/Right-adjusted-in-
                   * place shape: the God of Thunder Android port's own got_menu.c Sound/Music
                   * submenu ("Gain rows are Left/Right-adjusted in place... not Fire-confirmed
                   * like every other row"). Jill's own domenu_at() has no left/right-adjusts-the-
                   * highlighted-row concept the way GoT's own from-scratch menu engine does though
                   * (dx1/dy1 here only ever move the CURSOR between rows, never edit a value) --
                   * so each GAIN row is Fire-confirmed too, same as every other row here, and
                   * simply cycles to its own next 10% step on confirm and wraps back to 0 after
                   * its own max, the exact same one-key-cycles-a-multi-state-option idiom DISPLAY
                   * MODE above already established for an option with no natural two-state toggle
                   * shape of its own.
                   *
                   * SOUND EFFECTS keeps flipping the exact same soundf play()'s own separate,
                   * still-live 'N' hotkey already does (no change there). MUSIC is a genuinely new
                   * live toggle (MUSIC.H's own jill_music_enabled comment) -- stopping/restarting
                   * the actual currently-playing track immediately, not just gating the NEXT one,
                   * needs real calls: sb_shutup() (already gated only by musicflag, so it stops
                   * playback outright regardless of jill_music_enabled's own new value) turning
                   * Music off, and sb_playtune(oursong) turning it back on.
                   *
                   * Android bug fix, this round -- an EARLIER version of this same 'M' case called
                   * dolevelsong() (JMAN.c) instead, on the theory that repopulating newlevel[] from
                   * the current checkpoint and letting play()'s own do-loop notice newlevel[0] !=
                   * '\0' on its very next iteration would restart the track exactly like a fresh
                   * level entry does. wootbeer found the real bug that approach has: "music did not
                   * resume when toggling off then on, at least one time, worked the rest of the
                   * time." Root cause is play()'s own newlevel[] dispatch itself (just above in this
                   * file) -- a checkpoint's song command starting with '*' always calls
                   * sb_playtune(newlevel) unconditionally, but '#' and '&' only call it
                   * `if (!sb_playing())`, and MUSIC.c's own sb_playing() is a permanent stub
                   * (`return 1;` -- never actually tracks real playback state). So that branch is
                   * silently dead code: for any checkpoint whose song command starts with '#' or
                   * '&' (most of them), dolevelsong() alone never actually restarted anything --
                   * only '*'-prefixed checkpoints worked, which is exactly the "worked the rest of
                   * the time" wootbeer saw, depending on wherever he happened to be standing.
                   * sb_playtune() itself has no such gate (it unconditionally calls sb_shutup() then
                   * starts the new sequence), so calling it directly here -- using oursong, the
                   * prefix-stripped "whatever song is current" cache that same newlevel[] dispatch
                   * already maintains on every real level-song change (see its own three
                   * `strcpy(oursong, newlevel);` call sites just above in this file) -- restarts the
                   * correct track deterministically every time, the same way a '*' checkpoint always
                   * already did, without waiting on or depending on sb_playing()'s own broken
                   * signal. */
            /* Android port addition, not upstream -- wootbeer's own ask: "is there a way we could just
             * hide the pause menu while a sub-menu is pulled up? that would solve it from looking
             * weird." This top-level PAUSED menu's own box stays sitting on screen, undrawn-over,
             * once a submenu is opened on top of it (every submenu here is smaller than this menu's
             * own 11-row box, and this function's own domenu_at() call for THIS menu has already
             * returned -- and cleared its own transient mirror-exclude-rect, see that function's own
             * comment -- by the time a submenu's own domenu_at()/dotextmsg()/etc. call runs), so
             * whatever of it peeks out around/below the smaller submenu box stays visible the whole
             * time that submenu is open. Harmless-looking normally, but with Mirror Mode on it's
             * worse than cosmetic: that leftover box no longer has an active exclude rect protecting
             * it (only the submenu's own, freshly-pushed one does), so it gets caught by the ordinary
             * mirror pass and shows up fully backwards -- wootbeer's own screenshot, and his own
             * confirmation this is specifically the top-level menu showing through, not the submenu
             * itself. drawboard() (not moddrawboard()) -- same reasoning HELP's own matching call
             * below already established: this whole switch runs inside pausemenu()'s own modal
             * for(;;) loop, which never calls refresh() on its own, so moddrawboard()'s deferred
             * dirty-flag would just sit unconsumed until real gameplay resumes -- drawboard() repaints
             * the board immediately, erasing this menu's own box before the submenu below draws over
             * it, so there's nothing left showing through it at all, mirrored or not. Every other
             * submenu/dialog case below gets the identical one-line fix, right before it opens its
             * own window -- see this comment, not each of theirs individually. */
            drawboard();
            {
                int cursor = 0;
                /* Android port addition, not upstream -- see domenu_at()'s own slider_mask/
                 * slider_dx comment for the whole feature this drives; wootbeer's own follow-up ask
                 * ("can the left and right controls, d pad, and sticks, be used to move those
                 * sliders left and right instead of going up and down to other menu options?"),
                 * then his own very next ask, extending it further: "let's also make left/right
                 * change the other menu options too... anything with a toggle, so the behavior is
                 * consistent." SOUND/MUSIC's two gain rows (J/L, bits 0/2) were this feature's
                 * first two rows; SOUND EFFECTS/MUSIC (K/M, bits 1/3) are its own two plain boolean
                 * toggles, added for that same consistency -- every row in this whole submenu
                 * except BACK (B, bit 4, deliberately excluded) is in the mask now. */
                unsigned slider_mask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
                int slider_dx = 0;
                char sound_row[48];
                char music_row[48];
                for (;;) {
                    jill_format_gain_row(sound_row, sizeof(sound_row), "SOUND", jill_sound_gain,
                                          JILL_SOUND_GAIN_STEPS);
                    jill_format_gain_row(music_row, sizeof(music_row), "MUSIC", jill_music_gain,
                                          JILL_MUSIC_GAIN_MAX_STEP);
                    snprintf(message, sizeof(message),
                             "7SOUND/MUSIC\r"
                             "2%s\r"
                             "2SOUND EFFECTS: %s\r"
                             "2%s\r"
                             "2MUSIC: %s\r"
                             "4BACK\r",
                             sound_row, soundf ? "ON" : "OFF",
                             music_row, jill_music_enabled ? "ON" : "OFF");
                    (void)domenu_at(message, "JKLMB", 1, 5, 0, 20, 10, 13, 48, &cursor,
                                     slider_mask, &slider_dx);
                    if (key == 'J') {
                        /* Android port addition, not upstream -- slider_dx nonzero means LEFT/
                         * RIGHT was pressed while this row was highlighted (domenu_at()'s own
                         * comment) -- clamped at each end (0/max), not wrapped, the more natural
                         * feel for a direct left/right nudge vs. a Fire-confirm's own "cycle
                         * forward and wrap" below, which is UNCHANGED for a controller/keyboard
                         * with no separate analog left/right, or a player who just prefers Fire. */
                        if (slider_dx != 0) {
                            int new_gain = (int)jill_sound_gain + slider_dx;
                            if (new_gain < 0) new_gain = 0;
                            else if (new_gain > JILL_SOUND_GAIN_STEPS) new_gain = JILL_SOUND_GAIN_STEPS;
                            jill_sound_gain = (word)new_gain;
                        } else {
                            jill_sound_gain = (word)((jill_sound_gain + 1) % (JILL_SOUND_GAIN_STEPS + 1));
                        }
                    } else if (key == 'K') {
                        /* Android port addition, not upstream -- LEFT/RIGHT forces OFF/ON, same
                         * idiom as every other boolean toggle in this submenu/CHEATS/ENHANCEMENTS
                         * (this function's own slider_mask comment above). */
                        soundf = (slider_dx != 0) ? (slider_dx > 0) : !soundf;
                    } else if (key == 'L') {
                        /* Same LEFT/RIGHT-clamps-vs-Fire-cycles-and-wraps split as SOUND (J) just
                         * above. */
                        if (slider_dx != 0) {
                            int new_gain = (int)jill_music_gain + slider_dx;
                            if (new_gain < 0) new_gain = 0;
                            else if (new_gain > JILL_MUSIC_GAIN_MAX_STEP) new_gain = JILL_MUSIC_GAIN_MAX_STEP;
                            jill_music_gain = (word)new_gain;
                        } else {
                            jill_music_gain =
                                (word)((jill_music_gain + 1) % (JILL_MUSIC_GAIN_MAX_STEP + 1));
                        }
                    } else if (key == 'M') {
                        /* Android port addition, not upstream -- same LEFT/RIGHT-forces-OFF/ON
                         * idiom as SOUND EFFECTS (K) just above; either way, the real
                         * sb_shutup()/sb_playtune() side effect below still needs to run exactly
                         * when the value actually changes to OFF/ON respectively, same as before. */
                        jill_music_enabled = (slider_dx != 0) ? (slider_dx > 0) : !jill_music_enabled;
                        if (jill_music_enabled) {
                            if (oursong[0] != '\0') sb_playtune(oursong);
                        } else {
                            sb_shutup();
                        }
                    } else {
                        break;
                    }
                }
            }
            /* Android port addition, not upstream -- wootbeer's own follow-up report, after the "hide
             * the top-level PAUSED menu before a submenu opens" fix above (case 'N''s own entry-side
             * comment further up has that whole story): "with the remap gamepad sub-menu, it sticks
             * out longer than the pause menu, so when you return to the pause menu you can see the
             * rest of that menu sticking out to the right." Same root cause, opposite direction --
             * this submenu's own window is wider (window_width=13) than the top-level PAUSED menu's
             * own box (window_width=9), so on the way OUT, closing it leaves that extra ~64px-wide
             * strip on the right still showing this submenu's own last-drawn content, since nothing
             * repaints it before the outer loop's next domenu_at() call redraws the (narrower)
             * top-level menu back over just the middle of it. drawboard() here, symmetric with the
             * entry-side one above, erases this submenu's own full footprint (including that
             * overhanging strip) before the top-level menu redraws, the same fix REMAP GAMEPAD/TOUCH
             * OPTIONS below need for the exact same reason (also window_width=13). */
            drawboard();
            jill_save_app_config();
            break;
        case 'T': /* SPEED -- same "turtle" toggle as play()'s own 'T' hotkey. Android port change,
                   * not upstream -- slider_dx nonzero (LEFT/RIGHT was pressed on this row, this
                   * function's own comment) forces OFF/ON directionally, same idiom as CHEATS'/
                   * ENHANCEMENTS' own boolean rows; slider_dx==0 (a real Fire/Start confirm) keeps
                   * the exact original flip-in-place behavior. */
            turtle = (slider_dx != 0) ? (slider_dx > 0) : !turtle;
            statmodflg |= mod_screen;
            break;
        case 'S': /* SAVE -- identical to play()'s own 'S' case. */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story, including why this has to be drawboard(), not
             * moddrawboard()). savegame() below draws loadsavewin()'s own picker window, smaller
             * than this menu's own box. */
            drawboard();
            temppage = pagedraw;
            pagedraw = pageshow;
            setpages();
            savegame();
            drawcmds();
            pagedraw = (word)temppage;
            setpages();
            break;
        case 'L': /* RESTORE -- identical to play()'s own 'R' case (renamed here since 'R' is
                   * already RESUME above); loadgame() replaces the whole in-progress session, so
                   * this closes the pause menu afterward rather than looping back into more of
                   * it, same as play()'s own case falls out of its key-handling switch once it's
                   * done. */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story). loadgame() below draws loadsavewin()'s own picker
             * window, smaller than this menu's own box. */
            drawboard();
            temppage = pagedraw;
            pagedraw = pageshow;
            setpages();
            (void)loadgame();
            dolevelsong();
            drawcmds();
            pagedraw = (word)temppage;
            setpages();
            setorigin();
            moddrawboard();
            jill_drain_menu_exit_buttons(); /* see that function's own comment -- whatever button
                                              * confirmed the slot pick may still be held here. */
            return 0;
        case 'H': /* HELP -- identical to play()'s own key_f1 case, EXCEPT for the drawboard() call
                   * just below (see its own comment for why that one extra line has to be here
                   * and doesn't need to be in play()'s copy). */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story) -- dotextmsg() below draws its own, differently-sized
             * window; this is the entry-side half of the fix, the drawboard() call just below this
             * one is the pre-existing exit-side half (leftover Help text once this closes), kept as
             * its own separate call since it fixes a different moment. */
            drawboard();
            dotextmsg(1);
            /* Android port addition, not upstream -- wootbeer's own bug report: "if you push 'b' when
             * help is up, the menu text draws 'on top' of the help screen." Root cause: dotextmsg()
             * draws its own text window sized off *gamevp -- much bigger than this whole pause menu's
             * own small popup box (the domenu_at() call at this function's own top, window_width=9)
             * -- directly over the frozen game screen play() already froze in place (setpagemode(0))
             * before ever calling pausemenu(). Backing out of Help with B returns here, and this
             * function's own for(;;) loop immediately calls domenu_at() again, which DOES fully
             * redraw its own (small) window from scratch every time -- but nothing repaints the
             * larger area outside that small window that Help's own bigger window had just drawn
             * over, so whatever Help text didn't get covered by the pause menu's own narrower box
             * stayed on screen behind/around it, looking exactly like "the menu draws on top of the
             * help screen" instead of replacing it. play()'s own key_f1 case (the "identical" case
             * this comment references) never needed this fix: its own main game loop calls
             * refresh(pagemode) unconditionally every single frame regardless of what key was
             * pressed, so any leftover Help pixels there get silently overwritten on the very next
             * frame -- this function has no such per-frame safety net, it only ever repaints when a
             * case here explicitly asks it to. drawboard() (not moddrawboard()) specifically --
             * moddrawboard() only sets statmodflg's own dirty flag for some FUTURE refresh() call to
             * consume, which nothing in this modal loop ever makes; drawboard() calls refresh(0)
             * itself immediately, so the full game board is visibly repainted right now, before this
             * loop's own next domenu_at() call redraws the (smaller) pause menu box back on top of
             * it, exactly restoring the same frozen-backdrop-plus-small-popup look every other row
             * here already has. */
            drawboard();
            break;
        case 'V': /* DISPLAY MODE -- Android port addition, not upstream. Cycles Default -> Force
                   * 4:3 -> Minimal UI -> Default ..., same one-key-cycles-a-multi-state-option
                   * idiom SOUND/MUSIC and SPEED above use for their own two-state toggles, just
                   * with a third state. See jill_apply_display_mode()'s own comment for what each
                   * mode actually does; statmodflg isn't needed here the way the SOUND/MUSIC/SPEED
                   * cases set it below -- jill_apply_display_mode() already forces its own full
                   * redraw before returning.
                   *
                   * Android port change, not upstream -- same slider_dx-nonzero-means-LEFT/RIGHT
                   * idiom as SPEED (case 'T') just above, but wrapping (not forcing a direction)
                   * since this is a 3-state cycle, not a boolean -- LEFT steps backward, RIGHT
                   * steps forward, both wrapping at either end; a real Fire/Start confirm
                   * (slider_dx==0) keeps stepping forward-and-wrap only, its exact original
                   * behavior. */
            jill_apply_display_mode(
                ((jill_display_mode + (slider_dx != 0 ? slider_dx : 1)) % 3 + 3) % 3);
            break;
        case 'G': /* REMAP GAMEPAD / TOUCH OPTIONS -- Android port addition, not upstream. Shares
                   * one row/key with two different submenus now, exactly the God of Thunder Android
                   * port's own TOP_TOUCH_OPTIONS precedent (that row's own comment: it shows "Touch
                   * Options" or "Remap Gamepad" depending on got_controls_gamepad_connected(), always
                   * visible either way) -- wootbeer's own explicit ask for the new touch-controls
                   * feature: "if the gamepad is disconnected then the touch controls will show, and
                   * the 'remap gamepad' sub-menu listing will be replaced with 'touch options'."
                   * Real REMAP GAMEPAD behavior is unchanged below (just now gated behind
                   * jill_controls_gamepad_connected(), same reasoning GoT's own row switch already
                   * establishes: remapping a gamepad makes no sense with none connected) -- see its
                   * own original header comment, folded into the `if` branch below unchanged. The new
                   * `else` branch is wootbeer's own Touch Options ask: "the new sub-menu will have the
                   * same opacity and scaling sliders as god of thunder... of course if we need to
                   * save settings we can use our config too" -- see jill_controls.h's own comment for
                   * the native-side touch-button module these two sliders drive, and this file's own
                   * JillAppConfig struct comment for where touch_scale_step/touch_opacity_step
                   * persist. Left/Right-ONLY on both rows (no Fire-confirm-cycles-forward behavior),
                   * deliberately NOT this menu's own Sound/Music gain-row convention (which supports
                   * both) -- matching the God of Thunder Android port's own Touch Options submenu
                   * precedent exactly (that submenu's own dispatch_confirm() comment: "no
                   * MENU_TOUCH_OPTIONS branch at all -- there's simply nothing for a Fire press to do
                   * while this submenu is open"), since wootbeer's own request led with "the same...
                   * sliders as god of thunder" rather than this project's own gain-row idiom. */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story) -- covers both branches below (REMAP GAMEPAD and TOUCH
             * OPTIONS each open their own smaller window) with the one call, since neither can run
             * without the top-level menu having been on screen first. */
            drawboard();
            if (jill_controls_gamepad_connected()) {
                /* REMAP GAMEPAD -- wootbeer's own explicit ask: "let's again model after the one we made
                 * for god of thunder. have all the same gamepad buttons available to be mapped that
                 * god of thunder has, and have them able to be mapped to all of the controls jill of
                 * the jungle uses. of course the default controls will be what we have now, and we
                 * will have the same apply, default, cancel options, etc as god of thunder. and of
                 * course save it in our config.dat file." See JILL.H's own
                 * JILL_GAMEPAD_ACTION_JUMP/_THROW comment for the whole feature --
                 * jill_jni_bridge.c's own "Gamepad remapping" section has the native-side live
                 * table/capture state machine (mirroring the God of Thunder Android port's own
                 * got_controls.c exactly, just scoped to Jill's 2 real remappable actions instead of
                 * GoT's 3), and jill_gamepad_capture_button() just above has the "press a button for
                 * this action" capture step's own UI.
                 *
                 * `staging`/`cursor` -- same "Apply/Cancel/Default, edits only ever touch a staged
                 * copy until Apply" shape as the God of Thunder Android port's own Remap Gamepad
                 * screen (that port's own s_gp_staging[] comment: "wootbeer's own explicit 'apply,
                 * cancel, default' request means this screen doesn't take effect immediately the
                 * way every OTHER submenu... does"), and the same persisted-cursor-across-redraws
                 * shape CHEATS/ENHANCEMENTS/SOUND-MUSIC above already use. Seeded from the live
                 * table on entry; RESET TO DEFAULT only touches `staging`, never the live table,
                 * same as GoT's own Reset row; only APPLY actually commits (via
                 * jill_gamepad_remap_set_bound_keycode() per action) and saves config.dat; CANCEL
                 * (or key_back, B backing out one layer -- the exact same convention every other
                 * submenu here already relies on) discards `staging` outright. */
                int cursor = 0;
                int staging[JILL_GAMEPAD_NUM_ACTIONS];
                int action_index;
                char jump_row[32];
                char throw_row[32];
#if defined(JILL_EP1)
                /* Android port addition, not upstream -- Episode 1's own COIN TOSS action, JILL.H's
                 * own JILL_GAMEPAD_ACTION_COIN_TOSS comment has the whole story (wootbeer's own
                 * follow-up ask, after the touch controls: "I'm assuming this action already works
                 * with gamepad controls though?" -- it didn't, this closes that gap). #if-gated the
                 * same way JPLAYER.c's own real coin-toss trigger already is -- Episodes 2/3 have no
                 * such mechanic to remap. */
                char coin_row[32];
#endif

                for (action_index = 0; action_index < JILL_GAMEPAD_NUM_ACTIONS; ++action_index) {
                    staging[action_index] = jill_gamepad_remap_bound_keycode(action_index);
                }
                for (;;) {
                    snprintf(jump_row, sizeof(jump_row), "JUMP: %s",
                             jill_gamepad_remap_button_name(staging[JILL_GAMEPAD_ACTION_JUMP]));
                    snprintf(throw_row, sizeof(throw_row), "THROW: %s",
                             jill_gamepad_remap_button_name(staging[JILL_GAMEPAD_ACTION_THROW]));
#if defined(JILL_EP1)
                    snprintf(coin_row, sizeof(coin_row), "COIN TOSS: %s",
                             jill_gamepad_remap_button_name(staging[JILL_GAMEPAD_ACTION_COIN_TOSS]));
                    snprintf(message, sizeof(message),
                             "7REMAP GAMEPAD\r"
                             "2%s\r"
                             "2%s\r"
                             "2%s\r"
                             "2RESET TO DEFAULT\r"
                             "2APPLY\r"
                             "4CANCEL\r",
                             jump_row, throw_row, coin_row);
                    (void)domenu_at(message, "JTXRAB", 1, 6, 0, 20, 10, 13, 48, &cursor, 0, NULL);
#else
                    snprintf(message, sizeof(message),
                             "7REMAP GAMEPAD\r"
                             "2%s\r"
                             "2%s\r"
                             "2RESET TO DEFAULT\r"
                             "2APPLY\r"
                             "4CANCEL\r",
                             jump_row, throw_row);
                    (void)domenu_at(message, "JTRAB", 1, 5, 0, 20, 10, 13, 48, &cursor, 0, NULL);
#endif
                    if (key == 'J' || key == 'T'
#if defined(JILL_EP1)
                        || key == 'X'
#endif
                       ) {
                        /* Enter the capture step for whichever action row was just confirmed --
                         * see jill_gamepad_capture_button()'s own comment for why this isn't
                         * another domenu_at() call. On a real capture (not a Start/ESC cancel),
                         * "steal" the just-captured button away from whichever OTHER action might
                         * already be staged to it, same as the God of Thunder Android port's own
                         * capture-resolution comment ("clearing any OTHER action currently bound to
                         * the same captured keycode") -- otherwise two actions could end up staged
                         * to the same physical button, which jill_gamepad_dispatch_raw()
                         * (jill_jni_bridge.c) would then fire for BOTH actions on every press. */
                        int captured;
                        int other_index;
                        if (key == 'J') {
                            action_index = JILL_GAMEPAD_ACTION_JUMP;
                        } else if (key == 'T') {
                            action_index = JILL_GAMEPAD_ACTION_THROW;
                        }
#if defined(JILL_EP1)
                        else {
                            action_index = JILL_GAMEPAD_ACTION_COIN_TOSS;
                        }
#endif
                        /* Android port addition, not upstream -- wootbeer's own report + screenshot:
                         * opening the REBIND capture screen from within REMAP GAMEPAD's own row list
                         * left the list's own lower rows (RESET TO DEFAULT/APPLY/CANCEL) still
                         * showing underneath it, mirrored backwards with Mirror Mode on. Same "hide
                         * whatever's behind before a smaller/differently-sized window opens over it"
                         * fix as pausemenu()'s own case 'N'/'G' comments (that's the whole story) --
                         * just one level deeper here: jill_gamepad_capture_button()'s own capwin is
                         * SHORTER than this list's own window (3 content lines vs. up to 6), so it
                         * doesn't fully cover the list's box the way the list's later fresh redraw
                         * (this loop's own next domenu_at() call, above) covers the capture screen's
                         * smaller box back on the way out -- this is the one-directional gap, entry
                         * only. */
                        drawboard();
                        captured = jill_gamepad_capture_button(action_index);
                        if (captured != JILL_GAMEPAD_UNBOUND_KEYCODE) {
                            staging[action_index] = captured;
                            for (other_index = 0; other_index < JILL_GAMEPAD_NUM_ACTIONS;
                                 ++other_index) {
                                if (other_index != action_index &&
                                    staging[other_index] == captured) {
                                    staging[other_index] = JILL_GAMEPAD_UNBOUND_KEYCODE;
                                }
                            }
                        }
                    } else if (key == 'R') { /* RESET TO DEFAULT -- staging only, see this case's
                                               * own header comment. */
                        for (action_index = 0; action_index < JILL_GAMEPAD_NUM_ACTIONS;
                             ++action_index) {
                            staging[action_index] = jill_gamepad_remap_default_keycode(action_index);
                        }
                    } else if (key == 'A') { /* APPLY -- commits staging to the live table and
                                               * persists it, then leaves this submenu. */
                        /* Android port addition, not upstream -- temporary diagnostic for wootbeer's
                         * own bug report: rebinding JUMP/THROW away from their defaults, then
                         * finding A/B no longer behave as expected in menus afterward.
                         * domenu_at()'s own new fire1/fire2/menu_confirm/menu_cancel log (that
                         * function's own comment) showed the LIVE binding table still matching
                         * the compiled-in defaults (B=Jump, A=Throw) during that test, not
                         * whatever was actually staged here -- this line confirms (or rules out)
                         * whether APPLY itself is even being reached with the staged values wootbeer
                         * expects, since this whole row is only reached by navigating the cursor
                         * here and confirming, not by literally pressing a physical 'A' (key ==
                         * 'A' compares against key_table[choice], the APPLY row's own label
                         * letter, completely unrelated to which physical button was pressed). */
                        JILL_LOGI("REMAP GAMEPAD: APPLY -- jump_keycode=%d throw_keycode=%d",
                                  staging[JILL_GAMEPAD_ACTION_JUMP],
                                  staging[JILL_GAMEPAD_ACTION_THROW]);
                        for (action_index = 0; action_index < JILL_GAMEPAD_NUM_ACTIONS;
                             ++action_index) {
                            jill_gamepad_remap_set_bound_keycode(action_index,
                                                                  staging[action_index]);
                        }
                        jill_save_app_config();
                        break;
                    } else {
                        /* Android port addition, not upstream -- see the APPLY branch's own new
                         * diagnostic log comment just above for the whole story. Logs whatever
                         * `key` actually was so a CANCEL (key_back, from B/fire1/menu_cancel) can
                         * be told apart from anything else that might land here. */
                        JILL_LOGI("REMAP GAMEPAD: CANCEL (key=%d) -- staging discarded, jump/"
                                  "throw unchanged from jump_keycode=%d throw_keycode=%d", key,
                                  jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_JUMP),
                                  jill_gamepad_remap_bound_keycode(JILL_GAMEPAD_ACTION_THROW));
                        break; /* CANCEL, or key_back -- either way, staging discarded, out to the
                                * outer PAUSED menu. */
                    }
                }
            } else {
                /* TOUCH OPTIONS -- wootbeer's own ask, this case's own header comment above has the
                 * whole story. Both rows apply and save immediately on every real change (no
                 * Apply/Cancel staging the way REMAP GAMEPAD above needs it -- a slider nudge has no
                 * "wrong button" failure mode worth guarding against the way a capture mis-bind
                 * does), same "changes take effect immediately" shape every other submenu in this
                 * file except REMAP GAMEPAD already has. */
                int cursor = 0;
                unsigned slider_mask = (1u << 0) | (1u << 1); /* SCALING, OPACITY -- BACK (bit 2)
                                                                 * deliberately excluded, same
                                                                 * "BACK is never a slider" precedent
                                                                 * every other submenu's own mask
                                                                 * already establishes. */
                int slider_dx = 0;
                char scale_row[48];
                char opacity_row[48];
                for (;;) {
                    char scale_bar[TOUCH_SCALE_NUM_STEPS + 1];
                    char opacity_bar[JILL_TOUCH_OPACITY_NUM_STEPS + 1];
                    int i;

                    /* Bracket-bar shape mirrors jill_format_gain_row() above, but a single 'o'
                     * marking the CURRENT step, not a filled run of '=' -- same convention the God
                     * of Thunder Android port's own format_scale_label() uses for its own Scaling
                     * row, since a multiplier (not a percentage-of-max) is what's actually being
                     * shown here. */
                    for (i = 0; i < TOUCH_SCALE_NUM_STEPS; ++i) {
                        scale_bar[i] = (i == s_jill_touch_scale_step) ? 'o' : '-';
                    }
                    scale_bar[TOUCH_SCALE_NUM_STEPS] = '\0';
                    snprintf(scale_row, sizeof(scale_row), "SCALING: [%s] %.2fx", scale_bar,
                              (double) jill_controls_touch_scale_value(s_jill_touch_scale_step));

                    /* Same filled-run bracket-bar shape jill_format_gain_row() already uses, but the
                     * displayed percentage reflects the ACTUAL alpha jill_draw_touch_buttons() will
                     * render (jill_touch_opacity_alpha()'s own floored formula), not a raw 0-10 step
                     * fraction -- same reasoning the God of Thunder Android port's own
                     * format_opacity_label() comment gives for why step 0 can't just print "0%" when
                     * the floor is actually a real, visible 30% opacity. */
                    for (i = 0; i < JILL_TOUCH_OPACITY_NUM_STEPS; ++i) {
                        opacity_bar[i] = (i < s_jill_touch_opacity_step) ? '=' : '-';
                    }
                    opacity_bar[JILL_TOUCH_OPACITY_NUM_STEPS] = '\0';
                    snprintf(opacity_row, sizeof(opacity_row), "OPACITY: [%s] %d%%", opacity_bar,
                              (int) (jill_touch_opacity_alpha(s_jill_touch_opacity_step) * 100.0f +
                                     0.5f));

                    snprintf(message, sizeof(message),
                             "7TOUCH OPTIONS\r"
                             "2%s\r"
                             "2%s\r"
                             "4BACK\r",
                             scale_row, opacity_row);
                    (void)domenu_at(message, "SOB", 1, 3, 0, 20, 10, 13, 48, &cursor, slider_mask,
                                     &slider_dx);
                    if (key == 'S') {
                        /* SCALING -- Left/Right-only, see this case's own header comment; a real
                         * Fire/Start confirm (slider_dx==0) is a deliberate no-op, matching GoT's
                         * own dispatch_confirm() precedent. */
                        if (slider_dx != 0) {
                            int new_step = s_jill_touch_scale_step + slider_dx;
                            if (new_step < 0) new_step = 0;
                            else if (new_step >= TOUCH_SCALE_NUM_STEPS)
                                new_step = TOUCH_SCALE_NUM_STEPS - 1;
                            if (new_step != s_jill_touch_scale_step) {
                                s_jill_touch_scale_step = new_step;
                                jill_controls_set_scale_step(s_jill_touch_scale_step);
                                jill_save_app_config();
                            }
                        }
                    } else if (key == 'O') { /* OPACITY -- same Left/Right-only shape as SCALING. */
                        if (slider_dx != 0) {
                            int new_step = s_jill_touch_opacity_step + slider_dx;
                            if (new_step < 0) new_step = 0;
                            else if (new_step > JILL_TOUCH_OPACITY_NUM_STEPS)
                                new_step = JILL_TOUCH_OPACITY_NUM_STEPS;
                            if (new_step != s_jill_touch_opacity_step) {
                                s_jill_touch_opacity_step = new_step;
                                jill_controls_set_opacity(
                                    jill_touch_opacity_alpha(s_jill_touch_opacity_step));
                                jill_save_app_config();
                            }
                        }
                    } else {
                        break; /* BACK, or key_back -- out to the outer PAUSED menu. */
                    }
                }
            }
            /* Android port addition, not upstream -- wootbeer's own report: "with the remap gamepad
             * sub-menu, it sticks out longer than the pause menu, so when you return to the pause
             * menu you can see the rest of that menu sticking out to the right." Both branches above
             * (REMAP GAMEPAD and TOUCH OPTIONS, plus REMAP GAMEPAD's own nested capture screen) use
             * window_width=13, wider than the top-level PAUSED menu's own window_width=9 -- closing
             * either one leaves that extra ~64px-wide strip on the right still showing whichever of
             * them was open, since nothing repaints it before the outer loop's next domenu_at() call
             * redraws the (narrower) top-level menu back over just the middle of it. One drawboard()
             * here, after the if/else above, covers exiting either branch -- same symmetric
             * entry-side/exit-side fix as case 'N''s own matching comment (SOUND/MUSIC, the other
             * window_width=13 submenu). */
            drawboard();
            break;
        case 'C': /* CHEATS -- Android port addition, not upstream. wootbeer's own ask, mirroring the
                   * God of Thunder Android port's own Cheat Codes submenu exactly: "let's flesh out
                   * the cheat sub-menu now, also modeled after the god of thunder cheat menu... I
                   * want the cheats to function the same way as in god of thunder, off by default,
                   * new game, reloads etc." See JILL.H's own jill_cheat_unlimited_gems comment for
                   * the whole feature and each row's own real gate. Same in-place-toggle-row shape
                   * as ENHANCEMENTS just below (its own header comment has the window-geometry
                   * math this reuses verbatim -- four toggle rows here instead of five/BACK is
                   * strictly shorter, so the identical window bounds are even more comfortably
                   * inside budget) -- every row toggles and stays in the submenu on confirm, not a
                   * one-shot submenu the way QUIT's own C/M/D choices are. ALL ITEMS is the one row
                   * that isn't a plain flag flip -- see jill_cheat_set_all_items()'s own comment
                   * (JMAN.c) for why it goes through a function instead.
                   *
                   * `cursor` -- Android port addition, not upstream. wootbeer's own report: "when
                   * toggling the cheats on and off in the menu the cursor's position resets after
                   * each item. I'd like it to stay on that item." Lives here, not inside domenu()
                   * itself, precisely because this submenu redraws with a brand new domenu_at()
                   * call after every single toggle (see that function's own comment) -- passing its
                   * address through lets this one persist across every one of those calls for as
                   * long as this particular CHEATS visit lasts, while still starting fresh at row 0
                   * the next time CHEATS is opened (a new `cursor` local, zero-initialized, every
                   * time this case is entered). */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story). */
            drawboard();
            {
                int cursor = 0;
                /* Android port addition, not upstream -- wootbeer's own follow-up ask, this round:
                 * "let's also make left/right change the other menu options too, like the display
                 * mode, etc, anything with a toggle, so the behavior is consistent" (with the
                 * Sound/Music submenu's own new slider left/right support, this same session). All
                 * four real toggle rows here (GEMS/KEYS/GOD MODE/ALL ITEMS, choices 0-3) are
                 * boolean, unlike Sound/Music's own linear gain rows, so LEFT/RIGHT here means
                 * "force OFF"/"force ON" (see each row's own ?: below) rather than a clamped nudge
                 * -- the same real-world light-switch direction convention most game settings
                 * screens already use for a plain ON/OFF row, and idempotent besides (pressing
                 * LEFT again on an already-OFF row is a harmless no-op, unlike a flip-on-either-
                 * arrow scheme, which would incorrectly turn it back ON). BACK (choice 4) is left
                 * out of the mask -- it's not a toggle, so LEFT/RIGHT still just moves the cursor
                 * off of it like any other non-slider row. */
                int slider_dx;
                for (;;) {
                    snprintf(message, sizeof(message),
                             "7CHEATS\r"
                             "2GEMS: %s\r"
                             "2KEYS: %s\r"
                             "2GOD MODE: %s\r"
                             "2ALL ITEMS: %s\r"
                             "4BACK\r",
                             jill_cheat_unlimited_gems ? "ON" : "OFF",
                             jill_cheat_unlimited_keys ? "ON" : "OFF",
                             jill_cheat_god_mode ? "ON" : "OFF",
                             jill_cheat_all_items ? "ON" : "OFF");
                    (void)domenu_at(message, "JKLMB", 1, 5, 0, 20, 10, 9, 48, &cursor,
                                     (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3), &slider_dx);
                    if (key == 'J') {
                        jill_cheat_unlimited_gems =
                            (slider_dx != 0) ? (slider_dx > 0) : !jill_cheat_unlimited_gems;
                        statmodflg |= mod_screen;
                    } else if (key == 'K') {
                        jill_cheat_unlimited_keys =
                            (slider_dx != 0) ? (slider_dx > 0) : !jill_cheat_unlimited_keys;
                        statmodflg |= mod_screen;
                    } else if (key == 'L') {
                        jill_cheat_god_mode =
                            (slider_dx != 0) ? (slider_dx > 0) : !jill_cheat_god_mode;
                        statmodflg |= mod_screen;
                    } else if (key == 'M') {
                        jill_cheat_set_all_items(
                            (slider_dx != 0) ? (slider_dx > 0) : !jill_cheat_all_items);
                    } else {
                        break; /* BACK, or key_back -- either way, out to the outer PAUSED menu. */
                    }
                }
            }
            /* Android port addition, not upstream -- wootbeer's own report, confirming this menu has the
             * exact same gap ENHANCEMENTS' own matching comment just below describes (that comment
             * has the whole mechanism): "the cheats sub-menu does stick out yes." Same shape --
             * window_width=9, same as the top-level PAUSED menu, so no width-mismatch overhang the
             * way case 'G''s own SOUND/MUSIC/REMAP GAMEPAD fix addresses; this menu's own last-drawn
             * pixels are just still sitting in *gamevp on the way out, so if Mirror Mode is on when
             * the outer PAUSED menu's own next domenu_at() call captures its "clean" background for
             * its own shadow fill, it captures this menu's stale leftovers instead of real board and
             * pre-flips a mirrored copy of them into the shared reflection area. One drawboard() here
             * repaints the real board first, so there's nothing stale left to capture -- identical
             * fix to ENHANCEMENTS' own, below. */
            drawboard();
            break;
        case 'E': /* ENHANCEMENTS -- Android port addition, not upstream. wootbeer's own ask, mirroring
                   * the God of Thunder Android port's own Hammer/Armor Color Enhancements option:
                   * "add an enhancement like we did in god of thunder for his armor/hammer color,
                   * except now it will be jill's color, we can choose her color from episode 1 2 or
                   * 3, or the default for what that episode is supposed to be." See
                   * jill_color_setting's own comment (JILL.H) for the whole cross-episode
                   * sprite-swap mechanism this drives. Its own small for(;;) loop, unlike REMAP
                   * GAMEPAD just above (still an inert placeholder) -- this one is real, so
                   * every row needs to redraw with its own new value immediately after every press,
                   * the same one-key-cycles-a-multi-state-option idiom DISPLAY MODE (case 'V'
                   * above) uses -- not a single one-shot submenu the way QUIT's own C/M/D choices
                   * are below (those each leave the submenu outright; cycling a setting shouldn't).
                   * Same window geometry as the QUIT submenu just below (indent 20, window_x 10,
                   * window_width 9, window_y 48) -- both are short submenus launched from this same
                   * PAUSED menu, so keeping them the same size/position is deliberate, not a
                   * coincidence; even with all six rows below (title + 5 toggles + BACK = 7 lines),
                   * this still comes out to yl16 = 7/2+1 = 4, a 4*16+28 = 92px-tall window, bottom
                   * edge 48+92 = 140 -- well inside the same never-overlap-botvp 188px budget every
                   * domenu() screen shares (see domenu()'s own 9th-parameter comment).
                   *
                   * wootbeer's own follow-up round: "let's add some of the other types of enhancements
                   * to the menu we also have in god of thunder. let's do one for 'enemy speed'...
                   * then do one for jill's attack speed... then have one for jill's movement
                   * speed... then a big one, would it be possible to add a mirror mode option as
                   * well". ENEMY/ATK/MOV/MIRROR below are those four -- see jill_enemyspeed_setting/
                   * jill_attackspeed_setting/jill_movespeed_setting/jill_mirror_enabled's own
                   * comments (JILL.H) for what each actually drives. Labels shortened the same way
                   * COLOR's own row already had to be (wootbeer's own report: "text doesn't all fit in
                   * the box") -- "ENEMY SPEED"/"ATTACK SPEED"/"MOV SPEED" (wootbeer's own words) would
                   * all overflow this same 9-wide window paired with a value, so these use the
                   * shortest unambiguous prefix instead, matching COLOR's own precedent exactly.
                   *
                   * `cursor` -- Android port addition, not upstream. Same cursor-persistence fix as
                   * CHEATS' own -- see that case's own comment on `cursor` for the whole story;
                   * identical pattern here, just one more row/hotkey. */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story) -- this is the exact submenu wootbeer's own screenshot showed
             * the top-level menu flipping backwards underneath. */
            drawboard();
            {
                int cursor = 0;
                /* Android port addition, not upstream -- same "left/right, not just Fire, on every
                 * toggle/cycle row" ask as CHEATS' own comment just above -- see that case's own
                 * comment for the whole story. COLOR/ENEMY/ATK/MOV (choices 0-3) are multi-state
                 * cycles, not plain booleans, so they keep the SAME "cycle one step and wrap"
                 * behavior Fire already has, just now driven by slider_dx's own sign instead of
                 * always +1 -- consistent with how Sound/Music's own two GAIN rows already treat a
                 * multi-step option (wrap on Fire, same step size on LEFT/RIGHT), just wrapping
                 * here instead of clamping, matching what Fire already did for these specific rows
                 * before this round (unlike Sound/Music's own gain sliders, a true linear range
                 * this project deliberately chose to clamp instead -- see that case's own comment).
                 * MIRROR (choice 4) is boolean, so it uses the same LEFT-forces-OFF/RIGHT-forces-ON
                 * idiom CHEATS' own four rows use. BACK (choice 5) is excluded from the mask, same
                 * reasoning as CHEATS' own BACK. */
                int slider_dx;
                for (;;) {
                    snprintf(message, sizeof(message),
                             "7ENHANCEMENTS\r"
                             "2COLOR: %s\r"
                             "2ENEMY: %s\r"
                             "2ATK: %s\r"
                             "2MOV: %s\r"
                             "2MIRROR: %s\r"
                             "2DEBUG: %s\r"
                             "4BACK\r",
                             jill_color_setting == 1 ? "GREEN" :
                             jill_color_setting == 2 ? "RED" :
                             jill_color_setting == 3 ? "BLUE" : "DEFAULT",
                             jill_enemyspeed_setting == JILL_SPEED_SLOW ? "SLOW" :
                             jill_enemyspeed_setting == JILL_SPEED_FAST ? "FAST" : "DEFAULT",
                             jill_attackspeed_setting == JILL_SPEED_SLOW ? "SLOW" :
                             jill_attackspeed_setting == JILL_SPEED_FAST ? "FAST" : "DEFAULT",
                             jill_movespeed_setting == JILL_SPEED_SLOW ? "SLOW" :
                             jill_movespeed_setting == JILL_SPEED_FAST ? "FAST" : "DEFAULT",
                             jill_mirror_enabled == JILL_MIRROR_ON ? "ON" : "OFF",
                             debug ? "ON" : "OFF");
                    /* DEBUG -- Android port addition, not upstream. wootbeer's own ask, after we
                     * turned up upstream's own hidden "type Z three times during gameplay" cheat
                     * (play()'s own cheatchar=='Z' branch, just above in this file) while combing
                     * through the code for this project's pre-release cleanup: "ZZZ sounds cool
                     * though, let's add a toggle for it to the enhancement menu, call it 'debug'."
                     * Toggles the exact same upstream `debug` global that triple-tapping Z already
                     * did (drawstats()'s own `if (debug && !swrite)` block is the one real effect
                     * -- a live free-memory readout, host_coreleft(), next to the LEVEL number) --
                     * this row is just a second, discoverable way to flip the same switch, not a
                     * new mechanism. Deliberately NOT run through jill_save_app_config() the way
                     * COLOR/ENEMY/ATK/MOV/MIRROR above all are: `debug` was never a persisted
                     * player preference upstream either (ZZZ's own JUNGLE.c code never saved or
                     * reloaded it, see this function's own top -- no debug in JILL.H's app-config
                     * struct), so this row stays a plain live toggle of the same in-session-only
                     * global, matching upstream's own behavior exactly rather than promoting it to
                     * something it never was. Boolean, so it gets the same LEFT-forces-OFF/RIGHT-
                     * forces-ON idiom MIRROR's own row above already uses. */
                    (void)domenu_at(message, "JKLMNOB", 1, 7, 0, 20, 10, 9, 48, &cursor,
                                     (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) |
                                     (1u << 5),
                                     &slider_dx);
                    if (key == 'J') {
                        jill_color_setting =
                            ((jill_color_setting + (slider_dx != 0 ? slider_dx : 1)) % 4 + 4) % 4;
                        jill_save_app_config();
                        jill_refresh_color_override();
                        statmodflg |= mod_screen;
                    } else if (key == 'K') {
                        jill_enemyspeed_setting =
                            ((jill_enemyspeed_setting + (slider_dx != 0 ? slider_dx : 1)) % 3 + 3) % 3;
                        jill_save_app_config();
                        statmodflg |= mod_screen;
                    } else if (key == 'L') {
                        jill_attackspeed_setting =
                            ((jill_attackspeed_setting + (slider_dx != 0 ? slider_dx : 1)) % 3 + 3) % 3;
                        jill_save_app_config();
                        statmodflg |= mod_screen;
                    } else if (key == 'M') {
                        jill_movespeed_setting =
                            ((jill_movespeed_setting + (slider_dx != 0 ? slider_dx : 1)) % 3 + 3) % 3;
                        jill_save_app_config();
                        statmodflg |= mod_screen;
                    } else if (key == 'N') {
                        jill_mirror_enabled = (slider_dx != 0) ? (slider_dx > 0) : !jill_mirror_enabled;
                        jill_save_app_config();
                        host_set_mirror_enabled(jill_mirror_enabled);
                        statmodflg |= mod_screen;
                    } else if (key == 'O') {
                        /* See this row's own comment just above (in the message/domenu_at() block)
                         * for why this deliberately skips jill_save_app_config() -- plain toggle of
                         * upstream's own session-only `debug` global, same as ZZZ. */
                        debug = (word)((slider_dx != 0) ? (slider_dx > 0) : !debug);
                        statmodflg |= mod_screen;
                    } else {
                        break; /* BACK, or key_back -- either way, out to the outer PAUSED menu. */
                    }
                }
            }
            /* Android port addition, not upstream -- wootbeer's own report: "when I open the
             * enhancement sub-menu, turn mirror mode on, then close the sub-menu and go back to the
             * pause menu, I can still see the enhancement sub-menu sticking out from under the pause
             * menu." Same window_width=9 as the top-level PAUSED menu, so this was never the width-
             * mismatch overhang case 'G''s own comment just above describes -- a DIFFERENT gap, only
             * visible with Mirror Mode on: this menu's own for(;;) loop calls domenu_at() fresh every
             * redraw, including right after MIRROR gets toggled, so its own on-screen box always gets
             * proper shadow protection while it's actually open (HOSTANDROID.c's own
             * host_set_mirror_exclude_rect() comment has the shadow mechanism). The gap is on the way
             * OUT: with no redraw of the real board here before returning, this window's own last-
             * drawn pixels (border, row text, "MIRROR: ON") are still sitting in *gamevp when the
             * outer PAUSED menu's own next domenu_at() call runs -- and that call captures whatever's
             * THERE as its own "clean" background for ITS OWN shadow fill, so with Mirror on it
             * pre-flips a mirrored copy of this menu's stale leftover content into the shared shadow
             * region instead of real clean board -- visible exactly where this window and the
             * (same-width) PAUSED menu it returns to don't happen to overlap. Same fix as case 'G''s
             * own exit-side drawboard() just above, just needed here for a different reason: this
             * repaints the real board before that next capture ever happens, so there's nothing stale
             * left for it to capture. */
            drawboard();
            break;
        case 'Q': /* QUIT -- its own Continue Game/Quit to Main Menu/Quit to DOS submenu, styled
                   * (and sized) the same way askquit()'s own "REALLY QUIT?" submenu already is. */
            /* Android port addition, not upstream -- same "hide the top-level PAUSED menu before a
             * submenu/dialog opens over it" fix as case 'N''s own matching comment above (that
             * comment has the whole story). */
            drawboard();
            (void)domenu("7QUIT?\r"
                          "2CONTINUE GAME\r"
                          "2QUIT TO MAIN MENU\r"
                          "4QUIT TO DOS\r",
                          "CMD", 1, 3, 0, 20, 10, 9, 64);
            if (key == 'C') {
                jill_drain_menu_exit_buttons(); /* see that function's own comment -- same reasoning
                                                  * as RESUME above. */
                return 0; /* Same outcome as RESUME above. */
            } else if (key == 'M') {
                /* Android port addition, not upstream -- wootbeer's own follow-up ask: "turn off mirror
                 * mode before going back to the main menu (when selecting back to menu from pause
                 * menu) so we don't accidently cause issues with it." Same "session-only" reasoning
                 * as jmenu()'s own 'P' (PLAY) reset (that case's own comment, below, has the whole
                 * story) -- quitting all the way back to the main menu ends the current session just
                 * as surely as never having started one, so whatever comes next from that main menu
                 * (a brand new PLAY, same as before, OR a RESTORE -- the one path a PLAY-only reset
                 * could never reach, since jmenu()'s own 'R' case deliberately leaves this alone, by
                 * design, to keep restoring MID-session state faithful) should start from Mirror
                 * off, not silently inherit whatever this now-ending session left it at. Deliberately
                 * NOT jill_save_app_config() here either, same reasoning as jmenu()'s own reset --
                 * this only clears the live, in-session value, leaving the player's actual persisted
                 * preference (config.dat) untouched. play()'s own key_pause case, right where this
                 * return value lands, already unconditionally re-pushes jill_mirror_enabled to the
                 * renderer regardless of gameover's value, so this takes effect the instant control
                 * gets there -- no extra host_set_mirror_enabled() call needed here. */
                jill_mirror_enabled = JILL_MIRROR_OFF;
                return 1; /* play()'s key_pause case turns this straight into gameover=1. */
            } else if (key == 'D') {
                /* rexit() runs every one of upstream's own real cleanup calls (savecfg()/
                 * snd_exit()/shm_exit()/gc_exit()/gr_exit()/host_close()) and then longjmp()s all
                 * the way back to jill_jni_bridge.c's jill_run_game() -- the real "end the whole
                 * program" behavior QUIT TO DOS meant on actual DOS, and already this project's
                 * own established mechanism for it (see rexit()'s own header-comment entry,
                 * change 7, and jill_run_game()'s own comment). Never returns. */
                rexit(0);
            }
            break;
        default:
            break;
        }
    }
}

void dodemo(void)
{
    /* Android port addition, not upstream -- same "keep the main menu's own look Default" ask
     * play()'s own new lines above exist for (see that function's own comment for the whole
     * two-variable story): dodemo() is attract-mode playback, reached from jmenu()'s own 'D' key
     * while jmenu()'s own loop-top hook has already forced Default, but jmenu() only re-enters its
     * own loop top (and re-forces Default) on ITS OWN next iteration -- dodemo() itself never
     * returns to jmenu() between demo segments (its own do/while loop below calls play(1)
     * directly, segment after segment), and play()'s own restore-real-preference hook a few lines
     * up the file would otherwise flip every one of those segments to the player's real persisted
     * mode the instant the first one starts. Forcing it back to Default here, once, before this
     * function's own drawgamewin()/drawcmds() just below (which need the geometry already correct
     * to draw the right thing), keeps every demo segment Default-shaped the same way jmenu() itself
     * always is -- play()'s own hook then sees jill_active_display_mode already equal to
     * JILL_DISPLAY_DEFAULT on every one of dodemo()'s own play(1) calls and does nothing further,
     * exactly as intended for attract-mode playback. */
    if (jill_active_display_mode != JILL_DISPLAY_DEFAULT)
        jill_activate_display_mode(JILL_DISPLAY_DEFAULT);

    setpagemode(1);
    gamecount = 0;
    fout();
    drawgamewin();
    drawcmds();
    demonum = 0;
    macabort = 0;

    /* Android port addition, not upstream -- wootbeer's own bug report: "the next menu item that
     * doesn't work, demo, it just opens the demo for a split second then goes back to main menu."
     * Root cause, once the OTHER half of this bug was fixed (the demo .mac files themselves not
     * being copied onto the device at all -- see JillActivity.java's own isDemoFile() comment):
     * whichever button confirmed "DEMO" in jmenu()'s own domenu_at() call leaves a SECOND,
     * still-queued one-shot keypress sitting in HOSTANDROID.c's own host_queue. JillView.java's
     * own handleKey() forwards a physical A/B press as TWO separate native calls, not one --
     * jillKeyEvent(K_MENU_CONFIRM/K_MENU_CANCEL, ...) (KEYBOARD.H's own key_menu_confirm/
     * key_menu_cancel comment has the whole "Remap Gamepad" reasoning) AND jillGamepadButtonRaw()
     * for whatever real action (Throw/Jump by default) that same physical button is separately
     * bound to -- both end up calling jillhost_notify_key(), and every down edge queues its own
     * entry there (see that function's own comment). domenu_at()'s own checkctrl0() call only
     * ever drains ONE queued entry per tick (GAMECTRL.c's own checkctrl()), and it accepts --
     * breaking out of its own loop -- the instant EITHER signal reads true, so the SECOND queued
     * entry (typically key_alt/key_shift, from the raw-forward call) is still sitting there,
     * completely undrained, the moment control reaches here. Harmless for every other menu choice
     * (a leftover key just gets silently read and discarded on that screen's own next ordinary
     * checkctrl() tick), but GAMECTRL.c's own getmac() -- what actually reads demo macro input
     * once playmac() below succeeds -- treats ANY queued key at all as "the player wants to skip
     * this demo" (real upstream behavior, macabort's own "any key aborts" rule, see that
     * function's own header comment), so that stray leftover entry aborts playback on literally
     * its own first tick, before a single real macro instruction is ever read -- exactly the
     * "split second" symptom: the demo's own opening board still renders once (loadboard()/
     * drawboard()/pageflip() below, same as any real level), playmac() succeeds, but play(1)
     * exits again almost instantly. Flushing every currently-queued key here, once, right before
     * this function's own do-while loop ever calls playmac()/play() for the first time,
     * guarantees getmac()'s own very first abort check starts from a genuinely empty queue --
     * same "clean state before a mode that cares about queued keys" hygiene
     * jill_drain_menu_exit_buttons() (above) already established for fire1/fire2's own
     * continuous-held state, just for the one-shot queue getmac() actually reads instead. */
    while (k_pressed()) {
        k_read();
    }

    do {
        if (demonum != 0) {
            fout();
            setpagemode(1);
        }
        if (demolvl[demonum] == 0) demonum = 0;
        loadboard(demoboard[demonum]);
        pl.level = demolvl[demonum];
        p_reenter(0);
        drawboard();
        pageflip();
        setpagemode(0);
        fin();
        playmac(demoname[demonum]);
        if (macplay) {
            play(1);
            stopmac();
            ++demonum;
        } else {
            macaborted = 1;
        }
    } while (!macaborted && host_is_open());
}

void jmenu(void)
{
    int quit = 0;
    int c;

    loadboard(JILL_INTRO_LEVEL);
    while (host_is_open() && !quit) {
        /* Android port addition, not upstream -- wootbeer's own follow-up ask: "this option persists
         * to the main menu, I want to keep the default look for the main menu if possible" -- see
         * JILL.H's own comment on jill_display_mode vs jill_active_display_mode for the whole
         * two-variable story. Checked at the top of every loop iteration, not just once before the
         * loop starts, because this same iteration's own body below (setorigin()/drawboard(), plus
         * whichever key branch runs after domenu() returns) is what actually redraws the intro
         * board and statvp face icons every time control comes back here -- including right after
         * play(0)/dodemo() return (gamevp may still be sized for whatever mode the player's own
         * preference actually is, see play()'s own restore-on-entry hook) and after any of the
         * synchronous pageview() sub-screens (STORY/INSTRUCTIONS/CREDITS/HELP) this same switch can
         * reach below, since those run and return within a single iteration, after this check has
         * already forced Default for it. A full drawgamewin()/drawcmds() redraw (not just flipping
         * the mode variable) is needed here, unlike play()'s own guarded case: unlike play(), this
         * loop's own body doesn't call drawgamewin()/drawcmds() every iteration on its own (only
         * the 'P'/0x10/'R' key branches below do, right before entering play()), so without this,
         * a gamevp still sized/laid out for Minimal UI or the CONTROLS/INVENTORY panels being left
         * undrawn from a prior Minimal UI session would linger behind the freshly-drawn intro
         * board. Synchronous, immediately followed by this same iteration's own setpagemode(1)/
         * setorigin()/drawboard()/pageflip()/setpagemode(0) sequence just below with no intervening
         * host_pump(), so this is a second back-to-back redraw+pageflip, not a visible flash.
         *
         * Goes through jill_redraw_after_display_mode_change() (its own comment has the full
         * story) rather than a hand-rolled sequence -- an earlier round of this fix called
         * drawboard() here directly, before setorigin() had recentered vpox/vpoy for the new
         * geometry, which would have painted the intro board at a stale scroll position for one
         * frame (immediately overwritten by this same iteration's own setorigin()+drawboard() just
         * below regardless, so not something wootbeer would actually have seen -- but worth fixing
         * properly while consolidating this and play()'s own hook onto the same helper). */
        if (jill_active_display_mode != JILL_DISPLAY_DEFAULT) {
            jill_activate_display_mode(JILL_DISPLAY_DEFAULT);
            jill_redraw_after_display_mode_change();
        }

        setpagemode(1);
        setorigin();
        drawboard();
        printhi(0);
        clearvp(&botvp);
        fontcolor(statvp, 7, 8);
        clearvp(statvp);
        if (facetable != 0 && x_ourmode == x_vga) {
            for (c = 0; c < 16; ++c)
                drawshape(statvp,
                          mod_virtual + facetable * 256 + c,
                          (c & 3) * 16, (c >> 2) * 16);
        } else {
            wprint(statvp, 0, 28, 2, "");
            wprint(statvp, 0, 36, 2, "");
        }
        pageflip();
        setpagemode(0);

#if defined(JILL_EP1)
        /* Android port change, not upstream -- wootbeer's own explicit ask: "can we remove the epic bbs
         * option from the main menu? it doesn't function here, and doesn't exist in the actual game."
         * Real DOS Jill's own shareware-era EPIC'S BBS row dialed Epic MegaGames' own long-defunct
         * dial-up BBS (see pageview(20)'s own former call site just below, in this function's own
         * key-handler block -- that text page and the 'E' key that reached it are both gone now
         * too), something this Android port obviously has no way to actually do and never
         * implemented any behavior for -- it was carried over into the menu text/key_table purely
         * because this whole choice list was ported line-for-line from upstream, not because it was
         * ever meant to function here. Choices dropped from 10 to 9 and key_table's trailing 'E'
         * removed to match -- this makes Episode 1's own choice list byte-for-byte identical to
         * Episodes 2/3's own list just below (same key_table, same "1, 9, 1, 24, 9, 8, 64" params),
         * which already never had this row (EPIC'S BBS was Episode 1's own shareware-episode-only
         * upsell, never a thing in Episodes 2/3's registered-order questionnaire to begin with). */
        (void)domenu("7PICK A CHOICE:\r"
                     "2PLAY\r"
                     "2RESTORE\r"
                     "5STORY\r"
                     "5INSTRUCTIONS\r"
                     "5ORDERING INFO\r"
                     "5CREDITS\r"
                     "3DEMO\r"
                     "3NOISEMAKER\r"
                     "4QUIT\r",
                     "PRSIOCDNQ", 1, 9, 1, 24, 9, 8, 64);
#elif defined(JILL_EP2) || defined(JILL_EP3)
        (void)domenu("7PICK A CHOICE:\r"
                     "2PLAY\r"
                     "2RESTORE\r"
                     "5STORY\r"
                     "5INSTRUCTIONS\r"
                     "5ORDERING INFO\r"
                     "5CREDITS\r"
                     "3DEMO\r"
                     "3NOISEMAKER\r"
                     "4QUIT\r",
                     "PRSIOCDNQ", 1, 9, 1, 24, 9, 8, 64);
#endif
        setpagemode(0);

        /* Android port addition, not upstream -- diagnostic only, no behavior change. wootbeer's own
         * report: exiting Episode 1's DEMO with "nearly any" gamepad button lands back on the
         * Android episode-select screen instead of just re-showing this main menu -- the only way
         * that can happen is this main menu's own quit=1 branch immediately below firing on the
         * very first poll right after dodemo() returns (jmenu()'s own natural "QUIT" path, real
         * upstream 'Q'/ESC behavior, is what actually tears the whole app down via rexit()-style
         * cleanup -- see host_close()'s own JillView.java comment for why that's a full app exit,
         * not just a screen change). Nothing here proved WHY yet: domenu_at()'s own entry-side
         * fire1off/fire2off/menu_confirmoff/menu_canceloff suppression (that function's own
         * comment) should already guard against a button still physically held over from
         * dodemo()'s own exit read reading as an instant confirm/back here, and a leftover ONE-SHOT
         * queued key (this file's own dodemo() comment on why getmac() can leave one of those
         * behind) isn't covered by that same suppression at all -- key==key_escape gets silently
         * rewritten to 'Q' before domenu_at()'s own accept check ever runs (see that function's own
         * comment), so a stale queued key_escape specifically would explain this exactly, but
         * nothing here confirms that's the actual value. Logs every real key this menu ever reads,
         * not just quit's own case, since a silent, working PLAY/DEMO/etc. selection right
         * afterward is just as useful a data point as the bad one -- this line is cheap enough
         * (once per menu poll, never once per game frame) to leave in either way. */
        JILL_LOGI("jmenu: key=%d ('%c') fire1=%d fire2=%d menu_confirm=%d menu_cancel=%d",
                  key, (key >= 32 && key < 127) ? (char)key : '?', fire1, fire2, menu_confirm,
                  menu_cancel);

        if (key == key_escape || key == 'Q') {
            quit = 1;
        } else if (key == 'P') {
            gamecount = 0;
            setpagemode(1);
            fout();
            drawgamewin();
            drawcmds();
            loadboard(JILL_START_LEVEL);
            pl.level = JILL_START_LEVEL_NUMBER;
            /* Android port addition, not upstream -- Cheat Codes are session-only (JILL.H's own
             * jill_cheat_unlimited_gems comment); a brand new game always starts with every one
             * back off, matching GoT Android's own precedent (wootbeer's own ask: "off by default, new
             * game, reloads etc."). */
            jill_cheats_reset();
            /* Android port addition, not upstream -- wootbeer's own follow-up ask: "can we actually make
             * mirror mode one of the settings that doesn't persist between starting brand new games
             * from the main menu? only when restoring in the same session of gameplay." Deliberately
             * placed here, not in loadgame() (shared by jmenu()'s own 'R' case and pausemenu()'s own
             * 'L' case, both restore paths, JUNGLE.c) -- restoring a save is exactly the one case
             * wootbeer wants this to keep whatever Mirror Mode state the current session already has, so
             * only THIS branch (starting a genuinely new game) touches it. Unlike jill_cheats_reset()
             * just above, this deliberately does NOT call jill_save_app_config() -- it resets the
             * live, in-session state for this fresh game only, leaving whatever the player last
             * explicitly toggled (and had persisted to config.dat) untouched as their actual saved
             * preference for next time the app itself launches; play()'s own entry hook (this
             * function's own comment there) picks this fresh value up and pushes it to the renderer
             * the instant real control begins, same as any other toggle. */
            jill_mirror_enabled = JILL_MIRROR_OFF;
            p_reenter(0);
            drawboard();
            pageflip();
            setpagemode(0);
            fin();
            play(0);
            sb_playtune(JILL_MENU_MUSIC);
            loadboard(JILL_INTRO_LEVEL);
        } else if (key == 0x10) {
            setpagemode(1);
            drawgamewin();
            drawboard();
            drawcmds();
            pageflip();
            setpagemode(0);
            dolevelsong();
            curlevel[0] = '\0';
            play(0);
        } else if (key == 'R') {
            if (loadgame()) {
                setpagemode(1);
                fout();
                drawgamewin();
                drawcmds();
                setorigin();
                drawboard();
                dolevelsong();
                pageflip();
                setpagemode(0);
                fin();
                play(0);
                sb_playtune(JILL_MENU_MUSIC);
                loadboard(JILL_INTRO_LEVEL);
            }
        } else if (key == 'S') {
            pageview(0);
        } else if (key == 'I') {
            dotextmsg(1);
        } else if (key == 'O' || key == 'H') {
            pageview(8);
        } else if (key == 'C') {
            pageview(12);
            /* Android port note, not upstream -- Episode 1 used to have a "'E' -> pageview(20)"
             * branch here for its own EPIC'S BBS text page, removed alongside that menu row itself
             * (see the domenu() call above this loop for the full story: wootbeer's own ask, "it doesn't
             * function here, and doesn't exist in the actual game"). 'E' is no longer in Episode 1's
             * own key_table at all now, so this branch could never have been reached anyway. */
        } else if (key == 'D') {
            dodemo();
            sb_playtune(JILL_MENU_MUSIC);
            loadboard(JILL_INTRO_LEVEL);
        } else if (key == 'N') {
            pageview(99);
        } else if (key == 5) {
            setpagemode(1);
            drawgamewin();
            drawboard();
            pageflip();
            setpagemode(0);
            design();
        }
    }
}

/* Set by jill_jni_bridge.c's jill_run_game() with setjmp() right before it calls main() below --
 * see this function's own header-comment entry (change 7) for why rexit() jumps back to it instead
 * of calling exit(). Not upstream's own global -- this project's, same as jill_dma_path etc. */
extern jmp_buf jill_quit_jmp;

void rexit(int result)
{
    /* Android port addition, not upstream -- diagnostic only, no behavior change. wootbeer's own
     * report: Episode 1's DEMO played "great until Jill picked up some apples, then it crashed
     * the app completely" -- the very next Logcat line after that was actually "rexit(1) unwound
     * back here" (jill_jni_bridge.c's own jill_run_game(), see that log line's own comment), not a
     * native SIGSEGV/FATAL EXCEPTION at all: a clean, real upstream "Quit to DOS" unwind, just one
     * neither the player nor the recorded demo macro ever asked for. Upstream's own file-load
     * error codes (loadboard()'s own rexit(1)/(2)/(3)/(4) just above, SHM.c's rexit(100)-(103)/
     * (115), MUSIC.c's rexit(154), etc.) all look identical to a real player-requested quit from
     * here, with zero indication of WHICH file or step actually failed -- rexit(1) specifically
     * covers both "the board file wouldn't even open" and "it opened but read short", so it can't
     * even be narrowed to a missing file without more information. curlevel (this file's own
     * global, set by loadboard() before it can ever fail) is the one piece of context that
     * survives to here for that whole family of codes; logged before any of this function's own
     * real cleanup calls below run (savecfg() etc. can themselves touch errno), so a repeat of
     * this exact bug shows the real failing filename/errno directly in Logcat instead of just a
     * bare result code -- see this file's own top-of-file #include <android/log.h>/JILL_LOGE
     * comment for why this one log call doesn't pull in a full host header for it. */
    JILL_LOGE("rexit(%d) -- curlevel=\"%s\" errno=%d (%s)", result, curlevel, errno, strerror(errno));

    savecfg();
    snd_exit();
    shm_exit();
    gc_exit();
    gr_exit();
    host_close();
    longjmp(jill_quit_jmp, result != 0 ? result : 1);
}

int main(int argc, char **argv)
{
    char shape_path[520], sound_path[520];
    byte old_palette[JILL_PALETTE_SIZE * 3];
    const char *level = NULL;
    int validate_only = 0;
    int window_scale = 3;
    int index;

    host_start_clock();
    for (index = 1; index < argc; ++index) {
        if (strcasecmp(argv[index], "--validate") == 0) validate_only = 1;
        else if (strncasecmp(argv[index], "--scale=", 8) == 0) window_scale = atoi(argv[index] + 8);
        else if (strcasecmp(argv[index], "--play") == 0) { }
        else if (argv[index][0] != '/' && argv[index][0] != '-') level = argv[index];
    }
    cfg_getpath(argc, argv);
    data_path(tempname, sizeof(tempname), "temp");
    (void)loadcfg();
    /* Android port addition, not upstream -- see jill_load_app_config()'s own comment
     * (right alongside loadcfg()/savecfg() above) for why this is its own file, read here rather
     * than from anywhere in the save-game path. */
    jill_load_app_config();
    cfg_init(argc, argv);
    data_path(shape_path, sizeof(shape_path), JILL_SHAPE_FILE);
    data_path(sound_path, sizeof(sound_path), JILL_SOUND_FILE);
    snd_init(sound_path);
    gc_init();

    /* The native window is the SDL keyboard transport.  Open it before the
       original configuration dialog so getkey() receives the same keystrokes
       that DOS read from the BIOS.  --validate is a host-only test path. */
    if (!validate_only &&
        !host_open(JILL_WINDOW_TITLE, window_scale)) {
        fprintf(stderr, "Jill recovery: unable to create the game window.\n");
        snd_exit();
        gc_exit();
        return 3;
    }
    if (!validate_only && !doconfig()) {
        snd_exit();
        gc_exit();
        host_close();
        return 0;
    }

    if (!validate_only)
        host_set_title(JILL_WINDOW_TITLE);

    gr_init();
    clrpal();
    if (!validate_only) (void)savecfg();
    (void)shm_init(shape_path);

    if (validate_only) {
        set_game_layout();
        initinfo();
        initobjinfo();
        initboard();
        initobjs();
        if (level == NULL) level = JILL_INTRO_LEVEL;
        loadboard(level);
        printf("Jill recovery: loaded %s (%d objects, level %d, score %lu)\n",
               curlevel, (int)numobjs, (int)pl.level, (unsigned long)pl.score);
        snd_exit(); shm_exit(); gc_exit(); gr_exit(); host_close();
        return 0;
    }

    shm_want[1] = 1;
    shm_want[2] = 1;
    shm_want[7] = 1;
    shm_do();
    fontcolor(&mainvp, 9, 0);
    memcpy(old_palette, vgapal, sizeof(old_palette));
    pleasewait();
    snd_do();
    sb_playtune(JILL_MENU_MUSIC);
    shm_want[3] = 1;
    shm_want[4] = 1;
    shm_want[5] = 1;
    shm_want[6] = 1;
    shm_want[8] = 1;
    shm_want[14] = 1;
    shm_do();
    fout();
    memcpy(vgapal, old_palette, sizeof(old_palette));

    set_game_layout();
    initinfo();
    initobjinfo();
    initboard();
    initobjs();
    /* Android port addition, not upstream -- applies jill_color_setting's own persisted value
     * (jill_load_app_config(), near main()'s own top) the moment real gameplay can start,
     * same "apply once at startup, not just from the menu that changes it" precedent
     * jill_display_mode's own restore-on-entry hooks already establish elsewhere. Placed here
     * rather than any earlier alongside jill_load_app_config() itself: x_ourmode (gr_init(),
     * well before this point) has to already be set before xlate_table_core() can decode anything
     * -- see jill_refresh_color_override()'s own comment. */
    jill_refresh_color_override();
    if (level != NULL) {
        loadboard(level);
        setpagemode(1);
        drawgamewin();
        drawcmds();
        setorigin();
        drawboard();
        pageflip();
        setpagemode(0);
        fin();
        play(0);
    } else if (xdemoflag) {
        dodemo();
    } else {
        setpagemode(1);
        drawgamewin();
        pageflip();
        setpagemode(0);
        fin();
        jmenu();
    }
    fout();
    undrawwin(&ourwin);
    snd_exit(); shm_exit(); gc_exit(); gr_exit(); host_close();
    return 0;
}

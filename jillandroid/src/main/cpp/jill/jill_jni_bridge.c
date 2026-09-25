/*
 * jill_jni_bridge.c -- JNI entry points for the Android build. As of this round, jillMain() calls
 * the real engine for real: jill/JUNGLE.c's own main() -- the same entry point the desktop build
 * uses -- runs Jill's real menu (jmenu()), real gameplay loop (play()), real save/load, the works.
 * This replaces every earlier round's synthetic test harness (jill_test_loop() and its own
 * checkpoint helpers: test_input_solid_color()/count_loaded_dma_entries()/verify_object_system()/
 * spawn_player_object()/find_player_shape_table()/draw_test_frame() -- all now gone), exactly as
 * that harness's own header comment always said would happen once "more of the real engine
 * (JUNGLE.C for the real game loop chief among it) is wired in".
 *
 * Three real things had to be resolved to make that call safe -- the first two without touching
 * JUNGLE.c/CONFIG.c's own logic at all; the third is a small, deliberate, documented exception to
 * that (see its own entry below):
 *
 *   1. Path resolution. JUNGLE.C's own main() resolves the shape/sound/config/save file paths via
 *      a DOS-relative mechanism: CONFIG.c's cfg_getpath()/cfg_path (normally set from a DOS-style
 *      "/P<path>" command-line switch) plus JUNGLE.c's own data_path() helper, which just
 *      concatenates cfg_path onto an EPISODE.H filename macro -- and loadboard() (also JUNGLE.c)
 *      opens its own level-file arguments (JILL_INTRO_LEVEL, JILL_START_LEVEL, the demoboard[]
 *      table, etc.) as plain bare relative filenames with no path construction at all. Both are
 *      exactly how a DOS EXE behaves when launched from its own install directory: every relative
 *      path in the program just resolves against the process's current working directory. Android
 *      has a real equivalent of that: chdir(). jill_run_game() below chdir()s the process into the
 *      real per-episode asset directory JillActivity.java's SAF copier populated (derived from the
 *      already-JNI-supplied sha_path -- see jill_episode_dir_from_sha_path()) before ever calling
 *      main(), so both of those upstream path mechanisms resolve correctly with zero changes to
 *      either one -- cfg_path is left at its own upstream default ("", never set via a "/P" argv,
 *      see jill_run_game()'s own comment), and data_path()/loadboard() keep doing exactly what they
 *      always did. This is the same reasoning jill.dma's own jill_dma_path override used (PLATFORM.c/
 *      JINFO.c) -- an Android-side absolute path feeding a mechanism upstream's own code already
 *      has -- just via chdir() instead of a new global, since here the consumers (data_path() AND
 *      loadboard()) don't share a single override point the way JINFO.c's initinfo() did.
 *
 *   2. Filename case. JillActivity.java's own EPISODES table -- the real on-disk names its SAF
 *      copier writes -- is lowercase for every episode ("jill1.cfg"/"jill1.sha"/"jill1.vcl", etc.),
 *      but EPISODE.H's own Episode 1 macros were spelled upper case ("JILL1.CFG" etc.), which is
 *      harmless on DOS/Windows' case-insensitive filesystems but would not match on Android's real
 *      case-sensitive one. Episodes 2 and 3 were already lowercase upstream. Fixed in EPISODE.H
 *      itself (see that file's own comment), not here -- a data-only macro spelling fix, not a path-
 *      resolution concern.
 *
 *   3. rexit() calling exit(). Found the hard way: the very first time rexit() actually fired on a
 *      real device (loadboard()'s own rexit(1), triggered by jmenu()'s opening loadboard(
 *      JILL_INTRO_LEVEL) call), upstream's own trailing exit(result) tore down the ENTIRE Android
 *      process from this background thread -- not a clean "play session over", a destroyed-mutex
 *      SIGABRT on the AChoreographer thread moments later. JUNGLE.c's rexit() now longjmp()s to
 *      jill_quit_jmp (declared just below) instead of calling exit() -- see that function's own
 *      comment in JUNGLE.c (change 7 in that file's own header list) for the full reasoning, and
 *      jill_run_game()'s own comment below for the setjmp() side of it. This is the one real,
 *      deliberate exception to "neither touching JUNGLE.c/CONFIG.c's own logic" above: every one of
 *      rexit()'s real cleanup calls still runs first, unchanged, in the same order; only its own
 *      final "how does the caller get control back" mechanism is swapped.
 *
 * Sound/music playback (MUSIC.c's sb_playtune() et al): digitized sound effects (snd_play()'s own
 * VOC path) are real, and so is music now -- jillMain() below calls jillhost_bind_audio_view() before
 * jill_run_game() so HOSTANDROID.c's host_audio_play_voc()/host_audio_play_cmf()/etc. have a JillView
 * to call back into (see that function's own comment, and HOSTANDROID.c's host_audio_* section, for
 * the whole story -- host_audio_play_cmf() drives a real software OPL2 emulator, descore/audio_opl/,
 * synthesizing the user's own *.ddt CMF data in real time rather than embedding/redistributing any
 * pre-rendered copy of it). Separately: JILL_MENU_MUSIC/*.ddt files live in a "shared" directory
 * JillActivity.java copies into, a true sibling of the per-episode directory jill_run_game() chdir()s
 * into below, not inside it -- so MUSIC.c's own relative fopen() calls (UNKNOWN.c's OpenElement(),
 * which has no path-override hook the way jill.dma's own jill_dma_path does) would never find them by
 * chdir() alone. jill_link_shared_ddt_files() below closes that gap the same "make a relative
 * fopen() just work" way point 1 above already relies on for every other asset -- one step removed:
 * it symlinks every *.ddt file out of that sibling "shared" directory into the per-episode directory
 * right after the chdir(), rather than duplicating files at Java copy-time.
 *
 * jillSurfaceResized()/jillKeyEvent()/jillRequestStop() below are unchanged -- they still feed
 * HOSTANDROID.c's own host layer, which GAMECTRL.c's checkctrl()/getkey() (via KEYBOARD.c's
 * k_pressed()/k_status(), which already call host_pump() every time they're polled) read from
 * every frame inside JUNGLE.c's real play()/jmenu() loops -- no new wiring needed there at all.
 */

#include "HOSTANDROID.H"
#include "JILL.H"
#include "KEYBOARD.H"

#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <jni.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>   /* rename() -- jill_link_shared_config_file()'s own migration step. */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* lstat()/S_ISLNK()/S_ISREG() -- jill_link_shared_config_file()'s own probe
                        * of whatever's already sitting at the per-episode "config.dat" path. */
#include <unistd.h>

#define LOG_TAG "jillhost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

/* JUNGLE.c's own real entry point -- not declared in JUNGLE.H (that header's own declarations are
 * the functions jmenu()/play()/etc. that main() itself calls, not main() itself, matching the
 * desktop build where main() is just the file's own C entry symbol, not a cross-file API). Calling
 * it by name here is safe: this is a JNI shared library, not an executable, so "main" is just an
 * ordinary exported C symbol with no special linker meaning. */
extern int main(int argc, char **argv);

/* Where JUNGLE.c's own rexit() longjmp()s back to instead of calling upstream's own exit() -- see
 * that function's own comment (JUNGLE.c, change 7 in this file's own header list) for why: exit()
 * from a background native thread tears down the whole Android process, not just this play session,
 * and was observed doing exactly that (a destroyed-mutex SIGABRT on the AChoreographer thread) the
 * first time a legitimate rexit() ever fired on-device. jill_run_game() below sets this with
 * setjmp() immediately before calling main(); this is the one and only place it's read or written
 * outside JUNGLE.c's own rexit(). */
jmp_buf jill_quit_jmp;

/* Derives the real per-episode asset directory jill_run_game() needs to chdir() into, straight from
 * the sha_path JNI already supplies (JillGameActivity.java's own episodeShaPath(N), e.g.
 * ".../jill/ep1/jill1.sha") -- no new JNI parameter needed, since the directory containing that
 * file IS the directory every other per-episode asset (jill1.cfg, jill1.vcl, intro.jn1, map.jn1,
 * every other *.jn1 level file) was copied into by JillActivity.java's SAF copier. Returns a
 * malloc'd, '/'-terminated string the caller must free(), or NULL if sha_path has no '/' in it. */
static char *jill_episode_dir_from_sha_path(const char *sha_path)
{
    const char *slash;
    size_t dir_len;
    char *dir;

    if (sha_path == NULL) return NULL;
    slash = strrchr(sha_path, '/');
    if (slash == NULL) return NULL;
    dir_len = (size_t)(slash - sha_path) + 1; /* keep the trailing '/' */
    dir = (char *)malloc(dir_len + 1);
    if (dir == NULL) return NULL;
    memcpy(dir, sha_path, dir_len);
    dir[dir_len] = '\0';
    return dir;
}

/* Derives the "shared" directory that is a true sibling of the per-episode directory
 * jill_episode_dir_from_sha_path() above computes -- JillActivity.java's own SAF copier writes
 * JILL_MENU_MUSIC/*.ddt files (and jill.dma) into jillRootDir/shared/, a direct sibling of each
 * jillRootDir/ep{N}/ episode directory (see that Java file's own SHARED_DIR/EpisodeSpec.dirName()),
 * not into the per-episode directory itself. Takes the same '/'-terminated episode_dir string
 * jill_episode_dir_from_sha_path() returns and walks up exactly one more path component. Returns a
 * malloc'd, '/'-terminated string the caller must free(), or NULL if episode_dir doesn't have a
 * second '/' to walk up to (shouldn't happen for any real sha_path JillActivity.java ever produces --
 * those always look like ".../jill/ep{N}/jill{N}.sha"). */
static char *jill_shared_dir_from_episode_dir(const char *episode_dir)
{
    size_t len;
    const char *p;
    size_t parent_len;
    char *dir;
    static const char shared[] = "shared/";

    if (episode_dir == NULL) return NULL;
    len = strlen(episode_dir);
    if (len == 0) return NULL;

    /* episode_dir is '/'-terminated (".../jill/ep1/") -- step back past that trailing slash, then
     * find the NEXT '/' before it, to land on the parent directory (".../jill/"). */
    p = episode_dir + len - 1;
    while (p > episode_dir && *p == '/') p--;
    while (p > episode_dir && *p != '/') p--;
    if (*p != '/') return NULL;
    parent_len = (size_t)(p - episode_dir) + 1; /* keep the trailing '/' */

    dir = (char *)malloc(parent_len + sizeof(shared));
    if (dir == NULL) return NULL;
    memcpy(dir, episode_dir, parent_len);
    memcpy(dir + parent_len, shared, sizeof(shared)); /* includes shared's own trailing '\0' */
    return dir;
}

/* Symlinks every *.ddt file found in the "shared" directory (see jill_shared_dir_from_episode_dir()
 * above) into the current directory -- which by the time this is called, jill_run_game() has already
 * chdir()'d into: the per-episode directory. Makes MUSIC.c's own sb_playtune() -> UNKNOWN.c's
 * OpenElement() (a plain relative fopen(filename, "rb"), with no path-override mechanism to hook the
 * way jill.dma's own jill_dma_path has) find them, via the exact same "chdir() makes relative
 * fopen() calls just work with zero upstream changes" trick this file's own header comment (point 1)
 * already relies on for every other asset -- just one step removed, since *.ddt files live in a true
 * sibling directory rather than the one actually being chdir()'d into. A symlink, not a copy, so
 * nothing needs to be kept in sync if JillActivity.java's own copier ever re-runs; re-linking an
 * existing, already-correct symlink (EEXIST) is expected on a second play session and silently
 * tolerated, not logged as a failure. Best-effort throughout: a missing "shared" directory entirely
 * (the SAF copy step never ran, or the user never picked any menu-music files) is not an error here
 * either -- Jill's real gameplay works fine with no menu music, and host_audio_play_cmf() already
 * fails closed and safe on a file that then can't be found. */
static void jill_link_shared_ddt_files(const char *sha_path)
{
    char *episode_dir;
    char *shared_dir;
    DIR *dir;
    struct dirent *entry;
    size_t shared_len;
    int seen = 0, linked = 0, failed = 0;

    episode_dir = jill_episode_dir_from_sha_path(sha_path);
    if (episode_dir == NULL) {
        LOGI("jill_link_shared_ddt_files: jill_episode_dir_from_sha_path(\"%s\") returned NULL -- "
             "not linking any *.ddt files", sha_path != NULL ? sha_path : "(null)");
        return;
    }
    shared_dir = jill_shared_dir_from_episode_dir(episode_dir);
    if (shared_dir == NULL) {
        LOGI("jill_link_shared_ddt_files: jill_shared_dir_from_episode_dir(\"%s\") returned NULL -- "
             "not linking any *.ddt files", episode_dir);
        free(episode_dir);
        return;
    }
    LOGI("jill_link_shared_ddt_files: scanning \"%s\" for *.ddt files to link into \"%s\"",
         shared_dir, episode_dir);
    free(episode_dir);

    dir = opendir(shared_dir);
    if (dir == NULL) {
        LOGI("jill_link_shared_ddt_files: opendir(\"%s\") failed (errno=%d) -- fine if the SAF copy "
             "step never ran or nothing was selected, but a real problem if you know *.ddt files "
             "were copied", shared_dir, errno);
        free(shared_dir);
        return;
    }

    shared_len = strlen(shared_dir);
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        char *src_path;

        /* Case-sensitive on purpose -- JillActivity.java's own copier writes lowercase ".ddt" names,
         * the same lowercase-vs-uppercase story this file's own header comment (point 2) already
         * tells for every other asset extension. */
        if (name_len < 4 || strcmp(entry->d_name + name_len - 4, ".ddt") != 0) continue;
        seen++;

        src_path = (char *)malloc(shared_len + name_len + 1);
        if (src_path == NULL) { failed++; continue; }
        memcpy(src_path, shared_dir, shared_len);
        strcpy(src_path + shared_len, entry->d_name);

        if (symlink(src_path, entry->d_name) != 0 && errno != EEXIST) {
            LOGI("jill_link_shared_ddt_files: symlink(\"%s\" -> \"%s\") failed (errno=%d)", src_path,
                 entry->d_name, errno);
            failed++;
        } else {
            LOGI("jill_link_shared_ddt_files: linked \"%s\" -> \"%s\"", entry->d_name, src_path);
            linked++;
        }
        free(src_path);
    }
    closedir(dir);
    free(shared_dir);
    LOGI("jill_link_shared_ddt_files: done -- saw %d *.ddt file(s) in the shared directory, "
         "%d linked OK, %d failed", seen, linked, failed);
}

/* Migrates a real per-episode config.dat (if one still exists from before this port shared
 * settings across episodes) into the shared directory, then symlinks "config.dat" in the current
 * directory (the per-episode directory jill_run_game() has already chdir()'d into, same as
 * jill_link_shared_ddt_files() above) to point at it -- see that function's own comment for the
 * whole "chdir() makes relative fopen() calls just work" trick this reuses, one step removed via
 * the shared/ sibling directory the exact same way. wootbeer's own ask: "is there a way we can make
 * settings persist between episodes? like the gamepad mappings, touch settings, sound settings,
 * etc." -- JUNGLE.c's own jill_load_app_config()/jill_save_app_config() already read/write
 * "config.dat" via a plain relative data_path() call (that struct's own header comment used to
 * explicitly document per-episode config.dat as a DELIBERATE choice, "there's no cross-episode
 * compatibility for this to break" -- true until now, see its own updated comment); this function
 * is what turns that same plain relative filename into a real cross-episode file with zero changes
 * needed to either of those two functions, matching the exact same "one extra layer of indirection,
 * nothing upstream has to know about it" shape jill_link_shared_ddt_files() already established.
 *
 * config.dat can't just be treated as "another shared file to symlink in fresh" the way *.ddt files
 * above are, because an episode played before this feature shipped may already have a REAL,
 * non-empty config.dat sitting in its own per-episode directory (from JUNGLE.c's own jill_save_
 * app_config(), writing there via the plain relative path this function is about to start
 * redirecting). Overwriting that with a fresh empty shared file, or symlinking straight over it,
 * would silently throw away whichever episode's settings happen to be the most complete/recently-
 * used one -- so this checks first what's already there:
 *
 *   - Nothing at all (ENOENT) -- a fresh install of this episode, or one that's simply never saved
 *     a setting: just symlink straight to the shared path. The first jill_save_app_config() call
 *     (from ANY episode, whichever the player touches a setting in first) creates the real shared
 *     file the first time it's actually needed.
 *   - Already a symlink -- this function's own prior run (this session or an earlier one) already
 *     did the job; nothing more to do.
 *   - A real (non-symlink, non-directory) file -- genuine pre-existing per-episode settings. If the
 *     shared file doesn't exist yet either, this is the first of the three episodes to reach this
 *     point with real data to migrate: rename() it straight into the shared directory (same
 *     filesystem -- private app storage, both paths under the same jillRootDir tree -- so a plain
 *     rename() is safe, no copy+delete race), then symlink as normal.
 *
 *     If the shared file ALREADY exists (a different episode's own real file already got migrated
 *     first, this run or an earlier one), this episode's own file is now STALE, not just
 *     unmigrated -- the whole point of this feature is that every episode reads and writes the
 *     SAME file from here on, so leaving this one on its own separate file (an earlier version of
 *     this function's own approach) would mean it silently never joins the other two, which is
 *     exactly the bug wootbeer hit testing this: changed settings in Episode 1, they never showed up
 *     in Episode 2 at all, because Episode 2 already had its own leftover real config.dat from
 *     testing before this feature existed, hit this exact branch, and every prior run left it
 *     alone rather than linking it. Fixed by renaming this episode's own stale file ASIDE (to
 *     "config.dat.pre-shared", not deleting it -- same "don't destroy what you can't get back"
 *     caution as every other migration path here) and then symlinking as normal, so this episode
 *     converges onto the shared file too, going forward, the same as a fresh install would. A
 *     harmless no-op if "config.dat.pre-shared" already exists from an earlier run doing the same
 *     thing (rename() onto an existing destination just replaces it -- the CURRENT stale file is
 *     always what's worth keeping a copy of, not whatever an earlier run already backed up). */
static void jill_link_shared_config_file(const char *sha_path)
{
    char *episode_dir;
    char *shared_dir;
    char *shared_path;
    size_t shared_len;
    struct stat st;

    episode_dir = jill_episode_dir_from_sha_path(sha_path);
    if (episode_dir == NULL) {
        LOGI("jill_link_shared_config_file: no usable episode directory, not linking config.dat");
        return;
    }
    shared_dir = jill_shared_dir_from_episode_dir(episode_dir);
    free(episode_dir);
    if (shared_dir == NULL) {
        LOGI("jill_link_shared_config_file: no usable shared directory, not linking config.dat");
        return;
    }

    shared_len = strlen(shared_dir);
    shared_path = (char *)malloc(shared_len + sizeof("config.dat"));
    if (shared_path == NULL) {
        free(shared_dir);
        return;
    }
    memcpy(shared_path, shared_dir, shared_len);
    strcpy(shared_path + shared_len, "config.dat");
    free(shared_dir);

    if (lstat("config.dat", &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            LOGI("jill_link_shared_config_file: \"config.dat\" is already a symlink, nothing to do");
            free(shared_path);
            return;
        }
        if (!S_ISREG(st.st_mode)) {
            LOGI("jill_link_shared_config_file: \"config.dat\" exists but isn't a regular file "
                 "(st_mode=0x%x) -- leaving it alone", (unsigned)st.st_mode);
            free(shared_path);
            return;
        }
        /* A real, pre-existing per-episode config.dat. */
        if (lstat(shared_path, &st) == 0) {
            /* Stale, not a fresh migration source -- the shared file already won. Back this
             * episode's own copy up out of the way (never delete it outright) and fall through to
             * the symlink() call below so this episode actually joins the shared file, instead of
             * silently staying on its own separate one forever (see this function's own comment). */
            if (rename("config.dat", "config.dat.pre-shared") != 0) {
                LOGI("jill_link_shared_config_file: \"%s\" already exists and rename(\"config.dat\", "
                     "\"config.dat.pre-shared\") failed (errno=%d) -- leaving this episode's own "
                     "file in place, unshared, rather than lose it", shared_path, errno);
                free(shared_path);
                return;
            }
            LOGI("jill_link_shared_config_file: \"%s\" already exists -- this episode's own stale "
                 "config.dat backed up to \"config.dat.pre-shared\", now joining the shared file",
                 shared_path);
        } else if (rename("config.dat", shared_path) != 0) {
            LOGI("jill_link_shared_config_file: rename(\"config.dat\", \"%s\") failed (errno=%d) -- "
                 "leaving this episode's own file in place, unshared", shared_path, errno);
            free(shared_path);
            return;
        } else {
            LOGI("jill_link_shared_config_file: migrated this episode's own real config.dat to \"%s\"",
                 shared_path);
        }
    }

    if (symlink(shared_path, "config.dat") != 0 && errno != EEXIST) {
        LOGI("jill_link_shared_config_file: symlink(\"%s\" -> \"config.dat\") failed (errno=%d)",
             shared_path, errno);
    } else {
        LOGI("jill_link_shared_config_file: linked \"config.dat\" -> \"%s\"", shared_path);
    }
    free(shared_path);
}

/* Runs the real game. chdir()s into the real per-episode directory (see this file's own header
 * comment, point 1) and calls JUNGLE.c's real main() with a minimal synthetic argv -- just the
 * program name, no switches. main() itself parses argv for --validate/--scale=N/--play/a bare level
 * name (none present, so window_scale stays its own default of 3 and validate_only/level stay
 * off/NULL) and then calls CONFIG.c's cfg_getpath(), which only ever assigns cfg_path from a
 * DOS-style "/P<path>" argv entry -- also none present, so cfg_path stays upstream's own default ""
 * and data_path() keeps producing plain relative filenames, which the chdir() below is what makes
 * resolve to the right files.
 *
 * Does not return until the player quits or something fails hard enough to call rexit() (e.g.
 * loadboard()'s own rexit(1) on a level file that doesn't open) -- upstream's own rexit() ends in
 * exit(), which is fine for a real DOS/desktop process but not for a shared library loaded into a
 * long-lived Android app: it was observed tearing down the WHOLE process from this background
 * thread (a destroyed-mutex SIGABRT on the AChoreographer thread) the very first time rexit() ever
 * actually fired on-device. JUNGLE.c's rexit() has its own trailing exit() replaced with a
 * longjmp(jill_quit_jmp, ...) back to the setjmp() right below instead (see that function's own
 * comment) -- every one of its real cleanup calls (savecfg()/snd_exit()/shm_exit()/gc_exit()/
 * gr_exit()/host_close()) still runs first, unchanged; only how control gets back to the caller
 * changes, from "it doesn't" to "an ordinary C return, same as main()'s own handful of early
 * `return N;` branches (host_open()/doconfig() failing, or --validate) already produce". */
static void jill_run_game(const char *sha_path)
{
    char *episode_dir;
    /* A plain writable buffer, NOT a `char *argv0 = "jill";` string-literal pointer -- CONFIG.c's
     * cfg_getpath()/cfg_init() both do jill_strupr(argv[index]) (in-place upper-casing) on every
     * argv entry, including argv[0], exactly matching upstream's own DOS command-line handling.
     * A string literal lives in read-only memory on Android (ELF .rodata); writing into one through
     * toupper() is an immediate SIGSEGV. This was the crash on selecting Episode 1 -- jill_run_game()
     * is the first thing to ever call main() with a synthetic argv, so it's the first place this
     * could have bitten. */
    char argv0[] = "jill";
    char *argv[2];
    int quit_result;

    episode_dir = jill_episode_dir_from_sha_path(sha_path);
    if (episode_dir == NULL) {
        LOGI("jill_run_game: no usable episode directory from sha_path, aborting");
        return;
    }
    if (chdir(episode_dir) != 0) {
        LOGI("jill_run_game: chdir(\"%s\") failed, aborting", episode_dir);
        free(episode_dir);
        return;
    }
    LOGI("jill_run_game: chdir'd into \"%s\"; entering JUNGLE.c's real main()", episode_dir);
    free(episode_dir);

    /* Must happen after the chdir() above (this symlinks *into* the directory just chdir()'d into)
     * and before main() below (JUNGLE.c's jmenu() can reach sb_playtune() for menu music before the
     * player has done anything else) -- see jill_link_shared_ddt_files()'s own comment. */
    jill_link_shared_ddt_files(sha_path);
    /* Same "must happen after chdir(), before main()" timing as jill_link_shared_ddt_files() just
     * above, for the same reason -- main() (JUNGLE.c) calls jill_load_app_config() (display mode,
     * sound/touch/gamepad settings) before jmenu()'s own first frame, via the plain relative
     * "config.dat" data_path() this symlinks into place. See this function's own comment for the
     * whole cross-episode-sharing story. */
    jill_link_shared_config_file(sha_path);

    argv[0] = argv0;
    argv[1] = NULL;

    /* Everything jill_run_game() itself still needs after this point (episode_dir was already
     * freed; argv0/argv are never touched again either) is set before this call, so there's nothing
     * here setjmp()'s usual "don't trust non-volatile locals after a longjmp" caveat applies to. */
    quit_result = setjmp(jill_quit_jmp);
    if (quit_result == 0) {
        (void)main(1, argv);
        LOGI("jill_run_game: main() returned directly (no rexit() call -- e.g. host_open()/"
             "doconfig() failing before the game loop ever started)");
    } else {
        LOGI("jill_run_game: JUNGLE.c's real rexit(%d) unwound back here -- play session over, "
             "returning to Android normally", quit_result);
    }
}

/* --- Gamepad remapping ("Remap Gamepad" pause-menu submenu, JUNGLE.c's pausemenu()) -------------
 *
 * Mirrors the God of Thunder Android port's own got_controls.c "Gamepad remapping" section
 * (wootbeer's own explicit ask: "let's again model after the one we made for god of thunder"), scoped
 * down to Jill's only 2 real remappable gameplay actions -- JUMP (fire1/key_shift) and THROW
 * (fire2/key_alt), JILL_GAMEPAD_ACTION_JUMP/_THROW (JILL.H) -- instead of GoT's 3 (Fire/Magic/
 * Select Item). Same architecture at an even smaller scale: a keyCode<->action table, a raw JNI
 * forwarding channel decoupled from any pre-translation, a cross-thread capture flag pair.
 *
 * JillView.java's own handleKey() no longer hardcodes KEYCODE_BUTTON_A -> K_ALT (THROW) /
 * KEYCODE_BUTTON_B -> K_SHIFT (JUMP) the way it used to -- see that file's own class comment.
 * Instead, every one of the 7 capturable buttons (matching GoT's own pool exactly, wootbeer's own ask:
 * "have all the same gamepad buttons available to be mapped that god of thunder has") forwards its
 * RAW Android keyCode here via jillGamepadButtonRaw() below, and jill_gamepad_dispatch_raw()
 * decides what (if anything) that keycode currently drives by consulting the live binding table,
 * calling jillhost_notify_key(key_shift, down) or jillhost_notify_key(key_alt, down) exactly as if
 * that were still a hardcoded Java switch case -- HOSTANDROID.c's own host_keys[]/host_queue
 * plumbing downstream of that call is completely unchanged; only WHO decides to call it changes.
 *
 * Losing the ability to confirm/back out of a menu if both JUMP and THROW get remapped away from
 * A/B is a real risk this port takes seriously (domenu_at() (JUNGLE.c) has always read fire1/fire2
 * directly for "B backs out"/"A confirms") -- see KEYBOARD.H's own key_menu_confirm/key_menu_cancel
 * comment for the always-on, never-remapped fix, decoupled entirely from this table: JillView.java
 * sends those two codes straight through the ordinary jillKeyEvent() JNI entry below, unconditionally
 * alongside whatever this table does with A/B, so this section has nothing further to do with them. */

/* Mirrors android.view.KeyEvent.KEYCODE_BUTTON_* -- stable public Android API ints, same 7-button
 * pool got_controls.c's own GP_BUTTON_* table already uses (down to the exact same THUMBL-not-
 * THUMBR choice -- see that file's own comment for why). */
#define JILL_GP_BUTTON_A       96
#define JILL_GP_BUTTON_B       97
#define JILL_GP_BUTTON_X       99
#define JILL_GP_BUTTON_Y       100
#define JILL_GP_BUTTON_L1      102
#define JILL_GP_BUTTON_R1      103
#define JILL_GP_BUTTON_THUMBL  106

typedef struct {
    const char *label;
    int hostKeyCode;     /* key_shift or key_alt (KEYBOARD.H) -- never remapped itself, only which
                           * physical button drives it changes. */
    int defaultKeyCode;  /* Compiled-in default physical button -- wootbeer's own explicit ask: "of
                           * course the default controls will be what we have now" -- matches
                           * JillView.java's own former hardcoded A->THROW/B->JUMP mapping
                           * exactly. */
} JillGamepadRemapAction;

/* Real key_shift/key_alt already #defined (KEYBOARD.H) -- reused directly, not redefined a second
 * time, same as got_controls.c's own kGamepadRemapActions[] reuses KEY_FIRE/KEY_MAGIC/KEY_SELECT.
 * Android port addition, not upstream -- the 3rd (Episode 1 only) COIN TOSS row, JILL.H's own
 * JILL_GAMEPAD_ACTION_COIN_TOSS comment has the whole story. key_space (KEYBOARD.H, 32) is real
 * upstream's own already-#if-defined(JILL_EP1)-gated coin-toss trigger (JPLAYER.c's `key == ' '`
 * check) -- never remapped itself, only which physical button now also sends it. Defaults to
 * JILL_GP_BUTTON_X, the one capturable button with no other default binding (JUMP/THROW already
 * claim B/A) -- an otherwise permanently-idle button on a fresh install, same "give every new
 * action a sensible unclaimed default" reasoning JUMP/THROW's own defaults already follow. */
static const JillGamepadRemapAction kJillGamepadRemapActions[JILL_GAMEPAD_NUM_ACTIONS] = {
    { "Jump",  key_shift, JILL_GP_BUTTON_B }, /* fire1 -- was hardcoded to B */
    { "Throw", key_alt,   JILL_GP_BUTTON_A }, /* fire2 -- was hardcoded to A */
#if defined(JILL_EP1)
    { "Coin Toss", key_space, JILL_GP_BUTTON_X },
#endif
};

/* Live, persisted bindings -- consulted every time a gamepad button event arrives via
 * jillGamepadButtonRaw() below. Statically seeded to the defaults so a freshly-launched process
 * (before jill_load_app_config() ever runs, JUNGLE.c) is already correct -- same "correct before
 * any load" precedent got_controls.c's own s_gamepad_bound_keycode[] static initializer already
 * establishes. */
static int s_jill_gamepad_bound_keycode[JILL_GAMEPAD_NUM_ACTIONS] = {
    JILL_GP_BUTTON_B, JILL_GP_BUTTON_A,
#if defined(JILL_EP1)
    JILL_GP_BUTTON_X,
#endif
};

/* Cross-thread state for the Remap Gamepad screen's capture step -- jillGamepadButtonRaw() below
 * lands on whatever thread Android delivers key events on; JUNGLE.c's own per-tick poll
 * (jill_gamepad_capture_button()'s own checkctrl0(0) loop) runs on the same background thread
 * jillMain() itself runs the whole game loop on. Same informal single-writer/single-reader
 * volatile pattern got_controls.c's own s_gamepad_capturing/s_gamepad_captured_keycode already
 * establishes -- no mutex needed. Plain int, not bool -- this file doesn't otherwise use
 * <stdbool.h>, matching HOSTANDROID.c's own byte-flag precedent rather than introducing a new
 * dependency for just this. */
static volatile int s_jill_gamepad_capturing = 0;
static volatile int s_jill_gamepad_captured_keycode = JILL_GAMEPAD_UNBOUND_KEYCODE;

int jill_gamepad_remap_bound_keycode(int action)
{
    if (action < 0 || action >= JILL_GAMEPAD_NUM_ACTIONS) return JILL_GAMEPAD_UNBOUND_KEYCODE;
    return s_jill_gamepad_bound_keycode[action];
}

/* Sets the LIVE binding directly -- called only from JUNGLE.c's own pausemenu() Apply step (after
 * the player confirms a whole batch of staged changes at once, see that submenu's own comment on
 * why this port stages rather than applies immediately like every other submenu there) and from
 * jill_load_app_config()'s own startup config load. Never called mid-capture; the capture flow's
 * own in-progress choices live entirely in pausemenu()'s own local staging array until Apply
 * actually calls this. */
void jill_gamepad_remap_set_bound_keycode(int action, int keycode)
{
    if (action < 0 || action >= JILL_GAMEPAD_NUM_ACTIONS) return;
    s_jill_gamepad_bound_keycode[action] = keycode;
}

int jill_gamepad_remap_default_keycode(int action)
{
    if (action < 0 || action >= JILL_GAMEPAD_NUM_ACTIONS) return JILL_GAMEPAD_UNBOUND_KEYCODE;
    return kJillGamepadRemapActions[action].defaultKeyCode;
}

const char *jill_gamepad_remap_action_label(int action)
{
    if (action < 0 || action >= JILL_GAMEPAD_NUM_ACTIONS) return "";
    return kJillGamepadRemapActions[action].label;
}

/* Short display name for a capturable keycode (or the unbound sentinel) -- JUNGLE.c's own row
 * labels and capture-prompt title use this, same shape got_controls.c's own
 * got_gamepad_remap_button_name() already establishes. */
const char *jill_gamepad_remap_button_name(int keycode)
{
    switch (keycode) {
    case JILL_GP_BUTTON_A:      return "A";
    case JILL_GP_BUTTON_B:      return "B";
    case JILL_GP_BUTTON_X:      return "X";
    case JILL_GP_BUTTON_Y:      return "Y";
    case JILL_GP_BUTTON_L1:     return "L1";
    case JILL_GP_BUTTON_R1:     return "R1";
    case JILL_GP_BUTTON_THUMBL: return "L3";
    default:                    return "---";
    }
}

/* JUNGLE.c's own jill_load_app_config() calls this to validate a keycode loaded from config.dat --
 * only ever a real member of the 7-button capturable pool, or the unbound sentinel, is trusted;
 * anything else (a corrupt/foreign file) falls back to that action's own compiled-in default,
 * matching every other field's own "range check fails, default already in place" precedent in that
 * function. */
int jill_gamepad_remap_is_valid_keycode(int keycode)
{
    return keycode == JILL_GAMEPAD_UNBOUND_KEYCODE ||
           keycode == JILL_GP_BUTTON_A || keycode == JILL_GP_BUTTON_B ||
           keycode == JILL_GP_BUTTON_X || keycode == JILL_GP_BUTTON_Y ||
           keycode == JILL_GP_BUTTON_L1 || keycode == JILL_GP_BUTTON_R1 ||
           keycode == JILL_GP_BUTTON_THUMBL;
}

void jill_gamepad_remap_start_capture(void)
{
    s_jill_gamepad_captured_keycode = JILL_GAMEPAD_UNBOUND_KEYCODE;
    s_jill_gamepad_capturing = 1;
}

void jill_gamepad_remap_cancel_capture(void)
{
    s_jill_gamepad_capturing = 0;
    s_jill_gamepad_captured_keycode = JILL_GAMEPAD_UNBOUND_KEYCODE;
}

/* Read-and-clear poll for JUNGLE.c's own jill_gamepad_capture_button() per-tick capture loop --
 * returns JILL_GAMEPAD_UNBOUND_KEYCODE until a capturable button has actually been pressed since
 * jill_gamepad_remap_start_capture(), then that keycode exactly once. */
int jill_gamepad_remap_poll_capture(void)
{
    int captured = s_jill_gamepad_captured_keycode;
    s_jill_gamepad_captured_keycode = JILL_GAMEPAD_UNBOUND_KEYCODE;
    return captured;
}

/* Called from Java_wootbeer_jillandroid_JillView_jillGamepadButtonRaw() below, once per raw
 * physical-button press/release event for any of the 7 capturable buttons -- see that JNI entry's
 * own comment. While capturing, every press just records itself for
 * jill_gamepad_remap_poll_capture() to pick up on the game thread's own next tick, exactly like
 * got_controls.c's own gamepadButtonRaw() capture branch. Outside capture, dispatched straight to
 * whichever action (if any) currently claims that keycode, via jillhost_notify_key() -- the exact
 * same call HOSTANDROID.c's own real JNI key-event path (Java_..._jillKeyEvent(), just below) makes,
 * so K_SHIFT/K_ALT's own continuous-held-state/one-shot-queue semantics downstream (host_keys[]/
 * host_queue, HOSTANDROID.c) are completely unchanged -- only who decides to call it. */
static void jill_gamepad_dispatch_raw(int key_code, int down)
{
    int i;

    if (s_jill_gamepad_capturing) {
        if (down) s_jill_gamepad_captured_keycode = key_code;
        return;
    }
    for (i = 0; i < JILL_GAMEPAD_NUM_ACTIONS; ++i) {
        if (s_jill_gamepad_bound_keycode[i] == key_code) {
            jillhost_notify_key(kJillGamepadRemapActions[i].hostKeyCode, down);
        }
    }
}

JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillMain(JNIEnv *env, jobject thiz, jint width, jint height,
                                             jstring dma_path, jstring sha_path)
{
    const char *dma_path_chars;
    const char *sha_path_chars;

    jillhost_set_surface_size(width, height);

    /* Before anything below can reach MUSIC.c's snd_init()/snd_do() -- binds this JillView so
     * HOSTANDROID.c's host_audio_* functions can call back into it for real digitized sound-effect
     * playback (see jillhost_bind_audio_view()'s own comment). thiz is no longer unused past this
     * point, unlike every other JNI entry point below. */
    jillhost_bind_audio_view(env, thiz);

    /* dma_path/sha_path are JillGameActivity's own computed absolute paths to the user's jill.dma
     * and the chosen episode's jillN.sha (see JillView.java's own constructor comment) -- either
     * may legitimately be NULL if the Java side ever passes null, though JillGameActivity's own
     * sharedDmaPath()/episodeShaPath() never do today. jillhost_set_dma_path() takes its own
     * strdup'd copy, so releasing dma_path's JNI-owned buffer right after is safe; sha_path_chars is
     * instead passed straight into jill_run_game() (which derives the chdir() target from it, then
     * hands the same string on to JUNGLE.c's real main() -> shm_init(), which itself copies it into
     * its own static buffer before returning) and only released once jill_run_game() has returned. */
    dma_path_chars = (dma_path != NULL) ? (*env)->GetStringUTFChars(env, dma_path, NULL) : NULL;
    jillhost_set_dma_path(dma_path_chars);
    if (dma_path_chars != NULL) (*env)->ReleaseStringUTFChars(env, dma_path, dma_path_chars);

    sha_path_chars = (sha_path != NULL) ? (*env)->GetStringUTFChars(env, sha_path, NULL) : NULL;
    jill_run_game(sha_path_chars);
    if (sha_path_chars != NULL) (*env)->ReleaseStringUTFChars(env, sha_path, sha_path_chars);
}

JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillSurfaceResized(JNIEnv *env, jobject thiz, jint width,
                                                        jint height)
{
    (void)env;
    (void)thiz;
    jillhost_set_surface_size(width, height);
}

JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillKeyEvent(JNIEnv *env, jobject thiz, jint key_code,
                                                 jboolean down)
{
    (void)env;
    (void)thiz;
    jillhost_notify_key(key_code, down ? 1 : 0);
}

/* Android port addition, not upstream -- "Remap Gamepad" submenu's own raw forwarding channel for
 * the 7 capturable buttons (see this file's own "Gamepad remapping" section above, and
 * JillView.java's own class comment/handleKey() cases). key_code here is the ordinary Android
 * KeyEvent.KEYCODE_BUTTON_* constant itself, NOT translated through Jill's own key-code space the
 * way jillKeyEvent() above is -- jill_gamepad_dispatch_raw() is what maps it to a Jill key code
 * (key_shift/key_alt), via the live, player-editable binding table. */
JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillGamepadButtonRaw(JNIEnv *env, jobject thiz, jint key_code,
                                                          jboolean down)
{
    (void)env;
    (void)thiz;
    jill_gamepad_dispatch_raw((int)key_code, down ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillRequestStop(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    jillhost_request_stop();
}

/* Android port addition, not upstream -- see HOSTANDROID.H's own jillhost_request_pause()/
 * jillhost_notify_surface_destroyed() comments for the whole render-thread pause/resume feature
 * (screen lock/unlock leaving a permanently black screen). Both static natives, same reasoning as
 * jillSurfaceResized()/jillKeyEvent() above: each just sets a flag HOSTANDROID.c's own
 * jillhost_handle_pause() reads, needs no JillView/JillGameActivity instance. */
JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillView_jillNotifySurfaceDestroyed(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    jillhost_notify_surface_destroyed();
}

/* Declared on JillGameActivity.java (not JillView.java) -- see that class's own jillPause()
 * comment for why: it's the Activity that owns the onPause()/onResume() lifecycle this whole
 * feature hangs off of. */
JNIEXPORT void JNICALL
Java_wootbeer_jillandroid_JillGameActivity_jillPause(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    jillhost_request_pause();
}

/* Static native (JillView.java's own declaration is `private static native int
 * jillRenderMusic(short[] buf);`, correctly static since this needs no JillView instance at all --
 * see JillView.java's own comment on why this one is static where jillMain() above deliberately
 * isn't) -- called directly and synchronously by JillView.java's own background music-render thread
 * (started by jillStartMusic(), joined by jillStopMusic()), NOT by anything reached from jillMain()'s
 * own JNI call frame. Being Java's own thread calling into native code (rather than native calling
 * back into Java) is exactly why this needs no jill_audio_env()-style attach/detach dance the
 * host_audio_* functions in HOSTANDROID.c all need for their own native-to-Java direction.
 *
 * Pins buf's own backing storage directly (GetShortArrayElements/ReleaseShortArrayElements) rather
 * than bouncing through an intermediate malloc'd buffer -- this runs continuously, once per audio
 * chunk, for as long as any music is playing, so avoiding a heap alloc/free pair on every call
 * actually matters here in a way it wouldn't for a one-shot call like host_audio_play_voc()'s own
 * jbyteArray handling. jint16_t and jshort are the same 16-bit signed type on every real Android ABI,
 * so the (int16_t *) cast below is safe, not just convenient.
 *
 * Returns the number of stereo frames actually rendered into buf (buf's own length in shorts, not
 * frames -- so capacity/2 frames is the most this call can ever produce in one go), which may be
 * less than that when nothing is currently playing (0) or a non-looping song just ended mid-chunk;
 * either way JillView.java's own render loop is written to treat a short return as "nothing more to
 * write this call", not a fatal error -- see that function's own comment. */
JNIEXPORT jint JNICALL
Java_wootbeer_jillandroid_JillView_jillRenderMusic(JNIEnv *env, jclass clazz, jshortArray buf)
{
    jsize capacity;
    size_t num_frames;
    jshort *elems;
    size_t got;

    (void)clazz;
    if (buf == NULL) return 0;

    capacity = (*env)->GetArrayLength(env, buf);
    if (capacity < 2) return 0; /* need room for at least one full stereo frame (L, R) */
    num_frames = (size_t)capacity / 2;

    elems = (*env)->GetShortArrayElements(env, buf, NULL);
    if (elems == NULL) return 0;

    got = jillhost_render_music((int16_t *)elems, num_frames);

    (*env)->ReleaseShortArrayElements(env, buf, elems, 0); /* 0: copy back / commit, don't just free */
    return (jint)got;
}

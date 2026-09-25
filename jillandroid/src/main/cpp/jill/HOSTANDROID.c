/*
 * HOSTANDROID.c -- Android implementation of Jill of the Jungle Reconstructed's HOSTSDL.H /
 * HOSTAUDIO.H contract (see those headers, vendored unchanged from the decomp, for the real
 * API this file implements).
 *
 * Ground rule for this file: it is the Android equivalent of the decomp's own HOSTSDL.C, and it
 * should stay swappable with it -- anything that calls host_open()/host_present()/host_key_*()
 * etc. (the real Jill game code, once it's wired in) should work against this file completely
 * unchanged, the same way JILL_SOURCES currently lists HOSTSDL.C for desktop builds and would
 * list this file instead for the Android build.
 *
 * Where this genuinely differs from HOSTSDL.C, beyond swapping SDL2 calls for GLES/JNI ones:
 *  - There's no window to create -- host_open()'s `scale` argument is accepted (for signature
 *    compatibility) but unused; the actual on-screen size comes from Android's own Surface via
 *    jillhost_set_surface_size() (HOSTANDROID.H), called separately by the Java side.
 *  - host_pump() now calls gr_present_page() on every invocation, matching HOSTSDL.C's own
 *    documented behavior (this file used to skip it -- see host_pump()'s own comment for the
 *    real bug that turned up once JUNGLE.C, the real game loop, was actually ported and this
 *    got exercised for real: JUNGLE.C's domenu() (the menu cursor animation) and every other
 *    idle-wait screen built the same way -- loadsavewin(), pageview(), askquit(), etc. -- draw
 *    straight onto the CURRENTLY DISPLAYED page every tick with no pageflip()/refresh() call of
 *    their own, relying on real VGA hardware showing written pixels instantly with no separate
 *    "present" step. Android's jill_video buffer has no such hardware; nothing reached the
 *    screen from those loops until gr_present_page() actually ran, and outside play()'s own
 *    refresh(pagemode) -> pageflip() the game loop never called it on its own. wootbeer found this
 *    as an invisible, unusable main-menu selection cursor -- host_pump() already runs on every
 *    checkctrl()/k_pressed() tick throughout the whole game (that's the one thing every one of
 *    these loops has in common), so hooking the present call in here covers all of them at once,
 *    with zero changes to JUNGLE.C or any of its own draw logic.
 *  - The palette->RGB conversion and GL texture upload/quad-draw are a smaller version of what
 *    modex/modexgl.c already proved out for GoT (same technique: one power-of-2 GL_RGB texture,
 *    glTexSubImage2D per frame, a single textured GL_TRIANGLE_STRIP quad, GLES 1.1 fixed
 *    function) -- Jill doesn't need modex.h's much larger xfput/xget/mirror-mode/display-mode
 *    API on top of that, since GR.C (once wired in) already does its own drawing into a plain
 *    byte buffer; this file only needs the "get that finished buffer on screen" step.
 *
 * Audio (host_audio_* below): digitized sound effects (MUSIC.c's own VOC path, snd_play()) are
 * real -- played on the Java side via JillView.java's own single reused AudioTrack, reached through
 * a small native->Java JNI callback bound once by jillhost_bind_audio_view() (see that function's
 * own comment, and host_audio_play_voc()'s own comment for how a real VOC block gets from here to
 * there). Real-time AdLib/CMF music (MUSIC.c's sb_playtune() path) is now real too, the same general
 * shape: descore/audio_opl/descore_cmf.c (a real CMF sequencer driving descore/audio_opl/opl3.c's
 * Nuked OPL3 -- see host_audio_play_cmf()'s own comment for why that library/approach was chosen,
 * over both the GoT port's own offline-rendered-audio-file approach -- not viable here, Jill's real
 * music isn't freely redistributable the way GoT's was -- and the other Jill of the Jungle source
 * ports wootbeer had on hand, neither of which had solved this either) produces real PCM continuously,
 * streamed out through a second AudioTrack (JillView.java's own MODE_STREAM one, pulled from a
 * dedicated Java background thread that calls back into native via jillRenderMusic() -- see that
 * JNI entry point's own comment, jill_jni_bridge.c). */

#include "HOSTSDL.H"
#include "HOSTAUDIO.H"
#include "HOSTANDROID.H"
#include "GR.H"
#include "KEYBOARD.H"
#include "jill_controls.h"
#include "../descore/audio_opl/descore_cmf.h"

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "jillhost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define HOST_KEY_CAPACITY 256
#define HOST_QUEUE_CAPACITY 64

/* Android port addition, not upstream -- Sound/Music Options submenu (JUNGLE.c's pausemenu(),
 * MUSIC.H's own jill_sound_gain/jill_music_gain comment has the whole feature). Narrow local
 * externs rather than #include "MUSIC.H" -- this file's own header comment already draws a
 * deliberate line between it and the real game engine ("the Android equivalent of the decomp's own
 * HOSTSDL.C... should stay swappable with it"), so it reaches into MUSIC.c's globals the same
 * minimal way it already reaches into RECOVERY.H's jill_dma_path rather than pulling in JILL.H's
 * whole shared-engine surface for two word globals. JILL_SOUND_GAIN_STEPS must stay numerically in
 * sync with MUSIC.H's own identical #define -- see that file's own comment. */
extern word jill_sound_gain, jill_music_gain;
#define JILL_HOST_SOUND_GAIN_STEPS 10

/* modexgl.c rounds GoT's own canvas up to the next power of two for GLES 1.1, which has no
 * guaranteed non-power-of-two texture support -- same reasoning here. 512x256 comfortably
 * covers Jill's fixed 320x200. */
#define JILLGL_TEX_W 512
#define JILLGL_TEX_H 256

static volatile int host_opened;
static volatile int host_stop_requested;
/* Android port addition, not upstream -- see HOSTANDROID.H's own jillhost_request_pause()/
 * jillhost_notify_surface_destroyed() comments for the whole feature (screen lock/unlock leaving
 * a permanently black screen). Both volatile for the same cross-thread reason host_opened/
 * host_stop_requested above already are: set from JillView.java's/JillGameActivity.java's own
 * UI-thread lifecycle callbacks, read from host_pump() on the native render thread. */
static volatile int host_want_pause;
static volatile int host_surface_was_destroyed;
static int host_surface_width = 1, host_surface_height = 1;
/* Android port addition, not upstream -- set by JUNGLE.c's own jill_apply_display_mode() via
 * host_set_display_mode() (HOSTSDL.H) below, one of JUNGLE.c's own JILL_DISPLAY_DEFAULT(0)/
 * JILL_DISPLAY_FORCE43(1)/JILL_DISPLAY_MINIMAL(2). Only host_present() below reads it (for Force
 * 4:3's own pillarbox math) -- Default and Minimal UI don't change anything in this file, see
 * host_set_display_mode()'s own comment. */
static int host_display_mode;
/* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_enabled()/
 * host_set_mirror_rect()/host_set_mirror_suspended() comment for the whole Mirror Mode feature.
 * Declared up here (not just above host_present(), where they're actually read) specifically so
 * host_open() below can reset host_mirror_suspended -- see that function's own comment on why.
 * host_mirror_x1/y1/x2/y2 default to all-zero, which host_present() can never mistake for a real
 * rect to mirror since host_mirror_enabled also defaults to 0 (JILL_MIRROR_OFF) -- the first real
 * geometry always arrives from JUNGLE.c's own jill_activate_display_mode() well before any frame
 * that could have host_mirror_enabled already set. */
static int host_mirror_enabled;
static int host_mirror_suspended;
static int host_mirror_x1, host_mirror_y1, host_mirror_x2, host_mirror_y2;
/* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_exclude_rect()
 * comment for the whole "MAP"/"DEMO" banner feature. host_mirror_exclude_active defaults to 0
 * (inactive), the correct default until JUNGLE.c/JOBJ2.c ever push real geometry. */
static int host_mirror_exclude_active;
static int host_mirror_exclude_x1, host_mirror_exclude_y1, host_mirror_exclude_x2,
           host_mirror_exclude_y2;
/* Android port addition, not upstream -- see HOSTSDL.H's own host_set_mirror_hud_exclude_rect()
 * comment for why this is a THIRD, independent exclude rect rather than reusing the transient one
 * right above. host_mirror_hud_exclude_active defaults to 0 (inactive), correct until Minimal UI
 * mode is actually entered. */
static int host_mirror_hud_exclude_active;
static int host_mirror_hud_exclude_x1, host_mirror_hud_exclude_y1, host_mirror_hud_exclude_x2,
           host_mirror_hud_exclude_y2;
/* Android port addition, not upstream -- see host_set_mirror_exclude_rect()'s own comment, below,
 * for the whole "shadow duplicate" story (wootbeer's own reports: the pause menu "duplicating
 * underneath the main menu", then, once that got fixed at the JUNGLE.c level, "gamepad and sound
 * sound menus actually mirror under themselves"). A FOURTH, independent exclude rect, same shape as
 * host_mirror_hud_exclude_* just above -- this one is computed automatically, entirely inside
 * host_set_mirror_exclude_rect() itself, from whatever rect that function is given; no caller ever
 * sets it directly. host_mirror_exclude_shadow_active defaults to 0 (inactive), correct until the
 * first real host_set_mirror_exclude_rect(1, ...) call ever runs. */
static int host_mirror_exclude_shadow_active;
static int host_mirror_exclude_shadow_x1, host_mirror_exclude_shadow_y1,
           host_mirror_exclude_shadow_x2, host_mirror_exclude_shadow_y2;
/* Android port addition, not upstream, this round -- wootbeer's own report: "when turning mirror mode
 * on and off, the enhancements sub menu duplicates and sticks out from under itself." Root cause:
 * ENHANCEMENTS (and CHEATS, and every other domenu_at()-based submenu with its own toggle-and-
 * redraw for(;;) loop, JUNGLE.c) calls domenu_at() completely fresh every time a row changes --
 * necessary so the new value actually appears on screen (that function's own comment has the whole
 * story) -- and domenu_at() itself unconditionally calls host_set_mirror_exclude_rect(1, ...) again
 * on every one of those fresh calls, right before its own drawwin() redraws the SAME window box on
 * top of whatever's already there. host_set_mirror_exclude_rect()'s own shadow mechanism (below)
 * was built assuming the capture only ever happens ONCE per window, before its first pixel is ever
 * drawn (true for every OTHER caller -- dotextmsg(), jill_gamepad_capture_button() -- which really
 * do only call it once) -- but for one of these redraw loops, every call after the very first one
 * captures whatever's CURRENTLY in that window's own rect, which by then is this SAME window's own
 * last-drawn text/border from the PREVIOUS redraw, not real clean board. Pre-flipping that into the
 * shadow reproduces a mirrored copy of the menu's own last frame right next to itself, worse each
 * additional redraw (each capture can itself include the previous redraw's own pre-flipped shadow
 * pixels, if the window and its shadow overlap, compounding) -- exactly wootbeer's "duplicates and
 * sticks out from under itself."
 *
 * Fixed by caching the clean snapshot itself, keyed on the exact window rect, and only ever taking
 * a fresh one when that rect isn't the most recently cached one -- every later host_set_mirror_
 * exclude_rect(1, ...) call for the SAME still-open window (same rect) reuses the cached pixels as
 * its source instead of re-reading jill_video, so the shadow always reflects the one truly clean
 * snapshot taken the first time this window opened, no matter how many times its own for(;;) loop
 * redraws it in between. Still writes into whichever of the two video pages is CURRENTLY *pagedraw*
 * on every call (never skipped outright) -- domenu_at()'s own setpagemode(1)/pageflip() dance flips
 * that page on every single call, so the shadow needs re-writing into the new page each time even
 * when the SOURCE data being written is just the cached copy, not a fresh read.
 *
 * host_mirror_snapshot_valid is invalidated from exactly one place -- host_set_mirror_rect() below,
 * not host_set_mirror_exclude_rect()'s own (0, 0, 0, 0, 0) "close" calls domenu_at() makes on every
 * single return (including between two redraws of the very same still-open window, which is exactly
 * the case this cache needs to survive). host_set_mirror_rect() only ever runs at the boundaries
 * where real gameplay is about to (re)gain control -- play()'s own key_escape/'Q'/key_pause cases,
 * jill_activate_display_mode(), and this function's own header comment for the rest -- never while
 * still inside a single modal menu's own redraw loop, so it's exactly the "the frozen world this
 * snapshot was taken from might not be the SAME frozen world any more" signal this cache actually
 * needs, and nothing narrower (comparing WHICH window/submenu is asking would be wrong besides --
 * every domenu_at() screen in a single pause-menu visit sits over the exact same frozen frame
 * regardless of which submenu is open, so reusing one submenu's own cached snapshot for a
 * different, same-rect submenu later in that SAME visit is still completely correct, not just
 * close enough).
 *
 * host_set_mirror_exclude_rect()'s own capture-or-reuse step (below) runs on EVERY active call,
 * Mirror on or off -- NOT only while Mirror happens to be enabled, despite this cache existing
 * purely to serve Mirror's own shadow fill. See that function's own "Third round fix" comment for
 * why gating the capture on host_mirror_enabled (this cache's first version) was itself the bug:
 * opening a menu with Mirror off, then toggling it on from inside, needs THAT already-open window's
 * ORIGINAL entry-time snapshot, taken back when Mirror was still off -- not a fresh one taken at
 * toggle time, by which point the window has already drawn over its own true clean background at
 * least once. */
static int host_mirror_snapshot_valid;
static int host_mirror_snapshot_x1, host_mirror_snapshot_y1, host_mirror_snapshot_x2,
           host_mirror_snapshot_y2;
static byte host_mirror_snapshot[JILL_SCREEN_HEIGHT][JILL_SCREEN_WIDTH];
static byte host_keys[HOST_KEY_CAPACITY];
static int host_queue[HOST_QUEUE_CAPACITY];
static unsigned host_queue_read, host_queue_write;

static GLuint jillgl_texture;
/* Tightly packed at JILL_SCREEN_WIDTH (320), NOT JILLGL_TEX_W (512) -- see host_present()'s own
 * comment on why: GLES 1.1 has no GL_UNPACK_ROW_LENGTH, so glTexSubImage2D always reads its
 * source as tightly packed at the WIDTH ARGUMENT PASSED TO IT (320), never at the destination
 * texture's own width (512). This buffer previously used JILLGL_TEX_W*3-byte rows to match the
 * texture's allocation size, which was the bug: glTexSubImage2D read it assuming 320*3-byte rows
 * instead, so every row after the first was read from the wrong offset -- a 576-byte-per-row
 * skew that wraps around the 512-wide buffer every few rows, smearing/duplicating the real
 * pixel data at wrapped offsets. This is what wootbeer saw as multiple periodic copies of Jill's
 * sprite even though the CPU-side jill_video buffer (GR.c) only ever had one. */
static unsigned char jillgl_rgb[JILL_SCREEN_WIDTH * JILL_SCREEN_HEIGHT * 3];

/* --- host_* (HOSTSDL.H) ---------------------------------------------------------------------- */

/* Forward declaration -- defined much further down, alongside the jill_audio_view JNI plumbing it
 * reuses (see jillhost_bind_audio_view()'s own comment for why "audio view" really just means "the
 * JillView instance") -- host_close() just below needs to call it well before that point in the
 * file. Android port addition, not upstream -- see this function's own comment for the whole
 * feature. */
static void jillhost_request_app_exit(void);

int host_open(const char *title, int scale)
{
    (void)title;
    (void)scale;
    if (host_opened) return 1;
    host_clear_keys();
    host_stop_requested = 0;
    host_opened = 1;
    host_start_clock();
    /* Android port addition, not upstream -- defensive reset for Mirror Mode's own suspend latch
     * (see HOSTSDL.H's own host_set_mirror_suspended() comment). Normally this always gets paired
     * back to 0 by whichever of play()'s own key_escape/'Q'/key_pause cases set it to 1 (JUNGLE.c) --
     * but pausemenu()'s own QUIT TO DOS choice calls rexit(0) from *inside* that same suspended
     * window, which longjmp()s straight back to jill_jni_bridge.c's own jill_run_game() and never
     * runs the rest of that case at all, skipping the paired host_set_mirror_suspended(0). This
     * process's own static host_mirror_suspended would otherwise stay stuck at 1 for the rest of
     * this run, silently disabling Mirror Mode for every subsequent play session even after the
     * player picks a fresh episode/level from the main menu -- host_open() runs at the start of
     * each of those, so resetting here guarantees a clean slate regardless of how the previous
     * session ended. */
    host_mirror_suspended = 0;
    LOGI("host_open: opened (Android surface-driven, no window of its own)");
    return 1;
}

void host_close(void)
{
    host_stop_clock();
    host_opened = 0;
    host_clear_keys();
    LOGI("host_close");
    /* Android port addition, not upstream -- host_close() is the one place every real quit path
     * funnels through: JUNGLE.c's real rexit() (Quit to DOS from the pause menu, plus every hard
     * failure path that calls it) calls it directly, and main()'s own trailing cleanup (reached
     * once jmenu() itself returns -- the main menu's own QUIT, Escape, or 'Q') calls it too, right
     * alongside main()'s two early doconfig()/host_open() failure returns. Before this, none of
     * those actually left the app: they just ended this thread (jillMain() returning), leaving
     * JillGameActivity on screen showing a frozen last frame with no way out (its own
     * onBackPressed() is a deliberate no-op, see that method's own comment) -- wootbeer's own report:
     * "when I select quit on the main menu can we make it quit the app completely?" Every one of
     * those paths already reaches here, so one call closes all of them at once. */
    jillhost_request_app_exit();
}

int host_is_open(void)
{
    return host_opened && !host_stop_requested;
}

/* Forward declaration -- defined further down, alongside the other jill_audio_view/jill_mid_*
 * JNI plumbing it reuses (see that section's own comments), but host_pump() below (this file's
 * one shared per-tick checkpoint, see its own comment) needs to call it well before that point in
 * the file. */
static void jillhost_handle_pause(void);

int host_pump(void)
{
    /* Real input arrives asynchronously via jillhost_notify_key(), called from JillView.java's
     * own key event handlers on the UI thread -- there's no event queue to poll here the way
     * HOSTSDL.C's host_pump() polls SDL's. It DOES now call gr_present_page() every time it
     * runs, same as HOSTSDL.C's own host_pump() -- see this file's header comment for the real,
     * on-device bug this fixes: every idle-wait loop that draws straight onto the displayed page
     * without its own pageflip() (JUNGLE.C's domenu() menu cursor chief among them, but also
     * loadsavewin(), pageview(), askquit(), ...) counts on real VGA hardware showing those pixels
     * immediately, which Android's own jill_video buffer never did on its own. host_pump() is
     * called from k_pressed()/checkctrl() on every one of those loops' own per-tick clock wait,
     * so presenting here covers all of them for free. During real-time gameplay this means the
     * frame gets presented twice a tick -- once here (near the top of play()'s loop, via
     * checkctrl(1)'s own k_pressed() call, re-presenting the previous tick's already-shown frame)
     * and once for real at the bottom via refresh(pagemode)'s pageflip() -- but the first of those
     * two is just re-uploading an unchanged frame, a harmless no-op redraw, not a visible glitch
     * or a second real frame.
     *
     * Android port addition, not upstream -- checked first, before presenting: this is the same
     * once-per-tick checkpoint jillhost_handle_pause() (below) needs for the render-thread pause/
     * resume handshake (see HOSTANDROID.H's own jillhost_request_pause() comment for the whole
     * feature), so it piggybacks here rather than needing a checkpoint of its own. Deliberately
     * before gr_present_page(): while host_want_pause is being handled, this thread's EGL context/
     * surface may be mid-teardown-and-rebuild (a destroyed Surface, see jillhost_handle_pause()'s
     * own comment), so presenting a frame has to wait until that's resolved one way or the other. */
    if (host_want_pause) {
        jillhost_handle_pause();
    }
    gr_present_page();
    return host_is_open();
}

static void jillgl_ensure_texture(void)
{
    if (jillgl_texture != 0) return;
    glGenTextures(1, &jillgl_texture);
    glBindTexture(GL_TEXTURE_2D, jillgl_texture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Allocate the full power-of-two texture storage (512x256) without initializing it from
     * jillgl_rgb -- that buffer is now tightly packed at JILL_SCREEN_WIDTH*JILL_SCREEN_HEIGHT
     * (320x200, see its own comment above), not JILLGL_TEX_W*JILLGL_TEX_H, so it's the wrong
     * size to hand to a full-texture glTexImage2D call. Passing NULL just leaves the texture's
     * initial contents undefined outside the 320x200 region host_present() uploads into and the
     * quad ever samples (texcoords run 0..u,0..v where u=JILL_SCREEN_WIDTH/JILLGL_TEX_W), so
     * that's fine -- every visible pixel is written by the real glTexSubImage2D upload below
     * before anything is ever drawn.
     */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, JILLGL_TEX_W, JILLGL_TEX_H, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, NULL);
}

void host_present(const byte *pixels, const byte *palette)
{
    int x, y;
    float u = (float)JILL_SCREEN_WIDTH / (float)JILLGL_TEX_W;
    float v = (float)JILL_SCREEN_HEIGHT / (float)JILLGL_TEX_H;
    /* half_x/half_y default to the full -1..1 clip-space quad (today's existing Default/Minimal
     * UI behavior, stretched to fill the Surface) -- Force 4:3 below (host_display_mode == 1)
     * shrinks whichever axis needs it before vertices[] is filled in. */
    float half_x = 1.0f, half_y = 1.0f;
    GLfloat vertices[8];
    GLfloat texcoords[8] = {0.0f, v,  u, v,  0.0f, 0.0f,  u, 0.0f};

    if (!host_opened || pixels == NULL || palette == NULL) return;

    /* Same palette->RGB conversion HOSTSDL.C's own host_present() uses: the VGA DAC's native
     * range is 6 bits per channel (0-63), scaled up to a full 0-255 byte here.
     *
     * Android port addition, not upstream -- Mirror Mode's own horizontal pixel flip lives right
     * here, in this same per-pixel loop, rather than as a separate pass: mirror_row/mirror_col just
     * pick a different SOURCE column (the source row's own mirrored column within
     * host_mirror_x1..host_mirror_x2, else the normal x) before the exact same palette lookup below
     * runs -- no separate buffer, no second loop, and the DAC conversion itself is identical either
     * way. See HOSTSDL.H's own host_set_mirror_enabled()/host_set_mirror_rect()/
     * host_set_mirror_suspended() comment for the whole feature and why suspended fully disables
     * this rather than trying to exclude a sub-rect the way GoT's own modexgl.c does. exclude_row/
     * in_exclude below are the ONE deliberate exception to that -- see host_set_mirror_exclude_rect()'s
     * own comment (HOSTSDL.H) for why the "MAP"/"DEMO" banner specifically needs a real nested
     * exclude rect rather than just living outside host_mirror_x1..x2/y1..y2 the way CONTROLS/
     * INVENTORY and the pause menu do. hud_exclude_row/in_hud_exclude below are a THIRD, independent
     * check against host_set_mirror_hud_exclude_rect()'s own rect (HOSTSDL.H) -- see that function's
     * comment for why Minimal UI's HUD strip needs its own persistent exclusion separate from the
     * transient one above, and why both can be active on the same frame.
     *
     * Android port note, not upstream -- a round of this fix tried also excluding, from the mirror,
     * any destination column whose own mirror SOURCE fell inside one of these same exclude rects
     * (the idea: stop excluded content from being reproduced, backwards, wherever some OTHER column
     * happened to mirror from it). That traded one bug for a worse one: when an exclude rect is a
     * large fraction of the mirror rect's own width -- the pause menu box is roughly 160px wide
     * inside Default mode's own ~232px gamevp, for instance -- the excluded region and its own
     * "shadow" (same width, reflected) overlap so heavily that their union eats nearly the WHOLE
     * mirror rect, leaving almost nothing actually mirrored at all (wootbeer's own report: "that whole
     * band is screwed up now again... what we just had before that last prompt was almost fine").
     * It also didn't even fix the thing it was meant to fix (the "MAP"/"DEMO" banner still showed
     * duplicated) -- reverted back to the simpler, original destination-only exclusion below while
     * that's investigated properly, rather than leaving a change in that makes one real bug worse
     * without fixing the other.
     *
     * Android port note, not upstream, this round -- shadow_exclude_row/in_shadow_exclude below are
     * a FOURTH check, against host_mirror_exclude_shadow_*'s own rect (declared up top, alongside
     * host_mirror_hud_exclude_*) -- see host_set_mirror_exclude_rect()'s own comment for the whole
     * "shadow duplicate" mechanism this is the read side of: that function precomputes a correctly-
     * mirrored-looking fill for this window's own shadow rect and leaves it sitting in `pixels`
     * itself, so this loop's job is simply to also treat that rect as direct/unmirrored pass-through
     * (exactly like in_exclude/in_hud_exclude already do for their own rects), same as every other
     * excluded pixel here -- the precomputed fill IS the correct final color already, nothing more
     * to compute. */
    for (y = 0; y < JILL_SCREEN_HEIGHT; ++y) {
        const byte *src_row = pixels + (size_t)y * JILL_SCREEN_WIDTH;
        unsigned char *dst_row = jillgl_rgb + (size_t)y * JILL_SCREEN_WIDTH * 3;
        int mirror_row = host_mirror_enabled && !host_mirror_suspended &&
                          y >= host_mirror_y1 && y < host_mirror_y2;
        int exclude_row = host_mirror_exclude_active &&
                           y >= host_mirror_exclude_y1 && y < host_mirror_exclude_y2;
        int hud_exclude_row = host_mirror_hud_exclude_active &&
                               y >= host_mirror_hud_exclude_y1 && y < host_mirror_hud_exclude_y2;
        int shadow_exclude_row = host_mirror_exclude_shadow_active &&
                                  y >= host_mirror_exclude_shadow_y1 &&
                                  y < host_mirror_exclude_shadow_y2;
        for (x = 0; x < JILL_SCREEN_WIDTH; ++x) {
            unsigned color;
            int in_exclude = exclude_row && x >= host_mirror_exclude_x1 &&
                              x < host_mirror_exclude_x2;
            int in_hud_exclude = hud_exclude_row && x >= host_mirror_hud_exclude_x1 &&
                                  x < host_mirror_hud_exclude_x2;
            int in_shadow_exclude = shadow_exclude_row && x >= host_mirror_exclude_shadow_x1 &&
                                     x < host_mirror_exclude_shadow_x2;
            if (mirror_row && !in_exclude && !in_hud_exclude && !in_shadow_exclude &&
                x >= host_mirror_x1 && x < host_mirror_x2) {
                int mirror_col = host_mirror_x1 + (host_mirror_x2 - 1 - x);
                color = src_row[mirror_col];
            } else {
                color = src_row[x];
            }
            dst_row[x * 3 + 0] = (unsigned char)((unsigned)palette[color * 3 + 0] * 255U / 63U);
            dst_row[x * 3 + 1] = (unsigned char)((unsigned)palette[color * 3 + 1] * 255U / 63U);
            dst_row[x * 3 + 2] = (unsigned char)((unsigned)palette[color * 3 + 2] * 255U / 63U);
        }
    }

    jillgl_ensure_texture();
    glViewport(0, 0, host_surface_width, host_surface_height);

    if (host_display_mode == 1 /* JILL_DISPLAY_FORCE43, JUNGLE.c */) {
        /* Force 4:3 -- Android port addition, not upstream. Same technique GoT's own modexgl.c
         * uses for its own Force 4:3 mode: glViewport above stays full-surface, only the
         * presentation quad's clip-space extent shrinks/centers to the target aspect below, and
         * everything outside it is cleared to black every frame -- this Surface isn't guaranteed
         * EGL_BUFFER_PRESERVED, so last frame's now-stale margin pixels can't be trusted to still
         * be there otherwise. 4.0f/3.0f (not JILL_SCREEN_WIDTH/JILL_SCREEN_HEIGHT's own raw
         * 320:200 = 8:5) is deliberate: real 320x200 VGA "Mode 13h" was always displayed on CRT
         * hardware with non-square pixels that stretched the picture to a 4:3 shape, and that's
         * the corrected aspect Force 4:3 is restoring, not the buffer's own raw square-pixel one. */
        float target_aspect = 4.0f / 3.0f;
        float surface_aspect = (float)host_surface_width / (float)host_surface_height;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (surface_aspect > target_aspect) {
            half_x = target_aspect / surface_aspect; /* wider than 4:3 -- pillarbox (shrink X) */
        } else {
            half_y = surface_aspect / target_aspect; /* taller than 4:3 -- letterbox (shrink Y) */
        }
    }
    vertices[0] = -half_x; vertices[1] = -half_y;
    vertices[2] =  half_x; vertices[3] = -half_y;
    vertices[4] = -half_x; vertices[5] =  half_y;
    vertices[6] =  half_x; vertices[7] =  half_y;

    glBindTexture(GL_TEXTURE_2D, jillgl_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, JILL_SCREEN_WIDTH, JILL_SCREEN_HEIGHT, GL_RGB,
                     GL_UNSIGNED_BYTE, jillgl_rgb);

    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);

    /* Android port note, not upstream -- Minimal UI used to draw a translucent black backing bar
     * here, behind the top HUD strip (a screen-space alpha-blended GL quad, drawn after the main
     * textured quad above -- see this function's own git history / JUNGLE.c's jill_draw_minimal_hud()
     * for the two rounds of feedback that shaped it: "can we get rid of all the gray at the top" and
     * then "add a transparent backing... so they are easier to read"). wootbeer's own follow-up ask
     * removed it again: "can we just get rid of the transparent gray thing under the gui? can see
     * the gui elements fine without it" -- the HUD text/icons (JUNGLE.c) already draw with
     * fontcolor()'s own back=-1 transparent convention, so nothing here needs its own backing for
     * legibility. host_display_mode is still tracked and still switches Force 4:3's own pillarbox/
     * letterbox above; only this one now-unwanted draw call is gone. */

    /* The frame above is drawn into the back buffer -- without this it never reaches the screen
     * (host_open()/initEgl() never swap either, so nothing at all would ever be presented; a
     * black screen is exactly what an un-swapped, freshly-cleared Surface looks like). Uses the
     * NATIVE EGL API against whatever context/surface is already current on this thread, not a
     * display/surface handle passed down from Java -- JillView.java's initEgl() (a Java-side
     * javax.microedition.khronos.egl.EGL10 call) made a context current on this same render
     * thread before jillMain() (and so host_open()/this function) was ever called, and Android's
     * Java EGL10 wraps the exact same underlying native EGL implementation, so
     * eglGetCurrentDisplay()/eglGetCurrentSurface() here see that same context. Same technique
     * got_main.c's own got_show_render_buffer() uses for GoT's own eglSwapBuffers() call. */

    /* Android port addition, not upstream -- on-screen touch D-pad/JUMP/THROW/PAUSE buttons, drawn
     * as a separate screen-space GL overlay on top of the game frame just uploaded above, before the
     * swap -- same placement got_main.c's own render loop uses for got_draw_touch_buttons() (that
     * call site's own comment: "drawn as a separate screen-space GL overlay on top of whatever
     * modex_present_frame() just drew... before eglSwapBuffers()"). No-ops (draws nothing) while a
     * real gamepad is connected or before the first jillhost_set_surface_size() call has run -- see
     * jill_controls.c's own jill_draw_touch_buttons() comment. */
    jill_draw_touch_buttons();

    eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));
}

void host_set_title(const char *title)
{
    (void)title; /* No window titlebar on Android. */
}

/* Android port addition, not upstream -- see HOSTSDL.H's own comment for the whole feature. Just
 * latches the value for host_present() above to read on its very next call -- no GL/EGL state to
 * touch here, and this can be (and is, see JUNGLE.c's own jill_apply_display_mode()) called from
 * the same render thread host_present() itself always runs on, so no cross-thread synchronization
 * is needed either. */
void host_set_display_mode(int mode)
{
    host_display_mode = mode;
}

/* Android port addition, not upstream -- see HOSTSDL.H's own comment for the whole Mirror Mode
 * feature. Same "just latch a value for host_present() to read on its next call" shape as
 * host_set_display_mode() just above -- no GL/EGL state to touch, always called from the same
 * render thread host_present() itself runs on. */
void host_set_mirror_enabled(int enabled)
{
    host_mirror_enabled = enabled;
}

void host_set_mirror_rect(int x1, int y1, int x2, int y2)
{
    host_mirror_x1 = x1;
    host_mirror_y1 = y1;
    host_mirror_x2 = x2;
    host_mirror_y2 = y2;
    /* Android port addition, not upstream, this round -- see host_mirror_snapshot_valid's own
     * comment (up top) for the whole story. Every call here means real control is about to (re)
     * start, so whatever this cache was holding belongs to a frozen frame that's now stale -- the
     * next window to ask for shadow protection needs a fresh capture, not this visit's leftovers. */
    host_mirror_snapshot_valid = 0;
}

void host_set_mirror_suspended(int suspended)
{
    host_mirror_suspended = suspended;
}

/* Android port addition, not upstream -- see HOSTSDL.H's own comment for the whole "MAP"/"DEMO"
 * banner feature. Same "just latch a value for host_present() to read on its next call" shape as
 * every other mirror setter above -- PLUS, this round, the "shadow duplicate" fix host_present()'s
 * own shadow_exclude_row/in_shadow_exclude comment points back to here.
 *
 * Android port addition, not upstream, this round -- the shadow fix itself. wootbeer's own reports,
 * across two rounds: first the top-level pause menu "duplicating underneath" whichever submenu was
 * open (fixed in JUNGLE.c/pausemenu(), by hiding the top-level menu's own box before any submenu
 * draws over it), then, once that no longer masked it, "with mirror mode on gamepad and sound sound
 * menus actually mirror under themselves to the left side now when I open them" -- a DIFFERENT bug,
 * present in every one of these windows all along, just most visible on the two widest ones
 * (window_width=13, almost as wide as *gamevp itself in Default mode). Root cause: excluding [x1,x2)
 * from the mirror (the plain `active` logic just below, unchanged) stops THIS rect from being
 * mirrored, but every OTHER, still-mirrored destination column whose own mirror SOURCE happens to
 * fall inside [x1,x2) still reads those real pixels and reproduces them, backwards, wherever that
 * lookup lands -- this rect's own mirror-symmetric "shadow", at
 * [host_mirror_x1+host_mirror_x2-x2, host_mirror_x1+host_mirror_x2-x1) on the same rows. A prior
 * round tried fixing this the obvious way -- ALSO excluding the shadow, showing its own true,
 * unmirrored content there instead of a backwards copy of the window -- and that traded one bug for
 * a worse one (HOSTANDROID.c's own host_present() comment, further up, has wootbeer's "that whole band
 * is screwed up" report): the shadow is ordinary, real, SUPPOSED-to-be-mirrored background, so
 * showing it raw looks just as wrong, a glaring un-mirrored patch amid otherwise-mirrored terrain.
 *
 * The fix here does the same exclusion, but fills the shadow with a correctly-MIRRORED-LOOKING copy
 * of the background first, rather than leaving it showing raw: since every caller of this function
 * now pushes its rect BEFORE drawing anything into it (domenu_at()/dotextmsg()/
 * jill_gamepad_capture_button()'s own comments each explain why they moved), whatever's in `pixels`
 * at [x1,x2) right now is still clean, undrawn-over background -- captured below into `capture`, one
 * row at a time, then written into the shadow rect with each row's OWN column order reversed (a
 * manual pre-flip). Excluding the shadow rect too (host_present()'s own in_shadow_exclude) then
 * shows that pre-flipped copy completely unmirrored, i.e. exactly as-is -- which, being pre-flipped,
 * reads as an ordinary correctly-mirrored patch of background, indistinguishable from the real
 * mirroring happening right around it. Every one of these windows sits over a completely FROZEN
 * game world for its own entire on-screen lifetime (domenu_at()/dotextmsg()/loadsavewin()/
 * jill_gamepad_capture_button()/savegame() each poll their own input in a tight loop with nothing
 * else drawing or scrolling the world underneath, confirmed by reading all five in full), so this
 * one snapshot -- taken once, right now, before the window's own first pixel is drawn -- stays
 * correct for as long as the window stays open; nothing needs to redo this on every cursor blink or
 * redraw the way the window's own content does.
 *
 * Gated on host_mirror_enabled (no point computing any of this with Mirror Mode off -- msg_mapdemo()
 * itself only ever calls this, JOBJ2.c, while mirror IS off, so this whole block is always skipped
 * there) and !host_mirror_suspended, and defensively bounds-checked throughout: `active` transparently
 * fails (leaves host_mirror_exclude_shadow_active at 0, i.e. no shadow fix, same as before this
 * round) rather than reading or writing out of jill_video's own bounds, if [x1,x2)/[y1,y2) don't
 * fully sit inside the currently active mirror rect.
 *
 * Second round fix -- wootbeer's report after the first round shipped: "works perfect with mirror mode
 * off. but with mirror mode on gamepad and sound sound menus actually mirror under themselves to the
 * left side now when I open them" -- i.e. the shadow fix above was doing NOTHING for exactly the two
 * widest windows (window_width=13) it was most needed for. Root cause: the first round rejected the
 * whole fix outright whenever the computed shadow [sx1,sx2) overlapped the window's own [wx1,wx2) --
 * meant as a "can't happen, bail out safely" guard, but it turns out it DOES happen, for any window
 * not perfectly centered in the mirror rect. window_width=9 windows (CHEATS/ENHANCEMENTS, the
 * top-level PAUSED menu) happen to sit exactly centered -- shadow == window, a no-op either way, so
 * that guard never bit there. window_width=13 windows (SOUND/MUSIC, REMAP GAMEPAD/TOUCH OPTIONS) are
 * NOT centered -- their shadow only partly overlaps their own rect, leaving a real, un-covered strip
 * outside it that still needed protecting -- but the guard rejected the whole rect over that partial
 * overlap and left it unprotected, so those two windows never got any shadow fix and kept
 * self-duplicating exactly as before this feature was added. The overlap itself is harmless and
 * doesn't need guarding against: any part of the shadow that lands inside [wx1,wx2) is inside the
 * window's own rect, already covered by the plain host_mirror_exclude_active check above (so it's
 * never actually read as "shadow" by host_present()), and the pre-flipped bytes this function writes
 * there get fully overdrawn a moment later by the window's own real content anyway (every caller pushes
 * this rect right before drawwin() paints that exact same [x1,x2)x[y1,y2) box) -- so there's nothing
 * to protect against by rejecting the overlap, only a needless bailout to remove.
 *
 * Third round fix -- wootbeer's own report after the snapshot-cache round above shipped: "it's still
 * doing it" (his own screenshot: ENHANCEMENTS, MIRROR: ON, a full mirrored copy of the whole window
 * -- text, border, everything -- sitting immediately to its right, not stale/garbled, just a clean
 * backwards duplicate). Root cause: the capture-or-reuse decision below used to live INSIDE the
 * `active && host_mirror_enabled && !host_mirror_suspended` gate, so it only ever ran once Mirror
 * was ALREADY on -- exactly wrong for the most common way into this menu, opening it with Mirror
 * off and toggling it on from inside (wootbeer's own screenshot shows MIRROR: ON, meaning this is
 * precisely what happened). By the time that first "Mirror is on now" call ever ran, this window
 * had already drawn itself at least once (the entry redraw, with Mirror still off, drew normally
 * with no shadow logic involved at all) -- so that "first" capture-with-mirror-on read the window's
 * OWN already-drawn content as if it were clean board, same failure as the round before, just
 * shifted to trigger on the OFF-then-ON transition specifically instead of every redraw. Fixed by
 * splitting the two decisions apart: the snapshot itself is captured-or-reused on EVERY active call
 * regardless of whether Mirror happens to be on THIS particular call (so the very first call this
 * window ever makes, Mirror on or off, is what gets cached -- domenu_at()'s own call happens before
 * its own drawwin(), so that first call is always genuinely clean); the shadow fill below still only
 * ever runs while Mirror is actually on, same as before, just now always reading from whatever's in
 * the cache (guaranteed clean, however many redraws and on/off toggles have happened since) rather
 * than from live (possibly long-since-dirtied) video memory. */
void host_set_mirror_exclude_rect(int active, int x1, int y1, int x2, int y2)
{
    int wx1 = x1, wx2 = x2, wy1 = y1, wy2 = y2;

    host_mirror_exclude_active = active;
    host_mirror_exclude_x1 = x1;
    host_mirror_exclude_y1 = y1;
    host_mirror_exclude_x2 = x2;
    host_mirror_exclude_y2 = y2;

    host_mirror_exclude_shadow_active = 0;
    if (!active) return;

    if (wx1 < 0) wx1 = 0;
    if (wx2 > JILL_SCREEN_WIDTH) wx2 = JILL_SCREEN_WIDTH;
    if (wy1 < 0) wy1 = 0;
    if (wy2 > JILL_SCREEN_HEIGHT) wy2 = JILL_SCREEN_HEIGHT;
    if (wx2 <= wx1 || wy2 <= wy1) return;

    /* Android port addition, not upstream, this round -- see this function's own "Third round fix"
     * comment above. Captures (or confirms the cache already holds) the true clean background under
     * this exact window rect -- unconditionally, Mirror on or off, so whichever call is genuinely
     * the FIRST one for this window is the one that gets cached, before this call's own caller ever
     * draws anything new into it. */
    {
        int reuse = host_mirror_snapshot_valid && wx1 == host_mirror_snapshot_x1 &&
                    wy1 == host_mirror_snapshot_y1 && wx2 == host_mirror_snapshot_x2 &&
                    wy2 == host_mirror_snapshot_y2;
        if (!reuse) {
            int row;
            for (row = wy1; row < wy2; ++row)
                memcpy(&host_mirror_snapshot[row][wx1], &jill_video[pagedraw != 0][row][wx1],
                       (size_t)(wx2 - wx1));
            host_mirror_snapshot_valid = 1;
            host_mirror_snapshot_x1 = wx1;
            host_mirror_snapshot_y1 = wy1;
            host_mirror_snapshot_x2 = wx2;
            host_mirror_snapshot_y2 = wy2;
        }
    }

    if (host_mirror_enabled && !host_mirror_suspended) {
        int mx1 = host_mirror_x1, mx2 = host_mirror_x2;
        int sx1, sx2;

        sx1 = mx1 + mx2 - wx2;
        sx2 = mx1 + mx2 - wx1;
        if (sx1 < 0) sx1 = 0;
        if (sx2 > JILL_SCREEN_WIDTH) sx2 = JILL_SCREEN_WIDTH;

        if (sx2 > sx1 &&
            wx1 >= mx1 && wx2 <= mx2 && wy1 >= host_mirror_y1 && wy2 <= host_mirror_y2) {
            int row;
            for (row = wy1; row < wy2; ++row) {
                byte capture[JILL_SCREEN_WIDTH];
                int col, n = wx2 - wx1;
                memcpy(capture, &host_mirror_snapshot[row][wx1], (size_t)n);
                for (col = sx1; col < sx2; ++col) {
                    int src_col = mx1 + mx2 - 1 - col;
                    if (src_col < wx1) src_col = wx1;
                    if (src_col >= wx2) src_col = wx2 - 1;
                    jill_video[pagedraw != 0][row][col] = capture[src_col - wx1];
                }
            }
            host_mirror_exclude_shadow_active = 1;
            host_mirror_exclude_shadow_x1 = sx1;
            host_mirror_exclude_shadow_y1 = wy1;
            host_mirror_exclude_shadow_x2 = sx2;
            host_mirror_exclude_shadow_y2 = wy2;
        }
    }
}

void host_set_mirror_hud_exclude_rect(int active, int x1, int y1, int x2, int y2)
{
    host_mirror_hud_exclude_active = active;
    host_mirror_hud_exclude_x1 = x1;
    host_mirror_hud_exclude_y1 = y1;
    host_mirror_hud_exclude_x2 = x2;
    host_mirror_hud_exclude_y2 = y2;
}

int host_key_pressed(void) { return host_queue_read != host_queue_write; }

int host_peek_key(void)
{
    if (!host_key_pressed()) return 0;
    return host_queue[host_queue_read];
}

int host_read_key(void)
{
    int result;
    if (!host_key_pressed()) return 0;
    result = host_queue[host_queue_read];
    host_queue_read = (host_queue_read + 1U) % HOST_QUEUE_CAPACITY;
    return result;
}

int host_key_down(int key_code)
{
    return key_code > 0 && key_code < HOST_KEY_CAPACITY && host_keys[key_code] != 0;
}

void host_clear_keys(void)
{
    memset(host_keys, 0, sizeof(host_keys));
    host_queue_read = host_queue_write = 0;
}

void host_console_clear(void)
{
    /* No console on Android. */
}

void host_sleep(unsigned milliseconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(milliseconds / 1000U);
    ts.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
}

longword host_coreleft(void)
{
    return 0x7fffffffL; /* Not meaningful on Android; nothing currently reads this. */
}

uint32_t host_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

/* Real implementation, matching HOSTSDL.C's own clock_thread_proc()/host_start_clock()/
 * host_stop_clock() exactly in behavior (same 55ms tick, same longclock/myclock update, same
 * idempotent start/stop), just SDL_Thread/SDL_atomic_t -> pthread/a plain volatile flag since this
 * file has no SDL dependency of its own. This is a REAL prerequisite, not cosmetic: JUNGLE.C (the
 * real game loop, not linked into this build yet, but the whole reason this exists) busy-waits on
 * *myclock changing in a lot of places -- ourdelay(), donelevelmsg(), domenu(), loadsavewin(),
 * pageview(), and play()'s own frame-pacing loop chief among them. With *myclock frozen (this
 * function's old no-op body), every one of those would spin forever the instant it ran, hanging
 * the app solid. PLATFORM.C's jill_delay()/jill_ticks() (real-time delays/timestamps, unrelated to
 * this BIOS-clock word) already work fine without this -- they call host_sleep()/host_ticks()
 * directly -- so this was safe to defer until something that actually reads *myclock/longclock
 * needed it.
 *
 * 55ms (not a rounder number) is upstream's own real choice, byte-identical here: it approximates
 * the real DOS BIOS timer tick (18.2 Hz, ~54.9ms/tick) closely enough for HOSTSDL.C's own purposes,
 * and matching it keeps this port's pacing (menu cursor blink rate, message-box auto-advance
 * timeouts, textmsg scroll speed, etc. -- all counted in *myclock ticks upstream) feeling the same
 * as the reference desktop build rather than introducing a second, only-approximately-equivalent
 * timing constant. */
static pthread_t host_clock_thread;
static volatile int host_clock_thread_running;
static volatile int host_clock_stop_requested;

static void *host_clock_thread_proc(void *parameter)
{
    struct timespec ts;
    (void)parameter;
    ts.tv_sec = 0;
    ts.tv_nsec = 55000000L; /* 55ms -- see host_start_clock()'s own comment on why this exact value. */
    while (!host_clock_stop_requested) {
        nanosleep(&ts, NULL);
        ++longclock;
        *myclock = (uword)longclock;
    }
    return NULL;
}

void host_start_clock(void)
{
    if (host_clock_thread_running) return; /* Idempotent, matching HOSTSDL.C's own guard -- both
                                              * host_open() (this file, above) and JUNGLE.C's real
                                              * main() (not linked in yet) call this, and upstream's
                                              * own main() calls it before its own host_open() too,
                                              * so a real double-call is expected, not a bug to
                                              * guard against defensively -- it's the real contract. */
    longclock = 0;
    *myclock = 0;
    host_clock_stop_requested = 0;
    if (pthread_create(&host_clock_thread, NULL, host_clock_thread_proc, NULL) == 0) {
        host_clock_thread_running = 1;
    } else {
        LOGI("host_start_clock: pthread_create failed -- *myclock will not tick, anything that "
             "busy-waits on it changing will hang");
    }
}

void host_stop_clock(void)
{
    if (!host_clock_thread_running) return;
    host_clock_stop_requested = 1;
    pthread_join(host_clock_thread, NULL);
    host_clock_thread_running = 0;
}

/* --- host_audio_* (HOSTAUDIO.H) --------------------------------------------------------------- */

/* Bound once by jillhost_bind_audio_view() (see that function's own comment, in the jillhost_*
 * section below) before any of these can do anything real. All of MUSIC.c's own audio calls run
 * on the same background thread jillMain() itself runs on (the one thing every host_audio_* call
 * below shares -- snd_play() etc. are only ever reached from deep inside JUNGLE.c's main(), called
 * from that thread and never returning until the play session ends), so a bare cached JNIEnv*
 * captured once would technically stay valid the whole time -- jill_audio_env() below still goes
 * through the standard GetEnv()/AttachCurrentThread() dance anyway, since that's one JNI idiom
 * away from "only safe because of a fact about this specific call graph", and it costs nothing
 * here (GetEnv() on an already-attached thread is cheap, no real Attach/Detach pair happens). */
static JavaVM *jill_audio_jvm;
static jobject jill_audio_view;
static jmethodID jill_mid_play_voc;
static jmethodID jill_mid_stop_voc;
static jmethodID jill_mid_is_voc_playing;
static jmethodID jill_mid_set_master_volume;
static jmethodID jill_mid_start_music;
static jmethodID jill_mid_stop_music;
static jmethodID jill_mid_set_music_volume;
/* Android port addition, not upstream -- HOSTSDL.H's own host_show_keyboard()/host_hide_keyboard()
 * comment has the whole story. Bound alongside every other jill_audio_view method below despite
 * having nothing to do with audio -- jill_audio_view is really just "the JillView instance",
 * named for whichever host_* contract needed a callback into it first; reusing that one existing
 * binding (rather than adding a second jillhost_bind_*_view() call jillMain() would also have to
 * make) is deliberate, not an oversight. */
static jmethodID jill_mid_show_keyboard;
static jmethodID jill_mid_hide_keyboard;
/* Android port addition, not upstream -- HOSTANDROID.H's own jillhost_request_pause()/
 * jillhost_notify_surface_destroyed() comments have the whole feature. Same reuse-jill_audio_view
 * reasoning as jill_mid_show_keyboard/jill_mid_hide_keyboard just above -- it's really just "the
 * JillView instance". jill_mid_pause_render_thread calls JillView.java's pauseRenderThread()
 * (blocks the calling -- render -- thread until resumed); jill_mid_init_egl calls its initEgl()
 * (rebuilds the EGL context/surface against whatever Surface is current, used both on a cold
 * start from Java's own surfaceCreated() and, now, a second time from here after a destroyed
 * Surface is replaced -- see jillhost_handle_pause() below). */
static jmethodID jill_mid_pause_render_thread;
static jmethodID jill_mid_init_egl;
/* Android port addition, not upstream -- host_close()'s own comment (above, near host_open()) has
 * the whole "quit the app completely" feature. Same reuse-jill_audio_view reasoning as every other
 * jill_mid_* above. */
static jmethodID jill_mid_request_exit;

/* The real CMF music player (descore/audio_opl/descore_cmf.h) currently open, or NULL between
 * songs -- Jill's own engine only ever has one music track active at a time (MUSIC.c's sb_playtune()
 * always calls sb_shutup() -> StopSequence() -> host_audio_stop_music() before starting a new one),
 * so a single global is enough, the same reasoning host_audio_play_voc()'s single AudioTrack above
 * already relies on for sound effects. Guarded by jill_music_lock: jillhost_render_music() below
 * (called from JillView.java's own background music-render thread, NOT the thread every other
 * host_audio_* function here runs on) reads it on every chunk it renders, while host_audio_play_cmf()/
 * host_audio_stop_music() (the game's own audio thread) replace/free it. In practice
 * host_audio_stop_music() already can't free it out from under a render in progress -- it blocks on
 * jillStopMusic() joining that Java thread BEFORE touching jill_music_player at all, see that
 * function's own comment -- so this lock is cheap defense-in-depth, not the only thing preventing a
 * use-after-free. */
static descore_cmf_player *jill_music_player;
static pthread_mutex_t jill_music_lock = PTHREAD_MUTEX_INITIALIZER;

/* Fixed for this player's whole lifetime -- matches the sample rate JillView.java's own MODE_STREAM
 * AudioTrack (jillStartMusic()) is opened at. 44100 Hz is a safe, universally-supported Android
 * output rate; Nuked OPL3 resamples up to it internally from its own native 49716 Hz (OPL3_Reset(),
 * opl3.c) regardless of what's picked here, so there's no real accuracy reason to pick anything
 * else. */
#define JILL_MUSIC_SAMPLE_RATE 44100

static JNIEnv *jill_audio_env(int *out_did_attach)
{
    JNIEnv *env = NULL;
    *out_did_attach = 0;
    if (jill_audio_jvm == NULL) return NULL;
    if ((*jill_audio_jvm)->GetEnv(jill_audio_jvm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*jill_audio_jvm)->AttachCurrentThread(jill_audio_jvm, &env, NULL) != 0) return NULL;
        *out_did_attach = 1;
    }
    return env;
}

static void jill_audio_env_release(int did_attach)
{
    if (did_attach && jill_audio_jvm != NULL) (*jill_audio_jvm)->DetachCurrentThread(jill_audio_jvm);
}

int host_audio_start(void) { return 1; }

void host_audio_stop(void)
{
    host_audio_stop_voc();
    host_audio_stop_music();
}

int host_audio_digital_available(void) { return 1; }

/* This reports real AdLib/FM hardware presence -- which, now that host_audio_play_cmf() below is
 * real, is no longer even a white lie for its own sake. It still has to stay hardcoded true
 * regardless of the real descore_cmf_open() parse/synthesis outcome for any one song, though: this
 * is what upstream's own MUSIC.c's snd_init() needs: "if (!musicflag) vocflag = 0;" -- this flag
 * (AdlibDetect(), via UNKNOWN.c's WORX_CALL case 0x23) is ANDed into vocflag, the same flag that
 * gates the real digitized VOC sound-effect path below (host_audio_play_voc()). A real DOS machine
 * could never have digitized Sound Blaster effects without also having passed the AdLib-presence
 * check (any Sound Blaster IS AdLib-compatible), so upstream never needed to tell those two
 * capabilities apart or handle one working without the other. Reporting this honestly per-song would
 * require snd_init() itself to re-check it after every song load, which it doesn't do -- so this
 * stays a flat "yes" for the same reason it always has, and a song descore_cmf_open() can't parse
 * just plays nothing (host_audio_play_cmf() returns 0) rather than lying about that specific
 * failure. */
int host_audio_music_available(void) { return 1; }

void host_audio_set_master_volume(byte left, byte right)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    jfloat volume;
    if (env == NULL || jill_audio_view == NULL || jill_mid_set_master_volume == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    /* left/right are both 0-15 nibbles (see UNKNOWN.c's WORX_CALL case 0x20) -- Jill's own VOC
     * playback is mono with no balance concept, so this just averages them into the single
     * 0.0-1.0 gain AudioTrack.setVolume() (JillView.java) expects. */
    volume = ((jfloat)left + (jfloat)right) / 30.0f;
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_set_master_volume, volume);
    jill_audio_env_release(did_attach);
}

void host_audio_set_music_volume(byte left, byte right)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    jfloat volume;
    if (env == NULL || jill_audio_view == NULL || jill_mid_set_music_volume == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    /* Same 0-15/0-15 nibble-pair-to-0.0-1.0 averaging host_audio_set_master_volume() above uses --
     * see that function's own comment. */
    volume = ((jfloat)left + (jfloat)right) / 30.0f;
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_set_music_volume, volume);
    jill_audio_env_release(did_attach);
}

/* --- host_show_keyboard()/host_hide_keyboard() (HOSTSDL.H) -- Android port addition, not
 * upstream. See that header's own comment for the whole feature; both are simple fire-and-forget
 * calls into JillView.java's own jillShowKeyboard()/jillHideKeyboard() (see those methods' own
 * comments), same shape as host_audio_set_master_volume() above -- no return value, and the
 * caller (JUNGLE.c's savegame()) doesn't need to wait for the keyboard to actually be on screen
 * before calling winput(): that function's own busy-wait loop (WIN.c's wgetkey()) already just
 * blinks a cursor until a real keystroke arrives, so it tolerates the keyboard animating in over
 * the next moment with no visible glitch. */
void host_show_keyboard(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_show_keyboard == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_show_keyboard);
    jill_audio_env_release(did_attach);
}

void host_hide_keyboard(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_hide_keyboard == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_hide_keyboard);
    jill_audio_env_release(did_attach);
}

/* voc is a complete, real, standard Creative Voice File that MUSIC.c's own getvoc() assembled in
 * memory: a fixed 32-byte header (the 26-byte file header, byte-identical to a real .VOC file's
 * own magic/version/checksum, immediately followed by the first 6 bytes of a type-1 "Sound Data"
 * block -- block-type byte, 3-byte little-endian block length, sample-rate byte, codec byte) with
 * the raw sample data appended right after it. Parsed here rather than threading a separate length
 * through HOSTAUDIO.H's own host_audio_play_voc(const byte *voc, int volume) signature (upstream's
 * own, unchanged) -- the block's own 3-byte length field already says exactly how many PCM bytes
 * follow, the same way any real VOC-file reader would find out. Only the one codec getvoc() ever
 * produces (0x00, 8-bit unsigned PCM -- no ADPCM compression) is handled; anything else fails
 * closed (returns 0, "didn't play") rather than guessing. */
/* Must track MUSIC.c's own private VOC_BLOCK_SIZE/VOC_HEADER_SIZE #defines (not exposed via
 * MUSIC.H -- upstream's own encapsulation, kept as-is rather than widening that header just for
 * this) -- see MUSIC.c's own VOC_BLOCK_SIZE comment for the real on-device crash (a jill1.vcl
 * sample overrunning its cached slot, SIGSEGV inside this very function) this clamp is the
 * JNI-boundary half of the fix for. Kept as a second, independent constant here rather than a
 * shared header purely to avoid touching upstream's own MUSIC.H shape; the two need to be bumped
 * together if MUSIC.c's own slot size ever changes again. */
#define JILL_VOC_SLOT_PCM_CAPACITY (0x4000 - 0x20)

int host_audio_play_voc(const byte *voc, int volume)
{
    int did_attach;
    JNIEnv *env;
    long block_length;
    long pcm_length;
    int sample_rate;
    jbyteArray pcm_array;

    if (voc == NULL) { LOGW("host_audio_play_voc: called with voc == NULL"); return 0; }
    if (voc[26] != 0x01) { /* not a type-1 "Sound Data" block */
        LOGW("host_audio_play_voc: voc[26]=0x%02x, not a type-1 \"Sound Data\" block -- not playing",
             voc[26]);
        return 0;
    }
    if (voc[31] != 0x00) { /* not 8-bit unsigned PCM */
        LOGW("host_audio_play_voc: voc[31]=0x%02x, not 8-bit unsigned PCM -- not playing", voc[31]);
        return 0;
    }
    block_length = (long)voc[27] | ((long)voc[28] << 8) | ((long)voc[29] << 16);
    pcm_length = block_length - 2; /* rate + codec bytes are counted in block_length, not the PCM */
    if (pcm_length <= 0) {
        LOGW("host_audio_play_voc: computed pcm_length=%ld from block_length=%ld -- not playing",
             pcm_length, block_length);
        return 0;
    }
    if (pcm_length > JILL_VOC_SLOT_PCM_CAPACITY) {
        /* Defense in depth, matching getvoc()'s own clamp (MUSIC.c) -- this should only ever be
         * reached if that clamp's own bound and this one somehow drifted out of sync, or `voc`
         * didn't come from getvoc()'s own cache at all. Clamp rather than trust the block's own
         * length field, so a bad/oversized value can't drive SetByteArrayRegion() below into
         * reading past whatever this pointer actually has behind it -- this is the exact SIGSEGV
         * this function's own header comment above now documents. */
        LOGW("host_audio_play_voc: pcm_length=%ld exceeds the %d-byte cache-slot capacity -- "
             "clamping (see this function's own JILL_VOC_SLOT_PCM_CAPACITY comment)",
             pcm_length, JILL_VOC_SLOT_PCM_CAPACITY);
        pcm_length = JILL_VOC_SLOT_PCM_CAPACITY;
    }
    sample_rate = 1000000 / (256 - (int)voc[30]); /* real VOC type-1 rate-byte formula */

    env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_play_voc == NULL) {
        LOGW("host_audio_play_voc: not playing -- env=%s jill_audio_view=%s jill_mid_play_voc=%s",
             env ? "ok" : "NULL", jill_audio_view ? "ok" : "NULL", jill_mid_play_voc ? "ok" : "NULL");
        jill_audio_env_release(did_attach);
        return 0;
    }
    pcm_array = (*env)->NewByteArray(env, (jsize)pcm_length);
    if (pcm_array == NULL) {
        LOGW("host_audio_play_voc: NewByteArray(%ld) returned NULL", pcm_length);
        jill_audio_env_release(did_attach);
        return 0;
    }
    (*env)->SetByteArrayRegion(env, pcm_array, 0, (jsize)pcm_length, (const jbyte *)(voc + 32));
    /* Android port change, not upstream -- folds the Sound Effects gain slider (jill_sound_gain,
     * MUSIC.H's own comment) into this call's own already-existing intrinsic volume fraction, same
     * "combined with the last master-volume, not applied on its own" shape JillView.java's own
     * jillPlayVoc()/jillSetMasterVolume() pair already documents for sfxCallGain/sfxMasterVolume --
     * this is simply a third multiplicative factor into that same chain, so a later gain-slider
     * change still affects an in-flight sound as soon as the next one plays, no different from a
     * master-volume change today. No headroom past 1.0 here (see MUSIC.H's own jill_sound_gain
     * comment for why only music gets that) -- jill_sound_gain's own range is already exactly
     * 0-JILL_HOST_SOUND_GAIN_STEPS (0-100%), so this fraction never exceeds 1.0 on its own. */
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_play_voc, pcm_array, (jint)sample_rate,
                            (jfloat)volume / 127.0f *
                                ((jfloat)jill_sound_gain / (jfloat)JILL_HOST_SOUND_GAIN_STEPS));
    (*env)->DeleteLocalRef(env, pcm_array);
    jill_audio_env_release(did_attach);
    LOGI("host_audio_play_voc: playing %ld bytes @ %d Hz, volume=%d", pcm_length, sample_rate, volume);
    return 1;
}

int host_audio_voc_playing(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    jboolean playing;
    if (env == NULL || jill_audio_view == NULL || jill_mid_is_voc_playing == NULL) {
        jill_audio_env_release(did_attach);
        return 0;
    }
    playing = (*env)->CallBooleanMethod(env, jill_audio_view, jill_mid_is_voc_playing);
    jill_audio_env_release(did_attach);
    return playing ? 1 : 0;
}

void host_audio_stop_voc(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_stop_voc == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_stop_voc);
    jill_audio_env_release(did_attach);
}

/* Real-time AdLib/OPL2 FM-synthesis playback of Jill's own .ddt CMF music tracks (MUSIC.c's
 * sb_playtune() -> UNKNOWN.c's PlayCMFBlock() -> here), via descore/audio_opl/descore_cmf.c (a
 * from-scratch CMF sequencer informed by AdPlug's own cmf.cpp) driving descore/audio_opl/opl3.c
 * (Nuked OPL3, vendored byte-for-byte unchanged from https://github.com/nukeykt/Nuked-OPL3, run in
 * plain OPL2-compatible 2-op mode -- register 0x105/OPL3-mode-enable is never written, matching
 * what Jill's own CMF data targets). Both are LGPL 2.1-or-later, which combines freely with this
 * project's GPLv3 base and costs nothing extra beyond what shipping full source already does.
 *
 * Chosen over the GoT-style "pre-render each track to an audio file ahead of time, play it back
 * with a plain Java MediaPlayer" approach specifically because Jill's music isn't freely
 * redistributable the way GoT's rendered tracks are -- this synthesizes the .ddt bytes the user
 * already legally supplied, in real time, on-device, and never embeds or redistributes any of
 * Epic's own copyrighted audio.
 *
 * descore_cmf_open() COPIES cmf/length internally (see descore_cmf.h's own comment on why -- in
 * short, MUSIC.c's sb_shutup() frees its `song` buffer on the very next song change, which can't be
 * allowed to yank the buffer out from under a player still being rendered from asynchronously on a
 * separate Java thread). Fails closed: any CMF this parser can't fully validate (bad signature/
 * version, a music block that doesn't fit inside length) returns NULL, so this song just doesn't
 * play rather than risking undefined behavior on a real DOS-era file this project has never seen
 * before -- see host_audio_music_available()'s own comment for how that's still reported. */
int host_audio_play_cmf(const byte *cmf, size_t length, int loop)
{
    int did_attach;
    JNIEnv *env;
    descore_cmf_player *player;

    if (cmf == NULL || length == 0) {
        LOGW("host_audio_play_cmf: called with cmf=%s length=%zu -- not playing",
             cmf == NULL ? "NULL" : "non-NULL", length);
        return 0;
    }

    player = descore_cmf_open(cmf, length, loop, JILL_MUSIC_SAMPLE_RATE);
    if (player == NULL) {
        LOGW("host_audio_play_cmf: descore_cmf_open() returned NULL for a %zu-byte CMF -- bad "
             "signature/version, or a music/instrument block that doesn't fit inside length; not "
             "playing", length);
        return 0;
    }

    /* Stop whatever was playing before (normally MUSIC.c's own sb_shutup() already called
     * host_audio_stop_music() itself before reaching here, but this costs nothing extra and keeps
     * this function safe to call on its own too) before publishing the new player, so
     * jillhost_render_music() below never sees a half-replaced global. */
    host_audio_stop_music();

    env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_start_music == NULL) {
        LOGW("host_audio_play_cmf: parsed OK but not playing -- env=%s jill_audio_view=%s "
             "jill_mid_start_music=%s", env ? "ok" : "NULL", jill_audio_view ? "ok" : "NULL",
             jill_mid_start_music ? "ok" : "NULL");
        jill_audio_env_release(did_attach);
        descore_cmf_close(player);
        return 0;
    }

    pthread_mutex_lock(&jill_music_lock);
    jill_music_player = player;
    pthread_mutex_unlock(&jill_music_lock);

    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_start_music, (jint)JILL_MUSIC_SAMPLE_RATE);
    jill_audio_env_release(did_attach);
    LOGI("host_audio_play_cmf: parsed a %zu-byte CMF OK, started streaming @ %d Hz (loop=%d)",
         length, JILL_MUSIC_SAMPLE_RATE, loop);
    return 1;
}

/* Native-only -- no JNI round-trip needed, jill_music_player already says everything this needs to
 * know. Guarded by jill_music_lock for the same reason jillhost_render_music() below is: this can
 * run concurrently with a render in progress on JillView.java's own background music thread. */
int host_audio_music_playing(void)
{
    int playing;
    pthread_mutex_lock(&jill_music_lock);
    playing = descore_cmf_playing(jill_music_player);
    pthread_mutex_unlock(&jill_music_lock);
    return playing;
}

/* Order matters here and is load-bearing, not stylistic: jillStopMusic() (JNI, called first below)
 * blocks until JillView.java's own background music-render thread has actually stopped calling
 * back into jillhost_render_music() -- it Thread.join()s that thread before returning, see
 * JillView.java's own comment on jillStopMusic(). Only once that's guaranteed does this function
 * actually free and null out jill_music_player. Doing this in the other order (free first, stop
 * second) would leave a window where a render already in flight on the Java thread calls back into
 * jillhost_render_music() and reads a dangling pointer -- jill_music_lock alone can't prevent that,
 * since the render call and the free could still interleave one full render chunk apart. */
void host_audio_stop_music(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    descore_cmf_player *old_player;

    if (env != NULL && jill_audio_view != NULL && jill_mid_stop_music != NULL) {
        (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_stop_music);
    }
    jill_audio_env_release(did_attach);

    pthread_mutex_lock(&jill_music_lock);
    old_player = jill_music_player;
    jill_music_player = NULL;
    pthread_mutex_unlock(&jill_music_lock);

    descore_cmf_close(old_player);
}

/* --- jillhost_* (HOSTANDROID.H) -- Android-side glue, called from JNI ------------------------ */

void jillhost_set_surface_size(int width, int height)
{
    host_surface_width = width > 0 ? width : 1;
    host_surface_height = height > 0 ? height : 1;
    /* Android port addition, not upstream -- see jill_controls.h's own comment on why this call
     * lives right here rather than a separate one-time startup call the way the God of Thunder
     * Android port's own got_controls_init() is only ever called once (from gotMain()): this
     * function already fires on every real resize too (jillSurfaceResized(), jill_jni_bridge.c), not
     * just the very first jillMain() call, so the on-screen touch buttons' own layout_buttons() stays
     * correctly sized/positioned across a rotation or a windowing change for free, with no separate
     * resize hook of its own needed. */
    jill_controls_init(host_surface_width, host_surface_height);
}

void jillhost_notify_key(int key_code, int down)
{
    if (key_code <= 0 || key_code >= HOST_KEY_CAPACITY) return;
    host_keys[key_code] = (byte)(down ? 1 : 0);
    if (down) {
        unsigned next = (host_queue_write + 1U) % HOST_QUEUE_CAPACITY;
        if (next != host_queue_read) {
            host_queue[host_queue_write] = key_code;
            host_queue_write = next;
        }
    }
}

void jillhost_request_stop(void)
{
    host_stop_requested = 1;
}

/* GetMethodID() throws a Java NoSuchMethodError AND returns NULL when it can't find a matching
 * method -- that's a PENDING exception, and per the JNI spec almost every other JNI call made while
 * one is pending is undefined behavior (ExceptionCheck/ExceptionClear/ExceptionDescribe themselves,
 * NewGlobalRef, and DeleteLocalRef are among the few safe exceptions to that rule). Every one of
 * jillhost_bind_audio_view()'s own GetMethodID() calls used to just chain straight into the next one
 * with no check in between -- if any single lookup ever failed (a signature string here not matching
 * JillView.java's own real method exactly, or a method renamed/removed there), every GetMethodID()
 * call after it in that same run was made with a pending exception still live, which explains a
 * failure mode broader than just "that one method didn't bind" -- diagnosed here rather than left
 * to keep silently swallowing the real Java-side stack trace (ExceptionDescribe(), logged before
 * ExceptionClear() throws it away) that would otherwise say exactly which one and why. */
static jmethodID jill_get_audio_method(JNIEnv *env, jclass cls, const char *name, const char *sig)
{
    jmethodID mid = (*env)->GetMethodID(env, cls, name, sig);
    if (mid == NULL) {
        LOGE("jillhost_bind_audio_view: GetMethodID(\"%s\", \"%s\") returned NULL -- JillView.java's "
             "own method is missing, renamed, or this signature string doesn't match it exactly",
             name, sig);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env); /* full Java stack trace to logcat, while it's still live */
        (*env)->ExceptionClear(env);    /* mandatory before making any further non-exception JNI call */
    }
    return mid;
}

/* Called once from jill_jni_bridge.c's jillMain(), before jill_run_game() -> main() ever reaches
 * MUSIC.c's snd_init()/snd_do() -- caches the JVM and a global ref to the JillView instance (env/
 * thiz only stay valid for jillMain()'s own JNI call frame; a global ref is what lets the
 * host_audio_* functions above, called from deep inside that same call stack, keep using it) plus
 * the jmethodIDs for JillView.java's own jillPlayVoc()/jillStopVoc()/jillIsVocPlaying()/
 * jillSetMasterVolume()/jillStartMusic()/jillStopMusic()/jillSetMusicVolume() -- see HOSTAUDIO.H's
 * own header comment section above for how those get used. Safe to call more than once (replaces
 * the previous global ref rather than leaking it), though today's caller only ever does it the one
 * time.
 *
 * Logs a one-line summary of which of the 7 method IDs actually bound (non-NULL) -- added because
 * every host_audio_* function above already fails silent-and-safe when a jmethodID it needs is NULL
 * (see e.g. host_audio_play_voc()'s own env/view/methodID guard), which is the right runtime
 * behavior but means a binding failure here previously left no trace anywhere that anything was ever
 * wrong -- exactly the shape of bug that would make every single audio call, VOC and CMF alike,
 * quietly do nothing forever. */
void jillhost_bind_audio_view(JNIEnv *env, jobject view)
{
    jclass cls;

    (*env)->GetJavaVM(env, &jill_audio_jvm);
    if (jill_audio_view != NULL) (*env)->DeleteGlobalRef(env, jill_audio_view);
    jill_audio_view = (*env)->NewGlobalRef(env, view);
    if (jill_audio_view == NULL) {
        LOGE("jillhost_bind_audio_view: NewGlobalRef(view) returned NULL -- every host_audio_* "
             "function will fail its own jill_audio_view guard and do nothing from here on");
        return;
    }

    cls = (*env)->GetObjectClass(env, jill_audio_view);
    if (cls == NULL) {
        LOGE("jillhost_bind_audio_view: GetObjectClass(jill_audio_view) returned NULL -- should be "
             "impossible for a real, non-null object, but checked rather than assumed");
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
        return;
    }

    jill_mid_play_voc = jill_get_audio_method(env, cls, "jillPlayVoc", "([BIF)V");
    jill_mid_stop_voc = jill_get_audio_method(env, cls, "jillStopVoc", "()V");
    jill_mid_is_voc_playing = jill_get_audio_method(env, cls, "jillIsVocPlaying", "()Z");
    jill_mid_set_master_volume = jill_get_audio_method(env, cls, "jillSetMasterVolume", "(F)V");
    jill_mid_start_music = jill_get_audio_method(env, cls, "jillStartMusic", "(I)V");
    jill_mid_stop_music = jill_get_audio_method(env, cls, "jillStopMusic", "()V");
    jill_mid_set_music_volume = jill_get_audio_method(env, cls, "jillSetMusicVolume", "(F)V");
    jill_mid_show_keyboard = jill_get_audio_method(env, cls, "jillShowKeyboard", "()V");
    jill_mid_hide_keyboard = jill_get_audio_method(env, cls, "jillHideKeyboard", "()V");
    jill_mid_pause_render_thread = jill_get_audio_method(env, cls, "pauseRenderThread", "()V");
    jill_mid_init_egl = jill_get_audio_method(env, cls, "initEgl", "()V");
    jill_mid_request_exit = jill_get_audio_method(env, cls, "jillRequestExit", "()V");
    (*env)->DeleteLocalRef(env, cls);

    LOGI("jillhost_bind_audio_view: bound -- play_voc=%s stop_voc=%s is_voc_playing=%s "
         "set_master_volume=%s start_music=%s stop_music=%s set_music_volume=%s "
         "show_keyboard=%s hide_keyboard=%s pause_render_thread=%s init_egl=%s request_exit=%s",
         jill_mid_play_voc ? "OK" : "MISSING", jill_mid_stop_voc ? "OK" : "MISSING",
         jill_mid_is_voc_playing ? "OK" : "MISSING", jill_mid_set_master_volume ? "OK" : "MISSING",
         jill_mid_start_music ? "OK" : "MISSING", jill_mid_stop_music ? "OK" : "MISSING",
         jill_mid_set_music_volume ? "OK" : "MISSING",
         jill_mid_show_keyboard ? "OK" : "MISSING", jill_mid_hide_keyboard ? "OK" : "MISSING",
         jill_mid_pause_render_thread ? "OK" : "MISSING", jill_mid_init_egl ? "OK" : "MISSING",
         jill_mid_request_exit ? "OK" : "MISSING");
}

/* Android port addition, not upstream -- see host_close()'s own comment (above, near host_open())
 * for the whole "quit the app completely" feature this backs. Same jill_audio_env()/CallVoidMethod
 * pattern every host_audio_ and jillShowKeyboard()-style callback above already uses; JillView.java's
 * own jillRequestExit() hops to the UI thread and calls Activity.finish() -- see that method's own
 * comment. Static, unlike jillhost_request_stop() (declared in HOSTANDROID.H, called from
 * jill_jni_bridge.c) -- this one's only ever called from host_close(), right in this same file. */
static void jillhost_request_app_exit(void)
{
    int did_attach;
    JNIEnv *env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_request_exit == NULL) {
        jill_audio_env_release(did_attach);
        return;
    }
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_request_exit);
    jill_audio_env_release(did_attach);
}

/* --- Render-thread pause/resume (Android port addition, not upstream) -----------------------
 * See HOSTANDROID.H's own jillhost_request_pause()/jillhost_notify_surface_destroyed() comments
 * for the whole feature and the bug it fixes (screen lock/unlock leaving a permanently black
 * screen). Modeled directly on the God of Thunder Android port's own
 * got_show_render_buffer()/pauseRenderThread()/initEgl() trio (got_main.c/GotView.java) -- wootbeer's
 * own explicit ask was for "the same method we used to port God of Thunder". */

void jillhost_request_pause(void)
{
    host_want_pause = 1;
}

void jillhost_notify_surface_destroyed(void)
{
    host_surface_was_destroyed = 1;
}

/* Called from host_pump() (this file's one shared per-tick checkpoint -- see that function's own
 * comment) whenever host_want_pause is set. Runs on the render thread -- the same background
 * thread jillMain() (and so every one of JUNGLE.c's real game-loop ticks, all the way down to
 * this same host_pump() call) has run on since it started, and the only thread that has ever had
 * an EGL context current -- exactly like got_show_render_buffer() runs on GoT's own render
 * thread. */
static void jillhost_handle_pause(void)
{
    int did_attach;
    JNIEnv *env;
    EGLContext eglContext;
    EGLDisplay eglDisplay;
    EGLSurface eglSurface;

    host_want_pause = 0;
    env = jill_audio_env(&did_attach);
    if (env == NULL || jill_audio_view == NULL || jill_mid_pause_render_thread == NULL) {
        LOGW("jillhost_handle_pause: not pausing -- env=%s jill_audio_view=%s "
             "pause_render_thread=%s", env ? "ok" : "NULL", jill_audio_view ? "ok" : "NULL",
             jill_mid_pause_render_thread ? "ok" : "NULL");
        jill_audio_env_release(did_attach);
        return;
    }

    /* Cache these *before* blocking below -- if the Surface gets destroyed while backgrounded,
     * these (not whatever eglGetCurrent*() would report after unblocking) are what actually need
     * tearing down. Same ordering, and the same reasoning, got_show_render_buffer() uses. */
    eglContext = eglGetCurrentContext();
    eglDisplay = eglGetCurrentDisplay();
    eglSurface = eglGetCurrentSurface(EGL_DRAW);

    /* Blocks THIS thread inside JillView.java's pauseRenderThread() until
     * JillGameActivity.onResume() (or, if the Surface was actually destroyed, surfaceCreated()
     * itself, once it has a new one -- see those methods' own comments) calls resumeRenderThread()
     * back on the main thread. Nothing else on this thread runs while parked here -- no more
     * ticks, no more GL/EGL calls -- which is exactly what's wanted while backgrounded, not just
     * skipping the draw while the game loop keeps spinning underneath. */
    (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_pause_render_thread);

    if (host_surface_was_destroyed) {
        /* The Surface this thread's EGL context/surface were built against (JillView.java's own
         * initEgl(), called once from surfaceCreated() before jillMain() ever started) is gone --
         * screen lock/unlock, or backgrounding severe enough that Android tore the Surface down
         * rather than just hiding it. This is exactly what used to leave the screen permanently
         * black: nothing ever rebuilt the EGL surface, so every host_present() after that point
         * kept calling eglSwapBuffers() against a dead one. Tear the dead context down for real
         * and have JillView.java build a fresh one against whatever NEW Surface is current now --
         * surfaceCreated() already ran again by this point (Android always calls it, with the new
         * Surface, before resumeRenderThread() above could have unblocked this call -- see that
         * method's own comment) -- via the exact same initEgl() surfaceCreated() itself calls on a
         * cold start. */
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(eglDisplay, eglSurface);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);

        /* jillgl_texture's own name was allocated against the now-destroyed context above -- it
         * does not survive into the new one, but nothing else here knew that, so leaving it alone
         * would make jillgl_ensure_texture() (see its own comment) skip recreating it (it only
         * allocates a new one when jillgl_texture == 0) and every glBindTexture()/
         * glTexSubImage2D() from then on would silently target a name that means nothing in the
         * new context. This is the exact same "stale GL object survives a context recreation" bug
         * the God of Thunder Android port hit first (see got_show_render_buffer()'s own comment:
         * "the view of the game will go solid white, but music will continue playing" --
         * modex_reset_gl_state() there is this same fix for GoT's own texture cache; Jill's
         * failure mode was black rather than white, same root cause). Resetting to 0 here makes
         * jillgl_ensure_texture()'s next call allocate a real new texture name in the new context,
         * same as a cold start. */
        jillgl_texture = 0;

        if (jill_mid_init_egl != NULL) {
            (*env)->CallVoidMethod(env, jill_audio_view, jill_mid_init_egl);
        } else {
            LOGE("jillhost_handle_pause: Surface was destroyed but jill_mid_init_egl is NULL -- "
                 "cannot rebuild EGL, screen will stay black until the next real pause/resume");
        }
        host_surface_was_destroyed = 0;
    }

    jill_audio_env_release(did_attach);
}

/* Called from JillView.java's own background music-render thread (started by jillStartMusic(),
 * joined by jillStopMusic() -- see those functions' own comments) via the static native
 * jillRenderMusic(short[]) entry point in jill_jni_bridge.c, NOT from the thread every other
 * jillhost_ or host_audio_ function in this file runs on. Deliberately takes no JNIEnv pointer or
 * jobject -- it only ever touches jill_music_player, under jill_music_lock, never anything JNI-side -- so
 * jill_jni_bridge.c's own entry point can call straight in without an attach/detach dance (it's
 * already Java's own thread calling into native code, not native calling back into Java).
 *
 * Returns the number of stereo frames actually written to buf (<= num_frames) -- a short return
 * (including 0) means either there's no music player to render from right now (between songs, or
 * after host_audio_stop_music()) or a non-looping song just reached its own end; either way the
 * caller (JillView.java's render loop) treats that as "nothing more to write this call", not an
 * error. */
size_t jillhost_render_music(int16_t *buf, size_t num_frames)
{
    size_t got;
    pthread_mutex_lock(&jill_music_lock);
    if (jill_music_player == NULL) {
        got = 0;
    } else {
        got = descore_cmf_render(jill_music_player, buf, num_frames);
    }
    pthread_mutex_unlock(&jill_music_lock);

    /* Android port addition, not upstream -- the Music gain slider's own headroom past 100%
     * (jill_music_gain, MUSIC.H's own comment: "the music does seem a little quiet... some headroom
     * for some gain"). Unlike sound effects (host_audio_play_voc() above), there's no OS volume API
     * to lean on for this -- AudioTrack.setVolume() (JillView.java) is hard-capped to 1.0 gain by
     * Android itself, so amplifying past the track's own original level has to happen here, on the
     * real PCM samples this port already fully synthesizes, before Java ever sees them. Skipped
     * entirely at the default step (the overwhelmingly common case, and also true silence -- `got ==
     * 0` -- either way costs nothing extra). Clamped to INT16_MIN/MAX rather than left to wrap: a
     * step past 100% multiplies every sample by more than 1.0, and Nuked OPL3's own output can
     * already sit close to full scale on a loud passage, so an unclamped multiply could wrap into
     * ugly digital-clipping noise instead of just sounding loud. */
    if (got > 0 && jill_music_gain != JILL_HOST_SOUND_GAIN_STEPS) {
        float gain = (float)jill_music_gain / (float)JILL_HOST_SOUND_GAIN_STEPS;
        size_t sample_count = got * 2; /* interleaved stereo */
        size_t i;
        for (i = 0; i < sample_count; ++i) {
            int32_t scaled = (int32_t)((float)buf[i] * gain);
            if (scaled > 32767) scaled = 32767;
            else if (scaled < -32768) scaled = -32768;
            buf[i] = (int16_t)scaled;
        }
    }
    return got;
}

/* Owns a strdup'd copy so the caller's own string (typically a JNI GetStringUTFChars() buffer,
 * which must be released promptly and isn't guaranteed to stay valid) doesn't need to outlive
 * this call. jill_dma_path itself (RECOVERY.H) is just a plain global pointer -- PLATFORM.c owns
 * its storage/default (NULL); this function is the only thing that ever points it somewhere
 * real, and only ever from this one Android-side entry point. */
static char *host_dma_path;

void jillhost_set_dma_path(const char *path)
{
    free(host_dma_path);
    host_dma_path = (path != NULL) ? strdup(path) : NULL;
    jill_dma_path = host_dma_path;
    LOGI("jillhost_set_dma_path: %s", host_dma_path != NULL ? host_dma_path : "(null)");
}

/* rexit()'s temporary Android stand-in that used to live here is gone now that jill/JUNGLE.c
 * provides the real one (savecfg()/snd_exit()/shm_exit()/gc_exit()/gr_exit()/host_close()/exit(),
 * see that file's own definition) -- same removal trigger this comment always said would fire
 * (a duplicate-symbol link error), same as KEYBOARD.C's own former k_read() stand-in before it. */

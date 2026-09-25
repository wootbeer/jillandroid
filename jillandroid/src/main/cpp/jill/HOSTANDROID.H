#ifndef JILL_HOSTANDROID_H
#define JILL_HOSTANDROID_H

#include "RECOVERY.H"

#include <jni.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Port-specific additions for the Android host layer -- NOT part of Jill's own HOSTSDL.H /
 * HOSTAUDIO.H contract (those two headers, vendored unchanged above, are the real decomp API
 * HOSTANDROID.c implements against). These are what jill_jni_bridge.c calls from JNI to feed
 * Android-side state into the host layer -- the same relationship GoT's own modex.h draws
 * between the original DOS API and its own "port-specific additions" section.
 */

/* Called whenever the Android Surface's real pixel size is known/changes (JillView.java's
 * surfaceChanged()) -- host_present() stretches its fixed 320x200 output to fill this size
 * every frame. Safe to call before host_open(). */
void jillhost_set_surface_size(int width, int height);

/* Called from JillView.java's key event handlers. key_code is one of KEYBOARD.H's own key_*
 * values (0 is used for "this port doesn't map that Android key yet" and is ignored). */
void jillhost_notify_key(int key_code, int down);

/* Set by the Java render thread right before it stops calling into native code (Surface
 * destroyed / Activity paused) -- host_is_open()/host_pump() check this so the native render
 * loop returns promptly instead of running forever past its Surface's lifetime. */
void jillhost_request_stop(void);

/* Called once from JillGameActivity/JillView before jillMain() -- sets RECOVERY.H's own
 * jill_dma_path (see that declaration's own comment) to the real absolute path of the user's
 * jill.dma, as copied into private storage by JillActivity's SAF picker. Takes a copy of the
 * string (strdup), so the caller's own copy (e.g. a JNI GetStringUTFChars() buffer) doesn't need
 * to outlive this call. Safe to call before host_open(); must be called before initinfo(). */
void jillhost_set_dma_path(const char *path);

/* Called once from jill_jni_bridge.c's jillMain(), before the real game engine can reach any
 * host_audio_* call (HOSTAUDIO.H) -- binds the JillView instance those functions call back into
 * for real digitized sound-effect playback. See HOSTANDROID.c's own definition for the full
 * comment on why, and see HOSTANDROID.c's host_audio_* section for what it's used for. */
void jillhost_bind_audio_view(JNIEnv *env, jobject view);

/* Called from JillGameActivity.java's onPause() (via its own static native jillPause(), see
 * jill_jni_bridge.c) -- sets a flag HOSTANDROID.c's host_pump() checks once per tick, the same
 * checkpoint that already drives every host_present() call (see that file's own header comment).
 * Unlike jillhost_request_stop() above, this does NOT end the play session: it parks the render
 * thread inside JillView.java's pauseRenderThread() until JillGameActivity.onResume() calls
 * resumeRenderThread() back, the same render-thread-park technique the God of Thunder Android
 * port's own got_show_render_buffer()/GotActivity.java pair already uses (wootbeer's own explicit
 * ask: "the same method we used to port God of Thunder"). See HOSTANDROID.c's own
 * jillhost_handle_pause() for the whole story, including why a destroyed Surface (below) needs
 * more than just resuming to fix. */
void jillhost_request_pause(void);

/* Called from JillView.java's surfaceDestroyed() -- records that the ACTUAL Android Surface (not
 * just the Activity) was torn down, so jillhost_handle_pause() (HOSTANDROID.c) knows the EGL
 * context/surface it was rendering into is dead and must be rebuilt against the new one once
 * rendering resumes, instead of just continuing to draw into a dead surface -- exactly what left
 * a screen lock/unlock showing a permanently black screen before this fix: nothing ever told the
 * native side its old EGL surface had died, so every host_present() after that point kept trying
 * to swap buffers on a surface that no longer existed. */
void jillhost_notify_surface_destroyed(void);

/* Called from jill_jni_bridge.c's static native jillRenderMusic(short[]) entry point, itself
 * called directly by JillView.java's own background music-render thread (started by
 * jillStartMusic(), joined by jillStopMusic()) -- NOT called from any of the JNI entry points
 * jillMain() itself runs on. Renders up to num_frames interleaved stereo S16LE frames (buf must
 * hold at least num_frames * 2 int16_t's) from whatever CMF music is currently playing (HOSTANDROID.c's
 * host_audio_play_cmf()) into buf and returns how many frames it actually wrote -- 0 when nothing
 * is playing right now. See HOSTANDROID.c's own definition for the full comment on its threading/
 * lifetime contract. */
size_t jillhost_render_music(int16_t *buf, size_t num_frames);

#endif

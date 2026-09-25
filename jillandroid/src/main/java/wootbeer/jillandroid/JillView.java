package wootbeer.jillandroid;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.hardware.input.InputManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.os.Handler;
import android.os.Process;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.text.InputType;

import java.util.HashSet;
import java.util.Set;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

/**
 * Jill of the Jungle's render/input shell -- the Jill-specific equivalent of GotView.java, now
 * driving JUNGLE.c's real ported game engine through HOSTANDROID.c's real host_open()/host_pump()/
 * host_present() calls (see jill_jni_bridge.c's own header comment for how jillMain() got wired to
 * the real main()).
 *
 * EGL setup mirrors GotView.java's own initEgl() exactly -- same EGL_CONTEXT_CLIENT_VERSION,
 * same RED/GREEN/BLUE=8 with no alpha/depth/stencil config, same main-thread setFixedSize() hop
 * (guards against the "replacing an already-attached window's content view" crash GotView.java's
 * own comment documents, which can happen here too since JillGameActivity is reachable straight
 * from a background SAF-copy thread's runOnUiThread(), the same launch path). That pattern is
 * plain Android/EGL plumbing, not GoT-specific, so it's copied rather than reinvented -- this is
 * exactly the kind of piece the project's descore shell should eventually share outright, once
 * a second and third game have gone through this same copy-paste (see the project's notes on
 * generalizing only after patterns repeat, not before).
 *
 * What's deliberately NOT copied from GotView.java: its pauseRenderThread()/resumeRenderThread()
 * blocking handshake (that exists so a paused native render loop can be woken back up in place;
 * HOSTANDROID.c's own host loop has no such concept yet -- it just runs until
 * jillhost_request_stop() is called, see HOSTANDROID.H) and GoT's own SoundPool/MediaPlayer split
 * (Jill's real digitized sound effects are played here too, just with a single reused AudioTrack
 * instead of a pool -- see the "Audio" section further down for why one voice is enough; real CMF
 * music is a second, separately-streamed AudioTrack in that same section, MODE_STREAM rather than
 * MODE_STATIC since a song's length isn't known up front the way a VOC sound effect's is).
 *
 * Gamepad input (Android port addition, not upstream -- Jill only ever had a keyboard/joystick
 * input model, see GAMECTRL.c's own header comment on why its joystick reads are permanently
 * stubbed "absent"): D-pad buttons, D-pad-as-hat-axes (some controllers report the D-pad only via
 * AXIS_HAT_X/AXIS_HAT_Y, not KEYCODE_DPAD_*), and both analog sticks (AXIS_X/AXIS_Y left,
 * AXIS_Z/AXIS_RZ right -- Android's standard gamepad axis convention) are all merged into the same
 * four k_up/k_down/k_left/k_right key codes GAMECTRL.c's checkctrl() already reads for both
 * gameplay movement and menu cursor movement (JUNGLE.c's domenu() drives its own cursor off the
 * exact same dx1/dy1 checkctrl0() computes, so nothing extra was needed on the native side to make
 * these also steer every menu). See setDirSource()'s own comment for how three independent input
 * sources are merged into one held/not-held signal per direction.
 *
 * Button A/B USED to forward as a hardcoded key_alt/key_shift (fire2/fire1 -- JPLAYER.c's own
 * throw/jump triggers) straight from this switch. Android port change, not upstream, this round --
 * "Remap Gamepad" submenu (JUNGLE.c's pausemenu(), JILL.H's own JILL_GAMEPAD_ACTION_JUMP/_THROW
 * comment has the whole feature, wootbeer's own explicit ask: "let's again model after the one we made
 * for god of thunder... have them able to be mapped to all of the controls jill of the jungle
 * uses"). Now, every one of the 7 capturable buttons this port supports -- BUTTON_A/B/X/Y/L1/R1/
 * THUMBL, the exact same pool the God of Thunder Android port's own got_controls.c defines, per
 * wootbeer's own "have all the same gamepad buttons available to be mapped that god of thunder has" --
 * forwards its RAW Android keyCode via jillGamepadButtonRaw() below instead, so native code can
 * decide what (if anything) that physical button currently drives by consulting a live, player-
 * editable binding table (jill_jni_bridge.c's own "Gamepad remapping" section) rather than this
 * fixed switch. X/Y/L1/R1/THUMBL have no DEFAULT binding (JILL_GAMEPAD_UNBOUND_KEYCODE, that
 * module's own kJillGamepadRemapActions[] table) -- they're simply available to be captured, same
 * as GoT's own unbound-by-default X/Y/L1/R1/THUMBL. A/B keep their old default bindings (Throw/Jump
 * respectively, "the default controls will be what we have now," wootbeer's own words) but are no
 * longer hardcoded -- rebinding either one away in the new submenu takes effect immediately.
 *
 * Losing the ability to confirm/back out of a menu if BOTH Jump and Throw get remapped away from
 * A/B was a real risk this change had to solve, not just accept -- domenu()/domenu_at() (JUNGLE.c)
 * have always read fire1/fire2 directly for "B backs out"/"A confirms" (see that function's own
 * header comment for the pre-existing convention this doesn't change). The fix, mirroring the God
 * of Thunder Android port's own KEY_CONFIRM/KEY_CANCEL exactly: physical A and B ALSO,
 * unconditionally, forward the new key_menu_confirm/key_menu_cancel codes (KEYBOARD.H's own
 * comment has the full story) every single time, regardless of whatever they're currently bound to
 * in the remap table above -- so every domenu()-based menu (main menu, pause menu, its Quit
 * submenu, askquit(), loadsavewin()'s slot picker, and the new Remap Gamepad screen itself) can
 * always be confirmed with A/Start and backed out of with B, no matter how Jump/Throw get
 * remapped. This is purely additive: fire2/fire1 (whatever they're currently bound to) still ALSO
 * confirm/back exactly as before; key_menu_confirm/key_menu_cancel are the new always-on
 * alternate, not a replacement.
 *
 * System/gamepad Back (KEYCODE_BACK; several gamepads' B button is reported as this instead of, or
 * as well as, KEYCODE_BUTTON_B -- this is what was actually causing wootbeer's reported "B always exits
 * to the episode picker" bug, since this view used to leave KEYCODE_BACK unmapped and let it bubble
 * up to JillGameActivity's default Android behavior of finishing the Activity) is folded into the
 * exact same handling as KEYCODE_BUTTON_B below (both the always-on key_menu_cancel forward AND the
 * raw B-keycode forward, so Back remains fully equivalent to a real B press in every respect, same
 * as before this round) and always consumed here, so it can no longer reach that default behavior.
 * JillGameActivity.onBackPressed() is also overridden as a no-op, to catch Back delivered outside
 * the normal key-event path (e.g. a gesture-navigation swipe) -- see that override's own comment.
 * wootbeer's explicit call: no path back to the episode picker screen for now at all (a real "return to
 * episode picker" feature is deferred to later, not implemented as part of this fix). With B now
 * meaning "back one layer" in every menu (see above), gamepad Back and gamepad B doing the same
 * thing is the correct, expected behavior, not a special case.
 *
 * Save-name text entry (Android port addition, not upstream -- WIN.c's winput()/wgetkey(), real
 * and byte-identical to upstream per that file's own header comment, read typed characters through
 * KEYBOARD.c's k_read() -> host_read_key(), fed by whatever key events actually reach native; on a
 * real DOS box or a desktop SDL build a keyboard is just always there, but Android never shows one
 * for a SurfaceView by default and this file never forwarded ordinary printable-character key
 * events to native at all, only a small fixed set of special keys -- so JUNGLE.c's savegame() would
 * just wait forever for keystrokes that could never arrive). onCheckIsTextEditor()/
 * onCreateInputConnection() below make this view a text-input target (InputType.TYPE_NULL asks the
 * IME to emit raw KeyEvents instead of composed text, so typed characters flow through the exact
 * same onKeyDown()/onKeyUp() -> handleKey() path as every other key), handleKey()'s own default:
 * case now forwards any printable-range Unicode character via event.getUnicodeChar(), and
 * jillShowKeyboard()/jillHideKeyboard() (called from native, see HOSTSDL.H's own comment) bracket
 * JUNGLE.c's one real winput() call site to pop and dismiss the actual Android soft keyboard only
 * for that one text-entry screen.
 */
public class JillView extends SurfaceView implements SurfaceHolder.Callback,
		InputManager.InputDeviceListener {

	private boolean jillRunning;
	/* Android port addition, not upstream -- render-thread pause/resume handshake for screen lock/
	 * unlock and app backgrounding, modeled directly on the God of Thunder Android port's own
	 * GotView.java pauseRenderThread()/resumeRenderThread()/renderThreadObj trio (got_main.c's
	 * got_show_render_buffer() drives it there the same way HOSTANDROID.c's new
	 * jillhost_handle_pause() drives it here -- wootbeer's own explicit ask: "the same method we used
	 * to port God of Thunder"). Root cause of the bug this fixes: surfaceDestroyed() used to call
	 * jillRequestStop() (ending the whole play session, via JUNGLE.c's real rexit() path) and
	 * surfaceCreated() only ever started a NEW native thread when !jillRunning -- which stayed
	 * true forever once set, so unlocking the screen re-created the Surface but nothing was left
	 * to render into it: a permanently black screen. See surfaceCreated()/surfaceDestroyed() and
	 * pauseRenderThread()/resumeRenderThread() below for the real fix. */
	private boolean paused;
	private boolean surfaceWasDestroyed;
	private final Object renderThreadObj = new Object();
	private final Context context;
	private final Handler mainHandler;
	private final String dmaPath;
	private final String shaPath;
	private Point size;
	private SurfaceHolder holder;

	/* Android port addition, not upstream -- on-screen touch controls' own gamepad auto-detect, same
	 * InputManager.InputDeviceListener pattern GotView.java's own identical wiring already
	 * established (that class's own isGamepad()/onInputDeviceAdded()/onInputDeviceRemoved()/
	 * onInputDeviceChanged() comments have the full story) -- see this class's own isGamepad() below
	 * for why a HashSet, not a single boolean, tracks this (onInputDeviceRemoved() only ever gets an
	 * ID, the device is already gone by the time it fires, so there's nothing left to re-check
	 * isGamepad() against; the set is what lets "is at least one gamepad still connected" be computed
	 * from IDs alone). */
	private InputManager inputManager;
	private final Set<Integer> connectedGamepadIds = new HashSet<>();

	/** dmaPath: absolute path to the user's own jill.dma (JillGameActivity computes it from
	 *  JillActivity's own private-storage layout) -- forwarded to native via jillMain() below, see
	 *  HOSTANDROID.H's jillhost_set_dma_path() for what reads it. shaPath: absolute path to the
	 *  chosen episode's own jillN.sha (also JillGameActivity-computed) -- forwarded to native for
	 *  SHM.c's shm_init() to open directly (see jill_jni_bridge.c's own jillMain() comment).
	 *  episodeNumber: which of the three jillhostN native libraries to load -- see this class's
	 *  own static loadJillHostLibrary() comment below for why this can no longer be a fixed
	 *  System.loadLibrary("jillhost") the way it used to be. */
	public JillView(Activity activity, String dmaPath, String shaPath, int episodeNumber) {
		super(activity);
		this.context = activity;
		this.dmaPath = dmaPath;
		this.shaPath = shaPath;
		this.mainHandler = new Handler(activity.getMainLooper());
		this.holder = getHolder();
		loadJillHostLibrary(episodeNumber);
		// See GotView's constructor comment: pins the surface opaque up front so the EGL config's
		// zero-alpha request (below) can't get second-guessed into a translucent surface by the
		// platform, avoiding a whole class of seam/border rendering artifact.
		holder.setFormat(PixelFormat.OPAQUE);
		this.setFocusableInTouchMode(true);
		if (Build.VERSION.SDK_INT >= 26) {
			setDefaultFocusHighlightEnabled(false);
		}
		holder.addCallback(this);

		// Android port addition, not upstream -- on-screen touch controls' own gamepad auto-detect,
		// same InputManager.InputDeviceListener pattern GotView.java's own constructor already
		// establishes (that class's own comment has the full story): hides/disables the on-screen
		// touch buttons whenever a real controller is connected, and brings them back the instant one
		// isn't. registerInputDeviceListener()'s callbacks land on mainHandler's looper, not the
		// render thread, matching every other Android input callback in this class.
		this.inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
		if (inputManager != null) {
			inputManager.registerInputDeviceListener(this, mainHandler);
		}
		for (int deviceId : InputDevice.getDeviceIds()) {
			if (isGamepad(InputDevice.getDevice(deviceId))) {
				connectedGamepadIds.add(deviceId);
			}
		}
		jillSetGamepadConnected(!connectedGamepadIds.isEmpty());

		// Android port addition, not upstream -- one-time real display density (jill_controls.c's own
		// jill_controls_set_display_density() comment has the whole story on why this, not a
		// got_dp_to_px()-style JNI round-trip, is this port's own equivalent). Must run before
		// jillMain() (surfaceCreated()'s own render thread) ever calls into jill_controls_init() --
		// this constructor always finishes, on the UI thread, before that thread is ever started, so
		// there's no race to guard here.
		jillSetDisplayDensity(getResources().getDisplayMetrics().density);
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		this.holder = holder;
		if (!jillRunning) {
			new Thread(new Runnable() {
				@Override
				public void run() {
					size = new Point(getWidth(), getHeight());
					initEgl();
					jillMain(size.x, size.y, dmaPath, shaPath);
				}
			}).start();
			jillRunning = true;
		} else {
			// Android port addition, not upstream -- the native render thread from the FIRST
			// surfaceCreated() above is still alive (jillMain() never returned; only Jill's own
			// engine, via a real Quit-to-DOS rexit(), ever makes that thread end), just parked
			// inside pauseRenderThread() (see that method's own comment) waiting for exactly this.
			// This is the surface coming back after a screen lock/unlock or similar -- resuming it
			// lets HOSTANDROID.c's jillhost_handle_pause() rebuild EGL against this NEW holder
			// (already assigned above) if the old Surface was actually torn down, or just carry on
			// unchanged if it wasn't (see getSurfaceWasDestroyed()'s own comment for that case).
			resumeRenderThread();
		}
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		// See GotView's surfaceChanged() for why this re-pins the buffer: a transient bounds
		// change (system bars, etc.) shouldn't leave a stale-sized buffer compositing against new
		// bounds.
		if (size != null && holder != null) {
			holder.setFixedSize(size.x, size.y);
		}
		jillSurfaceResized(width, height);
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		// Android port addition, not upstream -- used to call jillRequestStop() here, which ends
		// the whole play session (JUNGLE.c's real rexit() path). That was wrong for what actually
		// triggers this: a screen lock, backgrounding, or any other transient loss of the Surface,
		// none of which mean "the player quit". jillRequestStop() is still exactly right for a
		// REAL quit (Jill's own in-game Quit to DOS already reaches it the normal way, through
		// rexit() itself, not through here) -- this path is now purely about the Surface, not the
		// play session: flag it so jillhost_handle_pause() (HOSTANDROID.c, called from host_pump()
		// once this tick's pauseRenderThread() call below is answered) knows its old EGL context/
		// surface are dead and must be rebuilt, not just resumed as-is, against whatever new
		// Surface surfaceCreated() reports next.
		surfaceWasDestroyed = true;
		jillNotifySurfaceDestroyed();
	}

	/** Whether the Surface was actually torn down (not just the Activity backgrounded) the last
	 *  time this view lost it -- JillGameActivity.onResume() checks this before deciding whether
	 *  to call resumeRenderThread() itself or leave it to surfaceCreated() (which will fire again,
	 *  with a genuinely new Surface, only in the torn-down case; Android calls it before
	 *  onResume() runs, so by the time onResume() checks this flag surfaceCreated() has already
	 *  had its chance to resume the render thread). Same split GotActivity.onResume() uses against
	 *  GotView.getSurfaceWasDestroyed(). */
	public boolean getSurfaceWasDestroyed() {
		return surfaceWasDestroyed;
	}

	/** Called from JillGameActivity.onResume() (when the Surface survived) or from
	 *  surfaceCreated() above (when it didn't, right after this view has a fresh one to render
	 *  into) -- wakes the render thread parked in pauseRenderThread() below. */
	public void resumeRenderThread() {
		synchronized (renderThreadObj) {
			paused = false;
			surfaceWasDestroyed = false;
			renderThreadObj.notifyAll();
		}
	}

	/** Called via JNI from native (HOSTANDROID.c's jillhost_handle_pause(), reached from
	 *  host_pump() once JillGameActivity.onPause() has called the new jillPause() -- see that
	 *  method's own comment) on the render thread itself -- the same thread jillMain() (and so
	 *  every one of JUNGLE.c's real game-loop ticks) has been running on since it started. Blocks
	 *  it right here, doing nothing else (in particular touching no GL/EGL call) until
	 *  resumeRenderThread() above is called back on the main thread -- exactly the same
	 *  render-thread-park technique GotView.java's own pauseRenderThread() uses. */
	@SuppressWarnings("unused")
	private void pauseRenderThread() {
		synchronized (renderThreadObj) {
			paused = true;
			while (paused) {
				try {
					renderThreadObj.wait();
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
		}
	}

	/** See GotView.java's own initEgl() -- identical EGL10 setup and the same main-thread
	 *  setFixedSize() hop; this pattern is Android/EGL-generic rather than GoT-specific. Called
	 *  once from surfaceCreated() on a cold start (see that method's own comment), same as always
	 *  -- and now ALSO called a second time via JNI, from HOSTANDROID.c's jillhost_handle_pause(),
	 *  after a screen lock/unlock (or similar) actually destroyed the Surface: `holder` was
	 *  already updated to the new one by that point (surfaceCreated()'s own `this.holder = holder`
	 *  runs before it resumes the render thread), so this builds a completely fresh EGL context/
	 *  surface against it, the exact same way it did the first time. Same technique GotView.java's
	 *  own initEgl() uses for the identical purpose there. */
	@SuppressWarnings("unused")
	private void initEgl() {
		int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
		int[] num_config = new int[1];
		final EGLConfig[] configs = new EGLConfig[1];
		int[] attrib_list = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL10.EGL_NONE};
		EGL10 egl;
		EGLConfig eglConfig;
		EGLContext eglContext;
		EGLDisplay eglDisplay;
		EGLSurface eglSurface;
		GL10 gl;

		if (size != null) {
			final Point fixedSize = size;
			final Object fixedSizeDone = new Object();
			final boolean[] applied = {false};
			synchronized (fixedSizeDone) {
				mainHandler.post(new Runnable() {
					@Override
					public void run() {
						holder.setFixedSize(fixedSize.x, fixedSize.y);
						synchronized (fixedSizeDone) {
							applied[0] = true;
							fixedSizeDone.notifyAll();
						}
					}
				});
				while (!applied[0]) {
					try {
						fixedSizeDone.wait();
					} catch (InterruptedException e) {
						Thread.currentThread().interrupt();
						break;
					}
				}
			}
		}

		egl = (EGL10) EGLContext.getEGL();
		eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
		egl.eglInitialize(eglDisplay, new int[]{1, 0});
		egl.eglChooseConfig(eglDisplay, new int[]{
				EGL10.EGL_RED_SIZE, 8,
				EGL10.EGL_GREEN_SIZE, 8,
				EGL10.EGL_BLUE_SIZE, 8,
				// No alpha / depth / stencil -- Jill, like GoT, is a flat 2D tile-and-sprite game
				// (one textured fullscreen quad per frame, see HOSTANDROID.c's host_present()).
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_DEPTH_SIZE, 0,
				EGL10.EGL_STENCIL_SIZE, 0,
				EGL10.EGL_NONE}, configs, 1, num_config);
		eglConfig = configs[0];
		eglContext = egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, attrib_list);
		eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, holder, null);
		egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
		gl = (GL10) eglContext.getGL();

		gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
		gl.glViewport(0, 0, size.x, size.y);
	}

	// --- Keyboard/gamepad input -----------------------------------------------------------------
	// Forwards to jillhost_notify_key() (HOSTANDROID.c) using Jill's OWN key-code convention --
	// NOT DOS scan-code-set-1 the way GotView.java's gotScancode() uses for GoT. The decomp's own
	// KEYBOARD.H documents these as "values returned by the original BIOS-backed k_read()" (its
	// enum: k_up=200, k_down=208, k_left=203, k_right=205, enter=13, esc/escape=27, plus its own
	// key_space=32 #define, key_shift=0x10, key_alt=0x12) -- read directly from that header rather
	// than guessed, since this port's host_key_down()/host_read_key() contract is meant to carry
	// exactly the values KEYBOARD.c's real Android implementation looks for.

	private static final int K_UP = 200;      // k_up
	private static final int K_DOWN = 208;    // k_down
	private static final int K_LEFT = 203;    // k_left
	private static final int K_RIGHT = 205;   // k_right
	private static final int K_SPACE = 32;    // key_space
	private static final int K_ENTER = 13;    // enter
	private static final int K_ESCAPE = 27;   // esc / escape
	private static final int K_SHIFT = 0x10;  // key_shift -- fire1 (jump in-game; see class comment).
	                                           // No longer forwarded directly from a hardcoded button
	                                           // case (see class comment) -- only
	                                           // jillGamepadButtonRaw()'s own native-side remap
	                                           // dispatch (jill_jni_bridge.c) still ever sends this.
	private static final int K_ALT = 0x12;    // key_alt -- fire2 (throw/use weapon). Same "no longer
	                                           // forwarded directly" note as K_SHIFT just above.
	private static final int K_PAUSE = 0x13;  // key_pause -- Android port addition (KEYBOARD.H's
	                                           // own comment), opens JUNGLE.c's new pausemenu()
	private static final int K_BACKSPACE = 8; // k_bs -- Android port addition, forwarded only for
	                                           // save-name text entry (see class comment); matches
	                                           // KEYBOARD.H's own k_bs value.
	// Android port addition, not upstream -- "Remap Gamepad" submenu's own always-on menu-confirm/
	// menu-cancel signals (see class comment's own paragraph on why these exist, and KEYBOARD.H's
	// key_menu_confirm/key_menu_cancel comment for the full story). Forwarded unconditionally by
	// physical A/B below, regardless of whatever those buttons currently happen to be remapped to.
	private static final int K_MENU_CONFIRM = 0x15; // key_menu_confirm
	private static final int K_MENU_CANCEL = 0x16;  // key_menu_cancel
	// Android port addition, not upstream -- KEYBOARD.H's own key_select comment has the whole
	// story: originally wired to exit the Noisemaker screen, now instead toggles the real Android
	// soft keyboard open/closed there (wootbeer's own follow-up ask), while Start (K_PAUSE) stays the
	// one gamepad exit. Forwarded unconditionally by KEYCODE_BUTTON_SELECT below, same always-on
	// shape as K_PAUSE (KEYCODE_BUTTON_START) just below it -- this file's own job is only to
	// forward the raw button press through; what key_select actually DOES is entirely up to
	// whichever native code reads it (right now, only JUNGLE.c's noisemaker()).
	private static final int K_SELECT = 0x17;       // key_select

	// Android port addition, not upstream -- the "Remap Gamepad" submenu's own 7-button capturable
	// pool (class comment above), mirroring the God of Thunder Android port's own got_controls.c
	// GP_BUTTON_* constants exactly (wootbeer's own ask: "have all the same gamepad buttons available to
	// be mapped that god of thunder has"). Used only to recognize which physical KeyEvent codes get
	// raw-forwarded via jillGamepadButtonRaw() below -- the actual Android KeyEvent.KEYCODE_BUTTON_*
	// constants are used directly in handleKey()'s own switch, these are not separately needed as
	// named constants there, unlike K_UP/K_SHIFT/etc above (which rename Jill's OWN key-code space,
	// a genuinely different set of numbers).

	private static final float STICK_DEADZONE = 0.5f;

	// --- Movement source aggregation --------------------------------------------------------------
	// D-pad buttons, D-pad-as-hat-axes, and both analog sticks can each independently claim "this
	// direction is held" at the same moment; jillhost_notify_key() (native) only tracks a single
	// held/not-held flag per Jill key code, so this file ORs every source together itself and only
	// calls jillKeyEvent() when the COMBINED result actually changes -- otherwise releasing one
	// source (say, the left stick recentering) while another source is still held (the D-pad) would
	// wrongly clear the native side's key state out from under the still-held source.
	private static final int SRC_DPAD = 0;
	private static final int SRC_HAT = 1;
	private static final int SRC_LEFT_STICK = 2;
	private static final int SRC_RIGHT_STICK = 3;
	private static final int SRC_COUNT = 4;
	private final boolean[] upSources = new boolean[SRC_COUNT];
	private final boolean[] downSources = new boolean[SRC_COUNT];
	private final boolean[] leftSources = new boolean[SRC_COUNT];
	private final boolean[] rightSources = new boolean[SRC_COUNT];

	private static boolean anyTrue(boolean[] sources) {
		for (boolean value : sources) {
			if (value) return true;
		}
		return false;
	}

	private void setDirSource(boolean[] sources, int source, boolean pressed, int jillCode) {
		if (sources[source] == pressed) return;
		boolean before = anyTrue(sources);
		sources[source] = pressed;
		boolean after = anyTrue(sources);
		if (before != after) {
			jillKeyEvent(jillCode, after);
		}
	}

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		return handleKey(keyCode, event, true);
	}

	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		return handleKey(keyCode, event, false);
	}

	private boolean handleKey(int androidKeyCode, KeyEvent event, boolean down) {
		switch (androidKeyCode) {
			case KeyEvent.KEYCODE_DPAD_UP:
				setDirSource(upSources, SRC_DPAD, down, K_UP);
				return true;
			case KeyEvent.KEYCODE_DPAD_DOWN:
				setDirSource(downSources, SRC_DPAD, down, K_DOWN);
				return true;
			case KeyEvent.KEYCODE_DPAD_LEFT:
				setDirSource(leftSources, SRC_DPAD, down, K_LEFT);
				return true;
			case KeyEvent.KEYCODE_DPAD_RIGHT:
				setDirSource(rightSources, SRC_DPAD, down, K_RIGHT);
				return true;
			case KeyEvent.KEYCODE_SPACE:
				jillKeyEvent(K_SPACE, down);
				return true;
			case KeyEvent.KEYCODE_ENTER:
			case KeyEvent.KEYCODE_DPAD_CENTER:
				jillKeyEvent(K_ENTER, down);
				return true;
			case KeyEvent.KEYCODE_ESCAPE:
				jillKeyEvent(K_ESCAPE, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_A:
				// Android port change, not upstream, this round -- see class comment's own
				// paragraph on the "Remap Gamepad" submenu for the whole story. Two signals now,
				// not one: an always-on menu-confirm (K_MENU_CONFIRM, never remapped, so confirming
				// a menu can never be lost) plus A's own raw keycode, forwarded to native's live
				// remap table for whatever gameplay action (if any) the player has bound to A
				// (Throw by default -- see the new Remap Gamepad submenu).
				jillKeyEvent(K_MENU_CONFIRM, down);
				jillGamepadButtonRaw(KeyEvent.KEYCODE_BUTTON_A, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_B:
			case KeyEvent.KEYCODE_BACK:
				// Both forward the same way and are always consumed here -- see class comment on
				// why Back is folded in (this is the actual fix for wootbeer's reported "B always exits
				// to the episode picker" bug). Android port change, not upstream, this round --
				// same two-signals-not-one split as A just above: an always-on menu-cancel
				// (K_MENU_CANCEL) plus B's own raw keycode forwarded to native's live remap table
				// (Jump by default). Back is deliberately folded into B's exact same two calls
				// (using B's own keycode for the raw forward, not KEYCODE_BACK's), not just the
				// menu-cancel half, so Back stays fully equivalent to a real B press in every
				// respect, same as before this round.
				jillKeyEvent(K_MENU_CANCEL, down);
				jillGamepadButtonRaw(KeyEvent.KEYCODE_BUTTON_B, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_X:
			case KeyEvent.KEYCODE_BUTTON_Y:
			case KeyEvent.KEYCODE_BUTTON_L1:
			case KeyEvent.KEYCODE_BUTTON_R1:
			case KeyEvent.KEYCODE_BUTTON_THUMBL:
				// Android port addition, not upstream -- the remaining 5 of the "Remap Gamepad"
				// submenu's own 7-button capturable pool (class comment above). Unbound by
				// default (nothing forwarded these at all before this round -- they fell through
				// to the default: case below and were silently ignored, since a real gamepad
				// button's KeyEvent carries no Unicode character), available to be captured for
				// Jump or Throw same as A/B are. No always-on menu-confirm/menu-cancel signal from
				// any of these -- that's deliberately only A/B (see class comment).
				jillGamepadButtonRaw(androidKeyCode, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_START:
				// Android port addition -- opens/drives JUNGLE.c's new pausemenu() (see that
				// function's own header comment for the whole feature). wootbeer's own request,
				// wired to the RP6's Start button specifically -- KEYCODE_BUTTON_START is
				// Android's standard gamepad code for it, same convention KEYCODE_BUTTON_A/B
				// above already rely on for A/B. Deliberately NOT part of the 7-button capturable
				// pool above -- see class comment's own "Remap Gamepad" paragraph and
				// jill_gamepad_capture_button()'s own comment (JUNGLE.c) for why Start needs to
				// stay hardcoded (it's the capture screen's own cancel button).
				jillKeyEvent(K_PAUSE, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_SELECT:
				// Android port addition -- see K_SELECT's own comment above and KEYBOARD.H's own
				// key_select comment for the whole story (now the Noisemaker screen's own gamepad
				// keyboard-show/hide toggle, not an exit). Same "deliberately not part of the
				// 7-button capturable pool" reasoning KEYCODE_BUTTON_START just above already has --
				// Select stays hardcoded too.
				jillKeyEvent(K_SELECT, down);
				return true;
			case KeyEvent.KEYCODE_DEL:
				// Backspace -- Android port addition, save-name text entry only (see class
				// comment); WIN.c's winput() already handles k_bs itself, unchanged from upstream.
				jillKeyEvent(K_BACKSPACE, down);
				return true;
			default:
				// Android port addition, save-name text entry only (see class comment). Every
				// special key Jill's own menus/gameplay care about is already handled by a case
				// above; anything else that carries an ordinary printable character (typed on the
				// soft keyboard shown for savegame()'s winput() call, or a physical keyboard) is
				// forwarded here so WIN.c's real, unmodified winput()/wgetkey() can see it via
				// KEYBOARD.c's k_read(). event.getUnicodeChar() returns 0 for keys with no
				// character (e.g. plain modifier presses), which this deliberately ignores rather
				// than forwarding a bogus keyCode 0 to native.
				if (event != null) {
					int unicodeChar = event.getUnicodeChar();
					if (unicodeChar >= 32 && unicodeChar < 128) {
						jillKeyEvent(unicodeChar, down);
						return true;
					}
				}
				return false;
		}
	}

	/** Android port addition, save-name text entry only (see class comment). Tells the platform
	 *  this SurfaceView can accept an input connection at all -- without this, no IME (soft
	 *  keyboard) would ever be shown for it, whatever jillShowKeyboard() below asks for. */
	@Override
	public boolean onCheckIsTextEditor() {
		return true;
	}

	/** Android port addition, save-name text entry only (see class comment). InputType.TYPE_NULL
	 *  asks the IME to behave as a "raw key event" target rather than composing text itself --
	 *  most keyboards then emit ordinary KeyEvents through onKeyDown()/onKeyUp() (handleKey()'s
	 *  own default: case above) instead of delivering composed text through the InputConnection,
	 *  which is what lets typed characters reach WIN.c's real winput()/k_read() path unchanged. A
	 *  plain BaseInputConnection (not an editable one) is enough since nothing here needs the IME
	 *  to see or edit actual on-screen text -- Jill's own real save-name field (WIN.c's winput())
	 *  already draws and tracks what's typed itself, the same way it does for a real keyboard. */
	@Override
	public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
		outAttrs.inputType = InputType.TYPE_NULL;
		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
		return new BaseInputConnection(this, false);
	}

	// --- Analog stick input (Android port addition -- see class comment) -----------------------

	@Override
	public boolean onGenericMotionEvent(MotionEvent event) {
		if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
				&& event.getAction() == MotionEvent.ACTION_MOVE) {
			// Android's standard gamepad axis convention: AXIS_X/AXIS_Y is the left stick,
			// AXIS_Z/AXIS_RZ is the right stick (AXIS_Z/AXIS_RZ are also used for analog
			// triggers on some devices, but never alongside a second stick on the same axes).
			updateStick(SRC_LEFT_STICK, event.getAxisValue(MotionEvent.AXIS_X),
					event.getAxisValue(MotionEvent.AXIS_Y));
			updateStick(SRC_RIGHT_STICK, event.getAxisValue(MotionEvent.AXIS_Z),
					event.getAxisValue(MotionEvent.AXIS_RZ));
			// Some gamepads report the D-pad only through hat axes, not KEYCODE_DPAD_* events --
			// merged in as its own source (SRC_HAT) alongside SRC_DPAD above so either path works.
			updateStick(SRC_HAT, event.getAxisValue(MotionEvent.AXIS_HAT_X),
					event.getAxisValue(MotionEvent.AXIS_HAT_Y));
			return true;
		}
		return super.onGenericMotionEvent(event);
	}

	private void updateStick(int source, float x, float y) {
		setDirSource(upSources, source, y < -STICK_DEADZONE, K_UP);
		setDirSource(downSources, source, y > STICK_DEADZONE, K_DOWN);
		setDirSource(leftSources, source, x < -STICK_DEADZONE, K_LEFT);
		setDirSource(rightSources, source, x > STICK_DEADZONE, K_RIGHT);
	}

	// --- Gamepad auto-detect (Android port addition, not upstream -- see the constructor's own
	// comment and class comment's own on-screen touch controls paragraph) -----------------------
	// Same test GotView.java's own isGamepad() uses: a real gamepad/joystick reports one of these
	// two source classes. `device` can be null (a just-removed device's InputDevice.getDevice()
	// call).

	private static boolean isGamepad(InputDevice device) {
		if (device == null) {
			return false;
		}
		int sources = device.getSources();
		return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
	}

	@Override
	public void onInputDeviceAdded(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
			jillSetGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceRemoved(int deviceId) {
		if (connectedGamepadIds.remove(deviceId)) {
			jillSetGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceChanged(int deviceId) {
		// A device can change source class after connecting (rare, but GotView.java's own identical
		// listener guards for it too) -- re-evaluate rather than assume whatever was recorded at
		// add-time still holds.
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
		} else {
			connectedGamepadIds.remove(deviceId);
		}
		jillSetGamepadConnected(!connectedGamepadIds.isEmpty());
	}

	// --- Touch input (Android port addition, not upstream -- on-screen D-pad/JUMP/THROW/PAUSE
	// buttons, see class comment) -----------------------------------------------------------------
	// Forwards raw multi-touch pointer events to native jillTouchHandler() (jill_controls.c), which
	// hit-tests them against the on-screen buttons and calls jillhost_notify_key() directly -- same
	// per-pointer-index loop GotView.onTouchEvent() already establishes (ACTION_POINTER_DOWN/UP only
	// concern the one pointer that changed -- getActionIndex(); ACTION_MOVE can carry more than one
	// pointer's new position at once -- every pointer via getPointerCount()). No renderScale
	// conversion needed, same reasoning GotView.onTouchEvent()'s own comment gives: this view's
	// on-screen buttons are drawn directly in real screen pixels (jill_controls.c's own
	// jill_draw_touch_buttons()), so raw event coordinates already line up with what native
	// hit-tests against.
	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(MotionEvent event) {
		int action = event.getActionMasked();
		int firstPointerIndex = (action == MotionEvent.ACTION_POINTER_DOWN
				|| action == MotionEvent.ACTION_POINTER_UP) ? event.getActionIndex() : 0;
		int numPointers = (action == MotionEvent.ACTION_MOVE) ? event.getPointerCount() : 1;
		boolean touchHandled = false;

		for (int i = firstPointerIndex; i < numPointers + firstPointerIndex; ++i) {
			float prevX, prevY;
			if (event.getHistorySize() > 0) {
				prevX = event.getHistoricalX(i, 0);
				prevY = event.getHistoricalY(i, 0);
			} else {
				prevX = event.getX(i);
				prevY = event.getY(i);
			}
			touchHandled |= jillTouchHandler(action, event.getPointerId(i), event.getX(i),
					event.getY(i), prevX, prevY);
		}
		return touchHandled;
	}

	// --- Audio (Android port addition -- see class comment) ------------------------------------
	// Digitized sound effects (MUSIC.c's own snd_play(), the real VOC path) are played here with a
	// plain AudioTrack, called from native (HOSTANDROID.c's host_audio_play_voc()/etc., bound to
	// this instance once via jillhost_bind_audio_view() -- see that function's own comment) over a
	// small JNI callback. Jill's own engine only ever has one digitized effect active at a time
	// (MUSIC.c's snd_play() itself gates a new one on "!VOCPlaying() || priority >= oldpri" before
	// starting it), so a single reused AudioTrack is enough -- no pooling needed, unlike GoT's own
	// SoundPool (which mixes several real-time gameplay sounds at once).
	//
	// Music (MUSIC.c's own sb_playtune(), the real CMF/AdLib path) is a second, independent
	// AudioTrack below -- MODE_STREAM rather than the sound-effect track's own MODE_STATIC, since a
	// song's total length isn't known up front the way a VOC file's fixed PCM buffer is: native
	// (descore/audio_opl/descore_cmf.c, driving Nuked OPL3) synthesizes the user's own *.ddt CMF
	// data in real time, chunk by chunk, for as long as the song plays or until told to stop, rather
	// than rendering a whole song to a buffer ahead of time. A background thread (started by
	// jillStartMusic(), joined by jillStopMusic()) pulls PCM from native via the static native
	// jillRenderMusic(short[]) below and writes it to the track with AudioTrack.write(...,
	// WRITE_BLOCKING) -- that call's own blocking-until-buffer-space behavior is what paces playback
	// with no manual sleep() needed, the same way a real hardware FIFO would. See jillStopMusic()'s
	// own comment for why joining that thread before returning is load-bearing, not just tidy
	// shutdown -- it's what keeps the native side's own free() of its player safe.
	//
	// The legacy AudioTrack(int,int,int,int,int,int) constructor is used deliberately instead of
	// AudioTrack.Builder -- this project's minSdkVersion is 21, and Builder needs API 23.

	private AudioTrack sfxTrack;
	private long sfxEndAtNanos;
	private float sfxCallGain = 1.0f;
	private float sfxMasterVolume = 1.0f;

	/** Called from native (HOSTANDROID.c's host_audio_play_voc()) to play one digitized sound
	 *  effect. pcm is raw 8-bit UNSIGNED mono PCM -- matches AudioFormat.ENCODING_PCM_8BIT exactly,
	 *  straight out of the real VOC data MUSIC.c's own getvoc() assembles, no conversion needed
	 *  (see that native function's own comment for how the sample rate/PCM range were found inside
	 *  the VOC block). volume is 0.0-1.0, this call's own intrinsic gain (from WORX_CALL's own
	 *  PlayVOCBlock volume argument) -- combined with the last jillSetMasterVolume() below, not
	 *  applied on its own, so a later master-volume change still affects an in-flight sound. */
	private void jillPlayVoc(byte[] pcm, int sampleRate, float volume) {
		if (sfxTrack != null) {
			sfxTrack.stop();
			sfxTrack.release();
			sfxTrack = null;
		}
		if (pcm == null || pcm.length == 0 || sampleRate <= 0) return;

		int minBufferSize = AudioTrack.getMinBufferSize(sampleRate,
				AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_8BIT);
		int bufferSize = Math.max(minBufferSize, pcm.length);
		sfxTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate,
				AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_8BIT, bufferSize,
				AudioTrack.MODE_STATIC);
		sfxTrack.write(pcm, 0, pcm.length);
		sfxCallGain = clampVolume(volume);
		sfxTrack.setVolume(clampVolume(sfxCallGain * sfxMasterVolume));
		sfxTrack.play();
		// AudioTrack.getPlayState() doesn't reliably transition away from PLAYSTATE_PLAYING on its
		// own once a MODE_STATIC track runs out of samples, so host_audio_voc_playing() (native,
		// via jillIsVocPlaying() below) is answered off a computed end time instead -- simple,
		// accurate enough for MUSIC.c's own "don't interrupt a still-playing higher-priority sound"
		// check, and avoids needing a playback-position listener/callback at all.
		sfxEndAtNanos = System.nanoTime()
				+ (long) ((pcm.length / (double) sampleRate) * 1_000_000_000L);
	}

	/** Called from native (HOSTANDROID.c's host_audio_stop_voc()). */
	private void jillStopVoc() {
		if (sfxTrack != null) {
			sfxTrack.stop();
			sfxTrack.release();
			sfxTrack = null;
		}
		sfxEndAtNanos = 0;
	}

	/** Called from native (HOSTANDROID.c's host_audio_voc_playing(), i.e. UNKNOWN.c's own
	 *  VOCPlaying()). See jillPlayVoc()'s own comment on why this is time-based, not state-based. */
	private boolean jillIsVocPlaying() {
		return sfxTrack != null && System.nanoTime() < sfxEndAtNanos;
	}

	/** Called from native (HOSTANDROID.c's host_audio_set_master_volume(), i.e. UNKNOWN.c's own
	 *  SetMasterVolume()) -- re-applied to whatever's currently playing too, not just the next
	 *  jillPlayVoc() call, so an in-game volume change (once Jill's own options menu is wired up
	 *  to it) takes effect immediately. */
	private void jillSetMasterVolume(float volume) {
		sfxMasterVolume = clampVolume(volume);
		if (sfxTrack != null) sfxTrack.setVolume(clampVolume(sfxCallGain * sfxMasterVolume));
	}

	private static float clampVolume(float volume) {
		if (volume < 0.0f) return 0.0f;
		if (volume > 1.0f) return 1.0f;
		return volume;
	}

	// --- Music (real-time CMF/AdLib synthesis, Android port addition -- see the Audio section's own
	// class-level comment above for the full design story) -----------------------------------------

	// 4096-frame chunks (interleaved stereo -> 8192 shorts per buffer) -- matches the chunk size the
	// native descore_cmf_render()/opl3.c pair was independently validated against before ever being
	// wired up to this file (see descore_cmf.c's own header comment on its own local test harness);
	// no real accuracy reason to pick a different size here, just needs to be "reasonably small" so
	// jillStopMusic()'s own Thread.join() below never has to wait out an oversized chunk.
	private static final int MUSIC_CHUNK_FRAMES = 4096;

	private AudioTrack musicTrack;
	private Thread musicRenderThread;
	private volatile boolean musicStopRequested;
	private float musicMasterVolume = 1.0f;
	/* Android port addition, not upstream -- set right before pausing for an app-background event
	 * (onAppPause() below), so onAppResume() knows whether bringing music back is actually
	 * correct. Matches the God of Thunder Android port's own GotView.musicWasPlayingBeforeAppPause
	 * exactly (same name, same purpose): if music was already off (the pause menu's own Sound/
	 * Music toggle, or simply nothing loaded yet) before backgrounding, this reads false and
	 * onAppResume() correctly leaves it off instead of starting something the player never asked
	 * for. */
	private boolean musicWasPlayingBeforeAppPause;

	/** Called from native (HOSTANDROID.c's host_audio_play_cmf(), after descore_cmf_open() has
	 *  already successfully parsed the song) to start real-time playback. sampleRate is always
	 *  HOSTANDROID.c's own fixed JILL_MUSIC_SAMPLE_RATE (44100) today -- passed through rather than
	 *  hardcoded here too, so the two sides can't quietly drift apart if that ever changes.
	 *
	 *  Stops/joins any previous music track first (see jillStopMusic()'s own comment) -- belt and
	 *  suspenders, since HOSTANDROID.c's own host_audio_play_cmf() already calls host_audio_stop_
	 *  music() before ever reaching this call, the same defensive-but-technically-redundant pattern
	 *  jillPlayVoc() above already uses for the sound-effect track. */
	private void jillStartMusic(final int sampleRate) {
		jillStopMusic();

		if (sampleRate <= 0) return;

		int minBufferSize = AudioTrack.getMinBufferSize(sampleRate,
				AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
		if (minBufferSize <= 0) return;
		// A few chunks' worth of headroom beyond the device's own reported minimum -- keeps the
		// render thread's own blocking writes from fighting the smallest buffer the device allows,
		// without adding so much latency that stopping/starting a new track feels sluggish.
		int bufferSize = minBufferSize * 4;

		musicTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate,
				AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT, bufferSize,
				AudioTrack.MODE_STREAM);
		musicTrack.setVolume(musicMasterVolume);

		musicStopRequested = false;
		final AudioTrack trackForThread = musicTrack;
		musicRenderThread = new Thread(new Runnable() {
			@Override
			public void run() {
				// Android port note (this file, not upstream -- there's no DOS/desktop equivalent of
				// Android's own thread-priority scheduler): a plain `new Thread(...)` runs at
				// THREAD_PRIORITY_DEFAULT, the same class as JillView's own GLSurfaceView render
				// thread and JUNGLE.c's native game-loop thread it drives. Under CPU pressure from
				// either of those, the scheduler can starve this thread for longer than one
				// MUSIC_CHUNK_FRAMES chunk's worth of real time, which drains musicTrack's buffer and
				// underruns it -- silence gaps and uneven note timing that can sound exactly like
				// "some instruments are missing" or "playing at the wrong speed" even though
				// descore_cmf_render()/opl3.c's own sequencing and resampling math is unaffected (an
				// underrun loses buffered audio, it doesn't distort the samples it did produce).
				// THREAD_PRIORITY_URGENT_AUDIO is the same class Android's own AudioTrack/AudioRecord
				// internal threads and MediaPlayer decoders run at -- it asks the scheduler to treat
				// this thread the way any other real-time audio producer on the system already is.
				Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);

				short[] buf = new short[MUSIC_CHUNK_FRAMES * 2];
				while (!musicStopRequested) {
					int framesRendered = jillRenderMusic(buf);
					if (framesRendered <= 0) break; // nothing playing, or a non-looping song ended
					trackForThread.write(buf, 0, framesRendered * 2, AudioTrack.WRITE_BLOCKING);
				}
			}
		});
		musicTrack.play();
		musicRenderThread.start();
	}

	/** Called from native (HOSTANDROID.c's host_audio_stop_music()) BEFORE that function frees its
	 *  own native player -- see that function's own comment for why this ordering is load-bearing,
	 *  not just tidy shutdown. Setting musicStopRequested and then Thread.join()ing the render thread
	 *  guarantees no call into jillRenderMusic() (and so no call into the native player this Java
	 *  method's own caller is about to free) is still in flight once this method returns -- the
	 *  render thread's own AudioTrack.write(..., WRITE_BLOCKING) call keeps unblocking on its own
	 *  (musicTrack is deliberately NOT stopped until after the join, so the track keeps draining and
	 *  the write keeps making progress) rather than ever hanging waiting for a stop that hasn't
	 *  happened yet. Safe to call when nothing is playing (both fields already null; the join is
	 *  skipped). */
	private void jillStopMusic() {
		musicStopRequested = true;
		if (musicRenderThread != null) {
			try {
				musicRenderThread.join();
			} catch (InterruptedException e) {
				Thread.currentThread().interrupt();
			}
			musicRenderThread = null;
		}
		if (musicTrack != null) {
			musicTrack.stop();
			musicTrack.release();
			musicTrack = null;
		}
	}

	/** Called from native (HOSTANDROID.c's host_audio_set_music_volume(), i.e. UNKNOWN.c's own
	 *  SetMasterVolume() -- the same native call also drives jillSetMasterVolume() above for sound
	 *  effects, Jill's own engine doesn't distinguish a separate music-only volume control). Applied
	 *  live to whatever's currently streaming, same as jillSetMasterVolume() does for sfxTrack. */
	private void jillSetMusicVolume(float volume) {
		musicMasterVolume = clampVolume(volume);
		if (musicTrack != null) musicTrack.setVolume(musicMasterVolume);
	}

	/** Called from JillGameActivity.onPause() -- pauses currently playing background music in
	 *  place (real DOS music_pause()) so it doesn't keep playing while the app is backgrounded or
	 *  the screen is locked. wootbeer's own report after testing the render-thread pause/resume fix
	 *  above (which only silences the SCREEN): "if I lock the screen music still plays". Modeled
	 *  on the God of Thunder Android port's own GotView.onAppPause() -- deliberately independent
	 *  of pauseRenderThread()/resumeRenderThread() elsewhere in this file: real music_pause()/
	 *  music_resume() are their own thing in the DOS source too, not tied to the render loop, and
	 *  AudioTrack.pause() needs no native involvement at all (unlike the render thread's own EGL
	 *  state, nothing here is tied to the now-possibly-destroyed Surface). AudioTrack.pause()
	 *  leaves the track's buffered samples and playback position exactly where they are -- it
	 *  doesn't stop or flush -- so musicRenderThread (jillStartMusic() above) doesn't need to know
	 *  anything happened: its own trackForThread.write(..., WRITE_BLOCKING) call just blocks on
	 *  the now-undrained buffer (see jillStopMusic()'s own comment on that same blocking-write
	 *  behavior) instead of spinning, and picks up automatically the moment onAppResume() below
	 *  calls play() again. Sound effects (sfxTrack) are deliberately left alone here, matching
	 *  GotView.onAppPause()'s own same scope (music only, not its SoundPool sfx) -- they're short
	 *  one-shot blips (a jump, a pickup), not a looping background track, and are done playing on
	 *  their own well before backgrounding could matter. */
	void onAppPause() {
		musicWasPlayingBeforeAppPause =
				musicTrack != null && musicTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING;
		if (musicWasPlayingBeforeAppPause) {
			musicTrack.pause();
		}
	}

	/** Called from JillGameActivity.onResume(). Resumes exactly where onAppPause() left off --
	 *  see that method's own comment for why a bare play() is enough (nothing was stopped or
	 *  flushed, and the render thread never needed to be told anything paused at all). */
	void onAppResume() {
		if (musicWasPlayingBeforeAppPause && musicTrack != null) {
			musicTrack.play();
		}
	}

	/** Called from native (HOSTANDROID.c's host_show_keyboard(), via the same jill_audio_view JNI
	 *  binding already used for the audio callbacks above -- see that file's own comment on why
	 *  reusing it here rather than binding a second reference). Brackets JUNGLE.c's one real
	 *  winput() call site inside savegame() (see class comment). Hops to the main thread since
	 *  InputMethodManager calls, like most view/window operations, aren't safe off it, and native
	 *  calls this from its own host loop thread, not the UI thread. requestFocus() first because
	 *  showSoftInput() only does anything for the currently focused view. */
	private void jillShowKeyboard() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				requestFocus();
				InputMethodManager imm =
						(InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
				if (imm != null) {
					imm.showSoftInput(JillView.this, InputMethodManager.SHOW_FORCED);
				}
			}
		});
	}

	/** Called from native (HOSTANDROID.c's host_hide_keyboard()) -- see jillShowKeyboard() above. */
	private void jillHideKeyboard() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				InputMethodManager imm =
						(InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
				if (imm != null) {
					imm.hideSoftInputFromWindow(getWindowToken(), 0);
				}
			}
		});
	}

	/** Called from native (HOSTANDROID.c's host_close(), via the same jill_audio_view JNI binding
	 *  reused for jillShowKeyboard()/jillHideKeyboard() above) -- host_close() is the one place
	 *  every real quit path funnels through (the main menu's own QUIT, and the pause menu's own
	 *  QUIT TO DOS), so this is reached from both. wootbeer's own original ask: "when I select quit on
	 *  the main menu can we make it quit the app completely?"
	 *
	 *  Android bug fix, this round -- a plain Activity.finish() (what this used to do) only ever
	 *  closed JillGameActivity itself, popping back to whichever Activity launched it (JillActivity's
	 *  episode picker), still sitting in the same still-alive process. That's not actually "quit the
	 *  app completely" -- the whole point wootbeer's original ask already named -- and it's also the
	 *  direct cause of his newest report: "a white screen when 'quitting to dos' then hitting a new
	 *  episode on episode select screen... I also want quit to dos to actually quit the app." Picking
	 *  a new episode from that still-alive process calls JillView's own loadJillHostLibrary() again
	 *  (this class, below) -- System.loadLibrary() is a harmless no-op for a jillhostN .so that's
	 *  already loaded (same episode picked twice) or loads a sibling library that still shares this
	 *  same process (a different episode), either way reusing a process whose native side just tore
	 *  itself down via rexit()'s own real cleanup calls (savecfg()/snd_exit()/shm_exit()/gc_exit()/
	 *  gr_exit()/host_close() -- see rexit()'s own comment, JUNGLE.c) rather than the fresh,
	 *  zero-initialized process state a real DOS EXE launch (or a genuine Android cold start) would
	 *  have -- exactly the kind of stale-native-state mismatch this project has already hit once
	 *  before for a different reason (HOSTANDROID.c's own jillgl_texture reset comment, a stale GL
	 *  object surviving a context recreation) and the likely source of this same white screen.
	 *
	 *  finishAffinity() (not finish()) first -- pops JillGameActivity AND JillActivity together, the
	 *  entire task, not just this one screen, so there is no "episode picker still sitting there to
	 *  return to" left at all -- then Process.killProcess(Process.myPid()) to actually end the
	 *  process outright rather than just leaving an empty task behind for Android to reclaim
	 *  whenever it feels like it. Selecting a new episode after this is necessarily a genuine fresh
	 *  app launch (a new process, every jillhostN native library loaded from scratch, every C global
	 *  zero-initialized for real) -- eliminating the stale-process class of bug above, not just this
	 *  one report of it. Both calls still hop to the main thread first, same reasoning as every other
	 *  mainHandler.post() in this class and unchanged from before this fix (Process.killProcess() has
	 *  no such requirement itself, but doing it right after finishAffinity() on the same thread, in
	 *  the same posted Runnable, needs no second hop). context is always the real JillGameActivity
	 *  instance this view was constructed with (see the constructor), never anything else, so the
	 *  instanceof check is defensive, not load-bearing. */
	private void jillRequestExit() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				if (context instanceof Activity) {
					((Activity) context).finishAffinity();
				}
				Process.killProcess(Process.myPid());
			}
		});
	}

	// --- Native entry points -------------------------------------------------------------------

	/** Android port note (this file, not upstream): used to be a fixed `static { System.
	 *  loadLibrary("jillhost"); }` block, back when the native side only ever built one "jillhost"
	 *  target compiled unconditionally for Episode 1 (see CMakeLists.txt's own comment on
	 *  JILLHOST_SOURCES for the real bug that caused: Episodes 2/3 chdir()'d into their own real
	 *  asset directory but the engine still opened Episode 1's hardcoded filenames in it, so the
	 *  screen just stayed blank). CMakeLists.txt now builds three separate libraries --
	 *  jillhost1/jillhost2/jillhost3, one per JILL_EPn -- so this has to pick the one matching
	 *  whichever episode JillGameActivity is actually launching, which means it can no longer run
	 *  as a static initializer (there's no episode number yet at class-load time, only once a
	 *  JillView instance is actually constructed for a specific episode). Called once, from the
	 *  constructor, before anything else touches a native method -- System.loadLibrary() only
	 *  needs to happen before the first native call is actually made, not before the class
	 *  loads, and calling it more than once for the *same* library name (e.g. a second JillView
	 *  built for the same episode) is a harmless no-op per its own contract.
	 *
	 *  Loading two DIFFERENT jillhostN libraries into the same process is what would actually be
	 *  unsafe (see CMakeLists.txt's own comment: all three export the same JNI-implicit symbol
	 *  names, so which one answers a call would be undefined) -- not possible today since
	 *  JillGameActivity.onBackPressed() is a deliberate no-op with no way back to JillActivity's
	 *  episode picker, so a process can only ever reach one episode's JillView. Revisit this
	 *  together with that no-op if real back-navigation between episodes is ever built. */
	private static void loadJillHostLibrary(int episodeNumber) {
		System.loadLibrary("jillhost" + episodeNumber);
	}

	// NOT static, unlike the three natives below -- jillhost_bind_audio_view() (jill_jni_bridge.c's
	// own jillMain(), C side) needs a real jobject reference to THIS JillView instance to call back
	// into for audio playback, not the jclass a static native method's own "thiz"-named parameter
	// actually is. This was the crash wootbeer saw the very next test after audio was added: jillMain()
	// was still declared static here, so the native side's "thiz" was silently JillView's Class
	// object, not an instance -- GetMethodID() against it can never find jillPlayVoc()/etc. (they're
	// instance methods on JillView, not on java.lang.Class), which throws and leaves a Java
	// exception pending; any further JNI call made after that (and jillMain() makes plenty, all the
	// way through the rest of the game session) is undefined behavior, which is exactly what an
	// early, otherwise-unexplained crash right after entering gameplay looks like. Dropping static
	// here needs no call-site change (surfaceCreated()'s own Runnable calls it unqualified either
	// way -- Java resolves that to the enclosing JillView instance automatically once it's no longer
	// static) and no native-side signature change either (jill_jni_bridge.c's own jillMain() already
	// declared its second parameter as "jobject thiz", which was always syntactically fine C either
	// way -- only the VALUE the JVM actually passes there changes, from a jclass to a real jobject).
	private native void jillMain(int width, int height, String dmaPath, String shaPath);

	private static native void jillSurfaceResized(int width, int height);

	private static native void jillKeyEvent(int keyCode, boolean down);

	// Android port addition, not upstream -- "Remap Gamepad" submenu's own raw forwarding channel
	// for the 7 capturable buttons (class comment above, handleKey()'s own new cases). keyCode here
	// is the ordinary Android KeyEvent.KEYCODE_BUTTON_* constant itself, NOT translated through
	// Jill's own K_* key-code space the way jillKeyEvent() above is -- jill_jni_bridge.c's own
	// jill_gamepad_dispatch_raw() (that file's "Gamepad remapping" section) is what maps this raw
	// keycode to a Jill key code, via the live, player-editable binding table.
	private static native void jillGamepadButtonRaw(int keyCode, boolean down);

	private static native void jillRequestStop();

	// Android port addition, not upstream -- on-screen touch controls' own JNI entry points
	// (jill_controls.c). jillTouchHandler() mirrors GotView.java's own touchHandler() exactly (see
	// onTouchEvent()'s own comment); jillSetGamepadConnected() is called both from this class's own
	// InputManager.InputDeviceListener callbacks and once from the constructor's own initial device
	// scan; jillSetDisplayDensity() is called once, also from the constructor, before the render
	// thread (and so jill_controls_init()) ever starts (see jill_controls.h's own comment on why this
	// is simpler here than the God of Thunder Android port's own got_px_to_dp()/got_dp_to_px() JNI
	// round-trip).
	private static native boolean jillTouchHandler(int action, int pointerId, float x, float y,
			float prevX, float prevY);

	private static native void jillSetGamepadConnected(boolean connected);

	private static native void jillSetDisplayDensity(float density);

	/* Android port addition, not upstream -- see surfaceDestroyed()'s own comment. Static, same
	 * reasoning as jillSurfaceResized()/jillKeyEvent() above: just sets a flag HOSTANDROID.c's
	 * jillhost_handle_pause() reads, needs no JillView instance. */
	private static native void jillNotifySurfaceDestroyed();

	// Static, unlike jillMain() above -- this needs no JillView instance at all, only the single
	// global native descore_cmf_player HOSTANDROID.c itself owns (see jillhost_render_music()'s own
	// comment, HOSTANDROID.c). Called directly by the background thread jillStartMusic() above
	// starts, NOT via any JNI callback bound by jillhost_bind_audio_view() -- this is Java's own
	// thread calling straight into native code, the reverse direction from every other native
	// entry point on this page. buf's length (in shorts, i.e. samples -- 2 per stereo frame) is
	// what tells native how many frames' worth of room it has to fill; the return value is how many
	// frames it actually wrote, which may be less (including 0) when nothing is playing right now or
	// a non-looping song just reached its own end -- see jillStartMusic()'s own render-loop comment
	// for how that's handled.
	private static native int jillRenderMusic(short[] buf);
}

package wootbeer.jillandroid;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

import java.io.File;

/**
 * Hosts JillView -- Jill's real game screen, driving JUNGLE.c's real, fully-ported game engine
 * through HOSTANDROID.c (see JillView.java's own header comment for exactly how). Launched from
 * JillActivity's "Play Episode N" button once that episode's asset files are confirmed present in
 * private storage.
 *
 * Deliberately still minimal in one real sense: no name-entry overlay (Jill's own save/load flow
 * has no equivalent of GoT's player-name prompt) and no "return to episode picker" path (see
 * onBackPressed()'s own comment below -- a deliberate, still-open TODO, not an oversight). Pause/
 * resume music handshake and on-screen touch controls are both real and wired up (onPause()/
 * onResume() below, jill_controls.c). Reuses JillActivity's own setImmersive()/fullscreen-flags
 * shape since that's plain Android boilerplate, not GoT-specific.
 */
public class JillGameActivity extends Activity {

	/** Intent extra key: which episode (1/2/3) to load -- JillActivity's "Play Episode N" button
	 *  sets this to its EpisodeSpec.number. Defaults to 1 (see onCreate()) if somehow missing,
	 *  rather than crashing -- there's currently no other caller that would omit it. */
	public static final String EXTRA_EPISODE_NUMBER = "episode_number";

	private JillView jillView;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		setImmersive();
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
					new View.OnSystemUiVisibilityChangeListener() {
						@Override
						public void onSystemUiVisibilityChange(int visibility) {
							if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
								setImmersive();
							}
						}
					});
		}
		int episodeNumber = getIntent().getIntExtra(EXTRA_EPISODE_NUMBER, 1);
		jillView = new JillView(this, sharedDmaPath(), episodeShaPath(episodeNumber), episodeNumber);
		setContentView(jillView);
		jillView.requestFocus();
	}

	/** Absolute path to the user's own jill.dma, as JillActivity's SAF picker copies it into
	 *  private storage -- duplicated here rather than referencing JillActivity's own private
	 *  SHARED_DIR/SHARED_DMA_FILE constants (different package, and those are private to that
	 *  class). Worth pulling into one shared place both classes reference once a third caller
	 *  needs it -- not yet. */
	private String sharedDmaPath() {
		File jillRootDir = new File(getFilesDir(), "jill");
		File sharedDir = new File(jillRootDir, "shared");
		return new File(sharedDir, "jill.dma").getAbsolutePath();
	}

	/** Absolute path to the given episode's own jillN.sha (the shape/sprite table SHM.c's
	 *  shm_init() loads), as JillActivity's SAF picker copies it into private storage --
	 *  "jill" + N + ".sha" and "ep" + N match JillActivity's own EpisodeSpec.shapeFile /
	 *  EpisodeSpec.dirName() exactly (see that class's own EPISODES table); duplicated here for
	 *  the same reason sharedDmaPath() duplicates SHARED_DIR/SHARED_DMA_FILE above. */
	private String episodeShaPath(int episodeNumber) {
		File jillRootDir = new File(getFilesDir(), "jill");
		File epDir = new File(jillRootDir, "ep" + episodeNumber);
		return new File(epDir, "jill" + episodeNumber + ".sha").getAbsolutePath();
	}

	/** Android port addition, not upstream -- pauses the native render thread instead of leaving
	 *  it to keep running (or, before this fix, killing the whole play session -- see
	 *  JillView.surfaceDestroyed()'s own comment for the bug this and onResume() below replace:
	 *  locking the screen left the game permanently black on unlock). jillView is only ever null
	 *  for the brief window before onCreate() finishes constructing it -- Android can't call
	 *  onPause() before onCreate() returns, so by the time this can run, jillView (and the native
	 *  library it loads in its own constructor) already exist; the null check is defensive, not
	 *  load-bearing, matching GotActivity.onPause()'s own same-shaped check there for a different
	 *  reason (its gotView can genuinely still be null this late, since it waits on an async SAF
	 *  copy first). */
	@Override
	protected void onPause() {
		super.onPause();
		if (jillView != null) {
			jillPause();
			// Pauses whatever background music is currently playing (see JillView.onAppPause()'s
			// own comment) -- deliberately independent of jillPause()/the native render-thread
			// pause above: real music_pause()/music_resume() are their own thing in the DOS
			// source too, not tied to the render loop, and a streaming AudioTrack has no reason to
			// keep playing while the app is backgrounded regardless of what the native side is
			// doing. wootbeer's own report: the render-thread pause/resume fix alone only silenced the
			// screen on a lock -- "if I lock the screen music still plays" -- this is the other
			// half. Same split GotActivity.onPause() uses for gotView.onAppPause().
			jillView.onAppPause();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
		// Android port addition, not upstream -- mirrors GotActivity.onResume()'s own
		// gotView.resumeRenderThread() call. Only resume here when the Surface itself survived
		// (getSurfaceWasDestroyed() false): if it didn't, Android calls JillView.surfaceCreated()
		// again with a genuinely new Surface BEFORE this runs, and that method's own else branch
		// already resumes the render thread itself once it has that new Surface to hand off --
		// calling resumeRenderThread() twice would be harmless (the second call just finds
		// `paused` already false) but redundant, so this mirrors GotActivity's own same guard.
		if (jillView != null && !jillView.getSurfaceWasDestroyed()) {
			jillView.resumeRenderThread();
		}
		// Unconditional, unlike resumeRenderThread() above -- music pause/resume has nothing to do
		// with the Surface, so it doesn't need that same guard. Same split GotActivity.onResume()
		// uses for gotView.onAppResume().
		if (jillView != null) {
			jillView.onAppResume();
		}
	}

	/** Android port addition, not upstream -- static native, same reasoning as JillView's own
	 *  jillSurfaceResized()/jillKeyEvent()/jillNotifySurfaceDestroyed(): just sets a flag
	 *  HOSTANDROID.c's jillhost_handle_pause() reads on its own next host_pump() tick, needs no
	 *  Activity or JillView instance. Declared here rather than on JillView itself, matching where
	 *  GotActivity.java's own analogous gotPause() lives -- the Activity is what actually owns the
	 *  onPause()/onResume() lifecycle this whole feature hangs off of. */
	private static native void jillPause();

	/** Android port addition, not upstream: overridden as a no-op so Back can't leave this screen
	 *  at all right now, however it's triggered -- a real hardware/on-screen Back key press
	 *  reaches JillView's onKeyDown/onKeyUp first (see that class's own comment: it now consumes
	 *  KEYCODE_BACK there, forwarding it as key_shift/fire1 same as gamepad button B), but gesture
	 *  navigation (an edge swipe) invokes onBackPressed() directly without ever going through the
	 *  key-event path this Activity's views see, so it needed its own override too. This is wootbeer's
	 *  explicit call, not a bug being papered over: there's currently no way back to JillActivity's
	 *  episode picker from here, and he asked for exactly that -- a working "return to episode
	 *  picker" feature is deferred to later, not implemented as part of this fix. When that feature
	 *  is actually built, replace this override with real logic rather than just deleting it. */
	@Override
	public void onBackPressed() {
	}

	/** Enables immersive mode, hiding navigation controls -- same as JillActivity.java's own. */
	private void setImmersive() {
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setSystemUiVisibility(
					View.SYSTEM_UI_FLAG_LAYOUT_STABLE
							| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
							| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_FULLSCREEN
							| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}
}

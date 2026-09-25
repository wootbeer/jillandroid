package wootbeer.jillandroid;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Jill of the Jungle's Storage Access Framework asset picker + episode-readiness screen.
 *
 * Renamed from GotActivity and moved from package wootbeer.gotandroid into wootbeer.jillandroid
 * (alongside JillGameActivity/JillView, its own only real siblings now) during this project's
 * pre-release cleanup, via Android Studio's own "Rename"/"Move" refactors rather than a
 * hand-edit -- see AndroidManifest.xml's own updated android:name for the Java side of that
 * move. This class's own job, despite its former name, has always been entirely Jill of the
 * Jungle's: there's no separate Gradle module or native library it was ever paired with either
 * -- this project only ever has the one jillandroid Gradle module (see settings.gradle), and the
 * "got" native SHARED library this project used to also build was itself dead weight, removed in
 * this project's pre-release cleanup (see CMakeLists.txt's own header comment).
 *
 * Adapted from the GoT Android port's own GotActivity.java (itself adapted from Descore's
 * DescoreActivity.java) -- that's a citation of the real, separate God of Thunder Android port
 * repo, not a leftover reference to this class's own former name. Same SAF folder-picker shape
 * -- game data is proprietary, so it isn't bundled; the user points us at their own copy, we
 * copy what we need into private app storage -- generalized from GoT's single GOTRES.DAT file to
 * Jill's real layout: three episodes, each with its own JILLn.CFG/SHA/VCL plus a folder full of
 * *.JNn level files, plus a handful of files shared across all three (JILL.DMA, the *.DDT music
 * tracks).
 *
 * The native game engine is real and fully ported (JUNGLE.c et al. -- see jill_jni_bridge.c's
 * own header comment for exactly how it's wired up). Each ready episode's own "Play Episode N"
 * button below hands off straight to JillGameActivity/JillView with a plain startActivity(Intent)
 * -- not a stub -- so the picker/copy/readiness pipeline built here leads directly into real
 * gameplay.
 */
public class JillActivity extends Activity {
	private static final String TAG = "JillActivity";

	// --- Jill of the Jungle's asset manifest --------------------------------------------------

	/**
	 * One entry per episode. Filenames are the real on-disk names (see EPISODE.H in the Jill
	 * decomp); matching against the picked folder is case-insensitive -- both the GoT picker's
	 * own findChildDocument() and openjill-rs's "matched case-insensitively" troubleshooting
	 * note landed on that same rule independently, and the decomp's own EPISODE.H uses
	 * uppercase names for episode 1 (JILL1.SHA) and lowercase for episodes 2/3 (jill2.sha),
	 * which a real shareware/GOG install doesn't necessarily preserve consistently anyway.
	 */
	private static final class EpisodeSpec {
		final int number;
		final String title;
		final String configFile, shapeFile, soundFile;
		final String introLevel, startLevel;
		final String levelExtension; // e.g. ".jn1" -- every file with this extension gets
		                             // copied opportunistically, not just the two named above

		EpisodeSpec(int number, String title, String configFile, String shapeFile,
				String soundFile, String introLevel, String startLevel, String levelExtension) {
			this.number = number;
			this.title = title;
			this.configFile = configFile;
			this.shapeFile = shapeFile;
			this.soundFile = soundFile;
			this.introLevel = introLevel;
			this.startLevel = startLevel;
			this.levelExtension = levelExtension;
		}

		/** The files that must be present (and non-empty) for this episode to be playable.
		 *  Individual numbered *.JNn level files are NOT included here on purpose -- see the
		 *  class header on isEpisodeReady() for why the level count isn't hardcoded.
		 *
		 *  AUDIO.EPC (MUSIC.c's own PC-speaker digitized sound-effect data table) is deliberately
		 *  NOT in this list, even though snd_do() looks for it unconditionally on Android today --
		 *  see that function's own comment for why: real GOG/registered installs (confirmed against
		 *  wootbeer's own copy) don't ship this file at all, so requiring it here would mean no episode
		 *  could ever be "ready". Fixed on the engine side instead (MUSIC.c degrades gracefully when
		 *  it's missing), not by pretending the asset picker can produce a file that doesn't exist. */
		String[] coreFiles() {
			return new String[]{configFile, shapeFile, soundFile, introLevel, startLevel};
		}

		String dirName() {
			return "ep" + number;
		}
	}

	private static final EpisodeSpec[] EPISODES = {
			new EpisodeSpec(1, "Jill of the Jungle",
					"jill1.cfg", "jill1.sha", "jill1.vcl", "intro.jn1", "map.jn1", ".jn1"),
			new EpisodeSpec(2, "Jill Goes Underground",
					"jill2.cfg", "jill2.sha", "jill2.vcl", "intro.jn2", "0.jn2", ".jn2"),
			new EpisodeSpec(3, "Jill Saves The Prince",
					"jill3.cfg", "jill3.sha", "jill3.vcl", "intro.jn3", "map.jn3", ".jn3"),
	};

	// Shared across all three episodes -- its own subfolder so it only needs to be present/
	// copied once regardless of which episodes the user owns.
	private static final String SHARED_DIR = "shared";
	private static final String SHARED_DMA_FILE = "jill.dma";
	private static final String MUSIC_EXTENSION = ".ddt";

	private static final int REQUEST_CODE_OPEN_DATA_FOLDER = 4242;

	private File jillRootDir;
	private float buttonSizeBias;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

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

		DisplayMetrics metrics = getResources().getDisplayMetrics();
		buttonSizeBias = (float) Math.min(Math.max((metrics.widthPixels / metrics.xdpi
				+ metrics.heightPixels / metrics.ydpi) / 5.5f, 1), 1.4);

		jillRootDir = new File(getFilesDir(), "jill");

		List<EpisodeSpec> ready = readyEpisodes();
		Log.i(TAG, "onCreate: ready episodes=" + ready.size() + " of " + EPISODES.length);
		if (!ready.isEmpty()) {
			showEpisodeSelect(null);
		} else {
			showDataPicker(null);
		}
	}

	// --- Episode readiness ----------------------------------------------------------------

	/**
	 * An episode is "ready" once every one of its core files exists in private storage and is
	 * non-empty. That's weaker than checking exact expected byte counts (which would make this
	 * brittle across different game releases/versions -- shareware vs. registered, etc.), but
	 * combined with the copy-time size verification in copyOneFile() below -- which DOES check
	 * against the real source size -- a short/interrupted copy can't silently pass as complete
	 * later. That's the same integrity problem GOTRES.DAT's old MIN_PLAUSIBLE_GOTRES_SIZE floor
	 * was guarding against, just solved at copy time instead of via a hardcoded size guess,
	 * since there's no longer one single file with one expected size.
	 */
	private boolean isEpisodeReady(EpisodeSpec episode) {
		File epDir = new File(jillRootDir, episode.dirName());
		for (String coreFile : episode.coreFiles()) {
			File f = new File(epDir, coreFile);
			if (!f.exists() || f.length() <= 0) {
				return false;
			}
		}
		return true;
	}

	private List<EpisodeSpec> readyEpisodes() {
		List<EpisodeSpec> ready = new ArrayList<>();
		for (EpisodeSpec episode : EPISODES) {
			if (isEpisodeReady(episode)) {
				ready.add(episode);
			}
		}
		return ready;
	}

	/** Which of an episode's core files are still missing -- shown to the user so a partial
	 *  folder (e.g. only episode 1's shareware files) tells them exactly what's absent instead
	 *  of a generic failure. */
	private List<String> missingCoreFiles(EpisodeSpec episode) {
		List<String> missing = new ArrayList<>();
		File epDir = new File(jillRootDir, episode.dirName());
		for (String coreFile : episode.coreFiles()) {
			File f = new File(epDir, coreFile);
			if (!f.exists() || f.length() <= 0) {
				missing.add(coreFile);
			}
		}
		return missing;
	}

	// --- UI: folder picker prompt -----------------------------------------------------------

	private void showDataPicker(String errorMessage) {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);
		int pad = (int) dpToPx(24);
		layout.setPadding(pad, pad, pad, pad);

		TextView title = new TextView(this);
		title.setText("Jill of the Jungle game data needed");
		title.setTextColor(Color.WHITE);
		title.setTextSize(22);
		title.setGravity(Gravity.CENTER);
		layout.addView(title);

		TextView message = new TextView(this);
		// Android port change, not upstream -- wootbeer's own ask: "change the part inside
		// parenthesis to read '(.sha, .vcl, .mac, .dem, .ddt, jill.dma, all numbered files
		// 0-50)'." Replaces the old JILL1.CFG/JILL1.SHA/JILL1.VCL example-filenames wording
		// with a plain extension list, since by this point the actual required-files reference
		// (claude/required-game-files.md) covers every real filename in far more detail than a
		// one-line in-app message ever could -- this is just a quick heads-up for what kinds of
		// files the chosen folder needs to contain.
		message.setText("Select the folder that contains your own copy of the game (.sha, "
				+ ".vcl, .mac, .dem, .ddt, jill.dma, all numbered files 0-50).");
		message.setTextColor(Color.LTGRAY);
		message.setGravity(Gravity.CENTER);
		message.setPadding(0, (int) dpToPx(16), 0, (int) dpToPx(16));
		layout.addView(message);

		if (errorMessage != null) {
			TextView error = new TextView(this);
			error.setText(errorMessage);
			error.setTextColor(Color.rgb(255, 120, 120));
			error.setGravity(Gravity.CENTER);
			error.setPadding(0, 0, 0, (int) dpToPx(16));
			layout.addView(error);
		}

		Button chooseButton = new Button(this);
		chooseButton.setText("Choose Folder");
		chooseButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		// Android port change, not upstream -- wootbeer's own ask: "change the width of the Choose
		// Folder button like we did on the episode select screen, it doesn't need to stretch the
		// whole width of the screen." Same measure()/getMeasuredWidth()-plus-buffer technique
		// showEpisodeSelect() already uses for its own buttons (see that method's own buttonWidth
		// comment) -- addView(chooseButton) with no explicit LayoutParams was falling back to a
		// vertical LinearLayout's own default (MATCH_PARENT width, WRAP_CONTENT height), which is
		// what stretched it edge to edge; buttonLayoutParams() (this class's own helper, already
		// shared with showEpisodeSelect()) gives it an explicit wrap-content-sized width instead.
		chooseButton.measure(View.MeasureSpec.UNSPECIFIED, View.MeasureSpec.UNSPECIFIED);
		int chooseButtonWidth = chooseButton.getMeasuredWidth() + (int) dpToPx(16);
		layout.addView(chooseButton, buttonLayoutParams(chooseButtonWidth));

		setContentView(layout);
	}

	private void showCopyingProgress() {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);

		ProgressBar progressBar = new ProgressBar(this);
		layout.addView(progressBar);

		TextView text = new TextView(this);
		text.setText("Copying game data...");
		text.setTextColor(Color.WHITE);
		text.setGravity(Gravity.CENTER);
		text.setPadding(0, (int) dpToPx(16), 0, 0);
		layout.addView(text);

		setContentView(layout);
	}

	// --- UI: episode select --------------------------------------------------------------------

	/**
	 * Shows every episode's status (ready to play, or what's missing) and, for each ready one,
	 * a "Play Episode N" button that hands straight off to JillGameActivity/JillView (see the
	 * click handler below) -- real gameplay, not a placeholder.
	 */
	private void showEpisodeSelect(String statusMessage) {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		// Android port change, not upstream -- wootbeer's own ask: "remove some of the black from
		// the top of this screen so all the buttons will actually fit." Gravity.CENTER (both
		// axes) split any leftover vertical space evenly above AND below the whole stack
		// (title + up to three episode rows/buttons + Add More Episode Data) whenever it was
		// shorter than the screen -- CENTER_HORIZONTAL keeps everything centered left-to-right
		// but anchors it to the top instead, reclaiming that space as room for content instead
		// of wasting it as a black band above the title.
		layout.setGravity(Gravity.CENTER_HORIZONTAL);
		layout.setBackgroundColor(Color.BLACK);
		int pad = (int) dpToPx(24);
		// Android port change, not upstream -- same ask as the gravity change just above: a
		// smaller top inset than the left/right/bottom padding this screen already used, now
		// that dropping vertical centering means this top padding is the only thing holding the
		// title down from the very top of the screen.
		int topPad = (int) dpToPx(8);
		layout.setPadding(pad, topPad, pad, pad);

		TextView title = new TextView(this);
		// Android port change, not upstream -- wootbeer's own ask: "change the header to read 'Jill
		// of the Jungle Android'."
		title.setText("Jill of the Jungle Android");
		title.setTextColor(Color.WHITE);
		title.setTextSize(22);
		title.setGravity(Gravity.CENTER);
		layout.addView(title);

		if (statusMessage != null) {
			TextView status = new TextView(this);
			status.setText(statusMessage);
			status.setTextColor(Color.rgb(150, 220, 150));
			status.setGravity(Gravity.CENTER);
			status.setPadding(0, (int) dpToPx(12), 0, (int) dpToPx(4));
			layout.addView(status);
		}

		// Android port addition, not upstream -- wootbeer's own ask: "shrink the width of the
		// actual buttons down? make them all the same width, just a little over the width
		// needed to fit the 'add more episode data'." Built (but not yet added to layout) here,
		// ahead of the per-episode loop below, purely to measure its own natural wrap-content
		// width via measure()/getMeasuredWidth() -- its label is the longest of the four
		// buttons this screen can ever show, so that width (plus a small fixed buffer for "a
		// little over") becomes the shared width every button below uses via
		// buttonLayoutParams(), instead of each one wrapping to its own, visibly inconsistent,
		// shorter text. Still added to `layout` itself at its normal spot, after the loop, same
		// as before -- only built early.
		Button addMoreButton = new Button(this);
		addMoreButton.setText("Add More Episode Data");
		addMoreButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		addMoreButton.measure(View.MeasureSpec.UNSPECIFIED, View.MeasureSpec.UNSPECIFIED);
		final int buttonWidth = addMoreButton.getMeasuredWidth() + (int) dpToPx(16);

		for (final EpisodeSpec episode : EPISODES) {
			boolean ready = isEpisodeReady(episode);

			TextView row = new TextView(this);
			row.setGravity(Gravity.CENTER);
			row.setPadding(0, (int) dpToPx(12), 0, 0);
			layout.addView(row);

			if (ready) {
				row.setText("Episode " + episode.number + ": " + episode.title);
				row.setTextColor(Color.WHITE);

				Button playButton = new Button(this);
				playButton.setText("Play Episode " + episode.number);
				playButton.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View v) {
						// Hands off to JillGameActivity/JillView -- see those classes' own header
						// comments. The real engine now cares which episode's data to load (SHM.c's
						// shm_init() needs the right jillN.sha), so episode.number is passed through
						// as an Intent extra -- see JillGameActivity.EXTRA_EPISODE_NUMBER.
						Intent intent = new Intent(JillActivity.this, JillGameActivity.class);
						intent.putExtra(JillGameActivity.EXTRA_EPISODE_NUMBER, episode.number);
						startActivity(intent);
					}
				});
				layout.addView(playButton, buttonLayoutParams(buttonWidth));
			} else {
				List<String> missing = missingCoreFiles(episode);
				row.setText("Episode " + episode.number + ": " + episode.title
						+ " -- missing " + joinCommas(missing));
				row.setTextColor(Color.GRAY);
			}
		}

		// Android port change, not upstream -- wootbeer's own ask: "add a space between the 'add
		// more episode data' button and the one above it" and "reduce the height of the add
		// more episode data button so it's not as tall and matches the others." The old
		// addMoreButton.setPadding(0, dpToPx(20), 0, 0) call this replaced set PADDING, not
		// spacing -- padding pads a view's own content (making the button itself visibly
		// taller, exactly wootbeer's second complaint), it does nothing for the gap between it and
		// the view above it (his first complaint) in a plain LinearLayout, which only respects
		// a child's own MARGIN for that. A LayoutParams topMargin is the real fix for both at
		// once: no extra internal padding (so this button's own height now matches every Play
		// Episode button above it, which never had any custom padding to begin with), and a
		// genuine gap above it.
		LinearLayout.LayoutParams addMoreParams = buttonLayoutParams(buttonWidth);
		addMoreParams.topMargin = (int) dpToPx(20);
		layout.addView(addMoreButton, addMoreParams);

		setContentView(layout);
	}

	/** Android port addition, not upstream -- see showEpisodeSelect()'s own comment on
	 *  buttonWidth for why every button on that screen shares one explicit width instead of
	 *  each wrapping to its own text. A fresh LayoutParams instance per call, not one shared
	 *  instance reused across addView() calls -- LinearLayout expects (and can misbehave
	 *  without) each child having its own LayoutParams object. */
	private LinearLayout.LayoutParams buttonLayoutParams(int width) {
		return new LinearLayout.LayoutParams(width, LinearLayout.LayoutParams.WRAP_CONTENT);
	}

	private static String joinCommas(List<String> values) {
		StringBuilder sb = new StringBuilder();
		for (int i = 0; i < values.size(); i++) {
			if (i > 0) {
				sb.append(", ");
			}
			sb.append(values.get(i));
		}
		return sb.toString();
	}

	// --- SAF folder pick + copy --------------------------------------------------------------

	private void openFolderPicker() {
		if (Build.VERSION.SDK_INT < 21) {
			showDataPicker("This Android version can't select external files.");
			return;
		}
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		startActivityForResult(intent, REQUEST_CODE_OPEN_DATA_FOLDER);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == REQUEST_CODE_OPEN_DATA_FOLDER) {
			if (resultCode != RESULT_OK || data == null || data.getData() == null) {
				// User cancelled -- leave whichever screen was already showing (picker prompt
				// or episode select) so they can try again.
				return;
			}
			copyDataFromTree(data.getData());
		}
	}

	private void copyDataFromTree(final Uri treeUri) {
		showCopyingProgress();
		new Thread(new Runnable() {
			@Override
			public void run() {
				final String result = doCopyData(treeUri);
				runOnUiThread(new Runnable() {
					@Override
					public void run() {
						List<EpisodeSpec> ready = readyEpisodes();
						if (ready.isEmpty()) {
							showDataPicker(result != null ? result
									: "Didn't find any of Jill's episode files in that folder.");
						} else {
							showEpisodeSelect(result);
						}
					}
				});
			}
		}).start();
	}

	/**
	 * Runs on a background thread. Lists the picked folder's children ONCE -- real folders here
	 * can hold well over a hundred files across three episodes' worth of levels, music, save
	 * data, DOSBox junk, etc., so querying the SAF provider per-filename the way the old
	 * single-file GOTRES.DAT picker did would mean a query per candidate file instead of one
	 * for the whole folder -- then copies every file that matches Jill's manifest: each
	 * episode's core files plus every *.JNn level file found for it, and the shared JILL.DMA +
	 * *.DDT music pool. Returns a short human-readable summary of what was copied (shown as the
	 * status line on the episode select screen), or null if nothing matched at all.
	 */
	private String doCopyData(Uri treeUri) {
		Map<String, Uri> children;
		try {
			children = listChildren(treeUri);
		} catch (IOException e) {
			Log.i(TAG, "doCopyData: failed to list picked folder: " + e.getMessage());
			return "Couldn't read that folder: " + e.getMessage();
		}
		Log.i(TAG, "doCopyData: picked folder has " + children.size() + " files");

		//noinspection ResultOfMethodCallIgnored
		jillRootDir.mkdirs();
		List<String> summary = new ArrayList<>();

		// Shared files: JILL.DMA plus every *.DDT music track present.
		File sharedDir = new File(jillRootDir, SHARED_DIR);
		//noinspection ResultOfMethodCallIgnored
		sharedDir.mkdirs();
		int sharedCopied = 0;
		for (Map.Entry<String, Uri> entry : children.entrySet()) {
			String lowerName = entry.getKey();
			if (lowerName.equals(SHARED_DMA_FILE) || lowerName.endsWith(MUSIC_EXTENSION)) {
				if (copyOneFile(entry.getValue(), lowerName, new File(sharedDir, lowerName))) {
					sharedCopied++;
				}
			}
		}
		if (sharedCopied > 0) {
			summary.add(sharedCopied + " shared file" + (sharedCopied == 1 ? "" : "s"));
		}

		// Per-episode files: the named core files plus every level file for that episode's
		// extension, plus that episode's own attract-mode demo macro(s) -- see isDemoFile()'s own
		// comment for why these need their own separate match instead of just another extension
		// like level files above. wootbeer's own bug report: "the next menu item that doesn't work,
		// demo, it just opens the demo for a split second then goes back to main menu." Root
		// cause: JUNGLE.c's own demoname[] table (jn1demo.mac / jn2dem1.mac-jn2dem3.mac /
		// jn3dem1.mac-jn3dem3.mac) was never in this copy manifest at all -- every OTHER file
		// dodemo() needs (the demo's own *.JNn level board, loaded via loadboard() same as any
		// real level) was already covered by the isLevel check above, so the demo level rendered
		// for exactly one frame, then GAMECTRL.c's own playmac() call failed its open() (ENOENT,
		// since the file was never copied to the device at all) and returned silently with
		// macplay left at 0 -- playmac() has no error signal for a missing file, see that
		// function's own comment. dodemo()'s do-while loop reads that back as "already over"
		// on its very first iteration and returns to the main menu, which is exactly the "split
		// second" symptom: one real frame of the demo board, then an immediate, deterministic
		// bail with no actual macro playback ever happening.
		for (EpisodeSpec episode : EPISODES) {
			File epDir = new File(jillRootDir, episode.dirName());
			int copiedForEpisode = 0;
			boolean sawAnyFileForEpisode = false;
			for (Map.Entry<String, Uri> entry : children.entrySet()) {
				String lowerName = entry.getKey();
				boolean isCore = isCoreFile(episode, lowerName);
				boolean isLevel = lowerName.endsWith(episode.levelExtension);
				boolean isDemo = isDemoFile(episode, lowerName);
				/* Android port addition, not upstream -- the OTHER half of the attract-mode demo
				 * fix above turned out not to be the whole story. wootbeer's own follow-up report:
				 * Episode 1's demo now plays for real, but crashes "as soon as [Jill] touches
				 * down" landing a jump to grab some apples on the intro board. Logcat (with the
				 * new diagnostic line rexit() below now has) pinned it down exactly: rexit(1) --
				 * curlevel="0.dem" errno=2 (No such file or directory)". JOBJ.c's own
				 * msg_checkpt() (checkpoint objects, msg_touch) is what set that -- real, upstream
				 * per-level object data embeds a checkpoint at that exact landing spot whose own
				 * `inside` string is the literal continuation board filename "0.dem" (JOBJ.c's own
				 * `if (checkpoint->inside != NULL) strcpy(newlevel, checkpoint->inside);`, JUNGLE.c's
				 * own play() then does a plain loadboard(newlevel) with no sigil prefix at all --
				 * see that do-while loop's own comment). This is genuine, authentic 1992 level
				 * design, not a decoding bug: intro.jn1 (the attract board itself, demoboard[0] for
				 * Episode 1) is never walked by a real player, so its own checkpoint is free to
				 * lead into a SEPARATE, demo-only continuation board with its own ".dem" extension
				 * instead of a real ".jn1" playable level -- exactly the same idea as this file's
				 * own isDemoFile() macro files just above, one level up (a whole BOARD dedicated
				 * to attract-mode, not just the macro that drives it), and just as absent from this
				 * copy manifest before this fix. Unlike isDemoFile()'s own ".mac" files, JUNGLE.c's
				 * own demoboard[]/demoname[] tables (JUNGLE.c) never name any ".dem" file directly
				 * for ANY episode -- these are reached only indirectly, through whatever checkpoint
				 * data a given episode's own intro/demo board happens to embed, so there is no
				 * per-episode name prefix here the way "jn" + episode.number was for isDemoFile()
				 * (only Episode 1's checkpoint has been confirmed to reference one at all, as plain
				 * "0.dem" with no episode marker whatsoever). Copying every ".dem" file into EVERY
				 * episode's own directory, unconditionally, is the only safe way to make sure
				 * whichever episode(s) actually need one always find it -- a few harmless extra
				 * kilobytes duplicated into episodes that never reference them beats silently
				 * leaving another one of these out and hitting this exact crash again down the
				 * road. Not added to EpisodeSpec.coreFiles() for the same "opportunistic, not
				 * required" reasoning isDemoFile()/isLevel already established -- a copy missing
				 * one just means that one checkpoint (reachable only from attract-mode demo
				 * playback, never real gameplay) can't be crossed, not that the episode itself is
				 * unplayable. */
				boolean isDemoBoard = lowerName.endsWith(".dem");
				if (isCore || isLevel || isDemo || isDemoBoard) {
					sawAnyFileForEpisode = true;
					//noinspection ResultOfMethodCallIgnored
					epDir.mkdirs();
					if (copyOneFile(entry.getValue(), lowerName, new File(epDir, lowerName))) {
						copiedForEpisode++;
					}
				}
			}
			if (sawAnyFileForEpisode) {
				summary.add(copiedForEpisode + " file" + (copiedForEpisode == 1 ? "" : "s")
						+ " for episode " + episode.number);
			}
		}

		if (summary.isEmpty()) {
			return null;
		}
		return "Copied " + joinCommas(summary) + ".";
	}

	private static boolean isCoreFile(EpisodeSpec episode, String lowerName) {
		for (String coreFile : episode.coreFiles()) {
			if (coreFile.equalsIgnoreCase(lowerName)) {
				return true;
			}
		}
		return false;
	}

	/** True for one of this episode's own attract-mode demo macro files -- JUNGLE.c's own
	 *  demoname[] table, e.g. "jn1demo.mac" (episode 1) or "jn2dem1.mac"/"jn2dem2.mac"/
	 *  "jn2dem3.mac" (episode 2). Deliberately its own check rather than folding ".mac" into
	 *  episode.levelExtension the way isLevel above matches ".jn1"/".jn2"/".jn3": unlike the level
	 *  extension, ".mac" is the SAME suffix for all three episodes, so an extension-only match run
	 *  per-episode (this loop iterates once per EpisodeSpec) would copy every episode's demo files
	 *  into every OTHER episode's own directory too. JUNGLE.c's own demoname[] entries all start
	 *  with "jn" + the episode number ("jn1demo.mac", "jn2dem1.mac", "jn3dem1.mac", ...), so that
	 *  prefix is what keeps each episode's demos going only into that episode's own directory --
	 *  matches the real on-disk names exactly, case-insensitively, same as every other match in
	 *  this class. Not added to EpisodeSpec.coreFiles(): these are opportunistic, like the level
	 *  files above, not required for isEpisodeReady() -- a copy missing its demo files should still
	 *  let the player play the actual game, just with a non-functional Demo menu item (same
	 *  "opportunistic, not required" precedent isLevel's own class comment already established for
	 *  *.JNn level files). */
	private static boolean isDemoFile(EpisodeSpec episode, String lowerName) {
		return lowerName.startsWith("jn" + episode.number) && lowerName.endsWith(".mac");
	}

	/**
	 * Lists every document directly under the picked tree, keyed by lowercased display name --
	 * one SAF query for the whole folder rather than one per candidate filename.
	 */
	private Map<String, Uri> listChildren(Uri treeUri) throws IOException {
		Map<String, Uri> children = new HashMap<>();
		String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
		Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeDocId);
		Cursor cursor = getContentResolver().query(childrenUri, new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null);
		if (cursor == null) {
			throw new IOException("folder query returned nothing");
		}
		try {
			while (cursor.moveToNext()) {
				String docId = cursor.getString(0);
				String name = cursor.getString(1);
				if (name == null) {
					continue;
				}
				Uri childUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
				children.put(name.toLowerCase(Locale.US), childUri);
			}
		} finally {
			cursor.close();
		}
		return children;
	}

	/**
	 * Copies one file and verifies the copy actually completed against the source's own
	 * reported size (when the provider reports one) -- the same real bug GOTRES.DAT's copy loop
	 * had before that check existed: an interrupted SAF read can silently produce a short file
	 * that a later existence-only check would accept as good. Logs and skips (does not throw)
	 * on failure, so one bad file doesn't abort copying the rest of the folder.
	 */
	private boolean copyOneFile(Uri sourceUri, String displayName, File destination) {
		File temp = new File(destination.getParentFile(), destination.getName() + ".tmp");
		InputStream in = null;
		OutputStream out = null;
		try {
			long expectedSize = queryDocumentSize(sourceUri);
			in = getContentResolver().openInputStream(sourceUri);
			if (in == null) {
				Log.i(TAG, "copyOneFile: could not open " + displayName);
				return false;
			}
			out = new FileOutputStream(temp);
			byte[] buffer = new byte[64 * 1024];
			long totalCopied = 0;
			int read;
			while ((read = in.read(buffer)) != -1) {
				out.write(buffer, 0, read);
				totalCopied += read;
			}
			out.flush();
			out.close();
			out = null;
			in.close();
			in = null;

			if (expectedSize >= 0 && totalCopied != expectedSize) {
				Log.i(TAG, "copyOneFile: " + displayName + " incomplete, got " + totalCopied
						+ " of " + expectedSize + " bytes");
				//noinspection ResultOfMethodCallIgnored
				temp.delete();
				return false;
			}
			if (!temp.renameTo(destination)) {
				Log.i(TAG, "copyOneFile: renameTo(" + destination + ") failed");
				return false;
			}
			return true;
		} catch (IOException e) {
			Log.i(TAG, "copyOneFile: " + displayName + ": " + e.getMessage());
			//noinspection ResultOfMethodCallIgnored
			temp.delete();
			return false;
		} finally {
			if (in != null) {
				try {
					in.close();
				} catch (IOException ignored) {
				}
			}
			if (out != null) {
				try {
					out.close();
				} catch (IOException ignored) {
				}
			}
		}
	}

	private long queryDocumentSize(Uri documentUri) {
		Cursor cursor = getContentResolver().query(documentUri,
				new String[]{DocumentsContract.Document.COLUMN_SIZE}, null, null, null);
		if (cursor == null) {
			return -1;
		}
		try {
			if (cursor.moveToFirst() && !cursor.isNull(0)) {
				return cursor.getLong(0);
			}
			return -1;
		} finally {
			cursor.close();
		}
	}

	// --- Lifecycle / misc --------------------------------------------------------------------

	@Override
	protected void onPause() {
		super.onPause();
		// This Activity itself has no native engine or render thread of its own to pause -- it's
		// just the SAF picker/episode-select screen. JillGameActivity is where a real game
		// session's onPause()/onResume() (render-thread pause, background-music pause) actually
		// lives -- see that class's own onPause()/onResume() comments.
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
	}

	@SuppressWarnings("unused")
	private float dpToPx(float dp) {
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		return dp * (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	@SuppressWarnings("unused")
	private float pxToDp(float px) {
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		return px / (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	/**
	 * Enables immersive mode, hiding navigation controls
	 */
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

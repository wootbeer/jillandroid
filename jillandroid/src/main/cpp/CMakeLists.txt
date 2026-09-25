cmake_minimum_required(VERSION 3.4.1)

# Android port note (this file, not upstream): this jillandroid module used to also build a "got"
# SHARED library here (got_stub.c/got_main.c/got_controls.c/got_hud.c/got_panel.c/got_font.c/
# got_menu.c/got_title.c/got_item.c/got_script.c/got_dialogue.c/got_story.c/res_man.c/lzss.c, plus
# the sibling modex/ subdirectory one level up from this module) -- leftover scaffolding copied in
# when this module was first bootstrapped from wootbeer's separate God of Thunder Android port's
# own template (the same reason wootbeer.jillandroid.JillActivity, this app's shared launcher, was
# still named/packaged as GotActivity in wootbeer.gotandroid until this project's pre-release
# cleanup renamed and moved it -- see that class's own header comment). That "got" library was
# never loaded by any
# Java code in this module -- JillView.java's System.loadLibrary("jillhost" + episodeNumber) call
# is the only native library this app ever actually loads -- so it was dead code, unrelated to
# Jill's own gameplay, that nonetheless got compiled and packaged into every shipped Jill of the
# Jungle APK simply because nothing ever excluded it from this file's build. Removed once this was
# noticed during a licensing-file audit (see this project's own COPYING, which no longer needs to
# account for it here). The got_*.c/.h files, res_man.c/.h, lzss.c/.h, and the sibling modex/
# directory this repo used to still carry (one level up from this module) were real, substantial
# source (that God of Thunder Android port's own engine) that was never referenced by this
# CMakeLists.txt or by settings.gradle and never needed to build or run Jill. Pre-release cleanup:
# wootbeer confirmed that source is kept safely elsewhere (its own separate repo) and deleted all
# of it from this repo -- there is no got_*/res_man/lzss/modex source left here to reference.

find_library(log-lib log)
find_library(GLESv1_CM-lib GLESv1_CM)
find_library(EGL-lib EGL)
find_library(android-lib android)

# Jill of the Jungle's own native host layer -- jill/jill_jni_bridge.c's jillMain() drives
# JUNGLE.c's real, fully-ported game engine through jill/HOSTANDROID.c's real host_open()/
# host_pump()/host_present() calls (see jill_jni_bridge.c's own header comment for exactly how
# jillMain() got wired to JUNGLE.c's real main()). Kept as its own SHARED library/CMake target --
# JillActivity (wootbeer.jillandroid), this app's SAF picker/episode-select screen, has no native
# engine of its own to link against at all; JillGameActivity/JillView are the only classes that
# ever load a native library here. jill/RECOVERY.H, jill/GR.H,
# jill/HOSTSDL.H and jill/HOSTAUDIO.H are vendored unchanged from the Jill of the Jungle
# Reconstructed decomp (GPLv3) -- see jill/HOSTANDROID.c's own header comment for what it
# implements against them.
#
# jill/HOSTANDROID.c is lowercase, unlike the decomp's own uppercase-.C convention (RECOVERY.H,
# GR.H, HOSTSDL.H, HOSTAUDIO.H above) -- this file has no real DOS-era counterpart to match (it's
# this port's own from-scratch Android host layer, not a recovered original), and CMake's default
# extension-to-language table treats a capital *.C as C++, which silently C++-name-mangled every
# symbol it defined when it was still named HOSTANDROID.C -- jill_jni_bridge.c (correctly compiled
# as plain C) then couldn't find them at link time ("undefined symbol: jillhost_set_surface_size",
# the linker itself suggesting the real cause: "did you mean to declare ... as extern \"C\"?").
# set_source_files_properties(... LANGUAGE C) is the fix the upstream decomp itself uses for this
# same gotcha on ITS real uppercase .C files (see its own src/CMakeLists.txt comment) -- tried here
# first, but didn't take effect even after a full clean rebuild on this project's CMake/AGP
# combination (worth revisiting if a future genuinely-recovered file, e.g. JOBJ.C once it's linked
# in, hits the same issue and can't just be renamed). Renaming sidesteps the question entirely.
#
# Android port note (this file, not upstream): this used to be a single add_library(jillhost ...)
# built unconditionally with JILL_EP1=1 (see the big comment above the object-system files below,
# still accurate about WHY three episode variants are needed -- just no longer about there being
# only one target). That was fine while Episode 1 was the only thing being proven out end to end,
# but it silently made Episodes 2/3 unplayable: JillActivity.java's asset picker and
# JillGameActivity/JillView already pass through a real per-episode directory and shape-file path
# for any of the three (JillGameActivity's own EXTRA_EPISODE_NUMBER), jill_jni_bridge.c's
# jill_run_game() already chdir()s into the right one -- but every relative filename the engine
# then opens (data_path()'s own JILL_CONFIG_FILE/JILL_SOUND_FILE, loadboard()'s own
# JILL_INTRO_LEVEL/JILL_START_LEVEL, all from EPISODE.H) was always Episode 1's, because JILL_EP1
# was the only macro ever compiled in. Selecting Episode 2 or 3 chdir()'d into a real ep2/ep3
# directory that has no jill1.cfg/jill1.sha/intro.jn1/map.jn1 in it at all, so shm_init()/
# CONFIG.c's doconfig() etc. failed to open anything and the screen just stayed blank -- confirmed
# by tracing exactly that chdir()-then-open() sequence, not observed as a crash (nothing SIGSEGVs;
# the engine just never gets past failing to open its own hardcoded-wrong filenames).
#
# The real fix, exactly as this file's own object-system comment already called out as the
# eventual next step once Episode 1 was proven out: three separate .so targets below
# (jillhost1/jillhost2/jillhost3), one build of the identical source list per JILL_EPn, so each
# one's EPISODE.H macros -- and the handful of real per-episode gameplay branches already sitting
# in JPLAYER.c/JUNGLE.c/JOBJ.c/JOBJ2.c/JMAN.c (grep JILL_EP1/JILL_EP2/JILL_EP3 in jill/*.c) --
# resolve correctly for that episode. JillView.java picks which one to System.loadLibrary() based
# on the episode number JillGameActivity already threads through (see that file's own comment).
# Safe to have all three built and packaged into the one APK (no ABI-split/target restriction in
# build.gradle's externalNativeBuild block) even though only one is ever loaded per process today
# -- JillGameActivity.onBackPressed() is still a deliberate no-op (see its own comment: "no path
# back to the episode picker screen for now at all"), so a process can never have more than one of
# these three .so's loaded at once, which is what keeps this safe: all three export the same
# JNI-implicit symbol names (Java_wootbeer_jillandroid_JillView_jillMain etc.), and loading two of
# them into one process at the same time would leave which one's jillMain() actually answers a
# given call up to JNI's own search order across loaded libraries -- undefined in practice. If
# real "return to episode picker, then pick a DIFFERENT episode" navigation is ever built, revisit
# this (e.g. force a process restart on episode switch, same as the CMake/JillView split here
# already assumes) rather than assuming it still holds.
set(JILLHOST_SOURCES
		# descore/audio_opl/ -- real-time AdLib/CMF music playback, generic across any "descore"-shell
		# port that needs it, not folded into jill/ (see descore_cmf.h's own header comment: no
		# game-engine dependency at all, plain uint8_t/int16_t in and out). opl3.c/opl3.h are Nuked
		# OPL3 (Copyright 2013-2020 Nuke.YKT, LGPL 2.1-or-later), vendored byte-for-byte unchanged --
		# chosen for its reverse-engineered, cycle-accurate reproduction of the real Yamaha chip's own
		# behavior over other OPL2 emulator options, run here purely in OPL2-compatible mode (register
		# 0x105, OPL3 mode enable, is never written -- see descore_cmf.c's own header comment). LGPL
		# 2.1+ combines freely with this project's own GPLv3 base as one work, and this project already
		# ships its own source, so LGPL's "make source available" obligation costs nothing extra --
		# opl3.c/opl3.h ARE that source, sitting right here. descore_cmf.c/.h are this port's own new
		# code (also released LGPL 2.1+, matching what it's derived from -- see its own header comment
		# for the full provenance story): a from-scratch CMF file parser/sequencer driving opl3.c's own
		# OPL3_WriteReg()/OPL3_GenerateResampled(), informed by AdPlug's own cmf.cpp (itself LGPL 2.1+)
		# for its real-driver-accurate tables/algorithms, restructured into a pull-based renderer
		# (descore_cmf_render() produces N audio frames on demand) instead of AdPlug's own host-
		# scheduled tracker-frontend shape, to suit HOSTANDROID.c's streaming-AudioTrack use below.
		descore/audio_opl/opl3.c
		descore/audio_opl/descore_cmf.c
		# descore/touch/descore_touch.c -- generic on-screen touch-button dispatch + gamepad-connected
		# tracking + Touch Options Scaling/Opacity slider infrastructure + the translucent-quad GL draw
		# routine, shared across any descore-shell Android port. Pulled out of jill_controls.c during
		# this project's pre-release cleanup, at wootbeer's own request -- see that file's own header
		# comment for the full rationale and exactly what stayed behind in jill_controls.c (button
		# geometry/layout and the game's own key-delivery primitive, deliberately NOT unified across
		# games).
		descore/touch/descore_touch.c
		jill/HOSTANDROID.c
		jill/jill_jni_bridge.c
		# jill_controls.c -- Jill's own on-screen D-pad/JUMP/THROW/PAUSE button layout and the JNI
		# entry points Java calls, now a thin adapter on top of descore/touch/descore_touch.c above
		# (which owns the actual touch-dispatch/gamepad-tracking/Touch-Options-slider/GL-draw
		# mechanics) -- Android port addition, not upstream. Originally ported from the God of Thunder
		# Android port's own got_controls.c, scoped to Jill's own much smaller 2-action gameplay
		# (JUMP/THROW, not GoT's Fire/Magic/Select) -- see that file's own header comment for the full
		# rationale, including why it calls jillhost_notify_key() (HOSTANDROID.c) directly rather than
		# needing a GoT-style key_flag[] array of its own.
		jill/jill_controls.c
		# The real decomp engine, ported (not rewritten) -- see each file's own header comment for
		# exactly what changed from upstream (mechanically: MSVC <io.h> -> POSIX; PLATFORM.c also
		# gains jill_dma_path's storage, JINFO.c's initinfo() reads it). Renamed .C -> .c (base
		# name unchanged) for the same reason as jill/HOSTANDROID.c above.
		jill/PLATFORM.c
		jill/JINFO.c
		# GR.C/GRASM.C/GAME-GRL.C -- the real VGA-emulation framebuffer/palette/drawing primitives
		# (jill_video, plot()/linex()/clrvp()/pageflip()/gr_present_page(), the real default VGA
		# palette) -- byte-identical to upstream, see jill/GR.c's own header comment. jill/GRL.c is
		# the decomp's GAME/GRL.C, flattened into this flat jill/ folder (see that file's own
		# comment). SHM.c is the shape-table loader (jill1.sha etc.) -- POSIX-ported like
		# PLATFORM.c/JINFO.c, see that file's own header comment, including for the temporary
		# rexit() stand-in this pulls in (HOSTANDROID.c).
		jill/GR.c
		jill/GRASM.c
		jill/GRL.c
		jill/SHM.c
		# The real object system -- JOBJ.c has initobjinfo() (the real per-object-kind
		# xl/yl/flags/table/score/msg tables) plus most of the per-object-kind msg_* draw/update/
		# touch handlers; JOBJ2.c/JOBJ3.c are the rest of those handlers (JOBJ3.c is specifically
		# msg_block(), the board-tile message dispatcher) -- upstream splits these three ways for
		# its own reasons (probably just DOS-era file-size limits), not by any meaningful
		# boundary, so they're kept as three files here too rather than merged. JPLAYER.c is the
		# real player object (input, state machine, drawing, inventory, scrolling). JMAN.c is the
		# object manager (addobj()/killobj()/trymove()/justmove()/crawl()/fishdo() etc.). All four
		# are pure game logic with no DOS/hardware dependency -- see each file's own header
		# comment for the couple of small _itoa()->snprintf() swaps that were needed (MSVC-only,
		# not on Android/bionic).
		#
		# These need EXACTLY ONE of JILL_EP1/JILL_EP2/JILL_EP3 defined (EPISODE.H #errors
		# otherwise) -- upstream itself compiles three entirely separate executables for this,
		# since a handful of real gameplay branches (coin-pushing physics, a couple of enemy touch
		# behaviors) differ per episode. This app currently has ONE native library shared across
		# all 3 episodes (episode choice is a runtime .sha path, not a build variant), so for now
		# JILL_EP1 is defined unconditionally below -- Episodes 2/3 will build and mostly run
		# correctly against Episode 1's version of those few branches, not a fully faithful port
		# for them yet. Revisit (e.g. three jillhost1/jillhost2/jillhost3 targets, one .so each,
		# JillView.java picking which to load) once Episode 1 itself is proven out end to end.
		jill/JOBJ.c
		jill/JOBJ2.c
		jill/JOBJ3.c
		jill/JPLAYER.c
		jill/JMAN.c
		# WIN.c is the window/dialog-box drawing and text layer (defwin()/drawwin()/wprint()/
		# fontcolor() etc.) -- a hard link dependency of JOBJ.c's msg_checkpt/msg_text6/msg_text8/
		# msg_score, plus JUNGLE.c's own savegame() text-entry field. See that file's own header
		# comment for why its wgetkey()/winput() busy-wait is safe (HOSTANDROID.c's real clock-tick
		# thread) and reachable (savegame() calls winput() for real) today.
		jill/WIN.c
		# KEYBOARD.c/KEYINTR.c are the real keyboard layer -- already built on the same host_*
		# abstraction HOSTANDROID.c implements, so this is a direct drop-in that retires
		# HOSTANDROID.c's own former temporary k_read() stand-in (see jill/KEYBOARD.c's own header
		# comment). MUSIC.c/MUSINTR.c/UNKNOWN.c/JVOL.c are the sound/music layer (snd_play() etc.,
		# real gameplay logic calls this) -- already built on HOSTAUDIO.H's own host_audio_*
		# abstraction, same reasoning; HOSTANDROID.c's own real backend behind that abstraction
		# (Nuked-OPL3 CMF music, real digitized VOC playback) came later, see that file's own header
		# comment. MUSINTR.c is MUSIC.c's own sound-timer interrupt handler (spkr_intr(), called
		# by MUSIC.c's testintr()) -- pure logic over MUSIC.c's own globals, the real PC-speaker
		# hardware calls already commented out by upstream itself (see that file's own header
		# comment). All six are link dependencies of the object system above (JOBJ.c/JOBJ2.c call
		# snd_play(); WIN.c/JOBJ.c call k_pressed() transitively via wgetkey() -- unreachable but
		# still needs to link).
		jill/KEYBOARD.c
		jill/KEYINTR.c
		jill/MUSIC.c
		jill/MUSINTR.c
		jill/UNKNOWN.c
		jill/JVOL.c
		# GAMECTRL.c -- the real control/macro-record layer (checkctrl()/checkctrl0()/getkey(),
		# the demo-macro record/playback format, gc_init()/gc_exit()) -- see that file's own header
		# comment for its small POSIX-port call-site changes (mechanically the same DOSIO.H ->
		# <fcntl.h>/<unistd.h> swap PLATFORM.c/JINFO.c/SHM.c/MUSIC.c already got) and for which of
		# jill_jungle_stubs.c's placeholders it retires. Its own readspeed()/checkctrl0() are the
		# first ported code to actually rely on *myclock ticking for real (HOSTANDROID.c's new
		# timer thread, added as this port's own hard prerequisite).
		jill/GAMECTRL.c
		# CONFIG.c -- the real options/config layer (cfg_getpath()/cfg_init()/doconfig()). See that
		# file's own header comment: two small MSVC-helper call-site swaps (_strupr -> a local
		# jill_strupr() loop, _ltoa -> snprintf(), same spirit as the project's existing
		# _itoa()->snprintf() swaps), and a note on why doconfig()'s big configuration-menu block
		# doesn't need porting yet (upstream's own "#if 0 // jmarshall" already makes it dead code
		# on the reference build too). Retires jill_jungle_stubs.c's nosnd placeholder.
		jill/CONFIG.c
		# COPYFILE.c/PIXWRITE.c -- both small, both the same DOSIO.H -> POSIX / _itoa()->snprintf()
		# swaps already used throughout this project (see each file's own header comment).
		# copyfile() is the real save-game duplication helper (JUNGLE.C's save/load path, not yet
		# ported, is its only caller); pixwrite() is a debug screenshot cheat (also only reachable
		# from JUNGLE.C's not-yet-ported menu) -- its own header comment notes upstream's DOS-style
		# "\screenN.RAW" absolute path is left as-is (harmless on POSIX, just an odd relative
		# filename) since this is unreachable dev-only code, same reasoning as DESIGN.C's design()
		# being left unported for now.
		jill/COPYFILE.c
		jill/PIXWRITE.c
		# JUNGLE.c -- the real ~1660-line game loop itself: level load/save, the save-game encode/
		# decode helpers, text-message paging, the demo/macro playback wiring, the full menu system
		# (jmenu()/domenu()/dodemo()/pageview()/printhi()), and main() (not called from anywhere
		# yet -- jill_jni_bridge.c's test harness is unchanged by this stage; wiring a real entry
		# point into it is later work). See that file's own header comment for its five real
		# changes (SDL.h drop, DOSIO.H->POSIX, _itoa()/_ltoa()->snprintf(), _stricmp()/_strnicmp()->
		# strcasecmp()/strncasecmp(), and the design() stand-in below) -- everything else is
		# byte-identical logic. This is also what retired jill_jungle_stubs.c entirely: every
		# placeholder that file ever held (gamevp/statvp/cmdvp, botvp/tempvp, scrnxs/scrnys, bd,
		# newlevel/curlevel, botmsg/botcol/bottime, inv_shape, putbotmsg()/dotextmsg()/drawcell()/
		# drawstats()/loadboard()) gets its real definition here -- that file is deleted (no longer
		# listed below; the file itself can be removed from the tree, it's no longer built).
		jill/JUNGLE.c
		# TEMPORARY -- jill_design_stub.c: a single no-op stand-in for design() (DESIGN.C's own
		# level editor, reachable only via jmenu()'s hidden dev key '5'). See that file's own header
		# comment: DESIGN.C is dev-only tooling, out of scope for this port for now, same call made
		# when JUNGLE.C's dependency list was first scoped. Removal trigger: a duplicate-symbol link
		# error once DESIGN.C is properly ported.
		jill/jill_design_stub.c
)

# The real object system (JOBJ.c/JOBJ2.c/JOBJ3.c/JPLAYER.c/JMAN.c) needs exactly one of
# JILL_EP1/JILL_EP2/JILL_EP3 defined -- see the big comment above JILLHOST_SOURCES for why this is
# now three separate targets, one per episode, instead of the one "jillhost" target this used to
# be (JillView.java's own comment on its System.loadLibrary() call has the Java side of this).
add_library(jillhost1 SHARED ${JILLHOST_SOURCES})
add_library(jillhost2 SHARED ${JILLHOST_SOURCES})
add_library(jillhost3 SHARED ${JILLHOST_SOURCES})

target_compile_definitions(jillhost1 PRIVATE JILL_EP1=1)
target_compile_definitions(jillhost2 PRIVATE JILL_EP2=1)
target_compile_definitions(jillhost3 PRIVATE JILL_EP3=1)

foreach(jillhost_target jillhost1 jillhost2 jillhost3)
	target_link_libraries(${jillhost_target} ${log-lib})
	target_link_libraries(${jillhost_target} ${GLESv1_CM-lib})
	target_link_libraries(${jillhost_target} ${EGL-lib})
	target_link_libraries(${jillhost_target} ${android-lib})
endforeach()

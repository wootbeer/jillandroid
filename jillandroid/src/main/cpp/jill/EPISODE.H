/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code.

===========================================================================
*/

#ifndef JILL_EPISODE_H
#define JILL_EPISODE_H

/* Android port note (this file, not upstream): Episode 1's own JILL_CONFIG_FILE/JILL_SHAPE_FILE/
 * JILL_SOUND_FILE were spelled in upper case below ("JILL1.CFG" etc.) -- harmless on DOS/Windows'
 * case-insensitive filesystems, but these get concatenated (jill/JUNGLE.c's own data_path()) onto
 * the real per-episode asset directory chdir()'d into from jill_jni_bridge.c's jill_run_game(),
 * where they must match the real files JillActivity.java's SAF copier writes on Android's real
 * case-SENSITIVE filesystem -- which are lowercase for every episode (see that file's own
 * EPISODES table: "jill1.cfg"/"jill1.sha"/"jill1.vcl"). Episodes 2 and 3 below were already spelled
 * lowercase upstream and already matched; only Episode 1's three macros are changed here, to the
 * same lowercase spelling, matching the real on-disk names -- a data-only fix, no logic involved. */

/* Every game target must select exactly one original Jill episode. */
#if (defined(JILL_EP1) + defined(JILL_EP2) + defined(JILL_EP3)) != 1
#  error Exactly one of JILL_EP1, JILL_EP2, or JILL_EP3 must be defined
#endif

#if defined(JILL_EP1)

#  define JILL_EPISODE_NUMBER       1
#  define JILL_PROGRAM_NAME         "JILL1"
#  define JILL_GAME_TITLE           "Jill of the Jungle"
#  define JILL_WINDOW_TITLE         "Jill of the Jungle - SHAREWARE"
#  define JILL_CONFIG_FILE          "jill1.cfg"
#  define JILL_SHAPE_FILE           "jill1.sha"
#  define JILL_SOUND_FILE           "jill1.vcl"
#  define JILL_INTRO_LEVEL          "intro.jn1"
#  define JILL_START_LEVEL          "map.jn1"
#  define JILL_START_LEVEL_NUMBER   127
#  define JILL_MENU_MUSIC           "funky.ddt"
#  define JILL_SAVE_PREFIX          "jn1save"

#elif defined(JILL_EP2)

#  define JILL_EPISODE_NUMBER       2
#  define JILL_PROGRAM_NAME         "JILL2"
#  define JILL_GAME_TITLE           "Jill Goes Underground"
#  define JILL_WINDOW_TITLE         "Jill Goes Underground"
#  define JILL_CONFIG_FILE          "jill2.cfg"
#  define JILL_SHAPE_FILE           "jill2.sha"
#  define JILL_SOUND_FILE           "jill2.vcl"
#  define JILL_INTRO_LEVEL          "intro.jn2"
#  define JILL_START_LEVEL          "0.jn2"
#  define JILL_START_LEVEL_NUMBER   0
#  define JILL_MENU_MUSIC           "peaks.ddt"
#  define JILL_SAVE_PREFIX          "jn2save"

#elif defined(JILL_EP3)

#  define JILL_EPISODE_NUMBER       3
#  define JILL_PROGRAM_NAME         "JILL3"
#  define JILL_GAME_TITLE           "Jill Saves The Prince"
#  define JILL_WINDOW_TITLE         "Jill Saves The Prince"
#  define JILL_CONFIG_FILE          "jill3.cfg"
#  define JILL_SHAPE_FILE           "jill3.sha"
#  define JILL_SOUND_FILE           "jill3.vcl"
#  define JILL_INTRO_LEVEL          "intro.jn3"
#  define JILL_START_LEVEL          "map.jn3"
#  define JILL_START_LEVEL_NUMBER   0
#  define JILL_MENU_MUSIC           "wild.ddt"
#  define JILL_SAVE_PREFIX          "jn3save"

#endif

#endif

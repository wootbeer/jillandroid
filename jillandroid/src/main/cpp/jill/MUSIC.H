/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code ("Jill of the Jungle Reconstructed").

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

#ifndef JILL_MUSIC_H
#define JILL_MUSIC_H

#include "RECOVERY.H"

extern word musicflag, vocflag;
extern word soundf;
/* Android port addition, not upstream -- wootbeer's own ask: "replace the current sound/music toggle
 * with just a sub-menu... the same kind of toggle and sliders we have in god of thunder." soundf
 * above already only ever gated sound EFFECTS (snd_play()'s own `if (vocflag && soundf)` below) --
 * music had no live on/off of its own at all before this (sb_playtune()'s only gate was the
 * hardware-detection musicflag, never toggled once set at startup), so jill_music_enabled is a
 * genuinely new capability, not just a rename. Declared here, right alongside soundf, for the exact
 * same reason soundf itself lives here: MUSIC.c's own snd_play()/sb_playtune() are the only real
 * gates for these, and JUNGLE.c's pausemenu() (the new SOUND/MUSIC submenu) is the only writer --
 * same two-file shape soundf's own toggle already established.
 *
 * jill_sound_gain/jill_music_gain are the submenu's own two gain sliders -- wootbeer's own ask again:
 * "each one will be an option with a slider... to set the audio/gain level... the current sound and
 * music level will be the max, and it should just go down to zero from there." Step indices (0-
 * JILL_SOUND_GAIN_STEPS / 0-JILL_MUSIC_GAIN_MAX_STEP), not a raw float, same "clean 10% increments"
 * reasoning the God of Thunder Android port's own s_sound_gain/s_music_gain (got_menu.c) already
 * established -- see JILL_SOUND_GAIN_STEPS's own comment just below for where each is actually
 * applied. jill_music_gain's own range goes past 100% (its own max step is 150%, not 100%) -- wootbeer,
 * after trying the normal range: "the music does seem a little quiet so maybe there is some headroom
 * for some gain on it if the player desires?" Real per-call VOC volume (0-127) leaves no headroom of
 * its own to multiply past unity gain (and Android's own AudioTrack.setVolume() is hard-capped to
 * 1.0 regardless), so sound-effects gain stays a plain 0-100% slider matching GoT's own precedent
 * exactly; only music -- synthesized in real time as raw PCM this port already fully controls before
 * it ever reaches an AudioTrack -- can actually be amplified past its own original volume without
 * relying on the OS to allow it. See HOSTANDROID.c's own jillhost_render_music() comment for where
 * that extra headroom is actually applied. */
extern word jill_sound_gain, jill_music_gain, jill_music_enabled;
/* 10 steps of 10% each, 0-100% -- sound-effects gain's own full range, and the "normal" (0-100%)
 * portion of music gain's own wider range too. Must stay numerically in sync with HOSTANDROID.c's
 * own identical #define (that file deliberately doesn't include this header -- see its own header
 * comment on staying decoupled from JILL.H/the rest of the game engine -- so it keeps its own
 * narrow copy instead), same "hand-audited cross-file constant" policy the God of Thunder Android
 * port's own GAMEPAD_NUM_ACTIONS comment (got_menu.c) already established. */
#define JILL_SOUND_GAIN_STEPS 10
/* Music gain's own extra headroom steps -- 15 steps of 10% each, 0-150%, vs. sound's own 0-100%.
 * Step 10 (JILL_SOUND_GAIN_STEPS) is still 100%/"current level" either way -- the same default both
 * sliders start at -- steps 11-15 are the new headroom past that. Same cross-file sync note as
 * JILL_SOUND_GAIN_STEPS just above. */
#define JILL_MUSIC_GAIN_MAX_STEP 15
extern word soundoff, makesound;
extern char *textmsg;
extern word textmsglen;

/* Shared with the recovered MUSINTR.ASM interrupt body. */
extern word *freq, *dur;
extern word oldfreq, clockrate, soundlen, soundptr;
extern word clockcount, soundcount;

void getvoc(int sample);
void snd_init(char *library_name);
void snd_play(int priority, int sound);
void snd_do(void);
void text_get(int index);
void snd_exit(void);
void sb_update(void);
int sb_playing(void);
void sb_shutup(void);
void sb_playtune(char *filename);
void sampadd1(int instrument, int length, int duration, int note);
void soundadd1(int priority, char *sequence);
void soundstop(void);
void timerset(int timer, int mode, unsigned divisor);
void sampadd(int instrument, int length, int duration, int note);
void soundadd2(int priority, char *sequence);
void soundadd(int priority, char *sequence);
void testintr(void);
void spkr_intr(void);
void WorxBugInt8(void);

#endif

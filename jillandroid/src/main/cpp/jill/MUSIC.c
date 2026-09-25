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

/*
 * Android port note (this file, not upstream): same shape as PLATFORM.c's/JINFO.c's/SHM.c's
 * own ports -- the only real change is swapping MSVC io.h's DOS file functions for POSIX
 * (<fcntl.h>/<unistd.h>: open/read/lseek/close, O_RDONLY -- there's no O_BINARY on
 * Android/bionic; POSIX has no distinct text/binary file modes to begin with, so it's simply
 * dropped rather than mapped to anything). Every byte read and the sound-effect/music logic
 * itself are unchanged. Renamed MUSIC.C -> MUSIC.c for the same CMake gotcha as jill/GR.c
 * (see that file's own header comment).
 *
 * snd_do() opens its PC-speaker sound-effect data file and snd_init() the caller's vocflag-driven
 * sound-effects library, both by a bare relative filename -- as predicted right here when this
 * file was first ported, that became live the moment JUNGLE.C wired snd_init()/snd_do() in for
 * real (jill_jni_bridge.c's jill_run_game()). snd_init()'s own path comes in as a parameter
 * (JUNGLE.c's main() already resolves it against the real per-episode directory via data_path(),
 * same as the shape file -- see that file's own comment), so it needed nothing further.
 *
 * snd_do()'s own open("AUDIO.EPC", O_RDONLY) below got two real changes, found in that order on a
 * real device. First, the same lowercase fix as EPISODE.H's own Episode 1 macros: upstream's own
 * literal was upper case, harmless on DOS/Windows' case-insensitive filesystems but not here.
 * Second, and more real: upstream's own `if (handle == -1) rexit(155);` right after it is only
 * ever reached on a real DOS/DOSBox system when NO Sound Blaster was detected (this whole branch
 * is snd_do()'s `else` -- the `if (vocflag)` branch above it is what runs when one was, and a real
 * or DOSBox-emulated Sound Blaster is effectively always there) -- so upstream could safely treat
 * a missing AUDIO.EPC as a real installation error. This fix predates real Android audio output: at
 * the time it was made, HOSTANDROID.c's own host_audio_* functions were all stubbed to report "not
 * available", so vocflag was unconditionally false and THIS branch ran on every single launch, not
 * just a rare no-sound-card case -- and wootbeer's own real registered/GOG install (confirmed by
 * listing it directly) simply doesn't ship an AUDIO.EPC at all; nothing in GOG's own re-release
 * does. Requiring it (an earlier version of this fix tried adding it to JillActivity.java's asset
 * manifest) could therefore never be satisfied by a real installation, so rexit(155) was replaced
 * with a zeroed SOUNDS table when the file isn't there: real behavior, unchanged, when it IS present
 * (still read byte-for-byte); an inert but harmless fallback instead of a hard failure when it isn't.
 * Still needed today -- AUDIO.EPC still isn't shipped -- but no longer the common case: now that
 * host_audio_digital_available()/host_audio_music_available() (HOSTANDROID.c) report real hardware
 * present rather than being stubbed, vocflag/musicflag both end up true in ordinary play, so this
 * `else` branch is rarely reached -- left in place as a harmless safety net rather than removed.
 */

#include "MUSIC.H"
#include "UNKNOWN.H"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VOC_COUNT       50
#define TEXT_COUNT      40
#define SOUNDMAC_COUNT 128
#define VOC_CACHE_COUNT 4
/* Android port note (this file, not upstream): VOC_BLOCK_SIZE was upstream's own 0x1800 (6144
 * bytes -- 6112 usable after VOC_HEADER_SIZE) per cached sound-effect slot. On a real device this
 * is provably too small: confirmed on wootbeer's own build, getvoc() below already had to copy a real
 * jill1.vcl sample of 6253 bytes into a nominally-6144-byte slot (logged by host_audio_play_voc()
 * as "playing 6251 bytes" -- block_length minus the 2-byte rate/codec header), silently spilling
 * past that slot's own boundary into the next cache slot's space. snd_do()'s own memvoc allocation
 * below (upstream's own literal 0x7800 -- five VOC_BLOCK_SIZE's worth for four VOC_CACHE_COUNT
 * slots) already reads as upstream's own deliberate slack for exactly this kind of overflow, but
 * it wasn't enough: a real crash was reproduced on-device (SIGSEGV, ARM MTE tag-mismatch fault
 * address, inside host_audio_play_voc()) triggered by JPLAYER.c's own snd_play(1, 1) jump sound
 * while a larger sample was cached in the last slot, which is exactly the case with the least
 * headroom before running off the end of the whole memvoc allocation. Real upstream/DOS hardware
 * never hit this because the real Sound Blaster DMA path didn't re-derive a read length from this
 * reconstructed VOC block's own length field the way host_audio_play_voc() below has to (there's
 * no other host boundary to hand a length across) -- so this slot size only ever needed to survive
 * upstream's own real-mode buffer, not a length-parsing round trip through JNI. Widened to 0x4000
 * (16384 bytes, 16352 usable) -- comfortably covers every real jill1.vcl sample length found so
 * far (up to 11518 bytes) with margin for the other two episodes' own sound banks, which haven't
 * been surveyed the same way. getvoc() below also gained a hard clamp as defense in depth, so a
 * sample that's still too big for this is truncated (with a native log line) instead of overrunning
 * the buffer again -- see that function's own comment. */
#define VOC_BLOCK_SIZE  0x4000
#define VOC_HEADER_SIZE 0x20
#define SOUND_CAPACITY  0x1000

word soundoff = 1;
word soundf = 1;
/* Android port addition, not upstream -- see MUSIC.H's own comment on these three for the whole
 * Sound/Music Options submenu. Default gain for both is JILL_SOUND_GAIN_STEPS (10 = 100%) -- wootbeer's
 * own "the current sound and music level will be the max... default" ask -- so a fresh install or an
 * existing config.dat from before this feature shipped both sound identical to before this round. */
word jill_sound_gain = JILL_SOUND_GAIN_STEPS;
word jill_music_gain = JILL_SOUND_GAIN_STEPS;
word jill_music_enabled = 1;
word makesound;
word SetDSP;
word SetWORX;
word vocuse;
word *freq;
word *dur;
word headersize = 640;
word vocflag = 1;
word musicflag = 1;
int vocfilehandle = -1;
char *song;

static const char vochdr[VOC_HEADER_SIZE] = {
    0x43, 0x72, 0x65, 0x61, 0x74, 0x69, 0x76, 0x65,
    0x20, 0x56, 0x6f, 0x69, 0x63, 0x65, 0x20, 0x46,
    0x69, 0x6c, 0x65, 0x1a, 0x1a, 0x00, 0x0a, 0x01,
    0x29, 0x11, 0x01, 0x5d, 0x2d, 0x00, 0xaa, 0x00
};

static const word notetable[144] = {
      64,    67,    71,    76,    80,    85,    90,    95,
     101,   107,   114,   121,     0,     0,     0,     0,
     128,   135,   143,   152,   161,   170,   181,   191,
     203,   215,   228,   242,     0,     0,     0,     0,
     256,   271,   287,   304,   322,   341,   362,   383,
     406,   430,   456,   483,     0,     0,     0,     0,
     512,   542,   574,   608,   645,   683,   724,   767,
     812,   861,   912,   967,     0,     0,     0,     0,
    1024,  1084,  1149,  1217,  1290,  1366,  1448,  1534,
    1625,  1722,  1825,  1933,     0,     0,     0,     0,
    2048,  2169,  2298,  2435,  2580,  2733,  2896,  3068,
    3250,  3444,  3649,  3866,     0,     0,     0,     0,
    4096,  4339,  4597,  4870,  5160,  5467,  5792,  6137,
    6501,  6888,  7298,  7732,     0,     0,     0,     0,
    8192,  8679,  9195,  9741, 10321, 10935, 11585, 12274,
   13003, 13777, 14596, 15646,     0,     0,     0,     0,
   16384, 17358, 18390, 19483, 20642, 21870, 23170, 24548,
   26007, 27554, 29192, 30928,     0,     0,     0,     0
};

/* The first 50 bytes of mirrortab are the VOC aliases used by snd_play. */
static const byte mirrortab[VOC_COUNT] = {
     0,  0,  0,  0,  0,  0,  0, 23,  0, 28,
     0,  0,  0, 24, 28,  0,  0,  0,  0,  0,
    48,  0,  0,  0,  0,  0,  5, 48,  0, 23,
    24, 18, 16,  0,  3,  0, 12,  0,  8,  0,
    41,  0, 32,  8, 24, 10,  0, 35,  0, 48
};

word *SOUNDS;
char *memvoc;
word oldpri;
word vocpri;
word vocused[VOC_COUNT];
word vocrate[VOC_COUNT];
sbyte vocnum[VOC_COUNT];
word voclen[VOC_COUNT];
char *textmsg;
char *soundmac[SOUNDMAC_COUNT];
word textlen[TEXT_COUNT];
longword vocposn[VOC_COUNT];
word oldfreq;
word clockrate;
word soundlen;
longword textposn[TEXT_COUNT];
word soundptr;
word textmsglen;
word clockcount;
word soundcount;
word notepriority;
word samppriority;

extern word nosnd;
extern void rexit(int result);

void testintr(void)
{
    spkr_intr();
    /* oldint8(): chaining the saved real-mode IRQ vector is a DOS boundary. */
}

void getvoc(int sample)
{
    word count;
    word oldest;
    word oldest_use;
    word index;
    word slot;
    char *block;

    /* nosound(): direct PC-speaker hardware call omitted at the host boundary. */
    if (voclen[sample] == 0 || vocnum[sample] != -1) return;
    count = 0;
    oldest = -1;
    oldest_use = -1;
    for (index = 0; index < VOC_COUNT; ++index) {
        if (vocnum[index] != -1) {
            ++count;
            if ((uword)vocused[index] < (uword)oldest_use) {
                oldest_use = vocused[index];
                oldest = index;
            }
        }
    }
    if (count >= VOC_CACHE_COUNT) {
        vocnum[sample] = vocnum[oldest];
        vocnum[oldest] = -1;
    } else {
        vocnum[sample] = (byte)count;
    }

    slot = vocnum[sample];
    block = memvoc + (uword)(slot * VOC_BLOCK_SIZE);
    memcpy(block, vochdr, VOC_HEADER_SIZE);
    {
        /* Android port note (this file, not upstream): defense in depth alongside VOC_BLOCK_SIZE's
         * own widening above (that comment has the real on-device crash this whole area fixes) --
         * clamp rather than trust voclen[sample] (read straight from the .vcl file) to fit in this
         * slot, so a sample bigger than even the widened VOC_BLOCK_SIZE truncates instead of
         * overrunning memvoc again. copylen's own type (word, upstream's own 16-bit unsigned) still
         * bounds this the same way voclen[] itself already does; the clamp only ever bites if
         * VOC_BLOCK_SIZE's own margin (widened above) still isn't enough for some real sample this
         * hasn't been tested against yet. Silent (no logging call) -- this file stays platform-
         * neutral like the rest of MUSIC.c's own port (see this file's top-of-file comment);
         * host_audio_play_voc() (HOSTANDROID.c, Android-specific) has its own matching clamp with
         * a log line, which is where this would actually surface if it ever fires. */
        word copylen = voclen[sample];
        if (copylen > VOC_BLOCK_SIZE - VOC_HEADER_SIZE)
            copylen = (word)(VOC_BLOCK_SIZE - VOC_HEADER_SIZE);
        block[0x1b] = (byte)copylen;
        block[0x1c] = (byte)((uword)copylen >> 8);
        block[0x1e] = 0x60;
        (void)lseek(vocfilehandle, vocposn[sample], SEEK_SET);
        (void)read(vocfilehandle, block + VOC_HEADER_SIZE, copylen);
    }
}

void snd_init(char *path)
{
    word index;

    clockrate = 0;
    clockcount = 0;
    textmsg = NULL;
    for (index = 0; index < VOC_COUNT; ++index) {
        vocposn[index] = -1;
        voclen[index] = 0;
        vocrate[index] = 0;
        vocnum[index] = -1;
        vocused[index] = 0;
    }
    for (index = 0; index < SOUNDMAC_COUNT; ++index) soundmac[index] = NULL;

    StartWorx();
    /* getvect/setvect for WorxBugInt8 are real-mode interrupt boundaries. */
    if (musicflag) musicflag = (word)AdlibDetect();
    if (!musicflag) vocflag = 0;
    if (*path == '\0') {
        vocflag = 0;
        return;
    }
    vocfilehandle = open(path, O_RDONLY);
    if (vocfilehandle == -1) {
        vocflag = 0;
        return;
    }
    (void)read(vocfilehandle, vocposn, sizeof(vocposn));
    (void)read(vocfilehandle, voclen, sizeof(voclen));
    (void)read(vocfilehandle, vocrate, sizeof(vocrate));
    (void)read(vocfilehandle, textposn, sizeof(textposn));
    (void)read(vocfilehandle, textlen, sizeof(textlen));
}

void snd_play(int priority, int sound)
{
    char *block;

    /* Android port note (this file, not upstream): defense in depth, same spirit and same silent-
     * clamp style as getvoc()'s own clamp above -- this is the REAL root cause of the on-device
     * SIGSEGV that VOC_BLOCK_SIZE's widening (this file's own comment above) turned out not to fix.
     * JOBJ.c's msg_checkpt() (checkpoint objects, msg_touch) calls
     * snd_play(4, macplay ? 5 : checkpoint->counter + 50); checkpoint->counter is a destination
     * level number, and the very next line up guards only the same-level case
     * ("if (pl.level == checkpoint->counter) return 0"), so every checkpoint that actually changes
     * the level reaches here with checkpoint->counter >= 0 -- making "checkpoint->counter + 50"
     * *always* >= VOC_COUNT (50), out of range for mirrortab[]/voclen[]/vocnum[]/vocposn[] below and
     * inside getvoc(), on the very first real checkpoint a player can touch (macplay true takes the
     * safe literal 5 instead, which is presumably why this was never caught). mirrortab is
     * `static const` and the rest are plain globals, so an out-of-range `sound` here reads/writes
     * whatever the linker happened to place adjacent to each array -- unpredictable, but confirmed
     * on-device to eventually corrupt vocnum[]/memvoc state badly enough to crash later inside
     * host_audio_play_voc() (HOSTANDROID.c) on a completely unrelated, perfectly in-range sound (the
     * jump sound, JPLAYER.c's snd_play(1, 1)), which is exactly the symptom wootbeer reported ("pressing
     * B to jump ... crashes") and reproduced twice on real hardware (SIGSEGV, ARM MTE tag-mismatch
     * fault, matching call stack both times). This has to be a real bug in the reconstructed source
     * itself (the arithmetic is unambiguous) rather than anything platform-specific -- most likely
     * VOC_COUNT (50) undercounts whatever the original checkpoint-jingle table's real size was in
     * the build this was reconstructed from. Nothing here can recover the original intent, so this
     * clamps out-of-range sound IDs to a silent no-op (skip both the VOC and soundmac paths below)
     * instead of guessing at a replacement -- the same conservative choice getvoc()'s own clamp
     * already made for an oversized sample, and JOBJ.c itself is left untouched since this project's
     * own discipline is to keep the reconstructed gameplay code byte-identical and fix real
     * deviations at the audio subsystem boundary instead. */
    if (sound < 0 || sound >= VOC_COUNT) return;

    if (vocflag && soundf) {
        if (!VOCPlaying() || priority >= oldpri) {
            if (mirrortab[sound] != 0) sound = mirrortab[sound];
            getvoc(sound);
            if (vocnum[sound] != -1) {
                block = memvoc + (uword)(vocnum[sound] * VOC_BLOCK_SIZE);
                (void)PlayVOCBlock(block, 0x7f);
                vocused[sound] = vocuse;
                ++vocuse;
            }
            oldpri = (word)priority;
        }
    } else if (sound < SOUNDMAC_COUNT && soundmac[sound] != NULL &&
               freq != NULL && dur != NULL) {
        soundadd(priority, soundmac[sound]);
    }
}

void snd_do(void)
{
    word index;
    word size;
    int handle;

    /* nosound(): direct PC-speaker hardware call omitted at the host boundary. */
    if (nosnd || musicflag || vocflag) clockrate = 0;
    else if (!vocflag) clockrate = 64;

    if (musicflag) (void)SetFMVolume(15, 15);
    if (vocflag) {
        SetDSP = (word)(DSPReset() != 0);
        vocflag = SetDSP;
        if (!vocflag) soundoff = 1;
        else (void)SetMasterVolume(15, 15);
    }

    if (vocflag) {
        /* Android port note (this file, not upstream): was upstream's own literal 0x7800 (five
         * VOC_BLOCK_SIZE's worth of upstream's own old 0x1800 slot size, for four VOC_CACHE_COUNT
         * slots -- one extra slot's worth of headroom past the last real slot, upstream's own
         * apparent margin for an oversized sample). Rescaled to match VOC_BLOCK_SIZE's own widening
         * above (VOC_BLOCK_SIZE's own comment has the real on-device crash this fixes), keeping the
         * same "N+1 slots" shape: 5 * 0x4000. */
        memvoc = (char *)malloc((VOC_CACHE_COUNT + 1) * VOC_BLOCK_SIZE);
    } else {
        memvoc = NULL;
        freq = (word *)malloc(0x2080);
        dur = (word *)malloc(0x2080);
        (void)lseek(vocfilehandle, headersize, SEEK_SET);
        for (index = 0; index < SOUNDMAC_COUNT; ++index) {
            (void)read(vocfilehandle, &size, sizeof(size));
            if (size != 0) {
                soundmac[index] = (char *)malloc((uword)size);
                if (soundmac[index] == NULL) rexit(154);
                (void)read(vocfilehandle, soundmac[index], (uword)size);
            } else {
                soundmac[index] = NULL;
            }
        }
        SOUNDS = (word *)malloc(0x28f0);
        handle = open("audio.epc", O_RDONLY);
        /* Android note (this file, not upstream): was `if (handle == -1) rexit(155);` -- see this
           file's own header comment for why that's wrong here specifically (real installs that
           don't ship this file at all, on the one platform where this branch always runs). */
        if (handle == -1) {
            memset(SOUNDS, 0, 0x28f0);
        } else {
            (void)read(handle, SOUNDS, 0x28a0);
            close(handle);
        }
    }

    if (clockrate == 0) {
        clockrate = 1;
        soundoff = 1;
    } else if (clockrate > 1) {
        soundoff = 0;
        timerset(0, 2, (unsigned)(0x10000UL / (uword)clockrate));
    }
}

void text_get(int index)
{
    textmsg = NULL;
    if (textlen[index] != 0) {
        textmsglen = textlen[index];
        textmsg = (char *)malloc((uword)textmsglen);
        if (textmsg != NULL) {
            (void)lseek(vocfilehandle, textposn[index], SEEK_SET);
            if (read(vocfilehandle, textmsg, (uword)textmsglen) == -1)
                textmsg = NULL;
        }
    }
}

void snd_exit(void)
{
    word index;

    timerset(0, 2, 0);
    /* nosound() and restoring the saved IRQ vectors are DOS boundaries. */
    if (freq != NULL) free(freq);
    if (dur != NULL) free(dur);
    for (index = 0; index < SOUNDMAC_COUNT; ++index)
        if (soundmac[index] != NULL) free(soundmac[index]);
    free(memvoc);
    if (vocfilehandle >= 0) close(vocfilehandle);
    if (SetDSP) DSPClose();
    CloseWorx();
}

void sb_update(void) { }

int sb_playing(void)
{
    return 1;
}

void sb_shutup(void)
{
    if (musicflag) {
        StopSequence();
        free(song);
        song = NULL;
    }
}

void sb_playtune(char *filename)
{
    /* Android port change, not upstream -- see MUSIC.H's own jill_music_enabled comment. Real
     * upstream gate was `if (musicflag)` alone; jill_music_enabled adds the new live Music on/off
     * toggle (JUNGLE.c's pausemenu() SOUND/MUSIC submenu) on top of it, same "ANDed onto the real
     * hardware-capability flag" shape snd_play()'s own `if (vocflag && soundf)` already established
     * for sound effects. While Music is toggled off, this simply no-ops for every caller (a level
     * transition, re-entering the main menu, etc.) exactly like a real `if(!setup.music) return;`
     * would -- no per-call-site changes needed anywhere else in this file or JUNGLE.c's own level-
     * transition code. */
    if (musicflag && jill_music_enabled) {
        sb_shutup();
        song = GetSequence(filename);
        if (song != NULL) {
            SetLoopMode(1);
            PlayCMFBlock(song);
        }
    }
}

void timerset(int timer, int mode, unsigned divisor)
{
    /* Exact PIT command: ((timer << 6) + (mode << 1) + 0x30), followed by
       the divisor low and high bytes.  Privileged port writes are omitted. */
    (void)timer;
    (void)mode;
    (void)divisor;
}

void sampadd1(int instrument, int length, int duration, int note)
{
    word sample_index;
    longword multiplier;
    longword sample;

    if (soundoff) return;
    sample_index = 0;
    multiplier = notetable[note + 16];
    makesound = 1;
    do {
        sample = SOUNDS[(uword)instrument * 128U + sample_index++];
        if (sample == -1L) freq[soundlen] = -1;
        else freq[soundlen] = (word)((sample * multiplier) >> 10);
        dur[soundlen++] = (word)duration;
    } while (sample_index < length && soundlen < SOUND_CAPACITY);
}

void sampadd(int instrument, int length, int duration, int note)
{
    word sample_index;
    longword multiplier;
    longword sample;

    if (soundoff) return;
    sample_index = 0;
    multiplier = notetable[note + 16];
    makesound = 1;
    do {
        sample = SOUNDS[(uword)instrument * 128U + sample_index++];
        if (sample == -1L) freq[soundlen] = -1;
        else freq[soundlen] = (word)((sample * multiplier) >> 10);
        dur[soundlen++] = (word)duration;
    } while (sample_index < length && soundlen < SOUND_CAPACITY);
}

static word instrument_length(word instrument)
{
    word length = SOUNDS[0x1400 + (uword)instrument];
    return length < 1 ? 1 : length;
}

void soundadd1(int priority, char *sequence)
{
    word instrument = -1;
    word cursor = 0;
    word note, duration_value, sample_count, total;
    word remaining;

    if (soundoff) return;
    if (makesound &&
        !((priority >= notepriority && notepriority != -1) || priority == -1))
        return;
    if (priority >= 0 || !makesound) {
        makesound = 0;
        soundptr = soundlen = soundcount = 0;
    }
    notepriority = (word)priority;
    do {
        if ((byte)sequence[cursor] == 0xf0) {
            ++cursor;
            instrument = (word)(sbyte)sequence[cursor++];
        }
        note = (word)(sbyte)sequence[cursor++];
        duration_value = (word)(sbyte)sequence[cursor++];
        if (instrument == -1) {
            freq[soundlen] = notetable[note];
            dur[soundlen++] = (word)((uword)duration_value * (uword)clockrate);
            makesound = 1;
        } else {
            sample_count = instrument_length(instrument);
            total = (word)((uword)duration_value * (uword)clockrate);
            remaining = (word)(total - (word)(sample_count << 7));
            if (remaining > 0) {
                sampadd(instrument, 128, sample_count, note);
                freq[soundlen] = -1;
                dur[soundlen++] = remaining;
            } else {
                sampadd(instrument, (word)((uword)total / (uword)sample_count),
                        sample_count, note);
            }
        }
    } while (sequence[cursor] != 0 && soundlen < SOUND_CAPACITY);
}

void soundadd2(int priority, char *sequence)
{
    word instrument = -1;
    word cursor = 0;
    word note, duration_value, sample_count, total;
    word remaining;

    if (soundoff) return;
    if (makesound &&
        !((priority >= notepriority && notepriority != -1) || priority == -1))
        return;
    if (priority >= 0 || !makesound) {
        makesound = 0;
        soundptr = soundlen = soundcount = 0;
    }
    notepriority = (word)priority;
    do {
        if ((byte)sequence[cursor] == 0xf0) {
            ++cursor;
            instrument = (word)(sbyte)sequence[cursor++];
        }
        note = (word)(sbyte)sequence[cursor++];
        duration_value = (word)(sbyte)sequence[cursor++];
        if (instrument == -1) {
            freq[soundlen] = notetable[note];
            dur[soundlen++] = (word)((uword)duration_value * (uword)clockrate);
            makesound = 1;
        } else {
            sample_count = instrument_length(instrument);
            total = (word)((uword)duration_value * (uword)clockrate);
            remaining = (word)(total - (word)(sample_count << 7));
            if (remaining > 0) {
                sampadd(instrument, 128, sample_count, note);
                freq[soundlen] = -1;
                dur[soundlen++] = remaining;
            } else {
                sampadd(instrument, (word)((uword)total / (uword)sample_count),
                        sample_count, note);
            }
        }
    } while (sequence[cursor] != 0 && soundlen < SOUND_CAPACITY);
}

void soundadd(int priority, char *sequence)
{
    word instrument = -1;
    word cursor = 0;
    word note, duration_value, sample_count, total;
    word remaining;

    if (soundoff) return;
    if (makesound &&
        !((priority >= notepriority && notepriority != -1) || priority == -1))
        return;
    if (priority >= 0 || !makesound) {
        makesound = 0;
        soundptr = soundlen = soundcount = 0;
    }
    notepriority = (word)priority;
    do {
        if ((byte)sequence[cursor] == 0xf0) {
            ++cursor;
            instrument = (word)(sbyte)sequence[cursor++];
        }
        note = (word)(sbyte)sequence[cursor++];
        duration_value = (word)(sbyte)sequence[cursor++];
        if (instrument == -1) {
            freq[soundlen] = notetable[note];
            dur[soundlen++] = (word)((uword)duration_value * (uword)clockrate);
            makesound = 1;
        } else {
            sample_count = instrument_length(instrument);
            total = (word)((uword)duration_value * (uword)clockrate);
            remaining = (word)(total - (word)(sample_count << 7));
            if (remaining > 0) {
                sampadd(instrument, 128, sample_count, note);
                freq[soundlen] = -1;
                dur[soundlen++] = remaining;
            } else {
                /* Jill's original signed-remainder division bug, at 160F1. */
                sampadd(instrument, (word)(remaining / (word)sample_count),
                        sample_count, note);
            }
        }
    } while (sequence[cursor] != 0 && soundlen < SOUND_CAPACITY);
}

void soundstop(void)
{
    makesound = 0;
    /* nosound(): direct PC-speaker hardware call omitted at the host boundary. */
}

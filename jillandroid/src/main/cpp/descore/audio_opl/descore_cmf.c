/*
 * descore_cmf.c -- real-time CMF (Creative Music File) sequencer/player. See descore_cmf.h for the
 * public API and its own design notes (generic across ports, no game-engine dependency).
 *
 * Provenance: this is a fresh, from-scratch C implementation, not a straight copy of any one file,
 * but it leans heavily on two LGPL 2.1-or-later references -- both freely combinable with this
 * project's GPLv3 base (Jill of the Jungle Reconstructed's own license) as parts of one combined
 * work, which is why this file carries the same LGPL 2.1+ license itself (see its own notice below):
 *
 *  1. AdPlug's cmf.cpp/cmf.h (CMF player by Adam Nielsen <malvineous@shikadi.net>, Copyright (C)
 *     1999-2009 Simon Peter <dn.tlp@gmx.net> et al, LGPL 2.1-or-later) -- specifically a fork of
 *     that file whose own comments describe real reverse-engineering work cross-referencing the
 *     actual Creative SBFMDRV/"fmdrv" reference driver (not just the on-disk CMF *format*, which is
 *     documented in several places, but the real *playback* behavior: the note-velocity-to-output-
 *     level formula, the note/octave frequency lookup tables, "new instrument number modulo the
 *     file's own instrument count" program-change wraparound, voice-stealing priority order, running-
 *     status/sysex/meta-event parsing, and the rhythm-mode channel layout). This file ports that same
 *     accuracy work -- the algorithms and lookup tables, not the C++ source text -- from AdPlug's own
 *     tracker-frontend shape (a Copl-abstracted, host-scheduled update() called once per tick) into a
 *     pull-based renderer: descore_cmf_render() below produces however many audio frames its caller
 *     asks for on demand, converting CMF's own tick timing to a sample count internally, which is
 *     what a continuously-streamed AudioTrack (see descore_cmf.h's own design note) needs rather than
 *     a scheduled-callback player.
 *  2. Nuked OPL3 (opl3.c/opl3.h, this same directory -- Copyright (C) 2013-2020 Nuke.YKT, LGPL
 *     2.1-or-later) as the actual chip emulation this file drives, in place of AdPlug's own abstract
 *     Copl output interface. Used here purely in OPL2-compatible mode (register 0x105, OPL3 mode
 *     enable, is never written) -- Jill's own CMF data only ever targets a real OPL2 (the Sound
 *     Blaster's AdLib-compatible FM chip), and OPL3 is digitally backward-compatible with it at the
 *     synthesis level when driven that way.
 *
 * Deliberate simplifications from the AdPlug reference, all safe for this project's own use (a
 * fire-and-forget background-music player, not a general-purpose CMF tracker/tagger):
 *  - No song title/composer/remarks metadata -- never parsed at all, since nothing in this project
 *    displays it.
 *  - No debug logging -- AdPlug's own AdPlug_LogWrite() calls (unknown-command/out-of-range/etc.
 *    diagnostics) are dropped outright rather than ported to some platform log call, keeping this
 *    file free of any platform dependency (see descore_cmf.h's own header comment on why that
 *    matters -- a future port's engine may want this file unchanged).
 *  - Fails closed on anything it can't fully validate: descore_cmf_open() returns NULL for a bad
 *    signature/version or a music/instrument block that doesn't fit inside the supplied buffer,
 *    the same defensive posture AdPlug's own load() takes ("fix crash for a lot of broken files").
 *
 * ---------------------------------------------------------------------------------------------
 * This file (descore_cmf.c/.h) is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software Foundation,
 * either version 2.1 of the License, or (at your option) any later version, consistent with the two
 * references above. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 */

#include "descore_cmf.h"
#include "opl3.h"

#include <stdlib.h>
#include <string.h>

/* ---- OPL2 register map (channels 0-8; see OPLOFFSET() below for the per-channel offset within
 * each BASE_* group) -- identical constants to AdPlug's own cmf.cpp. ------------------------------ */
#define BASE_CHAR_MULT  0x20
#define BASE_SCAL_LEVL  0x40
#define BASE_ATCK_DCAY  0x60
#define BASE_SUST_RLSE  0x80
#define BASE_FNUM_L     0xA0
#define BASE_KEYON_FREQ 0xB0
#define BASE_RHYTHM     0xBD
#define BASE_WAVE       0xE0
#define BASE_FEED_CONN  0xC0

#define OPLBIT_KEYON    0x20

/* Channel (0-8) -> offset from a BASE_* register for that channel's Modulator cell (add 3 more for
 * the Carrier cell). E.g. channel 4's modulator is at offset 0x09, so register 0x69 (BASE_ATCK_DCAY
 * + 0x09) is channel 4's modulator attack/decay. */
#define OPLOFFSET(channel) ((((channel) / 3) * 8) + ((channel) % 3))

/* ---- CMF instrument / channel state ------------------------------------------------------------ */

typedef struct {
    uint8_t char_mult;       /* reg 0x20 group: AM/VIB/EG-type/KSR flags + frequency multiplier */
    uint8_t scale_level;     /* reg 0x40 group: key-scale level + output level */
    uint8_t attack_decay;    /* reg 0x60 group: attack rate + decay rate */
    uint8_t sustain_release; /* reg 0x80 group: sustain level + release rate */
    uint8_t wave_sel;        /* reg 0xE0 group: waveform select */
} cmf_operator;

typedef struct {
    cmf_operator op[2]; /* 0 == modulator, 1 == carrier */
    uint8_t connection; /* reg 0xC0 group: feedback + FM/additive connection */
} cmf_instrument;

typedef struct {
    int patch;      /* current MIDI-channel patch (already wrapped modulo the file's own instrument
                        count, matching the real driver's own program-change handling) */
    int pitchbend;  /* 0-16383, 8192 == center/off */
    int transpose;  /* 1/128ths of a semitone, from CMF controllers 0x68/0x69 */
} cmf_midi_channel;

typedef struct {
    int note_start;   /* 0 == voice free/released; otherwise an increasing "age" counter */
    int midi_note;    /* MIDI note currently sounding on this OPL voice */
    int midi_channel; /* owning MIDI channel, or -1 if this voice has never been used */
    int midi_patch;   /* instrument currently programmed into this voice */
} cmf_opl_channel;

struct descore_cmf_player {
    opl3_chip chip;

    uint8_t *data;   /* owned copy of the CMF file's music block only (from its own music_offset
                         onward) -- see descore_cmf_open()'s own comment on why this is copied
                         rather than borrowed */
    size_t play_ptr;
    size_t song_len;

    uint16_t ticks_per_second;

    cmf_instrument *instruments; /* [drum_patch_base + 5] */
    int inst_count;              /* modulo divisor for program-change wraparound */
    int drum_patch_base;         /* index of the 5 fallback GM-channel-9-remap drum timbres */

    uint8_t current_regs[256];   /* mirror of every OPL register this player has written, so a
                                     read-modify-write (e.g. toggling one rhythm bit) never needs a
                                     real register read, which OPL hardware/emulation don't support */
    uint8_t prev_command;        /* for MIDI "running status" (a status byte omitted when it repeats
                                     the previous event's) */
    uint8_t note_playing[16];    /* last note-on per MIDI channel, 255 == none -- duplicate-note-on
                                     fixup, matching AdPlug's own iNotePlaying/bNoteFix pair */
    uint8_t note_fix[16];

    int note_count;                 /* increasing "age" counter, see cmf_opl_channel.note_start */
    cmf_midi_channel midi_ch[16];
    cmf_opl_channel opl_ch[9];

    int percussive; /* OPL rhythm mode on/off (CMF controller 0x67) */
    int midi_drums; /* true if this file drives percussion via General MIDI channel 9 instead of
                        CMF's own rhythm channels 11-15 -- see cmf_detect_midi_drums() */

    int loop;
    int song_ended;

    double samples_per_tick;         /* sample_rate / ticks_per_second, precomputed once */
    double samples_until_next_event; /* fractional to avoid drift over a long/looping song */
};

/* ---- Default instrument banks / lookup tables, ported verbatim (same values, same order) from the
 * AdPlug reference described in this file's own header comment -- itself ported from the real
 * SBFMDRV/"fmdrv" reference driver. See that reference's own comments (reproduced in spirit above)
 * for what each table is: cDefaultPatches is the 16-slot fallback bank used only when a CMF declares
 * zero of its own instruments; cInitInstrument is the timbre the real driver programs every channel
 * with at reset/mode-switch, before any real instrument is loaded; cDefaultDrumPatches are fallback
 * percussion timbres for files that drive drums via General MIDI channel 9 (cmf_detect_midi_drums())
 * rather than ever sending their own program change on channels 11-15; block_note_tbl/fnum_tbl are
 * the note -> OPL octave/F-number tuning tables. ------------------------------------------------- */

static const uint8_t cDefaultPatches[16][11] = {
    {0x21, 0x21, 0xD1, 0x07, 0xA3, 0xA4, 0x46, 0x25, 0x00, 0x00, 0x0A},
    {0x22, 0x22, 0x0F, 0x0F, 0xF6, 0xF6, 0x95, 0x36, 0x00, 0x00, 0x0A},
    {0xE1, 0xE1, 0x00, 0x00, 0x44, 0x54, 0x24, 0x34, 0x02, 0x02, 0x07},
    {0xA5, 0xB1, 0xD2, 0x80, 0x81, 0xF1, 0x03, 0x05, 0x00, 0x00, 0x02},
    {0x71, 0x22, 0xC5, 0x05, 0x6E, 0x8B, 0x17, 0x0E, 0x00, 0x00, 0x02},
    {0x32, 0x21, 0x16, 0x80, 0x73, 0x75, 0x24, 0x57, 0x00, 0x00, 0x0E},
    {0x01, 0x11, 0x4F, 0x00, 0xF1, 0xD2, 0x53, 0x74, 0x00, 0x00, 0x06},
    {0x07, 0x12, 0x4F, 0x00, 0xF2, 0xF2, 0x60, 0x72, 0x00, 0x00, 0x08},
    {0x31, 0xA1, 0x1C, 0x80, 0x51, 0x54, 0x03, 0x67, 0x00, 0x00, 0x0E},
    {0x31, 0xA1, 0x1C, 0x80, 0x41, 0x92, 0x0B, 0x3B, 0x00, 0x00, 0x0E},
    {0x31, 0x16, 0x87, 0x80, 0xA1, 0x7D, 0x11, 0x43, 0x00, 0x00, 0x08},
    {0x30, 0xB1, 0xC8, 0x80, 0xD5, 0x61, 0x19, 0x1B, 0x00, 0x00, 0x0C},
    {0xF1, 0x21, 0x01, 0x0D, 0x97, 0xF1, 0x17, 0x18, 0x00, 0x00, 0x08},
    {0x32, 0x16, 0x87, 0x80, 0xA1, 0x7D, 0x10, 0x33, 0x00, 0x00, 0x08},
    {0x01, 0x12, 0x4F, 0x00, 0x71, 0x52, 0x53, 0x7C, 0x00, 0x00, 0x0A},
    {0x02, 0x03, 0x8D, 0x03, 0xD7, 0xF5, 0x37, 0x18, 0x00, 0x00, 0x04},
};

static const uint8_t cInitInstrument[11] =
    {0x01, 0x11, 0x4F, 0x00, 0xF1, 0xF2, 0x53, 0x74, 0x00, 0x00, 0x08};

/* Order: bass drum (ch11), snare (ch12), tom (ch13), top cymbal (ch14), hi-hat (ch15). */
static const uint8_t cDefaultDrumPatches[5][11] = {
    {0x00, 0x00, 0x00, 0x00, 0xF8, 0xF8, 0x07, 0x07, 0x00, 0x00, 0x00},
    {0x0C, 0x0C, 0x00, 0x00, 0xF8, 0xF8, 0x07, 0x07, 0x00, 0x01, 0x00},
    {0x04, 0x04, 0x00, 0x00, 0xF8, 0xF8, 0x07, 0x07, 0x00, 0x00, 0x00},
    {0x0C, 0x0C, 0x00, 0x00, 0xF8, 0xF8, 0x05, 0x05, 0x03, 0x03, 0x00},
    {0x0E, 0x0E, 0x00, 0x00, 0xF8, 0xF8, 0x07, 0x07, 0x03, 0x03, 0x00},
};

/* MIDI note (0-127) -> byte with the OPL block/octave in the high nibble, semitone-within-octave in
 * the low nibble. */
static const uint8_t block_note_tbl[128] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
    0x7b, 0x7b, 0x7b, 0x7b, 0x7b, 0x7b, 0x7b, 0x7b
};

/* Note index in 1/64ths of a semitone (768 == 12 semitones * 64) -> 10-bit OPL F-number. */
static const uint16_t fnum_tbl[768] = {
    343, 343, 344, 344, 344, 344, 345, 345, 345, 346, 346, 346,
    347, 347, 347, 348, 348, 348, 349, 349, 349, 349, 350, 350,
    350, 351, 351, 351, 352, 352, 352, 353, 353, 353, 354, 354,
    354, 355, 355, 355, 356, 356, 356, 356, 357, 357, 357, 358,
    358, 358, 359, 359, 359, 360, 360, 360, 361, 361, 361, 362,
    362, 362, 363, 363, 363, 364, 364, 364, 365, 365, 365, 366,
    366, 366, 367, 367, 367, 368, 368, 368, 369, 369, 369, 370,
    370, 370, 371, 371, 371, 372, 372, 372, 373, 373, 373, 374,
    374, 374, 375, 375, 375, 376, 376, 376, 377, 377, 377, 378,
    378, 378, 379, 379, 379, 380, 380, 380, 381, 381, 381, 382,
    382, 382, 383, 383, 384, 384, 384, 385, 385, 385, 386, 386,
    386, 387, 387, 387, 388, 388, 388, 389, 389, 389, 390, 390,
    391, 391, 391, 392, 392, 392, 393, 393, 393, 394, 394, 394,
    395, 395, 395, 396, 396, 397, 397, 397, 398, 398, 398, 399,
    399, 399, 400, 400, 401, 401, 401, 402, 402, 402, 403, 403,
    403, 404, 404, 405, 405, 405, 406, 406, 406, 407, 407, 407,
    408, 408, 409, 409, 409, 410, 410, 410, 411, 411, 412, 412,
    412, 413, 413, 413, 414, 414, 414, 415, 415, 416, 416, 416,
    417, 417, 417, 418, 418, 419, 419, 419, 420, 420, 421, 421,
    421, 422, 422, 422, 423, 423, 424, 424, 424, 425, 425, 425,
    426, 426, 427, 427, 427, 428, 428, 429, 429, 429, 430, 430,
    430, 431, 431, 432, 432, 432, 433, 433, 434, 434, 434, 435,
    435, 436, 436, 436, 437, 437, 438, 438, 438, 439, 439, 440,
    440, 440, 441, 441, 442, 442, 442, 443, 443, 444, 444, 444,
    445, 445, 446, 446, 446, 447, 447, 448, 448, 448, 449, 449,
    450, 450, 450, 451, 451, 452, 452, 452, 453, 453, 454, 454,
    454, 455, 455, 456, 456, 457, 457, 457, 458, 458, 459, 459,
    459, 460, 460, 461, 461, 461, 462, 462, 463, 463, 464, 464,
    464, 465, 465, 466, 466, 467, 467, 467, 468, 468, 469, 469,
    469, 470, 470, 471, 471, 472, 472, 472, 473, 473, 474, 474,
    475, 475, 475, 476, 476, 477, 477, 478, 478, 478, 479, 479,
    480, 480, 481, 481, 481, 482, 482, 483, 483, 484, 484, 485,
    485, 485, 486, 486, 487, 487, 488, 488, 488, 489, 489, 490,
    490, 491, 491, 492, 492, 492, 493, 493, 494, 494, 495, 495,
    496, 496, 496, 497, 497, 498, 498, 499, 499, 500, 500, 501,
    501, 501, 502, 502, 503, 503, 504, 504, 505, 505, 506, 506,
    506, 507, 507, 508, 508, 509, 509, 510, 510, 511, 511, 511,
    512, 512, 513, 513, 514, 514, 515, 515, 516, 516, 517, 517,
    518, 518, 518, 519, 519, 520, 520, 521, 521, 522, 522, 523,
    523, 524, 524, 525, 525, 526, 526, 526, 527, 527, 528, 528,
    529, 529, 530, 530, 531, 531, 532, 532, 533, 533, 534, 534,
    535, 535, 536, 536, 537, 537, 538, 538, 538, 539, 539, 540,
    540, 541, 541, 542, 542, 543, 543, 544, 544, 545, 545, 546,
    546, 547, 547, 548, 548, 549, 549, 550, 550, 551, 551, 552,
    552, 553, 553, 554, 554, 555, 555, 556, 556, 557, 557, 558,
    558, 559, 559, 560, 560, 561, 561, 562, 562, 563, 563, 564,
    564, 565, 565, 566, 566, 567, 567, 568, 568, 569, 569, 570,
    571, 571, 572, 572, 573, 573, 574, 574, 575, 575, 576, 576,
    577, 577, 578, 578, 579, 579, 580, 580, 581, 581, 582, 582,
    583, 584, 584, 585, 585, 586, 586, 587, 587, 588, 588, 589,
    589, 590, 590, 591, 591, 592, 593, 593, 594, 594, 595, 595,
    596, 596, 597, 597, 598, 598, 599, 600, 600, 601, 601, 602,
    602, 603, 603, 604, 604, 605, 606, 606, 607, 607, 608, 608,
    609, 609, 610, 610, 611, 612, 612, 613, 613, 614, 614, 615,
    615, 616, 617, 617, 618, 618, 619, 619, 620, 620, 621, 622,
    622, 623, 623, 624, 624, 625, 626, 626, 627, 627, 628, 628,
    629, 629, 630, 631, 631, 632, 632, 633, 633, 634, 635, 635,
    636, 636, 637, 637, 638, 639, 639, 640, 640, 641, 642, 642,
    643, 643, 644, 644, 645, 646, 646, 647, 647, 648, 649, 649,
    650, 650, 651, 651, 652, 653, 653, 654, 654, 655, 656, 656,
    657, 657, 658, 659, 659, 660, 660, 661, 662, 662, 663, 663,
    664, 665, 665, 666, 666, 667, 668, 668, 669, 669, 670, 671,
    671, 672, 672, 673, 674, 674, 675, 675, 676, 677, 677, 678,
    678, 679, 680, 680, 681, 682, 682, 683, 683, 684, 685, 685
};

/* ---- Forward declarations ----------------------------------------------------------------------- */

static void cmf_write_opl(descore_cmf_player *p, uint8_t reg, uint8_t value);
static void cmf_write_instrument_settings(descore_cmf_player *p, uint8_t channel,
                                           uint8_t operator_source, uint8_t operator_dest,
                                           uint8_t instrument);
static void cmf_get_freq(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t *out_block,
                          uint16_t *out_fnum);
static void cmf_note_on(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t velocity);
static void cmf_note_off(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t velocity);
static void cmf_note_update(descore_cmf_player *p, uint8_t channel);
static uint8_t cmf_get_perc_channel(uint8_t channel);
static uint8_t cmf_gm_key_to_rhythm_channel(uint8_t note);
static int cmf_detect_midi_drums(const uint8_t *data, size_t song_len);
static void cmf_midi_change_instrument(descore_cmf_player *p, uint8_t opl_channel,
                                        uint8_t midi_channel, uint8_t new_instrument);
static void cmf_midi_controller(descore_cmf_player *p, uint8_t channel, uint8_t controller,
                                 uint8_t value);
static void cmf_rhythm_mode_reset(descore_cmf_player *p, int percussive);
static uint32_t cmf_read_midi_number(descore_cmf_player *p);
static void cmf_handle_system_event(descore_cmf_player *p, uint8_t cmd);
static void cmf_end_of_track(descore_cmf_player *p);
static void cmf_pump_events(descore_cmf_player *p);
static void cmf_init_state(descore_cmf_player *p);

/* ---- Implementation ------------------------------------------------------------------------------ */

static void cmf_write_opl(descore_cmf_player *p, uint8_t reg, uint8_t value)
{
    OPL3_WriteReg(&p->chip, reg, value);
    p->current_regs[reg] = value;
}

/* iChannel: OPL channel (0-8). operator_source: 0 == modulator, 1 == carrier -- which operator of
 * `instrument` to read. operator_dest: which OPL cell (0 == modulator, 1 == carrier) to write it
 * into -- almost always equal to operator_source (melodic instruments copy modulator to modulator
 * and carrier to carrier), except the rhythm-mode remaps in cmf_midi_change_instrument() below,
 * which sometimes point a single-operator percussion voice's only cell at either half of the
 * instrument definition. */
static void cmf_write_instrument_settings(descore_cmf_player *p, uint8_t channel,
                                           uint8_t operator_source, uint8_t operator_dest,
                                           uint8_t instrument)
{
    uint8_t offset = (uint8_t)OPLOFFSET(channel);
    const cmf_instrument *inst = &p->instruments[instrument];
    if (operator_dest) offset = (uint8_t)(offset + 3);

    cmf_write_opl(p, (uint8_t)(BASE_CHAR_MULT + offset), inst->op[operator_source].char_mult);
    cmf_write_opl(p, (uint8_t)(BASE_SCAL_LEVL + offset), inst->op[operator_source].scale_level);
    cmf_write_opl(p, (uint8_t)(BASE_ATCK_DCAY + offset), inst->op[operator_source].attack_decay);
    cmf_write_opl(p, (uint8_t)(BASE_SUST_RLSE + offset), inst->op[operator_source].sustain_release);
    cmf_write_opl(p, (uint8_t)(BASE_WAVE + offset), inst->op[operator_source].wave_sel);

    cmf_write_opl(p, (uint8_t)(BASE_FEED_CONN + channel), inst->connection);
}

static void cmf_get_freq(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t *out_block,
                          uint16_t *out_fnum)
{
    int clamped = note;
    uint8_t block_note;
    int blk, note_idx;

    if (clamped < 0) clamped = 0;
    if (clamped > 127) clamped = 127;
    block_note = block_note_tbl[clamped];

    blk = (block_note & 0x70) >> 4;
    note_idx = (block_note & 0x0F) << 6;

    note_idx += p->midi_ch[channel].transpose / 4;
    note_idx += (p->midi_ch[channel].pitchbend - 8192) / 128;

    if (note_idx < 0) {
        note_idx += 768;
        blk -= 1;
        if (blk < 0) { note_idx = 0; blk = 0; }
    }
    if (note_idx >= 768) {
        note_idx -= 768;
        blk += 1;
        if (blk > 7) { note_idx = 767; blk = 7; }
    }

    *out_block = (uint8_t)blk;
    *out_fnum = fnum_tbl[note_idx];
}

static uint8_t cmf_get_perc_channel(uint8_t channel)
{
    switch (channel) {
    case 11: return 7 - 1; /* bass drum */
    case 12: return 8 - 1; /* snare */
    case 13: return 9 - 1; /* tom tom */
    case 14: return 9 - 1; /* top cymbal */
    case 15: return 8 - 1; /* hi-hat */
    default: return 0;
    }
}

/* Maps a General MIDI percussion key number to the nearest CMF rhythm channel -- only reached when
 * cmf_detect_midi_drums() found this file drives its drums through GM channel 9 rather than CMF's
 * own rhythm channels. */
static uint8_t cmf_gm_key_to_rhythm_channel(uint8_t note)
{
    switch (note) {
    case 35: case 36: return 11; /* acoustic / bass drum */
    case 37: case 38: case 39: case 40: return 12; /* stick / snare / clap */
    case 42: case 44: case 46: return 15; /* closed / pedal / open hi-hat */
    case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 14; /* crash/ride/etc */
    case 80: case 81: return 14; /* triangle -> cymbal */
    default: return 13; /* toms and everything else */
    }
}

static void cmf_note_on(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t block = 0;
    uint16_t fnum = 0;

    if (p->midi_drums && channel == 9) channel = cmf_gm_key_to_rhythm_channel(note);

    cmf_get_freq(p, channel, note, &block, &fnum);

    if (channel > 10 && p->percussive) {
        uint8_t perc_channel = cmf_get_perc_channel(channel);
        uint8_t carrier_scal, base_level, ksl, level, bit;
        int offset;

        /* Always reprogram on note-on -- simpler than tracking whether the mod/car half actually
         * needs it, matching the AdPlug reference. */
        cmf_midi_change_instrument(p, perc_channel, channel, (uint8_t)p->midi_ch[channel].patch);

        carrier_scal = p->instruments[p->midi_ch[channel].patch].op[1].scale_level;
        base_level = (uint8_t)(63 - (carrier_scal & 0x3F));
        ksl = (uint8_t)(carrier_scal & 0xC0);
        level = (uint8_t)((63 - (((velocity | 0x80) * base_level) >> 8)) | ksl);

        offset = BASE_SCAL_LEVL + OPLOFFSET(perc_channel);
        if (channel == 11) offset += 3; /* bass drum: carrier controls volume */
        cmf_write_opl(p, (uint8_t)offset, level);

        /* Cymbal/hi-hat frequency writes are a known "maybe/maybe not" per the AdPlug reference's
         * own comment -- written unconditionally here too, matching it exactly. */
        cmf_write_opl(p, (uint8_t)(BASE_FNUM_L + perc_channel), (uint8_t)(fnum & 0xFF));
        cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + perc_channel),
                      (uint8_t)((block << 2) | ((fnum >> 8) & 0x03)));

        bit = (uint8_t)(1 << (15 - channel));
        if (p->current_regs[BASE_RHYTHM] & bit)
            cmf_write_opl(p, BASE_RHYTHM, (uint8_t)(p->current_regs[BASE_RHYTHM] & ~bit));
        cmf_write_opl(p, BASE_RHYTHM, (uint8_t)(p->current_regs[BASE_RHYTHM] | bit));

        p->opl_ch[perc_channel].note_start = ++p->note_count;
        p->opl_ch[perc_channel].midi_channel = channel;
        p->opl_ch[perc_channel].midi_note = note;
    } else {
        int opl_channel = -1;
        int num_channels = p->percussive ? 6 : 9;
        int i, prev_owner;
        uint8_t carrier_scal, base_level, ksl, level, offset;

        /* Tier 1: a voice this same MIDI channel last released -- reuse it, no reprogram needed. */
        for (i = 0; i < num_channels; i++) {
            if (p->opl_ch[i].note_start == 0 && p->opl_ch[i].midi_channel == channel) {
                opl_channel = i;
                break;
            }
        }
        /* Tier 2: a voice that has never been used. */
        if (opl_channel == -1) {
            for (i = 0; i < num_channels; i++) {
                if (p->opl_ch[i].midi_channel == -1) { opl_channel = i; break; }
            }
        }
        /* Tier 3: any released voice (last owned by a different channel). */
        if (opl_channel == -1) {
            for (i = 0; i < num_channels; i++) {
                if (p->opl_ch[i].note_start == 0) { opl_channel = i; break; }
            }
        }
        /* Tier 4: every voice busy -- steal the one playing longest and key it off first. */
        if (opl_channel == -1) {
            int earliest;
            opl_channel = 0;
            earliest = p->opl_ch[0].note_start;
            for (i = 1; i < num_channels; i++) {
                if (p->opl_ch[i].note_start < earliest) {
                    opl_channel = i;
                    earliest = p->opl_ch[i].note_start;
                }
            }
            cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + opl_channel),
                          (uint8_t)(p->current_regs[BASE_KEYON_FREQ + opl_channel] & ~OPLBIT_KEYON));
        }

        /* Only reprogram if this voice's last owner differs -- avoids redundant register writes
         * when the same MIDI channel plays consecutive notes with the same instrument. */
        prev_owner = p->opl_ch[opl_channel].midi_channel;
        if (prev_owner != (int)channel) {
            cmf_midi_change_instrument(p, (uint8_t)opl_channel, channel,
                                        (uint8_t)p->midi_ch[channel].patch);
        }

        p->opl_ch[opl_channel].note_start = ++p->note_count;
        p->opl_ch[opl_channel].midi_channel = channel;
        p->opl_ch[opl_channel].midi_note = note;

        carrier_scal = p->instruments[p->midi_ch[channel].patch].op[1].scale_level;
        base_level = (uint8_t)(63 - (carrier_scal & 0x3F));
        ksl = (uint8_t)(carrier_scal & 0xC0);
        level = (uint8_t)((63 - (((velocity | 0x80) * base_level) >> 8)) | ksl);
        offset = (uint8_t)(BASE_SCAL_LEVL + OPLOFFSET(opl_channel) + 3);
        cmf_write_opl(p, offset, level);

        cmf_write_opl(p, (uint8_t)(BASE_FNUM_L + opl_channel), (uint8_t)(fnum & 0xFF));
        cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + opl_channel),
                      (uint8_t)(OPLBIT_KEYON | (block << 2) | ((fnum & 0x300) >> 8)));
    }
}

static void cmf_note_off(descore_cmf_player *p, uint8_t channel, uint8_t note, uint8_t velocity)
{
    (void)velocity; /* release velocity -- unused, matching the AdPlug reference */

    if (p->midi_drums && channel == 9) channel = cmf_gm_key_to_rhythm_channel(note);

    if (channel > 10 && p->percussive) {
        uint8_t opl_channel = cmf_get_perc_channel(channel);
        if (p->opl_ch[opl_channel].midi_note != note) return; /* a different note is playing now */
        cmf_write_opl(p, BASE_RHYTHM,
                      (uint8_t)(p->current_regs[BASE_RHYTHM] & ~(1 << (15 - channel))));
        p->opl_ch[opl_channel].note_start = 0;
    } else {
        int num_channels = p->percussive ? 6 : 9;
        int i;
        for (i = 0; i < num_channels; i++) {
            if (p->opl_ch[i].midi_channel == channel && p->opl_ch[i].midi_note == note &&
                p->opl_ch[i].note_start != 0) {
                p->opl_ch[i].note_start = 0; /* released, owner retained so the next note from this
                                                 channel can reuse the voice without reprogramming */
                cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + i),
                              (uint8_t)(p->current_regs[BASE_KEYON_FREQ + i] & ~OPLBIT_KEYON));
            }
        }
    }
}

/* Re-applies frequency after a pitch-bend or transpose controller changes a channel's tuning while
 * a note on it is already sounding. */
static void cmf_note_update(descore_cmf_player *p, uint8_t channel)
{
    uint8_t block = 0;
    uint16_t fnum = 0;

    if (channel > 10 && p->percussive) {
        uint8_t perc_channel = cmf_get_perc_channel(channel);
        cmf_get_freq(p, channel, (uint8_t)p->opl_ch[perc_channel].midi_note, &block, &fnum);
        cmf_write_opl(p, (uint8_t)(BASE_FNUM_L + perc_channel), (uint8_t)(fnum & 0xFF));
        cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + perc_channel),
                      (uint8_t)((block << 2) | ((fnum >> 8) & 0x03)));
    } else {
        int num_channels = p->percussive ? 6 : 9;
        int i;
        for (i = 0; i < num_channels; i++) {
            if (p->opl_ch[i].midi_channel == channel && p->opl_ch[i].note_start > 0) {
                cmf_get_freq(p, channel, (uint8_t)p->opl_ch[i].midi_note, &block, &fnum);
                cmf_write_opl(p, (uint8_t)(BASE_FNUM_L + i), (uint8_t)(fnum & 0xFF));
                cmf_write_opl(p, (uint8_t)(BASE_KEYON_FREQ + i),
                              (uint8_t)(OPLBIT_KEYON | (block << 2) | ((fnum & 0x300) >> 8)));
            }
        }
    }
}

/* Scans the music block once, before playback starts, to decide whether this file drives percussion
 * as a General MIDI drum track on MIDI channel 9 rather than via CMF's own native rhythm channels
 * (11-15) -- the signature of a file converted from a General MIDI source without remapping the drum
 * channel. Flagged when channel 9 plays at least one note, never receives a program change (the GM
 * drum channel ignores patch numbers), and every channel-9 note falls within the GM percussion key
 * range (35-81). This is a read-only lookahead pass over a local cursor -- it never touches the
 * player's own play_ptr. */
static int cmf_detect_midi_drums(const uint8_t *data, size_t song_len)
{
    size_t pos = 0;
    uint8_t prev = 0;
    int ch9_note = 0, ch9_prog = 0, ch9_non_drum = 0;

    while (pos < song_len && (data[pos] & 0x80)) pos++;
    if (pos < song_len) pos++;

    while (pos < song_len) {
        uint8_t cmd = data[pos++];
        uint8_t ch;
        if ((cmd & 0x80) == 0) { pos--; cmd = prev; } else { prev = cmd; }
        ch = cmd & 0x0F;

        switch (cmd & 0xF0) {
        case 0x80: case 0xA0: case 0xB0: case 0xE0: /* two data bytes, not examined here */
            if (pos <= song_len - 2) pos += 2; else pos = song_len;
            break;
        case 0x90: {
            if (pos > song_len - 2) { pos = song_len; break; }
            {
                uint8_t note = data[pos++];
                uint8_t vel = data[pos++];
                if (ch == 9 && vel > 0) {
                    ch9_note = 1;
                    if (note < 35 || note > 81) ch9_non_drum = 1;
                }
            }
            break;
        }
        case 0xC0:
            if (ch == 9) ch9_prog = 1;
            if (pos < song_len) pos += 1; else pos = song_len;
            break;
        case 0xD0:
            if (pos < song_len) pos += 1; else pos = song_len;
            break;
        case 0xF0:
            if (cmd == 0xF0 || cmd == 0xF7) {
                uint32_t len = 0;
                int c = 0;
                while (pos < song_len && c < 4) {
                    uint8_t b = data[pos++];
                    c++;
                    len = (len << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                if (len > (uint32_t)(song_len - pos)) pos = song_len; else pos += len;
            } else if (cmd == 0xFF) {
                uint32_t len = 0;
                int c = 0;
                if (pos >= song_len) break;
                pos++; /* meta-event type byte, not examined here */
                while (pos < song_len && c < 4) {
                    uint8_t b = data[pos++];
                    c++;
                    len = (len << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                if (len > (uint32_t)(song_len - pos)) pos = song_len; else pos += len;
            } else if (cmd == 0xFC) {
                pos = song_len;
            }
            break;
        default:
            break;
        }

        if (pos >= song_len) break;
        while (pos < song_len && (data[pos] & 0x80)) pos++;
        if (pos < song_len) pos++;
    }

    return ch9_note && !ch9_prog && !ch9_non_drum;
}

static void cmf_midi_change_instrument(descore_cmf_player *p, uint8_t opl_channel,
                                        uint8_t midi_channel, uint8_t new_instrument)
{
    if (midi_channel > 10 && p->percussive) {
        switch (midi_channel) {
        case 11: /* bass drum: channel 7's modulator + carrier */
            cmf_write_instrument_settings(p, 7 - 1, 0, 0, new_instrument);
            cmf_write_instrument_settings(p, 7 - 1, 1, 1, new_instrument);
            break;
        case 12: /* snare: channel 8's carrier */
            cmf_write_instrument_settings(p, 8 - 1, 0, 1, new_instrument);
            break;
        case 13: /* tom tom: channel 9's modulator */
            cmf_write_instrument_settings(p, 9 - 1, 0, 0, new_instrument);
            break;
        case 14: /* top cymbal: channel 9's carrier */
            cmf_write_instrument_settings(p, 9 - 1, 0, 1, new_instrument);
            break;
        case 15: /* hi-hat: channel 8's modulator */
            cmf_write_instrument_settings(p, 8 - 1, 0, 0, new_instrument);
            break;
        default:
            break;
        }
        p->opl_ch[opl_channel].midi_patch = new_instrument;
    } else {
        cmf_write_instrument_settings(p, opl_channel, 0, 0, new_instrument);
        cmf_write_instrument_settings(p, opl_channel, 1, 1, new_instrument);
        p->opl_ch[opl_channel].midi_patch = new_instrument;
    }
}

static void cmf_midi_controller(descore_cmf_player *p, uint8_t channel, uint8_t controller,
                                 uint8_t value)
{
    switch (controller) {
    case 0x63: /* CMF-specific AM+VIB depth switch: 0 off, 1 VIB, 2 AM, 3 both (default) */
        if (value) {
            cmf_write_opl(p, BASE_RHYTHM,
                          (uint8_t)((p->current_regs[BASE_RHYTHM] & ~0xC0) | (value << 6)));
        } else {
            cmf_write_opl(p, BASE_RHYTHM, (uint8_t)(p->current_regs[BASE_RHYTHM] & ~0xC0));
        }
        break;
    case 0x66: /* song marker -- no player-visible effect */
        break;
    case 0x67: /* rhythm/percussive mode switch -- a full driver-style reset, not just a bit flip */
        cmf_rhythm_mode_reset(p, value != 0);
        break;
    case 0x68: /* transpose up, 1/128ths of a semitone */
        p->midi_ch[channel].transpose = value;
        cmf_note_update(p, channel);
        break;
    case 0x69: /* transpose down, 1/128ths of a semitone */
        p->midi_ch[channel].transpose = -value;
        cmf_note_update(p, channel);
        break;
    default:
        break; /* every other MIDI controller is unsupported by CMF and ignored, same as upstream */
    }
}

/* Switches OPL rhythm (percussive) mode in/out -- a full reset of every melodic channel's
 * instrument/voice state, not just the rhythm-enable bit, matching the real Creative driver's own
 * switch_mode()/opl_reset2() behavior (channels start the new mode from a clean, known register
 * state rather than inheriting whatever the previous mode left behind). */
static void cmf_rhythm_mode_reset(descore_cmf_player *p, int percussive)
{
    int i;
    p->percussive = percussive;

    cmf_write_opl(p, 0x08, 0x00);
    for (i = 0; i < 9; i++) {
        uint8_t offset = (uint8_t)OPLOFFSET(i);
        cmf_write_opl(p, (uint8_t)(BASE_CHAR_MULT + offset), cInitInstrument[0]);
        cmf_write_opl(p, (uint8_t)(BASE_CHAR_MULT + offset + 3), cInitInstrument[1]);
        cmf_write_opl(p, (uint8_t)(BASE_SCAL_LEVL + offset), cInitInstrument[2]);
        cmf_write_opl(p, (uint8_t)(BASE_SCAL_LEVL + offset + 3), cInitInstrument[3]);
        cmf_write_opl(p, (uint8_t)(BASE_ATCK_DCAY + offset), cInitInstrument[4]);
        cmf_write_opl(p, (uint8_t)(BASE_ATCK_DCAY + offset + 3), cInitInstrument[5]);
        cmf_write_opl(p, (uint8_t)(BASE_SUST_RLSE + offset), cInitInstrument[6]);
        cmf_write_opl(p, (uint8_t)(BASE_SUST_RLSE + offset + 3), cInitInstrument[7]);
        cmf_write_opl(p, (uint8_t)(BASE_WAVE + offset), cInitInstrument[8]);
        cmf_write_opl(p, (uint8_t)(BASE_WAVE + offset + 3), cInitInstrument[9]);
        cmf_write_opl(p, (uint8_t)(BASE_FEED_CONN + i), cInitInstrument[10]);

        p->opl_ch[i].note_start = 0;
        p->opl_ch[i].midi_note = -1;
        p->opl_ch[i].midi_channel = -1;
        p->opl_ch[i].midi_patch = -1;
    }
    for (i = 0; i < 16; i++) p->midi_ch[i].patch = 0;

    /* A General-MIDI-channel-9 drum file: point the rhythm channels at the reserved fallback drum
     * timbres so remapped percussion is audible immediately -- a later program change on channels
     * 11-15 (from a well-formed file) overrides these regardless. */
    if (p->midi_drums && percussive) {
        int ch;
        for (ch = 11; ch <= 15; ch++) p->midi_ch[ch].patch = p->drum_patch_base + (ch - 11);
    }

    /* The real Creative player always runs with AM+VIB depth enabled; the rhythm-enable bit (0x20)
     * is added on top in percussive mode. */
    cmf_write_opl(p, BASE_RHYTHM, percussive ? 0xE0 : 0xC0);
}

static uint32_t cmf_read_midi_number(descore_cmf_player *p)
{
    uint32_t value = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t next = (p->play_ptr < p->song_len) ? p->data[p->play_ptr++] : 0;
        value <<= 7;
        value |= (next & 0x7F);
        if ((next & 0x80) == 0) break;
    }
    return value;
}

static void cmf_end_of_track(descore_cmf_player *p)
{
    if (p->loop) {
        p->play_ptr = 0;
        p->prev_command = 0;
    } else {
        p->song_ended = 1;
    }
}

/* System-category events (status byte 0xF0-0xFF) -- sysex, MIDI realtime, and CMF's own use of the
 * MIDI meta-event shape (0xFF) for its end-of-track marker. Every length-prefixed payload here is
 * skipped rather than interpreted, matching upstream (CMF has no standard use for any of it beyond
 * end-of-track). */
static void cmf_handle_system_event(descore_cmf_player *p, uint8_t cmd)
{
    switch (cmd) {
    case 0xF0: /* sysex: <vlq length><payload...>, including the trailing EOX byte */
    case 0xF7: { /* "escape" -- same length-prefixed shape; should never occur standalone, but
                    handled defensively the same way upstream does */
        uint32_t len = cmf_read_midi_number(p);
        if (len > (uint32_t)(p->song_len - p->play_ptr)) p->play_ptr = p->song_len;
        else p->play_ptr += len;
        break;
    }
    case 0xF1: /* MIDI time code quarter frame -- one data byte, ignored */
        if (p->play_ptr < p->song_len) p->play_ptr++;
        break;
    case 0xF2: /* song position pointer -- two data bytes, ignored */
        if (p->play_ptr <= p->song_len - 2) p->play_ptr += 2; else p->play_ptr = p->song_len;
        break;
    case 0xF3: /* song select -- one data byte, ignored */
        if (p->play_ptr < p->song_len) p->play_ptr++;
        break;
    case 0xF6: /* tune request -- no data bytes */
        break;
    case 0xF8: case 0xFA: case 0xFB: case 0xFE: /* realtime messages, no data bytes, ignored */
        break;
    case 0xFC: /* stop */
        cmf_end_of_track(p);
        break;
    case 0xFF: { /* meta-event: <type><vlq length><payload...> */
        uint8_t event_type;
        uint32_t len;
        if (p->play_ptr >= p->song_len) break;
        event_type = p->data[p->play_ptr++];
        len = cmf_read_midi_number(p);
        if (event_type == 0x2F) { /* end of track */
            cmf_end_of_track(p);
        } else if (len > (uint32_t)(p->song_len - p->play_ptr)) {
            p->play_ptr = p->song_len;
        } else {
            p->play_ptr += len;
        }
        break;
    }
    default:
        break;
    }
}

/* Processes CMF events until either a nonzero inter-event delay is found (converted to samples and
 * added onto p->samples_until_next_event, then returns) or the song ends without looping (sets
 * p->song_ended and returns). Restructured from AdPlug's own host-driven CcmfPlayer::update() (one
 * call per scheduled tick) into a self-contained "catch up the sequencer" step descore_cmf_render()
 * calls whenever its own sample-domain countdown reaches zero -- see this file's own header comment. */
static void cmf_pump_events(descore_cmf_player *p)
{
    for (;;) {
        uint8_t cmd;
        uint32_t ticks;

        if (p->play_ptr < p->song_len) {
            cmd = p->data[p->play_ptr];
            if (cmd & 0x80) {
                p->play_ptr++;
                p->prev_command = cmd;
            } else {
                cmd = p->prev_command; /* running status: reuse the previous event's status byte */
            }
        } else {
            cmd = 0;
        }

        switch (cmd & 0xF0) {
        case 0x80: /* note off */
            if (p->play_ptr <= p->song_len - 2) {
                uint8_t note = p->data[p->play_ptr++];
                uint8_t vel = p->data[p->play_ptr++];
                cmf_note_off(p, (uint8_t)(cmd & 0x0F), note, vel);
            } else {
                p->play_ptr = p->song_len;
            }
            break;
        case 0x90: { /* note on (velocity 0 == note off, per MIDI convention) */
            if (p->play_ptr <= p->song_len - 2) {
                uint8_t ch = (uint8_t)(cmd & 0x0F);
                uint8_t note = p->data[p->play_ptr++];
                uint8_t vel = p->data[p->play_ptr++];
                if (vel) {
                    if (p->note_playing[ch] == note) { /* duplicated note: force it off first */
                        vel = 0;
                        p->note_fix[ch] = 1;
                    }
                } else if (p->note_fix[ch]) {
                    vel = 127;
                    p->note_fix[ch] = 0;
                }
                p->note_playing[ch] = vel ? note : 255;
                if (vel) cmf_note_on(p, ch, note, vel);
                else cmf_note_off(p, ch, note, vel);
            } else {
                p->play_ptr = p->song_len;
            }
            break;
        }
        case 0xA0: /* polyphonic key pressure -- unsupported, ignored */
            if (p->play_ptr <= p->song_len - 2) p->play_ptr += 2; else p->play_ptr = p->song_len;
            break;
        case 0xB0: { /* controller */
            if (p->play_ptr <= p->song_len - 2) {
                uint8_t ch = (uint8_t)(cmd & 0x0F);
                uint8_t controller = p->data[p->play_ptr++];
                uint8_t value = p->data[p->play_ptr++];
                cmf_midi_controller(p, ch, controller, value);
            } else {
                p->play_ptr = p->song_len;
            }
            break;
        }
        case 0xC0: { /* program change */
            if (p->play_ptr < p->song_len) {
                uint8_t ch = (uint8_t)(cmd & 0x0F);
                uint8_t new_instrument = p->data[p->play_ptr++];
                int patch = (p->inst_count > 0) ? (new_instrument % p->inst_count) : 0;
                p->midi_ch[ch].patch = patch;

                if (!p->percussive || ch < 11) {
                    int num_channels = p->percussive ? 6 : 9;
                    int i;
                    /* Voices merely parked (released) by this channel go back to "never used", so
                     * the new patch is loaded fresh next time this channel plays. */
                    for (i = 0; i < num_channels; i++) {
                        if (p->opl_ch[i].note_start == 0 && p->opl_ch[i].midi_channel == ch)
                            p->opl_ch[i].midi_channel = -1;
                    }
                    /* Voices currently sounding on this channel are reprogrammed live. */
                    for (i = 0; i < num_channels; i++) {
                        if (p->opl_ch[i].note_start > 0 && p->opl_ch[i].midi_channel == ch)
                            cmf_midi_change_instrument(p, (uint8_t)i, ch, (uint8_t)patch);
                    }
                } else {
                    cmf_midi_change_instrument(p, cmf_get_perc_channel(ch), ch, (uint8_t)patch);
                }
            } else {
                p->play_ptr = p->song_len;
            }
            break;
        }
        case 0xD0: /* channel pressure -- unsupported, ignored */
            if (p->play_ptr < p->song_len) p->play_ptr += 1; else p->play_ptr = p->song_len;
            break;
        case 0xE0: { /* pitch bend -- the official CMF player ignores this too, but it's cheap to
                        honor and AdPlug's own reference does, so this does too */
            if (p->play_ptr <= p->song_len - 2) {
                uint8_t ch = (uint8_t)(cmd & 0x0F);
                uint8_t lsb = p->data[p->play_ptr++];
                uint8_t msb = p->data[p->play_ptr++];
                p->midi_ch[ch].pitchbend = (int)(((unsigned)msb << 7) | lsb);
                cmf_note_update(p, ch);
            } else {
                p->play_ptr = p->song_len;
            }
            break;
        }
        case 0xF0:
            cmf_handle_system_event(p, cmd);
            break;
        default:
            break; /* stray/unresolvable running status with no prior command -- drop it */
        }

        if (p->song_ended) return; /* a meta/stop event inside the switch above may have ended it */

        if (p->play_ptr >= p->song_len) {
            cmf_end_of_track(p);
            if (p->song_ended) return;
            /* looped: play_ptr is back at 0 -- fall through and read the very first delay */
        }

        ticks = cmf_read_midi_number(p);
        if (ticks) {
            p->samples_until_next_event += (double)ticks * p->samples_per_tick;
            return;
        }
        /* ticks == 0: another event follows with no delay -- process it now, same iteration */
    }
}

/* Resets the OPL chip and every piece of sequencer state to the song's very beginning -- the CMF
 * equivalent of AdPlug's own rewind(), minus the opl->init() device-open call (OPL3_Reset(), called
 * once by descore_cmf_open() before this, already leaves the chip in a known-clean state). */
static void cmf_init_state(descore_cmf_player *p)
{
    int i;

    /* Enable WaveSel (reg 0x01) and make sure OPL3 mode / CSM+SEL are off (regs 0x05/0x08) -- the
     * real Creative CMF player does this every time too. Register 0x05 in particular must stay 0:
     * this file only ever drives OPL3 in OPL2-compatible mode (see this file's own header comment). */
    cmf_write_opl(p, 0x01, 0x20);
    cmf_write_opl(p, 0x05, 0x00);
    cmf_write_opl(p, 0x08, 0x00);

    for (i = 0; i < 9; i++) {
        uint8_t offset = (uint8_t)OPLOFFSET(i);
        cmf_write_opl(p, (uint8_t)(BASE_CHAR_MULT + offset), cInitInstrument[0]);
        cmf_write_opl(p, (uint8_t)(BASE_CHAR_MULT + offset + 3), cInitInstrument[1]);
        cmf_write_opl(p, (uint8_t)(BASE_SCAL_LEVL + offset), cInitInstrument[2]);
        cmf_write_opl(p, (uint8_t)(BASE_SCAL_LEVL + offset + 3), cInitInstrument[3]);
        cmf_write_opl(p, (uint8_t)(BASE_ATCK_DCAY + offset), cInitInstrument[4]);
        cmf_write_opl(p, (uint8_t)(BASE_ATCK_DCAY + offset + 3), cInitInstrument[5]);
        cmf_write_opl(p, (uint8_t)(BASE_SUST_RLSE + offset), cInitInstrument[6]);
        cmf_write_opl(p, (uint8_t)(BASE_SUST_RLSE + offset + 3), cInitInstrument[7]);
        cmf_write_opl(p, (uint8_t)(BASE_WAVE + offset), cInitInstrument[8]);
        cmf_write_opl(p, (uint8_t)(BASE_WAVE + offset + 3), cInitInstrument[9]);
        cmf_write_opl(p, (uint8_t)(BASE_FEED_CONN + i), cInitInstrument[10]);
    }

    /* AM+VIB depth amplified, melodic mode -- the real Creative player always does this and there's
     * no standard way to stop it (CMF controller 0x63 is a non-standard AdPlug/this-file extension). */
    cmf_write_opl(p, BASE_RHYTHM, 0xC0);

    p->song_ended = 0;
    p->play_ptr = 0;
    p->prev_command = 0;
    p->note_count = 0;
    p->percussive = 0;

    for (i = 0; i < 9; i++) {
        p->opl_ch[i].note_start = 0;
        p->opl_ch[i].midi_note = -1;
        p->opl_ch[i].midi_channel = -1;
        p->opl_ch[i].midi_patch = -1;
    }
    for (i = 0; i < 16; i++) {
        p->midi_ch[i].patch = 0;
        p->midi_ch[i].pitchbend = 8192;
        p->midi_ch[i].transpose = 0;
    }

    memset(p->current_regs, 0, sizeof(p->current_regs));
    memset(p->note_playing, 255, sizeof(p->note_playing));
    memset(p->note_fix, 0, sizeof(p->note_fix));

    /* A General-MIDI-channel-9 drum file: force rhythm mode on now and load the fallback drum
     * timbres, so remapped percussion is audible even if the song never sends controller 0x67
     * itself -- a song that does send it simply re-runs this harmlessly. */
    if (p->midi_drums) cmf_rhythm_mode_reset(p, 1);

    p->samples_until_next_event = (double)cmf_read_midi_number(p) * p->samples_per_tick;
}

/* ---- Public API (descore_cmf.h) ------------------------------------------------------------------ */

descore_cmf_player *descore_cmf_open(const uint8_t *data, size_t length, int loop,
                                      uint32_t sample_rate)
{
    descore_cmf_player *p;
    uint16_t version;
    uint16_t instrument_offset, music_offset, ticks_per_second;
    uint16_t num_instruments;
    size_t inst_alloc;
    size_t i;
    size_t music_len;

    if (data == NULL || length < 20 || sample_rate == 0) return NULL;
    if (data[0] != 'C' || data[1] != 'T' || data[2] != 'M' || data[3] != 'F') return NULL;

    version = (uint16_t)(data[4] | (data[5] << 8));
    if (version != 0x0100 && version != 0x0101) return NULL;

    instrument_offset = (uint16_t)(data[6] | (data[7] << 8));
    music_offset = (uint16_t)(data[8] | (data[9] << 8));
    /* offsets 10-11 (ticks-per-quarter-note) and 14-19 (title/composer/remarks tag offsets) are
     * never read -- see this file's own header comment on the metadata this player doesn't need. */
    ticks_per_second = (uint16_t)(data[12] | (data[13] << 8));

    if (version == 0x0100) {
        if (length < 37) return NULL;
        num_instruments = data[36];
    } else {
        if (length < 40) return NULL;
        num_instruments = (uint16_t)(data[36] | (data[37] << 8));
        /* offsets 38-39 (tempo, BPM) never read -- playback timing comes entirely from
         * ticks_per_second above, matching upstream. */
    }

    if (ticks_per_second == 0) return NULL; /* can't convert ticks to real time */
    if (music_offset >= length) return NULL;
    if ((size_t)instrument_offset + (size_t)num_instruments * 16 > length) return NULL;

    music_len = length - music_offset;

    p = (descore_cmf_player *)calloc(1, sizeof(*p));
    if (p == NULL) return NULL;

    p->data = (uint8_t *)malloc(music_len > 0 ? music_len : 1);
    if (p->data == NULL) { free(p); return NULL; }
    memcpy(p->data, data + music_offset, music_len);
    p->song_len = music_len;

    p->ticks_per_second = ticks_per_second;
    p->loop = loop;
    p->samples_per_tick = (double)sample_rate / (double)ticks_per_second;

    /* Always at least 128 slots (General MIDI's own patch range) plus 5 reserved fallback rhythm-
     * drum timbres for the GM-channel-9 remap (cmf_detect_midi_drums()). */
    p->drum_patch_base = (num_instruments < 128) ? 128 : (int)num_instruments;
    inst_alloc = (size_t)p->drum_patch_base + 5;
    p->instruments = (cmf_instrument *)calloc(inst_alloc, sizeof(cmf_instrument));
    if (p->instruments == NULL) { free(p->data); free(p); return NULL; }

    for (i = 0; i < 5; i++) {
        const uint8_t *row = cDefaultDrumPatches[i];
        cmf_instrument *inst = &p->instruments[p->drum_patch_base + (int)i];
        inst->op[0].char_mult = row[0];       inst->op[1].char_mult = row[1];
        inst->op[0].scale_level = row[2];     inst->op[1].scale_level = row[3];
        inst->op[0].attack_decay = row[4];    inst->op[1].attack_decay = row[5];
        inst->op[0].sustain_release = row[6]; inst->op[1].sustain_release = row[7];
        inst->op[0].wave_sel = row[8];        inst->op[1].wave_sel = row[9];
        inst->connection = row[10];
    }

    for (i = 0; i < num_instruments; i++) {
        const uint8_t *rec = data + instrument_offset + i * 16; /* 11 used + 5 padding bytes/slot */
        cmf_instrument *inst = &p->instruments[i];
        inst->op[0].char_mult = rec[0];       inst->op[1].char_mult = rec[1];
        inst->op[0].scale_level = rec[2];     inst->op[1].scale_level = rec[3];
        inst->op[0].attack_decay = rec[4];    inst->op[1].attack_decay = rec[5];
        inst->op[0].sustain_release = rec[6]; inst->op[1].sustain_release = rec[7];
        inst->op[0].wave_sel = rec[8];        inst->op[1].wave_sel = rec[9];
        inst->connection = rec[10];
    }

    if (num_instruments > 0) {
        p->inst_count = num_instruments;
    } else {
        /* A file with no instruments of its own falls back to the 16-slot default bank -- matches
         * the real driver's own pre-song state (g_num_inst == 16). */
        p->inst_count = 16;
        for (i = 0; i < 16; i++) {
            const uint8_t *row = cDefaultPatches[i];
            cmf_instrument *inst = &p->instruments[i];
            inst->op[0].char_mult = row[0];       inst->op[1].char_mult = row[1];
            inst->op[0].scale_level = row[2];     inst->op[1].scale_level = row[3];
            inst->op[0].attack_decay = row[4];    inst->op[1].attack_decay = row[5];
            inst->op[0].sustain_release = row[6]; inst->op[1].sustain_release = row[7];
            inst->op[0].wave_sel = row[8];        inst->op[1].wave_sel = row[9];
            inst->connection = row[10];
        }
    }

    p->midi_drums = cmf_detect_midi_drums(p->data, p->song_len);

    OPL3_Reset(&p->chip, sample_rate);
    cmf_init_state(p);

    return p;
}

size_t descore_cmf_render(descore_cmf_player *player, int16_t *buf, size_t num_frames)
{
    size_t i;

    if (player == NULL || buf == NULL) return 0;

    for (i = 0; i < num_frames; i++) {
        while (!player->song_ended && player->samples_until_next_event <= 0.0) {
            cmf_pump_events(player);
        }
        if (player->song_ended) return i;

        OPL3_GenerateResampled(&player->chip, &buf[i * 2]);
        player->samples_until_next_event -= 1.0;
    }
    return num_frames;
}

int descore_cmf_playing(const descore_cmf_player *player)
{
    return player != NULL && !player->song_ended;
}

void descore_cmf_close(descore_cmf_player *player)
{
    if (player == NULL) return;
    free(player->data);
    free(player->instruments);
    free(player);
}

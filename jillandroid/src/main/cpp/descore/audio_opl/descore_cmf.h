/*
 * descore_cmf.h -- real-time CMF (Creative Music File) player, generic across any "descore"-shell
 * port that needs it, not Jill-specific. See descore_cmf.c's own header comment for the full
 * provenance/design story (short version: a from-scratch C port of the CMF sequencing logic in
 * AdPlug's cmf.cpp, driving Nuked OPL3 -- opl3.c/opl3.h, this same directory -- instead of AdPlug's
 * abstract Copl output interface).
 *
 * Deliberately has ZERO dependency on any one game's engine headers (no RECOVERY.H/byte/word/etc.)
 * -- takes plain uint8_t pointers/size_t and hands back plain int16_t PCM, so a future port with its
 * own CMF or AdLib-MIDI music can reuse this file (and opl3.c/opl3.h) unchanged, the same way GR.C's
 * VGA-emulation primitives are meant to be shared once a second/third game has gone through the same
 * copy-paste (see this project's own notes on generalizing only after patterns repeat).
 */
#ifndef DESCORE_CMF_H
#define DESCORE_CMF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct descore_cmf_player descore_cmf_player;

/* Parses a CMF file already in memory and returns a new player primed to render from its first
 * event, or NULL if `data` isn't a CMF this parser can use (bad signature/version, or a music block
 * that doesn't fit inside `length`) -- never partially plays a file it couldn't fully validate.
 *
 * `data` is COPIED internally (the player owns its own buffer, freed by descore_cmf_close()) --
 * unlike a one-shot sound effect, a CMF player is rendered from asynchronously, chunk by chunk, for
 * as long as the song plays, so it can't safely borrow a caller's buffer that may be freed the
 * moment the call that started playback returns (which is exactly what happens here: MUSIC.c's own
 * sb_shutup() frees its `song` buffer on the ex-current track the next time a new one starts).
 *
 * `loop`: when the song reaches its own end-of-track marker, false stops it there (descore_cmf_
 * playing() then reads 0 forever after); true restarts it from the beginning, indefinitely.
 *
 * `sample_rate`: the output rate every descore_cmf_render() call on this player produces PCM at
 * (also handed to Nuked OPL3's own internal resampler, OPL3_Reset()) -- fixed for the player's
 * whole lifetime, matching whatever rate the caller's own audio output device was opened at. */
descore_cmf_player *descore_cmf_open(const uint8_t *data, size_t length, int loop,
                                      uint32_t sample_rate);

/* Renders `num_frames` interleaved stereo S16LE frames (so `buf` must hold at least
 * `num_frames * 2` int16_t's -- L, R, L, R, ...) starting from wherever the last call left off.
 * Returns the number of frames actually written. A short return (less than `num_frames`, including
 * 0) means the song reached its end without looping -- the caller should treat the player as
 * finished (descore_cmf_playing() now reads 0) and stop calling render on it; the remainder of
 * `buf` past the returned count is left untouched, NOT zero-filled, so the caller must not play it
 * as silence without clearing it first. Safe to keep calling after the song has finished (keeps
 * returning 0), just wasted work. */
size_t descore_cmf_render(descore_cmf_player *player, int16_t *buf, size_t num_frames);

/* True until a non-looping player reaches its own end-of-track; a looping player never stops on
 * its own (this stays true until descore_cmf_close()). */
int descore_cmf_playing(const descore_cmf_player *player);

/* Frees a player and everything it owns (its copy of the CMF bytes, its instrument table). Safe to
 * call with NULL. */
void descore_cmf_close(descore_cmf_player *player);

#ifdef __cplusplus
}
#endif

#endif /* DESCORE_CMF_H */

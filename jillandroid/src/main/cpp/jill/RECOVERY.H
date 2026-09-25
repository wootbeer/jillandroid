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

#ifndef JILL_RECOVERY_H
#define JILL_RECOVERY_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  byte;
typedef int8_t   sbyte;
typedef int16_t  word;
typedef uint16_t uword;
typedef int32_t  longword;
typedef uint32_t ulongword;

#if defined(_MSC_VER)
#  define JILL_PACKED
#  define JILL_INLINE __inline
#else
#  define JILL_PACKED __attribute__((packed))
#  define JILL_INLINE inline
#endif

#define JILL_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define JILL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define JILL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define JILL_SIGN(a) (((a) > 0) - ((a) < 0))

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(byte) == 1, "byte must be 8-bit");
_Static_assert(sizeof(word) == 2, "word must be 16-bit");
_Static_assert(sizeof(longword) == 4, "longword must be 32-bit");
/* The upstream decomp's own src/CMakeLists.txt asserts sizeof(void*)==4 for the WHOLE desktop
 * program ("Jill was a 16-bit DOS program. This recovery keeps the original 8/16/32-bit data
 * widths, but intentionally targets a flat 32-bit address space.") -- a real constraint for files
 * elsewhere in that tree (JOBJ.C, JMAN.C, etc., NOT yet linked into this Android build -- see
 * HOSTANDROID.c's own header comment for what is) that may carry over pointer arithmetic from the
 * original DOS-extender's flat 32-bit model. This checkpoint's own code only uses this header for
 * its portable byte/word/longword typedefs and two extern decls (myclock/longclock) -- nothing
 * pointer-width-dependent -- so the assertion is dropped here in this Android-side vendored copy
 * (the reference copy under jillfiles/ stays untouched as ground truth). MUST be revisited before
 * any real decomp .C file is linked into this build: either audit it for genuine 32-bit-pointer
 * dependence and fix it to be width-agnostic, or Jill's own engine ends up needing 32-bit ABIs
 * only (armeabi-v7a/x86), same as the desktop build already requires. Left assert-free rather than
 * reintroduced as an arm64/x86_64-only #if, because android/arm64-v8a and x86_64 always report
 * sizeof(void*)==8 -- keeping any form of this check here would otherwise hard-fail this whole
 * CMakeLists.txt's native build on those ABIs for EVERY target in it, including GoT's own
 * already-working got/modex targets, which share this same build file with Jill's jillhost. */
#endif

uword jill_read_u16_le(const byte *source);
ulongword jill_read_u32_le(const byte *source);
void jill_write_u16_le(byte *destination, uword value);
void jill_write_u32_le(byte *destination, ulongword value);
char *jill_strdup(const char *source);
int jill_random(int limit);
void jill_delay(unsigned milliseconds);
uint32_t jill_ticks(void);

/* Win32 emulation of the BIOS timer word at 0040:006c. */
extern volatile uword *myclock;
extern volatile longword longclock;

/* Android-only addition, not part of upstream RECOVERY.H -- full path to jill.dma, when set. See
 * PLATFORM.c's own definition/comment (it owns the storage) and JINFO.c's own initinfo() (the one
 * reader) for the full story: desktop builds never set this and parse byte-identically to
 * upstream; Android sets it via jillhost_set_dma_path() (HOSTANDROID.c/H) to the real absolute
 * path JillActivity's SAF picker copied the user's own jill.dma into. */
extern const char *jill_dma_path;

#endif

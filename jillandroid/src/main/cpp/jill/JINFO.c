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
 * Android port note (this file, not upstream): two real changes from the decomp's own JINFO.C.
 *
 * 1. _open()/_read() (MSVC <io.h>) -> plain POSIX open()/read() (<fcntl.h>/<unistd.h>) -- same
 *    reasoning as PLATFORM.c's own jill_dos_creat() port, see that file's header comment. No
 *    _O_BINARY equivalent needed -- POSIX has no text/binary mode distinction to begin with.
 *
 * 2. The hardcoded relative "jill.dma" path (opened in whatever the process's current working
 *    directory happens to be -- meaningful on desktop, meaningless on Android, which has no
 *    equivalent notion the app controls) is now jill_dma_path when that's set (see PLATFORM.c's
 *    own definition/comment), falling back to the literal "jill.dma" otherwise -- so desktop
 *    builds (which never set it) parse byte-identically to upstream. Android sets it via
 *    jillhost_set_dma_path() (HOSTANDROID.c) to the real absolute path JillActivity's own SAF
 *    picker copied the user's jill.dma into (private app storage, jill/shared/jill.dma).
 *
 * The actual parsing loop below -- the part that matters for correctness against the real file
 * format -- is untouched from upstream.
 *
 * Renamed JINFO.C -> JINFO.c (lowercase extension only) for the same capital-.C-means-C++ CMake
 * gotcha noted in PLATFORM.c's own header comment.
 */

#include "JILL.H"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

infotype info[numinfotypes];
word stateinfo[6];

void initinfo(void)
{
    static char empty_name[] = "";
    int handle;
    word number;
    word flags;
    sbyte length;
    int index;
    const char *dma_path = (jill_dma_path != NULL) ? jill_dma_path : "jill.dma";

    flags = 0x4006;
    for (index = 0; index < numinfotypes; ++index) {
        info[index].sh = 0x4700;
        info[index].name = empty_name;
        info[index].flags = flags;
    }

    /* Guarding on handle >= 0 and closing it afterward are the only two behavioral additions
     * beyond the mechanical io.h port -- upstream never checked _open()'s result or closed the
     * handle either (a single leaked DOS/desktop file handle for the process's whole lifetime
     * never mattered there); harmless either way, but worth calling out as not upstream-identical. */
    handle = open(dma_path, O_RDONLY);
    while (handle >= 0 && read(handle, &number, 2) > 0) {
        (void)read(handle, &info[number].sh, 2);
        (void)read(handle, &flags, 2);
        info[number].flags ^= flags;
        (void)read(handle, &length, 1);
        info[number].name = (char *)malloc((size_t)length + 1);
        (void)read(handle, info[number].name, (uword)length);
        info[number].name[length] = '\0';
    }
    if (handle >= 0) close(handle);

    for (index = 0; index < 6; ++index) stateinfo[index] = 0;
    stateinfo[st_begin] |= sti_invincible;
    stateinfo[st_stand] |= sti_canfire;
    stateinfo[st_jumping] |= sti_canfire;
    stateinfo[st_climbing] |= 0;
    stateinfo[st_die] |= sti_invincible;
}

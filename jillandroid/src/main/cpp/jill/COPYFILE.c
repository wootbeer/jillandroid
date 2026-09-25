/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code (?Jill of the Jungle Reconstructed?).

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
 * Android port note (this file, not upstream): same shape as PLATFORM.c/JINFO.c/SHM.c/MUSIC.c/
 * jill/GAMECTRL.c's own ports -- upstream's #include "DOSIO.H" is dropped (doesn't exist in this
 * project) for plain POSIX (<fcntl.h>/<unistd.h>/<sys/stat.h>, the last already included). _open/
 * _read/_write/_close -> open/read/write/close; _O_BINARY|_O_RDONLY -> O_RDONLY (no O_BINARY
 * equivalent, same reasoning as every other file this project already ported); _creat(destination,
 * 0) -> creat(destination, S_IRUSR | S_IWUSR) -- real POSIX permission bits, same substitution
 * PLATFORM.c's jill_dos_creat() and jill/GAMECTRL.c's macrecend() already made. The actual
 * 4096-byte-chunk copy loop is byte-identical.
 *
 * copyfile() is used by the real save-game path -- fully ported and reachable now: JUNGLE.c's own
 * loadgame() and savegame() (via loadsavewin(), the SAVE/LOAD pause-menu picker) both call it for
 * real to duplicate a save-slot file into/out of the working slot.
 */

#include "COPYFILE.H"
#include "RECOVERY.H"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

void copyfile(char *source, char *destination)
{
    byte *buffer;
    int input;
    int output;
    int count;

    buffer = (byte *)malloc(4096);
    if (buffer == NULL) return;
    input = open(source, O_RDONLY);
    if (input >= 0) {
        output = creat(destination, S_IRUSR | S_IWUSR);
        if (output >= 0) {
            do {
                count = (int)read(input, buffer, 4096);
                if (count > 0) (void)write(output, buffer, (size_t)count);
            } while (count > 0);
            close(output);
        }
        close(input);
    }
    free(buffer);
}

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
 * Android port note (this file, not upstream): same DOSIO.H -> POSIX swap as jill/COPYFILE.c
 * (<fcntl.h>/<unistd.h>/<sys/stat.h>; open/creat/write/close in place of the underscore-prefixed
 * MSVC forms, O_RDONLY in place of _O_BINARY|_O_RDONLY, real S_IRUSR|S_IWUSR permission bits in
 * place of DOS's "0" attributes argument to _creat()). The four _itoa() calls (one for the pixel-
 * dump filename's number, three for each palette entry's scaled R/G/B) become snprintf(), same
 * swap already used in JOBJ.c/JMAN.c/jill/CONFIG.c -- see those files' own header comments.
 * readpix_vga()/pixvalue/pagedraw/pageshow/vgapal are all already real (jill/GRASM.c, jill/GR.c).
 *
 * pixwrite() is a debug screenshot cheat -- reachable today via play()'s own hidden "type W three
 * times in a row" developer cheat (JUNGLE.c's own cheatchar=='W' branch in its main gameplay
 * loop, not any jmenu() dev key) -- upstream itself writes to "\screenN.RAW"/"\screenN.MAP", a
 * DOS-style path meaning "root of the current drive". POSIX has no such concept -- backslash is
 * just an ordinary filename character there, so this still compiles and runs unchanged, it just
 * creates a file literally named "\screen0.RAW" (etc.) in the process's current working directory
 * instead of landing at a drive root. Left as-is rather than "fixed" to a real Android path: the
 * porting discipline here is byte-identical logic with only the minimum change needed to compile
 * -- a nicer output path can be revisited if/when someone actually wants to use this cheat
 * on-device for real.
 */

#include "PIXWRITE.H"
#include "GR.H"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

word swrite;

void pixwrite(int value)
{
    char number[16];
    char name[80];
    char red[16], green[16], blue[16];
    char line[64];
    int handle;
    int x, y, color;
    word oldpage;

    snprintf(number, sizeof(number), "%d", value);
    strcpy(name, "\\screen");
    strcat(name, number);
    strcat(name, ".RAW");
    handle = creat(name, S_IRUSR | S_IWUSR);
    if (handle != -1) {
        oldpage = pagedraw;
        pagedraw = pageshow;
        for (y = 0; y < 200; ++y) {
            for (x = 0; x < 320; ++x) {
                readpix_vga(x, y);
                (void)write(handle, &pixvalue, 1);
            }
        }
        pagedraw = oldpage;
        close(handle);
    }

    strcpy(name, "\\screen");
    strcat(name, number);
    strcat(name, ".MAP");
    handle = creat(name, S_IRUSR | S_IWUSR);
    if (handle != -1) {
        for (color = 0; color < 256; ++color) {
            snprintf(red, sizeof(red), "%d", (int)vgapal[color * 3] * 4);
            snprintf(green, sizeof(green), "%d", (int)vgapal[color * 3 + 1] * 4);
            snprintf(blue, sizeof(blue), "%d", (int)vgapal[color * 3 + 2] * 4);
            strcpy(line, red);
            strcat(line, " ");
            strcat(line, green);
            strcat(line, " ");
            strcat(line, blue);
            strcat(line, "\r\n");
            (void)write(handle, line, (unsigned)strlen(line));
        }
        close(handle);
    }
}

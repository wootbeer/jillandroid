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
 * Android port note (this file, not upstream): same shape as PLATFORM.c/JINFO.c/SHM.c/MUSIC.c's
 * own ports -- upstream's #include "DOSIO.H" is dropped (that header doesn't exist in this
 * project; none of the other ported files use it either -- each just swaps MSVC's <io.h> calls for
 * plain POSIX directly, same here) in favor of <fcntl.h>/<unistd.h> (already had <sys/stat.h>).
 * Four call-site changes, all in playmac()/macrecend(), the only two functions here that touch a
 * file:
 *   1. _open(filename, _O_BINARY | _O_RDONLY) -> open(filename, O_RDONLY) -- no O_BINARY equivalent
 *      needed, same reasoning as SHM.c/MUSIC.c's own port (POSIX has no text/binary file-mode
 *      distinction to begin with).
 *   2. _filelength(handle) -> fstat(handle, &st) then st.st_size. First use of this pattern in the
 *      project (nothing ported so far needed a file's length) -- POSIX has no direct _filelength
 *      equivalent, fstat() is the standard way to ask an already-open handle its size.
 *   3. _read/_write/_close -> read/write/close (<unistd.h>), same as SHM.c/JINFO.c/MUSIC.c.
 *   4. _creat(macfname, 0) -> creat(macfname, S_IRUSR | S_IWUSR) -- real POSIX permission bits in
 *      place of DOS's "attributes" argument, same substitution PLATFORM.c's jill_dos_creat() port
 *      already made for its own _creat() call.
 * Everything else -- the real control-input state machine (checkctrl()/checkctrl0()), the demo
 * macro record/playback format (recmac()/getmac()), gc_init()/gc_exit() -- is byte-identical logic.
 * readspeed()/checkctrl0() read *myclock directly (via the same "(volatile sbyte *)myclock" cast
 * upstream itself uses to read just its low byte) -- this now works for real, since HOSTANDROID.c's
 * host_start_clock() drives a real ticking *myclock (see that file's own header comment for why
 * that thread was added first, as this port's own hard prerequisite).
 *
 * buttona1()/buttona2()/readjoy() are upstream's OWN stand-ins, not this port's -- the decomp
 * itself already replaced the real inportb(0x201) joystick-port reads with "always absent" (see
 * each function's own "Original: ..." comment, left untouched) since there's no DOS game port to
 * read from even on the reference desktop build. Nothing here makes joystick support any less
 * present than upstream's own already-decompiled behavior; gc_config()'s "no joystick found" path
 * (joypresent() always returning 0) means the keyboard/touch-D-pad path in checkctrl() is what
 * actually drives the player, same as it would on real DOS hardware with no joystick plugged in.
 *
 * This retires four of jill_jungle_stubs.c's category-3 placeholder groups (dx1/dy1/fire1/fire2/
 * fire1off/fire2off, key, macplay/macrecord/macabort/macaborted/mactime) and its macrecend() stand-
 * in (category 1) -- see that file's own updated header comment.
 */

#include "GAMECTRL.H"
#include "KEYBOARD.H"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern word gamecount;

longword systime;
word dx1, dy1, fire1, fire2, fire1off, fire2off;
/* Android port addition, not upstream -- see GAMECTRL.H's own comment for the whole "Remap
 * Gamepad" menu-lockout-prevention feature these four drive. */
word menu_confirm, menu_cancel, menu_confirmoff, menu_canceloff;
word joyflag;
word key;
word dx1hold, dy1hold, flow1;
word dx1old, dy1old;
word joyxsense, joyysense;

word macplay, macrecord, macabort, macaborted, mactime;
static char *macptr;
static char macfname[32];
static uword macofs, maclen;

word joyxl, joyxc, joyxr, joyyu, joyyc, joyyd;
static char keybuf[256];

static void game_cputs(const char *text)
{
    if (text != NULL) fputs(text, stdout);
}

int buttona1(void)
{
    /* Original: (inportb(0x201) & 0x10) == 0. */
    return 0;
}

int buttona2(void)
{
    /* Original: (inportb(0x201) & 0x20) == 0. */
    return 0;
}

void readspeed(void)
{
    int oldclock;
    systime = 0;
    oldclock = *(volatile sbyte *)myclock;
    do { } while (*(volatile sbyte *)myclock == oldclock);
    do { ++systime; } while ((*(volatile sbyte *)myclock - oldclock) < 5);
    systime /= 4L;
}

void readjoy(word *x, word *y)
{
    /* A DOS game-port timeout produced exactly this result. */
    *x = -1;
    *y = -1;
}

int caldir(char *text, word *jx, word *jy)
{
    int result = 0;
    int input = 0;
    game_cputs(text);
    do {
        readjoy(jx, jy);
        if (k_pressed()) input = k_read();
    } while (input != escape && !buttona1());
    jill_delay(25);
    if (input != escape) {
        result = 1;
        do {
            if (k_pressed()) input = k_read();
        } while (buttona1() && input != escape);
    }
    jill_delay(25);
    game_cputs("\r\n");
    return result;
}

int joypresent(void)
{
    word x, y;
    readjoy(&x, &y);
    if (x > 0 && y > 0) {
        joyxsense = x;
        joyysense = y;
        return 1;
    }
    return 0;
}

int calibratejoy(void)
{
    int input;
redo:
    joyflag = 0;
    game_cputs("\r\nJoystick calibration:  Press ESCAPE to abort.\r\n");
    if (caldir("  Center joystick and press button: ", &joyxc, &joyyc) &&
        caldir("  Move joystick to UPPER LEFT corner and press button: ",
               &joyxl, &joyyu) &&
        caldir("  Move joystick to LOWER RIGHT corner and press button: ",
               &joyxr, &joyyd)) {
        joyxl -= joyxc;
        joyxr -= joyxc;
        joyyu -= joyyc;
        joyyd -= joyyc;
        if (joyxl < -1 && joyxr > 1 && joyyu < -1 && joyyd > 1) return 1;
        game_cputs("  Calibration failed - try again (y/N)? ");
        do { } while (!k_pressed());
        game_cputs("\r\n");
        input = k_read();
        if (toupper(input) == 'Y') goto redo;
    }
    return 0;
}

void checkctrl(int pollflag)
{
    word x1, y1, xs, ys;

    if (macplay) {
        getmac();
        return;
    }

    dx1 = 0;
    dy1 = 0;
    fire1 = 0;
    flow1 = 0;
reloop:
    key = 0;
    if (k_pressed()) {
        key = (word)k_read();
        if (key == 0 || key == 1 || key == 2) key = (word)k_read();
    }
    if (key != 0) {
        switch (key) {
        case k_up:
        case '8':
            if (pollflag) goto reloop;
            dx1 = 0;
            dy1 = -1;
            break;
        case k_left:
        case '4':
            if (pollflag) goto reloop;
            dx1 = -1;
            dy1 = 0;
            break;
        case k_right:
        case '6':
            if (pollflag) goto reloop;
            dx1 = 1;
            dy1 = 0;
            break;
        case k_down:
        case '2':
            if (pollflag) goto reloop;
            dx1 = 0;
            dy1 = 1;
            break;
        }
    }
    k_status();
    fire1 = k_shift;
    fire2 = k_alt;
    /* Android port addition, not upstream -- see GAMECTRL.H's own menu_confirm/menu_cancel comment
     * for the whole "Remap Gamepad" menu-lockout-prevention feature. Same unconditional-every-call
     * shape as fire1/fire2 just above -- these are never gated by pollflag or the joystick/keydown[]
     * fallback branches below either, exactly like fire1/fire2 aren't. */
    menu_confirm = k_menu_confirm;
    menu_cancel = k_menu_cancel;
    if (dx1 == 0 && dy1 == 0 && joyflag) {
        readjoy(&x1, &y1);
        xs = (word)(x1 - joyxc);
        ys = (word)(y1 - joyyc);
        dx1 = (word)(((2 * xs) > joyxr) - ((2 * xs) < joyxl));
        dy1 = (word)(((2 * ys) > joyyd) - ((2 * ys) < joyyu));
        if (buttona1()) fire1 = 1;
        if (buttona2()) fire2 = 1;
    }
    if (dx1 == 0 && dy1 == 0 && pollflag) {
        if (keydown[0][scan_cursorleft] || keydown[1][scan_cursorleft]) --dx1;
        if (keydown[0][scan_cursorright] || keydown[1][scan_cursorright]) ++dx1;
        if (keydown[0][scan_cursorup] || keydown[1][scan_cursorup]) --dy1;
        if (keydown[0][scan_cursordown] || keydown[1][scan_cursordown]) ++dy1;
    }

    if (fire1) fire1 ^= fire1off;
    else fire1off = 0;
    if (fire2) fire2 ^= fire2off;
    else fire2off = 0;
    /* Android port addition, not upstream -- same carried-over-held-press suppression as fire1/
     * fire2 just above, see GAMECTRL.H's own menu_confirm/menu_cancel comment. */
    if (menu_confirm) menu_confirm ^= menu_confirmoff;
    else menu_confirmoff = 0;
    if (menu_cancel) menu_cancel ^= menu_canceloff;
    else menu_canceloff = 0;
    dx1old = dx1;
    dy1old = dy1;
    if (macrecord) recmac();
}

void checkctrl0(int pollflag)
{
    static word oldclock;
    do { } while (oldclock == *(volatile sbyte *)myclock);
    oldclock = *(volatile sbyte *)myclock;
    checkctrl(pollflag);
}

void sensectrlmode(void)
{
    joyflag = (word)joypresent();
}

int gc_config(void)
{
    int input = ' ';
    if (joypresent()) {
        game_cputs("\r\nGame controller:  K)eyboard,  J)oystick?  ");
        do {
            do { } while (!k_pressed());
            input = toupper(k_read());
        } while (input != 'K' && input != 'J' && input != escape);
        game_cputs("\r\n");
        joyflag = 0;
        if (input == 'J') joyflag = (word)calibratejoy();
    }
    return input != escape;
}

void getkey(void)
{
    do {
        checkctrl(0);
    } while (key == 0);
}

void stopmac(void)
{
    macplay = 0;
    macrecord = 0;
    if (macptr != NULL) {
        free(macptr);
        macptr = NULL;
    }
    macofs = 0;
    mactime = 1;
    srand(12345);
}

void playmac(char *filename)
{
    /* Android port addition, not upstream -- same bug class as JUNGLE.c's loadboard() fix and
     * UNKNOWN.c's OpenElement() fix (both have the full story): the '&' case in JUNGLE.c's own
     * level-transition dispatch calls playmac(newlevel) to play a macro/demo recording, and
     * newlevel is sourced the exact same way as the level filename that crashed -- a checkpoint
     * object's `inside` field, written upper case straight from the original DOS level data.
     * Android's real filesystem is case-sensitive and every real on-disk file JillActivity.java's
     * own copier wrote is lowercase, so an unmodified upper-case name here would silently fail
     * open() (handle < 0 below is already handled -- no crash -- but the macro/demo just never
     * plays). Lower-cased right here, the one place this function's own filename argument is
     * actually used, matching the same fix already applied at loadboard()'s and OpenElement()'s
     * own single choke points. */
    char lowercase_filename[64];
    int handle;
    int idx;

    stopmac();
    macaborted = 0;
    for (idx = 0; filename[idx] != '\0' && idx < (int)sizeof(lowercase_filename) - 1; ++idx)
        lowercase_filename[idx] = (char)tolower((unsigned char)filename[idx]);
    lowercase_filename[idx] = '\0';
    handle = open(lowercase_filename, O_RDONLY);
    if (handle >= 0) {
        struct stat st;
        maclen = (fstat(handle, &st) == 0) ? (uword)st.st_size : 0;
        macptr = (char *)malloc(maclen);
        if (macptr == NULL) macptr = NULL;
        else if (read(handle, macptr, maclen) >= 0) {
            macplay = 1;
            gamecount = 0;
        } else {
            free(macptr);
            macptr = NULL;
        }
        close(handle);
    }
}

void recordmac(char *filename)
{
    stopmac();
    macptr = (char *)malloc(8000);
    if (macptr != NULL) {
        macofs = 0;
        macrecord = 1;
        strcpy(macfname, filename);
        gamecount = 0;
    }
}

void macrecend(void)
{
    int handle;
    if (!macrecord) return;
    handle = creat(macfname, S_IRUSR | S_IWUSR);
    if (handle >= 0) {
        (void)write(handle, macptr, macofs);
        close(handle);
    }
    stopmac();
}

void recmac(void)
{
    static word curdx1, curdy1, curfire1, curfire2, oldclock;
    word dt;
    byte bits;

    if (key == '[') { mactime = 0; key = 0; }
    if (key == ']') { mactime = 1; key = 0; }
    if (key == '}') { macrecend(); return; }
    if (macofs == 0) {
        curdx1 = curdy1 = curfire1 = curfire2 = 0;
        oldclock = gamecount;
    }
    bits = (byte)(((curdx1 != dx1) << 0) |
                  ((curdy1 != dy1) << 1) |
                  ((curfire1 != fire1) << 2) |
                  ((curfire2 != fire2) << 3) |
                  (((key > 0) && (key <= 127)) << 4));
    if (bits) {
        if (macofs != 0) {
            if (mactime == 0) dt = 1;
            else dt = (word)(gamecount - oldclock);
            if (dt < 128) macptr[macofs++] = (char)dt;
            else {
                macptr[macofs++] = (char)((dt & 127) | 128);
                macptr[macofs++] = (char)(dt >> 7);
            }
        }
        macptr[macofs++] = (char)bits;
        if (bits & 1) macptr[macofs++] = (char)dx1;
        if (bits & 2) macptr[macofs++] = (char)dy1;
        if (bits & 4) macptr[macofs++] = (char)fire1;
        if (bits & 8) macptr[macofs++] = (char)fire2;
        if (bits & 16) macptr[macofs++] = (char)key;
        curdx1 = dx1;
        curdy1 = dy1;
        curfire1 = fire1;
        curfire2 = fire2;
    }
    if (macofs >= 30000) macrecend();
}

void getmac(void)
{
    static word oldclock, nextdt;
    int tempkey;
    byte bits;

    if (k_pressed()) {
        tempkey = k_read();
        if (macabort == 0 || (macabort == 1 && tempkey == escape)) {
            stopmac();
            macaborted = 1;
            /* Android port addition, not upstream -- real, reproducible NULL-pointer SIGSEGV,
             * confirmed on-device (wootbeer's own report: exiting Episode 1's DEMO via "nearly any"
             * gamepad button crashed instead of returning to the main menu; Logcat's own full
             * backtrace pinned it to getmac()+304, fault addr 0x0, called from checkctrl() <-
             * play() <- dodemo() <- jmenu()). Root cause: stopmac() just above frees macptr AND
             * sets it to NULL (GAMECTRL.c's own stopmac(), a few lines up this file) -- but
             * without this return, execution falls straight through into the rest of this
             * function as if playback were still live. stopmac() also unconditionally resets
             * macofs to 0, so the very next check below (`if (macofs == 0) { ...; oldclock =
             * gamecount; nextdt = 0; }`) always fires right after an abort, which makes the
             * following one (`if ((word)(gamecount - oldclock) >= nextdt)`, now comparing
             * gamecount against itself with nextdt freshly zeroed) always true too -- so
             * `macptr[macofs++]` a few lines further down always runs immediately after ANY
             * abort, unconditionally dereferencing the macptr this same call just freed and
             * NULLed. Not a timing-dependent or occasional crash: every single demo abort hits
             * this, deterministically, regardless of which key/button triggered it. This has to
             * be a real reconstruction gap rather than authentic upstream behavior -- a real DOS
             * EXE quitting a demo on keypress obviously never crashed -- most likely a missing
             * early-out that the original assembly had and this decomp lost; nothing here can
             * recover that original control flow exactly, so this adds the narrowest fix that
             * restores the obviously-intended behavior: stop touching macptr/macofs at all once
             * this same call has just stopped the macro, and leave every per-tick output in the
             * same harmless "nothing happened this tick" state the normal macofs==0 branch below
             * already establishes, so whatever reads dx1/dy1/fire1/fire2/key right after this
             * call (checkctrl()'s own caller) sees a clean, zeroed tick rather than stale values
             * from before the abort. */
            key = 0;
            dx1 = dy1 = fire1 = fire2 = 0;
            return;
        }
    }
    key = 0;
    if (macofs == 0) {
        dx1 = dy1 = fire1 = fire2 = 0;
        oldclock = gamecount;
        nextdt = 0;
    }
    if ((word)(gamecount - oldclock) >= nextdt) {
        bits = (byte)macptr[macofs++];
        if (bits & 1) dx1 = (word)(sbyte)macptr[macofs++];
        if (bits & 2) dy1 = (word)(sbyte)macptr[macofs++];
        if (bits & 4) fire1 = (word)(sbyte)macptr[macofs++];
        if (bits & 8) fire2 = (word)(sbyte)macptr[macofs++];
        if (bits & 16) key = (word)(sbyte)macptr[macofs++];
        nextdt = (word)(sbyte)macptr[macofs++];
        if (nextdt < 0) {
            nextdt = (word)((nextdt & 127) +
                            ((word)(sbyte)macptr[macofs++] << 7));
        }
    }
    if (macofs >= maclen) stopmac();
}

void gc_init(void)
{
    dx1 = dy1 = fire1 = fire1off = 0;
    dx1old = dy1old = 0;
    dx1hold = dy1hold = 0;
    keybuf[0] = 0;
    macplay = macrecord = 0;
    macabort = 1;
    joyflag = 0;
    installhandler(1);
}

void gc_exit(void)
{
    removehandler();
}

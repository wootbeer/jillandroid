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
 * Android port note (this file, not upstream): two MSVC-only helpers, both call-site swaps rather
 * than anything behavioral:
 *   1. _strupr(argv[index]) (in-place uppercase, MSVC <string.h>) -> a small local jill_strupr()
 *      loop over toupper() (<ctype.h>, already included) -- same idea as the project's existing
 *      _itoa()->snprintf() swaps (JOBJ.c/JMAN.c), just for a function with no portable libc
 *      equivalent at all rather than a portable near-equivalent.
 *   2. _ltoa(systime, string, 10) -> snprintf(string, sizeof(string), "%d", (int)systime) -- same
 *      _itoa()/_ltoa()-style-call -> snprintf() swap already used elsewhere in this project (see
 *      JOBJ.c/JMAN.c's own header comments), just for the long-taking variant. systime is a
 *      longword (int32_t, see RECOVERY.H) -- cast to int for the format specifier, safe here since
 *      this only ever prints systime's real range (a small tick-count-derived timing figure, see
 *      readspeed() in jill/GAMECTRL.c), never a value near INT32_MAX.
 * Everything else here is unchanged: host_console_clear()/host_coreleft() (HOSTANDROID.c already
 * implements both, see that file's own host_* section), joypresent()/checkctrl()/gc_config()
 * (jill/GAMECTRL.c, just ported), gr_config() (still unported, but unreachable here -- see below).
 *
 * doconfig()'s big "print your configuration back and let the player change it" block is wrapped
 * in upstream's OWN "#if 0 // jmarshall" (not a macro -- literally always false), so it's dead code
 * on the reference desktop build too, not just here -- none of its host_coreleft()/gr_config() etc.
 * calls need to link. What's left reachable is exactly what a first-ever run needs: detect speed
 * via readspeed(), read the joystick/sound/video state out of the persisted ConfigState (cf) or
 * default it, and hand back cf.firstthru's/joyflag's real values. gr_config() -- the CGA/EGA/VGA
 * picker, GR.c's own function -- is only called from inside that dead code, so it staying unported
 * is not a gap this checkpoint needs to close.
 *
 * CONFIG.H is vendored unchanged (no MSVC-specific declarations in it to begin with), same as
 * RECOVERY.H/GR.H/HOSTSDL.H/HOSTAUDIO.H. Renamed CONFIG.C -> CONFIG.c (lowercase extension only,
 * base name unchanged) for the same CMake-capital-.C-means-C++ reason as every other ported .c
 * file in this project -- see jill/HOSTANDROID.c's own header comment / this project's
 * CMakeLists.txt comment.
 *
 * This retires jill_jungle_stubs.c's nosnd placeholder (its real storage is right here, matching
 * upstream's own "word nosnd;" at this file's own top-level scope) -- see that file's own updated
 * header comment.
 *
 * One real, deliberate behavioral deviation, found the hard way once real audio (HOSTANDROID.c's
 * host_audio_* functions, descore/audio_opl/) was actually working end to end but a real device
 * still produced total silence: doconfig() itself, see that function's own comment at the
 * cf.musicflag0/cf.vocflag0 assignment. Short version -- upstream's own logic only ever clears those
 * two persisted flags to match a failed hardware check, and the only code that would ever set them
 * back to true (the interactive Sound-Blaster-detected wizard) is dead on every build of this
 * reconstruction, not just Android's -- so once any single run's audio detection ever came back
 * false (inevitably true for this port's own early sessions, before host_audio_* was real), the
 * persisted jillN.cfg permanently disabled both sound effects and music for every run after, no
 * matter how correct the detection became later. Fixed to mirror live detection symmetrically each
 * run instead of only ever ratcheting it down.
 */

#include "CONFIG.H"
#include "EPISODE.H"
#include "GAMECTRL.H"
#include "GR.H"
#include "HOSTSDL.H"
#include "KEYBOARD.H"
#include "MUSIC.H"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void jill_strupr(char *text)
{
    if (text == NULL) return;
    while (*text != '\0') {
        *text = (char)toupper((unsigned char)*text);
        ++text;
    }
}

char cfg_path[260] = "";
word nosnd;
word cfgdemo;

ConfigState cf = { 0, 0, 0, 0, 0, 0, 0, 0, x_vga, 1, 1 };

_Static_assert(sizeof(ConfigState) == 22, "configuration record must be 22 bytes");

void cfg_getpath(int argc, char **argv)
{
    int index;
    for (index = 0; index < argc; ++index) {
        jill_strupr(argv[index]);
        if (argv[index][0] == '/' && argv[index][1] == 'P')
            strcpy(cfg_path, argv[index] + 2);
    }
}

void cfg_init(int argc, char **argv)
{
    int index;

    host_console_clear();
    fputs("\r\n\r\nDetecting your hardward...\r\n", stdout);
    fputs("\r\nIf your system locks, reboot and type:\r\n", stdout);
    fputs("   " JILL_PROGRAM_NAME " /NOSB  (No Sound Blaster card)\r\n", stdout);
    fputs("   " JILL_PROGRAM_NAME " /SB    (With a Sound Blaster)\r\n", stdout);
    fputs("   " JILL_PROGRAM_NAME " /NOSND (If all else fails)\r\n", stdout);
    readspeed();
    for (index = 0; index < argc; ++index) {
        jill_strupr(argv[index]);
        if (strcmp(argv[index], "/TEST") == 0) {
            char string[16];
            snprintf(string, sizeof(string), "%d", (int)systime);
            fputs(string, stdout);
            getkey();
        } else if (strcmp(argv[index], "/NOSB") == 0) {
            vocflag = musicflag = 0;
        } else if (strcmp(argv[index], "/SB") == 0) {
            /* The original switch is accepted but makes no assignment. */
        } else if (strcmp(argv[index], "/NOSND") == 0) {
            vocflag = musicflag = 0;
            nosnd = 1;
        } else if (strcmp(argv[index], "/DEMO") == 0) {
            cfgdemo = 1;
        }
    }
}

int doconfig(void)
{
    int configure = cf.firstthru;
    char string[16];

    if (!configure) {
        x_ourmode = (byte)cf.video_mode;
        joyflag = (word)joypresent();
        if (!joyflag) {
            cf.joyflag0 = 0;
        } else if (cf.joyflag0) {
            joyxl = cf.joyxl0;
            joyxc = cf.joyxc0;
            joyxr = cf.joyxr0;
            joyyu = cf.joyyu0;
            joyyc = cf.joyyc0;
            joyyd = cf.joyyd0;
            checkctrl(0);
            configure |= dx1 != 0 || dy1 != 0;
        }
        /* Android port note (this file, not upstream): upstream's own logic here only ever CLEARS
         * cf.musicflag0/cf.vocflag0 (to match a hardware-detection failure) -- the only reachable
         * code that would ever set them back to true is the interactive "a Sound Blaster was
         * detected, do you want sound?" wizard a little further down, and that whole block is
         * permanently dead code even on the reference desktop build (upstream's own #if 0 //
         * jmarshall, see this file's own header comment). So on any real build of this
         * reconstruction, not just here, cf.musicflag0/cf.vocflag0 are a one-way ratchet: once a
         * single run's own hardware detection ever comes back false, nothing can ever set them true
         * again. Combined with doconfig()'s own trailing lines below (`vocflag = cf.vocflag0;
         * musicflag = cf.musicflag0;`, unconditional every run) and JUNGLE.c's own main() always
         * savecfg()-ing whatever comes out of that back to jillN.cfg, one early test run with this
         * port's own host_audio_* functions still stubbed (returning "not available") permanently
         * poisons every later run's persisted config -- confirmed on-device: real digitized VOC
         * effects and real CMF/AdLib music are now both fully implemented and correctly detected
         * live by snd_init()'s own AdlibDetect()/DSPReset() calls every single run, but a jillN.cfg
         * saved back when those were still stubs kept silently re-disabling both forever after, with
         * no way back short of clearing the app's data. Fixed by mirroring the clear side with a
         * matching set-to-true side, so cf.musicflag0/cf.vocflag0 simply track this run's own live
         * detection -- exactly what the (unreachable) interactive wizard would otherwise let a real
         * player do by hand each time their answer changed. */
        cf.musicflag0 = (word)(musicflag != 0);
        cf.vocflag0 = (word)(vocflag != 0);
    }
#if 0 // jmarshall
    if (!configure) {
        host_console_clear();
        fputs("\r\n", stdout);
        fputs(" Your configuration:\r\n", stdout);
        if (cf.vocflag0)
            fputs("    Digital Sound Blaster sound effects ON\r\n", stdout);
        else
            fputs("    No digitized sound effects\r\n", stdout);
        if (cf.musicflag0)
            fputs("    Sound Blaster musical sound track ON\r\n", stdout);
        else
            fputs("    No musical sound track\r\n", stdout);
        if (cf.joyflag0)
            fputs("    A joystick\r\n", stdout);
        else
            fputs("    No joystick\r\n", stdout);
        if (x_ourmode == x_cga) {
            fputs("    CGA graphics (You're missing some\r\n", stdout);
            fputs("    hot 256-color VGA scenery!)\r\n", stdout);
        } else if (x_ourmode == x_ega) {
            fputs("    16-color EGA graphics\r\n", stdout);
        } else {
            fputs("    256-color VGA graphics\r\n", stdout);
        }
        fputs("\r\n", stdout);
        fputs("  Press ENTER if this is correct\r\n", stdout);
        fputs("      or press 'C' to configure: ", stdout);

        do {
            getkey();
            key = (word)toupper(key);
        } while (key != enter && key != 'C' && key != escape);
        if (key == 'C') configure = 1;
        if (key == escape) return 0;
    }

    if (configure) {
        host_console_clear();
        if (!vocflag && !musicflag) {
            fputs("\r\n", stdout);
            fputs(" No Sound Blaster-compatible music card has been\r\n", stdout);
            fputs(" detected.\r\n\r\n", stdout);
            fputs(" Press any key to continue...", stdout);
            getkey();
        }

        if (vocflag && systime < 4000L) {
            fputs("\r\n\r\n", stdout);
            fputs(" A Sound Blaster card was detected, but your CPU is\r\n", stdout);
            fputs(" too slow to support digitized sound.  Digital sound\r\n", stdout);
            fputs(" is now OFF.\r\n\r\n", stdout);
            fputs(" Press any key to continue...", stdout);
            getkey();
        } else if (vocflag) {
            fputs(" A Sound Blaster card has been detected.\r\n\r\n", stdout);
            fputs(" This game will play high-quality digital sound\r\n", stdout);
            fputs(" through your Sound Blaster if you wish.\r\n\r\n", stdout);
            fputs(" Warning:  There's a teeny chance this will cause\r\n", stdout);
            fputs(" problems if you have less than 640K of RAM, or\r\n", stdout);
            fputs(" if your computer is not totally compatible.\r\n\r\n", stdout);
            fputs(" Do you want digital sound? ", stdout);
            do {
                getkey();
                key = (word)toupper(key);
                if (key == '~') {
                    _ltoa(host_coreleft(), string, 10);
                    fputs(string, stdout);
                }
                if (key == escape) return 0;
            } while (key != 'Y' && key != 'N');
            cf.vocflag0 = (word)(key == 'Y');
        }

        if (musicflag) {
            host_console_clear();
            fputs("\r\n\r\n\r\n", stdout);
            fputs(" This game features a Sound Blaster-compatible\r\n", stdout);
            fputs(" musical sound track.\r\n\r\n\r\n", stdout);
            fputs(" Do you want the musical sound track? ", stdout);
            do {
                getkey();
                key = (word)toupper(key);
                if (key == escape) return 0;
            } while (key != 'Y' && key != 'N');
            cf.musicflag0 = (word)(key == 'Y');
        }

        host_console_clear();
        fputs("\r\n", stdout);
        if (!gc_config()) return 0;
        cf.joyflag0 = joyflag;

        host_console_clear();
        fputs("\r\n", stdout);
        fputs(" Please tell us about your graphics:\r\n", stdout);
        fputs("     CGA 4-color graphics\r\n", stdout);
        fputs("     EGA 16-color graphics\r\n", stdout);
        fputs("     VGA 256-color graphics\r\n", stdout);
        fputs("\r\n", stdout);
        fputs(" Note: If you have a slow old computer, CGA\r\n", stdout);
        fputs("       graphics are recommended.\r\n", stdout);
        if (!gr_config()) return 0;
    }
#endif
    if (systime < 4000L) {
        vocflag = 0;
        cf.vocflag0 = 0;
    }
    cf.firstthru = 0;
    joyflag = cf.joyflag0;
    cf.joyxl0 = joyxl;
    cf.joyxc0 = joyxc;
    cf.joyxr0 = joyxr;
    cf.joyyu0 = joyyu;
    cf.joyyc0 = joyyc;
    cf.joyyd0 = joyyd;
    cf.video_mode = x_ourmode;
    vocflag = cf.vocflag0;
    musicflag = cf.musicflag0;
    return 1;
}

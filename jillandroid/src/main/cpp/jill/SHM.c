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
 * Android port note (this file, not upstream): same shape as PLATFORM.c's own port -- the only
 * real change is swapping MSVC's <io.h> (_open/_read/_lseek/_close, _O_BINARY|_O_RDONLY) for
 * POSIX (<fcntl.h>/<unistd.h>: open/read/lseek/close, O_RDONLY -- there's no O_BINARY on
 * Android/bionic; POSIX has no distinct text/binary file modes to begin with, so it's simply
 * dropped rather than mapped to anything). Every byte read/written and the xlate_table() decode
 * logic itself are unchanged. Renamed SHM.C -> SHM.c for the same CMake gotcha as jill/GR.c (see
 * that file's own header comment).
 *
 * rexit(int result) (called from a couple of this file's own error paths -- a bad/missing .SHA
 * file, or a failed malloc) is JUNGLE.C's own function (savecfg() + snd_exit() + shm_exit() +
 * gc_exit() + gr_exit() + host_close() + exit()) -- JUNGLE.C, the real game loop, isn't ported
 * yet. HOSTANDROID.c currently provides a small temporary stand-in with the same name/signature
 * so this file links and fails safely (closes what Android-side state exists, then exits) instead
 * of leaving the symbol undefined -- see that stand-in's own comment for when/how to remove it.
 */
#include "SHM.H"
#include "GR.H"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

word shm_want[SHM_MAX_TABLES];
byte *shm_tbladdr[SHM_MAX_TABLES];
word shm_tbllen[SHM_MAX_TABLES];
word shm_flags[SHM_MAX_TABLES];

static int shafile = -1;
static char shm_filename[80];
static ulongword shm_offset[128];
static uword shm_length[128];
static byte color_table[256];
static const byte ega_color_table[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x00, 0x08, 0x08, 0x07, 0x07, 0x07, 0x0F, 0x0F, 0x00, 0x04, 0x0C, 0x0C, 0x08, 0x08, 0x02, 0x06,
    0x06, 0x0C, 0x02, 0x02, 0x02, 0x06, 0x06, 0x0E, 0x02, 0x02, 0x02, 0x02, 0x06, 0x0E, 0x0A, 0x0A,
    0x0A, 0x0E, 0x0E, 0x0A, 0x0A, 0x0A, 0x0E, 0x0E, 0x00, 0x00, 0x00, 0x04, 0x04, 0x05, 0x00, 0x00,
    0x00, 0x04, 0x0C, 0x0C, 0x08, 0x08, 0x07, 0x06, 0x04, 0x0C, 0x02, 0x02, 0x02, 0x06, 0x06, 0x06,
    0x02, 0x02, 0x02, 0x02, 0x06, 0x06, 0x0A, 0x0A, 0x0A, 0x0E, 0x0E, 0x0A, 0x0A, 0x0A, 0x0E, 0x0E,
    0x00, 0x00, 0x04, 0x04, 0x0C, 0x0C, 0x00, 0x00, 0x04, 0x04, 0x0C, 0x0C, 0x08, 0x08, 0x08, 0x06,
    0x06, 0x0C, 0x02, 0x02, 0x08, 0x08, 0x06, 0x06, 0x02, 0x02, 0x02, 0x02, 0x06, 0x06, 0x0A, 0x0A,
    0x0A, 0x0E, 0x0E, 0x09, 0x09, 0x09, 0x0E, 0x0E, 0x01, 0x01, 0x05, 0x05, 0x04, 0x02, 0x01, 0x01,
    0x05, 0x05, 0x0D, 0x0D, 0x01, 0x01, 0x08, 0x05, 0x05, 0x0C, 0x03, 0x03, 0x07, 0x07, 0x07, 0x0C,
    0x03, 0x03, 0x03, 0x07, 0x07, 0x0E, 0x02, 0x02, 0x03, 0x07, 0x07, 0x0A, 0x0A, 0x0A, 0x0E, 0x0E,
    0x01, 0x01, 0x01, 0x05, 0x0D, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x0D, 0x0D, 0x01, 0x01, 0x05, 0x05,
    0x0D, 0x0D, 0x09, 0x09, 0x09, 0x05, 0x05, 0x0E, 0x03, 0x03, 0x03, 0x07, 0x07, 0x0F, 0x04, 0x04,
    0x0B, 0x07, 0x07, 0x0B, 0x0B, 0x0B, 0x0F, 0x0F, 0x09, 0x09, 0x09, 0x05, 0x0D, 0x0D, 0x09, 0x09,
    0x09, 0x05, 0x0D, 0x0D, 0x09, 0x09, 0x09, 0x05, 0x05, 0x0D, 0x09, 0x09, 0x09, 0x09, 0x05, 0x0D,
    0x0B, 0x0B, 0x03, 0x03, 0x0D, 0x0D, 0x0B, 0x0B, 0x0D, 0x0F, 0x0D, 0x0D, 0x0D, 0x0F, 0x0F, 0x00,
};

extern void rexit(int result);

void shm_init(char *filename)
{
    int table;
    strcpy(shm_filename, filename);
    for (table = 0; table < SHM_MAX_TABLES; ++table) {
        shm_want[table] = 0;
        shm_tbladdr[table] = NULL;
    }
    shafile = open(shm_filename, O_RDONLY);
    if (shafile < 0) rexit(115);
    if (read(shafile, shm_offset, sizeof(shm_offset)) == 0) rexit(102);
    if (read(shafile, shm_length, sizeof(shm_length)) == 0) rexit(102);
}

void init8bit(void)
{
    int color;

    switch (x_ourmode & 0xfe) {
    case x_cga:
        for (color = 0; color < 256; ++color)
            color_table[color] = (byte)(color & 3);
        break;
    case x_ega:
        memcpy(color_table, ega_color_table, sizeof(color_table));
        break;
    case x_vga:
        for (color = 0; color < 256; ++color)
            color_table[color] = (byte)color;
        break;
    }
    for (color = 0; color < 256; ++color) color_table[color] = (byte)color;
}

/* Android port change, not upstream -- xlate_table() below (its real signature/call site,
 * shm_do(), both completely unchanged) is now a thin wrapper around this: everything it used to do
 * is still done exactly the same way, just through out_addr/out_len/out_flags pointers instead of
 * writing shm_tbladdr[table_number]/shm_tbllen[table_number]/shm_flags[table_number] directly.
 * That's *all* this split changes -- every line of real decode logic below is byte-for-byte
 * identical to before. Done so shm_load_single_table() (SHM.H's own comment has the whole "Jill
 * Color" Enhancements feature this exists for) can reuse the exact same decode logic against a
 * completely different, one-shot table/buffer -- one NOT going into the live shm_tbladdr[] registry
 * at all -- without duplicating any of it. */
static void xlate_table_core(byte *source, byte *work_buffer, byte **out_addr, word *out_len,
                             word *out_flags)
{
    byte number_of_shapes = 0;
    byte color_bits = 1;
    uword rotations, cga_length, ega_length, vga_length, flags;
    uword output_length;
    uword color_mask, color_shift;
    uword table_offset, data_offset;
    byte *output;
    byte shape;

    memcpy(&number_of_shapes, source, 1); source += 1;
    rotations = jill_read_u16_le(source); source += 2;
    cga_length = jill_read_u16_le(source); source += 2;
    ega_length = jill_read_u16_le(source); source += 2;
    vga_length = jill_read_u16_le(source); source += 2;
    memcpy(&color_bits, source, 1); source += 1;
    flags = jill_read_u16_le(source); source += 2;
    (void)rotations;

    if (x_ourmode == x_cga) {
        output_length = (uword)(cga_length + 16);
        color_mask = 3;
        color_shift = 0;
    } else if (x_ourmode == x_ega) {
        output_length = (uword)(ega_length + 16);
        color_mask = 15;
        color_shift = 8;
    } else {
        output_length = (uword)(vga_length + 16);
        color_mask = 255;
        color_shift = 16;
    }

    if ((flags & shm_fontf) != 0) {
        unsigned color;
        unsigned count = 1U << color_bits;
        for (color = 0; color < count; ++color) color_table[color] = (byte)color;
    } else if (color_bits == 8) {
        init8bit();
    } else {
        unsigned color;
        unsigned count = 1U << color_bits;
        for (color = 0; color < count; ++color) {
            ulongword packed = jill_read_u32_le(source);
            source += 4;
            color_table[color] = (byte)((packed >> color_shift) & color_mask);
        }
    }

    output = (byte *)malloc(output_length);
    if (output == NULL) rexit(100);
    *out_len = (word)output_length;
    *out_addr = output;
    *out_flags = (word)flags;
    table_offset = 0;
    data_offset = (uword)((uword)number_of_shapes * 4U);

    for (shape = 0; shape < number_of_shapes; ++shape) {
        byte width, width_bytes, height, storage;
        int x, y, plane;
        byte packed_byte, shape_byte;

        memcpy(&width, source, 1); source += 1;
        memcpy(&height, source, 1); source += 1;
        memcpy(&storage, source, 1); source += 1;
        (void)storage;
        memcpy(work_buffer, source, (uword)width * height);
        source += (uword)width * height;

        if (color_bits == 8 && width == 64 && height == 12 &&
            x_ourmode == x_vga) {
            memmove(vgapal, work_buffer, sizeof(vgapal));
            vga_setpal();
        }

        if (x_ourmode == x_cga) width_bytes = (byte)((width + 3) / 4);
        else width_bytes = width;
        jill_write_u16_le(output + table_offset, (uword)data_offset);
        output[table_offset + 2] = width_bytes;
        output[table_offset + 3] = height;
        table_offset += 4;

        if (x_ourmode == x_cga) {
            for (y = 0; y < height; ++y) {
                packed_byte = 0;
                for (x = 0; x < width; ++x) {
                    shape_byte = color_table[work_buffer[x + y * width]];
                    packed_byte |= (byte)(shape_byte << (6 - ((x & 3) << 1)));
                    if ((x & 3) == 3 || x == width - 1) {
                        output[data_offset++] = packed_byte;
                        packed_byte = 0;
                    }
                }
            }
        } else if (x_ourmode == x_ega || x_ourmode == x_egagrey) {
            if ((flags & shm_blflag) == 0) {
                for (y = 0; y < height; ++y) {
                    for (x = 0; x < width; x += 2) {
                        packed_byte = color_table[work_buffer[x + y * width]];
                        packed_byte |= (byte)(color_table[work_buffer[x + 1 + y * width]] << 4);
                        output[data_offset++] = packed_byte;
                    }
                }
            } else {
                for (plane = 8; plane > 0; plane >>= 1) {
                    for (y = 0; y < height; ++y) {
                        packed_byte = 0;
                        for (x = 0; x < width; ++x) {
                            shape_byte = color_table[work_buffer[x + y * width]];
                            packed_byte |= (byte)(((shape_byte & plane) != 0) <<
                                                  (7 - (x & 7)));
                            if ((x & 7) == 7 || x == width - 1) {
                                output[data_offset++] = packed_byte;
                                packed_byte = 0;
                            }
                        }
                    }
                }
            }
        } else if ((flags & shm_blflag) == 0) {
            for (y = 0; y < height; ++y)
                for (x = 0; x < width; ++x)
                    output[data_offset++] = color_table[work_buffer[x + y * width]];
        } else {
            for (plane = 3; plane >= 0; --plane)
                for (y = 0; y < height; ++y)
                    for (x = 0; x < width; x += 4)
                        output[data_offset++] =
                            color_table[work_buffer[x + plane + y * width]];
        }

        if (data_offset >= output_length) exit(199);
    }
}

/* The real, upstream-signature entry point -- unchanged behavior, now just a thin pass-through
 * into xlate_table_core() above (see that function's own comment). shm_do() below is still this
 * function's only real caller. */
void xlate_table(int table_number, byte *source, byte *work_buffer)
{
    xlate_table_core(source, work_buffer, &shm_tbladdr[table_number], &shm_tbllen[table_number],
                     &shm_flags[table_number]);
}

/* Android port addition, not upstream -- see SHM.H's own comment for the full contract. */
int shm_load_single_table(const char *filename, int table_number, byte **out_addr, word *out_len,
                          word *out_flags)
{
    int fd;
    ulongword offsets[128];
    uword lengths[128];
    byte *encoded;
    byte *work_buffer;

    *out_addr = NULL;
    *out_len = 0;
    *out_flags = 0;

    if (table_number < 0 || table_number >= 128) return 0;

    fd = open(filename, O_RDONLY);
    if (fd < 0) return 0;

    if (read(fd, offsets, sizeof(offsets)) != (int)sizeof(offsets) ||
        read(fd, lengths, sizeof(lengths)) != (int)sizeof(lengths)) {
        close(fd);
        return 0;
    }
    if (lengths[table_number] == 0) {
        close(fd);
        return 0;
    }

    encoded = (byte *)malloc(lengths[table_number]);
    if (encoded == NULL) {
        close(fd);
        return 0;
    }
    (void)lseek(fd, (long)offsets[table_number], SEEK_SET);
    if (read(fd, encoded, lengths[table_number]) != (int)lengths[table_number]) {
        free(encoded);
        close(fd);
        return 0;
    }

    work_buffer = (byte *)malloc(0x1000);
    if (work_buffer == NULL) {
        free(encoded);
        close(fd);
        return 0;
    }

    /* Same decode logic the primary shafile's own shm_do()->xlate_table() uses (see
     * xlate_table_core()'s own comment) -- table_number here indexes THIS file's own offset/length
     * header, decoded into out_addr/out_len/out_flags directly rather than the live
     * shm_tbladdr[]/shm_tbllen[]/shm_flags[] registry, so nothing about the primary shafile's own
     * currently-loaded tables is disturbed. Still calls rexit(100)/exit(199) on a real malloc
     * failure or a shape stream that overruns its own declared output_length, same as the primary
     * path -- both are hard-failure conditions that should never legitimately occur for a valid,
     * unmodified jillN.sha (this is the exact same format/decoder already successfully parsing the
     * CURRENT episode's own file), not something worth a softer fallback here. */
    xlate_table_core(encoded, work_buffer, out_addr, out_len, out_flags);

    free(work_buffer);
    free(encoded);
    close(fd);
    return *out_addr != NULL;
}

void shm_do(void)
{
    byte *work_buffer;
    int table;

    work_buffer = (byte *)malloc(0x1000);
    if (work_buffer == NULL) rexit(101);

    for (table = 0; table < SHM_MAX_TABLES; ++table) {
        if (!shm_want[table] && shm_tbladdr[table] != NULL) {
            free(shm_tbladdr[table]);
            shm_tbladdr[table] = NULL;
        }
    }

    for (table = 0; table < SHM_MAX_TABLES; ++table) {
        if (shm_want[table] && shm_tbladdr[table] == NULL &&
            shm_length[table] != 0) {
            byte *encoded;
            (void)lseek(shafile, (long)shm_offset[table], SEEK_SET);
            encoded = (byte *)malloc(shm_length[table]);
            if (encoded == NULL) rexit(103);
            (void)read(shafile, encoded, shm_length[table]);
            xlate_table(table, encoded, work_buffer);
            free(encoded);
        }
    }
    free(work_buffer);
}

void shm_exit(void)
{
    int table;
    close(shafile);
    for (table = 0; table < SHM_MAX_TABLES; ++table) {
        free(shm_tbladdr[table]);
        shm_tbladdr[table] = NULL;
    }
}

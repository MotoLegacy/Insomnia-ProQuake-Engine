/*
Copyright (c) 2016-2019 Marco "eukara" Hladik <marco@icculus.org>
Copyright (C) 2023 Ivy Bowling

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//
// Image to Quake Palette Converter
// -------
// Simple/Fast converter that takes in an image via stb image
// and estimates the color values to the supplied Quake Palette.
// Not perfect, the distance calculation is rather rudamentary
// in order to value speed over perfection. Notably confuses some
// oranges as purples when using the standard Quake 1 Palette.
//

#include "quakedef.h"
#include <fcntl.h>

#define PALETTE_ENTRIES     256                     // Quake Palettes store 256 Color entries.

//
// get_palette_index_of_closest(r, g, b, a)
// -------
// Takes in a set of 24 bit RGBA values and returns the index
// for the best found match in the qpallete array.
//
byte get_palette_index_of_closest(byte* r, byte* g, byte* b, byte* a)
{
    // First check: does this pixel have a translucency < 50%?
    // Consider it transparent.
    if (a < 127)
        return 255;

    int i, j;
    int best_index = -1;
    int best_dist = 0;
    int rgb[3];

    rgb[0] = (int)r;
    rgb[1] = (int)g;
    rgb[2] = (int)b;

    for(i = 0; i < PALETTE_ENTRIES; i++) {
        int dist = 0;

        for(j = 0; j < 3; j++)
        {
            int d = abs(rgb[j] - host_basepal[i * 3 + j]);
            dist += d * d;
        }

        if (best_index == -1 || dist < best_dist) {
            best_index = i;
            best_dist = dist;
        }
    }

    return (byte)best_index;
}

//
// convert_image_to_lmp(image_data, out_name, width, height)
// -------
// Loads the input data provided by image_data, iterates
// over all of its pixels, and stores the best matching
// Quake Palette index for them. It'll write the output
// data to an .LMP file for cache as well as return it.
//
int last_printed_number;
byte *convert_image_to_lmp(byte* image_data, char* out_name, int width, int height)
{
    if (image_data != NULL) {
        // Create the Quake .LMP file.
        byte* lump_data;
        lump_data = malloc(width * height + 8);     // Extra 8 bytes for dimension information.

        // Credit to eukara - Store the image dimension data.
        lump_data[3] = (width >> 24) & 0xFF;
        lump_data[2] = (width >> 16) & 0xFF;
        lump_data[1] = (width >> 8) & 0xFF;
        lump_data[0] = width & 0xFF;
        lump_data[7] = (height >> 24) & 0xFF;
        lump_data[6] = (height >> 16) & 0xFF;
        lump_data[5] = (height >> 8) & 0xFF;
        lump_data[4] = height & 0xFF;

        // Iterate over every pixel in the RGBA image data.
        int pixel_tracker = 0;
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                // Just a little fancy status print thing.. for potentially big images..
                int progress = (int)((double)pixel_tracker / (width*height) * 100);
                if (progress % 25 == 0) {
                    if (last_printed_number != progress)
                        Con_Printf("%d%%..", progress);
                    last_printed_number = progress;
                }

                // Grab the pixel byte position, 4 bytes for RGBA.
                byte* pixel = image_data + (4 * (i * width + j));

                // Store them individually.
                byte* r = pixel[0];
                byte* g = pixel[1];
                byte* b = pixel[2];
                byte* a = pixel[3];

                // Now grab a Palette index approimixation and store it in the Lump.
                lump_data[8 + pixel_tracker] = get_palette_index_of_closest(r, g, b, a);
                pixel_tracker++;
            }
        }

        COM_WriteFile(out_name, (void*)lump_data, (width * height) + 8);
        return;
    }
}
/*
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
// qex.c  -- Support for the 2021 Quake Enhanced Re-Release, seperate from
// compat.c because there is a lot going on here..
#include "quakedef.h"

#define STB_IMAGE_IMPLEMENTATION                    // We are, in fact, using stb image.

#include "stb_image.h"

// Support .PNG and .TGA for input.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA

qboolean IS_ENHANCED;

qpic_t *enhanced_m_complete_de;

/*
====================
QEX_GameIsRerelease

Checks if <gamedir>/mapdb.json exists, which
holds level information for Enhanced.
====================
*/
qboolean QEX_GameIsRerelease(void)
{
    if (COM_FileExists("mapdb.json", NULL))
        return true;
    return false;
};

qpic_t *QEX_ConvertEnhancedAsset(const char* filename)
{
    // Load an already converted lump.
    if (COM_FileExists(va("%s.lmp", filename), NULL)) {
        return Draw_CachePic(va("%s.lmp", filename));
    }

    // Does the image exist?
    if (!COM_FileExists(va("%s.png", filename), NULL) && !COM_FileExists(va("%s.tga", filename), NULL))
        Sys_Error("QEX_ConvertEnhancedAsset: Couldn't load %s for conversion, did you read the installation instructions?\n", filename);

    Con_Printf("QEX_ConvertEnhancedAsset: Beginning conversion for %s..", filename);
    
    // Figure out the length.. for some reason sizeof(COM_LoadFile) can't do this?
    int handle;
    int len = COM_OpenFile (va("%s.png", filename), &handle, NULL);
    if (handle == -1) len = COM_OpenFile (va("%s.tga", filename), &handle, NULL);
    COM_CloseFile(handle);

    // Load the raw png data, then the rgb data
    byte* raw_file = COM_LoadFile(va("%s.png", filename), 0, NULL);
    if (raw_file == NULL) raw_file = COM_LoadFile(va("%s.tga", filename), 0, NULL);
    int bpp;
    int width, height;
    byte *file = stbi_load_from_memory(raw_file, len, &width, &height, &bpp, 4);

    if (file == NULL)
        Sys_Error("%s\n", stbi_failure_reason());

    // image_to_qpalette.c
    convert_image_to_lmp(file, va("%s.lmp", filename), width, height);

    // Free the bloated rgb image and its rgb data
    Z_Free(raw_file);
    free(file);

    Con_Printf("done.\n");

    // Now load it.
    return Draw_CachePic(va("%s.lmp", filename));
}

void QEX_LoadGraphics(void)
{
    enhanced_m_complete_de = QEX_ConvertEnhancedAsset("gfx/de/complete");
}

/*
====================
QEX_Init

Performs initial check for Quake
Re-Release content, and begins
initializing extended features if
found.
====================
*/
void QEX_Init(void)
{
    if ((IS_ENHANCED = QEX_GameIsRerelease())) {
        Con_Printf("-- Quake Enhanced Detected --\n");
        QEX_LoadGraphics();
    }
};
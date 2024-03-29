/*
 * refit/icns.c
 * Loader for .icns icon files
 *
 * Copyright (c) 2006-2007 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "global.h"
#include "lib.h"
#include "icns.h"
#include "config.h"
#include "../refind/screen.h"

//
// well-known icons
//

typedef struct {
    EG_IMAGE    *Image;
    CHAR16      *FileName;
    UINTN       PixelSize;
} BUILTIN_ICON;

BUILTIN_ICON BuiltinIconTable[BUILTIN_ICON_COUNT] = {
   { NULL, L"func_about", 48 },
   { NULL, L"func_reset", 48 },
   { NULL, L"func_shutdown", 48 },
   { NULL, L"func_exit", 48 },
   { NULL, L"tool_shell", 48 },
   { NULL, L"tool_part", 48 },
   { NULL, L"tool_rescue", 48 },
   { NULL, L"tool_apple_rescue", 48 },
   { NULL, L"tool_mok_tool", 48 },
   { NULL, L"vol_internal", 32 },
   { NULL, L"vol_external", 32 },
   { NULL, L"vol_optical", 32 },
};

EG_IMAGE * BuiltinIcon(IN UINTN Id)
{
    if (Id >= BUILTIN_ICON_COUNT)
        return NULL;

    if (BuiltinIconTable[Id].Image == NULL) {
       BuiltinIconTable[Id].Image = egFindIcon(BuiltinIconTable[Id].FileName, BuiltinIconTable[Id].PixelSize);
       if (BuiltinIconTable[Id].Image == NULL)
          BuiltinIconTable[Id].Image = DummyImage(BuiltinIconTable[Id].PixelSize);
    } // if

    return BuiltinIconTable[Id].Image;
}

//
// Load an icon for an operating system
//

// Load an OS icon from among the comma-delimited list provided in OSIconName.
// Searches for icons with extensions in the ICON_EXTENSIONS list (via
// egFindIcon()).
// Returns image data. On failure, returns an ugly "dummy" icon.
EG_IMAGE * LoadOSIcon(IN CHAR16 *OSIconName OPTIONAL, IN CHAR16 *FallbackIconName, BOOLEAN BootLogo)
{
    EG_IMAGE        *Image = NULL;
    CHAR16          *CutoutName, BaseName[256];
    UINTN           Index = 0;

    if (GlobalConfig.TextOnly)      // skip loading if it's not used anyway
        return NULL;

    // First, try to find an icon from the OSIconName list....
    while (((CutoutName = FindCommaDelimited(OSIconName, Index++)) != NULL) && (Image == NULL)) {
       SPrint(BaseName, 255, L"%s_%s", BootLogo ? L"boot" : L"os", CutoutName);
       Image = egFindIcon(BaseName, 128);
    }

    // If that fails, try again using the FallbackIconName....
    if (Image == NULL) {
       SPrint(BaseName, 255, L"%s_%s", BootLogo ? L"boot" : L"os", FallbackIconName);
       Image = egFindIcon(BaseName, 128);
    }

    // If that fails and if BootLogo was set, try again using the "os_" start of the name....
    if (BootLogo && (Image == NULL)) {
       SPrint(BaseName, 255, L"os_%s", FallbackIconName);
       Image = egFindIcon(BaseName, 128);
    }

    // If all of these fail, return the dummy image....
    if (Image == NULL)
       Image = DummyImage(128);

    return Image;
} /* EG_IMAGE * LoadOSIcon() */


static EG_PIXEL BlackPixel  = { 0x00, 0x00, 0x00, 0 };
//static EG_PIXEL YellowPixel = { 0x00, 0xff, 0xff, 0 };

EG_IMAGE * DummyImage(IN UINTN PixelSize)
{
    EG_IMAGE        *Image;
    UINTN           x, y, LineOffset;
    CHAR8           *Ptr, *YPtr;

    Image = egCreateFilledImage(PixelSize, PixelSize, TRUE, &BlackPixel);

    LineOffset = PixelSize * 4;

    YPtr = (CHAR8 *)Image->PixelData + ((PixelSize - 32) >> 1) * (LineOffset + 4);
    for (y = 0; y < 32; y++) {
        Ptr = YPtr;
        for (x = 0; x < 32; x++) {
            if (((x + y) % 12) < 6) {
                *Ptr++ = 0;
                *Ptr++ = 0;
                *Ptr++ = 0;
            } else {
                *Ptr++ = 0;
                *Ptr++ = 255;
                *Ptr++ = 255;
            }
            *Ptr++ = 144;
        }
        YPtr += LineOffset;
    }

    return Image;
}

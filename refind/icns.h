/*
 * refit/icns.h
 * Icon management header file
 *
 * Copyright (c) 2006-2009 Christoph Pfisterer
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
/*
 * Modifications copyright (c) 2012 Roderick W. Smith
 * 
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), a copy of which must be distributed
 * with this source code or binaries made from it.
 * 
 */

#ifndef __ICNS_H_
#define __ICNS_H_

//
// icns loader module
//

EG_IMAGE * LoadOSIcon(IN CHAR16 *OSIconName OPTIONAL, IN CHAR16 *FallbackIconName, BOOLEAN BootLogo);

EG_IMAGE * DummyImage(IN UINTN PixelSize);

EG_IMAGE * BuiltinIcon(IN UINTN Id);

#define BUILTIN_ICON_FUNC_ABOUT     (0)
#define BUILTIN_ICON_FUNC_RESET     (1)
#define BUILTIN_ICON_FUNC_SHUTDOWN  (2)
#define BUILTIN_ICON_FUNC_EXIT      (3)
#define BUILTIN_ICON_TOOL_SHELL     (4)
#define BUILTIN_ICON_TOOL_PART      (5)
#define BUILTIN_ICON_TOOL_RESCUE    (6)
#define BUILTIN_ICON_TOOL_APPLE_RESCUE (7)
#define BUILTIN_ICON_TOOL_MOK_TOOL  (8)
#define BUILTIN_ICON_VOL_INTERNAL   (9)
#define BUILTIN_ICON_VOL_EXTERNAL   (10)
#define BUILTIN_ICON_VOL_OPTICAL    (11)
#define BUILTIN_ICON_COUNT          (12)

#endif

/* EOF */

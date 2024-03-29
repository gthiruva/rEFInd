/*
 * refind/main.c
 * Main code for the boot menu
 *
 * Copyright (c) 2006-2010 Christoph Pfisterer
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

#include "global.h"
#include "config.h"
#include "screen.h"
#include "lib.h"
#include "icns.h"
#include "menu.h"
#include "mok.h"
#include "security_policy.h"
#include "../include/Handle.h"
#include "../include/refit_call_wrapper.h"
#include "driver_support.h"
#include "../include/syslinux_mbr.h"

#ifdef __MAKEWITH_GNUEFI
#define EFI_SECURITY_VIOLATION    EFIERR (26)
#else
#include "../EfiLib/BdsHelper.h"
#endif // __MAKEWITH_GNUEFI

//
// variables

#define MACOSX_LOADER_PATH      L"System\\Library\\CoreServices\\boot.efi"
#if defined (EFIX64)
#define SHELL_NAMES             L"\\EFI\\tools\\shell.efi,\\EFI\\tools\\shellx64.efi,\\shellx64.efi"
#define DRIVER_DIRS             L"drivers,drivers_x64"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\bootx64.efi"
#define FALLBACK_BASENAME       L"bootx64.efi"
#elif defined (EFI32)
#define SHELL_NAMES             L"\\EFI\\tools\\shell.efi,\\EFI\\tools\\shellia32.efi,\\shellia32.efi"
#define DRIVER_DIRS             L"drivers,drivers_ia32"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\bootia32.efi"
#define FALLBACK_BASENAME       L"bootia32.efi"
#else
#define SHELL_NAMES             L"\\EFI\\tools\\shell.efi"
#define DRIVER_DIRS             L"drivers"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\boot.efi" /* Not really correct */
#define FALLBACK_BASENAME       L"boot.efi"            /* Not really correct */
#endif

// Filename patterns that identify EFI boot loaders. Note that a single case (either L"*.efi" or
// L"*.EFI") is fine for most systems; but Gigabyte's buggy Hybrid EFI does a case-sensitive
// comparison when it should do a case-insensitive comparison, so I'm doubling this up. It does
// no harm on other computers, AFAIK. In theory, every case variation should be done for
// completeness, but that's ridiculous....
#define LOADER_MATCH_PATTERNS   L"*.efi,*.EFI"

// Patterns that identify Linux kernels. Added to the loader match pattern when the
// scan_all_linux_kernels option is set in the configuration file. Causes kernels WITHOUT
// a ".efi" extension to be found when scanning for boot loaders.
#define LINUX_MATCH_PATTERNS    L"vmlinuz*,bzImage*"

// Default hint text for program-launch submenus
#define SUBSCREEN_HINT1            L"Use arrow keys to move cursor; Enter to boot;"
#define SUBSCREEN_HINT2            L"Insert or F2 to edit options; Esc to return to main menu"
#define SUBSCREEN_HINT2_NO_EDITOR  L"Esc to return to main menu"

static REFIT_MENU_ENTRY MenuEntryAbout    = { L"About rEFInd", TAG_ABOUT, 1, 0, 'A', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryReset    = { L"Reboot Computer", TAG_REBOOT, 1, 0, 'R', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryShutdown = { L"Shut Down Computer", TAG_SHUTDOWN, 1, 0, 'U', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryReturn   = { L"Return to Main Menu", TAG_RETURN, 0, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryExit     = { L"Exit rEFInd", TAG_EXIT, 1, 0, 0, NULL, NULL, NULL };

static REFIT_MENU_SCREEN MainMenu       = { L"Main Menu", NULL, 0, NULL, 0, NULL, 0, L"Automatic boot",
                                            L"Use arrow keys to move cursor; Enter to boot;",
                                            L"Insert or F2 for more options; Esc to refresh" };
static REFIT_MENU_SCREEN AboutMenu      = { L"About", NULL, 0, NULL, 0, NULL, 0, NULL, L"Press Enter to return to main menu", L"" };

REFIT_CONFIG GlobalConfig = { FALSE, FALSE, 0, 0, 0, DONT_CHANGE_TEXT_MODE, 20, 0, 0, GRAPHICS_FOR_OSX, LEGACY_TYPE_MAC, 0,
                              NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                              {TAG_SHELL, TAG_APPLE_RECOVERY, TAG_MOK_TOOL, TAG_ABOUT, TAG_SHUTDOWN, TAG_REBOOT, 0, 0, 0, 0, 0 }};

// Structure used to hold boot loader filenames and time stamps in
// a linked list; used to sort entries within a directory.
struct LOADER_LIST {
   CHAR16              *FileName;
   EFI_TIME            TimeStamp;
   struct LOADER_LIST  *NextEntry;
};

//
// misc functions
//

static VOID AboutrEFInd(VOID)
{
    CHAR16 *TempStr; // Note: Don't deallocate; moved to menu structure

    if (AboutMenu.EntryCount == 0) {
        AboutMenu.TitleImage = BuiltinIcon(BUILTIN_ICON_FUNC_ABOUT);
        AddMenuInfoLine(&AboutMenu, L"rEFInd Version 0.6.7.1");
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"Copyright (c) 2006-2010 Christoph Pfisterer");
        AddMenuInfoLine(&AboutMenu, L"Copyright (c) 2012 Roderick W. Smith");
        AddMenuInfoLine(&AboutMenu, L"Portions Copyright (c) Intel Corporation and others");
        AddMenuInfoLine(&AboutMenu, L"Distributed under the terms of the GNU GPLv3 license");
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"Running on:");
        TempStr = AllocateZeroPool(256 * sizeof(CHAR16));
        SPrint(TempStr, 255, L" EFI Revision %d.%02d", ST->Hdr.Revision >> 16, ST->Hdr.Revision & ((1 << 16) - 1));
        AddMenuInfoLine(&AboutMenu, TempStr);
#if defined(EFI32)
        AddMenuInfoLine(&AboutMenu, L" Platform: x86 (32 bit)");
#elif defined(EFIX64)
        TempStr = AllocateZeroPool(256 * sizeof(CHAR16));
        SPrint(TempStr, 255, L" Platform: x86_64 (64 bit); Secure Boot %s", secure_mode() ? L"active" : L"inactive");
        AddMenuInfoLine(&AboutMenu, TempStr);
#else
        AddMenuInfoLine(&AboutMenu, L" Platform: unknown");
#endif
        TempStr = AllocateZeroPool(256 * sizeof(CHAR16));
        SPrint(TempStr, 255, L" Firmware: %s %d.%02d",
               ST->FirmwareVendor, ST->FirmwareRevision >> 16, ST->FirmwareRevision & ((1 << 16) - 1));
        AddMenuInfoLine(&AboutMenu, TempStr);
        TempStr = AllocateZeroPool(256 * sizeof(CHAR16));
        SPrint(TempStr, 255, L" Screen Output: %s", egScreenDescription());
        AddMenuInfoLine(&AboutMenu, TempStr);
        AddMenuInfoLine(&AboutMenu, L"");
#if defined(__MAKEWITH_GNUEFI)
        AddMenuInfoLine(&AboutMenu, L"Built with GNU-EFI");
#else
        AddMenuInfoLine(&AboutMenu, L"Built with TianoCore EDK2");
#endif
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"For more information, see the rEFInd Web site:");
        AddMenuInfoLine(&AboutMenu, L"http://www.rodsbooks.com/refind/");
        AddMenuEntry(&AboutMenu, &MenuEntryReturn);
    }

    RunMenu(&AboutMenu, NULL);
} /* VOID AboutrEFInd() */

static VOID WarnSecureBootError(CHAR16 *Name, BOOLEAN Verbose) {
   if (Name == NULL)
      Name = L"the loader";

   refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
   Print(L"Secure Boot validation failure loading %s!\n", Name);
   refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
   if (Verbose && secure_mode()) {
      Print(L"\nThis computer is configured with Secure Boot active, but\n%s has failed validation.\n", Name);
      Print(L"\nYou can:\n * Launch another boot loader\n");
      Print(L" * Disable Secure Boot in your firmware\n");
      Print(L" * Sign %s with a machine owner key (MOK)\n", Name);
      Print(L" * Use a MOK utility (often present on the second row) to add a MOK with which\n");
      Print(L"   %s has already been signed.\n", Name);
      Print(L" * Use a MOK utility to register %s (\"enroll its hash\") without\n", Name);
      Print(L"   signing it.\n");
      Print(L"\nSee http://www.rodsbooks.com/refind/secureboot.html for more information\n");
      PauseForKey();
   } // if
} // VOID WarnSecureBootError()

// Launch an EFI binary.
static EFI_STATUS StartEFIImageList(IN EFI_DEVICE_PATH **DevicePaths,
                                    IN CHAR16 *LoadOptions, IN CHAR16 *LoadOptionsPrefix,
                                    IN CHAR16 *ImageTitle, IN CHAR8 OSType,
                                    OUT UINTN *ErrorInStep,
                                    IN BOOLEAN Verbose)
{
    EFI_STATUS              Status, ReturnStatus;
    EFI_HANDLE              ChildImageHandle;
    EFI_LOADED_IMAGE        *ChildLoadedImage = NULL;
    UINTN                   DevicePathIndex;
    CHAR16                  ErrorInfo[256];
    CHAR16                  *FullLoadOptions = NULL;

    if (ErrorInStep != NULL)
        *ErrorInStep = 0;

    // set load options
    if (LoadOptions != NULL) {
        if (LoadOptionsPrefix != NULL) {
            MergeStrings(&FullLoadOptions, LoadOptions, L' ');
            if (OSType == 'M') {
               MergeStrings(&FullLoadOptions, L" ", 0);
               // NOTE: That last space is also added by the EFI shell and seems to be significant
               //  when passing options to Apple's boot.efi...
            } // if
        } else {
            MergeStrings(&FullLoadOptions, LoadOptions, 0);
        } // if/else
    } else { // LoadOptions == NULL
       // NOTE: We provide a non-null string when no options are specified for safety;
       // some systems (at least DUET) can hang when launching some programs (such as
       // an EFI shell) without this.
       FullLoadOptions = StrDuplicate(L" ");
    }
    if (Verbose)
       Print(L"Starting %s\nUsing load options '%s'\n", ImageTitle, FullLoadOptions);

    // load the image into memory (and execute it, in the case of a shim/MOK image).
    ReturnStatus = Status = EFI_NOT_FOUND;  // in case the list is empty
    for (DevicePathIndex = 0; DevicePaths[DevicePathIndex] != NULL; DevicePathIndex++) {
       // NOTE: Below commented-out line could be more efficient if file were read ahead of
       // time and passed as a pre-loaded image to LoadImage(), but it doesn't work on my
       // 32-bit Mac Mini or my 64-bit Intel box when launching a Linux kernel; the
       // kernel returns a "Failed to handle fs_proto" error message.
       // TODO: Track down the cause of this error and fix it, if possible.
       // ReturnStatus = Status = refit_call6_wrapper(BS->LoadImage, FALSE, SelfImageHandle, DevicePaths[DevicePathIndex],
       //                                            ImageData, ImageSize, &ChildImageHandle);
       ReturnStatus = Status = refit_call6_wrapper(BS->LoadImage, FALSE, SelfImageHandle, DevicePaths[DevicePathIndex],
                                                   NULL, 0, &ChildImageHandle);
       if (ReturnStatus != EFI_NOT_FOUND) {
          break;
       }
    }
    if ((Status == EFI_ACCESS_DENIED) || (Status == EFI_SECURITY_VIOLATION)) {
       WarnSecureBootError(ImageTitle, Verbose);
       goto bailout;
    }
    SPrint(ErrorInfo, 255, L"while loading %s", ImageTitle);
    if (CheckError(Status, ErrorInfo)) {
        if (ErrorInStep != NULL)
            *ErrorInStep = 1;
        goto bailout;
    }

    ReturnStatus = Status = refit_call3_wrapper(BS->HandleProtocol, ChildImageHandle, &LoadedImageProtocol,
                                                (VOID **) &ChildLoadedImage);
    if (CheckError(Status, L"while getting a LoadedImageProtocol handle")) {
       if (ErrorInStep != NULL)
          *ErrorInStep = 2;
       goto bailout_unload;
    }
    ChildLoadedImage->LoadOptions = (VOID *)FullLoadOptions;
    ChildLoadedImage->LoadOptionsSize = ((UINT32)StrLen(FullLoadOptions) + 1) * sizeof(CHAR16);
    // turn control over to the image
    // TODO: (optionally) re-enable the EFI watchdog timer!

    // close open file handles
    UninitRefitLib();
    ReturnStatus = Status = refit_call3_wrapper(BS->StartImage, ChildImageHandle, NULL, NULL);

    // control returns here when the child image calls Exit()
    SPrint(ErrorInfo, 255, L"returned from %s", ImageTitle);
    if (CheckError(Status, ErrorInfo)) {
        if (ErrorInStep != NULL)
            *ErrorInStep = 3;
    }

    // re-open file handles
    ReinitRefitLib();

bailout_unload:
    // unload the image, we don't care if it works or not...
    Status = refit_call1_wrapper(BS->UnloadImage, ChildImageHandle);

bailout:
    MyFreePool(FullLoadOptions);
    return ReturnStatus;
} /* static EFI_STATUS StartEFIImageList() */

static EFI_STATUS StartEFIImage(IN EFI_DEVICE_PATH *DevicePath,
                                IN CHAR16 *LoadOptions, IN CHAR16 *LoadOptionsPrefix,
                                IN CHAR16 *ImageTitle, IN CHAR8 OSType,
                                OUT UINTN *ErrorInStep,
                                IN BOOLEAN Verbose)
{
    EFI_DEVICE_PATH *DevicePaths[2];

    DevicePaths[0] = DevicePath;
    DevicePaths[1] = NULL;
    return StartEFIImageList(DevicePaths, LoadOptions, LoadOptionsPrefix, ImageTitle, OSType, ErrorInStep, Verbose);
} /* static EFI_STATUS StartEFIImage() */

//
// EFI OS loader functions
//

static VOID StartLoader(IN LOADER_ENTRY *Entry)
{
    UINTN ErrorInStep = 0;

    BeginExternalScreen(Entry->UseGraphicsMode, L"Booting OS");
    StartEFIImage(Entry->DevicePath, Entry->LoadOptions, Basename(Entry->LoaderPath),
                  Basename(Entry->LoaderPath), Entry->OSType, &ErrorInStep, !Entry->UseGraphicsMode);
    FinishExternalScreen();
}

// Locate an initrd or initramfs file that matches the kernel specified by LoaderPath.
// The matching file has a name that begins with "init" and includes the same version
// number string as is found in LoaderPath -- but not a longer version number string.
// For instance, if LoaderPath is \EFI\kernels\bzImage-3.3.0.efi, and if \EFI\kernels
// has a file called initramfs-3.3.0.img, this function will return the string
// '\EFI\kernels\initramfs-3.3.0.img'. If the directory ALSO contains the file
// initramfs-3.3.0-rc7.img or initramfs-13.3.0.img, those files will NOT match;
// however, initmine-3.3.0.img might match. (FindInitrd() returns the first match it
// finds). Thus, care should be taken to avoid placing duplicate matching files in
// the kernel's directory.
// If no matching init file can be found, returns NULL.
static CHAR16 * FindInitrd(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume) {
   CHAR16              *InitrdName = NULL, *FileName, *KernelVersion, *InitrdVersion, *Path;
   REFIT_DIR_ITER      DirIter;
   EFI_FILE_INFO       *DirEntry;

   FileName = Basename(LoaderPath);
   KernelVersion = FindNumbers(FileName);
   Path = FindPath(LoaderPath);

   // Add trailing backslash for root directory; necessary on some systems, but must
   // NOT be added to all directories, since on other systems, a trailing backslash on
   // anything but the root directory causes them to flake out!
   if (StrLen(Path) == 0) {
      MergeStrings(&Path, L"\\", 0);
   } // if
   DirIterOpen(Volume->RootDir, Path, &DirIter);
   // Now add a trailing backslash if it was NOT added earlier, for consistency in
   // building the InitrdName later....
   if ((StrLen(Path) > 0) && (Path[StrLen(Path) - 1] != L'\\'))
      MergeStrings(&Path, L"\\", 0);
   while ((DirIterNext(&DirIter, 2, L"init*", &DirEntry)) && (InitrdName == NULL)) {
      InitrdVersion = FindNumbers(DirEntry->FileName);
      if (KernelVersion != NULL) {
         if (StriCmp(InitrdVersion, KernelVersion) == 0) {
            MergeStrings(&InitrdName, Path, 0);
            MergeStrings(&InitrdName, DirEntry->FileName, 0);
         } // if
      } else {
         if (InitrdVersion == NULL) {
            MergeStrings(&InitrdName, Path, 0);
            MergeStrings(&InitrdName, DirEntry->FileName, 0);
         } // if
      } // if/else
      MyFreePool(InitrdVersion);
   } // while
   DirIterClose(&DirIter);

   // Note: Don't FreePool(FileName), since Basename returns a pointer WITHIN the string it's passed.
   MyFreePool(KernelVersion);
   MyFreePool(Path);
   return (InitrdName);
} // static CHAR16 * FindInitrd()

LOADER_ENTRY * AddPreparedLoaderEntry(LOADER_ENTRY *Entry) {
   AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);

   return(Entry);
} // LOADER_ENTRY * AddPreparedLoaderEntry()

// Creates a copy of a menu screen.
// Returns a pointer to the copy of the menu screen.
static REFIT_MENU_SCREEN* CopyMenuScreen(REFIT_MENU_SCREEN *Entry) {
   REFIT_MENU_SCREEN *NewEntry;
   UINTN i;

   NewEntry = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
   if ((Entry != NULL) && (NewEntry != NULL)) {
      CopyMem(NewEntry, Entry, sizeof(REFIT_MENU_SCREEN));
      NewEntry->Title = (Entry->Title) ? StrDuplicate(Entry->Title) : NULL;
      NewEntry->TimeoutText = (Entry->TimeoutText) ? StrDuplicate(Entry->TimeoutText) : NULL;
      if (Entry->TitleImage != NULL) {
         NewEntry->TitleImage = AllocatePool(sizeof(EG_IMAGE));
         if (NewEntry->TitleImage != NULL)
            CopyMem(NewEntry->TitleImage, Entry->TitleImage, sizeof(EG_IMAGE));
      } // if
      NewEntry->InfoLines = (CHAR16**) AllocateZeroPool(Entry->InfoLineCount * (sizeof(CHAR16*)));
      for (i = 0; i < Entry->InfoLineCount && NewEntry->InfoLines; i++) {
         NewEntry->InfoLines[i] = (Entry->InfoLines[i]) ? StrDuplicate(Entry->InfoLines[i]) : NULL;
      } // for
      NewEntry->Entries = (REFIT_MENU_ENTRY**) AllocateZeroPool(Entry->EntryCount * (sizeof (REFIT_MENU_ENTRY*)));
      for (i = 0; i < Entry->EntryCount && NewEntry->Entries; i++) {
         AddMenuEntry(NewEntry, Entry->Entries[i]);
      } // for
      NewEntry->Hint1 = (Entry->Hint1) ? StrDuplicate(Entry->Hint1) : NULL;
      NewEntry->Hint2 = (Entry->Hint2) ? StrDuplicate(Entry->Hint2) : NULL;
   } // if
   return (NewEntry);
} // static REFIT_MENU_SCREEN* CopyMenuScreen()

// Creates a copy of a menu entry. Intended to enable moving a stack-based
// menu entry (such as the ones for the "reboot" and "exit" functions) to
// to the heap. This enables easier deletion of the whole set of menu
// entries when re-scanning.
// Returns a pointer to the copy of the menu entry.
static REFIT_MENU_ENTRY* CopyMenuEntry(REFIT_MENU_ENTRY *Entry) {
   REFIT_MENU_ENTRY *NewEntry;

   NewEntry = AllocateZeroPool(sizeof(REFIT_MENU_ENTRY));
   if ((Entry != NULL) && (NewEntry != NULL)) {
      CopyMem(NewEntry, Entry, sizeof(REFIT_MENU_ENTRY));
      NewEntry->Title = (Entry->Title) ? StrDuplicate(Entry->Title) : NULL;
      if (Entry->BadgeImage != NULL) {
         NewEntry->BadgeImage = AllocatePool(sizeof(EG_IMAGE));
         if (NewEntry->BadgeImage != NULL)
            CopyMem(NewEntry->BadgeImage, Entry->BadgeImage, sizeof(EG_IMAGE));
      }
      if (Entry->Image != NULL) {
         NewEntry->Image = AllocatePool(sizeof(EG_IMAGE));
         if (NewEntry->Image != NULL)
            CopyMem(NewEntry->Image, Entry->Image, sizeof(EG_IMAGE));
      }
      if (Entry->SubScreen != NULL) {
         NewEntry->SubScreen = CopyMenuScreen(Entry->SubScreen);
      }
   } // if
   return (NewEntry);
} // REFIT_MENU_ENTRY* CopyMenuEntry()

// Creates a new LOADER_ENTRY data structure and populates it with
// default values from the specified Entry, or NULL values if Entry
// is unspecified (NULL).
// Returns a pointer to the new data structure, or NULL if it
// couldn't be allocated
LOADER_ENTRY *InitializeLoaderEntry(IN LOADER_ENTRY *Entry) {
   LOADER_ENTRY *NewEntry = NULL;

   NewEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
   if (NewEntry != NULL) {
      NewEntry->me.Title        = NULL;
      NewEntry->me.Tag          = TAG_LOADER;
      NewEntry->Enabled         = TRUE;
      NewEntry->UseGraphicsMode = FALSE;
      NewEntry->OSType          = 0;
      if (Entry != NULL) {
         NewEntry->LoaderPath      = (Entry->LoaderPath) ? StrDuplicate(Entry->LoaderPath) : NULL;
         NewEntry->VolName         = (Entry->VolName) ? StrDuplicate(Entry->VolName) : NULL;
         NewEntry->DevicePath      = Entry->DevicePath;
         NewEntry->UseGraphicsMode = Entry->UseGraphicsMode;
         NewEntry->LoadOptions     = (Entry->LoadOptions) ? StrDuplicate(Entry->LoadOptions) : NULL;
         NewEntry->InitrdPath      = (Entry->InitrdPath) ? StrDuplicate(Entry->InitrdPath) : NULL;
      }
   } // if
   return (NewEntry);
} // LOADER_ENTRY *InitializeLoaderEntry()

// Adds InitrdPath to Options, but only if Options doesn't already include an
// initrd= line. Done to enable overriding the default initrd selection in a
// refind_linux.conf file's options list.
// Returns a pointer to a new string. The calling function is responsible for
// freeing its memory.
static CHAR16 *AddInitrdToOptions(CHAR16 *Options, CHAR16 *InitrdPath) {
   CHAR16 *NewOptions = NULL;

   if (Options != NULL)
      NewOptions = StrDuplicate(Options);
   if ((InitrdPath != NULL) && !StriSubCmp(L"initrd=", Options)) {
      MergeStrings(&NewOptions, L"initrd=", L' ');
      MergeStrings(&NewOptions, InitrdPath, 0);
   }
   return NewOptions;
} // CHAR16 *AddInitrdToOptions()

// Prepare a REFIT_MENU_SCREEN data structure for a subscreen entry. This sets up
// the default entry that launches the boot loader using the same options as the
// main Entry does. Subsequent options can be added by the calling function.
// If a subscreen already exists in the Entry that's passed to this function,
// it's left unchanged and a pointer to it is returned.
// Returns a pointer to the new subscreen data structure, or NULL if there
// were problems allocating memory.
REFIT_MENU_SCREEN *InitializeSubScreen(IN LOADER_ENTRY *Entry) {
   CHAR16              *FileName, *MainOptions = NULL;
   REFIT_MENU_SCREEN   *SubScreen = NULL;
   LOADER_ENTRY        *SubEntry;

   FileName = Basename(Entry->LoaderPath);
   if (Entry->me.SubScreen == NULL) { // No subscreen yet; initialize default entry....
      SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
      if (SubScreen != NULL) {
         SubScreen->Title = AllocateZeroPool(sizeof(CHAR16) * 256);
         SPrint(SubScreen->Title, 255, L"Boot Options for %s on %s",
                (Entry->Title != NULL) ? Entry->Title : FileName, Entry->VolName);
         SubScreen->TitleImage = Entry->me.Image;
         // default entry
         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title = StrDuplicate(L"Boot using default options");
            MainOptions = SubEntry->LoadOptions;
            SubEntry->LoadOptions = AddInitrdToOptions(MainOptions, SubEntry->InitrdPath);
            MyFreePool(MainOptions);
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // if (SubEntry != NULL)
         SubScreen->Hint1 = StrDuplicate(SUBSCREEN_HINT1);
         if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR) {
            SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2_NO_EDITOR);
         } else {
            SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2);
         } // if/else
      } // if (SubScreen != NULL)
   } else { // existing subscreen; less initialization, and just add new entry later....
      SubScreen = Entry->me.SubScreen;
   } // if/else
   return SubScreen;
} // REFIT_MENU_SCREEN *InitializeSubScreen()

VOID GenerateSubScreen(LOADER_ENTRY *Entry, IN REFIT_VOLUME *Volume) {
   REFIT_MENU_SCREEN  *SubScreen;
   LOADER_ENTRY       *SubEntry;
   CHAR16             *InitrdName;
   CHAR16             DiagsFileName[256];
   REFIT_FILE         *File;
   UINTN              TokenCount;
   CHAR16             **TokenList;

   // create the submenu
   if (StrLen(Entry->Title) == 0) {
      MyFreePool(Entry->Title);
      Entry->Title = NULL;
   }
   SubScreen = InitializeSubScreen(Entry);

   // loader-specific submenu entries
   if (Entry->OSType == 'M') {          // entries for Mac OS X
#if defined(EFIX64)
      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Boot Mac OS X with a 64-bit kernel";
         SubEntry->LoadOptions     = L"arch=x86_64";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      } // if

      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Boot Mac OS X with a 32-bit kernel";
         SubEntry->LoadOptions     = L"arch=i386";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      } // if
#endif

      if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_SINGLEUSER)) {
         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Mac OS X in verbose mode";
            SubEntry->UseGraphicsMode = FALSE;
            SubEntry->LoadOptions     = L"-v";
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // if

#if defined(EFIX64)
         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Mac OS X in verbose mode (64-bit)";
            SubEntry->UseGraphicsMode = FALSE;
            SubEntry->LoadOptions     = L"-v arch=x86_64";
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         }

         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Mac OS X in verbose mode (32-bit)";
            SubEntry->UseGraphicsMode = FALSE;
            SubEntry->LoadOptions     = L"-v arch=i386";
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         }
#endif

         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Mac OS X in single user mode";
            SubEntry->UseGraphicsMode = FALSE;
            SubEntry->LoadOptions     = L"-v -s";
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // if
      } // single-user mode allowed

      if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_SAFEMODE)) {
         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Mac OS X in safe mode";
            SubEntry->UseGraphicsMode = FALSE;
            SubEntry->LoadOptions     = L"-v -x";
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // if
      } // safe mode allowed

      // check for Apple hardware diagnostics
      StrCpy(DiagsFileName, L"System\\Library\\CoreServices\\.diagnostics\\diags.efi");
      if (FileExists(Volume->RootDir, DiagsFileName) && !(GlobalConfig.HideUIFlags & HIDEUI_FLAG_HWTEST)) {
         SubEntry = InitializeLoaderEntry(Entry);
         if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Run Apple Hardware Test";
            MyFreePool(SubEntry->LoaderPath);
            SubEntry->LoaderPath      = StrDuplicate(DiagsFileName);
            SubEntry->DevicePath      = FileDevicePath(Volume->DeviceHandle, SubEntry->LoaderPath);
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // if
      } // if diagnostics entry found

   } else if (Entry->OSType == 'L') {   // entries for Linux kernels with EFI stub loaders
      File = ReadLinuxOptionsFile(Entry->LoaderPath, Volume);
      if (File != NULL) {
         InitrdName =  FindInitrd(Entry->LoaderPath, Volume);
         TokenCount = ReadTokenLine(File, &TokenList);
         // first entry requires special processing, since it was initially set
         // up with a default title but correct options by InitializeSubScreen(),
         // earlier....
         if ((SubScreen->Entries != NULL) && (SubScreen->Entries[0] != NULL)) {
            MyFreePool(SubScreen->Entries[0]->Title);
            SubScreen->Entries[0]->Title = TokenList[0] ? StrDuplicate(TokenList[0]) : StrDuplicate(L"Boot Linux");
         } // if
         FreeTokenLine(&TokenList, &TokenCount);
         while ((TokenCount = ReadTokenLine(File, &TokenList)) > 1) {
            SubEntry = InitializeLoaderEntry(Entry);
            SubEntry->me.Title = TokenList[0] ? StrDuplicate(TokenList[0]) : StrDuplicate(L"Boot Linux");
            MyFreePool(SubEntry->LoadOptions);
            SubEntry->LoadOptions = AddInitrdToOptions(TokenList[1], InitrdName);
            FreeTokenLine(&TokenList, &TokenCount);
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_LINUX;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
         } // while
         MyFreePool(InitrdName);
         MyFreePool(File);
      } // if Linux options file exists

   } else if (Entry->OSType == 'E') {   // entries for ELILO
      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Run ELILO in interactive mode";
         SubEntry->LoadOptions     = L"-p";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      }

      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Boot Linux for a 17\" iMac or a 15\" MacBook Pro (*)";
         SubEntry->UseGraphicsMode = TRUE;
         SubEntry->LoadOptions     = L"-d 0 i17";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      }

      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Boot Linux for a 20\" iMac (*)";
         SubEntry->UseGraphicsMode = TRUE;
         SubEntry->LoadOptions     = L"-d 0 i20";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      }

      SubEntry = InitializeLoaderEntry(Entry);
      if (SubEntry != NULL) {
         SubEntry->me.Title        = L"Boot Linux for a Mac Mini (*)";
         SubEntry->UseGraphicsMode = TRUE;
         SubEntry->LoadOptions     = L"-d 0 mini";
         SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
         AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
      }

      AddMenuInfoLine(SubScreen, L"NOTE: This is an example. Entries");
      AddMenuInfoLine(SubScreen, L"marked with (*) may not work.");

   } else if (Entry->OSType == 'X') {   // entries for xom.efi
        // by default, skip the built-in selection and boot from hard disk only
        Entry->LoadOptions = L"-s -h";

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
           SubEntry->me.Title        = L"Boot Windows from Hard Disk";
           SubEntry->LoadOptions     = L"-s -h";
           SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
           AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
           SubEntry->me.Title        = L"Boot Windows from CD-ROM";
           SubEntry->LoadOptions     = L"-s -c";
           SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
           AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
           SubEntry->me.Title        = L"Run XOM in text mode";
           SubEntry->UseGraphicsMode = FALSE;
           SubEntry->LoadOptions     = L"-v";
           SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
           AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }
   } // entries for xom.efi
   AddMenuEntry(SubScreen, &MenuEntryReturn);
   Entry->me.SubScreen = SubScreen;
} // VOID GenerateSubScreen()

// Returns options for a Linux kernel. Reads them from an options file in the
// kernel's directory; and if present, adds an initrd= option for an initial
// RAM disk file with the same version number as the kernel file.
static CHAR16 * GetMainLinuxOptions(IN CHAR16 * LoaderPath, IN REFIT_VOLUME *Volume) {
   CHAR16 *Options = NULL, *InitrdName, *FullOptions = NULL;

   Options = GetFirstOptionsFromFile(LoaderPath, Volume);
   InitrdName = FindInitrd(LoaderPath, Volume);
   FullOptions = AddInitrdToOptions(Options, InitrdName);

   MyFreePool(Options);
   MyFreePool(InitrdName);
   return (FullOptions);
} // static CHAR16 * GetMainLinuxOptions()

// Sets a few defaults for a loader entry -- mainly the icon, but also the OS type
// code and shortcut letter. For Linux EFI stub loaders, also sets kernel options
// that will (with luck) work fairly automatically.
VOID SetLoaderDefaults(LOADER_ENTRY *Entry, CHAR16 *LoaderPath, REFIT_VOLUME *Volume) {
   CHAR16      *FileName, *PathOnly, *NoExtension, *OSIconName = NULL, *Temp, *SubString;
   CHAR16      ShortcutLetter = 0;
   UINTN       i = 0, Length;

   FileName = Basename(LoaderPath);
   PathOnly = FindPath(LoaderPath);
   NoExtension = StripEfiExtension(FileName);

   // locate a custom icon for the loader
   // Anything found here takes precedence over the "hints" in the OSIconName variable
   if (!Entry->me.Image)
      Entry->me.Image = egLoadIconAnyType(Volume->RootDir, PathOnly, NoExtension, 128);
   if (!Entry->me.Image)
      Entry->me.Image = egCopyImage(Volume->VolIconImage);

   // Begin creating icon "hints" by using last part of directory path leading
   // to the loader
   Temp = FindLastDirName(LoaderPath);
   MergeStrings(&OSIconName, Temp, L',');
   MyFreePool(Temp);
   Temp = NULL;
   if (OSIconName != NULL) {
      ShortcutLetter = OSIconName[0];
   }

   // Add every "word" in the volume label, delimited by spaces, dashes (-), or
   // underscores (_), to the list of hints to be used in searching for OS
   // icons.
   if ((Volume->VolName) && (StrLen(Volume->VolName) > 0)) {
      Temp = SubString = StrDuplicate(Volume->VolName);
      if (Temp != NULL) {
         Length = StrLen(Temp);
         for (i = 0; i < Length; i++) {
            if ((Temp[i] == L' ') || (Temp[i] == L'_') || (Temp[i] == L'-')) {
               Temp[i] = 0;
               if (StrLen(SubString) > 0)
                  MergeStrings(&OSIconName, SubString, L',');
               SubString = Temp + i + 1;
            } // if
         } // for
         MergeStrings(&OSIconName, SubString, L',');
         MyFreePool(Temp);
      } // if
   } // if

   // detect specific loaders
   if (StriSubCmp(L"bzImage", LoaderPath) || StriSubCmp(L"vmlinuz", LoaderPath)) {
      MergeStrings(&OSIconName, L"linux", L',');
      Entry->OSType = 'L';
      if (ShortcutLetter == 0)
         ShortcutLetter = 'L';
      Entry->LoadOptions = GetMainLinuxOptions(LoaderPath, Volume);
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_LINUX;
   } else if (StriSubCmp(L"refit", LoaderPath)) {
      MergeStrings(&OSIconName, L"refit", L',');
      Entry->OSType = 'R';
      ShortcutLetter = 'R';
   } else if (StriCmp(LoaderPath, MACOSX_LOADER_PATH) == 0) {
      if (Volume->VolIconImage != NULL) { // custom icon file found
         Entry->me.Image = Volume->VolIconImage;
      }
      MergeStrings(&OSIconName, L"mac", L',');
      Entry->OSType = 'M';
      ShortcutLetter = 'M';
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
   } else if (StriCmp(FileName, L"diags.efi") == 0) {
      MergeStrings(&OSIconName, L"hwtest", L',');
   } else if (StriCmp(FileName, L"e.efi") == 0 || StriCmp(FileName, L"elilo.efi") == 0 || StriSubCmp(L"elilo", FileName)) {
      MergeStrings(&OSIconName, L"elilo,linux", L',');
      Entry->OSType = 'E';
      if (ShortcutLetter == 0)
         ShortcutLetter = 'L';
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
   } else if (StriSubCmp(L"grub", FileName)) {
      Entry->OSType = 'G';
      ShortcutLetter = 'G';
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_GRUB;
   } else if (StriCmp(FileName, L"cdboot.efi") == 0 ||
              StriCmp(FileName, L"bootmgr.efi") == 0 ||
              StriCmp(FileName, L"bootmgfw.efi") == 0) {
      MergeStrings(&OSIconName, L"win", L',');
      Entry->OSType = 'W';
      ShortcutLetter = 'W';
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
   } else if (StriCmp(FileName, L"xom.efi") == 0) {
      MergeStrings(&OSIconName, L"xom,win", L',');
      Entry->UseGraphicsMode = TRUE;
      Entry->OSType = 'X';
      ShortcutLetter = 'W';
      Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
   }

   if ((ShortcutLetter >= 'a') && (ShortcutLetter <= 'z'))
      ShortcutLetter = ShortcutLetter - 'a' + 'A'; // convert lowercase to uppercase
   Entry->me.ShortcutLetter = ShortcutLetter;
   if (Entry->me.Image == NULL)
      Entry->me.Image = LoadOSIcon(OSIconName, L"unknown", FALSE);
   MyFreePool(PathOnly);
} // VOID SetLoaderDefaults()

// Add a specified EFI boot loader to the list, using automatic settings
// for icons, options, etc.
LOADER_ENTRY * AddLoaderEntry(IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, IN REFIT_VOLUME *Volume) {
   LOADER_ENTRY      *Entry;

   CleanUpPathNameSlashes(LoaderPath);
   Entry = InitializeLoaderEntry(NULL);
   if (Entry != NULL) {
      Entry->Title = StrDuplicate((LoaderTitle != NULL) ? LoaderTitle : LoaderPath);
      Entry->me.Title = AllocateZeroPool(sizeof(CHAR16) * 256);
      SPrint(Entry->me.Title, 255, L"Boot %s from %s", (LoaderTitle != NULL) ? LoaderTitle : LoaderPath, Volume->VolName);
      Entry->me.Row = 0;
      Entry->me.BadgeImage = Volume->VolBadgeImage;
      if ((LoaderPath != NULL) && (LoaderPath[0] != L'\\')) {
         Entry->LoaderPath = StrDuplicate(L"\\");
      } else {
         Entry->LoaderPath = NULL;
      }
      MergeStrings(&(Entry->LoaderPath), LoaderPath, 0);
      Entry->VolName = Volume->VolName;
      Entry->DevicePath = FileDevicePath(Volume->DeviceHandle, Entry->LoaderPath);
      SetLoaderDefaults(Entry, LoaderPath, Volume);
      GenerateSubScreen(Entry, Volume);
      AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
   }

   return(Entry);
} // LOADER_ENTRY * AddLoaderEntry()

// Returns -1 if (Time1 < Time2), +1 if (Time1 > Time2), or 0 if
// (Time1 == Time2). Precision is only to the nearest second; since
// this is used for sorting boot loader entries, differences smaller
// than this are likely to be meaningless (and unlikely!).
INTN TimeComp(IN EFI_TIME *Time1, IN EFI_TIME *Time2) {
   INT64 Time1InSeconds, Time2InSeconds;

   // Following values are overestimates; I'm assuming 31 days in every month.
   // This is fine for the purpose of this function, which is limited
   Time1InSeconds = Time1->Second + (Time1->Minute * 60) + (Time1->Hour * 3600) + (Time1->Day * 86400) +
                    (Time1->Month * 2678400) + ((Time1->Year - 1998) * 32140800);
   Time2InSeconds = Time2->Second + (Time2->Minute * 60) + (Time2->Hour * 3600) + (Time2->Day * 86400) +
                    (Time2->Month * 2678400) + ((Time2->Year - 1998) * 32140800);
   if (Time1InSeconds < Time2InSeconds)
      return (-1);
   else if (Time1InSeconds > Time2InSeconds)
      return (1);

   return 0;
} // INTN TimeComp()

// Adds a loader list element, keeping it sorted by date. Returns the new
// first element (the one with the most recent date).
static struct LOADER_LIST * AddLoaderListEntry(struct LOADER_LIST *LoaderList, struct LOADER_LIST *NewEntry) {
   struct LOADER_LIST *LatestEntry, *CurrentEntry, *PrevEntry = NULL;

   LatestEntry = CurrentEntry = LoaderList;
   if (LoaderList == NULL) {
      LatestEntry = NewEntry;
   } else {
      while ((CurrentEntry != NULL) && (TimeComp(&(NewEntry->TimeStamp), &(CurrentEntry->TimeStamp)) < 0)) {
         PrevEntry = CurrentEntry;
         CurrentEntry = CurrentEntry->NextEntry;
      } // while
      NewEntry->NextEntry = CurrentEntry;
      if (PrevEntry == NULL) {
         LatestEntry = NewEntry;
      } else {
         PrevEntry->NextEntry = NewEntry;
      } // if/else
   } // if/else
   return (LatestEntry);
} // static VOID AddLoaderListEntry()

// Delete the LOADER_LIST linked list
static VOID CleanUpLoaderList(struct LOADER_LIST *LoaderList) {
   struct LOADER_LIST *Temp;

   while (LoaderList != NULL) {
      Temp = LoaderList;
      LoaderList = LoaderList->NextEntry;
      MyFreePool(Temp->FileName);
      MyFreePool(Temp);
   } // while
} // static VOID CleanUpLoaderList()

// Returns FALSE if the specified file/volume matches the GlobalConfig.DontScanDirs
// or GlobalConfig.DontScanVolumes specification, or if Path points to a volume
// other than the one specified by Volume, or if the specified path is SelfDir.
// Returns TRUE if none of these conditions is met -- that is, if the path is
// eligible for scanning.
static BOOLEAN ShouldScan(REFIT_VOLUME *Volume, CHAR16 *Path) {
   CHAR16   *VolName = NULL, *DontScanDir;
   UINTN    i = 0, VolNum;
   BOOLEAN  ScanIt = TRUE;

   if (IsIn(Volume->VolName, GlobalConfig.DontScanVolumes))
      return FALSE;

   if ((StriCmp(Path, SelfDirPath) == 0) && (Volume->DeviceHandle == SelfVolume->DeviceHandle))
      return FALSE;

   while ((DontScanDir = FindCommaDelimited(GlobalConfig.DontScanDirs, i++)) && ScanIt) {
      SplitVolumeAndFilename(&DontScanDir, &VolName);
      CleanUpPathNameSlashes(DontScanDir);
      if (VolName != NULL) {
         if ((StriCmp(VolName, Volume->VolName) == 0) && (StriCmp(DontScanDir, Path) == 0))
            ScanIt = FALSE;
         if ((StrLen(VolName) > 2) && (VolName[0] == L'f') && (VolName[1] == L's') && (VolName[2] >= L'0') && (VolName[2] <= L'9')) {
            VolNum = Atoi(VolName + 2);
            if ((VolNum == Volume->VolNumber) && (StriCmp(DontScanDir, Path) == 0))
               ScanIt = FALSE;
         }
      } else {
         if (StriCmp(DontScanDir, Path) == 0)
            ScanIt = FALSE;
      }
      MyFreePool(DontScanDir);
      DontScanDir = NULL;
   }
   return ScanIt;
} // BOOLEAN ShouldScan()

// Returns TRUE if the file is byte-for-byte identical with the fallback file
// on the volume AND if the file is not itself the fallback file; returns
// FALSE if the file is not identical to the fallback file OR if the file
// IS the fallback file. Intended for use in excluding the fallback boot
// loader when it's a duplicate of another boot loader.
static BOOLEAN DuplicatesFallback(IN REFIT_VOLUME *Volume, IN CHAR16 *FileName) {
   CHAR8           *FileContents, *FallbackContents;
   EFI_FILE_HANDLE FileHandle, FallbackHandle;
   EFI_FILE_INFO   *FileInfo, *FallbackInfo;
   UINTN           FileSize = 0, FallbackSize = 0;
   EFI_STATUS      Status;
   BOOLEAN         AreIdentical = FALSE;

   CleanUpPathNameSlashes(FileName);

   if (StriCmp(FileName, FALLBACK_FULLNAME) == 0)
      return FALSE; // identical filenames, so not a duplicate....

   Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
   if (Status == EFI_SUCCESS) {
      FileInfo = LibFileInfo(FileHandle);
      FileSize = FileInfo->FileSize;
   } else {
      return FALSE;
   }

   Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FallbackHandle, FALLBACK_FULLNAME, EFI_FILE_MODE_READ, 0);
   if (Status == EFI_SUCCESS) {
      FallbackInfo = LibFileInfo(FallbackHandle);
      FallbackSize = FallbackInfo->FileSize;
   } else {
      refit_call1_wrapper(FileHandle->Close, FileHandle);
      return FALSE;
   }

   if (FallbackSize != FileSize) { // not same size, so can't be identical
      AreIdentical = FALSE;
   } else { // could be identical; do full check....
      FileContents = AllocatePool(FileSize);
      FallbackContents = AllocatePool(FallbackSize);
      if (FileContents && FallbackContents) {
         Status = refit_call3_wrapper(FileHandle->Read, FileHandle, &FileSize, FileContents);
         if (Status == EFI_SUCCESS)
            Status = refit_call3_wrapper(FallbackHandle->Read, FallbackHandle, &FallbackSize, FallbackContents);
         if (Status == EFI_SUCCESS) {
            AreIdentical = (CompareMem(FileContents, FallbackContents, FileSize) == 0);
         } // if
      } // if
      MyFreePool(FileContents);
      MyFreePool(FallbackContents);
   } // if/else

   refit_call1_wrapper(FileHandle->Close, FileHandle);
   refit_call1_wrapper(FileHandle->Close, FallbackHandle);
   return AreIdentical;

} // BOOLEAN DuplicatesFallback()

// Scan an individual directory for EFI boot loader files and, if found,
// add them to the list. Exception: Ignores FALLBACK_FULLNAME, which is picked
// up in ScanEfiFiles(). Sorts the entries within the loader directory so that
// the most recent one appears first in the list.
// Returns TRUE if a duplicate for FALLBACK_FILENAME was found, FALSE if not.
static BOOLEAN ScanLoaderDir(IN REFIT_VOLUME *Volume, IN CHAR16 *Path, IN CHAR16 *Pattern)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  FileName[256], *Extension;
    struct LOADER_LIST      *LoaderList = NULL, *NewLoader;
    BOOLEAN                 FoundFallbackDuplicate = FALSE;

    if ((!SelfDirPath || !Path || ((StriCmp(Path, SelfDirPath) == 0) && (Volume->DeviceHandle != SelfVolume->DeviceHandle)) ||
           (StriCmp(Path, SelfDirPath) != 0)) &&
           (ShouldScan(Volume, Path))) {
       // look through contents of the directory
       DirIterOpen(Volume->RootDir, Path, &DirIter);
       while (DirIterNext(&DirIter, 2, Pattern, &DirEntry)) {
          Extension = FindExtension(DirEntry->FileName);
          if (DirEntry->FileName[0] == '.' ||
              StriCmp(Extension, L".icns") == 0 ||
              StriCmp(Extension, L".png") == 0 ||
              (StriCmp(DirEntry->FileName, FALLBACK_BASENAME) == 0 && (StriCmp(Path, L"EFI\\BOOT") == 0)) ||
              StriSubCmp(L"shell", DirEntry->FileName) ||
              IsIn(DirEntry->FileName, GlobalConfig.DontScanFiles))
                continue;   // skip this

          if (Path)
             SPrint(FileName, 255, L"\\%s\\%s", Path, DirEntry->FileName);
          else
             SPrint(FileName, 255, L"\\%s", DirEntry->FileName);
          CleanUpPathNameSlashes(FileName);
          NewLoader = AllocateZeroPool(sizeof(struct LOADER_LIST));
          if (NewLoader != NULL) {
             NewLoader->FileName = StrDuplicate(FileName);
             NewLoader->TimeStamp = DirEntry->ModificationTime;
             LoaderList = AddLoaderListEntry(LoaderList, NewLoader);
             if (DuplicatesFallback(Volume, FileName))
                FoundFallbackDuplicate = TRUE;
          } // if
          MyFreePool(Extension);
       } // while
       NewLoader = LoaderList;
       while (NewLoader != NULL) {
          AddLoaderEntry(NewLoader->FileName, NULL, Volume);
          NewLoader = NewLoader->NextEntry;
       } // while
       CleanUpLoaderList(LoaderList);
       Status = DirIterClose(&DirIter);
       if (Status != EFI_NOT_FOUND) {
          if (Path)
             SPrint(FileName, 255, L"while scanning the %s directory", Path);
          else
             StrCpy(FileName, L"while scanning the root directory");
          CheckError(Status, FileName);
       } // if (Status != EFI_NOT_FOUND)
    } // if not scanning our own directory

    return FoundFallbackDuplicate;
} /* static VOID ScanLoaderDir() */

static VOID ScanEfiFiles(REFIT_VOLUME *Volume) {
   EFI_STATUS              Status;
   REFIT_DIR_ITER          EfiDirIter;
   EFI_FILE_INFO           *EfiDirEntry;
   CHAR16                  FileName[256], *Directory, *MatchPatterns, *VolName = NULL;
   UINTN                   i, Length;
   BOOLEAN                 ScanFallbackLoader = TRUE;

//   Print(L"Entering ScanEfiFiles(), GlobalConfig.ScanAllLinux = %s\n", GlobalConfig.ScanAllLinux ? L"TRUE" : L"FALSE");
   MatchPatterns = StrDuplicate(LOADER_MATCH_PATTERNS);
   if (GlobalConfig.ScanAllLinux)
      MergeStrings(&MatchPatterns, LINUX_MATCH_PATTERNS, L',');
//   Print(L"MatchPatterns = '%s'\n", MatchPatterns);

   if ((Volume->RootDir != NULL) && (Volume->VolName != NULL)) {
      // check for Mac OS X boot loader
      if (ShouldScan(Volume, L"System\\Library\\CoreServices")) {
         StrCpy(FileName, MACOSX_LOADER_PATH);
         if (FileExists(Volume->RootDir, FileName) && !IsIn(L"boot.efi", GlobalConfig.DontScanFiles)) {
            AddLoaderEntry(FileName, L"Mac OS X", Volume);
            if (DuplicatesFallback(Volume, FileName))
               ScanFallbackLoader = FALSE;
         }

         // check for XOM
         StrCpy(FileName, L"System\\Library\\CoreServices\\xom.efi");
         if (FileExists(Volume->RootDir, FileName) && !IsIn(L"boot.efi", GlobalConfig.DontScanFiles)) {
            AddLoaderEntry(FileName, L"Windows XP (XoM)", Volume);
            if (DuplicatesFallback(Volume, FileName))
               ScanFallbackLoader = FALSE;
         }
      } // if should scan Mac directory

      // check for Microsoft boot loader/menu
      StrCpy(FileName, L"EFI\\Microsoft\\Boot\\Bootmgfw.efi");
      if (FileExists(Volume->RootDir, FileName) && ShouldScan(Volume, L"EFI\\Microsoft\\Boot") &&
          !IsIn(L"bootmgfw.efi", GlobalConfig.DontScanFiles)) {
         AddLoaderEntry(FileName, L"Microsoft EFI boot", Volume);
         if (DuplicatesFallback(Volume, FileName))
            ScanFallbackLoader = FALSE;
      }

      // scan the root directory for EFI executables
      if (ScanLoaderDir(Volume, L"\\", MatchPatterns))
         ScanFallbackLoader = FALSE;

      // scan subdirectories of the EFI directory (as per the standard)
      DirIterOpen(Volume->RootDir, L"EFI", &EfiDirIter);
      while (DirIterNext(&EfiDirIter, 1, NULL, &EfiDirEntry)) {
         if (StriCmp(EfiDirEntry->FileName, L"tools") == 0 || EfiDirEntry->FileName[0] == '.')
            continue;   // skip this, doesn't contain boot loaders or is scanned later
         SPrint(FileName, 255, L"EFI\\%s", EfiDirEntry->FileName);
         if (ScanLoaderDir(Volume, FileName, MatchPatterns))
            ScanFallbackLoader = FALSE;
      } // while()
      Status = DirIterClose(&EfiDirIter);
      if (Status != EFI_NOT_FOUND)
         CheckError(Status, L"while scanning the EFI directory");

      // Scan user-specified (or additional default) directories....
      i = 0;
      while ((Directory = FindCommaDelimited(GlobalConfig.AlsoScan, i++)) != NULL) {
         SplitVolumeAndFilename(&Directory, &VolName);
         CleanUpPathNameSlashes(Directory);
         Length = StrLen(Directory);
         if ((Length > 0) && ScanLoaderDir(Volume, Directory, MatchPatterns))
            ScanFallbackLoader = FALSE;
         MyFreePool(Directory);
         MyFreePool(VolName);
      } // while

      // If not a duplicate & if it exists & if it's not us, create an entry
      // for the fallback boot loader
      if (ScanFallbackLoader && FileExists(Volume->RootDir, FALLBACK_FULLNAME) && ShouldScan(Volume, L"EFI\\BOOT")) {
         AddLoaderEntry(FALLBACK_FULLNAME, L"Fallback boot loader", Volume);
      }
   } // if
} // static VOID ScanEfiFiles()

// Scan internal disks for valid EFI boot loaders....
static VOID ScanInternal(VOID) {
   UINTN                   VolumeIndex;

   for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
      if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_INTERNAL) {
         ScanEfiFiles(Volumes[VolumeIndex]);
      }
   } // for
} // static VOID ScanInternal()

// Scan external disks for valid EFI boot loaders....
static VOID ScanExternal(VOID) {
   UINTN                   VolumeIndex;

   for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
      if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_EXTERNAL) {
         ScanEfiFiles(Volumes[VolumeIndex]);
      }
   } // for
} // static VOID ScanExternal()

// Scan internal disks for valid EFI boot loaders....
static VOID ScanOptical(VOID) {
   UINTN                   VolumeIndex;

   for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
      if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_OPTICAL) {
         ScanEfiFiles(Volumes[VolumeIndex]);
      }
   } // for
} // static VOID ScanOptical()

//
// legacy boot functions
//

static EFI_STATUS ActivateMbrPartition(IN EFI_BLOCK_IO *BlockIO, IN UINTN PartitionIndex)
{
    EFI_STATUS          Status;
    UINT8               SectorBuffer[512];
    MBR_PARTITION_INFO  *MbrTable, *EMbrTable;
    UINT32              ExtBase, ExtCurrent, NextExtCurrent;
    UINTN               LogicalPartitionIndex = 4;
    UINTN               i;
    BOOLEAN             HaveBootCode;

    // read MBR
    Status = refit_call5_wrapper(BlockIO->ReadBlocks, BlockIO, BlockIO->Media->MediaId, 0, 512, SectorBuffer);
    if (EFI_ERROR(Status))
        return Status;
    if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55)
        return EFI_NOT_FOUND;  // safety measure #1

    // add boot code if necessary
    HaveBootCode = FALSE;
    for (i = 0; i < MBR_BOOTCODE_SIZE; i++) {
        if (SectorBuffer[i] != 0) {
            HaveBootCode = TRUE;
            break;
        }
    }
    if (!HaveBootCode) {
        // no boot code found in the MBR, add the syslinux MBR code
        SetMem(SectorBuffer, MBR_BOOTCODE_SIZE, 0);
        CopyMem(SectorBuffer, syslinux_mbr, SYSLINUX_MBR_SIZE);
    }

    // set the partition active
    MbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);
    ExtBase = 0;
    for (i = 0; i < 4; i++) {
        if (MbrTable[i].Flags != 0x00 && MbrTable[i].Flags != 0x80)
            return EFI_NOT_FOUND;   // safety measure #2
        if (i == PartitionIndex)
            MbrTable[i].Flags = 0x80;
        else if (PartitionIndex >= 4 && IS_EXTENDED_PART_TYPE(MbrTable[i].Type)) {
            MbrTable[i].Flags = 0x80;
            ExtBase = MbrTable[i].StartLBA;
        } else
            MbrTable[i].Flags = 0x00;
    }

    // write MBR
    Status = refit_call5_wrapper(BlockIO->WriteBlocks, BlockIO, BlockIO->Media->MediaId, 0, 512, SectorBuffer);
    if (EFI_ERROR(Status))
        return Status;

    if (PartitionIndex >= 4) {
        // we have to activate a logical partition, so walk the EMBR chain

        // NOTE: ExtBase was set above while looking at the MBR table
        for (ExtCurrent = ExtBase; ExtCurrent; ExtCurrent = NextExtCurrent) {
            // read current EMBR
            Status = refit_call5_wrapper(BlockIO->ReadBlocks, BlockIO, BlockIO->Media->MediaId, ExtCurrent, 512, SectorBuffer);
            if (EFI_ERROR(Status))
                return Status;
            if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55)
                return EFI_NOT_FOUND;  // safety measure #3

            // scan EMBR, set appropriate partition active
            EMbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);
            NextExtCurrent = 0;
            for (i = 0; i < 4; i++) {
                if (EMbrTable[i].Flags != 0x00 && EMbrTable[i].Flags != 0x80)
                    return EFI_NOT_FOUND;   // safety measure #4
                if (EMbrTable[i].StartLBA == 0 || EMbrTable[i].Size == 0)
                    break;
                if (IS_EXTENDED_PART_TYPE(EMbrTable[i].Type)) {
                    // link to next EMBR
                    NextExtCurrent = ExtBase + EMbrTable[i].StartLBA;
                    EMbrTable[i].Flags = (PartitionIndex >= LogicalPartitionIndex) ? 0x80 : 0x00;
                    break;
                } else {
                    // logical partition
                    EMbrTable[i].Flags = (PartitionIndex == LogicalPartitionIndex) ? 0x80 : 0x00;
                    LogicalPartitionIndex++;
                }
            }

            // write current EMBR
            Status = refit_call5_wrapper(BlockIO->WriteBlocks, BlockIO, BlockIO->Media->MediaId, ExtCurrent, 512, SectorBuffer);
            if (EFI_ERROR(Status))
                return Status;

            if (PartitionIndex < LogicalPartitionIndex)
                break;  // stop the loop, no need to touch further EMBRs
        }

    }

    return EFI_SUCCESS;
} /* static EFI_STATUS ActivateMbrPartition() */

// early 2006 Core Duo / Core Solo models
static UINT8 LegacyLoaderDevicePath1Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF9, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};
// mid-2006 Mac Pro (and probably other Core 2 models)
static UINT8 LegacyLoaderDevicePath2Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF7, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};
// mid-2007 MBP ("Santa Rosa" based models)
static UINT8 LegacyLoaderDevicePath3Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF8, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};
// early-2008 MBA
static UINT8 LegacyLoaderDevicePath4Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF8, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};
// late-2008 MB/MBP (NVidia chipset)
static UINT8 LegacyLoaderDevicePath5Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x40, 0xCB, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

static EFI_DEVICE_PATH *LegacyLoaderList[] = {
    (EFI_DEVICE_PATH *)LegacyLoaderDevicePath1Data,
    (EFI_DEVICE_PATH *)LegacyLoaderDevicePath2Data,
    (EFI_DEVICE_PATH *)LegacyLoaderDevicePath3Data,
    (EFI_DEVICE_PATH *)LegacyLoaderDevicePath4Data,
    (EFI_DEVICE_PATH *)LegacyLoaderDevicePath5Data,
    NULL
};

#define MAX_DISCOVERED_PATHS (16)

static VOID StartLegacy(IN LEGACY_ENTRY *Entry)
{
    EFI_STATUS          Status;
    EG_IMAGE            *BootLogoImage;
    UINTN               ErrorInStep = 0;
    EFI_DEVICE_PATH     *DiscoveredPathList[MAX_DISCOVERED_PATHS];

    BeginExternalScreen(TRUE, L"Booting Legacy OS (Mac mode)");

    BootLogoImage = LoadOSIcon(Entry->Volume->OSIconName, L"legacy", TRUE);
    if (BootLogoImage != NULL)
        BltImageAlpha(BootLogoImage,
                      (UGAWidth  - BootLogoImage->Width ) >> 1,
                      (UGAHeight - BootLogoImage->Height) >> 1,
                      &StdBackgroundPixel);

    if (Entry->Volume->IsMbrPartition) {
        ActivateMbrPartition(Entry->Volume->WholeDiskBlockIO, Entry->Volume->MbrPartitionIndex);
    }

    ExtractLegacyLoaderPaths(DiscoveredPathList, MAX_DISCOVERED_PATHS, LegacyLoaderList);

    Status = StartEFIImageList(DiscoveredPathList, Entry->LoadOptions, NULL, L"legacy loader", 0, &ErrorInStep, TRUE);
    if (Status == EFI_NOT_FOUND) {
        if (ErrorInStep == 1) {
            Print(L"\nPlease make sure that you have the latest firmware update installed.\n");
        } else if (ErrorInStep == 3) {
            Print(L"\nThe firmware refused to boot from the selected volume. Note that external\n"
                  L"hard drives are not well-supported by Apple's firmware for legacy OS booting.\n");
        }
    }
    FinishExternalScreen();
} /* static VOID StartLegacy() */

// Start a device on a non-Mac using the EFI_LEGACY_BIOS_PROTOCOL
#ifdef __MAKEWITH_TIANO
static VOID StartLegacyUEFI(IN LEGACY_ENTRY *Entry)
{
    BeginExternalScreen(TRUE, L"Booting Legacy OS (UEFI mode)");

    BdsLibConnectDevicePath (Entry->BdsOption->DevicePath);
    BdsLibDoLegacyBoot(Entry->BdsOption);

    // If we get here, it means that there was a failure....
    Print(L"Failure booting legacy (BIOS) OS.");
    PauseForKey();
    FinishExternalScreen();
} // static VOID StartLegacyUEFI()
#endif // __MAKEWITH_TIANO

static LEGACY_ENTRY * AddLegacyEntry(IN CHAR16 *LoaderTitle, IN REFIT_VOLUME *Volume)
{
    LEGACY_ENTRY            *Entry, *SubEntry;
    REFIT_MENU_SCREEN       *SubScreen;
    CHAR16                  *VolDesc;
    CHAR16                  ShortcutLetter = 0;

    if (LoaderTitle == NULL) {
        if (Volume->OSName != NULL) {
            LoaderTitle = Volume->OSName;
            if (LoaderTitle[0] == 'W' || LoaderTitle[0] == 'L')
                ShortcutLetter = LoaderTitle[0];
        } else
            LoaderTitle = L"Legacy OS";
    }
    if (Volume->VolName != NULL)
        VolDesc = Volume->VolName;
    else
        VolDesc = (Volume->DiskKind == DISK_KIND_OPTICAL) ? L"CD" : L"HD";

    // prepare the menu entry
    Entry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    Entry->me.Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(Entry->me.Title, 255, L"Boot %s from %s", LoaderTitle, VolDesc);
    Entry->me.Tag          = TAG_LEGACY;
    Entry->me.Row          = 0;
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image        = LoadOSIcon(Volume->OSIconName, L"legacy", FALSE);
    Entry->me.BadgeImage   = Volume->VolBadgeImage;
    Entry->Volume          = Volume;
    Entry->LoadOptions     = (Volume->DiskKind == DISK_KIND_OPTICAL) ? L"CD" :
        ((Volume->DiskKind == DISK_KIND_EXTERNAL) ? L"USB" : L"HD");
    Entry->Enabled         = TRUE;

    // create the submenu
    SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
    SubScreen->Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(SubScreen->Title, 255, L"Boot Options for %s on %s", LoaderTitle, VolDesc);
    SubScreen->TitleImage = Entry->me.Image;
    SubScreen->Hint1 = StrDuplicate(SUBSCREEN_HINT1);
    if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR) {
       SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2_NO_EDITOR);
    } else {
       SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2);
    } // if/else

    // default entry
    SubEntry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    SubEntry->me.Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(SubEntry->me.Title, 255, L"Boot %s", LoaderTitle);
    SubEntry->me.Tag          = TAG_LEGACY;
    SubEntry->Volume          = Entry->Volume;
    SubEntry->LoadOptions     = Entry->LoadOptions;
    AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);

    AddMenuEntry(SubScreen, &MenuEntryReturn);
    Entry->me.SubScreen = SubScreen;
    AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
    return Entry;
} /* static LEGACY_ENTRY * AddLegacyEntry() */


#ifdef __MAKEWITH_GNUEFI
static VOID ScanLegacyUEFI(IN UINTN DiskType){}
#else
// default volume badge icon based on disk kind
static EG_IMAGE * GetDiskBadge(IN UINTN DiskType) {
   EG_IMAGE * Badge = NULL;

   switch (DiskType) {
      case BBS_HARDDISK:
         Badge = BuiltinIcon(BUILTIN_ICON_VOL_INTERNAL);
         break;
      case BBS_USB:
         Badge = BuiltinIcon(BUILTIN_ICON_VOL_EXTERNAL);
         break;
      case BBS_CDROM:
         Badge = BuiltinIcon(BUILTIN_ICON_VOL_OPTICAL);
         break;
   } // switch()
   return Badge;
} // static EG_IMAGE * GetDiskBadge()

/**
    Create a rEFInd boot option from a Legacy BIOS protocol option.
*/
static LEGACY_ENTRY * AddLegacyEntryUEFI(BDS_COMMON_OPTION *BdsOption, IN UINT16 DiskType)
{
    LEGACY_ENTRY            *Entry, *SubEntry;
    REFIT_MENU_SCREEN       *SubScreen;
    CHAR16                  ShortcutLetter = 0;
    CHAR16 *LegacyDescription = BdsOption->Description;

    // prepare the menu entry
    Entry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    Entry->me.Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(Entry->me.Title, 255, L"Boot legacy target %s", LegacyDescription);
    Entry->me.Tag          = TAG_LEGACY_UEFI;
    Entry->me.Row          = 0;
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image        = LoadOSIcon(L"legacy", L"legacy", TRUE);
    Entry->LoadOptions     = (DiskType == BBS_CDROM) ? L"CD" :
                             ((DiskType == BBS_USB) ? L"USB" : L"HD");
    Entry->me.BadgeImage   = GetDiskBadge(DiskType);
    Entry->BdsOption       = BdsOption;
    Entry->Enabled         = TRUE;

    // create the submenu
    SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
    SubScreen->Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(SubScreen->Title, 255, L"No boot options for legacy target");
    SubScreen->TitleImage = Entry->me.Image;
    SubScreen->Hint1 = StrDuplicate(SUBSCREEN_HINT1);
    if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR) {
       SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2_NO_EDITOR);
    } else {
       SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2);
    } // if/else

    // default entry
    SubEntry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    SubEntry->me.Title = AllocateZeroPool(256 * sizeof(CHAR16));
    SPrint(SubEntry->me.Title, 255, L"Boot %s", LegacyDescription);
    SubEntry->me.Tag          = TAG_LEGACY_UEFI;
    Entry->BdsOption          = BdsOption; 
    AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);

    AddMenuEntry(SubScreen, &MenuEntryReturn);
    Entry->me.SubScreen = SubScreen;
    AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
    return Entry;
} /* static LEGACY_ENTRY * AddLegacyEntryUEFI() */

/**
    Scan for legacy BIOS targets on machines that implement EFI_LEGACY_BIOS_PROTOCOL.
    In testing, protocol has not been implemented on Macs but has been
    implemented on several Dell PCs and an ASUS motherboard.
    Restricts output to disks of the specified DiskType.
*/
static VOID ScanLegacyUEFI(IN UINTN DiskType)
{
    EFI_STATUS                Status;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
    UINT16                    *BootOrder = NULL;
    UINTN                     Index = 0;
    CHAR16                    BootOption[10];
    UINTN                     BootOrderSize = 0;
    CHAR16            Buffer[20];
    BDS_COMMON_OPTION *BdsOption;
    LIST_ENTRY        TempList;
    BBS_BBS_DEVICE_PATH * BbsDevicePath = NULL;

    InitializeListHead (&TempList);
    ZeroMem (Buffer, sizeof (Buffer));

    // If LegacyBios protocol is not implemented on this platform, then
    //we do not support this type of legacy boot on this machine.
    Status = gBS->LocateProtocol(&gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
    if (EFI_ERROR (Status))
        return;

    // Grab the boot order
    BootOrder = BdsLibGetVariableAndSize(L"BootOrder", &gEfiGlobalVariableGuid, &BootOrderSize);
    if (BootOrder == NULL) {
        BootOrderSize = 0;
    }

    Index = 0;
    while (Index < BootOrderSize / sizeof (UINT16))
    {
        // Grab each boot option variable from the boot order, and convert
        // the variable into a BDS boot option
        UnicodeSPrint (BootOption, sizeof (BootOption), L"Boot%04x", BootOrder[Index]);
        BdsOption = BdsLibVariableToOption (&TempList, BootOption);

        if (BdsOption != NULL) {
           BbsDevicePath = (BBS_BBS_DEVICE_PATH *)BdsOption->DevicePath;

           // Only add the entry if it is of a requested type (e.g. USB, HD)

           // Two checks necessary because some systems return EFI boot loaders
           // with a DeviceType value that would inappropriately include them
           // as legacy loaders....
           if ((BbsDevicePath->DeviceType == DiskType) && (BdsOption->DevicePath->Type == DEVICE_TYPE_BIOS)) {
              AddLegacyEntryUEFI(BdsOption, BbsDevicePath->DeviceType);
           }
        }
        Index++;
    }
} /* static VOID ScanLegacyUEFI() */
#endif // __MAKEWITH_GNUEFI

static VOID ScanLegacyVolume(REFIT_VOLUME *Volume, UINTN VolumeIndex) {
   UINTN VolumeIndex2;
   BOOLEAN ShowVolume, HideIfOthersFound;

   ShowVolume = FALSE;
   HideIfOthersFound = FALSE;
   if (Volume->IsAppleLegacy) {
      ShowVolume = TRUE;
      HideIfOthersFound = TRUE;
   } else if (Volume->HasBootCode) {
      ShowVolume = TRUE;
      if (Volume->BlockIO == Volume->WholeDiskBlockIO &&
         Volume->BlockIOOffset == 0 &&
         Volume->OSName == NULL)
         // this is a whole disk (MBR) entry; hide if we have entries for partitions
         HideIfOthersFound = TRUE;
   }
   if (HideIfOthersFound) {
      // check for other bootable entries on the same disk
      for (VolumeIndex2 = 0; VolumeIndex2 < VolumesCount; VolumeIndex2++) {
         if (VolumeIndex2 != VolumeIndex && Volumes[VolumeIndex2]->HasBootCode &&
             Volumes[VolumeIndex2]->WholeDiskBlockIO == Volume->WholeDiskBlockIO)
            ShowVolume = FALSE;
      }
   }

   if (ShowVolume)
      AddLegacyEntry(NULL, Volume);
} // static VOID ScanLegacyVolume()

// Scan attached optical discs for legacy (BIOS) boot code
// and add anything found to the list....
static VOID ScanLegacyDisc(VOID)
{
   UINTN                   VolumeIndex;
   REFIT_VOLUME            *Volume;

   if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
      for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
         Volume = Volumes[VolumeIndex];
         if (Volume->DiskKind == DISK_KIND_OPTICAL)
            ScanLegacyVolume(Volume, VolumeIndex);
      } // for
   } else if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
      ScanLegacyUEFI(BBS_CDROM);
   }
} /* static VOID ScanLegacyDisc() */

// Scan internal hard disks for legacy (BIOS) boot code
// and add anything found to the list....
static VOID ScanLegacyInternal(VOID)
{
    UINTN                   VolumeIndex;
    REFIT_VOLUME            *Volume;

    if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
       for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
           Volume = Volumes[VolumeIndex];
           if (Volume->DiskKind == DISK_KIND_INTERNAL)
               ScanLegacyVolume(Volume, VolumeIndex);
       } // for
    } else if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
       ScanLegacyUEFI(BBS_HARDDISK);
    }
} /* static VOID ScanLegacyInternal() */

// Scan external disks for legacy (BIOS) boot code
// and add anything found to the list....
static VOID ScanLegacyExternal(VOID)
{
   UINTN                   VolumeIndex;
   REFIT_VOLUME            *Volume;

   if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
      for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
         Volume = Volumes[VolumeIndex];
         if (Volume->DiskKind == DISK_KIND_EXTERNAL)
            ScanLegacyVolume(Volume, VolumeIndex);
      } // for
   } else if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
      ScanLegacyUEFI(BBS_USB);
   }
} /* static VOID ScanLegacyExternal() */

//
// pre-boot tool functions
//

static VOID StartTool(IN LOADER_ENTRY *Entry)
{
   BeginExternalScreen(Entry->UseGraphicsMode, Entry->me.Title + 6);  // assumes "Start <title>" as assigned below
   StartEFIImage(Entry->DevicePath, Entry->LoadOptions, Basename(Entry->LoaderPath),
                 Basename(Entry->LoaderPath), Entry->OSType, NULL, TRUE);
   FinishExternalScreen();
} /* static VOID StartTool() */

static LOADER_ENTRY * AddToolEntry(EFI_HANDLE DeviceHandle, IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, IN EG_IMAGE *Image,
                                   IN CHAR16 ShortcutLetter, IN BOOLEAN UseGraphicsMode)
{
    LOADER_ENTRY *Entry;
    CHAR16       *TitleStr = NULL;

    Entry = AllocateZeroPool(sizeof(LOADER_ENTRY));

    MergeStrings(&TitleStr, L"Start ", 0);
    MergeStrings(&TitleStr, LoaderTitle, 0);
    Entry->me.Title = TitleStr;
    Entry->me.Tag = TAG_TOOL;
    Entry->me.Row = 1;
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image = Image;
    Entry->LoaderPath = (LoaderPath) ? StrDuplicate(LoaderPath) : NULL;
    Entry->DevicePath = FileDevicePath(DeviceHandle, Entry->LoaderPath);
    Entry->UseGraphicsMode = UseGraphicsMode;

    AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
    return Entry;
} /* static LOADER_ENTRY * AddToolEntry() */

//
// pre-boot driver functions
//

static UINTN ScanDriverDir(IN CHAR16 *Path)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    UINTN                   NumFound = 0;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  FileName[256];

    CleanUpPathNameSlashes(Path);
    // look through contents of the directory
    DirIterOpen(SelfRootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, LOADER_MATCH_PATTERNS, &DirEntry)) {
        if (DirEntry->FileName[0] == '.')
            continue;   // skip this

        SPrint(FileName, 255, L"%s\\%s", Path, DirEntry->FileName);
        NumFound++;
        Status = StartEFIImage(FileDevicePath(SelfLoadedImage->DeviceHandle, FileName),
                               L"", DirEntry->FileName, DirEntry->FileName, 0, NULL, FALSE);
    }
    Status = DirIterClose(&DirIter);
    if (Status != EFI_NOT_FOUND) {
        SPrint(FileName, 255, L"while scanning the %s directory", Path);
        CheckError(Status, FileName);
    }
    return (NumFound);
}

#ifdef __MAKEWITH_GNUEFI
static EFI_STATUS ConnectAllDriversToAllControllers(VOID)
{
    EFI_STATUS           Status;
    UINTN                AllHandleCount;
    EFI_HANDLE           *AllHandleBuffer;
    UINTN                Index;
    UINTN                HandleCount;
    EFI_HANDLE           *HandleBuffer;
    UINT32               *HandleType;
    UINTN                HandleIndex;
    BOOLEAN              Parent;
    BOOLEAN              Device;

    Status = LibLocateHandle(AllHandles,
                             NULL,
                             NULL,
                             &AllHandleCount,
                             &AllHandleBuffer);
    if (EFI_ERROR(Status))
        return Status;

    for (Index = 0; Index < AllHandleCount; Index++) {
        //
        // Scan the handle database
        //
        Status = LibScanHandleDatabase(NULL,
                                       NULL,
                                       AllHandleBuffer[Index],
                                       NULL,
                                       &HandleCount,
                                       &HandleBuffer,
                                       &HandleType);
        if (EFI_ERROR (Status))
            goto Done;

        Device = TRUE;
        if (HandleType[Index] & EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE)
            Device = FALSE;
        if (HandleType[Index] & EFI_HANDLE_TYPE_IMAGE_HANDLE)
            Device = FALSE;

        if (Device) {
            Parent = FALSE;
            for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
                if (HandleType[HandleIndex] & EFI_HANDLE_TYPE_PARENT_HANDLE)
                    Parent = TRUE;
            } // for

            if (!Parent) {
                if (HandleType[Index] & EFI_HANDLE_TYPE_DEVICE_HANDLE) {
                   Status = refit_call4_wrapper(BS->ConnectController,
                                                AllHandleBuffer[Index],
                                                NULL,
                                                NULL,
                                                TRUE);
                }
            }
        }

        MyFreePool (HandleBuffer);
        MyFreePool (HandleType);
    }

Done:
    MyFreePool (AllHandleBuffer);
    return Status;
} /* EFI_STATUS ConnectAllDriversToAllControllers() */
#else
static EFI_STATUS ConnectAllDriversToAllControllers(VOID) {
   BdsLibConnectAllDriversToAllControllers();
   return 0;
}
#endif

// Load all EFI drivers from rEFInd's "drivers" subdirectory and from the
// directories specified by the user in the "scan_driver_dirs" configuration
// file line.
static VOID LoadDrivers(VOID)
{
    CHAR16        *Directory, *SelfDirectory;
    UINTN         i = 0, Length, NumFound = 0;

    // load drivers from the subdirectories of rEFInd's home directory specified
    // in the DRIVER_DIRS constant.
    while ((Directory = FindCommaDelimited(DRIVER_DIRS, i++)) != NULL) {
       SelfDirectory = SelfDirPath ? StrDuplicate(SelfDirPath) : NULL;
       CleanUpPathNameSlashes(SelfDirectory);
       MergeStrings(&SelfDirectory, Directory, L'\\');
       NumFound += ScanDriverDir(SelfDirectory);
       MyFreePool(Directory);
       MyFreePool(SelfDirectory);
    }

    // Scan additional user-specified driver directories....
    i = 0;
    while ((Directory = FindCommaDelimited(GlobalConfig.DriverDirs, i++)) != NULL) {
       CleanUpPathNameSlashes(Directory);
       Length = StrLen(Directory);
       if (Length > 0) {
          NumFound += ScanDriverDir(Directory);
       } // if
       MyFreePool(Directory);
    } // while

    // connect all devices
    if (NumFound > 0)
       ConnectAllDriversToAllControllers();
} /* static VOID LoadDrivers() */

// Determine what (if any) type of legacy (BIOS) boot support is available
static VOID FindLegacyBootType(VOID) {
#ifdef __MAKEWITH_TIANO
   EFI_STATUS                Status;
   EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
#endif

   GlobalConfig.LegacyType = LEGACY_TYPE_NONE;

   // UEFI-style legacy BIOS support is available only with the TianoCore EDK2
   // build environment, and then only with some EFI implementations....
#ifdef __MAKEWITH_TIANO
   Status = gBS->LocateProtocol (&gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
   if (!EFI_ERROR (Status))
      GlobalConfig.LegacyType = LEGACY_TYPE_UEFI;
#endif

   // Macs have their own system. If the firmware vendor code contains the
   // string "Apple", assume it's available. Note that this overrides the
   // UEFI type, and might yield false positives if the vendor string
   // contains "Apple" as part of something bigger, so this isn't 100%
   // perfect.
   if (StriSubCmp(L"Apple", ST->FirmwareVendor))
      GlobalConfig.LegacyType = LEGACY_TYPE_MAC;
} // static VOID FindLegacyBootType

// Warn the user if legacy OS scans are enabled but the firmware or this
// application can't support them....
static VOID WarnIfLegacyProblems() {
   BOOLEAN  found = FALSE;
   UINTN    i = 0;

   if (GlobalConfig.LegacyType == LEGACY_TYPE_NONE) {
      do {
         if (GlobalConfig.ScanFor[i] == 'h' || GlobalConfig.ScanFor[i] == 'b' || GlobalConfig.ScanFor[i] == 'c')
            found = TRUE;
         i++;
      } while ((i < NUM_SCAN_OPTIONS) && (!found));
      if (found) {
         Print(L"NOTE: refind.conf's 'scanfor' line specifies scanning for one or more legacy\n");
         Print(L"(BIOS) boot options; however, this is not possible because ");
#ifdef __MAKEWITH_TIANO
         Print(L"your computer lacks\n");
         Print(L"the necessary Compatibility Support Module (CSM) support.\n");
#else
         Print(L"this program was\n");
         Print(L"compiled without the necessary support. Please visit\n");
         Print(L"http://www.rodsbooks.com/refind/getting.html and download and install a rEFInd\n");
         Print(L"binary built with the TianoCore EDK2 to enable legacy boot support.\n");
#endif
         PauseForKey();
      } // if (found)
   } // if no legacy support
} // static VOID WarnIfLegacyProblems()

// Locates boot loaders. NOTE: This assumes that GlobalConfig.LegacyType is set correctly.
static VOID ScanForBootloaders(VOID) {
   UINTN                     i;

   ScanVolumes();

   // scan for loaders and tools, add them to the menu
   for (i = 0; i < NUM_SCAN_OPTIONS; i++) {
      switch(GlobalConfig.ScanFor[i]) {
         case 'c': case 'C':
            ScanLegacyDisc();
            break;
         case 'h': case 'H':
            ScanLegacyInternal();
            break;
         case 'b': case 'B':
            ScanLegacyExternal();
            break;
         case 'm': case 'M':
            ScanUserConfigured(CONFIG_FILE_NAME);
            break;
         case 'e': case 'E':
            ScanExternal();
            break;
         case 'i': case 'I':
            ScanInternal();
            break;
         case 'o': case 'O':
            ScanOptical();
            break;
      } // switch()
   } // for

   // assign shortcut keys
   for (i = 0; i < MainMenu.EntryCount && MainMenu.Entries[i]->Row == 0 && i < 9; i++)
      MainMenu.Entries[i]->ShortcutDigit = (CHAR16)('1' + i);

   // wait for user ACK when there were errors
   FinishTextScreen(FALSE);
} // static VOID ScanForBootloaders()

// Add the second-row tags containing built-in and external tools (EFI shell,
// reboot, etc.)
static VOID ScanForTools(VOID) {
   CHAR16 *FileName = NULL, *MokLocations, *MokName, *PathName, Description[256];
   REFIT_MENU_ENTRY *TempMenuEntry;
   UINTN i, j, k, VolumeIndex;

   MokLocations = StrDuplicate(MOK_LOCATIONS);
   if (MokLocations != NULL)
      MergeStrings(&MokLocations, SelfDirPath, L',');

   for (i = 0; i < NUM_TOOLS; i++) {
      switch(GlobalConfig.ShowTools[i]) {
         // NOTE: Be sure that FileName is NULL at the end of each case.
         case TAG_SHUTDOWN:
            TempMenuEntry = CopyMenuEntry(&MenuEntryShutdown);
            TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_SHUTDOWN);
            AddMenuEntry(&MainMenu, TempMenuEntry);
            break;
         case TAG_REBOOT:
            TempMenuEntry = CopyMenuEntry(&MenuEntryReset);
            TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_RESET);
            AddMenuEntry(&MainMenu, TempMenuEntry);
            break;
         case TAG_ABOUT:
            TempMenuEntry = CopyMenuEntry(&MenuEntryAbout);
            TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_ABOUT);
            AddMenuEntry(&MainMenu, TempMenuEntry);
            break;
         case TAG_EXIT:
            TempMenuEntry = CopyMenuEntry(&MenuEntryExit);
            TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_EXIT);
            AddMenuEntry(&MainMenu, TempMenuEntry);
            break;
         case TAG_SHELL:
            j = 0;
            while ((FileName = FindCommaDelimited(SHELL_NAMES, j++)) != NULL) {
               if (FileExists(SelfRootDir, FileName)) {
                  AddToolEntry(SelfLoadedImage->DeviceHandle, FileName, L"EFI Shell", BuiltinIcon(BUILTIN_ICON_TOOL_SHELL),
                               'S', FALSE);
               }
               MyFreePool(FileName);
            } // while
            break;
         case TAG_GPTSYNC:
            FileName = StrDuplicate(L"\\efi\\tools\\gptsync.efi");
            if (FileExists(SelfRootDir, FileName)) {
               AddToolEntry(SelfLoadedImage->DeviceHandle, FileName, L"Make Hybrid MBR", BuiltinIcon(BUILTIN_ICON_TOOL_PART), 'P', FALSE);
            }
            MyFreePool(FileName);
            FileName = NULL;
            break;
         case TAG_APPLE_RECOVERY:
            FileName = StrDuplicate(L"\\com.apple.recovery.boot\\boot.efi");
            for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
               if ((Volumes[VolumeIndex]->RootDir != NULL) && (FileExists(Volumes[VolumeIndex]->RootDir, FileName))) {
                  SPrint(Description, 255, L"Apple Recovery on %s", Volumes[VolumeIndex]->VolName);
                  AddToolEntry(Volumes[VolumeIndex]->DeviceHandle, FileName, Description,
                               BuiltinIcon(BUILTIN_ICON_TOOL_APPLE_RESCUE), 'R', TRUE);
               }
            } // for
            MyFreePool(FileName);
            FileName = NULL;
            break;
         case TAG_MOK_TOOL:
            j = 0;
            while ((FileName = FindCommaDelimited(MokLocations, j++)) != NULL) {
               k = 0;
               while ((MokName = FindCommaDelimited(MOK_NAMES, k++)) != NULL) {
                  PathName = StrDuplicate(FileName);
                  MergeStrings(&PathName, MokName, (StriCmp(PathName, L"\\") == 0) ? 0 : L'\\');
                  for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
                     if ((Volumes[VolumeIndex]->RootDir != NULL) && (FileExists(Volumes[VolumeIndex]->RootDir, PathName))) {
                        SPrint(Description, 255, L"MOK utility at %s on %s", PathName, Volumes[VolumeIndex]->VolName);
                        AddToolEntry(Volumes[VolumeIndex]->DeviceHandle, PathName, Description,
                                     BuiltinIcon(BUILTIN_ICON_TOOL_MOK_TOOL), 'S', FALSE);
                     } // if
                  } // for
                  MyFreePool(PathName);
                  MyFreePool(MokName);
               } // while MOK_NAMES
               MyFreePool(FileName);
            } // while MokLocations

            break;
      } // switch()
   } // for
} // static VOID ScanForTools

// Rescan for boot loaders
VOID RescanAll(VOID) {
   EG_PIXEL           BGColor;

   BGColor.b = 255;
   BGColor.g = 175;
   BGColor.r = 100;
   BGColor.a = 0;
   egDisplayMessage(L"Scanning for new boot loaders; please wait....", &BGColor);
   FreeList((VOID ***) &(MainMenu.Entries), &MainMenu.EntryCount);
   MainMenu.Entries = NULL;
   MainMenu.EntryCount = 0;
   ReadConfig(CONFIG_FILE_NAME);
   ConnectAllDriversToAllControllers();
   ScanVolumes();
   ScanForBootloaders();
   ScanForTools();
   SetupScreen();
} // VOID RescanAll()

#ifdef __MAKEWITH_TIANO

// Minimal initialization function
static VOID InitializeLib(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
   gST            = SystemTable;
   //    gImageHandle   = ImageHandle;
   gBS            = SystemTable->BootServices;
   //    gRS            = SystemTable->RuntimeServices;
   gRT = SystemTable->RuntimeServices; // Some BDS functions need gRT to be set
   EfiGetSystemConfigurationTable (&gEfiDxeServicesTableGuid, (VOID **) &gDS);

   InitializeConsoleSim();
}

#endif

// Set up our own Secure Boot extensions....
// Returns TRUE on success, FALSE otherwise
static BOOLEAN SecureBootSetup(VOID) {
   EFI_STATUS Status;
   BOOLEAN    Success = FALSE;

   if (secure_mode() && ShimLoaded()) {
      Status = security_policy_install();
      if (Status == EFI_SUCCESS) {
         Success = TRUE;
      } else {
         Print(L"Failed to install MOK Secure Boot extensions");
      }
   }
   return Success;
} // VOID SecureBootSetup()

// Remove our own Secure Boot extensions....
// Returns TRUE on success, FALSE otherwise
static BOOLEAN SecureBootUninstall(VOID) {
   EFI_STATUS Status;
   BOOLEAN    Success = TRUE;

   if (secure_mode()) {
      Status = security_policy_uninstall();
      if (Status != EFI_SUCCESS) {
         Success = FALSE;
         BeginTextScreen(L"Secure Boot Policy Failure");
         Print(L"Failed to uninstall MOK Secure Boot extensions; forcing a reboot.");
         PauseForKey();
         refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
      }
   }
   return Success;
} // VOID SecureBootUninstall

//
// main entry point
//
EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS         Status;
    BOOLEAN            MainLoopRunning = TRUE;
    BOOLEAN            MokProtocol;
    REFIT_MENU_ENTRY   *ChosenEntry;
    UINTN              MenuExit, i;
    CHAR16             *Selection = NULL;
    EG_PIXEL           BGColor;

    // bootstrap
    InitializeLib(ImageHandle, SystemTable);
    Status = InitRefitLib(ImageHandle);
    if (EFI_ERROR(Status))
        return Status;

    // read configuration
    CopyMem(GlobalConfig.ScanFor, "ieom      ", NUM_SCAN_OPTIONS);
    FindLegacyBootType();
    if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC)
       CopyMem(GlobalConfig.ScanFor, "ihebocm   ", NUM_SCAN_OPTIONS);
    ScanVolumes();
    ReadConfig(CONFIG_FILE_NAME);

    InitScreen();
    WarnIfLegacyProblems();
    MainMenu.TimeoutSeconds = GlobalConfig.Timeout;

    // disable EFI watchdog timer
    refit_call4_wrapper(BS->SetWatchdogTimer, 0x0000, 0x0000, 0x0000, NULL);

    // further bootstrap (now with config available)
    MokProtocol = SecureBootSetup();
    LoadDrivers();
    ScanForBootloaders();
    ScanForTools();
    SetupScreen();

    if (GlobalConfig.ScanDelay > 0) {
       BGColor.b = 255;
       BGColor.g = 175;
       BGColor.r = 100;
       BGColor.a = 0;
       egDisplayMessage(L"Pausing before disk scan; please wait....", &BGColor);
       for (i = 0; i < GlobalConfig.ScanDelay; i++)
          refit_call1_wrapper(BS->Stall, 1000000);
       RescanAll();
    } // if

    if (GlobalConfig.DefaultSelection)
       Selection = StrDuplicate(GlobalConfig.DefaultSelection);

    while (MainLoopRunning) {
        MenuExit = RunMainMenu(&MainMenu, Selection, &ChosenEntry);

        // The Escape key triggers a re-scan operation....
        if (MenuExit == MENU_EXIT_ESCAPE) {
            RescanAll();
            continue;
        }

        switch (ChosenEntry->Tag) {

            case TAG_REBOOT:    // Reboot
                TerminateScreen();
                refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
                MainLoopRunning = FALSE;   // just in case we get this far
                break;

            case TAG_SHUTDOWN: // Shut Down
                TerminateScreen();
                refit_call4_wrapper(RT->ResetSystem, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
                MainLoopRunning = FALSE;   // just in case we get this far
                break;

            case TAG_ABOUT:    // About rEFInd
                AboutrEFInd();
                break;

            case TAG_LOADER:   // Boot OS via .EFI loader
                StartLoader((LOADER_ENTRY *)ChosenEntry);
                break;

            case TAG_LEGACY:   // Boot legacy OS
                StartLegacy((LEGACY_ENTRY *)ChosenEntry);
                break;

#ifdef __MAKEWITH_TIANO
            case TAG_LEGACY_UEFI: // Boot a legacy OS on a non-Mac
                StartLegacyUEFI((LEGACY_ENTRY *)ChosenEntry);
                break;
#endif

            case TAG_TOOL:     // Start a EFI tool
                StartTool((LOADER_ENTRY *)ChosenEntry);
                break;

            case TAG_EXIT:    // Terminate rEFInd
                if ((MokProtocol) && !SecureBootUninstall()) {
                   MainLoopRunning = FALSE;   // just in case we get this far
                } else {
                   BeginTextScreen(L" ");
                   return EFI_SUCCESS;
                }
                break;

        } // switch()
        MyFreePool(Selection);
        Selection = (ChosenEntry->Title) ? StrDuplicate(ChosenEntry->Title) : NULL;
    } // while()

    // If we end up here, things have gone wrong. Try to reboot, and if that
    // fails, go into an endless loop.
    refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
    EndlessIdleLoop();

    return EFI_SUCCESS;
} /* efi_main() */

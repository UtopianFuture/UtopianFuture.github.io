// #include <Uefi.h>
// // #include <stdio.h>
//
// EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
//                            IN EFI_SYSTEM_TABLE *SystemTable) {
//   SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello\n");
//   // printf("helloworld\n");
//
//   return EFI_SUCCESS;
// }
//

/** @file
    A simple, basic, EDK II native, "hello" application to verify that
    we can build applications without LibC.

    Copyright (c) 2010 - 2011, Intel Corporation. All rights reserved.<BR>
    SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/ShellCEntryLib.h>

/***
  Print a welcoming message.

  Establishes the main structure of the application.

  @retval  0         The application exited normally.
  @retval  Other     An error occurred.
***/
// INTN
// EFIAPI
// ShellAppMain (
//   IN UINTN Argc,
//   IN CHAR16 **Argv
//   )
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
                            IN EFI_SYSTEM_TABLE *SystemTable)
{
  Print(L"Hello there fellow Programmer.\n");
  Print(L"Welcome to the world of EDK II.\n");

  return(0);
}

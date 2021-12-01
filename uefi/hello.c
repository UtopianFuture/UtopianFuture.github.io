#include <efi.h>
#include <efilib.h>

efi_status efiapi efi_main(efi_handle imagehandle,
                           efi_system_table *systemtable) {
  initializelib(imagehandle, systemtable);
  print(l "hello, world!\n");

  return efi_success;
}

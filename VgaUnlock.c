#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Pci.h>

#ifndef PCI_BRIDGE_CONTROL_ISA
#define PCI_BRIDGE_CONTROL_ISA      0x0004
#endif
#ifndef PCI_BRIDGE_CONTROL_VGA
#define PCI_BRIDGE_CONTROL_VGA      0x0008
#endif
#ifndef PCI_BRIDGE_CONTROL_VGA_16
#define PCI_BRIDGE_CONTROL_VGA_16   0x0010
#endif

BOOLEAN
IsPciBridge (
  IN EFI_PCI_IO_PROTOCOL *PciIo
  )
{
  EFI_STATUS Status;
  UINT8      HeaderType;
  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_HEADER_TYPE_OFFSET, 1, &HeaderType);
  if (EFI_ERROR(Status)) return FALSE;
  return ((HeaderType & 0x7F) == HEADER_TYPE_PCI_TO_PCI_BRIDGE);
}

VOID
ForceCommandRegister (
  IN EFI_PCI_IO_PROTOCOL *PciIo,
  IN CHAR16              *Name
  )
{
  EFI_STATUS Status;
  UINT16     Cmd;
  UINT16     Original;
  
  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_COMMAND_OFFSET, 1, &Cmd);
  if (EFI_ERROR(Status)) return;
  Original = Cmd;

  Cmd |= (EFI_PCI_COMMAND_IO_SPACE | 
          EFI_PCI_COMMAND_MEMORY_SPACE | 
          EFI_PCI_COMMAND_BUS_MASTER | 
          EFI_PCI_COMMAND_VGA_PALETTE_SNOOP);

  if (Cmd != Original) {
    PciIo->Pci.Write (PciIo, EfiPciIoWidthUint16, PCI_COMMAND_OFFSET, 1, &Cmd);
    Print(L"    [%s] CMD: %04x -> %04x (IO+MEM+MAST+PAL)\n", Name, Original, Cmd);
  } else {
    Print(L"    [%s] CMD: %04x (Already OK)\n", Name, Cmd);
  }
}

VOID
ForceBridgeControl (
  IN EFI_PCI_IO_PROTOCOL *PciIo
  )
{
  EFI_STATUS Status;
  UINT16     Ctrl;
  UINT16     Original;

  if (!IsPciBridge(PciIo)) return;

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_BRIDGE_CONTROL_REGISTER_OFFSET, 1, &Ctrl);
  if (EFI_ERROR(Status)) return;
  Original = Ctrl;

  Ctrl |= (PCI_BRIDGE_CONTROL_ISA | 
           PCI_BRIDGE_CONTROL_VGA | 
           PCI_BRIDGE_CONTROL_VGA_16);

  if (Ctrl != Original) {
    PciIo->Pci.Write (PciIo, EfiPciIoWidthUint16, PCI_BRIDGE_CONTROL_REGISTER_OFFSET, 1, &Ctrl);
    Print(L"    [Bridge] CTRL: %04x -> %04x (ISA+VGA+VGA16)\n", Original, Ctrl);
  } else {
    Print(L"    [Bridge] CTRL: %04x (Already OK)\n", Original);
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                Status;
  UINTN                     HandleCount;
  EFI_HANDLE                *HandleBuffer;
  UINTN                     Index;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  PCI_TYPE00                PciHeader;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *ParentDevicePath;
  EFI_HANDLE                ParentHandle;
  EFI_PCI_IO_PROTOCOL       *ParentPciIo;

  Print(L"Scanning...\n");

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR (Status)) {
    Print(L"Error: No PCI handles found.\n");
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->OpenProtocol (
               HandleBuffer[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo,
               ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL
               );
    if (EFI_ERROR (Status)) continue;

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, sizeof (PciHeader) / sizeof (UINT32), &PciHeader);
    
    if (PciHeader.Hdr.ClassCode[2] == 0x03) {
      Print(L"Found GPU at Handle %x.\n", HandleBuffer[Index]);
      ForceCommandRegister(PciIo, L"GPU");

      Status = gBS->OpenProtocol(HandleBuffer[Index], &gEfiDevicePathProtocolGuid, (VOID**)&DevicePath, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (!EFI_ERROR(Status)) {
        ParentDevicePath = DuplicateDevicePath(DevicePath);
        
        while (TRUE) {
           EFI_DEVICE_PATH_PROTOCOL *Node = ParentDevicePath;
           while (!IsDevicePathEnd(NextDevicePathNode(Node))) {
                Node = NextDevicePathNode(Node);
           }
           if (Node == ParentDevicePath) break; 

           SetDevicePathEndNode(Node); 
           
           EFI_DEVICE_PATH_PROTOCOL *TempPath = ParentDevicePath;
           Status = gBS->LocateDevicePath(&gEfiPciIoProtocolGuid, &TempPath, &ParentHandle);
           if (EFI_ERROR(Status)) break;

           Status = gBS->OpenProtocol(
               ParentHandle, &gEfiPciIoProtocolGuid, (VOID **)&ParentPciIo,
               ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL
           );

           if (!EFI_ERROR(Status)) {
               ForceCommandRegister(ParentPciIo, L"Bridge");
               ForceBridgeControl(ParentPciIo);
           }
        }
        FreePool(ParentDevicePath);
      }
    }
  }

  if (HandleBuffer) FreePool (HandleBuffer);
  
  Print(L"VGA Resources Fully Unlocked. Booting...\n");
  gBS->Stall(2000000); 
  
  return EFI_SUCCESS;
}
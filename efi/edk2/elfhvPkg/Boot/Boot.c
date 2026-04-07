// Boot.c - elf hv bootloader scaffold (EDK2)
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Tcg2Protocol.h>
#include <Protocol/SimpleFileSystem.h>
#include "Boot.h"
#include "BootMenu.h"
#include "BootLog.h"

EFI_GUID gElfBootStatusGuid = { 0x4c7aaf1d, 0x8f1f, 0x4a0b, {0x9e,0x34,0x52,0x8e,0x21,0x46,0x12,0xa9} };

typedef enum {
  HV_STAGE_NONE = 0,
  HV_STAGE_DETECT,
  HV_STAGE_ENABLE,
  HV_STAGE_ALLOC,
  HV_STAGE_VMCS,
  HV_STAGE_CONTROLS,
  HV_STAGE_LAUNCH,
  HV_STAGE_RUNTIME
} HV_STAGE;

typedef struct {
  HV_STAGE Stage;
  EFI_STATUS Status;
  UINT32 Code;
} HV_INIT_RESULT;

#define HV_INIT_MENU_TIMEOUT_SECONDS 10
#define HV_INIT_MENU_DEFAULT_ACTION  L'c'

typedef struct {
  CONST CHAR16* Name;
  BOOLEAN Enabled;
  VOID (*Init)(THV_HANDOFF* Handoff, THV_CONFIG* Config);
} HV_FEATURE;

static VOID HvFeatureInitLog(THV_HANDOFF* Handoff, THV_CONFIG* Config, CONST CHAR16* Name) {
  (void)Handoff;
  (void)Config;
  BootLogAdd(L"[hv] feature init: %s (placeholder)", Name);
}

static VOID HvFeatureCpuid(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"cpuid monitor"); }
static VOID HvFeatureMsr(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"msr monitor"); }
static VOID HvFeatureCr(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"control register monitor"); }
static VOID HvFeatureEpt(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"ept hook engine"); }
static VOID HvFeatureMemTrace(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"memory tracer"); }
static VOID HvFeatureSyscall(THV_HANDOFF* Handoff, THV_CONFIG* Config) { HvFeatureInitLog(Handoff, Config, L"syscall monitor"); }

static UINTN HvRegisterFeatures(THV_HANDOFF* Handoff, THV_CONFIG* Config, HV_FEATURE* Out, UINTN Max) {
  if (!Out || Max == 0) return 0;
  UINTN count = 0;
  if (Config->HookCpuid && count < Max) Out[count++] = (HV_FEATURE){ L"cpuid monitor", TRUE, HvFeatureCpuid };
  if (Config->HookMsr && count < Max) Out[count++] = (HV_FEATURE){ L"msr monitor", TRUE, HvFeatureMsr };
  if (Config->EnableHooks && count < Max) Out[count++] = (HV_FEATURE){ L"control register monitor", TRUE, HvFeatureCr };
  if (Config->EnableHooks && count < Max) Out[count++] = (HV_FEATURE){ L"ept hook engine", TRUE, HvFeatureEpt };
  if (Config->EnableMemIntegrity && count < Max) Out[count++] = (HV_FEATURE){ L"memory tracer", TRUE, HvFeatureMemTrace };
  if (Config->HookSyscall && count < Max) Out[count++] = (HV_FEATURE){ L"syscall monitor", TRUE, HvFeatureSyscall };
  BootLogAdd(L"[hv] features registered: %u", (UINT32)count);
  for (UINTN i = 0; i < count; ++i) {
    BootLogAdd(L"[hv] feature: %s", Out[i].Name);
  }
  return count;
}

static VOID HvInitFeatures(THV_HANDOFF* Handoff, THV_CONFIG* Config) {
  HV_FEATURE features[16];
  UINTN count = HvRegisterFeatures(Handoff, Config, features, 16);
  for (UINTN i = 0; i < count; ++i) {
    if (features[i].Enabled && features[i].Init) {
      features[i].Init(Handoff, Config);
    }
  }
}

static EFI_STATUS HvInitSequence(THV_HANDOFF* Handoff, THV_CONFIG* Config, HV_INIT_RESULT* Out) {
  if (Out) {
    Out->Stage = HV_STAGE_NONE;
    Out->Status = EFI_SUCCESS;
    Out->Code = 0;
  }

  if (!Config->EnableHv) {
    BootLogAdd(L"[hv] hypervisor disabled by config");
    return EFI_SUCCESS;
  }

  BootLogAdd(L"[hv] stage 1: detect virtualization");
  if (!Handoff->Features.Vmx && !Handoff->Features.Svm) {
    if (Out) { Out->Stage = HV_STAGE_DETECT; Out->Status = EFI_UNSUPPORTED; Out->Code = 1; }
    BootLogAdd(L"[hv] stage failed: virtualization unsupported");
    return EFI_UNSUPPORTED;
  }

  BootLogAdd(L"[hv] stage 2: enable vmx/svm (placeholder)");
  if (Config->EnableVmx == 0 && Config->EnableSvm == 0) {
    if (Out) { Out->Stage = HV_STAGE_ENABLE; Out->Status = EFI_ABORTED; Out->Code = 2; }
    BootLogAdd(L"[hv] stage failed: vmx/svm disabled in config");
    return EFI_ABORTED;
  }

  BootLogAdd(L"[hv] stage 3: allocate vmx regions (placeholder)");
  VOID* vmx_region = AllocateZeroPool(4096);
  if (!vmx_region) {
    if (Out) { Out->Stage = HV_STAGE_ALLOC; Out->Status = EFI_OUT_OF_RESOURCES; Out->Code = 3; }
    BootLogAdd(L"[hv] stage failed: vmx region alloc");
    return EFI_OUT_OF_RESOURCES;
  }
  FreePool(vmx_region);

  BootLogAdd(L"[hv] stage 4: initialize vmcs (placeholder)");
  BootLogAdd(L"[hv] stage 5: configure vm exit controls (placeholder)");
  BootLogAdd(L"[hv] stage 6: launch hypervisor (placeholder)");
  BootLogAdd(L"[hv] stage 7: vmx root runtime (placeholder)");

  HvInitFeatures(Handoff, Config);
  BootLogAdd(L"[hv] hypervisor runtime active (scaffold)");
  return EFI_SUCCESS;
}

static VOID ShowInitFailure(const HV_INIT_RESULT* Res) {
  if (!Res) return;
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"hypervisor initialization failed\n\n");
  Print(L"error stage: %u\n", (UINT32)Res->Stage);
  Print(L"status code: 0x%08x\n\n", (UINT32)Res->Status);
  Print(L"options:\n");
  Print(L"  r - retry initialization\n");
  Print(L"  c - continue boot without hv\n");
  Print(L"  l - view log entries\n\n");
  Print(L"default action in %u seconds: continue without hv\n\n", (UINT32)HV_INIT_MENU_TIMEOUT_SECONDS);
}

static BOOLEAN WaitForInitChoice(CHAR16* OutChoice, UINTN TimeoutSeconds, CHAR16 DefaultChoice) {
  EFI_EVENT TimerEvent = NULL;
  EFI_EVENT Events[2];

  if (!OutChoice) return FALSE;
  *OutChoice = 0;
  if (EFI_ERROR(gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TimerEvent))) return FALSE;
  gBS->SetTimer(TimerEvent, TimerRelative, TimeoutSeconds * 10000000ull);
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;

  while (TRUE) {
    EFI_INPUT_KEY Key;
    UINTN Index = 0;
    gBS->WaitForEvent(2, Events, &Index);
    if (Index == 1) {
      *OutChoice = DefaultChoice;
      gBS->CloseEvent(TimerEvent);
      return TRUE;
    }
    if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) continue;
    if (Key.UnicodeChar == L'r' || Key.UnicodeChar == L'c' || Key.UnicodeChar == L'l') {
      *OutChoice = Key.UnicodeChar;
      gBS->CloseEvent(TimerEvent);
      return TRUE;
    }
  }
}

static VOID ShowLogScreen(VOID) {
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"hypervisor boot log\n");
  Print(L"-------------------\n\n");
  CONST BOOT_LOG_LINE* lines = BootLogGetLines();
  UINTN count = BootLogCount();
  if (!lines || count == 0) {
    Print(L"(no log entries)\n");
  } else {
    for (UINTN i = 0; i < count; ++i) {
      Print(L"%s\n", lines[i].Text);
    }
  }
  Print(L"\npress any key to return...\n");
  EFI_INPUT_KEY Key;
  UINTN Index = 0;
  gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
}

static VOID ShowSplashLogo(VOID) {
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"███████╗██╗     ███████╗\n");
  Print(L"██╔════╝██║     ██╔════╝\n");
  Print(L"█████╗  ██║     █████╗  \n");
  Print(L"██╔══╝  ██║     ██╔══╝  \n");
  Print(L"███████╗███████╗██║     \n");
  Print(L"╚══════╝╚══════╝╚═╝     \n");
  Print(L"\nelf hypervisor loader\n\n");
  gBS->Stall(650000);
}

static UINT64 HvPackTimestamp(VOID) {
  EFI_TIME Time;
  if (EFI_ERROR(gRT->GetTime(&Time, NULL))) return 0;
  // pack as yyyymmddhhmmss
  return ((UINT64)Time.Year * 10000000000ULL) +
         ((UINT64)Time.Month * 100000000ULL) +
         ((UINT64)Time.Day * 1000000ULL) +
         ((UINT64)Time.Hour * 10000ULL) +
         ((UINT64)Time.Minute * 100ULL) +
         (UINT64)Time.Second;
}

static EFI_STATUS WriteBootStatusVariable(const ELF_BOOT_STATUS* Status) {
  if (!Status) return EFI_INVALID_PARAMETER;
  UINT32 Attr = EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS;
  return gRT->SetVariable(L"elf_boot_status", &gElfBootStatusGuid, Attr, sizeof(*Status), (VOID*)Status);
}


STATIC
EFI_STATUS
WriteBootedVariable(void) {
  UINT8 value = 1;
  UINT32 Attr = EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS;
  return gRT->SetVariable(L"elfhvBooted", &gThvHandoffGuid, Attr, sizeof(value), &value);
}

STATIC
EFI_STATUS
LoadAndStartImage(EFI_HANDLE ImageHandle, EFI_HANDLE* OutHandle, CONST CHAR16* Path) {
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Sfsp;
  EFI_FILE_PROTOCOL* Root;
  EFI_FILE_PROTOCOL* File;
  EFI_DEVICE_PATH_PROTOCOL* DevicePath;

  BootLogAdd(L"[boot] chainload locate: %s", Path);
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
  if (EFI_ERROR(Status)) { BootLogAdd(L"[boot] chainload failed: loaded image %r", Status); return Status; }

  Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Sfsp);
  if (EFI_ERROR(Status)) { BootLogAdd(L"[boot] chainload failed: fs protocol %r", Status); return Status; }

  Status = Sfsp->OpenVolume(Sfsp, &Root);
  if (EFI_ERROR(Status)) { BootLogAdd(L"[boot] chainload failed: open volume %r", Status); return Status; }

  Status = Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    BootLogAdd(L"[boot] chainload failed: open file %r", Status);
    return Status;
  }

  // change the path of the IMAGE_BASE to the hander exit module
  // use DeviceHandle to another path as the boot manager will not send
  DevicePath = FileDevicePath(LoadedImage->DeviceHandle, (CHAR16*)Path);
  File->Close(File);
  Root->Close(Root);
  if (!DevicePath) { BootLogAdd(L"[boot] chainload failed: device path not found"); return EFI_NOT_FOUND; }

  BootLogAdd(L"[boot] chainload load image");
  Status = gBS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, OutHandle);
  if (EFI_ERROR(Status)) { BootLogAdd(L"[boot] chainload failed: load image %r", Status); return Status; }

  EFI_LOADED_IMAGE_PROTOCOL* ChildImage = NULL;
  if (!EFI_ERROR(gBS->HandleProtocol(*OutHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&ChildImage)) && ChildImage) {
    Print(L"\nnext image\n");
    Print(L"path         : %s\n", Path);
    Print(L"image base   : %p\n", ChildImage->ImageBase);
    Print(L"image size   : 0x%lx\n", (UINT64)ChildImage->ImageSize);
    Print(L"device       : %p\n", ChildImage->DeviceHandle);
  }

  BootLogAdd(L"[boot] chainload start image");
  Status = gBS->StartImage(*OutHandle, NULL, NULL);
  if (EFI_ERROR(Status)) { BootLogAdd(L"[boot] chainload failed: start image %r", Status); return Status; }
  BootLogAdd(L"[boot] chainload transfer complete");
  return Status;
}


EFI_STATUS
MeasureBufferTpm(IN EFI_HANDLE ImageHandle, IN CONST CHAR16* EventDesc, IN CONST VOID* Buffer, IN UINTN BufferSize) {
  EFI_TCG2_PROTOCOL* Tcg2 = NULL;
  EFI_STATUS Status = gBS->LocateProtocol(&gEfiTcg2ProtocolGuid, NULL, (VOID**)&Tcg2);
  if (EFI_ERROR(Status) || !Tcg2) return EFI_UNSUPPORTED;
  if (!Buffer || BufferSize == 0) return EFI_INVALID_PARAMETER;

  UINTN descSize = (EventDesc ? StrSize(EventDesc) : sizeof(CHAR16));
  UINTN eventSize = sizeof(EFI_TCG2_EVENT_HEADER) + descSize;
  EFI_TCG2_EVENT* Event = AllocateZeroPool(eventSize);
  if (!Event) return EFI_OUT_OF_RESOURCES;

  Event->Header.HeaderSize = sizeof(EFI_TCG2_EVENT_HEADER);
  Event->Header.HeaderVersion = EFI_TCG2_EVENT_HEADER_VERSION;
  Event->Header.PCRIndex = 7;
  Event->Header.EventType = EV_EFI_ACTION;
  Event->Size = (UINT32)descSize;
  if (EventDesc) {
    CopyMem(Event->Event, EventDesc, descSize);
  } else {
    ((CHAR16*)Event->Event)[0] = L'\0';
  }

  Status = Tcg2->HashLogExtendEvent(
      Tcg2,
      0,
      (EFI_PHYSICAL_ADDRESS)(UINTN)Buffer,
      BufferSize,
      Event);

  FreePool(Event);
  return Status;
}


EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
  (void)SystemTable;
  BootLogInit();
  ShowSplashLogo();

  THV_HANDOFF* Handoff = NULL;
  EFI_STATUS Status = CreateHandoff(&Handoff);
  if (EFI_ERROR(Status)) {
    BootLogAdd(L"[boot] handoff create failed: %r", Status);
    Print(L"handoff create failed: %r\n", Status);
    return Status;
  }

  BootLogAdd(L"[boot] handoff created");
  BOOLEAN config_loaded = !EFI_ERROR(LoadConfigFile(ImageHandle, &Handoff->Config));

  if (!EFI_ERROR(RunConfigMenu(ImageHandle, Handoff))) {
    BootLogAdd(L"[boot] config commit requested");
    WriteConfigVariable(&Handoff->Config);
    RecalcHandoffCrc(Handoff);
    BootLogAdd(L"[boot] config saved");
  }

  HV_INIT_RESULT init = {0};
  BOOLEAN hv_initialized = FALSE;
  while (TRUE) {
    Status = HvInitSequence(Handoff, &Handoff->Config, &init);
    if (!EFI_ERROR(Status)) { hv_initialized = TRUE; break; }
    BootLogAdd(L"[boot] hv init failed: stage=%u status=%r", (UINT32)init.Stage, Status);
    if (!Handoff->Config.Verbose && !Handoff->Config.EnableHv) {
      break;
    }
    ShowInitFailure(&init);
    CHAR16 choice = 0;
    if (!WaitForInitChoice(&choice, HV_INIT_MENU_TIMEOUT_SECONDS, HV_INIT_MENU_DEFAULT_ACTION)) break;
    if (choice == L'l') {
      ShowLogScreen();
      ShowInitFailure(&init);
      continue;
    }
    if (choice == L'c') {
      Handoff->Config.EnableHv = 0;
      BootLogAdd(L"[boot] continue without hv");
      break;
    }
    if (choice == L'r') {
      BootLogAdd(L"[boot] retry hv init");
      continue;
    }
  }

  {
    ELF_BOOT_STATUS bs;
    ZeroMem(&bs, sizeof(bs));
    bs.BootTimestamp = HvPackTimestamp();
    bs.HvEnabled = (Handoff->Config.EnableHv != 0);
    bs.HvInitialized = (hv_initialized ? TRUE : FALSE);
    bs.VirtualizationSupported = (Handoff->Features.Vmx || Handoff->Features.Svm);
    bs.EptSupported = (Handoff->Features.Ept || Handoff->Features.Npt);
    bs.ConfigLoaded = (config_loaded ? TRUE : FALSE);
    bs.HvVersion = 1;
    EFI_STATUS bs_st = WriteBootStatusVariable(&bs);
    if (EFI_ERROR(bs_st)) {
      BootLogAdd(L"[boot] boot status variable write failed: %r", bs_st);
    } else {
      BootLogAdd(L"[boot] boot status variable written");
    }
  }

  Status = WriteHandoffVariable(Handoff);
  if (EFI_ERROR(Status)) {
    BootLogAdd(L"[boot] handoff variable write failed: %r", Status);
    Print(L"handoff variable write failed: %r\n", Status);
  }
  Status = WriteBootedVariable();
  if (EFI_ERROR(Status)) {
    BootLogAdd(L"[boot] booted variable write failed: %r", Status);
    Print(L"booted variable write failed: %r\n", Status);
  }
  BootLogAdd(L"[boot] boot transition pending (scaffold)");
  MeasureBufferTpm(ImageHandle, L"elfhv:Handoff", Handoff, sizeof(*Handoff));

  EFI_HANDLE NextImage = NULL;
  Status = LoadAndStartImage(ImageHandle, &NextImage, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
  if (EFI_ERROR(Status)) {
    BootLogAdd(L"[boot] chainload fallback: bootmgfw failed");
    Status = LoadAndStartImage(ImageHandle, &NextImage, L"\\EFI\\Boot\\bootx64.efi");
  }

  if (EFI_ERROR(Status)) {
    Print(L"boot chainload failed: %r\n", Status);
  }
  return Status;
}

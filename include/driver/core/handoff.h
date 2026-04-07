// handoff.h - efi handoff structures
#pragma once
#include <ntddk.h>

typedef struct thv_features_t {
    UCHAR Vmx;
    UCHAR Ept;
    UCHAR Vpid;
    UCHAR Svm;
    UCHAR Npt;
} thv_features_t;

typedef struct thv_config_t {
    UCHAR EnableVmx;
    UCHAR EnableSvm;
    UCHAR EnableEpt;
    UCHAR EnableNpt;
    UCHAR EnableVpid;
    UCHAR Verbose;
} thv_config_t;

typedef struct thv_handoff_t {
    UINT32 Signature;
    UINT32 Version;
    UINT32 Size;
    UINT32 Crc32;
    CHAR Vendor[13];
    thv_features_t Features;
    thv_config_t Config;
    UINT64 AcpiRsdpAddress;
    UINT64 AcpiRsdtAddress;
    UINT64 AcpiXsdtAddress;
    UCHAR AcpiRevision;
    UCHAR AcpiReserved[7];
} thv_handoff_t;

#define THV_HANDOFF_SIGNATURE 0x56484F54u
#define THV_HANDOFF_VERSION   0x0002

extern const GUID g_thv_handoff_guid;
NTSTATUS hv_load_efi_handoff(thv_handoff_t* out);
UINT64 hv_hash64(const VOID* data, SIZE_T size, UINT64 seed);
UINT64 hv_handoff_integrity_hash(const thv_handoff_t* handoff, const thv_config_t* config);

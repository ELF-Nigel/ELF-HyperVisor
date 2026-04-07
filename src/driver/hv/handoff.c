// handoff.c - efi handoff reader
#include "driver/core/handoff.h"

NTSYSAPI NTSTATUS NTAPI ZwQuerySystemEnvironmentValueEx(
    PUNICODE_STRING VariableName,
    LPGUID VendorGuid,
    PVOID Value,
    PULONG ValueLength,
    PULONG Attributes);

const GUID g_thv_handoff_guid = { 0x3f7a9fe2, 0x0e84, 0x4de4, {0x86,0x92,0xa2,0x9f,0x31,0x66,0x6a,0xb5} };

static UINT32 hv_crc32(const VOID* data, SIZE_T size) {
    UINT32 crc = 0xFFFFFFFF;
    const UINT8* p = (const UINT8*)data;
    for (SIZE_T i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            UINT32 mask = (UINT32)-(INT32)(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

UINT64 hv_hash64(const VOID* data, SIZE_T size, UINT64 seed) {
    const UINT8* p = (const UINT8*)data;
    UINT64 h = 1469598103934665603ull ^ seed;
    for (SIZE_T i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

UINT64 hv_handoff_integrity_hash(const thv_handoff_t* handoff, const thv_config_t* config) {
    UINT64 h;
    if (!handoff || !config) return 0;
    h = hv_hash64(handoff, sizeof(*handoff), 0x48414e444f464655ull);
    return hv_hash64(config, sizeof(*config), h);
}

NTSTATUS hv_load_efi_handoff(thv_handoff_t* out) {
    if (!out) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(out, sizeof(*out));

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"elfhvHandoff");

    ULONG attrs = 0;
    ULONG size = sizeof(*out);
    NTSTATUS st = ZwQuerySystemEnvironmentValueEx(&name, (LPGUID)&g_thv_handoff_guid, out, &size, &attrs);
    if (!NT_SUCCESS(st)) return st;

    if (out->Signature != THV_HANDOFF_SIGNATURE || out->Version != THV_HANDOFF_VERSION)
        return STATUS_INVALID_IMAGE_FORMAT;
    if (out->Size != sizeof(thv_handoff_t)) return STATUS_INFO_LENGTH_MISMATCH;
    {
        UINT32 saved = out->Crc32;
        out->Crc32 = 0;
        UINT32 calc = hv_crc32(out, sizeof(thv_handoff_t));
        out->Crc32 = saved;
        if (calc != saved) return STATUS_CRC_ERROR;
    }
    return STATUS_SUCCESS;
}

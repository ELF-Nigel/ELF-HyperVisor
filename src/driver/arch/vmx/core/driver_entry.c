// driver_entry.c - wdm skeleton (no ioctl path)
#include <ntddk.h>
#include "driver/core/hv.h"
#include "driver/control/control.h"

static hv_state_t g_hv;

DRIVER_UNLOAD DriverUnload;

_Use_decl_annotations_
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    hv_shutdown(&g_hv);
    hvctl_detach_driver();
}

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&g_hv, sizeof(g_hv));
    DriverObject->DriverUnload = DriverUnload;

    NTSTATUS st = hvctl_attach_driver(DriverObject);
    if (!NT_SUCCESS(st)) return st;

    st = hv_init(&g_hv);
    if (!NT_SUCCESS(st)) {
        hvctl_detach_driver();
        return st;
    }
    hvctl_set_backend(&g_hv);

    st = hv_start(&g_hv);
    if (!NT_SUCCESS(st)) {
        hv_shutdown(&g_hv);
        hvctl_detach_driver();
        return st;
    }
    return STATUS_SUCCESS;
}

#include "driver/core/page_audit.h"
#include "driver/util/log.h"

static const uint64_t* hv_audit_map(void* context, uint64_t physical_address) {
    PHYSICAL_ADDRESS address;
    UNREFERENCED_PARAMETER(context);
    address.QuadPart = (LONGLONG)physical_address;
    return (const uint64_t*)MmGetVirtualForPhysical(address);
}

int hv_page_audit_query(ULONG architecture, const void* root, UINT64 gpa, hv_page_audit_t* report) {
    return hv_page_audit_walk(architecture, (const uint64_t*)root, gpa, hv_audit_map, NULL, report);
}

#if DBG
static void hv_audit_log(void* context, const hv_page_audit_t* report) {
    UNREFERENCED_PARAMETER(context);
    hv_log("[page-audit] arch=%lu gpa=0x%llx size=0x%llx present=%lu rwx=%lu flags=0x%lx\n",
        (ULONG)report->architecture, report->gpa, report->page_size,
        (ULONG)report->present, (ULONG)report->permissions, (ULONG)report->flags);
}

void hv_debug_audit_page(ULONG architecture, const void* root, UINT64 gpa) {
    hv_page_audit_t report;
    if (hv_page_audit_query(architecture, root, gpa, &report))
        hv_page_audit_emit(&report, hv_audit_log, NULL);
    else hv_log("[page-audit] table walk failed arch=%lu gpa=0x%llx\n", architecture, gpa);
}
#endif

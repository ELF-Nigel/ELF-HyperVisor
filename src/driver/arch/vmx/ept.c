// ept.c - ept skeleton (annotated)
#include "driver/arch/ept.h"
#include "driver/util/alloc.h"
#include "driver/core/page_audit.h"

int ept_init(ept_state_t* ept) {
    if (!ept) return STATUS_INVALID_PARAMETER;
    ept->pml4 = hv_alloc_page_aligned(PAGE_SIZE, 'tpeE');
    if (!ept->pml4) return STATUS_INSUFFICIENT_RESOURCES;
    if (!hv_is_page_aligned(ept->pml4)) {
        hv_free_page_aligned(ept->pml4, 'tpeE');
        ept->pml4 = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ept->pml4, PAGE_SIZE);
    ept->pml4_pa = MmGetPhysicalAddress(ept->pml4);
    if (ept->pml4_pa.QuadPart == 0 || !hv_is_phys_page_aligned(ept->pml4_pa)) {
        hv_free_page_aligned(ept->pml4, 'tpeE');
        ept->pml4 = NULL;
        return STATUS_UNSUCCESSFUL;
    }
    // base outline: allocate pdpt/pd/pt pages, identity map low physical ranges,
    // set eptp (memory type + page-walk length), then write eptp into vmcs.
    return STATUS_SUCCESS;
}

void ept_shutdown(ept_state_t* ept) {
    if (!ept) return;
    if (ept->pml4) hv_free_page_aligned(ept->pml4, 'tpeE');
    ept->pml4 = NULL;
}

int ept_set_rw(ept_state_t* ept, UINT64 gpa, int read, int write) {
    if (!ept || !ept->pml4) return STATUS_INVALID_PARAMETER;
    if (gpa == 0) return STATUS_INVALID_PARAMETER;
    hv_debug_audit_page(HV_EXIT_VMX, ept->pml4, gpa);
    // layout:
    // 1) locate pml4/pdpt/pd/pt entry for gpa
    // 2) set read/write bits based on input
    // 3) flush with invept single/all
    UNREFERENCED_PARAMETER(read);
    UNREFERENCED_PARAMETER(write);
    return STATUS_NOT_IMPLEMENTED;
}

int ept_set_exec(ept_state_t* ept, UINT64 gpa, int exec) {
    if (!ept || !ept->pml4) return STATUS_INVALID_PARAMETER;
    if (gpa == 0) return STATUS_INVALID_PARAMETER;
    hv_debug_audit_page(HV_EXIT_VMX, ept->pml4, gpa);
    // layout:
    // 1) locate pml4/pdpt/pd/pt entry for gpa
    // 2) set/clear execute bit
    // 3) flush with invept single/all
    UNREFERENCED_PARAMETER(exec);
    return STATUS_NOT_IMPLEMENTED;
}

int ept_map_page(ept_state_t* ept, UINT64 gpa, UINT64 hpa, int r, int w, int x) {
    if (!ept || !ept->pml4) return STATUS_INVALID_PARAMETER;
    if (gpa == 0 || hpa == 0) return STATUS_INVALID_PARAMETER;
    // layout:
    // 1) walk ept to locate the entry for gpa
    // 2) set pfn = hpa >> 12
    // 3) set r/w/x permissions
    // 4) flush with invept single/all
    UNREFERENCED_PARAMETER(r);
    UNREFERENCED_PARAMETER(w);
    UNREFERENCED_PARAMETER(x);
    return STATUS_NOT_IMPLEMENTED;
}

int ept_unmap_page(ept_state_t* ept, UINT64 gpa) {
    if (!ept || !ept->pml4) return STATUS_INVALID_PARAMETER;
    if (gpa == 0) return STATUS_INVALID_PARAMETER;
    // layout:
    // 1) locate ept entry for gpa
    // 2) clear valid/rwx bits
    // 3) flush with invept single/all
    return STATUS_NOT_IMPLEMENTED;
}

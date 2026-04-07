// npt.c - nested page tables helpers (skeleton)
#include "driver/arch/npt.h"
#include "driver/arch/svm.h"
#include "driver/util/alloc.h"

static int svm_npt_alloc_table_page(void** out, ULONG tag) {
    *out = hv_alloc_page_aligned(PAGE_SIZE, tag);
    if (!*out) return 0;
    if (!hv_is_page_aligned(*out)) {
        hv_free_page_aligned(*out, tag);
        *out = NULL;
        return 0;
    }
    RtlZeroMemory(*out, PAGE_SIZE);
    return 1;
}

USHORT svm_get_npt_index(svm_paging_struct_t level, UINT64 gva) {
    switch (level) {
        case PS_PML4: return (USHORT)hv_pml4_index(gva);
        case PS_PDP: return (USHORT)hv_pdpt_index(gva);
        case PS_PDT: return (USHORT)hv_pd_index(gva);
        case PS_PTE: return (USHORT)hv_pt_index(gva);
        default: return 0;
    }
}

static int svm_npt_range_covers_pfn(svm_pt_range_t* r, UINT64 pfn) {
    return r && (pfn >= r->start_pfn) && (pfn <= r->end_pfn);
}

static int svm_npt_ensure_pt(svm_npt_tables_t* npt, UINT64 pdp_idx, UINT64 pd_idx) {
    if (npt->pt[pdp_idx][pd_idx]) return 1;
    void* page = NULL;
    if (!svm_npt_alloc_table_page(&page, 'tpeN')) return 0;
    npt->pt[pdp_idx][pd_idx] = (PPT_Entry)page;
    return 1;
}

static void svm_npt_free_pt_ranges(svm_npt_tables_t* npt) {
    if (!npt) return;
    for (ULONG r = 0; r < npt->pt_range_count; ++r) {
        if (npt->pt_ranges[r].pt) {
            hv_free_page_aligned(npt->pt_ranges[r].pt, 'tpeN');
            npt->pt_ranges[r].pt = NULL;
            npt->pt_ranges[r].num_ptes = 0;
        }
    }
    npt->pt_range_count = 0;
}

_Use_decl_annotations_
int svm_npt_setup_ranges(svm_npt_tables_t* npt) {
    if (!npt) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    npt->pt_range_count = 0;

    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRangesEx2(NULL, 0);
    if (!ranges) return STATUS_UNSUCCESSFUL;

    for (ULONG i = 0; ranges[i].BaseAddress.QuadPart || ranges[i].NumberOfBytes.QuadPart; ++i) {
        if (npt->pt_range_count >= 128) break;
        UINT64 start_pfn = (UINT64)(ranges[i].BaseAddress.QuadPart >> PAGE_SHIFT);
        UINT64 end_pfn = (UINT64)((ranges[i].BaseAddress.QuadPart + ranges[i].NumberOfBytes.QuadPart - 1) >> PAGE_SHIFT);
        npt->pt_ranges[npt->pt_range_count].start_pfn = start_pfn;
        npt->pt_ranges[npt->pt_range_count].end_pfn = end_pfn;
        npt->pt_ranges[npt->pt_range_count].pt = NULL; // allocated per-need
        npt->pt_range_count++;
    }

    ExFreePoolWithTag(ranges, 0);
    return (npt->pt_range_count > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

_Use_decl_annotations_
int svm_npt_allocate_pts_for_ranges(svm_npt_tables_t* npt) {
    if (!npt) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;

    svm_npt_free_pt_ranges(npt);

    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRangesEx2(NULL, 0);
    if (!ranges) return STATUS_UNSUCCESSFUL;

    INT64 allocations = 0;
    for (ULONG i = 0; ranges[i].BaseAddress.QuadPart || ranges[i].NumberOfBytes.QuadPart; ++i) {
        if (allocations >= 128) { ExFreePoolWithTag(ranges, 0); return STATUS_INSUFFICIENT_RESOURCES; }

        UINT64 range_base = (UINT64)ranges[i].BaseAddress.QuadPart;
        UINT64 ptes_in_range = (UINT64)ranges[i].NumberOfBytes.QuadPart / PAGE_SIZE;
        if (ptes_in_range == 0) continue;

        if (ptes_in_range % 512 != 0) {
            ptes_in_range = ((ptes_in_range + 512) / 512) * 512;
        }

        SIZE_T bytes = (SIZE_T)(ptes_in_range * sizeof(PT_Entry));
        PPT_Entry pte_alloc = (PPT_Entry)hv_alloc_page_aligned(bytes, 'tpeN');
        if (!pte_alloc) { ExFreePoolWithTag(ranges, 0); return STATUS_INSUFFICIENT_RESOURCES; }
        RtlZeroMemory(pte_alloc, bytes);

        npt->pt_ranges[allocations].start_pfn = (range_base >> PAGE_SHIFT);
        npt->pt_ranges[allocations].end_pfn = (range_base >> PAGE_SHIFT) + ptes_in_range - 1;
        npt->pt_ranges[allocations].pt = pte_alloc;
        npt->pt_ranges[allocations].num_ptes = ptes_in_range;
        allocations++;

        USHORT curr_pdpe = svm_get_npt_index(PS_PDP, range_base);
        USHORT curr_pde = svm_get_npt_index(PS_PDT, range_base);
        UINT64 curr_pte_idx = 0;
        while (curr_pte_idx < ptes_in_range) {
            if (curr_pdpe > 511) break;
            npt->pt[curr_pdpe][curr_pde] = &pte_alloc[curr_pte_idx];
            curr_pte_idx += 512;
            curr_pde++;
            if (curr_pde > 511) {
                curr_pdpe += 1;
                curr_pde = 0;
            }
        }
    }

    npt->pt_range_count = (ULONG)allocations;
    ExFreePoolWithTag(ranges, 0);
    return (allocations > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

int svm_npt_build_tables(svm_npt_tables_t* npt) {
    if (!npt || !npt->pml4 || !npt->pdp) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;

    npt->pml4_pa = MmGetPhysicalAddress(npt->pml4);
    npt->pdp_pa = MmGetPhysicalAddress(npt->pdp);
    if (npt->pml4_pa.QuadPart == 0 || npt->pdp_pa.QuadPart == 0) return STATUS_UNSUCCESSFUL;

    npt->pml4[0].Fields.Valid = 1;
    npt->pml4[0].Fields.Write = 1;
    npt->pml4[0].Fields.User = 1;
    npt->pml4[0].Fields.PageFrameNumber = (UINT64)(npt->pdp_pa.QuadPart >> PAGE_SHIFT);

    for (UINT64 pdp_idx = 0; pdp_idx < 512; ++pdp_idx) {
        if (!npt->pd[pdp_idx]) continue;
        PHYSICAL_ADDRESS pd_pa = MmGetPhysicalAddress(npt->pd[pdp_idx]);
        if (pd_pa.QuadPart == 0) continue;
        npt->pdp[pdp_idx].Fields_Sub1GB.Valid = 1;
        npt->pdp[pdp_idx].Fields_Sub1GB.Write = 1;
        npt->pdp[pdp_idx].Fields_Sub1GB.User = 1;
        npt->pdp[pdp_idx].Fields_Sub1GB.PageFrameNumber = (UINT64)(pd_pa.QuadPart >> PAGE_SHIFT);

        for (UINT64 pd_idx = 0; pd_idx < 512; ++pd_idx) {
            UINT64 pfn_base = (pdp_idx << 18) | (pd_idx << 9);
            int needs_pt = 0;
            for (ULONG r = 0; r < npt->pt_range_count; ++r) {
                if (svm_npt_range_covers_pfn(&npt->pt_ranges[r], pfn_base)) {
                    needs_pt = 1;
                    break;
                }
            }

            if (!needs_pt || !npt->pt[pdp_idx][pd_idx]) {
                npt->pd[pdp_idx][pd_idx].Fields_2MB.Valid = 1;
                npt->pd[pdp_idx][pd_idx].Fields_2MB.Write = 1;
                npt->pd[pdp_idx][pd_idx].Fields_2MB.User = 1;
                npt->pd[pdp_idx][pd_idx].Fields_2MB.LargePage = 1;
                npt->pd[pdp_idx][pd_idx].Fields_2MB.PageFrameNumber = pfn_base;
                continue;
            }

            PHYSICAL_ADDRESS pt_pa = MmGetPhysicalAddress(npt->pt[pdp_idx][pd_idx]);
            if (pt_pa.QuadPart == 0) continue;
            npt->pd[pdp_idx][pd_idx].Fields_4KB.Valid = 1;
            npt->pd[pdp_idx][pd_idx].Fields_4KB.Write = 1;
            npt->pd[pdp_idx][pd_idx].Fields_4KB.User = 1;
            npt->pd[pdp_idx][pd_idx].Fields_4KB.PageFrameNumber = (UINT64)(pt_pa.QuadPart >> PAGE_SHIFT);

            for (UINT64 pte_idx = 0; pte_idx < 512; ++pte_idx) {
                npt->pt[pdp_idx][pd_idx][pte_idx].Fields.Valid = 1;
                npt->pt[pdp_idx][pd_idx][pte_idx].Fields.Write = 1;
                npt->pt[pdp_idx][pd_idx][pte_idx].Fields.User = 1;
                npt->pt[pdp_idx][pd_idx][pte_idx].Fields.PageFrameNumber =
                    (pfn_base + pte_idx);
            }
        }
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
int svm_npt_sync_physical_ranges(svm_npt_tables_t* npt) {
    if (!npt) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;

    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRangesEx2(NULL, 0);
    if (!ranges) return STATUS_UNSUCCESSFUL;

    for (ULONG i = 0; ranges[i].BaseAddress.QuadPart || ranges[i].NumberOfBytes.QuadPart; ++i) {
        UINT64 start = (UINT64)ranges[i].BaseAddress.QuadPart;
        UINT64 end = start + (UINT64)ranges[i].NumberOfBytes.QuadPart;
        for (UINT64 addr = start; addr < end; addr += PAGE_SIZE) {
            PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, addr);
            if (!pte) continue;
            pte->Fields.Valid = 1;
            pte->Fields.Write = 1;
            pte->Fields.User = 1;
            pte->Fields.PageFrameNumber = (addr >> PAGE_SHIFT);
        }
    }

    ExFreePoolWithTag(ranges, 0);
    return STATUS_SUCCESS;
}

int svm_npt_init(svm_npt_tables_t* npt) {
    if (!npt) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(npt, sizeof(*npt));

    if (!svm_npt_alloc_table_page((void**)&npt->pml4, '4lmN')) return STATUS_INSUFFICIENT_RESOURCES;
    if (!svm_npt_alloc_table_page((void**)&npt->pdp, 'pdpN')) return STATUS_INSUFFICIENT_RESOURCES;

    for (UINT64 i = 0; i < 512; ++i) {
        if (!svm_npt_alloc_table_page((void**)&npt->pd[i], 'dpeN')) return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (svm_npt_setup_ranges(npt) != STATUS_SUCCESS) return STATUS_UNSUCCESSFUL;
    if (svm_npt_allocate_pts_for_ranges(npt) != STATUS_SUCCESS) return STATUS_INSUFFICIENT_RESOURCES;
    if (svm_npt_build_tables(npt) != STATUS_SUCCESS) return STATUS_UNSUCCESSFUL;
    return svm_npt_sync_physical_ranges(npt);
}

void svm_npt_shutdown(svm_npt_tables_t* npt) {
    if (!npt) return;
    svm_npt_free_pt_ranges(npt);
    for (UINT64 i = 0; i < 512; ++i) {
        if (npt->pd[i]) hv_free_page_aligned(npt->pd[i], 'dpeN');
    }
    if (npt->pdp) hv_free_page_aligned(npt->pdp, 'pdpN');
    if (npt->pml4) hv_free_page_aligned(npt->pml4, '4lmN');
    RtlZeroMemory(npt, sizeof(*npt));
}

PPT_Entry svm_get_npt_pte_for_gpa(svm_npt_tables_t* npt, UINT64 gpa) {
    if (!npt) return NULL;
    UINT64 pdp_idx = svm_get_npt_index(PS_PDP, gpa);
    UINT64 pd_idx = svm_get_npt_index(PS_PDT, gpa);
    PPT_Entry pt = npt->pt[pdp_idx][pd_idx];
    if (!pt) return NULL;
    return &pt[svm_get_npt_index(PS_PTE, gpa)];
}

UINT64 svm_get_hpa_for_gpa(svm_npt_tables_t* npt, UINT64 gpa) {
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte || !pte->Fields.Valid) return 0;
    return (pte->Fields.PageFrameNumber << PAGE_SHIFT);
}

void* svm_get_hva_for_gpa(svm_npt_tables_t* npt, UINT64 gpa) {
    UINT64 hpa = svm_get_hpa_for_gpa(npt, gpa);
    if (!hpa) return NULL;
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (LONGLONG)hpa;
    return MmGetVirtualForPhysical(pa);
}

PPD_Entry svm_get_npt_pde_for_gpa(svm_npt_tables_t* npt, UINT64 gpa) {
    if (!npt || !npt->pd[svm_get_npt_index(PS_PDP, gpa)]) return NULL;
    return &npt->pd[svm_get_npt_index(PS_PDP, gpa)][svm_get_npt_index(PS_PDT, gpa)];
}

PPDP_Entry svm_get_npt_pdp_for_gpa(svm_npt_tables_t* npt, UINT64 gpa) {
    if (!npt || !npt->pdp) return NULL;
    return &npt->pdp[svm_get_npt_index(PS_PDP, gpa)];
}

int svm_npt_set_rw(svm_npt_tables_t* npt, UINT64 gpa, int read, int write) {
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte) return STATUS_UNSUCCESSFUL;
    pte->Fields.Valid = (read || write) ? 1 : 0;
    pte->Fields.Write = (write != 0);
    svm_invlpga(gpa, 0);
    return STATUS_SUCCESS;
}

int svm_npt_set_exec(svm_npt_tables_t* npt, UINT64 gpa, int exec) {
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte) return STATUS_UNSUCCESSFUL;
    pte->Fields.NoExecute = exec ? 0 : 1;
    svm_invlpga(gpa, 0);
    return STATUS_SUCCESS;
}

int svm_npt_get_rw(svm_npt_tables_t* npt, UINT64 gpa, int* read, int* write) {
    if (!read || !write) return STATUS_INVALID_PARAMETER;
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte) return STATUS_UNSUCCESSFUL;
    *read = pte->Fields.Valid ? 1 : 0;
    *write = pte->Fields.Write ? 1 : 0;
    return STATUS_SUCCESS;
}

int svm_npt_get_exec(svm_npt_tables_t* npt, UINT64 gpa, int* exec) {
    if (!exec) return STATUS_INVALID_PARAMETER;
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte) return STATUS_UNSUCCESSFUL;
    *exec = pte->Fields.NoExecute ? 0 : 1;
    return STATUS_SUCCESS;
}

int svm_npt_is_mapped(svm_npt_tables_t* npt, UINT64 gpa) {
    PPT_Entry pte = svm_get_npt_pte_for_gpa(npt, gpa);
    if (!pte) return 0;
    return pte->Fields.Valid ? 1 : 0;
}

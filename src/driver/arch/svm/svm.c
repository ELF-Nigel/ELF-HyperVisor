// svm.c - amd-v skeleton
#include "driver/arch/svm.h"
#include "driver/util/log.h"
#include "driver/core/vmexit.h"
#include "driver/core/diag.h"
#include "driver/util/alloc.h"
#include "driver/util/regs.h"
#include <intrin.h>
#include "public/hv_runtime_checks.h"

#define MSR_EFER 0xC0000080
#define EFER_SVME (1ULL << 12)

int svm_check_support(void) {
    int cpu[4] = {0};
    __cpuid(cpu, 0x80000001);
    return (cpu[2] & (1 << 2)) != 0; // SVM bit
}

int svm_init(svm_state_t* st) {
    if (!st) return STATUS_INVALID_PARAMETER;
    if (!svm_check_support()) return STATUS_NOT_SUPPORTED;
    {
        int cpu[4];
        __cpuid(cpu, (int)0x80000000);
        if ((ULONG)cpu[0] < 0x8000000a) return STATUS_NOT_SUPPORTED;
        __cpuid(cpu, (int)0x8000000a);
        if (!hv_svm_revision_valid((ULONG)cpu[0], (ULONG)cpu[1]) ||
            (st->npt_enabled && !(cpu[3] & 1))) {
            hv_log("[svm] unsupported vmcb layout: revision=%lu asids=%lu\n",
                (ULONG)cpu[0], (ULONG)cpu[1]);
            return STATUS_REVISION_MISMATCH;
        }
        st->svm_revision = (ULONG)cpu[0];
        st->asid_count = (ULONG)cpu[1];
    }

    if (st->svm_on) return STATUS_ALREADY_COMPLETE;
    st->vmcb = hv_alloc_page_aligned(PAGE_SIZE, 'bcmV');
    if (!st->vmcb) return STATUS_INSUFFICIENT_RESOURCES;
    if (!hv_is_page_aligned(st->vmcb)) {
        hv_free_page_aligned(st->vmcb, 'bcmV');
        st->vmcb = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(st->vmcb, PAGE_SIZE);
    st->vmcb_pa = MmGetPhysicalAddress(st->vmcb);
    if (st->vmcb_pa.QuadPart == 0 || !hv_is_phys_page_aligned(st->vmcb_pa)) {
        hv_free_page_aligned(st->vmcb, 'bcmV');
        st->vmcb = NULL;
        return STATUS_UNSUCCESSFUL;
    }

    st->guest_cr0 = __readcr0();
    st->guest_cr2 = __readcr2();
    st->guest_cr3 = __readcr3();
    st->guest_cr4 = __readcr4();
    st->guest_efer = __readmsr(MSR_EFER_MSR);

    // base outline: wire guest save-state area from these fields if VMCB is defined
    // Processor->GuestVmcb->SaveStateArea.CR0 = st->guest_cr0;
    // Processor->GuestVmcb->SaveStateArea.CR2 = st->guest_cr2;
    // Processor->GuestVmcb->SaveStateArea.CR3 = st->guest_cr3;
    // Processor->GuestVmcb->SaveStateArea.CR4 = st->guest_cr4;
    // Processor->GuestVmcb->SaveStateArea.EFER = st->guest_efer;

    if (st->npt_enabled) {
        if (svm_npt_init(&st->npt) != STATUS_SUCCESS) {
            hv_free_page_aligned(st->vmcb, 'bcmV');
            st->vmcb = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    ULONG64 efer = __readmsr(MSR_EFER);
    if (!(efer & EFER_SVME)) {
        __writemsr(MSR_EFER, efer | EFER_SVME);
    }

    st->svm_on = 1;
    return STATUS_SUCCESS;
}

int svm_launch(svm_state_t* st) {
    if (!st || !st->svm_on) return STATUS_INVALID_PARAMETER;
    // real VMRUN requires VMCB + host save area
    return STATUS_SUCCESS;
}

int svm_shutdown(svm_state_t* st) {
    if (!st) return STATUS_INVALID_PARAMETER;
    if (st->vmcb) { hv_free_page_aligned(st->vmcb, 'bcmV'); st->vmcb = NULL; }
    if (st->npt_enabled) svm_npt_shutdown(&st->npt);
    st->svm_on = 0;
    return STATUS_SUCCESS;
}


typedef struct svm_vmcb_control_min_t {
    UINT8 reserved0[0x70];
    UINT64 exit_code;
    UINT64 exit_info1;
    UINT64 exit_info2;
    UINT64 exit_int_info;
} svm_vmcb_control_min_t;

static ULONG64 svm_read_exit_code(const svm_state_t* st) {
    if (!st || !st->vmcb) return 0;
    const volatile svm_vmcb_control_min_t* ctrl = (const volatile svm_vmcb_control_min_t*)st->vmcb;
    return ctrl->exit_code;
}

void svm_vmexit_dispatch(svm_state_t* st) {
    ULONG64 reason = svm_read_exit_code(st);
    if (reason == 0) hv_log("svm vmexit\n");
    PROCESSOR_NUMBER pn;
    KeGetCurrentProcessorNumberEx(&pn);
    hv_trace_push_global(reason, (ULONG)pn.Number + ((ULONG)pn.Group * 64));
    // registry handlers get first chance to handle the vm-exit
    if (hv_vmexit_dispatch_if_registered(reason)) return;
    vmexit_route(reason);
}

int svm_invlpga(UINT64 gpa, UINT16 asid) {
    if (gpa == 0) return STATUS_INVALID_PARAMETER;
    // layout:
    // 1) load gpa into rax, asid into ecx (or use inline intrinsic)
    // 2) execute invlpga
    UNREFERENCED_PARAMETER(asid);
    return STATUS_NOT_IMPLEMENTED;
}

// base outline: allocate npt tables, build identity map, set ncr3 in vmcb,
// configure intercepts for cpuid/msr/io/npf in vmcb controls.


static void svm_handle_npt_violation(void) {
    // base outline: read exitinfo1/2 for npt fault, decode gpa and access,
    // update npt permissions or emulation path, then resume.
    hv_log("npt violation\n");
}

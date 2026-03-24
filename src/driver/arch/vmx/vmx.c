// vmx.c - vt-x skeleton (windows kernel)
#include "driver/arch/vmx.h"
#include "driver/arch/ept.h"
#include "driver/util/alloc.h"
#include <intrin.h>
#include "driver/util/log.h"
#include "driver/core/stats.h"
#include "driver/core/cpuid.h"
#include "driver/core/msr.h"
#include "driver/util/ringlog.h"
#include "driver/core/vmexit.h"
#include "driver/core/diag.h"
extern hv_stats_t g_hv_stats;

#define IA32_FEATURE_CONTROL 0x3A
#define IA32_VMX_BASIC 0x480

static void vmexit_handle_cpuid(void);
static void vmexit_handle_rdtsc(void);
static void vmexit_handle_rdmsr(void);
static void vmexit_handle_wrmsr(void);

static void vmx_free_region(void** region_raw, void** region, ULONG tag) {
    if (region) *region = NULL;
    if (region_raw && *region_raw) {
        hv_free_guarded_page_aligned(*region_raw, tag);
        *region_raw = NULL;
    }
}

static int vmx_alloc_region(void** region_raw, void** region, PHYSICAL_ADDRESS* pa) {
    *region = hv_alloc_guarded_page_aligned(PAGE_SIZE, 'Vvmx', region_raw);
    if (!*region) return 0;
    if (!hv_is_page_aligned(*region)) {
        vmx_free_region(region_raw, region, 'Vvmx');
        return 0;
    }
    RtlZeroMemory(*region, PAGE_SIZE);
    *pa = MmGetPhysicalAddress(*region);
    if (pa->QuadPart == 0 || !hv_is_phys_page_aligned(*pa)) {
        vmx_free_region(region_raw, region, 'Vvmx');
        return 0;
    }
        // base outline: vmwrite msr/io bitmap addresses, pin/cpu controls,
        // vm-entry/vm-exit controls, and eptp if enabled.
    return 1;
}

int vmx_check_support(void) {
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 1);
    return (cpu_info[2] & (1 << 5)) != 0; // vmx bit
}

static int vmx_enable_vmx(void) {
    ULONG64 cr4 = __readcr4();
    if (!(cr4 & (1ULL << 13))) {
        __writecr4(cr4 | (1ULL << 13));
    }

    ULONG64 fc = __readmsr(IA32_FEATURE_CONTROL);
    if (!(fc & 0x1)) {
        fc |= 0x1; // lock
        fc |= 0x4; // enable vmx outside smx
        __writemsr(IA32_FEATURE_CONTROL, fc);
    }

        // base outline: vmwrite control fields and bitmap addresses after vmxon.
    return 1;
}

static int vmx_vmxon(vmx_state_t* st) {
    if (!st || !st->vmxon_region) return 0;
    ULONG64 basic = __readmsr(IA32_VMX_BASIC);
    *(ULONG32*)st->vmxon_region = (ULONG32)(basic & 0xFFFFFFFF);
    return __vmx_on(&st->vmxon_pa.QuadPart) == 0;
}

static void vmx_vmxoff(void) {
    __vmx_off();
}

int vmx_init(vmx_state_t* st) {
    if (!st) return STATUS_INVALID_PARAMETER;
    if (!vmx_check_support()) return STATUS_NOT_SUPPORTED;

    if (!vmx_alloc_region(&st->vmxon_region_raw, &st->vmxon_region, &st->vmxon_pa))
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!vmx_alloc_region(&st->vmcs_region_raw, &st->vmcs_region, &st->vmcs_pa))
        return STATUS_INSUFFICIENT_RESOURCES;

    st->msr_bitmap = hv_alloc_page_aligned(PAGE_SIZE, 'rsmB');
    st->io_bitmap_a = hv_alloc_page_aligned(PAGE_SIZE, 'ioA ');
    st->io_bitmap_b = hv_alloc_page_aligned(PAGE_SIZE, 'ioB ');
    if (!st->msr_bitmap || !st->io_bitmap_a || !st->io_bitmap_b) {
        if (st->msr_bitmap) { hv_free_page_aligned(st->msr_bitmap, 'rsmB'); st->msr_bitmap = NULL; }
        if (st->io_bitmap_a) { hv_free_page_aligned(st->io_bitmap_a, 'ioA '); st->io_bitmap_a = NULL; }
        if (st->io_bitmap_b) { hv_free_page_aligned(st->io_bitmap_b, 'ioB '); st->io_bitmap_b = NULL; }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(st->msr_bitmap, PAGE_SIZE);
    RtlZeroMemory(st->io_bitmap_a, PAGE_SIZE);
    RtlZeroMemory(st->io_bitmap_b, PAGE_SIZE);
    st->msr_bitmap_pa = MmGetPhysicalAddress(st->msr_bitmap);
    st->io_bitmap_a_pa = MmGetPhysicalAddress(st->io_bitmap_a);
    st->io_bitmap_b_pa = MmGetPhysicalAddress(st->io_bitmap_b);
    if (st->msr_bitmap_pa.QuadPart == 0 || st->io_bitmap_a_pa.QuadPart == 0 || st->io_bitmap_b_pa.QuadPart == 0 ||
        !hv_is_phys_page_aligned(st->msr_bitmap_pa) || !hv_is_phys_page_aligned(st->io_bitmap_a_pa) || !hv_is_phys_page_aligned(st->io_bitmap_b_pa)) {
        hv_free_page_aligned(st->msr_bitmap, 'rsmB'); st->msr_bitmap = NULL;
        hv_free_page_aligned(st->io_bitmap_a, 'ioA '); st->io_bitmap_a = NULL;
        hv_free_page_aligned(st->io_bitmap_b, 'ioB '); st->io_bitmap_b = NULL;
        return STATUS_UNSUCCESSFUL;
    }

    if (!vmx_enable_vmx()) return STATUS_UNSUCCESSFUL;
    if (!vmx_vmxon(st)) return STATUS_UNSUCCESSFUL;

    st->vmx_on = 1;
    return STATUS_SUCCESS;
}


static int vmx_write_vmcs(ULONG64 field, ULONG64 value) {
    return __vmx_vmwrite(field, value) == 0;
}


static int vmx_setup_host_guest_state(void) {
    // skeleton: set host/guest state fields here
    // e.g., __vmx_vmwrite(HOST_CR0, __readcr0());
    //       __vmx_vmwrite(GUEST_CR0, __readcr0());
    return 1;
}
static int vmx_setup_vmcs_fields(void) {
    // skeleton: no real fields set (requires full host/guest state)
    // add VM_EXIT_CONTROLS, VM_ENTRY_CONTROLS, PIN/CPU controls, etc.
        // base outline: set exception bitmap, msr/io bitmap fields, eptp,
        // and control masks before vmlaunch.
    return 1;
}
int vmx_setup_vmcs(vmx_state_t* st) {
    if (!st || !st->vmcs_region) return STATUS_INVALID_PARAMETER;

    ULONG64 basic = __readmsr(IA32_VMX_BASIC);
    *(ULONG32*)st->vmcs_region = (ULONG32)(basic & 0xFFFFFFFF);

    if (__vmx_vmclear(&st->vmcs_pa.QuadPart) != 0) return STATUS_UNSUCCESSFUL;
    if (__vmx_vmptrld(&st->vmcs_pa.QuadPart) != 0) return STATUS_UNSUCCESSFUL;

        vmx_setup_vmcs_fields();
    vmx_setup_host_guest_state();
    st->vmcs_loaded = 1;
    return STATUS_SUCCESS;
}

int vmx_launch(vmx_state_t* st) {
    if (!st || !st->vmcs_loaded) return STATUS_INVALID_PARAMETER;
    // NOTE: real hypervisor requires VMCS fields setup before vmlaunch
    return __vmx_vmlaunch() == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

int vmx_shutdown(vmx_state_t* st) {
    if (!st) return STATUS_INVALID_PARAMETER;
    if (st->vmx_on) vmx_vmxoff();
    vmx_free_region(&st->vmcs_region_raw, &st->vmcs_region, 'Vvmx');
    if (st->msr_bitmap) { hv_free_page_aligned(st->msr_bitmap, 'rsmB'); st->msr_bitmap = NULL; }
    if (st->io_bitmap_a) { hv_free_page_aligned(st->io_bitmap_a, 'ioA '); st->io_bitmap_a = NULL; }
    if (st->io_bitmap_b) { hv_free_page_aligned(st->io_bitmap_b, 'ioB '); st->io_bitmap_b = NULL; }
    vmx_free_region(&st->vmxon_region_raw, &st->vmxon_region, 'Vvmx');
    st->vmx_on = 0;
    st->vmcs_loaded = 0;
    return STATUS_SUCCESS;
}

int vmx_set_ept_perm(vmx_state_t* st, ept_perm_t perm) {
    UNREFERENCED_PARAMETER(st);
    UNREFERENCED_PARAMETER(perm);
    // real ept permissions require ept tables and vmcs setup
    return STATUS_SUCCESS;
}

int vmx_invept_all(void) {
    // layout:
    // 1) fill invept descriptor with eptp=0
    // 2) call __vmx_invept with type=all-context
    return STATUS_NOT_IMPLEMENTED;
}

int vmx_invept_single(UINT64 eptp) {
    if (eptp == 0) return STATUS_INVALID_PARAMETER;
    // layout:
    // 1) fill invept descriptor with eptp
    // 2) call __vmx_invept with type=single-context
    return STATUS_NOT_IMPLEMENTED;
}

int vmx_invvpid_all(void) {
    // layout:
    // 1) fill invvpid descriptor with vpid=0 and address=0
    // 2) call __vmx_invvpid with type=all-context
    return STATUS_NOT_IMPLEMENTED;
}

int vmx_invvpid_single(UINT16 vpid) {
    if (vpid == 0) return STATUS_INVALID_PARAMETER;
    // layout:
    // 1) fill invvpid descriptor with vpid
    // 2) call __vmx_invvpid with type=single-context
    return STATUS_NOT_IMPLEMENTED;
}



void vmexit_dispatch(void) {
    ULONG64 reason = 0;
    __vmx_vmread(VM_EXIT_REASON, &reason); // VM_EXIT_REASON

    hv_stats_on_vmexit(reason);
    hv_stats_export_inc_vmexit();
    if ((reason & 0xFF) < 256) {
        hv_stats_export_inc_reason((ULONG)(reason & 0xFF));
    }
    PROCESSOR_NUMBER pn;
    KeGetCurrentProcessorNumberEx(&pn);
    hv_trace_push_global(reason, (ULONG)pn.Number + ((ULONG)pn.Group * 64));
    // registry handlers get first chance to handle the vm-exit
    if (hv_vmexit_dispatch_if_registered(reason)) return;
    switch (reason & 0xFFFF) {
        case EXIT_REASON_CPUID: // CPUID
            vmexit_handle_cpuid();
            break;
        case EXIT_REASON_RDTSC: // RDTSC
            vmexit_handle_rdtsc();
            break;
        case EXIT_REASON_RDMSR: // RDMSR
            vmexit_handle_rdmsr();
            break;
        case EXIT_REASON_WRMSR: // WRMSR
            vmexit_handle_wrmsr();
            break;
        default:
            hv_vmexit_dispatch(reason);
            break;
    }
}


static void vmexit_handle_cpuid(void) {
    int cpu[4] = {0};
    __cpuid(cpu, 0);
    hv_stats_on_cpuid();
    hv_ringlog_write(HV_LOG_EVT_CPUID, 0, 0);
    hv_log("cpuid eax=%x\n", cpu[0]);
}

static void vmexit_handle_rdtsc(void) {
    unsigned __int64 t = __rdtsc();
    hv_stats_on_rdtsc();
    hv_log("rdtsc=%llu\n", t);
}

static void vmexit_handle_rdmsr(void) {
    hv_stats_on_rdmsr();
    hv_msr_on_access(HV_MSR_READ, 0, 0);
    hv_log("rdmsr\n");
}

static void vmexit_handle_wrmsr(void) {
    hv_stats_on_wrmsr();
    hv_msr_on_access(HV_MSR_WRITE, 0, 0);
    hv_log("wrmsr\n");
}

static void vmexit_handle_default(ULONG64 reason) {
    hv_log("unhandled vmexit=%s (0x%llx)\n", hv_vmexit_reason_str(reason), reason);
}


static void vmexit_handle_ept_violation(void) {
    // base outline: vmread exit qualification + gpa, resolve hook/perm,
    // update ept entry, invept, then resume guest.
    hv_log("ept violation\n");
}


static void vmexit_handle_execute_trap(void) {
    // base outline: clear ept exec bit for target page, set monitor flag,
    // resume and re-arm after single-step.
    hv_log("execute trap\n");
}

// hv.c - hypervisor core (skeleton)
#include "driver/core/hv.h"
#include "driver/util/alloc.h"
#include "driver/util/paging.h"
#include "driver/util/regs.h"
#include "driver/util/log.h"
#include <intrin.h>
#include <string.h>

hv_stats_t g_hv_stats = {0};

static const ULONG hv_launch_fail_limit = 3;
static const ULONG hv_launch_retry_limit = 3;
static const ULONG hv_launch_backoff_base_ms = 10;

static NTSTATUS hv_selftest_fail(const char* suite, const char* check) {
    hv_log("[selftest] %s failed: %s\n", suite, check);
    NT_ASSERTMSG(check, FALSE);
    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS hv_selftest_alloc_helpers(void) {
    NTSTATUS st;
    void* page;
    void* raw;
    UCHAR dst[8];
    UCHAR src[4] = {1, 2, 3, 4};
    UINT64 marker;
    PHYSICAL_ADDRESS pa;

    page = hv_alloc_page_aligned(96, 'tShH');
    if (!page) return hv_selftest_fail("alloc", "hv_alloc_page_aligned returned NULL");
    if (!hv_is_page_aligned(page)) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "page-aligned allocation was not aligned");
    }

    raw = ((void**)page)[-1];
    if (!raw || raw == page) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "aligned allocation metadata was not preserved");
    }

    pa.QuadPart = PAGE_SIZE * 4;
    if (!hv_is_phys_page_aligned(pa)) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "aligned physical address was rejected");
    }

    pa.QuadPart += 1;
    if (hv_is_phys_page_aligned(pa)) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "unaligned physical address was accepted");
    }

    marker = ~0ull;
    hv_zero_struct(&marker, sizeof(marker));
    if (marker != 0) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "hv_zero_struct did not zero the buffer");
    }

    RtlZeroMemory(dst, sizeof(dst));
    st = hv_memcpy_checked(dst, sizeof(dst), src, sizeof(src));
    if (!NT_SUCCESS(st) || RtlCompareMemory(dst, src, sizeof(src)) != sizeof(src)) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "hv_memcpy_checked did not copy a fitting buffer");
    }

    st = hv_memcpy_checked(dst, sizeof(dst), src, sizeof(dst) + 1);
    if (st != STATUS_BUFFER_TOO_SMALL) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "hv_memcpy_checked missed oversize detection");
    }

    st = hv_memcpy_checked(NULL, sizeof(dst), src, sizeof(src));
    if (st != STATUS_INVALID_PARAMETER) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "hv_memcpy_checked missed null destination");
    }

    if (hv_min_u64(9, 3) != 3 || hv_max_u64(9, 3) != 9) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "min/max helpers returned unexpected values");
    }

    if (!hv_is_canonical_addr(0x00007FFFFFFFFFFFULL) ||
        !hv_is_canonical_addr(0xFFFF800000000000ULL) ||
        hv_is_canonical_addr(0x0000800000000000ULL) ||
        hv_is_canonical_addr(0xFFFF7FFFFFFFFFFFULL)) {
        hv_free_page_aligned(page, 'tShH');
        return hv_selftest_fail("alloc", "canonical address helper classified an address incorrectly");
    }

    hv_free_page_aligned(page, 'tShH');
    return STATUS_SUCCESS;
}

static NTSTATUS hv_selftest_paging_helpers(void) {
    UINT64 va;

    if (sizeof(PML4_Entry) != sizeof(UINT64) ||
        sizeof(PDP_Entry) != sizeof(UINT64) ||
        sizeof(PD_Entry) != sizeof(UINT64) ||
        sizeof(PT_Entry) != sizeof(UINT64)) {
        return hv_selftest_fail("paging", "paging entries are not 64-bit wide");
    }

    va = ((UINT64)0x1A5 << 39) |
         ((UINT64)0x12C << 30) |
         ((UINT64)0x055 << 21) |
         ((UINT64)0x0F3 << 12) |
         0x0A5;

    if (hv_pml4_index(va) != 0x1A5 ||
        hv_pdpt_index(va) != 0x12C ||
        hv_pd_index(va) != 0x055 ||
        hv_pt_index(va) != 0x0F3 ||
        hv_page_offset(va) != 0x0A5) {
        return hv_selftest_fail("paging", "virtual address index helpers returned incorrect values");
    }

    va = 0xFFFF8123456789ABULL;
    if (hv_pml4_index(va) > 0x1FF ||
        hv_pdpt_index(va) > 0x1FF ||
        hv_pd_index(va) > 0x1FF ||
        hv_pt_index(va) > 0x1FF ||
        hv_page_offset(va) > 0xFFF) {
        return hv_selftest_fail("paging", "paging helper masks overflowed their field widths");
    }

    return STATUS_SUCCESS;
}

static NTSTATUS hv_run_selftests(void) {
    NTSTATUS st;

    st = hv_selftest_alloc_helpers();
    if (!NT_SUCCESS(st)) return st;

    st = hv_selftest_paging_helpers();
    if (!NT_SUCCESS(st)) return st;

    hv_log("[selftest] alloc/paging helpers passed\n");
    return STATUS_SUCCESS;
}

static ULONG64 hv_now_ticks(void) {
    return KeQueryInterruptTime();
}

static void hv_record_launch_failure(hv_cpu_t* cpu, ULONG cpu_index, NTSTATUS st, ULONG attempts) {
    hv_vmexit_cpu_snapshot_t snap[64];

    if (!cpu) return;
    cpu->launch_failures += 1;
    cpu->last_launch_status = st;
    cpu->last_launch_time = hv_now_ticks();
    cpu->failure_dump.last_error = st;
    cpu->failure_dump.cpu_index = cpu_index;
    cpu->failure_dump.attempts = attempts;
    cpu->failure_dump.timestamp = cpu->last_launch_time;
    cpu->failure_dump.cr0 = cpu->cr0;
    cpu->failure_dump.cr3 = cpu->cr3;
    cpu->failure_dump.cr4 = cpu->cr4;
    cpu->failure_dump.efer = cpu->efer;
    cpu->failure_dump.vmexit_last_reason = 0;
    cpu->failure_dump.vmexit_last_tsc = 0;

    hv_trace_global_snapshot_per_cpu(snap, RTL_NUMBER_OF(snap));
    if (cpu_index < RTL_NUMBER_OF(snap) && snap[cpu_index].samples != 0) {
        cpu->failure_dump.vmexit_last_reason = snap[cpu_index].last_reason;
        cpu->failure_dump.vmexit_last_tsc = snap[cpu_index].last_tsc;
    }

    hv_log("[launch] cpu=%lu fail st=0x%08x attempts=%lu cr3=0x%llx last_vmexit=%s (0x%llx)\n",
           cpu_index,
           (UINT32)st,
           attempts,
           cpu->failure_dump.cr3,
           hv_vmexit_reason_str(cpu->failure_dump.vmexit_last_reason),
           cpu->failure_dump.vmexit_last_reason);
}

static void hv_capture_panic_vmexit_snapshots(hv_state_t* hv) {
    if (!hv) return;
    hv_trace_global_snapshot_per_cpu(hv->panic_vmexit_snapshots, RTL_NUMBER_OF(hv->panic_vmexit_snapshots));
    hv->panic_vmexit_snapshot_count = RTL_NUMBER_OF(hv->panic_vmexit_snapshots);
    hv_log("[panic] captured per-cpu vmexit snapshots (%lu slots)\n", hv->panic_vmexit_snapshot_count);
}

static void hv_launch_backoff(ULONG attempt) {
    LARGE_INTEGER interval;
    ULONG delay_ms = hv_launch_backoff_base_ms << attempt;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    interval.QuadPart = -((LONGLONG)delay_ms * 10000LL);
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static NTSTATUS hv_launch_cpu_with_retry(hv_state_t* hv, ULONG cpu_index) {
    hv_cpu_t* cpu;
    NTSTATUS st;

    if (!hv || cpu_index >= hv->cpu_count) return STATUS_INVALID_PARAMETER;
    cpu = &hv->cpus[cpu_index];

    for (ULONG attempt = 0; attempt < hv_launch_retry_limit; ++attempt) {
        st = STATUS_UNSUCCESSFUL;
        if (hv->cpu_vendor == HV_CPU_VENDOR_INTEL) {
            if (!cpu->vmx.vmx_on || !cpu->vmx.vmcs_loaded) return STATUS_INVALID_DEVICE_STATE;
            st = vmx_launch(&cpu->vmx);
        } else if (hv->cpu_vendor == HV_CPU_VENDOR_AMD) {
            if (!cpu->svm.svm_on) return STATUS_INVALID_DEVICE_STATE;
            st = svm_launch(&cpu->svm);
        } else {
            return STATUS_NOT_SUPPORTED;
        }

        if (st == STATUS_SUCCESS) {
            cpu->last_launch_status = STATUS_SUCCESS;
            cpu->last_launch_time = hv_now_ticks();
            cpu->launched = 1;
            return STATUS_SUCCESS;
        }

        hv_log("[launch] cpu=%lu retry=%lu st=0x%08x\n", cpu_index, attempt + 1, (UINT32)st);
        if (attempt + 1 < hv_launch_retry_limit) {
            hv_launch_backoff(attempt);
        } else {
            hv_record_launch_failure(cpu, cpu_index, st, attempt + 1);
            return st;
        }
    }

    return STATUS_UNSUCCESSFUL;
}

static ULONG hv_get_cpu_count(void) {
    return KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
}

static void hv_set_thread_affinity(ULONG cpu_index, GROUP_AFFINITY* prev) {
    GROUP_AFFINITY ga;
    RtlZeroMemory(&ga, sizeof(ga));
    ga.Group = (USHORT)(cpu_index / 64);
    ga.Mask = 1ULL << (cpu_index % 64);
    KeSetSystemGroupAffinityThread(&ga, prev);
}

static void hv_restore_thread_affinity(GROUP_AFFINITY* prev) {
    if (prev) KeSetSystemGroupAffinityThread(prev, NULL);
}

static hv_cpu_vendor_t hv_detect_cpu_vendor(void) {
    int cpu[4] = {0};
    char vendor[13] = {0};
    __cpuid(cpu, 0);
    memcpy(&vendor[0], &cpu[1], sizeof(int));
    memcpy(&vendor[4], &cpu[3], sizeof(int));
    memcpy(&vendor[8], &cpu[2], sizeof(int));
    vendor[12] = '\0';
    if (strcmp(vendor, "GenuineIntel") == 0) return HV_CPU_VENDOR_INTEL;
    if (strcmp(vendor, "AuthenticAMD") == 0) return HV_CPU_VENDOR_AMD;
    return HV_CPU_VENDOR_UNKNOWN;
}

int hv_init(hv_state_t* hv) {
    NTSTATUS st;

    if (!hv) return STATUS_INVALID_PARAMETER;
    if (hv->initialized) return STATUS_ALREADY_COMPLETE;

    st = hv_run_selftests();
    if (!NT_SUCCESS(st)) return st;

    hv_feature_register_defaults();

    hv->cpu_vendor = hv_detect_cpu_vendor();
    if (hv->cpu_vendor == HV_CPU_VENDOR_UNKNOWN) return STATUS_NOT_SUPPORTED;

    hv->handoff_valid = 0;
    hv->config_valid = 0;
    if (NT_SUCCESS(hv_load_efi_handoff(&hv->handoff))) {
        hv->handoff_valid = 1;
        hv->active_config = hv->handoff.Config;
        hv->config_valid = 1;
    }
    if (hv->handoff_valid) {
        hv_config_validate(&hv->active_config);
        hv_apply_handoff_config(hv);
    } else {
        hv_config_defaults(&hv->active_config);
    }
    hv->handoff_integrity_hash = hv_handoff_integrity_hash(&hv->handoff, &hv->active_config);
    hv->config_integrity_hash = hv_hash64(&hv->active_config, sizeof(hv->active_config), 0x4346475f48415348ull);
    hv_log("[integrity] handoff=0x%llx config=0x%llx\n", hv->handoff_integrity_hash, hv->config_integrity_hash);

    hv->cpu_count = hv_get_cpu_count();
    if (hv->cpu_count == 0) return STATUS_NOT_SUPPORTED;
    hv->cpus = (hv_cpu_t*)hv_alloc_page_aligned(sizeof(hv_cpu_t) * hv->cpu_count, 'vHVT');
    if (!hv->cpus) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(hv->cpus, sizeof(hv_cpu_t) * hv->cpu_count);

    hv->dpcs = (hv_dpc_t*)hv_alloc_page_aligned(sizeof(hv_dpc_t) * hv->cpu_count, 'cDpH');
    if (!hv->dpcs) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(hv->dpcs, sizeof(hv_dpc_t) * hv->cpu_count);
    hv_dpc_init(hv->dpcs, hv->cpu_count);

    hv_trace_global_init(4096);
    hv_stats_export_init(L"\\BaseNamedObjects\\elfhvStats");
    hv_gpa_map_export_init(L"\\BaseNamedObjects\\elfhvGpaMap", 256);
    hv_gpa_map_export_refresh();

    for (ULONG i = 0; i < hv->cpu_count; ++i) {
        int ping = hv_dpc_ping_cpu(&hv->dpcs[i], 1000);
        if (ping != STATUS_SUCCESS) { hv_stop(hv); return ping; }
        GROUP_AFFINITY prev_affinity;
        hv_set_thread_affinity(i, &prev_affinity);

        hv->cpus[i].cpu_id = (int)i;
        hv_cpu_snapshot(&hv->cpus[i]);
        if (hv->cpu_vendor == HV_CPU_VENDOR_INTEL) {
            int st = vmx_init(&hv->cpus[i].vmx);
            if (st != STATUS_SUCCESS) { hv_restore_thread_affinity(&prev_affinity); hv_stop(hv); return st; }
            if (hv->ept_enabled) {
                st = ept_init(&hv->cpus[i].ept);
                if (st != STATUS_SUCCESS) { vmx_shutdown(&hv->cpus[i].vmx); hv_restore_thread_affinity(&prev_affinity); hv_stop(hv); return st; }
            }
            st = vmx_setup_vmcs(&hv->cpus[i].vmx);
            if (st != STATUS_SUCCESS) { ept_shutdown(&hv->cpus[i].ept); vmx_shutdown(&hv->cpus[i].vmx); hv_restore_thread_affinity(&prev_affinity); hv_stop(hv); return st; }
        } else if (hv->cpu_vendor == HV_CPU_VENDOR_AMD) {
            hv->cpus[i].svm.npt_enabled = hv->npt_enabled ? 1 : 0;
            int st = svm_init(&hv->cpus[i].svm);
            if (st != STATUS_SUCCESS) { hv_restore_thread_affinity(&prev_affinity); hv_stop(hv); return st; }
            if (!hv->cpus[i].svm.npt.pml4 || !hv->cpus[i].svm.npt.pdp) { hv_restore_thread_affinity(&prev_affinity); hv_stop(hv); return STATUS_INSUFFICIENT_RESOURCES; }
        }
        hv_restore_thread_affinity(&prev_affinity);
    }

    hv->initialized = 1;
    return STATUS_SUCCESS;
}

int hv_cpu_snapshot(hv_cpu_t* cpu) {
    if (!cpu) return STATUS_INVALID_PARAMETER;
    cpu->cr0 = __readcr0();
    {
        CR3 cr3 = hv_read_cr3_union();
        cpu->cr3 = cr3.Contents;
    }
    cpu->cr4 = __readcr4();
    cpu->efer = __readmsr(0xC0000080);
    return STATUS_SUCCESS;
}

const char* hv_cpu_vendor_str(hv_cpu_vendor_t v) {
    switch (v) {
        case HV_CPU_VENDOR_INTEL: return "intel";
        case HV_CPU_VENDOR_AMD: return "amd";
        default: return "unknown";
    }
}

ULONG hv_query_features(hv_state_t* hv) {
    if (!hv) return 0;
    ULONG f = 0;
    if (hv->cpu_vendor == HV_CPU_VENDOR_INTEL) {
        f |= HV_FEAT_VMX | HV_FEAT_EPT;
    } else if (hv->cpu_vendor == HV_CPU_VENDOR_AMD) {
        f |= HV_FEAT_SVM | HV_FEAT_NPT;
    }
    return f;
}

ULONG hv_query_features_auto(void) {
    int cpu[4] = {0};
    ULONG f = 0;
    __cpuid(cpu, 1);
    if (cpu[2] & (1 << 5)) f |= HV_FEAT_VMX;
    if (cpu[2] & (1 << 6)) f |= HV_FEAT_VPID;
    __cpuid(cpu, 0x80000001);
    if (cpu[2] & (1 << 2)) f |= HV_FEAT_SVM;
    if (f & HV_FEAT_VMX) f |= HV_FEAT_EPT;
    if (f & HV_FEAT_SVM) f |= HV_FEAT_NPT;
    return f;
}

void hv_apply_handoff_config(hv_state_t* hv) {
    if (!hv || !hv->handoff_valid) return;
    thv_config_t* cfg = &hv->active_config;
    thv_features_t* feat = &hv->handoff.Features;
    ULONG auto_feat = hv_query_features_auto();

    hv_log("[handoff] vendor=%s cfg vmx=%u svm=%u ept=%u npt=%u vpid=%u verbose=%u\n",
           hv_cpu_vendor_str(hv->cpu_vendor),
           cfg->EnableVmx, cfg->EnableSvm, cfg->EnableEpt, cfg->EnableNpt,
           cfg->EnableVpid, cfg->Verbose);

    if (cfg->EnableVmx && !feat->Vmx) { cfg->EnableVmx = 0; hv_log("[handoff] disable vmx: handoff feature=0\n"); }
    if (cfg->EnableEpt && !feat->Ept) { cfg->EnableEpt = 0; hv_log("[handoff] disable ept: handoff feature=0\n"); }
    if (cfg->EnableVpid && !feat->Vpid) { cfg->EnableVpid = 0; hv_log("[handoff] disable vpid: handoff feature=0\n"); }
    if (cfg->EnableSvm && !feat->Svm) { cfg->EnableSvm = 0; hv_log("[handoff] disable svm: handoff feature=0\n"); }
    if (cfg->EnableNpt && !feat->Npt) { cfg->EnableNpt = 0; hv_log("[handoff] disable npt: handoff feature=0\n"); }

    if (cfg->EnableVmx && !(auto_feat & HV_FEAT_VMX)) { cfg->EnableVmx = 0; hv_log("[handoff] disable vmx: cpu reports unsupported\n"); }
    if (cfg->EnableEpt && !(auto_feat & HV_FEAT_EPT)) { cfg->EnableEpt = 0; hv_log("[handoff] disable ept: cpu reports unsupported\n"); }
    if (cfg->EnableVpid && !(auto_feat & HV_FEAT_VPID)) { cfg->EnableVpid = 0; hv_log("[handoff] disable vpid: cpu reports unsupported\n"); }
    if (cfg->EnableSvm && !(auto_feat & HV_FEAT_SVM)) { cfg->EnableSvm = 0; hv_log("[handoff] disable svm: cpu reports unsupported\n"); }
    if (cfg->EnableNpt && !(auto_feat & HV_FEAT_NPT)) { cfg->EnableNpt = 0; hv_log("[handoff] disable npt: cpu reports unsupported\n"); }

    if (hv->cpu_vendor != HV_CPU_VENDOR_INTEL && cfg->EnableVmx) {
        cfg->EnableVmx = 0;
        cfg->EnableEpt = 0;
        cfg->EnableVpid = 0;
        hv_log("[handoff] disable vmx/ept/vpid: non-intel vendor\n");
    }
    if (hv->cpu_vendor != HV_CPU_VENDOR_AMD && cfg->EnableSvm) {
        cfg->EnableSvm = 0;
        cfg->EnableNpt = 0;
        hv_log("[handoff] disable svm/npt: non-amd vendor\n");
    }

    hv_config_validate(cfg);

    hv->ept_enabled = cfg->EnableEpt ? 1 : 0;
    hv->npt_enabled = cfg->EnableNpt ? 1 : 0;

    if (hv->cpu_vendor == HV_CPU_VENDOR_INTEL && !cfg->EnableVmx) {
        hv_log("[handoff] vmx disabled; suppressing intel launch\n");
        hv->cpu_vendor = HV_CPU_VENDOR_UNKNOWN;
    }
    if (hv->cpu_vendor == HV_CPU_VENDOR_AMD && !cfg->EnableSvm) {
        hv_log("[handoff] svm disabled; suppressing amd launch\n");
        hv->cpu_vendor = HV_CPU_VENDOR_UNKNOWN;
    }

    hv_log("[handoff] applied cfg vmx=%u svm=%u ept=%u npt=%u vpid=%u\n",
           cfg->EnableVmx, cfg->EnableSvm, cfg->EnableEpt, cfg->EnableNpt, cfg->EnableVpid);
}

int hv_cpu_is_intel(hv_state_t* hv) {
    return hv && hv->cpu_vendor == HV_CPU_VENDOR_INTEL;
}

int hv_cpu_is_amd(hv_state_t* hv) {
    return hv && hv->cpu_vendor == HV_CPU_VENDOR_AMD;
}

int hv_feature_enabled(hv_state_t* hv, ULONG feature) {
    if (!hv) return 0;
    return (hv_query_features(hv) & feature) != 0;
}

void hv_config_defaults(thv_config_t* cfg) {
    if (!cfg) return;
    cfg->EnableVmx = 1;
    cfg->EnableSvm = 1;
    cfg->EnableEpt = 1;
    cfg->EnableNpt = 1;
    cfg->EnableVpid = 1;
    cfg->Verbose = 1;
}

int hv_config_validate(thv_config_t* cfg) {
    if (!cfg) return STATUS_INVALID_PARAMETER;
    if (!cfg->EnableVmx && cfg->EnableEpt) cfg->EnableEpt = 0;
    if (!cfg->EnableVmx && cfg->EnableVpid) cfg->EnableVpid = 0;
    if (!cfg->EnableSvm && cfg->EnableNpt) cfg->EnableNpt = 0;
    return STATUS_SUCCESS;
}

int hv_should_use_ept(hv_state_t* hv) {
    return hv && hv->ept_enabled && hv_cpu_is_intel(hv);
}

int hv_should_use_npt(hv_state_t* hv) {
    return hv && hv->npt_enabled && hv_cpu_is_amd(hv);
}

int hv_can_launch(hv_state_t* hv) {
    return hv && hv->initialized && (hv_cpu_is_intel(hv) || hv_cpu_is_amd(hv));
}

ULONG hv_config_flags(thv_config_t* cfg) {
    if (!cfg) return 0;
    ULONG f = 0;
    if (cfg->EnableVmx) f |= HV_FEAT_VMX;
    if (cfg->EnableEpt) f |= HV_FEAT_EPT;
    if (cfg->EnableVpid) f |= HV_FEAT_VPID;
    if (cfg->EnableSvm) f |= HV_FEAT_SVM;
    if (cfg->EnableNpt) f |= HV_FEAT_NPT;
    return f;
}

int hv_start(hv_state_t* hv) {
    UINT64 config_hash_now;

    if (!hv || !hv->initialized) return STATUS_INVALID_PARAMETER;
    config_hash_now = hv_hash64(&hv->active_config, sizeof(hv->active_config), 0x4346475f48415348ull);
    if (config_hash_now != hv->config_integrity_hash) {
        hv_log("[integrity] runtime config hash changed old=0x%llx new=0x%llx\n", hv->config_integrity_hash, config_hash_now);
        hv->config_integrity_hash = config_hash_now;
    }

    // per-cpu launch (skeleton)
    for (ULONG i = 0; i < hv->cpu_count; ++i) {
        NTSTATUS st;
        if (hv->cpus[i].launch_failures >= hv_launch_fail_limit) {
            hv_log("[launch] cpu %lu blocked by watchdog (failures=%lu)\n",
                   i, hv->cpus[i].launch_failures);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }
        st = hv_launch_cpu_with_retry(hv, i);
        if (!NT_SUCCESS(st)) {
            hv_capture_panic_vmexit_snapshots(hv);
            hv_stop(hv);
            return st;
        }
    }
    return STATUS_SUCCESS;
}

int hv_stop(hv_state_t* hv) {
    if (!hv || !hv->initialized) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < hv->cpu_count; ++i) {
        if (hv->cpu_vendor == HV_CPU_VENDOR_INTEL) {
            vmx_shutdown(&hv->cpus[i].vmx);
            ept_shutdown(&hv->cpus[i].ept);
        } else if (hv->cpu_vendor == HV_CPU_VENDOR_AMD) {
            svm_shutdown(&hv->cpus[i].svm);
        }
        hv->cpus[i].launched = 0;
    }
    return STATUS_SUCCESS;
}

void hv_shutdown(hv_state_t* hv) {
    if (!hv) return;
    hv_stop(hv);
    hv_gpa_map_export_shutdown();
    hv_trace_global_shutdown();
    hv_stats_export_shutdown();
    if (hv->dpcs) {
        hv_dpc_shutdown(hv->dpcs, hv->cpu_count);
        hv_free_page_aligned(hv->dpcs, 'cDpH');
        hv->dpcs = NULL;
    }
    if (hv->cpus) {
        hv_free_page_aligned(hv->cpus, 'vHVT');
        hv->cpus = NULL;
    }
    hv->handoff_integrity_hash = 0;
    hv->config_integrity_hash = 0;
    hv->panic_vmexit_snapshot_count = 0;
    hv->cpu_count = 0;
    hv->initialized = 0;
}

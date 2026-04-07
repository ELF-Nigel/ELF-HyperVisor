// diag.c - diagnostics tools (skeleton)
#include "driver/core/diag.h"
#include "driver/util/log.h"
#include "driver/util/alloc.h"
#include "driver/arch/vmcs.h"
#include <intrin.h>

typedef struct hv_vmexit_reason_name_t {
    UINT16 reason;
    const char* name;
} hv_vmexit_reason_name_t;

typedef struct hv_trace_export_header_t {
    ULONG version;
    ULONG capacity;
    volatile LONG64 head;
    ULONG rate_limit_tsc_delta;
    volatile LONG64 dropped_rate_limited;
    volatile LONG64 dropped_backpressure;
    ULONG reserved;
} hv_trace_export_header_t;

hv_trace_ring_t g_hv_trace = {0};
static HANDLE g_stats_section = NULL;
static void* g_stats_view = NULL;
static HANDLE g_gpa_map_section = NULL;
static void* g_gpa_map_view = NULL;
static ULONG g_gpa_map_max_ranges = 0;
static ULONG64 g_trace_last_tsc[256] = {0};

typedef struct hv_stats_export_t {
    ULONG version;
    ULONG reserved;
    hv_stats_t stats;
} hv_stats_export_t;

typedef struct hv_gpa_map_export_header_t {
    ULONG version;
    ULONG range_count;
    ULONG max_ranges;
    ULONG reserved;
} hv_gpa_map_export_header_t;

static const hv_vmexit_reason_name_t g_vmexit_reason_names[] = {
    { EXIT_REASON_CPUID, "cpuid" },
    { EXIT_REASON_RDTSC, "rdtsc" },
    { EXIT_REASON_VMCALL, "vmcall" },
    { EXIT_REASON_CR_ACCESS, "cr_access" },
    { EXIT_REASON_IO_INSTRUCTION, "io_instruction" },
    { EXIT_REASON_RDMSR, "rdmsr" },
    { EXIT_REASON_WRMSR, "wrmsr" },
    { EXIT_REASON_EPT_VIOLATION, "ept_violation" }
};

void hv_dump_vmx(vmx_state_t* st) {
    if (!st) return;
    hv_log("vmx dump: vmxon=%p vmcs=%p\n", st->vmxon_region, st->vmcs_region);
}

void hv_dump_svm(svm_state_t* st) {
    if (!st) return;
    hv_log("svm dump: vmcb=%p\n", st->vmcb);
}

const char* hv_vmexit_reason_str(ULONG64 reason) {
    UINT16 idx = (UINT16)(reason & 0xFFFF);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_vmexit_reason_names); ++i) {
        if (g_vmexit_reason_names[i].reason == idx) return g_vmexit_reason_names[i].name;
    }
    return "unknown";
}

static hv_gpa_map_export_header_t* hv_gpa_export_header(void) {
    return (hv_gpa_map_export_header_t*)g_gpa_map_view;
}

static hv_gpa_range_snapshot_t* hv_gpa_export_ranges(void) {
    return (hv_gpa_range_snapshot_t*)((UINT8*)g_gpa_map_view + sizeof(hv_gpa_map_export_header_t));
}

int hv_trace_init(hv_trace_ring_t* r, ULONG capacity) {
    if (!r || capacity == 0) return STATUS_INVALID_PARAMETER;
    r->events = (hv_trace_event_t*)hv_alloc_page_aligned(sizeof(hv_trace_event_t) * capacity, 'rhvT');
    if (!r->events) return STATUS_INSUFFICIENT_RESOURCES;
    hv_zero_struct(r->events, sizeof(hv_trace_event_t) * capacity);
    r->dropped_rate_limited = 0;
    r->dropped_backpressure = 0;
    r->rate_limit_tsc_delta = 0;
    r->capacity = capacity;
    r->head = 0;
    return STATUS_SUCCESS;
}

static hv_trace_export_header_t* hv_trace_export_header(hv_trace_ring_t* r) {
    return (hv_trace_export_header_t*)r->export_view;
}

static hv_trace_event_t* hv_trace_export_events(hv_trace_ring_t* r) {
    return (hv_trace_event_t*)((UINT8*)r->export_view + sizeof(hv_trace_export_header_t));
}

int hv_trace_export_init(hv_trace_ring_t* r, ULONG capacity, PCWSTR name) {
    if (!r || !name || capacity == 0) return STATUS_INVALID_PARAMETER;
    if (r->export_view) return STATUS_ALREADY_COMPLETE;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    SIZE_T size = sizeof(hv_trace_export_header_t) + (SIZE_T)capacity * sizeof(hv_trace_event_t);
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;
    NTSTATUS st = ZwCreateSection(&r->export_section, SECTION_ALL_ACCESS, &oa, &li, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(st)) return st;
    st = MmMapViewInSystemSpace(r->export_section, &r->export_view, &size);
    if (!NT_SUCCESS(st)) {
        ZwClose(r->export_section);
        r->export_section = NULL;
        r->export_view = NULL;
        return st;
    }
    r->export_size = size;
    RtlZeroMemory(r->export_view, size);
    hv_trace_export_header_t* hdr = hv_trace_export_header(r);
    hdr->version = 2;
    hdr->capacity = capacity;
    hdr->head = 0;
    hdr->rate_limit_tsc_delta = r->rate_limit_tsc_delta;
    hdr->dropped_rate_limited = 0;
    hdr->dropped_backpressure = 0;
    return STATUS_SUCCESS;
}

void hv_trace_export_shutdown(hv_trace_ring_t* r) {
    if (!r) return;
    if (r->export_view) {
        MmUnmapViewInSystemSpace(r->export_view);
        r->export_view = NULL;
    }
    if (r->export_section) {
        ZwClose(r->export_section);
        r->export_section = NULL;
    }
    r->export_size = 0;
}

void hv_trace_shutdown(hv_trace_ring_t* r) {
    if (!r) return;
    hv_trace_export_shutdown(r);
    if (r->events) {
        hv_free_page_aligned(r->events, 'rhvT');
        r->events = NULL;
    }
    r->capacity = 0;
    r->head = 0;
}

void hv_trace_push(hv_trace_ring_t* r, ULONG64 reason, ULONG cpu) {
    ULONG slot_cpu;
    ULONG64 tsc;

    if (!r || !r->events || r->capacity == 0) return;

    tsc = __rdtsc();
    slot_cpu = (ULONG)(cpu % RTL_NUMBER_OF(g_trace_last_tsc));
    if (r->rate_limit_tsc_delta != 0) {
        ULONG64 last = g_trace_last_tsc[slot_cpu];
        if (last != 0 && (tsc - last) < r->rate_limit_tsc_delta) {
            InterlockedIncrement64(&r->dropped_rate_limited);
            if (r->export_view) {
                InterlockedExchange64(&hv_trace_export_header(r)->dropped_rate_limited, r->dropped_rate_limited);
            }
            return;
        }
    }
    g_trace_last_tsc[slot_cpu] = tsc;

    LONG64 idx = InterlockedIncrement64(&r->head) - 1;
    if (idx >= (LONG64)r->capacity) {
        InterlockedIncrement64(&r->dropped_backpressure);
    }
    ULONG slot = (ULONG)(idx % r->capacity);
    r->events[slot].tsc = tsc;
    r->events[slot].reason = reason;
    r->events[slot].cpu = cpu;
    if (r->export_view) {
        hv_trace_export_header_t* hdr = hv_trace_export_header(r);
        hv_trace_event_t* events = hv_trace_export_events(r);
        events[slot] = r->events[slot];
        hdr->rate_limit_tsc_delta = r->rate_limit_tsc_delta;
        InterlockedExchange64(&hdr->dropped_rate_limited, r->dropped_rate_limited);
        InterlockedExchange64(&hdr->dropped_backpressure, r->dropped_backpressure);
        InterlockedExchange64(&hdr->head, r->head);
    }
}

ULONG hv_trace_read(hv_trace_ring_t* r, hv_trace_event_t* out, ULONG max_count) {
    if (!r || !r->events || !out || max_count == 0) return 0;
    ULONG count = (r->capacity < max_count) ? r->capacity : max_count;
    LONG64 head = r->head;
    ULONG start = (ULONG)((head > (LONG64)count) ? (head - count) : 0);
    for (ULONG i = 0; i < count; ++i) {
        ULONG idx = (start + i) % r->capacity;
        out[i] = r->events[idx];
    }
    return count;
}

void hv_trace_reset(hv_trace_ring_t* r) {
    if (!r || !r->events) return;
    hv_zero_struct(r->events, sizeof(hv_trace_event_t) * r->capacity);
    r->head = 0;
    r->dropped_rate_limited = 0;
    r->dropped_backpressure = 0;
    hv_zero_struct(g_trace_last_tsc, sizeof(g_trace_last_tsc));
    if (r->export_view) {
        hv_trace_export_header_t* hdr = hv_trace_export_header(r);
        hv_zero_struct(hv_trace_export_events(r), sizeof(hv_trace_event_t) * r->capacity);
        hdr->head = 0;
        hdr->rate_limit_tsc_delta = r->rate_limit_tsc_delta;
        hdr->dropped_rate_limited = 0;
        hdr->dropped_backpressure = 0;
    }
}

ULONG hv_trace_capacity(hv_trace_ring_t* r) {
    if (!r) return 0;
    return r->capacity;
}

ULONG hv_trace_count(hv_trace_ring_t* r) {
    if (!r || r->capacity == 0) return 0;
    LONG64 head = r->head;
    if (head < 0) return 0;
    return (ULONG)hv_min_u64((UINT64)head, (UINT64)r->capacity);
}

int hv_trace_peek(hv_trace_ring_t* r, ULONG index, hv_trace_event_t* out) {
    if (!r || !r->events || !out || r->capacity == 0) return STATUS_INVALID_PARAMETER;
    if (index >= r->capacity) return STATUS_INVALID_PARAMETER;
    *out = r->events[index];
    return STATUS_SUCCESS;
}

void hv_trace_set_rate_limit(hv_trace_ring_t* r, ULONG tsc_delta) {
    if (!r) return;
    r->rate_limit_tsc_delta = tsc_delta;
    if (r->export_view) {
        hv_trace_export_header(r)->rate_limit_tsc_delta = tsc_delta;
    }
}

void hv_trace_get_drop_counters(hv_trace_ring_t* r, hv_trace_drop_counters_t* out) {
    if (!r || !out) return;
    out->rate_limited = (ULONGLONG)r->dropped_rate_limited;
    out->backpressure = (ULONGLONG)r->dropped_backpressure;
}

void hv_trace_snapshot_per_cpu(hv_trace_ring_t* r, hv_vmexit_cpu_snapshot_t* out, ULONG max_out) {
    ULONG count;
    LONG64 head;
    ULONG start;

    if (!r || !r->events || !out || max_out == 0) return;
    for (ULONG i = 0; i < max_out; ++i) {
        out[i].cpu = i;
        out[i].samples = 0;
        out[i].last_tsc = 0;
        out[i].last_reason = 0;
    }

    count = (r->capacity < 2048) ? r->capacity : 2048;
    head = r->head;
    start = (ULONG)((head > (LONG64)count) ? (head - count) : 0);

    for (ULONG i = 0; i < count; ++i) {
        ULONG idx = (start + i) % r->capacity;
        hv_trace_event_t* e = &r->events[idx];
        ULONG slot = (ULONG)(e->cpu % max_out);
        if (e->tsc == 0) continue;
        out[slot].cpu = slot;
        out[slot].samples += 1;
        if (e->tsc >= out[slot].last_tsc) {
            out[slot].last_tsc = e->tsc;
            out[slot].last_reason = e->reason;
        }
    }
}

int hv_trace_global_init(ULONG capacity) {
    int st = hv_trace_init(&g_hv_trace, capacity);
    if (st != STATUS_SUCCESS) return st;
    hv_trace_set_rate_limit(&g_hv_trace, 5000);
    return hv_trace_export_init(&g_hv_trace, capacity, L"\\BaseNamedObjects\\elfhvTrace");
}

void hv_trace_global_shutdown(void) {
    hv_trace_shutdown(&g_hv_trace);
}

void hv_trace_push_global(ULONG64 reason, ULONG cpu) {
    hv_trace_push(&g_hv_trace, reason, cpu);
}

void hv_trace_global_get_drop_counters(hv_trace_drop_counters_t* out) {
    hv_trace_get_drop_counters(&g_hv_trace, out);
}

void hv_trace_global_snapshot_per_cpu(hv_vmexit_cpu_snapshot_t* out, ULONG max_out) {
    hv_trace_snapshot_per_cpu(&g_hv_trace, out, max_out);
}

int hv_gpa_map_export_init(PCWSTR name, ULONG max_ranges) {
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    SIZE_T size;
    LARGE_INTEGER li;
    NTSTATUS st;

    if (!name || max_ranges == 0) return STATUS_INVALID_PARAMETER;
    if (g_gpa_map_view) return STATUS_ALREADY_COMPLETE;

    size = sizeof(hv_gpa_map_export_header_t) + ((SIZE_T)max_ranges * sizeof(hv_gpa_range_snapshot_t));
    RtlInitUnicodeString(&us, name);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    li.QuadPart = (LONGLONG)size;
    st = ZwCreateSection(&g_gpa_map_section, SECTION_ALL_ACCESS, &oa, &li, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(st)) return st;

    st = MmMapViewInSystemSpace(g_gpa_map_section, &g_gpa_map_view, &size);
    if (!NT_SUCCESS(st)) {
        ZwClose(g_gpa_map_section);
        g_gpa_map_section = NULL;
        g_gpa_map_view = NULL;
        return st;
    }

    g_gpa_map_max_ranges = max_ranges;
    RtlZeroMemory(g_gpa_map_view, size);
    hv_gpa_export_header()->version = 1;
    hv_gpa_export_header()->max_ranges = max_ranges;
    hv_gpa_map_export_refresh();
    return STATUS_SUCCESS;
}

void hv_gpa_map_export_shutdown(void) {
    if (g_gpa_map_view) {
        MmUnmapViewInSystemSpace(g_gpa_map_view);
        g_gpa_map_view = NULL;
    }
    if (g_gpa_map_section) {
        ZwClose(g_gpa_map_section);
        g_gpa_map_section = NULL;
    }
    g_gpa_map_max_ranges = 0;
}

void hv_gpa_map_export_refresh(void) {
    PPHYSICAL_MEMORY_RANGE ranges;
    ULONG out_count = 0;

    if (!g_gpa_map_view || g_gpa_map_max_ranges == 0) return;
    ranges = MmGetPhysicalMemoryRanges();
    if (!ranges) return;

    hv_gpa_export_header()->range_count = 0;
    RtlZeroMemory(hv_gpa_export_ranges(), sizeof(hv_gpa_range_snapshot_t) * g_gpa_map_max_ranges);

    for (ULONG i = 0; ranges[i].BaseAddress.QuadPart || ranges[i].NumberOfBytes.QuadPart; ++i) {
        if (out_count >= g_gpa_map_max_ranges) break;
        hv_gpa_export_ranges()[out_count].base_pfn = (ULONGLONG)(ranges[i].BaseAddress.QuadPart >> PAGE_SHIFT);
        hv_gpa_export_ranges()[out_count].page_count = (ULONGLONG)(ranges[i].NumberOfBytes.QuadPart >> PAGE_SHIFT);
        out_count++;
    }
    hv_gpa_export_header()->range_count = out_count;
    ExFreePool(ranges);
}

void hv_stats_reset(void) {
    hv_zero_struct(&g_hv_stats, sizeof(g_hv_stats));
    if (g_stats_view) {
        hv_stats_export_t* ex = (hv_stats_export_t*)g_stats_view;
        hv_zero_struct(&ex->stats, sizeof(ex->stats));
    }
}

void hv_stats_snapshot(hv_stats_t* out) {
    if (!out) return;
    hv_memcpy_checked(out, sizeof(*out), &g_hv_stats, sizeof(g_hv_stats));
}

elfhv_i64 hv_stats_reason_get(ULONG reason) {
    if (reason >= 256) return 0;
    return g_hv_stats.reason_counts[reason];
}

void hv_stats_reason_reset(ULONG reason) {
    if (reason >= 256) return;
    g_hv_stats.reason_counts[reason] = 0;
    if (g_stats_view) {
        hv_stats_export_t* ex = (hv_stats_export_t*)g_stats_view;
        ex->stats.reason_counts[reason] = 0;
    }
}

static void hv_reason_insert(hv_reason_stat_t* out, ULONG max, ULONG reason, elfhv_i64 count) {
    for (ULONG i = 0; i < max; ++i) {
        if (count > out[i].count) {
            for (ULONG j = max - 1; j > i; --j) out[j] = out[j - 1];
            out[i].reason = reason;
            out[i].count = count;
            break;
        }
    }
}

ULONG hv_stats_top_reasons(hv_reason_stat_t* out, ULONG max_out) {
    if (!out || max_out == 0) return 0;
    for (ULONG i = 0; i < max_out; ++i) { out[i].reason = 0; out[i].count = 0; }
    for (ULONG r = 0; r < 256; ++r) {
        elfhv_i64 count = g_hv_stats.reason_counts[r];
        if (count > 0) hv_reason_insert(out, max_out, r, count);
    }
    return max_out;
}

int hv_stats_export_init(PCWSTR name) {
    if (!name) return STATUS_INVALID_PARAMETER;
    if (g_stats_view) return STATUS_ALREADY_COMPLETE;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    SIZE_T size = sizeof(hv_stats_export_t);
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;
    NTSTATUS st = ZwCreateSection(&g_stats_section, SECTION_ALL_ACCESS, &oa, &li, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(st)) return st;
    st = MmMapViewInSystemSpace(g_stats_section, &g_stats_view, &size);
    if (!NT_SUCCESS(st)) {
        ZwClose(g_stats_section);
        g_stats_section = NULL;
        g_stats_view = NULL;
        return st;
    }
    RtlZeroMemory(g_stats_view, size);
    ((hv_stats_export_t*)g_stats_view)->version = 1;
    return STATUS_SUCCESS;
}

void hv_stats_export_shutdown(void) {
    if (g_stats_view) {
        MmUnmapViewInSystemSpace(g_stats_view);
        g_stats_view = NULL;
    }
    if (g_stats_section) {
        ZwClose(g_stats_section);
        g_stats_section = NULL;
    }
}

void hv_stats_export_inc_reason(ULONG reason) {
    if (!g_stats_view || reason >= 256) return;
    hv_stats_export_t* ex = (hv_stats_export_t*)g_stats_view;
    InterlockedIncrement64(&ex->stats.reason_counts[reason]);
}

void hv_stats_export_inc_vmexit(void) {
    if (!g_stats_view) return;
    hv_stats_export_t* ex = (hv_stats_export_t*)g_stats_view;
    InterlockedIncrement64(&ex->stats.vmexit_count);
}

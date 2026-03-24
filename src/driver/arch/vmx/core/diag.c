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
    ULONG reserved;
} hv_trace_export_header_t;

hv_trace_ring_t g_hv_trace = {0};
static HANDLE g_stats_section = NULL;
static void* g_stats_view = NULL;

typedef struct hv_stats_export_t {
    ULONG version;
    ULONG reserved;
    hv_stats_t stats;
} hv_stats_export_t;

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

int hv_trace_init(hv_trace_ring_t* r, ULONG capacity) {
    if (!r || capacity == 0) return STATUS_INVALID_PARAMETER;
    r->events = (hv_trace_event_t*)hv_alloc_page_aligned(sizeof(hv_trace_event_t) * capacity, 'rhvT');
    if (!r->events) return STATUS_INSUFFICIENT_RESOURCES;
    hv_zero_struct(r->events, sizeof(hv_trace_event_t) * capacity);
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
    hdr->version = 1;
    hdr->capacity = capacity;
    hdr->head = 0;
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
    if (!r || !r->events || r->capacity == 0) return;
    LONG64 idx = InterlockedIncrement64(&r->head) - 1;
    ULONG slot = (ULONG)(idx % r->capacity);
    r->events[slot].tsc = __rdtsc();
    r->events[slot].reason = reason;
    r->events[slot].cpu = cpu;
    if (r->export_view) {
        hv_trace_event_t* events = hv_trace_export_events(r);
        events[slot] = r->events[slot];
        InterlockedExchange64(&hv_trace_export_header(r)->head, r->head);
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
    if (r->export_view) {
        hv_trace_export_header_t* hdr = hv_trace_export_header(r);
        hv_zero_struct(hv_trace_export_events(r), sizeof(hv_trace_event_t) * r->capacity);
        hdr->head = 0;
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

int hv_trace_global_init(ULONG capacity) {
    int st = hv_trace_init(&g_hv_trace, capacity);
    if (st != STATUS_SUCCESS) return st;
    return hv_trace_export_init(&g_hv_trace, capacity, L"\\BaseNamedObjects\\elfhvTrace");
}

void hv_trace_global_shutdown(void) {
    hv_trace_shutdown(&g_hv_trace);
}

void hv_trace_push_global(ULONG64 reason, ULONG cpu) {
    hv_trace_push(&g_hv_trace, reason, cpu);
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

// diag.h - diagnostics tools (skeleton)
#pragma once
#include <ntddk.h>
#include "driver/arch/vmx.h"
#include "driver/arch/svm.h"
#include "driver/core/stats.h"

void hv_dump_vmx(vmx_state_t* st);
void hv_dump_svm(svm_state_t* st);
const char* hv_vmexit_reason_str(ULONG64 reason);

typedef struct hv_trace_event_t {
    ULONG64 tsc;
    ULONG64 reason;
    ULONG cpu;
} hv_trace_event_t;

typedef struct hv_trace_ring_t {
    volatile LONG64 head;
    ULONG capacity;
    hv_trace_event_t* events;
    HANDLE export_section;
    void* export_view;
    SIZE_T export_size;
} hv_trace_ring_t;

int hv_trace_init(hv_trace_ring_t* r, ULONG capacity);
void hv_trace_shutdown(hv_trace_ring_t* r);
void hv_trace_push(hv_trace_ring_t* r, ULONG64 reason, ULONG cpu);
ULONG hv_trace_read(hv_trace_ring_t* r, hv_trace_event_t* out, ULONG max_count);
void hv_trace_reset(hv_trace_ring_t* r);
ULONG hv_trace_capacity(hv_trace_ring_t* r);
ULONG hv_trace_count(hv_trace_ring_t* r);
int hv_trace_peek(hv_trace_ring_t* r, ULONG index, hv_trace_event_t* out);

int hv_trace_export_init(hv_trace_ring_t* r, ULONG capacity, PCWSTR name);
void hv_trace_export_shutdown(hv_trace_ring_t* r);
int hv_trace_global_init(ULONG capacity);
void hv_trace_global_shutdown(void);
void hv_trace_push_global(ULONG64 reason, ULONG cpu);

void hv_stats_reset(void);
void hv_stats_snapshot(hv_stats_t* out);
elfhv_i64 hv_stats_reason_get(ULONG reason);
void hv_stats_reason_reset(ULONG reason);
int hv_stats_export_init(PCWSTR name);
void hv_stats_export_shutdown(void);
void hv_stats_export_inc_reason(ULONG reason);
void hv_stats_export_inc_vmexit(void);

typedef struct hv_reason_stat_t {
    ULONG reason;
    elfhv_i64 count;
} hv_reason_stat_t;

ULONG hv_stats_top_reasons(hv_reason_stat_t* out, ULONG max_out);

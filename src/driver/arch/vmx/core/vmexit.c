// vmexit.c - vmexit routing (skeleton)
#include "driver/core/vmexit.h"
#include "driver/util/log.h"
#include "driver/util/alloc.h"
#include "driver/core/stats.h"
#include "driver/core/cpuid.h"
#include "driver/core/msr.h"
#include "driver/core/control_reg.h"
#include "driver/util/ringlog.h"

static hv_vmexit_handler_t g_vmexit_table[0x10000];

static void handle_cpuid(void){
    hv_stats_on_cpuid();
    hv_ringlog_write(HV_LOG_EVT_CPUID, 0, 0);
    hv_log("route:cpuid\n");
}
static void handle_rdtsc(void){
    hv_stats_on_rdtsc();
    hv_log("route:rdtsc\n");
}
static void handle_rdmsr(void){
    hv_stats_on_rdmsr();
    hv_msr_on_access(HV_MSR_READ, 0, 0);
    hv_log("route:rdmsr\n");
}
static void handle_wrmsr(void){
    hv_stats_on_wrmsr();
    hv_msr_on_access(HV_MSR_WRITE, 0, 0);
    hv_log("route:wrmsr\n");
}
static void handle_vmcall(void){
    hv_stats_on_vmcall();
    hv_ringlog_write(HV_LOG_EVT_VMEXIT, EXIT_REASON_VMCALL, 0);
    hv_log("route:vmcall\n");
}
static void handle_pf(void){ hv_log("route:pf\n"); }
static void handle_int(void){ hv_log("route:int\n"); }
static void handle_cr_access(void){
    hv_stats_on_cr_access();
    hv_cr_on_access(0, 0, 1);
    hv_log("route:cr_access\n");
}
static void handle_io(void){
    hv_stats_on_io();
    hv_ringlog_write(HV_LOG_EVT_IO, 0, 0);
    hv_log("route:io\n");
}
static void handle_ept(void){
    hv_stats_on_ept();
    hv_ringlog_write(HV_LOG_EVT_VMEXIT, EXIT_REASON_EPT_VIOLATION, 0);
    hv_log("route:ept\n");
}

static void hv_vmexit_default(ULONG64 reason) {
    hv_log("route:default %s (0x%llx)\n", hv_vmexit_reason_str(reason), reason);
}

int hv_vmexit_register(UINT16 reason, hv_vmexit_handler_t handler) {
    if (!handler) return STATUS_INVALID_PARAMETER;
    g_vmexit_table[reason] = handler;
    return STATUS_SUCCESS;
}

int hv_vmexit_unregister(UINT16 reason) {
    g_vmexit_table[reason] = NULL;
    return STATUS_SUCCESS;
}

void hv_vmexit_dispatch(ULONG64 reason) {
    UINT16 idx = (UINT16)(reason & 0xFFFF);
    hv_vmexit_handler_t h = g_vmexit_table[idx];
    if (h) h(reason);
    else hv_vmexit_default(reason);
}

int hv_vmexit_dispatch_if_registered(ULONG64 reason) {
    UINT16 idx = (UINT16)(reason & 0xFFFF);
    hv_vmexit_handler_t h = g_vmexit_table[idx];
    if (!h) return 0;
    h(reason);
    return 1;
}

void vmexit_route(ULONG64 reason) {
    hv_stats_on_vmexit(reason);
    switch (reason & 0xFFFF) {
        case EXIT_REASON_CPUID: handle_cpuid(); break;
        case EXIT_REASON_RDTSC: handle_rdtsc(); break;
        case EXIT_REASON_RDMSR: handle_rdmsr(); break;
        case EXIT_REASON_WRMSR: handle_wrmsr(); break;
        case EXIT_REASON_CR_ACCESS: handle_cr_access(); break;
        case EXIT_REASON_IO_INSTRUCTION: handle_io(); break;
        case EXIT_REASON_EPT_VIOLATION: handle_ept(); break;
        case EXIT_REASON_VMCALL: handle_vmcall(); break;
        default: hv_vmexit_dispatch(reason); break;
    }
}

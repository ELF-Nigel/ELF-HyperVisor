#pragma once
#include <ntddk.h>
#include "public/hv_exit_runtime.h"

typedef struct hv_runtime_config_t {
    ULONG selftest_mode;
    ULONG stats_interval_us;
    const ULONG* cpu_stats_intervals_us;
    ULONG cpu_stats_interval_count;
    const hv_exit_policy_t* exit_policy;
} hv_runtime_config_t;

void hv_runtime_config_defaults(hv_runtime_config_t* config);
NTSTATUS hv_vmexit_runtime_init(ULONG cpu_count, const hv_runtime_config_t* config);
void hv_vmexit_runtime_shutdown(void);
int hv_vmexit_filter(ULONG architecture, ULONG64 reason);
NTSTATUS hv_vmexit_cpu_sample(ULONG cpu, hv_exit_sample_t* sample);

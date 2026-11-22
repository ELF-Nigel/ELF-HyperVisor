#include "driver/core/runtime_control.h"
#include "driver/util/alloc.h"
#include "driver/util/log.h"

static hv_exit_policy_t g_exit_policy;
static hv_exit_cpu_stats_t* g_cpu_stats;
static ULONG g_cpu_count;

void hv_runtime_config_defaults(hv_runtime_config_t* config) {
    if (!config) return;
    RtlZeroMemory(config, sizeof(*config));
    config->stats_interval_us = 1000;
}

NTSTATUS hv_vmexit_runtime_init(ULONG cpu_count, const hv_runtime_config_t* config) {
    int smoke;
    if (!config || !cpu_count || config->stats_interval_us > HV_EXIT_SAMPLE_MAX_US ||
        config->selftest_mode > HV_SELFTEST_HARDWARE ||
        (config->cpu_stats_interval_count && (!config->cpu_stats_intervals_us ||
            config->cpu_stats_interval_count != cpu_count))) return STATUS_INVALID_PARAMETER;
    if (g_cpu_stats) return STATUS_ALREADY_COMPLETE;
    if (config->exit_policy && !hv_exit_policy_validate(config->exit_policy)) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < config->cpu_stats_interval_count; ++i) {
        if (config->cpu_stats_intervals_us[i] > HV_EXIT_SAMPLE_MAX_US) return STATUS_INVALID_PARAMETER;
    }
    smoke = hv_exit_selftest(config->selftest_mode);
    if (smoke < 0) return STATUS_NOT_SUPPORTED;
    if (!smoke) return STATUS_UNSUCCESSFUL;
    if (config->selftest_mode) hv_log("[selftest] software vmexit smoke test passed; hardware not tested\n");
    if (cpu_count > MAXULONG_PTR / sizeof(*g_cpu_stats)) return STATUS_INTEGER_OVERFLOW;
    g_cpu_stats = hv_alloc_page_aligned((SIZE_T)cpu_count * sizeof(*g_cpu_stats), 'sExH');
    if (!g_cpu_stats) return STATUS_INSUFFICIENT_RESOURCES;
    if (config->exit_policy) g_exit_policy = *config->exit_policy;
    else hv_exit_policy_defaults(&g_exit_policy);
    for (ULONG i = 0; i < cpu_count; ++i) {
        ULONG interval = config->cpu_stats_interval_count ? config->cpu_stats_intervals_us[i] : config->stats_interval_us;
        hv_exit_stats_configure(&g_cpu_stats[i], interval);
    }
    g_cpu_count = cpu_count;
    return STATUS_SUCCESS;
}

void hv_vmexit_runtime_shutdown(void) {
    // stop and join exit producers before freeing telemetry.
    if (g_cpu_stats) {
        RtlSecureZeroMemory(g_cpu_stats, (SIZE_T)g_cpu_count * sizeof(*g_cpu_stats));
        hv_free_page_aligned(g_cpu_stats, 'sExH');
        g_cpu_stats = NULL;
    }
    g_cpu_count = 0;
    RtlZeroMemory(&g_exit_policy, sizeof(g_exit_policy));
}

NTSTATUS hv_vmexit_cpu_sample(ULONG cpu, hv_exit_sample_t* sample) {
    // caller quiesces exit producers before reading a consistent snapshot.
    if (!sample || !g_cpu_stats || cpu >= g_cpu_count) return STATUS_INVALID_PARAMETER;
    *sample = g_cpu_stats[cpu].published;
    return STATUS_SUCCESS;
}

int hv_vmexit_filter(ULONG architecture, ULONG64 reason) {
    PROCESSOR_NUMBER number;
    ULONG index;
    if (!g_cpu_stats) return 0;
    KeGetCurrentProcessorNumberEx(&number);
    index = KeGetProcessorIndexFromNumber(&number);
    if (index >= g_cpu_count) return 0;
    return hv_exit_process(&g_exit_policy, &g_cpu_stats[index], architecture, reason,
        KeQueryInterruptTime(), NULL, NULL);
}

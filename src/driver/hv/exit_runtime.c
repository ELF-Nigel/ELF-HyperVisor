#include "public/hv_exit_runtime.h"
#include <string.h>

void hv_exit_policy_defaults(hv_exit_policy_t* policy) {
    if (!policy) return;
    memset(policy, 0, sizeof(*policy));
    policy->default_action = HV_EXIT_ALLOW;
}

int hv_exit_policy_validate(const hv_exit_policy_t* policy) {
    uint32_t i, j;
    if (!policy || policy->count > HV_EXIT_POLICY_MAX || policy->default_action > HV_EXIT_ALLOW) return 0;
    for (i = 0; i < policy->count; ++i) {
        const hv_exit_rule_t* rule = &policy->rules[i];
        if ((rule->architecture != HV_EXIT_VMX && rule->architecture != HV_EXIT_SVM) ||
            rule->reason > UINT16_MAX || rule->action > HV_EXIT_ALLOW) return 0;
        for (j = 0; j < i; ++j) {
            if (policy->rules[j].architecture == rule->architecture &&
                policy->rules[j].reason == rule->reason) return 0;
        }
    }
    return 1;
}

int hv_exit_policy_allows(const hv_exit_policy_t* policy, uint32_t architecture, uint64_t reason) {
    uint32_t i;
    uint64_t key = reason;
    if (!policy || policy->count > HV_EXIT_POLICY_MAX ||
        (architecture != HV_EXIT_VMX && architecture != HV_EXIT_SVM)) return 0;
    if (architecture == HV_EXIT_VMX) {
        // entry failures and malformed high bits never reach ordinary handlers.
        if (reason > UINT32_MAX || (reason & (1ull << 31))) return 0;
        key = reason & UINT16_MAX;
    }
    if (key > UINT16_MAX) return 0;
    for (i = 0; i < policy->count; ++i) {
        const hv_exit_rule_t* rule = &policy->rules[i];
        if (rule->architecture == architecture && rule->reason == key)
            return rule->action == HV_EXIT_ALLOW;
    }
    return policy->default_action == HV_EXIT_ALLOW;
}

int hv_exit_stats_configure(hv_exit_cpu_stats_t* stats, uint32_t interval_us) {
    if (!stats || interval_us > HV_EXIT_SAMPLE_MAX_US) return 0;
    memset(stats, 0, sizeof(*stats));
    stats->interval_us = interval_us;
    return 1;
}

int hv_exit_process(const hv_exit_policy_t* policy, hv_exit_cpu_stats_t* stats,
    uint32_t architecture, uint64_t reason, uint64_t now_100ns,
    hv_exit_callback_t callback, void* context) {
    int allowed;
    if (!stats) return 0;
    allowed = hv_exit_policy_allows(policy, architecture, reason);
    ++stats->current.observed;
    if (allowed) ++stats->current.allowed;
    else ++stats->current.denied;
    stats->current.last_reason = reason;
    stats->current.timestamp_100ns = now_100ns;
    if (!stats->sampled || now_100ns < stats->published.timestamp_100ns ||
        now_100ns - stats->published.timestamp_100ns >= (uint64_t)stats->interval_us * 10) {
        ++stats->current.publications;
        stats->published = stats->current;
        stats->sampled = 1;
    }
    if (allowed && callback) callback(context, architecture, reason);
    return allowed;
}

typedef struct hv_smoke_context_t {
    uint32_t calls;
    uint32_t architecture;
    uint64_t reason;
} hv_smoke_context_t;

static void hv_smoke_callback(void* context, uint32_t architecture, uint64_t reason) {
    hv_smoke_context_t* smoke = (hv_smoke_context_t*)context;
    ++smoke->calls;
    smoke->architecture = architecture;
    smoke->reason = reason;
}

int hv_exit_selftest(uint32_t mode) {
    hv_exit_policy_t policy;
    hv_exit_cpu_stats_t stats;
    hv_smoke_context_t smoke = {0};
    uint32_t architecture;
    if (mode == HV_SELFTEST_OFF) return 1;
    if (mode == HV_SELFTEST_HARDWARE) return -1;
    if (mode != HV_SELFTEST_DISPATCH) return 0;
    hv_exit_policy_defaults(&policy);
    for (architecture = HV_EXIT_VMX; architecture <= HV_EXIT_SVM; ++architecture) {
        uint64_t reason = architecture == HV_EXIT_VMX ? 10 : 0x72;
        smoke.calls = 0;
        hv_exit_stats_configure(&stats, 1000);
        if (!hv_exit_process(&policy, &stats, architecture, reason, 1, hv_smoke_callback, &smoke) ||
            smoke.calls != 1 || smoke.architecture != architecture || smoke.reason != reason ||
            stats.published.observed != 1 || stats.published.allowed != 1) return 0;
        policy.default_action = HV_EXIT_DENY;
        if (hv_exit_process(&policy, &stats, architecture, reason, 10001, hv_smoke_callback, &smoke) ||
            smoke.calls != 1 || stats.published.denied != 1 || stats.published.observed != 2) return 0;
        policy.default_action = HV_EXIT_ALLOW;
    }
    return 1;
}

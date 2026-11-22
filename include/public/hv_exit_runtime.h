#pragma once
#include <stdint.h>
#include <stddef.h>

#define HV_EXIT_VMX 1u
#define HV_EXIT_SVM 2u
#define HV_EXIT_ALLOW 1u
#define HV_EXIT_DENY 0u
#define HV_EXIT_POLICY_MAX 128u
#define HV_EXIT_SAMPLE_MAX_US 60000000u
#define HV_SELFTEST_OFF 0u
#define HV_SELFTEST_DISPATCH 1u
#define HV_SELFTEST_HARDWARE 2u

typedef struct hv_exit_rule_t {
    uint32_t architecture;
    uint32_t reason;
    uint32_t action;
} hv_exit_rule_t;

typedef struct hv_exit_policy_t {
    uint32_t default_action;
    uint32_t count;
    hv_exit_rule_t rules[HV_EXIT_POLICY_MAX];
} hv_exit_policy_t;

typedef struct hv_exit_sample_t {
    uint64_t observed;
    uint64_t allowed;
    uint64_t denied;
    uint64_t last_reason;
    uint64_t timestamp_100ns;
    uint64_t publications;
} hv_exit_sample_t;

typedef struct hv_exit_cpu_stats_t {
    uint32_t interval_us;
    uint32_t sampled;
    hv_exit_sample_t current;
    hv_exit_sample_t published;
} hv_exit_cpu_stats_t;

typedef void (*hv_exit_callback_t)(void* context, uint32_t architecture, uint64_t reason);

void hv_exit_policy_defaults(hv_exit_policy_t* policy);
int hv_exit_policy_validate(const hv_exit_policy_t* policy);
int hv_exit_policy_allows(const hv_exit_policy_t* policy, uint32_t architecture, uint64_t reason);
int hv_exit_stats_configure(hv_exit_cpu_stats_t* stats, uint32_t interval_us);
int hv_exit_process(const hv_exit_policy_t* policy, hv_exit_cpu_stats_t* stats,
    uint32_t architecture, uint64_t reason, uint64_t now_100ns,
    hv_exit_callback_t callback, void* context);
// returns -1 for unsupported hardware mode, 0 on failure, and 1 on success.
int hv_exit_selftest(uint32_t mode);

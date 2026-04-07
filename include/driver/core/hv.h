// hv.h - hypervisor core interfaces (skeleton)
#pragma once
#include <ntddk.h>
#include "driver/arch/vmx.h"
#include "driver/arch/ept.h"
#include "driver/arch/svm.h"
#include "driver/core/stats.h"
#include "driver/hooks/hook.h"
#include "driver/hooks/hook_ext.h"
#include "driver/hooks/hook_more.h"
#include "driver/core/hypercall.h"
#include "driver/core/diag.h"
#include "driver/core/manager.h"
#include "driver/core/write.h"
#include "driver/util/map.h"
#include "driver/core/handoff.h"
#include "driver/core/dpc.h"
#include "driver/core/feature.h"

typedef struct hv_launch_failure_dump_t {
    NTSTATUS last_error;
    ULONG cpu_index;
    ULONG attempts;
    ULONG64 timestamp;
    ULONG64 cr0;
    ULONG64 cr3;
    ULONG64 cr4;
    ULONG64 efer;
    ULONG64 vmexit_last_reason;
    ULONG64 vmexit_last_tsc;
} hv_launch_failure_dump_t;

typedef struct hv_cpu_t {
    ept_state_t ept;
    vmx_state_t vmx;
    svm_state_t svm;
    int cpu_id;
    int launched;
    ULONG launch_failures;
    NTSTATUS last_launch_status;
    ULONG64 last_launch_time;
    ULONG64 cr0;
    ULONG64 cr3;
    ULONG64 cr4;
    ULONG64 efer;
    hv_launch_failure_dump_t failure_dump;
} hv_cpu_t;

typedef enum hv_cpu_vendor_t {
    HV_CPU_VENDOR_UNKNOWN = 0,
    HV_CPU_VENDOR_INTEL,
    HV_CPU_VENDOR_AMD
} hv_cpu_vendor_t;

typedef struct hv_state_t {
    hv_cpu_t* cpus;
    ULONG cpu_count;
    int initialized;
    hv_cpu_vendor_t cpu_vendor;
    thv_handoff_t handoff;
    int handoff_valid;
    thv_config_t active_config;
    int config_valid;
    int ept_enabled;
    int npt_enabled;
    hv_dpc_t* dpcs;
    UINT64 handoff_integrity_hash;
    UINT64 config_integrity_hash;
    hv_vmexit_cpu_snapshot_t panic_vmexit_snapshots[64];
    ULONG panic_vmexit_snapshot_count;
} hv_state_t;

int hv_init(hv_state_t* hv);
int hv_start(hv_state_t* hv);
int hv_stop(hv_state_t* hv);
void hv_shutdown(hv_state_t* hv);
int hv_cpu_snapshot(hv_cpu_t* cpu);
const char* hv_cpu_vendor_str(hv_cpu_vendor_t v);

typedef enum hv_feature_flags_t {
    HV_FEAT_VMX = 1 << 0,
    HV_FEAT_EPT = 1 << 1,
    HV_FEAT_VPID = 1 << 2,
    HV_FEAT_SVM = 1 << 3,
    HV_FEAT_NPT = 1 << 4
} hv_feature_flags_t;

ULONG hv_query_features(hv_state_t* hv);
ULONG hv_query_features_auto(void);
void hv_apply_handoff_config(hv_state_t* hv);

int hv_cpu_is_intel(hv_state_t* hv);
int hv_cpu_is_amd(hv_state_t* hv);
int hv_feature_enabled(hv_state_t* hv, ULONG feature);
void hv_config_defaults(thv_config_t* cfg);
int hv_config_validate(thv_config_t* cfg);
int hv_should_use_ept(hv_state_t* hv);
int hv_should_use_npt(hv_state_t* hv);
int hv_can_launch(hv_state_t* hv);
ULONG hv_config_flags(thv_config_t* cfg);

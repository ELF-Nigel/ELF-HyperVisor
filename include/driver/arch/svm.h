// svm.h - amd-v skeleton
#pragma once
#include <ntddk.h>
#include "driver/arch/npt.h"

typedef struct svm_state_t {
    void* vmcb_raw;
    void* vmcb;
    PHYSICAL_ADDRESS vmcb_pa;
    UINT64 guest_cr3;
    UINT64 guest_cr0;
    UINT64 guest_cr2;
    UINT64 guest_cr4;
    UINT64 guest_efer;
    ULONG svm_revision;
    ULONG asid_count;
    svm_npt_tables_t npt;
    int npt_enabled;
    int svm_on;
} svm_state_t;

int svm_check_support(void);
int svm_init(svm_state_t* st);
int svm_launch(svm_state_t* st);
int svm_shutdown(svm_state_t* st);
void svm_vmexit_dispatch(svm_state_t* st);
int svm_invlpga(UINT64 gpa, UINT16 asid);

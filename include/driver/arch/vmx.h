// vmx.h - vt-x skeleton (windows kernel)
#pragma once
#include <ntddk.h>
#include "driver/arch/vmcs.h"

typedef struct ept_perm_t {
    int read;
    int write;
    int exec;
} ept_perm_t;

typedef struct vmx_state_t {
    void* msr_bitmap_raw;
    void* msr_bitmap;
    PHYSICAL_ADDRESS msr_bitmap_pa;
    void* io_bitmap_a_raw;
    void* io_bitmap_a;
    PHYSICAL_ADDRESS io_bitmap_a_pa;
    void* io_bitmap_b_raw;
    void* io_bitmap_b;
    PHYSICAL_ADDRESS io_bitmap_b_pa;
    void* vmxon_region_raw;
    void* vmxon_region;
    PHYSICAL_ADDRESS vmxon_pa;
    void* vmcs_region_raw;
    void* vmcs_region;
    PHYSICAL_ADDRESS vmcs_pa;
    UINT64 guest_cr3;
    ULONG vmcs_revision;
    int vmx_on;
    int vmcs_loaded;
} vmx_state_t;

int vmx_check_support(void);
int vmx_init(vmx_state_t* st);
int vmx_setup_vmcs(vmx_state_t* st);
int vmx_set_ept_perm(vmx_state_t* st, ept_perm_t perm);
void vmexit_dispatch(void);
int vmx_launch(vmx_state_t* st);
int vmx_shutdown(vmx_state_t* st);
int vmx_invept_all(void);
int vmx_invept_single(UINT64 eptp);
int vmx_invvpid_all(void);
int vmx_invvpid_single(UINT16 vpid);

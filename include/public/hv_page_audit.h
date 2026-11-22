#pragma once
#include <stdint.h>
#include "public/hv_exit_runtime.h"

#define HV_PAGE_READ 1u
#define HV_PAGE_WRITE 2u
#define HV_PAGE_EXECUTE 4u
#define HV_PAGE_AUDIT_WX 1u
#define HV_PAGE_AUDIT_INVALID 2u

typedef const uint64_t* (*hv_page_map_t)(void* context, uint64_t physical_address);
typedef struct hv_page_audit_t {
    uint64_t gpa;
    uint64_t leaf;
    uint64_t page_size;
    uint32_t permissions;
    uint32_t flags;
    uint32_t architecture;
    uint32_t present;
} hv_page_audit_t;

// caller holds page tables stable; map returns a readable 512-entry table.
int hv_page_audit_walk(uint32_t architecture, const uint64_t* root, uint64_t gpa,
    hv_page_map_t map, void* context, hv_page_audit_t* report);
typedef void (*hv_page_audit_sink_t)(void* context, const hv_page_audit_t* report);
void hv_page_audit_emit(const hv_page_audit_t* report, hv_page_audit_sink_t sink, void* context);

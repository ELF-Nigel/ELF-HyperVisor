#pragma once
#include <stdint.h>

static __inline int hv_vmx_basic_valid(uint64_t basic) {
    uint32_t size = (uint32_t)((basic >> 32) & 0x1fff);
    return !(basic & (1ull << 31)) && size >= 8 && size <= 4096 &&
        ((basic >> 50) & 15) == 6;
}

static __inline int hv_svm_revision_valid(uint32_t revision, uint32_t asids) {
    return revision == 1 && asids > 1;
}

static __inline uint32_t hv_feature_mismatch(uint32_t baseline, uint32_t actual) {
    return baseline ^ actual;
}

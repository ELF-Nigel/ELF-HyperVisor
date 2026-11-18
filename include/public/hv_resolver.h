#pragma once
#include <stddef.h>
#include <stdint.h>

#define HV_MODULE_DIGEST_SIZE 32

typedef struct hv_module_signature_t {
    uint8_t sha256[HV_MODULE_DIGEST_SIZE];
    const uint8_t* pattern;
    const char* mask;
    size_t pattern_size;
} hv_module_signature_t;

typedef enum hv_resolver_status_t {
    HV_RESOLVER_OK = 0,
    HV_RESOLVER_INVALID,
    HV_RESOLVER_HASH_FAILED,
    HV_RESOLVER_DENIED,
    HV_RESOLVER_NOT_FOUND,
    HV_RESOLVER_AMBIGUOUS
} hv_resolver_status_t;

// resolve offsets only; the caller owns a stable image and trusted policy.
hv_resolver_status_t hv_resolver_resolve(const void* image, size_t image_size,
    const hv_module_signature_t* whitelist, size_t count, size_t* offset);

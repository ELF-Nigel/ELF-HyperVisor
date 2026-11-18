#ifdef elfhv_usermode
#include <windows.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <ntddk.h>
#pragma comment(lib, "cng.lib")
#endif
#include <bcrypt.h>
#include <string.h>
#include "public/hv_resolver.h"

hv_resolver_status_t hv_resolver_resolve(const void* image, size_t image_size,
    const hv_module_signature_t* whitelist, size_t count, size_t* offset) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t digest[HV_MODULE_DIGEST_SIZE];
    const uint8_t* bytes = (const uint8_t*)image;
    size_t match = 0, matches = 0, entry, i, j;
    int allowed = 0;
    NTSTATUS status;
    if (offset) *offset = 0;
    if (!image || !image_size || image_size > UINT32_MAX || !whitelist || !count || !offset)
        return HV_RESOLVER_INVALID;
#ifndef elfhv_usermode
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return HV_RESOLVER_INVALID;
#endif
    // validate policy before hashing or scanning the image.
    for (entry = 0; entry < count; ++entry) {
        const hv_module_signature_t* signature = &whitelist[entry];
        int concrete = 0;
        if (!signature->pattern || !signature->mask || !signature->pattern_size ||
            signature->pattern_size > image_size) return HV_RESOLVER_INVALID;
        for (i = 0; i < signature->pattern_size; ++i) {
            if (signature->mask[i] == 'x') concrete = 1;
            else if (signature->mask[i] != '?') return HV_RESOLVER_INVALID;
        }
        if (!concrete) return HV_RESOLVER_INVALID;
    }
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) return HV_RESOLVER_HASH_FAILED;
    status = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, (PUCHAR)image, (ULONG)image_size, 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        RtlSecureZeroMemory(digest, sizeof(digest));
        return HV_RESOLVER_HASH_FAILED;
    }
    for (entry = 0; entry < count; ++entry) {
        const hv_module_signature_t* signature = &whitelist[entry];
        if (memcmp(digest, signature->sha256, sizeof(digest))) continue;
        allowed = 1;
        for (i = 0; i <= image_size - signature->pattern_size; ++i) {
            for (j = 0; j < signature->pattern_size; ++j) {
                if (signature->mask[j] == 'x' && bytes[i + j] != signature->pattern[j]) break;
            }
            if (j == signature->pattern_size) {
                if (matches && match != i) {
                    RtlSecureZeroMemory(digest, sizeof(digest));
                    return HV_RESOLVER_AMBIGUOUS;
                }
                match = i;
                matches = 1;
            }
        }
    }
    RtlSecureZeroMemory(digest, sizeof(digest));
    if (!allowed) return HV_RESOLVER_DENIED;
    if (!matches) return HV_RESOLVER_NOT_FOUND;
    *offset = match;
    return HV_RESOLVER_OK;
}

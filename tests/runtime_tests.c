#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include "public/hv_runtime_checks.h"
#include "public/hv_trace_codec.h"
#include "public/hv_resolver.h"

#define CHECK(condition) do { if (!(condition)) { \
    printf("failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static int test_trace(void) {
    uint8_t encoded[128], short_buffer[1] = {0xaa};
    uint64_t previous = 0, decoded_previous = 0, tsc, reason;
    uint32_t cpu;
    uint64_t times[] = {1000000, 1000001, 1000100, UINT64_MAX, 3, 2};
    size_t offset = 0, consumed = 0, sizes[6], i;
    for (i = 0; i < 6; ++i) {
        sizes[i] = hv_trace_encode_event(encoded + offset, sizeof(encoded) - offset,
            &previous, times[i], i == 3 ? UINT64_MAX : 12, i == 3 ? UINT32_MAX : 2);
        CHECK(sizes[i] > 0);
        offset += sizes[i];
    }
    CHECK(sizes[1] == 3);
    CHECK(offset < 6 * 24);
    for (i = 0; i < 6; ++i) {
        size_t n = hv_trace_decode_event(encoded + consumed, offset - consumed,
            &decoded_previous, &tsc, &reason, &cpu);
        CHECK(n == sizes[i] && tsc == times[i]);
        CHECK(reason == (i == 3 ? UINT64_MAX : 12));
        CHECK(cpu == (i == 3 ? UINT32_MAX : 2));
        consumed += n;
    }
    CHECK(consumed == offset);
    previous = 99;
    CHECK(!hv_trace_encode_event(short_buffer, sizeof(short_buffer), &previous, 100, 1, 1));
    CHECK(previous == 99 && short_buffer[0] == 0xaa);
    CHECK(!hv_trace_decode_event(encoded, 1, &previous, &tsc, &reason, &cpu));
    memset(encoded, 0xff, 10);
    CHECK(!hv_trace_get_varint(encoded, 10, &tsc));
    previous = 0;
    offset = hv_trace_put_varint(encoded, 1);
    offset += hv_trace_put_varint(encoded + offset, 2);
    offset += hv_trace_put_varint(encoded + offset, (uint64_t)UINT32_MAX + 1);
    CHECK(!hv_trace_decode_event(encoded, offset, &previous, &tsc, &reason, &cpu));
    CHECK(previous == 0);
    return 0;
}

static int test_versions(void) {
    uint64_t basic = 42ull | (4096ull << 32) | (6ull << 50);
    CHECK(hv_vmx_basic_valid(basic));
    CHECK(!hv_vmx_basic_valid(basic | (1ull << 31)));
    CHECK(!hv_vmx_basic_valid(42ull | (4097ull << 32) | (6ull << 50)));
    CHECK(!hv_vmx_basic_valid(42ull | (6ull << 50)));
    CHECK(!hv_vmx_basic_valid(42ull | (4096ull << 32)));
    CHECK(hv_svm_revision_valid(1, 2));
    CHECK(!hv_svm_revision_valid(0, 2));
    CHECK(!hv_svm_revision_valid(2, 2));
    CHECK(!hv_svm_revision_valid(1, 1));
    CHECK(hv_feature_mismatch(7, 7) == 0);
    CHECK(hv_feature_mismatch(7, 3) == 4);
    CHECK(hv_feature_mismatch(3, 7) == 4);
    return 0;
}

static int test_resolver(void) {
    const uint8_t image[] = {'a', 'b', 'c'};
    const uint8_t pattern[] = {'b', 'c'};
    const uint8_t abc_sha256[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    hv_module_signature_t policy = {{0}, pattern, "xx", sizeof(pattern)};
    size_t offset = 99;
    CHECK(hv_resolver_resolve(image, sizeof(image), &policy, 1, &offset) == HV_RESOLVER_DENIED);
    CHECK(offset == 0);
    memcpy(policy.sha256, abc_sha256, 32);
    CHECK(hv_resolver_resolve(image, sizeof(image), &policy, 1, &offset) == HV_RESOLVER_OK);
    CHECK(offset == 1);
    CHECK(hv_resolver_resolve("abd", 3, &policy, 1, &offset) == HV_RESOLVER_DENIED);
    policy.pattern = (const uint8_t*)"zz";
    CHECK(hv_resolver_resolve(image, sizeof(image), &policy, 1, &offset) == HV_RESOLVER_NOT_FOUND);
    policy.pattern = pattern;
    policy.mask = "?x";
    CHECK(hv_resolver_resolve(image, sizeof(image), &policy, 1, &offset) == HV_RESOLVER_OK);
    policy.pattern_size = 1;
    policy.pattern = (const uint8_t*)"a";
    policy.mask = "?";
    CHECK(hv_resolver_resolve(image, 3, &policy, 1, &offset) == HV_RESOLVER_INVALID);
    policy.mask = "z";
    CHECK(hv_resolver_resolve(image, 3, &policy, 1, &offset) == HV_RESOLVER_INVALID);
    CHECK(hv_resolver_resolve(image, 3, NULL, 0, &offset) == HV_RESOLVER_INVALID);
    CHECK(hv_resolver_resolve(image, 3, &policy, 1, NULL) == HV_RESOLVER_INVALID);
    policy.pattern = (const uint8_t*)"a";
    policy.mask = "x";
    {
        BCRYPT_ALG_HANDLE algorithm = NULL;
        CHECK(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0);
        CHECK(BCryptHash(algorithm, NULL, 0, (PUCHAR)"aaa", 3, policy.sha256, 32) >= 0);
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CHECK(hv_resolver_resolve("aaa", 3, &policy, 1, &offset) == HV_RESOLVER_AMBIGUOUS);
    CHECK(offset == 0);
    return 0;
}

int main(void) {
    if (test_trace() || test_versions() || test_resolver()) return 1;
    puts("runtime tests passed");
    return 0;
}

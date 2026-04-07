// alloc.h - page-aligned pool helpers
#pragma once
#include <ntddk.h>

__forceinline void* hv_alloc_pool_fallback(SIZE_T size, ULONG tag) {
    void* p = ExAllocatePoolWithTag(NonPagedPoolNx, size, tag);
    if (p) return p;
    // low-memory fallback for older targets where nx pool may be exhausted.
    return ExAllocatePoolWithTag(NonPagedPool, size, tag);
}

__forceinline void* hv_alloc_page_aligned(SIZE_T size, ULONG tag) {
    SIZE_T total = size + PAGE_SIZE + sizeof(void*);
    void* raw = hv_alloc_pool_fallback(total, tag);
    if (!raw) return NULL;
    ULONG_PTR start = (ULONG_PTR)raw + sizeof(void*);
    ULONG_PTR aligned = (start + (PAGE_SIZE - 1)) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ((void**)aligned)[-1] = raw;
    return (void*)aligned;
}

__forceinline void* hv_alloc_guarded_page_aligned(SIZE_T size, ULONG tag, void** raw_out) {
    const SIZE_T slop = (PAGE_SIZE * 3) - 1;
    SIZE_T total;
    void* raw;
    ULONG_PTR start;
    ULONG_PTR aligned;

    if (!raw_out || !size) return NULL;
    *raw_out = NULL;
    if (size > ((SIZE_T)-1) - slop) return NULL;

    total = size + slop;
    raw = hv_alloc_pool_fallback(total, tag);
    if (!raw) return NULL;

    start = (ULONG_PTR)raw + PAGE_SIZE;
    aligned = (start + (PAGE_SIZE - 1)) & ~(ULONG_PTR)(PAGE_SIZE - 1);

    *raw_out = raw;
    return (void*)aligned;
}

__forceinline void hv_free_page_aligned(void* aligned, ULONG tag) {
    if (!aligned) return;
    void* raw = ((void**)aligned)[-1];
    ExFreePoolWithTag(raw, tag);
}

__forceinline void hv_free_guarded_page_aligned(void* raw, ULONG tag) {
    if (!raw) return;
    ExFreePoolWithTag(raw, tag);
}

__forceinline int hv_is_page_aligned(const void* p) {
    return (((ULONG_PTR)p) & (PAGE_SIZE - 1)) == 0;
}

__forceinline int hv_is_phys_page_aligned(PHYSICAL_ADDRESS pa) {
    return ((pa.QuadPart & (PAGE_SIZE - 1)) == 0);
}

#define hv_check_page_aligned(p) (hv_is_page_aligned((p)))

#define hv_assert_passive() NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL)

__forceinline void hv_zero_struct(void* p, SIZE_T sz) {
    if (p && sz) RtlZeroMemory(p, sz);
}

__forceinline NTSTATUS hv_memcpy_checked(void* dst, SIZE_T dst_size, const void* src, SIZE_T src_size) {
    if (!dst || !src) return STATUS_INVALID_PARAMETER;
    if (src_size > dst_size) return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(dst, src, src_size);
    return STATUS_SUCCESS;
}

__forceinline UINT64 hv_min_u64(UINT64 a, UINT64 b) { return (a < b) ? a : b; }
__forceinline UINT64 hv_max_u64(UINT64 a, UINT64 b) { return (a > b) ? a : b; }

__forceinline int hv_is_canonical_addr(UINT64 addr) {
    UINT64 top = addr >> 48;
    return (top == 0x0000ULL) || (top == 0xFFFFULL);
}

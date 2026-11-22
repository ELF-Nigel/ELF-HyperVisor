#pragma once
#include <ntddk.h>
#include "public/hv_page_audit.h"

int hv_page_audit_query(ULONG architecture, const void* root, UINT64 gpa, hv_page_audit_t* report);
#if DBG
void hv_debug_audit_page(ULONG architecture, const void* root, UINT64 gpa);
#else
#define hv_debug_audit_page(architecture, root, gpa) ((void)0)
#endif

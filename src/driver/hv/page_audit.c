#include "public/hv_page_audit.h"
#include <string.h>

int hv_page_audit_walk(uint32_t architecture, const uint64_t* root, uint64_t gpa,
    hv_page_map_t map, void* context, hv_page_audit_t* report) {
    const uint64_t address_mask = 0x000ffffffffff000ull;
    uint32_t level, effective = 7;
    const uint64_t* table = root;
    if (report) memset(report, 0, sizeof(*report));
    if (!root || !map || !report || gpa >= (1ull << 48) ||
        (architecture != HV_EXIT_VMX && architecture != HV_EXIT_SVM)) return 0;
    report->gpa = gpa;
    report->architecture = architecture;
    for (level = 0; level < 4; ++level) {
        uint32_t shift = 39 - 9 * level;
        uint64_t entry = table[(gpa >> shift) & 511];
        uint32_t permissions;
        int large = level < 3 && (entry & (1ull << 7)) != 0;
        report->leaf = entry;
        if (architecture == HV_EXIT_VMX) {
            permissions = (uint32_t)(entry & 7);
            if ((permissions & HV_PAGE_WRITE) && !(permissions & HV_PAGE_READ))
                report->flags |= HV_PAGE_AUDIT_INVALID;
        } else {
            permissions = (entry & 1) ? HV_PAGE_READ : 0;
            if (entry & 2) permissions |= HV_PAGE_WRITE;
            if (!(entry & (1ull << 63))) permissions |= HV_PAGE_EXECUTE;
        }
        if ((architecture == HV_EXIT_VMX && !permissions) ||
            (architecture == HV_EXIT_SVM && !(entry & 1))) return 1;
        effective &= permissions;
        if (large && level == 0) {
            report->flags |= HV_PAGE_AUDIT_INVALID;
            return 1;
        }
        if (large || level == 3) {
            uint64_t page_size = 1ull << shift;
            uint64_t reserved = (page_size - 1) & address_mask;
            if (architecture == HV_EXIT_SVM && large) reserved &= ~(1ull << 12);
            if (entry & reserved) report->flags |= HV_PAGE_AUDIT_INVALID;
            report->page_size = page_size;
            report->permissions = effective;
            report->present = !(report->flags & HV_PAGE_AUDIT_INVALID);
            if ((effective & (HV_PAGE_WRITE | HV_PAGE_EXECUTE)) ==
                (HV_PAGE_WRITE | HV_PAGE_EXECUTE)) report->flags |= HV_PAGE_AUDIT_WX;
            return 1;
        }
        table = map(context, entry & address_mask);
        if (!table) return 0;
    }
    return 0;
}

void hv_page_audit_emit(const hv_page_audit_t* report, hv_page_audit_sink_t sink, void* context) {
#if defined(DBG) && DBG
    if (report && sink) sink(context, report);
#else
    (void)report;
    (void)sink;
    (void)context;
#endif
}

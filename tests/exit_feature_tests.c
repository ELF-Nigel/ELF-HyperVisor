#include <stdio.h>
#include <string.h>
#include "public/hv_exit_runtime.h"
#include "public/hv_page_audit.h"

#define CHECK(condition) do { if (!(condition)) { \
    printf("failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static void on_exit(void* context, uint32_t architecture, uint64_t reason) {
    unsigned* calls = (unsigned*)context;
    (void)architecture;
    (void)reason;
    ++*calls;
}

static int test_policy_and_sampling(void) {
    hv_exit_policy_t policy;
    hv_exit_cpu_stats_t cpus[2];
    unsigned calls = 0;
    hv_exit_policy_defaults(&policy);
    CHECK(hv_exit_policy_validate(&policy));
    policy.rules[0].architecture = HV_EXIT_VMX;
    policy.rules[0].reason = 10;
    policy.rules[0].action = HV_EXIT_DENY;
    policy.count = 1;
    CHECK(hv_exit_policy_validate(&policy));
    CHECK(!hv_exit_policy_allows(&policy, HV_EXIT_VMX, 10));
    CHECK(hv_exit_policy_allows(&policy, HV_EXIT_SVM, 10));
    CHECK(hv_exit_policy_allows(&policy, HV_EXIT_SVM, 0x400));
    CHECK(!hv_exit_policy_allows(&policy, HV_EXIT_SVM, 0x1000a));
    CHECK(!hv_exit_policy_allows(&policy, HV_EXIT_VMX, (1ull << 31) | 10));
    CHECK(!hv_exit_policy_allows(&policy, HV_EXIT_VMX, (1ull << 32) | 10));
    CHECK(!hv_exit_policy_allows(&policy, 0, 10));
    CHECK(!hv_exit_policy_allows(NULL, HV_EXIT_VMX, 10));
    policy.rules[1] = policy.rules[0];
    policy.count = 2;
    CHECK(!hv_exit_policy_validate(&policy));
    policy.count = HV_EXIT_POLICY_MAX + 1;
    CHECK(!hv_exit_policy_validate(&policy));
    policy.count = 1;
    CHECK(hv_exit_stats_configure(&cpus[0], 100));
    CHECK(hv_exit_stats_configure(&cpus[1], 200));
    CHECK(!hv_exit_process(&policy, &cpus[0], HV_EXIT_VMX, 10, 0, on_exit, &calls));
    CHECK(calls == 0 && cpus[0].published.denied == 1);
    CHECK(hv_exit_process(&policy, &cpus[0], HV_EXIT_VMX, 16, 999, on_exit, &calls));
    CHECK(cpus[0].published.observed == 1 && cpus[0].current.observed == 2);
    CHECK(hv_exit_process(&policy, &cpus[0], HV_EXIT_VMX, 16, 1000, on_exit, &calls));
    CHECK(cpus[0].published.observed == 3 && cpus[0].published.allowed == 2);
    CHECK(cpus[0].published.publications == 2 && calls == 2);
    CHECK(hv_exit_process(&policy, &cpus[1], HV_EXIT_SVM, 0x72, 0, on_exit, &calls));
    CHECK(hv_exit_process(&policy, &cpus[1], HV_EXIT_SVM, 0x400, 1999, on_exit, &calls));
    CHECK(cpus[1].published.observed == 1 && cpus[0].current.observed == 3);
    CHECK(hv_exit_process(&policy, &cpus[1], HV_EXIT_SVM, 0x400, 2000, on_exit, &calls));
    CHECK(cpus[1].published.observed == 3 && cpus[1].published.last_reason == 0x400);
    CHECK(hv_exit_process(&policy, &cpus[1], HV_EXIT_SVM, 0x72, 1, on_exit, &calls));
    CHECK(cpus[1].published.observed == 4);
    CHECK(hv_exit_stats_configure(&cpus[0], 0));
    CHECK(hv_exit_process(&policy, &cpus[0], HV_EXIT_VMX, 16, 0, NULL, NULL));
    CHECK(hv_exit_process(&policy, &cpus[0], HV_EXIT_VMX, 16, 0, NULL, NULL));
    CHECK(cpus[0].published.publications == 2);
    CHECK(!hv_exit_stats_configure(&cpus[0], HV_EXIT_SAMPLE_MAX_US + 1));
    CHECK(!hv_exit_stats_configure(NULL, 0));
    policy.default_action = HV_EXIT_DENY;
    policy.rules[0].action = HV_EXIT_ALLOW;
    CHECK(hv_exit_policy_allows(&policy, HV_EXIT_VMX, 10));
    CHECK(!hv_exit_policy_allows(&policy, HV_EXIT_VMX, 16));
    CHECK(hv_exit_selftest(HV_SELFTEST_OFF) == 1);
    CHECK(hv_exit_selftest(HV_SELFTEST_DISPATCH) == 1);
    CHECK(hv_exit_selftest(HV_SELFTEST_HARDWARE) == -1);
    CHECK(hv_exit_selftest(99) == 0);
    return 0;
}

static uint64_t tables[4][512];

static const uint64_t* map_page(void* context, uint64_t address) {
    (void)context;
    if (address < 0x1000 || address > 0x3000 || (address & 0xfff)) return NULL;
    return tables[address >> 12];
}

static void prepare_tables(void) {
    memset(tables, 0, sizeof(tables));
    tables[0][0] = 0x1000 | 7;
    tables[1][0] = 0x2000 | 7;
    tables[2][0] = 0x3000 | 7;
    tables[3][0] = 0x5000 | 7;
}

static void audit_sink(void* context, const hv_page_audit_t* report) {
    unsigned* calls = (unsigned*)context;
    if (report->present) ++*calls;
}

static int test_audit(void) {
    hv_page_audit_t report;
    unsigned calls = 0;
    prepare_tables();
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 123, map_page, NULL, &report));
    CHECK(report.present && report.page_size == 4096 && report.permissions == 7);
    CHECK(report.flags & HV_PAGE_AUDIT_WX);
    hv_page_audit_emit(&report, audit_sink, &calls);
#if defined(DBG) && DBG
    CHECK(calls == 1);
#else
    CHECK(calls == 0);
#endif
    tables[0][0] &= ~2ull;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 123, map_page, NULL, &report));
    CHECK(report.present && report.permissions == 5 && !report.flags);
    prepare_tables();
    tables[0][0] |= 1ull << 63;
    CHECK(hv_page_audit_walk(HV_EXIT_SVM, tables[0], 123, map_page, NULL, &report));
    CHECK(report.present && report.permissions == 3);
    tables[1][0] &= ~2ull;
    CHECK(hv_page_audit_walk(HV_EXIT_SVM, tables[0], 123, map_page, NULL, &report));
    CHECK(report.permissions == 1);
    prepare_tables();
    tables[1][0] = 0x40000000 | 0x87;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0x12345678, map_page, NULL, &report));
    CHECK(report.present && report.page_size == (1ull << 30));
    tables[1][0] |= 0x1000;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0, map_page, NULL, &report));
    CHECK(!report.present && (report.flags & HV_PAGE_AUDIT_INVALID));
    CHECK(hv_page_audit_walk(HV_EXIT_SVM, tables[0], 0, map_page, NULL, &report));
    CHECK(report.present);
    prepare_tables();
    tables[2][0] = 0x200000 | 0x87;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0, map_page, NULL, &report));
    CHECK(report.present && report.page_size == (1ull << 21));
    prepare_tables();
    tables[3][0] = 0;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0, map_page, NULL, &report));
    CHECK(!report.present);
    tables[3][0] = 2;
    CHECK(hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0, map_page, NULL, &report));
    CHECK(!report.present && (report.flags & HV_PAGE_AUDIT_INVALID));
    prepare_tables();
    tables[0][0] = 0x9000 | 7;
    CHECK(!hv_page_audit_walk(HV_EXIT_VMX, tables[0], 0, map_page, NULL, &report));
    CHECK(!hv_page_audit_walk(HV_EXIT_VMX, tables[0], 1ull << 48, map_page, NULL, &report));
    CHECK(!hv_page_audit_walk(HV_EXIT_VMX, NULL, 0, map_page, NULL, &report));
    CHECK(!hv_page_audit_walk(0, tables[0], 0, map_page, NULL, &report));
    return 0;
}

int main(void) {
    if (test_policy_and_sampling() || test_audit()) return 1;
    puts("exit policy, sampling, smoke test, and page audit tests passed");
    return 0;
}

#include <stdio.h>
#include <string.h>
#include "public/hv_exit_runtime.h"

int main(int argc, char** argv) {
    uint32_t mode = HV_SELFTEST_DISPATCH;
    int result;
    if (argc == 2 && strcmp(argv[1], "--hardware") == 0) mode = HV_SELFTEST_HARDWARE;
    else if (argc != 1) {
        puts("usage: hvselftest [--hardware]");
        return 2;
    }
    result = hv_exit_selftest(mode);
    if (result < 0) {
        puts("hardware smoke test unsupported: guest entry and resume must be supplied by the driver");
        return 2;
    }
    puts(result ? "software vmexit smoke test passed; hardware not tested" : "software vmexit smoke test failed");
    return result ? 0 : 1;
}

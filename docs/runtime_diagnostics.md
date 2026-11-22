# runtime diagnostics

## trace compression

`hv_trace_read_encoded` takes `HV_TRACE_ENCODING_RAW` or
`HV_TRACE_ENCODING_DELTA`. stop trace producers before taking a snapshot.
the live ring and version 2 shared trace section keep their existing layout.
compression applies to snapshots, without adding work to the vmexit path.

the output starts with `hv_trace_stream_header_t` (four little-endian uint32s):
version 1, encoding, event count, and payload byte count. raw records use the
existing x64 `hv_trace_event_t` layout (24 bytes). delta records contain three
unsigned leb128 values: timestamp delta, reason, and cpu index. start the
timestamp accumulator at zero and add deltas modulo 2^64; this preserves
out-of-order timestamps and wraparound. `hv_trace_decode_event` reads a record.
the first record in each snapshot is independent of earlier snapshots.

allocate at least `sizeof(hv_trace_stream_header_t) + capacity * 25` bytes for
delta output, checking size arithmetic. worst-case deltas can expand a record.
`written` stays zero on failure; discard partial output. successful snapshots
contain only recorded events, in ring order, including after ring wrap.

## resolver whitelist

`hv_resolver_resolve` hashes the supplied image with sha-256 through windows
cng before scanning any patterns. each `hv_module_signature_t` pins the exact
image bytes plus a pattern and a mask (`x` for exact bytes, `?` for wildcards).
there are no default approved hashes. unknown or modified images are denied;
missing and ambiguous matches return distinct errors. duplicate policy entries
for the same offset do not create an ambiguity. output is a bounded offset,
never an executable pointer. errors clear the offset.

this policy pins image hashes, not authenticode publishers. hashes must match
the supplied representation: disk images and relocated loaded modules differ.
the caller supplies trusted policy and stable, readable image/pattern/mask
buffers; mask and pattern buffers must contain `pattern_size` bytes. keep the
image immutable from hashing through scanning. kernel calls require passive
level and `cng.lib`; usermode tests use `elfhv_usermode` and `bcrypt.lib`.

## cpu checks

vmx initialization validates the vmcs size, write-back memory type, and reserved
revision bit in `IA32_VMX_BASIC`, then checks the saved revision before vmxon
and vmcs loading. svm validates cpuid leaf 0x8000000a, revision 1, usable asids,
and npt support when enabled. a vmcb has no vmcs-style revision header;
the check uses the cpu-reported svm revision instead.

init pins the thread to each processor using windows processor indices before
enabling virtualization. each cpu stores its vendor, feature mask, and xor
difference from cpu 0. the log reports mismatches and the common mask. mixed
vendors or feature masks fail init before any cpu enters virtualization.
ept/vpid come from vmx capability msrs, and npt from amd cpuid.

## shutdown and verification

trace, stats, and gpa-map views are wiped with `RtlSecureZeroMemory` over their
mapped sizes before unmapping and closing. the private trace ring is also wiped.
stop and join all trace/stat/map producers before shutdown; wiping cannot erase
copies a consumer has already made. shutdown of shared sections requires passive
level. repeated section shutdowns are safe.

ci compiles and runs the codec, version checks, feature comparison, and actual
resolver code as usermode tests. it also builds the controller and efi image.
this is not a kernel driver build or a hardware virtualization test. the repo's
driver remains a skeleton and needs a wdk build and dedicated machine validation.

references: [intel vmx architecture](https://cdrdv2-public.intel.com/843859/332831-sdm-vol-3d-dec-24.pdf),
[secure zeroing](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlsecurezeromemory).

## optional exit controls

the existing kernel skeleton and handler extension points remain in place.
`hv_init_ex` accepts a separate `hv_runtime_config_t`; `hv_init` uses defaults.
the efi handoff format has not changed. configuration and policy are copied at
init; changes require stopping producers, shutting down, and initializing again.
the current backend is a single hypervisor instance.

```c
hv_runtime_config_t config;
hv_exit_policy_t policy;
hv_runtime_config_defaults(&config);
hv_exit_policy_defaults(&policy);
config.selftest_mode = HV_SELFTEST_DISPATCH;
config.stats_interval_us = 1000;
policy.rules[0] = (hv_exit_rule_t){ HV_EXIT_VMX, 10, HV_EXIT_DENY };
policy.count = 1;
config.exit_policy = &policy;
/* call hv_init_ex with this config from your driver. */
```

policy entries use native reasons with an explicit vmx/svm architecture, and
deny skips both registered handlers and fallback routing at the architecture
entry points. this filters dispatch, not hardware interception: a denied exit
still happened. driver authors must decide whether to stop the guest, inject a
fault, or emulate it; blindly resuming can repeat the exit. direct calls to the
old routing helpers do not pass through the architecture-entry policy gate.
duplicate rules and invalid configuration are rejected. the default allows
ordinary reasons; vmx entry failures and oversized native reason values fail
closed. the existing skeleton's reason constants and handlers are preserved.

`stats_interval_us` defaults to 1000; zero publishes on every exit. an optional
`cpu_stats_intervals_us` array must have one entry per windows processor index.
the upper limit is 60 seconds. each cpu counts every exit, including denials,
and publishes cumulative counters on its first exit and on the first exit after
the interval. idle cpus have no timer-driven publications. `hv_vmexit_cpu_sample`
reads a published snapshot after the caller quiesces producers. the existing
aggregate statistics and shared section layout are unchanged.

`hvselftest.exe` runs a software cpuid exit smoke test for both architectures
through the same policy/sampling code used by the driver, with a test callback.
it does not execute a guest or touch live policy/counters. `--hardware`, or
`HV_SELFTEST_HARDWARE` at init, reports unsupported rather than claiming a
hardware vmexit. a driver must supply real guest entry/resume to support that.

debug builds (`DBG=1`) audit existing ept tables on permission requests and npt
tables after permission updates. `hv_debug_audit_page` can also be called by
driver code while tables are stable. the read-only walker supports 4 kb, 2 mb,
and 1 gb leaves, combines parent permissions, and flags writable/executable
pages and invalid entry combinations. it assumes four-level tables and readable
page-table mappings; it is a diagnostic, not a hardware validation substitute.
release builds omit the kernel logger and its automatic table walks. no ept/npt
mapping, invalidation, guest-entry, or guest-resume stubs have been replaced.

ci builds the existing usermode/efi targets, runs the new production core tests
in debug and release modes, and uploads `hvselftest.exe` with the tools artifact.
kernel glue and hardware behavior still require the driver's own wdk and machine
tests. the tests do not claim a complete kernel driver build.

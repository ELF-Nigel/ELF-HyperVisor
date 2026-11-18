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

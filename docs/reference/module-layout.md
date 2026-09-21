# Dynamic Module Layout

Reference: ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9`,
OpenHarmony-v7.0-Release,
`ecmascript/module/accessor/module_data_accessor.cpp` (Apache-2.0).

ReadModuleInfo takes a direct module entity offset. It retains the leading U32
word without interpreting it as a length: the pinned constructor skips it.
The following sections each start with a U32 entry count:

| Section | Entry encoding, in order |
| --- | --- |
| Module requests | U32 string entity |
| Regular imports | U32 local name, U32 imported name, U16 request index |
| Namespace imports | U32 local name, U16 request index |
| Local exports | U32 local name, U32 exported name |
| Indirect exports | U32 exported name, U32 imported name, U16 request index |
| Star exports | U16 request index |

Offsets and request indexes are distinct strong types. Every entity reference
is range-checked and every request index must address the retained request
vector. String content remains lazy. The reader preserves section and entry
order, including duplicates; it does not bind names or reconstruct source yet.

In EnumerateLocalExportEntry the runtime increments localIndex when the decoded
local name differs from the immediately preceding local name. A future binding
pass must compare actual string values and preserve this adjacency rule, not
deduplicate all local names or compare entity offsets alone. Namespace imports
receive a runtime star-name marker, but this reader represents them by their
dedicated entry type instead of a semantic string sentinel.

Each section validates the encoded count against remaining bytes before
allocation. All retained vector capacities share one cumulative budget; vectors
are reserved once. Total time/storage is linear in the number of entries.
Typed errors distinguish malformed ranges, request indexes, truncation,
unsupported profiles, changed files and resource exhaustion. Existing
stat-based immutability checks apply before/after the read.

Tests cover all six sections, field ordering, complete truncation sweeps,
low/high-byte out-of-range request indexes in all four referencing sections,
exact cumulative budget limits, empty modules and 200/201/1024/8192 requests.
Historical dynamic profiles use this pinned layout policy; fixture success is
not independent historical runtime certification. Record-to-module discovery,
string validation, binding recovery and full-corpus module checks remain.

Verification after integration: Debug CTest 25/25 and ASan/UBSan CTest 19/19
passed. No full-corpus module-layout claim is made yet.

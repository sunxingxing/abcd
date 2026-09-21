# Initial Method Attributes

Reference is the pinned `OpenHarmony-v7.0-Release` runtime_core
`libpandafile/file_items.h`, `index_accessor.h`, and ets_runtime
`ecmascript/jspandafile/{panda_file_translator.cpp,js_pandafile.cpp}`.

The encoded method access word contains low-eight access bits, eight function
kind bits, then a sixteen-bit pool-header index. `DecodeMethodAttributes`
separates these into typed fields without allocation. It retains the encoded
region index but does not validate it against a file or replace existing
regional reference resolution.

The runtime strips bit 3 (sendable) from the kind byte and maps base values:

| Encoded Base | Initial Runtime Kind |
| --- | --- |
| 0 | None |
| 1 | Base constructor |
| 2 | Arrow |
| 3 | Generator |
| 4 | Async |
| 5 | Async generator |
| 6 | Async arrow |
| 7 | Concurrent |

Higher base values fail explicitly. The decoder preserves all sixteen
combinations of these base values with the sendable bit, as the runtime mapping
does; acceptance does not establish that a source producer should emit every
combination or that JS alone represents sendable/concurrent behavior.

The new-version translator obtains this kind from `IndexAccessor`; old-version
translation instead considers bytecode creation sites and constant-pool types.
`JSPandaFile` defines new-version as file version greater than `{0,0,0,2}`.
The six supported dynamic container profiles all satisfy that comparison in
the pinned runtime. Historical-profile acceptance is not a proof that the
original historical engine treated every kind identically.

This metadata is an initial runtime classification, not a complete source
function specification. In particular None does not authorize arbitrary normal
function syntax. Creation-site processing, class/method semantics, lexical this,
constructibility, function length and async/generator protocols still require
recovery before a semantically faithful wrapper can be emitted.

Tests exhaust all 256 kind-byte values across all six supported profiles and
region indices 0/1/200/201/65535, preserving access bits and rejecting unknown
kind/profile values. `abc_frames` reports successful methods by `FunctionKind`
enum order in `function_kinds`, and counts sendable methods separately. Other
inspection stages leave these counters zero because they do not exercise this
classification.

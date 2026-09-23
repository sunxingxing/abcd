# Record module context checkpoint (2026-09-23)

`ReadRecordModuleContext` now reads merged-record attributes and the module
table once per record. A module handle is available only when both the open
ABC object and indexed method-owner offset match. Shared ownership keeps the
table alive after the context is moved or released; the caller must still keep
the file, header and index alive for deferred method work. CommonJS records
and bundle ownership without a runtime descriptor are typed refusals, not
silently treated as ESM.

The optional class fixture probe streamed one context per record across the
five paired dynamic ABCs. It did not use sidecar source as an engine input.

| ABC | Module records | Class sites | Ready with context |
| --- | ---: | ---: | ---: |
| Immersive/modules.abc | 35 | 41 | 4 |
| MultiCommunityApplication/modules.abc | 45 | 32 | 6 |
| pasteboard/modules.abc | 10 | 11 | 1 |
| push-kit-sample-code-clientdemo-arkts/modules.abc | 15 | 16 | 1 |
| push-kit-sample-code-clientdemo-arkts/widgets.abc | 1 | 1 | 0 |

MultiCommunityApplication improved from 5/32 without module context to 6/32
with it. Its record 44106, class site 44256:9, produced an explicitly partial
module fixture with a real `TabBarsInfo` import and a complete admitted class
expression. `node --check` passed. An isolated Node VM module oracle supplied a
controlled import and verified constructor assignment and `getTabList()`
return the same imported object. The opt-in CTest is enabled with
`-DABCD_PAIRED_CORPUS_ROOT=/home/rabbit/sample/abc_with_code`.

The module-aware MultiCommunityApplication probe used 9,856 KiB peak RSS;
Immersive used 9,344 KiB. These are individual fixture processes under a
1,000,000,000-byte address-space limit, not full-corpus peak RSS evidence.
The partial fixture is not a recovered record program: top-level execution,
exports, globals, complete signatures, async behavior and unsupported bodies
remain unresolved. The bundle and CommonJS refusals are also still outside
the requested complete dynamic-corpus coverage.

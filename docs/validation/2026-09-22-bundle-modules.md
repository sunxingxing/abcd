# Bundle Module Validation

Implementation: `ccf1c74`. Reference: pinned OpenHarmony-v7.0-Release.
Evidence: `tests/corpus/bundle-modules.json`. Input classification manifest is
unchanged from the preceding merged-module validation (900 paths, 402 dynamic,
498 cached static exclusions). Both roots are `/home/rabbit/sample/abc` and
`/home/rabbit/sample/abc_with_code`.

Release `abc_modules` without a bundle descriptor exits 1: all 289 merged files
and 73 descriptor-independent bundles pass; 40 bundles report the typed missing
descriptor diagnostic (kind 1 in this implementation). No other failures occur.
It checks 275,964 runtime records, with 149,815,296 bytes peak RSS in 35.94 s.

Repeating with `--bundle-descriptor ''` before MANIFEST exits 0. This is an
explicit conditional experiment, NOT evidence of the descriptor used by the
original application. It checks 276,004 runtime records and 274,395 module
tables, with 1,316,277 requests, 1,675,371 imports, 652,982 exports and 5,946,211
string reads. Peak RSS is 149,905,408 bytes in 32.63 s. Both runs enforce a
1,000,000,000-byte address-space limit. The second run benefits from warm input
cache; elapsed times are not an algorithm speed comparison.

Relative to merged-only tables, the conditional run adds 40 modules, one request,
one import, 39 exports and 81 string reads. The remaining 73 bundles have no
module fields whose selection depends on the runtime descriptor.

Debug and Release CTest suites pass 27/27; the final descriptor-reporting test
was rerun in Debug and included in the final complete Release suite. Fixtures
cover descriptor-independent success, missing-descriptor rejection, explicit
selection and invalid selected fields. No library code changed in this increment.

Module discovery is not decompilation. Runtime descriptor provenance, historical
semantic equivalence, module binding, request phases, instruction semantics and
per-record JS/TS/ArkTS generation remain unverified or unimplemented. Preserve
the strict failure result alongside the conditional success result.

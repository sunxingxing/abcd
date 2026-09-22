# Debug Entity Parsing

## Scope

Lazy DebugInfo accessor and structural JSON serialization. Preserves line start,
nullable parameter-name references, bounded constant-pool range, and resolved
global line-program reference. Does not execute line programs or infer source
signatures/types/local scopes from parameter counts or names.

## Verification

- Failing accessor build observed before implementation; structural integration
  assertion also failed on the preceding export because debug output was absent.
- Debug warnings-as-errors build succeeded; complete suite 87/87, 26.41 s.
- Additional boundary-test changes: targeted Debug 3/3, 0.15 s;
  ASan/UBSan debug_info_test and module_writer_test 2/2, 0.23 s.
- Tests cover every fixture truncation, malformed ULEB, unsupported profile,
  parameter reference, pool extent, index count/offset/alignment, program
  references at zero/EOF/overflow, exact/short storage budgets, and
  0/200/201/8192 parameters. Writer distinguishes named, empty and absent names.
- Independent schema/bounds review found no correctness issue; its suggested
  boundary cases were added. `git diff --check` clean.

## Real Inputs

Five paired ABCs exported to `build/structure-debug/`. TypeScript 5.9.3 syntax
and manifest/JSON checks passed for all 152 record declarations; counts remain
2111 methods, 626 raw metadata fields, 241 requests, 276 imports, 139 exports.
All 2111 methods have debug entities, but their parameter-name tables contain
zero non-null names. No original parameter names are claimed for these samples.

External peak RSS: Immersive 8448 KiB, MultiCommunityApplication 8576 KiB,
pasteboard 8320 KiB, push modules 8448 KiB, push widgets 8320 KiB. Export times
0.05/0.06/0.01/0.02/0.00 s respectively, under CLI 1,000,000,000-byte RLIMIT_AS.
These measurements concern structural export of paired inputs only.

Next structural work: line-program local scopes/source positions, class literal
layouts/ownership, global bindings and evidence-backed source signatures.
Function-body expansion remains deferred. Static corpus cache unchanged.

# Module Read Binding

This is the metadata-binding prerequisite for the shared source route, not a
second body writer or a module source-publication certificate.

## Pipeline

```text
ABC module loads -> original semantic IR -> verified working ValueModuleRead
record-owned ModuleInfo -----------------> ResolveModuleReads
                                          -> ModuleReads in FileFunction
                                          -> future module scope/TDZ proof
                                          -> shared SourceTree and printer
```

The first two arrows and metadata binding are implemented. Conditional module
scope aliases now exist in SourceTree, but read lowering, TDZ correspondence and
record binding remain open. Default `ReadFunctionValues` leaves binding
obligations open; an explicit `RecordModuleContext` enables resolution and
retains the typed result alongside the working graph. Family/record callers
must reuse the same immutable record context, not rescan all records per child.
The diagnostic CLI resolves one context for its single requested function.

`BuildFunctionValues` additionally rebases original guarded-load proofs into
`ModuleGuards`, before environment/cell preparation. This applies to closed and
conditional ordinary functions alike. The verifier follows original instruction
backlinks and checks exact working load/guard kinds, original PC/provenance,
space/slot, diagnostic-name pool slot, SSA result/guard operand, effects and
same-block adjacency. It does not infer guard redundancy from a Language domain:
that domain can itself depend on the original two-instruction lowering premise.
The O(P) pair pass uses indexed origin lookup and O(P) retained storage, where P
is original module-proof count; verification uses constant additional storage.
Guarded, initialized, caller-guarded and catch-guarded proofs remain distinct;
only the first is rebased by the current normal-flow API. Missing proof entries
remain missing permission, even when a raw unused module read survives in IR.
Both IR/CFG revision changes invalidate the table. The standalone ValueMethod
receipt wrapper validates the correspondence but does not retain these private
plans or gain module-print permission.

## Identity and Semantics

- Binding identity is record owner plus `ModuleSpace` and `ModuleSlot`, never
  a source name, pool index or raw local-export row index.
- The pinned v7 `ModuleDataAccessor::EnumerateImportEntry` places regular
  import entries before namespace-import entries. Preserve the selected request
  ID, specifier entity and local/imported name entities; never reorder slots by
  dependency request order. Regular imported-name `*` is still a wire name here;
  source import classification must decode it at the textual boundary later.
- `EnumerateLocalExportEntry` assigns the same slot to adjacent equal decoded
  local names, including names stored at different string entities. Nonadjacent
  equal names remain different slots. The representative name entity is naming
  evidence only; it must not replace space/slot identity.
- Keep original read operations, domains, effects, exceptional completions and
  origins unchanged. Resolution neither snapshots live bindings nor eliminates
  initialization/Hole guards. String payload validation for external names and
  dependency resolution are still downstream obligations.
- The mutable result records working revisions and lives under `FileFunction`'s
  file/method/record identity. It cannot be reused after graph mutation or passed
  to the printer as a certificate. Standalone resolver callers must establish
  same-file metadata and verified-IR preconditions themselves.

Missing module metadata, out-of-range slots and invalid selected requests fail
with typed errors. Slot/request failures retain the operation's original PC.
No unresolved read is silently converted into a free global lookup.

## Complexity and Budgets

Let I be working operations, L local-export rows, U distinct local-name entities,
S their decoded UTF-16 units, and C units examined in adjacent-name comparisons.
External-only resolution is O(I), with O(reads) output. When local reads exist,
resolution is O(I + L log(L+1) + S + C): sort/deduplicate entity IDs, decode each
entity once, and traverse the original row order with indexed name lookup.
It does not compare every export pair. Temporary/output vector capacities and
name payloads share the byte allowance; comparison and traversal work is capped.
Returned allocation accounting conservatively includes temporary storage too.

The record context and existing graph are caller-admitted. Existing metadata
readers and function preparation passes retain their own bounds; this change
does not claim a shared cumulative family-work budget or whole-process RSS proof.

## Remaining Gates

`BuildEvaluationPlan` accepts explicit conditional `ModuleEvaluation` entries.
Its indexed checker requires an exact census of live module reads, matching
space/slot and an adjacent same-block static-name guard consuming the read's
SSA result. Default preparation without the table still refuses module reads.
Initialized and exceptional/caller-guarded forms are not admitted by this API.
The read stays pinned at its original root; the guard has `kModuleGuard`
placement, not `kRemoved`. Evaluation verification consumes both original
ordered events together, checking their separate imported effects rather than
adding the guard to the pure-operation skip set. No IR operation is deleted.

Source-use projection excludes exactly the absorbed guard dependency from the
read's verified linked use list, in O(1). Guard-only reads retain their throwing
effect but need no unused source snapshot. Real value uses, including a use
after a call that may update the live binding, still retain a snapshot. The
table is a source-binding obligation, not proof that its `ModuleSymbol` matches
record metadata; original `ModuleGuards` and record correspondence must be
checked independently before publication. The source wrapper explicitly rejects
nonempty conditional module evaluation tables. Shared expression/statement
construction and combined PC origins are the next integration step, not yet
implemented by conditional evaluation alone.

`FunctionSyntax.modules` constructs aliases to typed source module symbols in
an outer module scope. Captures, parameters and block declarations use the same
scope activation/checking machinery; module aliases do not produce function-local
declarations. Read-only aliases cannot become assignment targets, while mutable
aliases are distinct from snapshots. The standalone printer/publication wrapper
still rejects all module aliases. `module_scope_test.cc` contains unrun source
specifications for ordinary/captured functions, scope nesting/shadowing, duplicate
targets, mutable/read-only writes, source-operand mismatch and 200/201 sizes.

Before source emission, bind these aliases to actual record module symbols, prove unshadowed uses
and live-binding behavior, preserve cyclic module initialization/TDZ and thrown
values, and discharge record dependencies. Only then may shared expression
construction/verification accept module reads. Source evidence rules remain 23.

Regression source `module_reads_test.cc` covers slot/table separation, adjacent
aliases across different string entities, nonadjacent identical names,
namespace imports, missing metadata, request/slot bounds, original-PC errors,
removed operations, resource/work refusal and 200/201/geometric sizes. Tests
are uncompiled/unrun under the current pause; no behavior or RSS claim follows.
`module_guards_test.cc` adds actual bytecode-to-working-IR pairs (narrow/wide,
local/external), removed/changed guards, wrong SSA input, reordered pairs,
changed PC/name/slot/effects/revisions, missing pair coverage, unguarded reads,
budget boundaries and 200/201/geometric counts. It also remains uncompiled/unrun.
`module_evaluation_test.cc` specifies repeated local/external pairs with calls
between snapshot and use, guard-only reads without unused snapshots, census,
slot/guard/effect mutation and resource/work boundaries at 200/201/geometric
sizes. This regression source is also uncompiled/unrun.

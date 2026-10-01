# Module Read Binding

This is the metadata-binding prerequisite for the shared source route, not a
second body writer or a module source-publication certificate.

## Pipeline

```text
ABC module loads -> original semantic IR -> verified working ValueModuleRead
record-owned ModuleInfo -----------------> ResolveModuleReads
                                          -> ModuleReads in FileFunction
original guarded/initialized reads ------> BindModuleAliases
                                          -> evaluation + shared SourceTree
                                          -> VerifyModuleAliasTree
                                          -> future record/TDZ proof + printer
```

The first two arrows and metadata binding are implemented. Conditional module
scope aliases and guarded-read lowering now exist in SourceTree. Metadata-name
binding is implemented below; TDZ correspondence and complete enclosing record
ownership remain open. Default `ReadFunctionValues` leaves binding
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
normal initialized reads now use the occurrence premise below. Missing proof entries
remain missing permission, even when a raw unused module read survives in IR.
Both IR/CFG revision changes invalidate the table. The standalone ValueMethod
receipt wrapper validates the correspondence but does not retain these private
plans or gain module-print permission.

## Initialization before a normal child creation

`FunctionInput` indexes the earliest Language-valued module store for each slot
in its normal entry block. That block dominates reachable execution. A query
requires an actual later function-creation instruction; stores after that
creation, branch-local stores and stores beyond the entry block are excluded.
For E entry instructions and S stores, preparation uses O(E + S log(S+1)) time
and O(S) retained storage. Child reads query this index in O(log(S+1)), without
rescanning the parent body for each child or each read.

`ParentModuleInput` is borrowed only during child preparation. File discovery
supplies the genuine owner context and checks file/profile/record, creator,
target, PC and original creation operands before using the premise. The child
retains typed original read/slot/store/creation identities. An adjacent original
Hole guard keeps its existing guarded path, even when initialization is known.
Guard-free initialization currently applies only across one normal parent edge;
phased and inherited grandparent premises are not inferred.

`ModuleReadSite` has an optional guard. Absence means an initialized local read
with an actual input premise, not permission to delete a guard. Alias binding
resolves its record slot without inventing a diagnostic-name pool reference.
The shared evaluation, expression forest and TreeFlow retain one original effect
and one read origin; guarded reads retain both original events and origins.
Generated reads remain native live-binding references, never value snapshots
shared across invocations.

Complete family write verification independently rechecks the original parent
store, its Language-valued SSA input and its order before the actual creation.
The existing capture/creation checks still establish the child target and edge.
A detached root with inherited initialization premises is refused; an input
premise does not certify module declarations or complete record publication.

The real pasteboard PageA entry 7283 stores slot 1 at PC11 before creating child
7061 at PC196; the child's raw local read is at PC44. The file family now reads
successfully. A decoded isolated read/return body using that same occurrence and
metadata reaches the common AST/printer and passes independent live-value tests.
It is a focused fixture, not a claim that the complete PageA record executes.
The record still fails at a different class-member family's evaluation stage.

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

`BindModuleAliases` connects the immutable input, original guarded-pair ledger,
verified working values and same-file `RecordModuleContext`. It rechecks the
original pair proof and resolves the record table afresh instead of trusting a
mutable `FileFunction.module_reads` cache. It requires an exact live-read/pair
census, then derives both `ModuleEvaluation` and `ModuleSyntax` rows. Source
symbol identity is canonical record binding order: all regular/namespace import
slots followed by local binding slots, never raw local-export row ordinals.
Sibling methods therefore agree even if their read subsets/order differ. The
caller still owns the record-to-`ModuleOwnerId` mapping; this numeric assignment
alone is not an enclosing-record certificate.

Pool slots and string entities are sorted/deduplicated before decoding. The
guard diagnostic and metadata local name must have equal decoded UTF-16 content;
distinct string entities with the same content are allowed. Content classes are
computed once so repeated long names are not compared per instruction. Names
retain exact metadata spelling. Invalid/shadowed/duplicate binding spellings
remain the shared scope check's responsibility, not a reason to invent aliases.
Memory includes transient resolution/name tables, decoded strings and returned
name copies. Work includes comparisons, decoding and copying. Beyond the existing
metadata resolver cost, index work is O(M log(M+1) + U log(R+1)), for M reads,
U distinct pool slots and R index regions; decoded payload and metered text-sort
comparison work are additional dimensions. Retained storage is O(M + S_out),
including copied name payload S_out; this does not admit or claim an unbounded
record/family cache.

All aliases from this read-only adapter have `ModuleAccess::kReadOnly`, including
locals. This limits operations through the alias; it does not freeze a live
binding or infer a `const` source declaration. A local-export row alone does
not prove declaration kind or legal writes. Store/record proof must establish
those facts before widening permission.

`VerifyModuleAliasTree` checks fresh unchanged binding evidence against the
actual evaluation targets and AST aliases, including input/method/revisions,
coverage, spelling, evidence, module scope and access. Alias lookup is indexed
by sorted source targets rather than depending on function-local SymbolIds.
It complements, not replaces, shared structure/flow/operand/origin checks. Like
capture-tree correspondence it requires those checks on the same unchanged tree.
It does not claim engine-specific thrown-error/message equality merely because
diagnostic names match, nor resolve TDZ/cyclic initialization or module requests.

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
nonempty conditional module evaluation tables.

The shared expression builder now resolves each conditional module target to
the supplied source alias by indexed lookup, emitting one `SymbolExpr` per
read occurrence. Distinct reads never share an expression or snapshot, even
when they read the same binding. The expression has a `CombinedOrigin` retaining
the original load and guard sites; both operation placements reference that one
expression. Statement construction emits it only at the pinned load position,
with a snapshot assignment only when there are real source value uses.

TreeFlow independently requires paired expression identity and checks original
statement order. Operand correspondence recovers the actual module alias from
the AST symbol and compares its source target to the evaluation obligation; a
free global name or unrelated parameter symbol is not equivalent. The existing
origin checker requires both original PCs to belong to the same actual root,
without reconstructing provenance from printed text. This completes conditional
IR/evaluation/AST correspondence for these pairs, not record/TDZ/name proof or
executable source publication. Printer-generated spans for module expressions
remain gated on the enclosing record certificate.

`FunctionSyntax.modules` constructs aliases to typed source module symbols in
an outer module scope. Captures, parameters and block declarations use the same
scope activation/checking machinery; module aliases do not produce function-local
declarations. Read-only aliases cannot become assignment targets, while mutable
aliases are distinct from snapshots. The standalone printer/publication wrapper
still rejects all module aliases. `module_scope_test.cc` contains unrun source
specifications for ordinary/captured functions, scope nesting/shadowing, duplicate
targets, mutable/read-only writes, source-operand mismatch and 200/201 sizes.

Before source emission, certify the enclosing record symbol inventory, prove
unshadowed uses and live-binding behavior, preserve cyclic module initialization/TDZ and thrown
values, and discharge record dependencies. Only then may conditional module
expressions be published as executable source. Source evidence rules remain 23.

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
sizes. It now also constructs/checks the shared AST, paired placements, distinct
module reads, retained snapshots and dual-PC origins, with negative mutations
for global-name substitution, target changes and dropped/duplicated provenance.
This regression source is also uncompiled/unrun.
`module_aliases_test.cc` adds serialized module/name fixtures and original
bytecode through metadata binding, shared evaluation/AST and alias-tree checking.
It specifies external/local/namespace bindings, adjacent export aliases, stable
sibling identity, nonzero name-arena bases, guard-name mismatch, changed AST
spelling/target/access, altered evaluation targets, revisions, resource/work
limits and 200/201/geometric sizes. It remains uncompiled/unrun.

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

## Guarded reads in selected expressions

Normal conditional and short-circuit arms can now own a verified read/guard pair
as part of their single evaluation subtree. Selection discovery builds one peer
index with the common exact census, adjacency, operand and effect checks. A guard
is admitted only with `kModuleGuard` placement and an inline or phi-input read in
that arm; arbitrary or separately rooted guards remain barriers.

Phi-expression admission excludes precisely the indexed guard use from the
read's original use chain, still requiring one real phi input and a single exit.
Only that adjacent guard and inert control may follow the producer. Both planning
and independent evaluation verification use the checked index; the original
read/guard effects and dual origins remain consumed by the ordinary subtree
verifier and TreeFlow. This extends the earlier pinned-read domain described
below, without changing phased admission or original alias/provenance checks.

The index adds O(I+M) work and O(I) temporary space for I operations and M module
evaluations. Peer lookup in each arm is O(1); eligible phi chains have at most the
guard and one source input. No per-selection module scan or runtime helper exists.
Decoded conditional/and/or fixtures cover direct values, member reads, adjacent
calls, 200/201 boundaries, TDZ, receiver and exception ordering; forged missing
guards and intervening debugger events remain rejected. Complete record exports
and platform behavior are separate acceptance layers.

## Initialization before a normal child creation

Rules 78/79 replace the entry-block initialization admission with sparse SSA over
all reachable normal control flow. Original module stores define structural TRUE
candidates; full unchanged input verification independently checks every store
operand as Language before any candidate grants permission. Incoming slots use
the immutable parent-entry premise.
Complete (unpruned) IDF placement represents joins without fabricating reads of
every slot at every child creation. An indexed reverse-phi worklist propagates
FALSE for Boolean AND, including zero-trip and cyclic paths. Dominator traversal
then shares persistent slot-set roots and records a snapshot at each genuine
function/class allocation. Instruction IDs index snapshots; numeric ID order
does not choose control-flow ownership.

A store in a joining block initializes subsequent creations. Stores on both
branch arms can establish the same fact at their join; a store on just one arm
cannot. A loop does not prove initialization merely by containing a store. The
analysis uses the owner's unchanged normal CFG; full semantic verification
discharges store obligations before permission is published. It does not omit
exception/resume edges or prove phased parent execution.

`ParentModuleInput` is borrowed only during child preparation. File discovery
supplies the genuine owner context and checks file/profile/record, creator,
target, PC and original creation operands before using the premise. Normal and
phased children retain the exact creation-time root; publication independently
rechecks its identity. An adjacent original Hole guard retains its guarded path.
Legacy direct-entry store backlinks remain optional provenance; their absence
no longer blocks a proved initialized slot beyond the entry block. Transparent
normal ancestors can pass the same persistent premise to their actual children.

`ModuleReadSite` has an optional guard. Absence means an initialized local read
with an actual input premise, not permission to delete a guard. Alias binding
resolves its record slot without inventing a diagnostic-name pool reference.
The shared evaluation, expression forest and TreeFlow retain one original effect
and one read origin; guarded reads retain both original events and origins.
Generated reads remain native live-binding references, never value snapshots
shared across invocations.

Complete family write verification independently rechecks the original parent
creation and its immutable initialization root. Optional direct-entry store
backlinks also retain their original Language-valued input/order checks.
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

Aliases from the default read-only request have `ModuleAccess::kReadOnly`, including
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


## Callable module writes (rules 77)

The normal and phased family routes now request mutable binding access and
inventory **actual retained `ValueModuleWrite` sites**, including write-only
functions with no module reads. The request alone grants no permission. The
binder rechecks original typed slot/effects/origin and the actual stored operand's
Language domain. Literal operands use the same pinned numeric value rules;
VM-private NaN encodings do not become Language merely because they are literals.

A write currently consumes the immutable `FunctionInput` / `FilePhasedFunction`
creation-time initialized-slot set. These are the same move-stable persistent
roots and genuine parent edges already used for initialized module reads. The
family publication check revalidates that root identity against the actual parent
creation even if the child has no reads. A slot may be written in any child block,
branch or exception region; no child block colocation or producer list is used.

The semantic reason for requiring initialization is pinned v7.0
`SourceTextModule::StoreModuleValue`: raw storage can create a slot's value array
and initialize Hole. Native assignment to an uninitialized lexical binding throws
ReferenceError. A metadata local-export row cannot erase that difference. Native
record declarations remain at the original initializer stores. Child aliases
stay `local=false`, in module scope; they use `let` access permission and ordinary
`AssignmentExpr`, never a new declaration or an initializer constant. Import
aliases stay read-only, and the actual record route retains its ordinary-module
admission (shared module export checks must not disappear).

Read and write destinations share the canonical external-then-local module
identity and one sorted alias inventory. Fresh `VerifyModuleAliasTree` checks
mutable aliases against the actual write inventory, rather than trusting an
access enum; changing both a syntax alias and its access table does not grant
writes without the write proof. Common TreeOperands independently checks the
original store against the actual assignment's symbol and RHS. Normal/phased
instruction, effect, scope and destination validation still run.

For I operations, R reads and W writes, the new inventory costs O(I) scans and
O((R+W) log(R+W+1)) sorting, with O(R+W) scratch/retained records plus names. Entry
membership uses the existing fixed-width ModuleSlot trie, not a parent-body walk
for each write. Every scan, comparison, allocation and membership path is charged.

Rules 78 now proves normal parent initialization outside the entry block through
the common SSA/dominance mechanism above. Writers created before initialization
still need escape/invocation analysis. A successful child Hole guard can also
establish initialization, but its **successful Commit**, not its Before node,
must govern subsequent writes through catch paths. Rules 79 also connects
same-body raw local reads to these shared candidates. Record declarations first
initialized solely inside branch arms still require separate control/scope
integration. Do not widen these with fixed sequences, same-block conditions or
a count of guards/selections.

`module_writer_source_test` uses real encoded fixtures in profiles 1202/1206/1301
for parameter, branch, constant and catch writes, including parent initialization
after a branch join with both taken and bypassed branches. Independent native ESM checks
cover return/value identity, aliases and live exports, arity/name, no coercion,
initialization failures and cyclic TDZ. The early-created writer refuses with
`ModuleAliasError::kInitialization`; it is not counted as successful recovery.
Mutating a retained slot disagrees with its original store and fails coverage.

## Rules 78: initialization analysis cost and limits

This reuses RegisterSsa/IdfIndex and the existing dominator implementation (their
algorithm provenance and maintenance contracts are in algorithms.md); no new
third-party dependency or pattern recognizer is introduced. For I instructions,
S distinct stored slots, V abstract SSA values, P phi inputs, B blocks and C real
creations, scans and false propagation cost O(I+V+P+B+C), beyond sorting stores,
phis and creation identities and the indexed SSA/dominator construction cost.
Store-to-slot lookup costs O(log(S+1)). Sparse phi output V/P can itself be dense
for some CFGs; this is counted, admitted and work/byte bounded, not assumed linear
in I. No per-slot whole-CFG scan or S-by-C fabricated-read matrix is constructed.

Persistent set insertion follows ModuleSlot's fixed-width path; unchanged roots
share storage across blocks/creations/ancestors. Queries cost O(log(C+1)) plus
fixed-width slot membership. Accounting conservatively charges temporary SSA,
indexes, worklist, dominators and allocated trie nodes, as well as returned state
records. Exhaustion returns typed work/resource failure, never FALSE permission
or silently changed semantics. Unit fixtures exercise 200/201 and geometric
independent slot/creation dimensions, exact work exhaustion, unreachable writes,
before-store creation, diamonds and zero-trip/do-first loops. Work counters are
not a whole-process RSS or latency measurement.

## Rules 79: same-body reads share original and working state analysis

ModuleSlotSet and the sparse initialization algorithm now belong to analysis;
source has only a type include/alias, not a second implementation. The owner
adapter records before-site snapshots for real local module reads and function/
class allocations. An abstract site's own successful store affects later sites,
never its before snapshot. FunctionInput retains only the creation snapshots.

VerifySemanticSource replaces its entry-store admission with common all-path
candidates. Every original register use, including every store operand, must
subsequently pass Language verification. Unknown/Hole inputs therefore cannot
bootstrap initialization, even through read/store cycles. kStored keeps an
optional old direct-entry backlink as provenance; a join of stores on both arms
need not invent one dominating store. Adjacent encoded Hole guards retain their
existing guarded path and both original effects. Reads still use a fresh native
live binding, including after calls which change its value.

Build/VerifyModuleGuards rebuild the same initialization analysis on the CURRENT
normal ValueProgram. The adapter first verifies value availability/control, then
checks each live module write's actual stored operand and domain, slot and uses.
An original input candidate does not authorize moving/removing a working store.
Lookup at the actual read uses common sparse SSA and persistent snapshots; no
current_store.block==0 or same-block order condition remains for kStored.
Original read/store origins, effects and slot correspondence remain checked;
phased parents and successful child guard Commit facts are not inferred.

Both adapters add linear scans/storage and fixed-width persistent insertions on
top of the already documented SSA/dominator/sort/output costs. Working analysis
runs once per module-guard build/verification, not once per read. Candidate roots
and optional backlink indexes die before the original value solver's scratch;
working build roots also die before independent source verification reuses its
scratch allowance. Allocation/work admission remains typed and conservative,
with lexical work reserved before candidate analysis to prevent quota wrap.
No new dependency,
capability wrapper, interpreter helper or per-variable CFG walk is introduced.

Encoded tests cover joined/two-arm/one-arm stores, zero-trip/do-first loops,
unknown/Hole cycles, current-store removal, exact work/resource limits and shared
200/201/geometric dimensions. Full record source fixtures exercise raw reads
following parent branch joins in normal and catch-child families across three
profiles; independent ESM compares both branch directions, exports/live updates,
TDZ, name/arity, receiver, value/error identity and no coercion. First source RED
was the actual family refusal, not reference-source output or INCOMPLETE success.
Record declaration placement, host/numeric proofs and stage-one whole-corpus
acceptance remain distinct gates.

## Phased creators hand on their entry set (2026-10-23)

A phased parent (try regions, async, generator) cannot yet prove its own module
stores before a creation, so its children previously received no premise and
an unguarded local read refused as a possible Hole (`InternalValueReason::kMixed`).
The parent's own creation-time initialized set is still sound at every later
child creation: a module slot holds Hole only between instantiation and its
first store, and every family write is verified Language-valued, so a slot
never returns to Hole. `InheritedModuleInput` carries exactly that set plus
the original creator method and creation PC; both readers check them against
the child's owner context. Premises have no store backlink. Publication
(`VerifyInheritedModuleReads`) takes the creator/PC from the actual edge
context (async working bodies are normalized; their offsets are not original
PCs), requires the creator to be the phased parent, the child's entry set to
equal the parent's entry set, and every premise to name that creation.
Parent-local stores before the creation remain unproved (not inherited).
Fixture `tests/fixtures/isa/inherited_modules` (try/finally, async, generator
and class-method creators; live `var` writes); KikaInputMethod record 10284
completes.

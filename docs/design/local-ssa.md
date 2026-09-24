# Local cell SSA and constructor origins

## Model

`BuildLocalSsa` resolves ordinary lexical accesses through `AnalyzeEnvironments`,
indexes distinct `(EnvironmentId, LexicalSlot)` pairs, and reuses
`RegisterSsa::BuildSites` for sparse cell definitions, reads and phi inputs.
The register IDs in that effect program are cell-binding keys, not ABC frame
registers. Sites retain original instruction indices and PCs. No bytecode or
synthetic opcode is generated.

Only unchanged, forward-only normal graphs are admitted. Without re-entry, an
allocation site executes at most once; its cells can begin as unknown SSA entry
values rather than requiring one fabricated allocation write per slot. Incoming
captured cells are not promoted. A store definition refers to the actual VM
store input, while a load reads the reaching cell value, including phi merges.
This is not a proof that initial cells are Language or initialized.

## Escape boundaries

All operations must pass typed semantic lifting for the caller's profile.
Explicit current-environment loads are refused. Every reachable allocation's
accumulator definition must have no uses in the supplied original register SSA,
including direct reads, private copies and phi inputs. This deliberately
refuses even harmless environment copies until a separate escape proof exists.

The first function creation or object/array literal-buffer creation is retained
as a capture barrier. Buffers may contain method templates; absent decoded
literal evidence, they cannot be assumed noncapturing. Consumers must not infer
local identities for loads at or after that barrier unless the same-body
capture-preservation analysis supplies a certificate for that creation PC.
Certificates must be strictly ordered, match creation sites and be completely
consumed. See [capture preservation](capture-preservation.md). Local SSA records
explicit stores only, not unknown closure mutations.

## Constructor integration

`ProveRangeDerivedConstructor` joins the register and local SSA origin graphs:
register store-input to cell definition, cell phi inputs to cell phi, and cell
read to register load-result. Entry cells are `Other`, not undefined. Earlier
register snapshots remain snapshots after later cell writes. The existing
Undefined/SuperResult/Other lattice, single-super/dominance proof and guard
checks remain in force. Unsupported cell analysis retains the prior
register-only proof; resource exhaustion remains an explicit refusal.

This is separate from `ClosureWrites::kLanguage`: that contract does not prove
object identity or exclude undefined, and is not used for these super guards.

## Complexity and ownership

Environment resolution and cell indexing use the existing ancestry index and
sorting, O((I + A) log D + A log A), where I is instruction count, A lexical
accesses and D ancestry depth. The existing sparse SSA construction retains its
documented quota and phi-output-sensitive cost, not a quadratic per-cell CFG
walk. The merged origin graph uses O(V + E) storage and propagation work, with
at most two enqueue operations per value. Current SSA read/input lookups add
their logarithmic lookup cost during graph construction.

Cell SSA, live register SSA, the environment-use bitmap, graph scratch and
merged origin nodes/edges are separately charged while simultaneously live.
No dependencies or licensing changes are introduced. Runtime scaling and RSS
measurement remain deferred, not claimed from these complexity bounds.

## Current evidence

Both CLI targets build. Debugging the actual pasteboard record-output run shows
Before capture-preservation integration, Home constructor's first failing super
guard was at PC414, with origin `Other`.
The earlier checks, including PC92, now trace through the lexical this cell.
The first capture barrier is instruction 146, the object-buffer site at PC392.
PC412 reloads the cell after that barrier, so PC414 intentionally failed without
additional evidence. The subsequent preservation analysis certifies PC392 and
PC398, and the whole constructor origin proof now succeeds with super PC84.
Evidence is in `build/pasteboard-local-ssa-final-debug.txt` and
`build/pasteboard-source-local-ssa/manifest.json`.

The complete record manifest is unchanged. Tests, executable equivalence checks
and RSS tracking remain paused. Deferred regressions include same-value and
mixed phi joins, missing initial stores, distinct environments with equal slot
numbers, pre-/post-capture snapshots, method-bearing buffers, allocation-result
copies/phi uses, unreachable allocations, and memory/work boundaries.

# Parameter-initializer expression proof

The new path owns conditional initializer events in parameter scope. It does
not turn a body `if` into a default to repair `.length`, and it introduces no
source contract. The existing literal/captured/cell-backed paths retain their
priority and output. Only an unmet signature with no old direct projection
enters expression discovery.

`parameter_expression_discovery.cc` starts from argument SSA entries and their
strict undefined tests, follows single-entry straight-line header/missing-arm
regions and finds the initialized-parameter phi through original incoming edges.
Regions may span blocks. No operation-count limit, instruction adjacency,
register identity or same-block rule grants admission. A shared phi-input index
and visited-region census avoid per-parameter whole-graph searches.

`ParameterExpressionProof` seals the immutable original graph revision, argument
and initialized-value identities, undefined test, provided/missing edges,
ordered initializer events and original caller exits. Its separate verifier
reconstructs regions, ownership, event order by operand postorder, closed uses,
parameter order and exception coverage. A property getter remains an event,
not its base value. Each binding load/HoleGuard retains the original caller
exception and live binding identity. Later parameters, body-local cells,
receiver/callable/unknown entries, extra arm effects, shared/external entries,
escaping results and body catch destinations refuse. Incoming lexical storage
is established by the environment/cell planner, never by a plan-local name.

`DefaultBody` retains the original owner and produces a residual under
initialized parameters. Migrated events are tombstones with stable original
IDs; the residual is not represented as a stand-alone equivalent ABC body.
`SignatureString` distinguishes string literal pools from property/global name
pools. Module guards partition their original read/guard facts between signature
and residual and independently verify both; no module read or guard disappears.
Signature requirements go through the actual family capture/module bindings.

`ParameterExpressionSource` is sealed syntax correspondence. Its explicit-stack
verifier checks actual initializer operands, operators, constant meanings,
canonical keys, binding targets, roots and single consumption against fresh raw
proof inputs. The normal structure/ABI checks accept expression defaults only
with this owner. The first default's ordinal must equal the original length
metadata; the printer emits native defaults rather than changing `.length`.

OriginalFlow retains the same immutable owner. RecoveryFlow has argument,
missing decision, provided/missing commit, initialized parameter and per-op
Before/Commit/caller-exception ports. A guarded GetValue has a distinct TDZ stage
inside the source read, with the original guard identity. Every initializer is a
disjoint normal chain with only caller exits. The independent verifier checks
its event ranks, exact control edges, binding definitions, raw event coverage
and exception destinations on every recovery projection. A conservative raw
numeric-conversion exit may be discharged only by the existing source value-
domain proof (`kDomainImpossible`); getter/global/TDZ exits require a real caller
port. Internal/raw identities never acquire fabricated source ExprIds.

Discovery and raw event census are linear; sorted module/name lookup makes the
whole admitted path O((V+E) log(V+E)), with O(V+E) prepaid storage. Traversals use
explicit stacks. Byte and work exhaustion are typed refusals. The resource
charge includes retained immutable raw input, certificate, name strings, source
mapping and shared-owner control blocks, not only temporary scratch.

The implementation currently admits synchronous normal graphs whose initializer
is a uniquely consumed expression tree. Async/phased defaults, calls, allocations,
receiver/arguments/new-target defaults, body-local dependencies, shared expression
DAGs and unsupported storage transport remain typed refusals. Existing accepted
plans are not retried or rebuilt after verifier failure.

## Class-self enclosure consequence

After member ABI becomes valid, a multi-record class initializer must bind its
module owner through the actual `ModuleEnclosure`. Deferred preparation used a
provisional owner; it is not publication evidence. `BindDeferredClassSelf` now
rebuilds aliases from original input/current metadata and the actual enclosure,
requires the same record, read/guard pairs and slot symbols, then independently
checks the tree aliases and semantics. Names, TDZ domains and values are not
invented or copied.

## Acceptance and exact bytecode differences

Evidence is under `build/review/parameter-expressions/`. The compiler is DevEco
es2abc, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Commands use `--module --merge-abc --extension=js`, matching record/source names,
separately default optimization and `--opt-level=0`. The only source adaptation
is identifier export aliases without quotes, applied identically on both sides.
`grouping_bytecode.py`'s exact disassembler helpers are used; no register,
branch, exception or metadata difference is normalized.

- Six generic positive fixtures publish and pass Node syntax and original-vs-
  engine-output differentials (values, length, earlier parameters, live capture,
  missing/undefined versus provided falsy values, getter order, exceptions,
  empty body log on default failure and method receiver identity). `later`
  refuses `DefaultBodyError::kProof`; the initializer scope/guard proof remains unproved.
- Across the twelve fixture comparisons, six full disassemblies are identical.
  The class-method fixture's six method comparisons are identical; only pool/
  string offsets differ. The two capture fixtures add private parameter-copy moves
  and a source `let` HoleGuards on reads/writes of the already initialized creator capture; it
  has the same storage/capture target and getter order. Default optimization
  removes some moves. These are exact recorded differences, not equality passes.
- The live module export and captured cell are reassigned between calls without
  changing the already published function; separate factory calls remain isolated.
- The class-self constructor fixture additionally checks static self construction,
  `.length === 1`, defaults and explicit provided arguments in Node.
- o20's emitted constructor recompiles at O0 to the **identical instruction body**
  as m8638, including all six conditional default paths, the module HoleGuard,
  global lookup, property getter reads and seven original field stores. Eight
  accessor bodies and both callbacks are also identical after matching their recorded source roles.
  Original hashed/mangled method identities and new es2abc identities are kept
  separately in the report, not silently renamed in the disassembly.
- The remainder of o20 is semantically recovered source rather than bytecode
  equality: private cache/register moves, generated method IDs, IC positions,
  named-vs-unnamed lexical-environment metadata, source HoleGuards on already
  initialized backgroundBar/thinStrip and existing logical source projections
  differ. Default optimization additionally changes registers/branches and
  folds constants. The accessor installation/static initialization/cleanup/public
  publication order remains the previously verified class-port order.
- Original static `definefieldbyname` and recompiled `definepropertybyname`
  both call `SlowRuntimeStub::DefineField` with the same object/key/value and
  preserve the accumulator: pinned interpreter-inl.cpp **7597–7637**. They are
  own-field definitions, not inherited-setter writes. Module request/import/
  export identities remain the two original imports and one class export.

Target original ABC is disassembled directly. Paired source is never an engine
input. Full o20 Node differential against the Ark runtime is not claimed; the
record passes Node syntax and the generic independent fixtures supply behavior
oracles. o0 still needs a completed-class availability port for getInstance's
result; no PublicPublish is fabricated for its export.


Final verification: full build; 65/65 serial source-regression; 4/4 focused;
221/235 broad sweep, exactly the 14 documented pre-existing failures. The
200/201/400/800 parameter-width and initializer-depth tests exercise exact byte/
work boundaries and one-below refusals, plus block splits, actual earlier-
parameter read ports, source-owner substitution, altered bindings/keys, guard/
commit/order/escape/domain and exception mutations. Flow-buffer reuse clears
all old ports and re-derives them; retained memory accounting includes them.

Fresh 40-input cohorts against wtbase 024e759c: 532→533/620 and 584→585/620,
only o20/8166 changes, with no regressions or previously complete text drift.
`acceptance.json`, `verified-{focused,regression,sweep}.log`,
`cmp-verified-{numeric,source}.log` and per-flag manifest changes under the
scratch evidence directory record the results. Earlier interleaved build/test
runs with stale-producer errors are retained but not counted as acceptance.

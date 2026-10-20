# For-of inside an ordinary user try

The RpcTool query methods have an ordinary, effectful catch surrounding the
iterator activation. In m20512, Try4 starts at original PC236 and catches at
H759; m25655 uses PC246/H801. The expanded close's PC654→690 and PC663→667
edges (PC696→732 and PC705→709 in m25655) are implementation edges of one
close operation. They must not become normal exits of the enclosing source try.

The close boundary now distinguishes *owned* regions from *ordinary enclosures*.
An enclosure has the whole close member set in its original protected range,
with no overlap of its handler. Partial overlap remains a typed ownership
refusal. It never grants ownership of the user catch. Independent close-summary
verification recomputes these identities and containment from original flow.

Protocol topology first seals close implementation into entry-qualified units,
retaining correlated outcome ports and original dispatch. Ordinary enclosure
projection then installs the existing TryControlUnit/TryExitUnit model around
that graph. The shared ordinary try/loop composer sees only native completions,
not the close's internal branches. No ValueProgram or expression IDs are forged.
The independent exception-control verifier rebuilds the projection.

The enclosing catch must observe every escaping Throw, including close errors
replacing Break/Return and the original Throw when close fails. Normal
continuations remain ordinary try ports. Return remains abrupt. Catch payloads
and targets remain the originals; neither forwarding nor caller escape can be
invented for an effectful user handler.

The ECMAScript basis is ForIn/OfBodyEvaluation and IteratorClose (§14.7.5.7 and
§7.4.11). The lowering is pinned in ets_frontend/es2panda/compiler/base/
iterators.cpp:80–152 and compiler/core/dynamicContext.cpp:126–153. Existing
IterationAcquisition and ExternalExceptionNoHole contract predicates still
apply; this composition adds no premise.

All projection arrays and stacks are admitted before allocation. Scope parents
are compressed once in lexical order, membership is indexed with an explicit
DFS, and exits are sorted/deduplicated and propagated bottom-up. Work is
O((V+E+R) log(V+R)) plus the shared bounded control/loop analyses. Exhaustion is
never treated as a successful containment or correspondence proof.

Publication requires tree and every recovery projection to verify independently;
standalone close/control success is not sufficient.

## Completed production checkpoint (2026-10-20)

Baseline: **3e475a3b**, including its built wtbase. Both records now publish:

| Record / query | Previous first refusal | Current result |
| --- | --- | --- |
| o37/20175, m20512 | PhasedMethodError::kControl → ExceptionControlError::kMultipleExits, inner Try0 PC640/H665 | Complete; native for-of, enclosing Try4/H759 |
| o38/25106, m25655 | Same chain, inner Try0 PC682/H707 | Complete; native for-of, enclosing Try4/H801 |

The second original protected range starts with pure input transport at242;
246 is its first throwing instruction. The range and dispatch identities, not
those PCs, drive the proof. The owned close/body-wrapper regions remain
Try0/Try1/Try3. The surrounding Try4 is ordinary. Scsi's acquisition-input
Try2 (getter at305, forwarding handler311) is independently proved transparent
and terminates at ordinary H801, not at the caller.

Implementation details:

- Boundary certificates retain an `enclosures` table separate from `regions`.
  All close members, the acquisition, advance/done/value boundary and independently
  proved body range must be contained; handler overlap and partial containment
  still refuse. No activation evidence means a missing ownership proof, including
  the standalone forged-user-try test; it never grants enclosure automatically.
- `protocol_enclosure.cc` consumes the sealed topology. `ProjectExceptionScopes`
  reuses the ordinary projection's inert-corridor/parent analysis with retained
  region identities. A typed `TryLoopLayout` supplies the shared ordinary loop
  composer; no ValueProgram, ExceptionPlan, operation or ExprId is fabricated.
- A normal port is keyed by **region, protected/handler arm, continuation**.
  Exhaustion and break share the protected port; handler completion has a distinct
  port, so two independent source arms never share control ownership. All original
  incoming arcs remain in the mapping. Internal close edges are not try exits.
- Ordinary catch bodies remain source catches. Transparent forwarding routes may
  terminate at a retained ordinary catch only when their original same-payload
  dispatch proves it. Recovery independently resolves the terminating handler.
- TreeFlow projects expected exception scopes through owned/forwarded regions.
  Recovery reuses verified guarded-read action regions and conditional TDZ
  exclusions, retaining their value/provenance dependencies. Each final Throw
  port is checked against the actual source catch point or caller exit; an
  intermediate owned handler must belong to the same activation/protocol.
- Scope/activation/catch/escape/ownership refusals remain. Original contracts stay
  unchanged. The existing V2 acquisition and V4 external-exception predicates
  still gate native iteration. There is no new premise.

The shared indexes, explicit traversal stack and sparse completion-port arena
bound admitted work to O((V+E+R) log(V+R)); dense port propagation exhausts a
prepaid, input-sized resource allowance, never silently truncates a proof.
The tests exercise200/201/400/800 padding, register remapping, inserted blocks,
exact byte/work boundaries, forged enclosures, wrong/missing ports and handlers,
wrong completion payloads, caller substitution for the enclosing catch, receiver,
exhaustion binding and original exception mutations.

## Semantic and SDK evidence

`for_of_user_try` has default/O0 ABCs and an exact compiler/hash registry. The
engine-produced `main`/`member` modules pass433 Node traces, including a getter
throw before acquisition. The existing multiple-close fixture plus its newly
published `catch` module pass432 traces. The refreshed throw-domain test adds36
engine/original close/catch cases. Captured-local `cells` and `skip` fixtures also
publish after the same guarded-read handoff; their test adds108 differential
cases, per-call isolation and effective arithmetic mutations. Older assertions
for the three still-unproved forms remain intact.

For the two real RpcTool queries, `tests/tools/rpc_tool_query.mjs` extracts the
published method and the independent paired TS method. Only Node sees the
paired source (transpiled with the pinned SDK TypeScript); the engine receives
ABCs exclusively. **324 cases per query /648 total** cover early exits,
query/next/property failures, match/exhaustion, exact caught identity, close
lookup/call failure, primitive/missing/null return and the receiver's devices
accessors. Full query texts next to originals are in
`build/review/for-of-enclosing/query-o{37,38}/comparison.md`.

SDK compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Disassembler SHA-256:
`a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
The comparisons use grouping_bytecode.py's strict assembly reader: debug/line
information only is stripped; registers, branches, handlers and metadata are
not normalized. Options and every per-method diff are retained under
`build/review/for-of-enclosing/sdk-{main,member,rpc}`.

- Synchronous fixtures: default andO0, both original source and engine output,
  **8 compiler runs /8 method comparisons**. Four module-entry bodies are
  identical; four scan bodies differ. Differences are assignment-form key
  initialization outside the loop, register/temporary allocation, inverted
  predicates/explicit continue and their branch/handler labels. The same real
  acquisition, return reads, object checks and receiver calls remain. Both O0
  recompilations verify the complete standalone protocol/evaluation proof.
  Default recompilations retain the existing ExceptionPlan layout/handler-shape
  gaps atPC331 (`main`) /129 (`member`), not ownership/equality passes.
- Real queries: both compile at default andO0; all four query comparisons differ.
  Full diffs include source-level control restructuring, registers/IC slots,
  materialized lexical reads and assignment-form loop binding. Each retains one
  acquisition, two return reads and five catch ranges; O0 retains all three
  object checks. Default optimization removes the unreachable check following
  rethrow of the original completion. This is not counted as bytecode equality.
- Scsi's original uses eight `callthis*withname` instructions; the SDK default
  disables that compiler option. Additional recompiles with
  `--target-api-version=24 --enable-callable-name=true` reproduce all eight.
  This option matters for non-callable TypeError diagnostics and is recorded,
  not normalized away: pinned interpreter-inl.cpp:284–310/1401–1443 uses the
  extra operand for that error while retaining the same callable/receiver path.
  The frontend selection is callExpression.cpp:167/220–232 and
  pandagen.cpp:1382–1417. No runtime message-erasure premise is claimed.
- Only identifier-valued string export aliases are adapted by the same Acorn
  transformation used by grouping_bytecode.py; the SDK predates that JS syntax.

This round does not claim that regenerated bytecode is instruction-identical,
or that Node executes Ark. The original-graph certificates, source recovery,
Node values/effect/error traces, SDK compilation and unnormalized differences
are separate evidence layers.

## Remaining boundaries

The async fixture still lacks the raw resume/rejection adapter
(CloseEntryError::kCorrespondence / CompletionRelationError::kInput). The finally
fixture still lacks completion-continuation derivation (CloseEntryError::kContinuation).
Both remain explicit negative tests, not differential successes. DataDriver
m42717 was re-probed and still stops at the correlated inner→outer continuation
at692; enclosing-try composition does not discharge that distinct obligation.


## Acceptance result

Final full build passed. Serial source-regression: **65/65**. Focused close,
for-of, enclosure, refreshed captured-local and throw-domain tests: **21/21**.
Requested sweep: **219/226**. Its seven failures are the supplied pre-existing
`family_source_cli`, `tree_structure`, `try_tree`, `class_tree_source`,
`try_tree_source`, `file_generator_return_source` and
`file_generator_return_loop_source`; no new failure remains. The initial
standalone ownership negative and stale captured-local refusal expectations
were fixed and re-run without weakening behavioral/mutation checks.

Both final40-input runs compared against the built3e475a3b baseline have no
regressions and no byte changes to previously complete records. Previous flags:
**537/620 →537/620**; all manifests unchanged. Language-source:
**589/620 →591/620**; only the two RpcTool manifests change. Both published
records pass `node --check`. Full method comparisons and Node reports are linked
above; the source texts used by those evaluations were byte-checked against
this final cohort output.

All80 input/flag combinations were also measured with a four-worker runner.
Maximum engine RSS: **42,713,088 bytes**; measured runner plus conservative
four-child aggregate upper bound: **189,759,488 bytes**, below1,000,000,000.
This is an aggregate bound from measured process peaks, not a claim that the
peaks occurred simultaneously. Evidence is under
`build/review/for-of-enclosing/`: `acceptance-build.log`,
`acceptance-source-regression.log`, `acceptance-new-tests.log`,
`acceptance-sweep.log`, `accepted-{source,previous}-comparison.txt`,
`resource-parallel/report.json`, `sdk-*` and `query-o{37,38}`.

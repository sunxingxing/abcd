# Verified iterator completion ports

The source contract is unchanged (`language_source_v10`). This route uses the
approved V2 synchronous for-of acquisition, external-exception-no-Hole, and
V10 array-pattern acquisition premises. It adds no premise and does not admit
`for await` or additional pattern forms.

## Ownership and activation

`completion_ports.{h,cc}` owns facts; exception/control planning owns placement.
The entry point requires the actual `ExceptionControlRegionFailure` with
`kMultipleExits`, the region, and its two original exit witnesses. It replays the
ordinary control query before deriving anything. Other failures and uncovered
user tries retain their refusals. Already successful routes never enter this
retry. Retained legacy inputs and retry scratch share the remaining method
budget.

The certificate retains original edges, pending kind and SSA payload, source
and destination protection contexts, all copy/phi predecessor correspondences,
and the local close outcome proofs. Call-success validation and absent-method
bypass are separate resume ports even if their eventual native completion is
identical. An internally caught close failure is a candidate outcome; it does
not replace the pending payload before the proven priority selection.

Protection contexts are parent links, with separate protected/handler identities.
Protocol body wrappers are identified by certified ownership, never by interval
overlap. An ordinary enclosing user try remains ordinary. Raw completion replay
includes the surviving await/yield and array-pattern normalization certificates,
so folded coordinates cannot be interpreted as undecorated bytecode.

## Nested routes and coverage

`close_entries`, `close_boundary`, `close_loop_body`, `close_completion`, and
`protocol_topology` compose only across certified original cuts. Entry guards
retain their SSA definition, storage, predicate and activation write. A persistent
closed-iterator set qualifies each route. On re-entry through an already closed
compiler handler, only the proven guard-infeasible-close transport path is legal:
it contributes zero additional native close events. Physical SSA/storage identities
are not merged to make that path work.

A route consists of an entry plus its closed-set root and selected transitions.
Routes and persistent set additions are memoized. Return/break/continue payloads
and targets remain distinct. Close order is inside-out; a throw keeps the pending
throw according to the existing close-priority proof. Every original entry,
outcome, resume edge and member must be covered. `kNested` is replaced only after
all transition, private-state, entry and port coverage obligations close.

Native syntax contains nested `for (... of ...)` and the initiating native
`return`, `throw`, `break` or `continue`. There is one native return at its action;
no hand-written `.return()` calls. Pure VM restore copies are transport, while
observable producers, iterable inputs and returned values remain real source
observations with their original evaluation/storage identity.

A facts certificate alone does not admit output. Before replacing the legacy
control refusal, the retry asks the existing for-of evaluation and placement
consumers to build their views. This keeps a partial-record omission available
when a later source representation is unsupported, and publishes the new typed
placement cause and original/working location alongside the original two-exit
witness.

## Independent checks

- The port verifier derives the certificate again from immutable inputs and
  compares activation, protection, entries, payloads, guards, routes and coverage.
- The close verifier and ownership seal replay local reference, activation,
  priority, private-state and coverage proofs with the certified cuts.
- Exception/control and completion placement check original edge and protection
  correspondence, including ordinary user tries.
- Evaluation, operands and origins recheck every member/use and native output.
  Phi/catch/snapshot identities are queried after all actual assignments have
  been witnessed; they are not borrowed from a successful builder.
- Tree flow and recovery retain the certificate. Original/raw route recovery and
  native recovery independently check enclosing iterator context, pending payload,
  target, order and close count. Native recovery memoizes close chains. A moved
  loop, duplicate return, omitted throwing event or extra close cannot be sealed.

`CompletionPortError` distinguishes input, entry, coverage, payload, activation,
protection, order, priority, interference, target and correspondence failures,
plus resource/work limits. Existing underlying typed causes remain available;
for-of placement errors now have a public stable typed diagnostic. Exhaustion
in a fresh guarded-payload verifier remains a limit, not a mismatched-input error.

## Cost and admission

With N instructions/nodes, E edges, U uses and K retained port/route/coverage
incidences, the intended bound is O((N+E+U+K) log(N+K+1)). Shared indexes,
parent links, sparse transfers, persistent closed sets and memoized routes avoid
per-action graph expansion. Dense route/coverage output is charged as K and may
refuse under the common resource/work budget. Allocation capacity, map nodes,
retained diagnostic/legacy owners and delegated query grants are charged before
allocation/use. Work exhaustion is a typed refusal, not discarded semantics.

The scaling test adds 100/200/201/400/800 independent instructions to an owned
body and keeps 18 entries, 70 exits and 18 qualified routes unchanged. It checks
charged N log N growth, and separately rebuilds register-renumbered, pure-copy
and split-edge variants. This is N-scaling evidence with fixed K, not a claim
that every combinatorial protocol graph is admitted.

## Tests and remaining representation gaps

`tests/fixtures/isa/completion_ports` contains pinned O0 ABCs, commands and hashes.
Eleven positive exports have 6,578 Node differential comparisons covering close
lookup/call order and receivers, missing/null/non-callable methods, primitive
results, getter/call errors, pending return/throw priority, nested break/continue,
ordinary catches, pattern closes, iterable phis, result identity and suspension.
Native errors compare constructor names; user errors additionally retain identity
and message evidence. Owned certificate mutations and an unrelated multi-exit
user-try test check refusal; source text is never an engine input.

Three real-compiler negative fixtures retain exact typed refusals: labelled
outer continue whose pinned lowering closes/rebinds the destination iterator,
an acyclic single-iteration loop lacking native-loop placement, and an ordinary
user-try/pattern enclosure whose control structure is not proved. No extra source
premise is introduced to admit them.

The original goal methods advance, but whole records remain incomplete:

- DataDriver m42717 and m42744 pass. m42798 (`getSuiteStress`) next fails the
  legacy AST expression-ownership census: orphan expression 59, kind 1, original
  pc542. Published causes remain FunctionTreeError9 / TreeSemanticError0 /
  TreeFlowError1 / TreeObligation10. General AST arena ownership is separate work.
- Service m60551 passes completion discovery/ownership/control beyond original
  pc313. Native placement next refuses an acyclic inner iterator at original
  pc452 / working428 with ForOfIterationError::kAdvance (3), wrapped in
  CompletionPortError::kTarget (9). The original region8, handler338 and distinct
  continuations340/363 remain published. Its ordinary user try [434,2118) stays
  ordinary. Acyclic iterator source placement is separate work; later source
  placement is not claimed complete.

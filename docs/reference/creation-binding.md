# Creation Site Binding

This implements the next binding/capture dependency in the approved design:
join a modern function creation instruction, its caller-regional target method
and the incoming ordinary lexical environment. It does not generate closures.

## Contract

`BindCreation` consumes a validated caller body, its unchanged successful
`EnvironmentFlow`, and the file's immutable `MethodLookup`. Instruction positions
index the decoded body, not byte offsets. The returned `CreationBinding` retains
the byte offset, typed creation operation (including length and home-object
input), target method position/attributes and capture source.

The four modern DEFINEFUNC/DEFINEMETHOD encodings are covered. Unsupported
instructions remain explicit. Normal-unreachable creation sites are reported
without binding their targets. Local environment IDs belong to the caller's
flow, not the target method. All contexts must agree; shape and local-ID checks
are not ownership tokens or a substitute for the unchanged-input precondition.

For ordinary DEFINEFUNC and DEFINEMETHOD, `OrdinaryCapture` retains the current
environment. The pinned runtime's `RuntimeDefinefunc` at
`ecmascript/stubs/runtime_stubs-inl.h:2485` assigns module, lexical environment
and home object on the ordinary path. Its sendable path instead generates a
sendable module, propagates its separate sendable environment when applicable,
and assigns the empty shared-function lexical environment. `SendableCapture`
marks that path without pretending it captures the caller's ordinary chain.
`RuntimeDefineMethod` at line 2651 sets the supplied lexical environment
unconditionally. Thus target sendable attributes alone cannot select capture
mode; the creation operation is also required.

Each site remains distinct even if targets match. A site inside a loop still
creates fresh runtime functions/environments on every execution. The binder
does not resolve captured slot values, TDZ, lexical this, home-object identity,
module contents, creation exceptions or source wrapper syntax. Historical
runtime equivalence requires separate evidence. Shared environment analysis
and class/literal-buffer creation remain outside this interface.

## Resources and Integration

Binding one site uses O(log R + log M) time through the method-target resolver,
constant auxiliary space and no retained allocation. The caller reuses a single
O(M)-payload lookup per file. Building all source/closure IR remains lazy work;
this synchronous primitive is not the required asynchronous function service.

`abc_creations` exercises the binder after CFG and environment analysis. It
retains one decoded method and environment analysis at a time, streams file
reports, and discards each site result after updating typed bounded histograms.
The existing 1,000,000,000-byte process address-space limit remains enabled.

- `creation_methods` counts methods with accepted environment analysis, not
  methods with fully implemented creation/source semantics.
- `creation_rejected_methods` counts failed environment analyses; their
  creation sites are not examined or included in site totals.
- `creation_sites = creation_bound_sites + creation_failed_sites +
  creation_unreachable_sites` for the four modern encodings in examined methods.
- Bound sites partition into local ordinary, external ordinary and sendable
  capture categories. External means relative to the caller's entry environment,
  not resolved across the complete closure graph.
- The obstacle histogram combines one error per rejected method with one per
  failed site; these are different units and must not be called failed methods.
- `other_method_instructions` records other opcodes with method operands,
  including unreachable ones, within environment-accepted methods. It is not
  an exhaustive inventory of every closure creation hidden in literal arrays.
- Exit zero means inspection completed, even with binding refusals. Lookup
  construction, file/decode and CFG failures produce nonzero status. Lookup
  failures are reported separately from method failures.

## Verification

The test-first implementation sequence covered the missing binding API before
implementation, then a missing CLI before integration. Unit tests check multiple
sites sharing a target but capturing different contexts, instruction lengths,
home-object input, sendable DEFINEFUNC versus DEFINEMETHOD, unsupported and
malformed operations, invalid flow/local IDs, unreachable sites and propagation
of target/metadata/attribute errors.

CLI fixtures contain parsed ABC records/methods/pools. They check distinct local
and external captures, reject a pool reference to a string as a method, avoid
binding unreachable creation sites, preserve static exclusions, and distinguish
environment refusal from invalid bytecode. Sendable counters and lookup-build
failure reporting still lack direct CLI fixtures; their underlying operations
are covered by unit tests.

The next source-recovery step must translate target captured-depth/slot uses
through each ordinary creation context, preserving instance freshness and
multiple parents, then recover values and Hole/TDZ semantics. None of these
counts constitutes a recovered record.

Full-corpus target resolution and memory measurements are recorded in the
[validation report](../validation/2026-09-22-creation-bindings.md).

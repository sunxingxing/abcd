# Record property snapshots

Ordinary `NamedRead` in the existing record interpreter now evaluates a
property at its bytecode position and stores the result in a private const
binding. `RecordValue::kPropertySnapshot` identifies this value, not a deferred
member expression or a receiver-bound callable. Register and lexical moves
preserve that identity without re-running a getter. Module stores, ordinary
undefined-receiver calls and constructor calls consume the saved value.

This path does not admit internal Hole/environment values as receivers.
Existing fresh-class `prototype` recovery retains its separate proof; other
property reads after class construction begins remain refused so an observable
getter cannot cross deferred class emission. No getter purity, stable global
property, or host-specific name is assumed. The temporary decoded property
name is charged while the receiver and key are printed.

## Evidence and remaining integration

Actual pasteboard ABC entry 9915 progresses from PC107 to PC120 in
`build/pasteboard-source-property-snapshots/manifest.json`. PC120 is a
receiver call, which remains unsupported by this record path. Re-reading the
property at the call or using a mutable Function.prototype.call/apply would
not preserve the semantics and is not a permitted workaround.

The normal semantic trunk already lifts this initialization prefix's reads
and calls. Whole-entry lifting stops at PC264, the class-buffer creation;
later accessor-definition instructions are also unsupported by that IR.
The next integration needs owner-aware class operations and record regions
consuming the shared call/reference proofs, not further name-specific entry
patterns. This increment only improves the retained record bridge: it does
not claim a shared SSA/AST path for record initialization, record-wide PC
mapping, a complete ProgressBar output or source equivalence.

Both production CLI targets compile. Tests and RSS tracking remain paused.
When resumed, regressions must check getter evaluation count/order, saved
values across register overwrites, throwing/nullish receivers, module stores,
undefined-receiver ordinary calls, constructor invocation and class deferral.

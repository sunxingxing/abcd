# Capture preservation on exceptional flow

## Contract

`ProveExceptionLocals` accepts an optional sorted span of same-body
`CapturePreservation` certificates. It validates strict ordering, exact original
PC matches, admitted creation kinds and complete consumption before using them.
A byte index maps each certified instruction to all its phase nodes.

Only the capture barrier changes: a certified function or literal-buffer
creation does not mark its current environment and ancestors as potentially
written by closures. Uncertified function, object-buffer and array-buffer
creation remain barriers. In particular, array buffers are not assumed plain
data in the absence of literal evidence.

The phase graph remains unchanged. Stores count only after normal Commit;
Before exceptions preserve the incoming register state; Catch nodes retain
their pending-exception behavior. Allocation and completed-store dominance use
the complete phase graph, never ordinary CFG dominance. Certificates do not
initialize cells, supply a captured value domain or license removal of guards.
Every store and other actual SSA read still passes `VerifyExceptionInputs`.

The production exception-body owner obtains certificates from actual ABC
targets/literals through `ProveCapturePreservation`. Child methods are inspected
transitively; unsupported or writing children get no certificate. Certificate
storage is deducted while local proof construction runs, then released. Work is
charged to the owning method. Analysis-only callers default to no certificates;
detached inspection must not silently assume the production owner's evidence.

## Cost

For I instructions and P certificate entries, admission is O(I + P) time and
O(I) temporary byte-index storage when P is nonzero. Node capture checks are
O(1); the existing reverse ancestry propagation and complete-graph dominance
costs are unchanged. Transitive summary cost is documented separately in
[capture preservation](capture-preservation.md). All simultaneously live arrays
use the remaining byte budget. No new dependency or licensing change occurs.

## Pasteboard Evidence

`checkPermissionGrant` (6733) previously failed exception-local analysis at
PC504 with `kCapturedEnvironment`; it creates child methods 5739 and 5932 at
PC425 and PC448. The actual record-output owner now passes local initialization
and reaches `VerifyExceptionInputs`, which rejects PC96 with `kInternalValue`.
PC96 is the catch entry's accumulator spill, not an uninitialized local cell.
Its pending exception still has the conservative Unknown domain. A future
exception-payload proof must explain every incoming throw edge; no global
Unknown-to-Language promotion is introduced here.

Both CLI targets build. Debugger evidence is in
`build/pasteboard-exception-preservation-debug.txt`; the complete record manifest
in `build/pasteboard-source-exception-preservation/manifest.json` is unchanged.
This is a proof-stage advance, not completion of the method or Home record.
Tests, executable equivalence and RSS tracking remain paused. Deferred cases
include creation throws before Commit, mixed certified/unknown siblings,
ancestor captures, buffered methods, stale/duplicate PCs and budget exhaustion.

## Escaping Local Bindings

`ExceptionLocals` now retains a separate full-phase capture certificate:
completed-store dominance intervals sorted by cell and phase-entry preorder
numbers. It never reuses the normal CFG's initialization intervals. The existing
store union is moved into the result, filtering cells whose environments remain
possibly written by closures. All parent stores must still pass the Language
input verifier before any owner may consume this conditional certificate.

The certificate is absent if any edge can re-enter an earlier instruction,
including exception edges and same-instruction re-entry. Before-to-Commit and
Catch-to-Before progression at the same instruction are allowed; backedges are
not. This avoids treating repeated environment allocations as one escaping
source cell. Forward closure creation also requires the original allocation to
dominate a completed initializing store.

The phased printer receives the same owner's environment topology and local
certificate. A child capture is resolved at its actual creation Before node,
then looked up in the initialized cell's phase interval. An outer capture stays
a typed parent obligation. Defined requirements still reject: this certificate
only proves initialized Language storage, not non-undefinedness. Neither the
topology-only overload nor the public Language-only template binder gains an
implicit stronger premise.

Storage adds O(N) phase entries and retains the existing bounded store-interval
buffer without copying. Dominator storage remains charged while entries are
allocated. Binding costs O(log L + log S) for L local cells and S merged store
intervals; there is no per-capture traversal of the phase graph.

In the actual 6733 owner, the sole retained cell interval is [10, 405).
Creation Before nodes for PC425 and PC448 have preorder entries 237 and 253,
respectively, both inside that initialized interval. Evidence is in
`build/pasteboard-phased-captures-debug.txt`. The record manifest remains
unchanged because PC96's pending-exception domain still fails earlier source
input verification. The host-boundary question remains unresolved and strict
Unknown defaults are unchanged. Both CLI targets build; no tests or RSS work
was resumed.

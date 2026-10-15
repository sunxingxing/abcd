# Close completion diagnostic analysis (A3b, partial)

`close_completion` is standalone and has no production hook. It re-derives
`close_entries` and the original completion relation, and records original
members and edges (including exception cuts). It does not grant topology
ownership or source admission. No region, instruction or exception is removed.

The proof states are intentionally separate:

- Guard and return-reference facts reuse the independently checked A3a activation
  proof, plus the unique callee use and receiver identity from the immutable IR.
- For a Throw entry, priority requires every reported exit to retain the entire
  original Throw tuple. An exit-free cycle does not prove this obligation.
- Non-Throw priority remains missing. The A2 relation correlates completion
  kind/payload/target but does not retain a path's return-getter/call outcome.
  Even unchanged output tuples cannot distinguish success from a swallowed
  close error. A future extension must correlate that outcome with the exit;
  comparing output tuples alone cannot prove the full priority table.
- Ownership records raw membership and checks external predecessor edges and
  semantic roles. It remains missing: private scalar live-outs and overlapping
  user-try exclusion have not been proved. An arbitrary object check does not
  count as a return-result check; its operand must be this call's result.
- Nested current-completion propagation has not been implemented. Multiple
  acquisitions are conservatively missing for this obligation, including
  unrelated acquisitions; this is a diagnostic coverage gap, not a restriction
  on source semantics or permission for a production path to refuse a program.

The verifier re-derives the immutable entry proof with A3a's backwards guard
algorithm and recomputes the summary; it does not consume supplied member masks
or verified flags. Priority/reference transfer definitions are shared; this is
not an independent implementation of the pinned interpreter. Telemetry is not a
certificate field. `peak_bytes` is a conservative scratch bound including
exclusive delegated grants, not measured allocations or RSS.

Every PMR allocation and surviving vector capacity is prepaid. All sparse walks
and sorting are charged to one input-sized N log N admission grant; dense
repeated-region inputs refuse work rather than admitting quadratic work.

The unit test covers the existing real m423/m451 ABC, original-payload Throw
preservation, conservative Break/Return refusal, forged flags/payload/targets,
missing/duplicated members, missing/changed edges, wrong receiver/reset/guard
inputs through A3a, register permutation and edge splitting. 200/201/400/800
prefix-size cases test exact producer byte/work admission and verifier work
boundaries. These are not dense completion/nesting scaling tests. No new Node
or runtime priority-matrix equivalence is claimed.

Remaining A3b work: outcome-correlated priority for Break/Return/outer Continue,
complete private ownership and user-try exclusion, nested current-completion
bridges, independent obligation-specific verification, and the full additional
fixture/mutation matrix. This partial analysis is not a completed A3b certificate.

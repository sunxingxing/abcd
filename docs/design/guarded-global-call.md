# Guarded global call in record entries

Sixteen Immersive record entries share a v7 bytecode shape that tests whether
a string key is present in a property of a global receiver. If absent, the
entry looks up a method on another global receiver and calls it with the
receiver property, the same key, and a freshly defined function. This is a
compiler control-flow idiom, not a special case for names such as `ViewPU`,
`Reflect`, `prototype` or `finalizeConstruction`.

`MatchGuardedGlobalCall` validates the exact decoded operations, repeated
pool slots, register def-use links, both conditional targets, the diamond
join, call receiver/argument order, and a subsequent accumulator overwrite.
It refuses external jumps into the region and any later read of a transient
register before an overwrite. It returns typed pool slots and instruction
positions, or a distinct CFG/resource failure rather than hiding exhaustion
as an unsupported pattern.
It does not emit source or establish global name equivalence. The record
interpreter attempts this pinned entry prelude at the first string-key
candidate only, avoiding repeated whole-method scans. The private source
renderer resolves each pool string, checks free global identifiers, preserves
the property-read and call order, and emits the freshly created function only
inside the absent-key branch. The enclosing record's two-pass admission still
rejects publication if any later instruction or class body lacks a source
proof. Dynamic 12.0.6.0 uses the same exact matcher and pinned v7
receiver/property/call order; dynamic 11.0.2.0 remains outside record source
admission.

Dynamic 12.0.2.0 also enters this exact matcher. In the paired
MultiCommunity ABC, 13 ArkUI page records progress past the guard to derived
class admission; their complex constructors still refuse whole-record source
publication. This is evidence of prelude recovery, not page decompilation.

The paired Immersive test checks all 16 matches, the Shopping operand slots,
branch/key corruption, a live-register counterexample, and 200/201/400/800
NOP suffixes. A Node oracle verifies the absent/present-key branch behavior
and function identity; the record test verifies that interpretation progresses
past the matched region. No sidecar source is read by the matcher or its test.
The paired 12.0.6.0 widgets ABC matches the same shape at entry instruction
3 and executes the emitted guard through the same Node oracle. Its later class
creation is still refused, so this evidence does not publish that record.

## Forward transient liveness

The suffix proof now reuses `BuildNormalFlow` and forward may-read-before-write
data flow. Each block carries a fixed set of bits for the eight store sites
in the matched region. A read while its bit is set rejects admission. Writes
kill bits only after all reads of that instruction have been checked; joins
union the predecessor states. Duplicate register operands retain separate
bits. Return/throw end a path; backedges and resume edges remain unsupported,
and the matcher still refuses exception tables. No purity assumption is made
about property reads or calls.

All edges must be forward, so bytecode block order is a topological order.
The propagation is O(I + E), with fixed-width register access windows; CFG
construction makes total time O(I log I). CFG scratch and the O(B) live-state
array share the caller's remaining byte budget. Allocation failures propagate
as `FlowFailure` with `FlowError::kResourceLimit`, not a fallback match.

ABC-only production inspection of pasteboard entry 9915 previously stopped
at PC30: suffix PC142 has a branch while registers 9 and 10 still retain
prelude values. With path-aware liveness the guard is admitted and record
9550 instead stops at PC107 (`kValue`). Artifact:
`build/pasteboard-source-guard-flow/manifest.json`. This does not publish a
complete ProgressBar record or establish source equivalence.

CLI compilation and read-only review completed. Existing integration callers
were adapted to the typed result, but tests and RSS tracking remain paused.
After resumption, add focused diamond cases (kill on both arms, read on one
arm, untouched arm reaching a join), read/write overlap, unreachable suffix,
backedge/resume refusal and zero-budget failure before claiming regression
coverage for the new proof.

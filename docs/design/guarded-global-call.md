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
register before an overwrite. The function is O(I) for I method instructions,
uses O(1) extra memory, and returns typed pool slots and instruction positions.
It does not emit source or establish global name equivalence. The record
interpreter attempts this pinned v7 entry prelude once after its three ABI
register moves, avoiding repeated whole-method scans. The private source
renderer resolves each pool string, checks free global identifiers, preserves
the property-read and call order, and emits the freshly created function only
inside the absent-key branch. The enclosing record's two-pass admission still
rejects publication if any later instruction or class body lacks a source
proof. Historic profiles do not use this pattern without their own proof.

The paired Immersive test checks all 16 matches, the Shopping operand slots,
branch/key corruption, a live-register counterexample, and 200/201/400/800
NOP suffixes. A Node oracle verifies the absent/present-key branch behavior
and function identity; the record test verifies that interpretation progresses
past the matched region. No sidecar source is read by the matcher or its test.

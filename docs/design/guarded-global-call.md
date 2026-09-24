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
proof. Dynamic 12.0.6.0 uses the same exact matcher and pinned v7
receiver/property/call order; dynamic 11.0.2.0 remains outside record source
admission.

The paired Immersive test checks all 16 matches, the Shopping operand slots,
branch/key corruption, a live-register counterexample, and 200/201/400/800
NOP suffixes. A Node oracle verifies the absent/present-key branch behavior
and function identity; the record test verifies that interpretation progresses
past the matched region. No sidecar source is read by the matcher or its test.
The paired 12.0.6.0 widgets ABC matches the same shape at entry instruction
3 and executes the emitted guard through the same Node oracle. Its later class
creation is still refused, so this evidence does not publish that record.

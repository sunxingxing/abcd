# Non-NaN multiplication proof

The pinned v7 `FastRuntimeStub::FastMul` directly constructs a tagged value
from double multiplication. Arbitrary accepted NaN payloads and zero/infinity
products must not be assumed to remain ordinary language values. General
arithmetic therefore cannot use a blanket Language result rule.

The value lattice now contains `FiniteNonzeroNumber`, seeded by nonzero
integer literals. Zero integer literals remain `NonNaNNumber`; raw double
literal admission is unchanged. For multiplication, both operands must be
proven non-NaN Number and at least one must be finite and nonzero. The result
is then non-NaN Number. This excludes both NaN inputs and zero/infinity pairs.
Overflow may produce infinity and underflow may produce signed zero, so the
result deliberately loses the finite/nonzero refinement. No constant folding,
coercion removal or source evaluation reordering occurs.

Copies and phi joins preserve the new fact when all inputs retain it. Numeric
joins widen to NonNaNNumber, mixed defined values to Defined, and other values
to their existing VM-category union. The monotone sparse solver still has
constant lattice height: its longest scalar widening chain gains one step.
No per-variable graph walk, numeric-range iteration or new buffer is added.
Normal and exception SSA share the propagation rule; the legacy linear
lexical path uses the same binary transfer function.

Deferred regressions while tests are paused: nonzero integer copies/phis,
zero/infinity in either order, unknown/NaN inputs, signed zero, overflow and
underflow, loop widening, 200/201 scaling and low-work-budget rejection.
This proof is not general NaN canonicalization, finite-range analysis or
completion of numeric operator support.

## Pasteboard checkpoint

Actual entry 9915 now passes complete semantic input verification and region
construction with strict unknown-closure defaults plus its proven creation
certificates. `abc_semantic ... 9915 --function-source` moves from the PC200
multiply-result rejection to AST rejection at PC15, a module store. Thus the
next boundary is module/class AST ownership and printing, not a remaining
numeric proof for this entry. No complete record or source equivalence is
claimed. Both production CLI targets are the compilation scope; read-only
review found no specific issue. Tests and RSS tracking remain paused.

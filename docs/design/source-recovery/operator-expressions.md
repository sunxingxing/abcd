# Unified ISA operator expressions

Implementation scope authorized on 2026-10-02: cover every instruction in the
pinned dynamic ISA groups `unary operations`, `binary operations` and
`comparation instructions` through the existing unified source pipeline. This
extends the current stage-one capability work; no sample-specific recovery path.

## Fixed inventory

Source: `arkcompiler/runtime_core/isa/isa.yaml` at the commit in
`docs/reference/openharmony-v7.0-release.lock.json`. There are 37 signatures and
38 encoded forms: 18 binary, 15 unary signatures (typeof has two widths), and
4 comparison instructions. Static ISA remains outside the supported container
contract. Historical dynamic profiles retain explicit decoding/semantic checks.

- Binary: add2, sub2, mul2, div2, mod2, eq, noteq, less, lesseq, greater,
  greatereq, shl2, shr2, ashr2, and2, or2, xor2, exp.
- Unary: typeof (IMM8/IMM16), tonumber, tonumeric, neg, not, inc, dec,
  istrue, isfalse; deprecated register forms of tonumber, tonumeric, neg,
  not, inc and dec.
- Comparison: isin, instanceof, strictnoteq, stricteq.

## One implementation route

Decode/profile -> Scalar/SemanticOperation -> verified Value IR -> shared
binding/evaluation and region plans -> FunctionTree expressions -> independent
operand/effect/source checks -> common printer. Extend these typed operations
and existing exhaustive switches; never dispatch production behavior on opcode
text, sample IDs, emitted strings or paired source. Standalone legacy expression
printing is not evidence that the common FunctionTree route works.

The same operators must compose in returns, assignments, call arguments,
conditions, object/array initializers, nested expressions and phased bodies when
the enclosing ownership contract is satisfied. A temporary protects a real
snapshot, coercion or multi-use obligation; it is not a substitute for finishing
expression composition. Numeric update expressions operate on proved source
bindings or owned temporary values, never silently mutate the original operand.

## Structure and printing

Keep operand grouping in ExprId edges. Define precedence/associativity and
syntactic exceptions centrally for the common printer, independently of value
planning. Preserve `a - (b - c)`, `a / (b / c)`, `(a ** b) ** c`, `a ** (b ** c)`,
and exponentiation with unary/negative left operands. Never reassociate floating
arithmetic or coercing addition because the operator appears mathematically
associative. Mixing nullish with logical operators, conditional expressions,
assignment/update, await, calls and member bases requires their actual grammar
constraints. Current fully parenthesized output is a safe starting point, not
proof of the requested final expression presentation quality.

## Current gaps and execution order

1. Complete in-flight literal-binary validation and commit its verified change.
2. Add missing instanceof to the common typed operation route, with pinned
   runtime operand order, @@hasInstance, Proxy, primitive and exception tests.
3. Enumerate and test all 38 encodings through shared input, values, tree and
   printer. Close unary ToNumeric/update composition and deprecated.neg gaps
   using runtime/profile evidence rather than assuming identical old semantics.
4. Complete centralized precedence/context printing and nested expression tests.
5. Expand operator combinations across ordinary/phased expression contexts,
   negative ownership cases and geometric scaling; rerun fixed stage-one inputs.

Arithmetic value-domain proof remains a distinct requirement. Existing
arbitrary-NaN/internal-tag refusals and the deprecated.neg INT32_MIN discrepancy
must be resolved with pinned runtime evidence or exact typed semantics; deleting
checks does not implement coverage. Explicit refusals remain incomplete work,
not successful coverage. Report semantic/profile limits honestly if a native
source representation cannot preserve a particular input contract.

## Acceptance

Per encoded form, retain source-grounded operand and completion semantics plus
an actual common-pipeline result. Independently execute generated nested code
against handwritten native behavior: Number/BigInt/Symbol, NaN/-0, comparisons,
ToPrimitive hints and order, getters, mutations, callee/receiver identity and
exceptions. Test missing/forged witnesses, shared values and intervening effects.
Compare precedence-sensitive expressions by behavior and expected grouping, not
by a snapshot generated from the implementation. Keep source-location evidence
and budgets, including 200/201 and geometric tests, and preserve paused full
suite/RSS/corpus expansion constraints. Opcode decoding, a token table or
syntax-only parsing cannot close the goal.

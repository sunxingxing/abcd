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
constraints. The common printer now uses typed precedence, associativity and operand-side
contexts. Printer-only fixtures do not prove decoded numeric-domain admission
or the requested complete source-style acceptance.

## Current gaps and execution order

1. Shared binary literal composition, instanceof and centralized precedence
   printing are implemented and verified; keep them on the common route.
2. Unary initializer fixtures enumerate all 16 unary encodings through the
   pinned catalog for proved numeric inputs. General ToNumber and ToNumeric
   also have decoded normal/phased tests and independent coercion oracles;
   deprecated.neg retains its tag exclusion, and updates retain numeric gates.
3. Integer-exponent Number EXP now has a common result proof; general
   arithmetic input domains and right-nested exponent facts remain incomplete.
4. The decoded binary/comparison matrix now enumerates all 22 encodings under
   proved input premises. Extend operator combinations across ordinary/phased
   expression contexts and general input domains; preserve ownership refusals
   and geometric scaling checks, and rerun the fixed stage-one inputs.

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

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

### Conditional polarity and equality complements

Rule 117 extends the common printer's existing Boolean observation/polarity
algebra. A conditional observes only ToBoolean of its condition; an outer chain
of NOTs can therefore be omitted, swapping the original branches for odd parity.
The condition is still evaluated once before the selected branch. Branch values,
getter/coercion/receiver effects and abrupt completion retain their original
occurrences. The FunctionTree, control/evaluation plans and instruction origins
are unchanged; no sample, producer whitelist or graph-layout admission is added.

TreeEqualityComplement defines the language complements of ==/!= and ===/!==
with an exhaustive typed operator switch. These operators already return exact
Booleans, so a positive Boolean conversion is also redundant. Relational and
membership operators have no native complementary operator here: in particular,
!(a < b) cannot become a >= b in the NaN domain. They retain the original NOT.
Discarded results still evaluate the original comparison and its conversions.
The actual normalized operator is passed to the existing parent-grammar grouping
query, preserving exponentiation, unary and nested equality parentheses.

Omitted condition NOTs retain the printed condition's span, ending before either
branch; reordered branches retain their original ExprIds and actual locations.
The chain has two bounded linear passes and uses the existing print-action stack,
without a per-expression CFG walk, new proof wrapper or dependency. For the tree's
unique expression ownership, added work is O(printed expression occurrences).
Budget/work evidence does not certify process RSS.

Provenance: ECMA-262 2024 [logical NOT](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-logical-not-operator),
[equality](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-equality-operators)
and [conditional evaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-conditional-operator).
The handwritten extension introduces no new licensed dependency. Printer-only
polarity/grammar fixtures remain separate from actual decoded conditional-call,
class-field and paired-record behavior evidence. Normalizing syntax does not
widen numeric/private-value admission, unary VM profiles or constructor protocols.

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

## General-parameter binary verification

The decoded matrix additionally routes two Language parameters through all 17
currently admitted general binary/comparison operators: remainder, loose/strict
equality, ordered comparison, shifts, bitwise operations, in and instanceof.
ADD/SUB/MUL/DIV/EXP retain explicit general-input refusals; successful constant
cases do not remove those gates. Ordinary and try/catch fixtures share the same
bytecode construction and production expression pipeline, and retain separate
source and independent tree checks.

The handwritten native oracle reconstructs operands for each execution and
compares coercion events, receiver/argument identity, mutation order, primitive
and mixed BigInt/Symbol results, Proxy accesses, @@hasInstance and injected
failures. Both repeated field expressions and interruption before a later field
are observed. Catch fixtures return undefined, matching their actual ABC catch
body. These checks cover source-language behavior, not execution of the pinned
Ark binary; they do not establish whole-record completion or general arithmetic
admission. No production behavior depends on the fixture's operator list.

## Boolean observation in the common printer

Rule 83 introduced typed exact-value/truthiness observations. Rule 85 adds a
discarded-value observation and composes Boolean choices with logical NOT.
If/conditional tests and logical NOT consume truthiness. Within that
context, &&/|| and selected conditional branches forward the same observation;
an exact-value consumer keeps every Boolean conversion. Assignment RHS, call
arguments, property bases/keys, arithmetic operands and returns remain values.
Nullish selection keeps its operands as values even in a Boolean context:
`!!left ?? right` and `left ?? right` have different RHS execution conditions.

The language justification is ECMAScript
[ToBoolean](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-toboolean)
and [logical NOT evaluation](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-logical-not-operator):
they introduce no user conversion hooks, and negating twice preserves
truthiness. This is a source-AST observation rule, not a new VM value-domain
premise or an admission rule based on compiler/opcode/CFG layout. ToNumber,
ToNumeric and bitwise conversion remain independent obligations.

The iterative printer composes NOT and Boolean-literal conditional results as
a typed polarity. `condition ? true : false` prints as `!!condition` for an
exact value and as the condition for truthiness; the inverted choice prints as
`!condition`. Arbitrary compositions consume each node once and emit at most
one negative or two positive NOT operators. Nonliteral branch values retain
their conditional evaluation. These are language expression identities, not
opcode sequences, producer lists or graph-shape recovery permissions.

Equal Boolean arms still evaluate the condition: `condition ? true : true`
prints as `(condition, true)`, with the corresponding false/inverted result.
The condition's value is discarded, but its GetValue, getter, reference errors
and arbitrary thrown value remain observable. This follows
[conditional evaluation](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-conditional-operator)
and [comma evaluation](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-comma-operator).
Nested discarded Boolean results may lose their pure conversions and literal
selection; they never lose the original condition evaluation. For discarded
&&/||, the left still observes truthiness and the selected right is discarded.
For discarded ??, the left still observes its exact value and only the right
is discarded. This keeps nullish selection and short-circuit effects intact.

The exposed child's original parent context decides precedence/nullish grammar,
rather than inheriting an omitted unary's precedence. A remaining conversion
uses the common unary grouping rules, including exponentiation's left operand.
Omitted NOT, conditional and Boolean-literal nodes retain nonempty spans over
their normalized expression; AST identities, original operations, scope and
effect proofs are unchanged. Work and action storage remain linear, without
recursive printing or chain rescans. No VM profile, input admission or V3 host
premise changes with this output transformation.

Printer isolation tests distinguish exact results, predicates, short-circuit
logical/nullish expressions, conditional branches, comparisons, and bitwise
consumers. They exercise 1/2/200/201/402/804 NOT depths, span bounds and an
independent getter/Proxy/mutation/throw oracle. Boolean choices additionally
cover all four Boolean-arm combinations in eleven contexts, 1/2/200/201/402/804
depths, exact Boolean results, comma GetValue, nullish selection, exponent
grouping and four effective semantic mutants. These are printer isolation
tests, not a proof of arbitrary decoded arithmetic inputs. Existing ordinary,
phased, branch, snapshot and paired-record tests independently cover the full
source route. These layers do not close general arithmetic admission or the
whole phase-one semantic/style goal.

## Comparison arguments and linear execution prefixes (rule 112)

Normal conditional discovery consumes the common BooleanEvaluation result
proof instead of repeating an operator admission list. All ten equality,
relational, in and instanceof results use the same conditional representation;
numeric VM false/zero tests agree with source truthiness on these Boolean
values. The shared expression printer retains original grouping and precedence.

Logical evaluation indexes linear ordinary control corridors once in O(B+E),
then includes the original prefix operations in its existing forward/reverse
operand and effect interval equations. Pure parameter-free control anchors
have no language event; getters, conversions, environment changes and abrupt
completion still do. Previously consumed join prefixes stop at their consumer.
Single-entry/single-successor corridors prove equal execution counts for this
linear composition, not a language restriction on references. General nonlinear
prefix composition remains a coverage gap. No alternative writer or dependency.

Initialized cell guards reuse the common initialization proof both when locating
the first real snapshot occurrence and when closing selected subtrees. The
original guard anchor and ordered cell read survive fresh verification. Native
member-call references retain the original callee lookup before conditional
arguments, including parameter getters that overwrite the property; overwritable
.call is never a substitute. Decoded tests, independent generated JS and paired
callback evidence are recorded in status.md. They do not close all operator,
constructor, complete-record or stage-one acceptance requirements.

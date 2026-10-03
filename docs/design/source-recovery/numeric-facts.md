# Shared numeric facts

The common value analysis distinguishes FiniteNonzeroNumber, FiniteNumber
(including both signed zeros), NonNaNNumber, Number (including NaN), Defined and Language. Numeric
joins keep finite information when both inputs are finite and drop nonzero
information when zero is possible. No source/debug spelling supplies a fact.

A separate `ExcludesInt32MinTag` witness describes the original VM value's
representation. Integer literals other than INT32_MIN and non-NaN FLDAI doubles
supply it. A double numerically equal to INT32_MIN is not the tagged int32
minimum. Joins retain this witness only when every alternative has it. Numeric
identity conversion preserves the incoming witness; updates and arithmetic do
not inherit it merely because their inputs have it.

ADD2 can produce a proved non-NaN Number when both inputs are non-NaN Numbers
and at least one is finite: infinity cancellation cannot occur. String addition
continues to require Language inputs with a proved String operand. SUB2 uses
the same numeric criterion and also requires the RHS tag exclusion. Pinned
`interpreter-inl.cpp` (OpenHarmony-v7.0-Release, lock identity) first negates an
int32 RHS before checking overflow; negating INT32_MIN is undefined C++. A
source-level UBSan probe establishes this limitation, not actual Ark VM output.
Do not silently treat that case as native JS subtraction or authorize it from a
Language-only input. The original noncanonical-NaN counterexamples still refuse.

Normal and phased SSA use the same sparse worklist transfer rules; linear
lexical analysis calls the same scalar helpers. ToNumber preserves all facts of a proved Number; otherwise a Language
input has a Number normal result. The pinned conversion returns Number or
throws, including BigInt/Symbol and conversion-hook failures; it does not imply
non-NaN output. ToNumeric preserves existing Number facts but otherwise has a
Defined normal-result bound while retaining BigInt semantics. Known Number input (including NaN) uses identity; other inputs use exclusive
postfix scratch under the resource-exclusion contract. Independent verification
checks the old-value result and rejects reads/captures/reuse of that scratch.
V1 does not authorize the extra numeric operation. This does not admit arbitrary
arithmetic over conversion results. See ECMAScript 13.4.2.1 for the returned
ToNumeric oldValue; this is distinct from platform differential acceptance.

The fixed-height lattice uses finite refinements and independently weakening
representation and integrality facts. Phi joins stay incremental; arithmetic nodes inspect at
most two operands, with no per-variable CFG walk or interval fixed-point engine.
Existing allocation accounting uses actual ValueKinds size. Work-counter tests
are not physical RSS or whole-corpus performance evidence.

Decoded tests cover integer overflow to double, signed zeros, subnormals,
finite extremes, infinities, NaN rejection, normal/phased results, identity
conversion and update-created tagged minima. Independent native JS compares
actual generated results with Object.is. These component checks do not replace
full record, platform or source-style acceptance.

## Modern and deprecated negation

The semantic model retains `kLegacyNegate` separately from `kNegate`. Pinned
modern NEG explicitly promotes tagged INT32_MIN to a double result; deprecated
NEG lacks that guard. Legacy lowering therefore requires non-NaN Number input
and ExcludesInt32MinTag. Both proved forms preserve finite/nonzero facts and
produce a representation excluding tagged INT32_MIN. The common forest and
independent operand verifier recheck the legacy premise, including literal
operands exposed by copy propagation. The old standalone scalar formatter has
no such witness and refuses the legacy operation. This is conditional native
lowering, not blanket admission of every input or an alias of opcode identity.

## Integral exponents

`WithIntegralNumber` / `IsIntegralNumber` carry a finite integer Number fact,
including both signed zeros, independently of the tagged-int32 exclusion.
LDAI and finite FLDAI literals establish it; copies, proved negation, identity
conversions and joins preserve it only with evidence on every alternative. This is not a
sample-name or printed-expression inference. The fixed-height sparse worklist
and linear lexical pass share `ExponentNumbers`.

Pinned `interpreter-inl.cpp` EXP and `RuntimeExp` delegate Number exponentiation
to `std::pow` after their special guards. A non-NaN Number base with a finite
integer exponent cannot enter a NaN domain; negative bases, signed zeros,
infinities, overflow and underflow remain non-NaN Numbers. The common rule
therefore admits that case and leaves fractional/infinite exponents and NaN
bases unproved. In particular this does not authorize arbitrary Language or
BigInt inputs, nor every right-nested exponent result. General coverage remains
open rather than weakening the private-tag checks.

Decoded binary/comparison inventory tests cover all 22 encodings through the
common value/tree/origin/printer route, with distinct accumulator/register
operands. Together with the unary inventory this exercises all 38 encodings
under their current input premises. Separate exponent cases test ordinary and
phased flows, lattice joins, fractional/NaN refusal, signed-zero behavior and
left-nested powers. Native JS comparisons are source-language evidence, not
execution of the pinned Ark VM binary. Arithmetic-library approximation and
platform equivalence remain part of eventual platform acceptance.

## Numeric updates in composed expressions

The shared `UpdatedNumber` rule for modern/deprecated INC and DEC preserves
finite Number and proven integrality. Pinned integer paths guard overflow;
binary64 addition/subtraction by one cannot overflow a finite input (near the
maximum magnitude, one is smaller than half an ULP). Infinities remain non-NaN
without becoming finite. The rule deliberately drops nonzero and tagged-minimum
exclusion: updates can produce zero or tagged INT32_MIN. General Language/NaN
input admission is unchanged.

This lets decoded exponent expressions consume integer results of updates using
the existing exclusive-scratch lowering and common precedence printer. Ordinary
and phased tests cover the four update encodings, signed zeros, finite extremes,
subnormal inputs, infinities and rejected NaNs; nested exponent fixtures cover
modern update-produced exponents and 200/201/geometric boundaries. No source
spelling, sample identity or special expression writer supplies the proof.

## Bitwise normal-result facts

`BitwiseValues` and `UnsignedShiftValues` are shared by ordinary/phased SSA and
linear lexical analysis. Pinned RuntimeShl2/Ashr2/And2/Or2/Xor2 perform ToNumeric
left before right and reject mixed Number/BigInt. If either operand is proved a
Number (including NaN), every normal result is an int32 Number: finite and integral,
but potentially zero or tagged INT32_MIN. With two unclassified Language inputs,
the result is only Defined because BigInt remains possible.

RuntimeShr2 and BigInt::UnsignedRightShift reject every BigInt unsigned-shift
path. Thus two Language inputs give a finite integral uint32 normal result,
which independently excludes the negative tagged minimum. Neither rule removes
coercions or abrupt completion. They enable common compositions such as
`(1 << value) ** 2` and `(left >>> right) ** 2` without recognizing a source-text
pattern or introducing another writer.

A safe bitwise result alone does not authorize changing an input NaN. The
use-specific integer-consumer proof below permits canonicalization only when
no payload observer remains; other source-literal uses stay refused. Runtime NaN parameters, including a noncanonical
payload, are exercised separately through the ordinary/catch generated code.

## Number type versus arithmetic safety

`Number` is a distinct refinement between NonNaNNumber and Defined. Accepted
FLDAI NaNs and the canonical NaN constant establish Number without claiming
non-NaN, finiteness, integrality or a representation exclusion. Impure encodings
still remain Unknown. Joins preserve Number only when every alternative is a
Number; joining with String or a possible BigInt loses that type fact.

The typed ToNumber rule now uses `ConvertedNumber`; ToNumeric keeps its separate
BigInt-preserving rule. Bitwise operations and NOT can use a Number type proof
because their normal Number path converts through int32/uint32, including NaN.
NOT yields a finite integral Number for a proved Number input and otherwise only
Defined. Neither signed bitwise nor NOT inherits a tagged-minimum exclusion.

This admits decoded `(+left << +right) ** 2` and NOT-produced integer exponents
through the existing expression tree. General ADD/SUB/MUL/DIV/EXP and numeric
update/negation still require their independent stronger premises. A Number
fact is not permission to ignore NaN payloads or exceptional conversion. The
execution-equivalence contract has not changed with this refinement.

## ToNumeric identity for proven Number

Source planning and independent operand verification now use `NumberEvaluation`
for ToNumeric. A proved Number is returned unchanged, including NaN and signed
zero; no postfix scratch or hidden arithmetic is needed. The Number proof may
come from ToNumber or another verified producer. Without it, the existing
Number/BigInt-preserving scratch lowering and resource-exclusion gate remain.

Literal classification is shared through `FloatNumberKinds`, including the
pinned impure-NaN boundary. Integer and canonical special Number operands are
handled explicitly; VM-private bit patterns and invalid ValueIds do not become
Number identities. Source representability of noncanonical NaN literals remains
an independent boundary.

Decoded modern/deprecated ToNumber-to-ToNumeric chains run under V1 as well as
ordinary/catch paths, with no UpdateExpr. Removing the Number witness causes the
independent verifier to reject the previously built identity. Behavior tests
check one conversion per occurrence, errors, input preservation, signed zero and
NaN payloads. This removes a lowering artifact; it changes no equivalence scope.

## NaN payload erasure at integer consumers

A valid noncanonical Number NaN may be represented by canonical NaN only where
its payload cannot be observed. Direct bitwise/shift operands and NOT consume
Number through ToInt32/ToUint32; Number/BigInt mismatch and conversion order
remain unchanged. Other operators do not receive this permission.

For materialized constants, the source evaluation projection compresses exact
pure-copy identities once and makes one census of live observations. A constant
is eligible only when every effective use is integer-coercing and at least one
such use exists. Phi transport, returns, calls, stores and guards block the
projection. The independent evaluation verifier checks the producer, complete
observation condition and flag; operand verification checks the canonical
literal only in the authorized producer/use positions. Original Value IR and
source origins remain intact. A second observable use never receives changed
bits as an incidental effect of printing the bitwise expression.

The functional-graph compression and use census are O(values + operations +
uses), with budgeted scratch and one optional per-operation projection table.
There is no per-use copy-chain traversal, constant folding of arbitrary arithmetic
or mutable global helper. Decoded normal/phased tests include noncanonical NaNs,
shared-return refusal, forged flags/literals and 200/201/geometric copy chains.
This is a use-specific erasure proof, not a blanket claim that NaN payloads are
unobservable or that all noncanonical literals can be printed.


## Boolean/null coercion and conditional normal results (rules 80)

`ZeroOrOnePrimitive` describes Boolean or null, not a Number or a wrapper object.
Pinned `JSTaggedValue::ToNumber` in `ecmascript/js_tagged_value.cpp` maps true to
1 and false/null to 0; `ToNumeric` delegates this non-BigInt path to ToNumber.
RuntimeInc/Dec/Neg and binary runtime stubs retain their original conversions,
normal completion and exception effects. Both repository identities remain those
in `openharmony-v7.0-release.lock.json`; no new runtime/dependency is introduced.

The shared `ArithmeticNumber` converts this proved domain to a finite integral
Number excluding the tagged minimum. Ordinary/phased SSA and linear lexical
analysis reuse it in existing arithmetic/conversion rules. The original value
remains non-Number, including after a Boolean/null phi join. Joins with Number or
String weaken to Defined; joins with undefined/general Language or VM markers
cannot authorize a numeric conversion fact. There is no guessed parameter,
property, host API, debug-type or printed-expression evidence.

Boolean-producing unary/comparison operations have this conditional normal
result; typeof has a conditional String result. Shared ValueNode stores that
result separately from fixed literal seeds. It is propagated only after input
obligations hold (including the existing equality marker policy), never injected
before an unproved cycle has input evidence. This admits native comparison
arithmetic and typeof string concatenation without another expression writer.
Legacy minus independently rechecks the same coercion/tag fact on actual value
and literal operands. General NaN, infinity/zero multiplication, possible
zero/zero division, tagged-minimum and arbitrary Language refusals remain.

Encoded ordinary and catch fixtures test Boolean/null literals, ordered/strict
comparison results, Boolean/null phi transport, modern ToNumber/ToNumeric and
negation, legacy negation, all four numeric update formats and NOT in arithmetic
initializers. Both operand positions use actual encoding; three profiles and
1/200/201/402 phi dimensions are checked. The independent native oracle exercises
coercion/getter/Proxy mutations, Symbol/BigInt, signed zeros, NaN payload inputs
and arbitrary thrown values; the newly added primitive matrix has 112840
comparisons. The branch fixture uses real named property writes: first-own-store
aggregate projection spanning a branch is still an existing source-evaluation
gap, not claimed successful here. Complete mode/independent tree checks verify
actual generated source, not a reference-source return or INCOMPLETE success.

This refinement does not prove the fixed stage-one inputs' parameter/property
arithmetic. Native JS comparison is not execution of the pinned Ark VM binary,
and these component tests do not close full-record/platform/style acceptance.

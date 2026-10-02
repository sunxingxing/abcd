# Shared numeric facts

The common value analysis distinguishes FiniteNonzeroNumber, FiniteNumber
(including both signed zeros), NonNaNNumber, Defined and Language. Numeric
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
lexical analysis calls the same scalar helpers. ToNumber preserves all facts of a proved non-NaN Number; otherwise a Language
input has a Defined normal result. The pinned conversion returns Number or
throws, including BigInt/Symbol and conversion-hook failures; it does not imply
non-NaN output. ToNumeric has the same Defined normal-result bound while retaining BigInt
semantics. Known non-NaN Number input uses identity; other inputs use exclusive
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
non-NaN Number, every normal result is an int32 Number: finite and integral,
but potentially zero or tagged INT32_MIN. With two unclassified Language inputs,
the result is only Defined because BigInt remains possible.

RuntimeShr2 and BigInt::UnsignedRightShift reject every BigInt unsigned-shift
path. Thus two Language inputs give a finite integral uint32 normal result,
which independently excludes the negative tagged minimum. Neither rule removes
coercions or abrupt completion. They enable common compositions such as
`(1 << value) ** 2` and `(left >>> right) ** 2` without recognizing a source-text
pattern or introducing another writer.

A noncanonical NaN may have a safe bitwise result while its input literal still
fails the existing source-tree representation contract. Tests retain separate
normal-result and explicit source-refusal checks; this change does not silently
canonicalize such literals. Runtime NaN parameters, including a noncanonical
payload, are exercised separately through the ordinary/catch generated code.

# Explicit language numeric semantics

**Historical / superseded CLI (2026-10-26).** The selector described below has
been removed. Source recovery defaults to `language_source_v10`;
`--language-source` is its optional alias. The remainder records the original
approved premise and evidence, not current CLI instructions. Pinned runtime
analysis facts remain valid. See [the current contract](language-source.md).

**Engine retirement (increment B, 2026-10-27).** The non-source target
identities and their selector plumbing are removed. Old serialized labels are
rejected with a typed identity error, never reinterpreted as SourceV10. The ten
historical SourceV1–V10 identities retain their IDs and labels. Pinned-runtime
numeric/private-value/call/exception facts remain independent analysis inputs;
constructor/capture/forwarding semantics are shared source premises.

User-approved on 2026-10-06. `SourceContract::kLanguageNumericV1`, selected
with `--value-source --language-numeric`, includes the binary arithmetic
premise of [LanguageArithmeticV1](language-arithmetic.md) and the language
numeric meaning of modern/deprecated NEG, INC and DEC. Default V3 and
LanguageArithmeticV1 retain their meanings. The two numeric selectors are
mutually exclusive and require the common value-tree route.

Normal NEG/INC/DEC completion returns Number or BigInt after ToNumeric.
NaN payloads, private tag layout and undefined C++ integer overflow are outside
this explicit equivalence declaration. Operand evaluation, conversion hooks,
original thrown values, receiver identity, execution count, scopes and
exception/effect order remain obligations of the existing common analyses.
This is a language equivalence premise, not measured Ark binary agreement.
It does not supply a constructor brand or a new host lifetime assumption.

ToNumber and ToNumeric retain their distinct typed adapters, including both
deprecated encodings: ToNumber rejects BigInt, while ToNumeric preserves it.
Conversions are never removed because a later numeric consumer exists.
Known Number ToNumeric uses the existing identity path; general conversion
and INC/DEC use the existing exclusive scratch and native postfix/prefix
UpdateExpr. Resource exclusions and scratch ownership retain V3's rules.

## Common recovery

The source contract maps to `ArithmeticSemantics::kSourceNumeric`. The shared
scalar adapter retains unary operation identity and passes this premise to
normal/phased sparse SSA and linear lexical analysis. `SourceUnaryValue`
preserves existing pinned refinements when sound, otherwise provides Number
for proved Number/boolean/null inputs or Defined for other Language inputs.
It never upgrades Hole, environments, other internal values or unresolved
cycles. Linear analysis snapshots the original input before writing an aliased
accumulator, so classification cannot use the newly written result as input.

Legacy NEG's common forest and independent operand check consume the same
typed arithmetic premise. Its pinned minimum-tag witness remains required
under both old modes. Native UnaryExpr/BinaryExpr/UpdateExpr and the existing
printer own composition and precedence; there is no additional writer or
sample/name/register/PC/sequence/graph-shape admission.

For accepted noncanonical NaN literals, AST bits and instruction origins stay
unchanged. The printer emits native NaN under LanguageNumericV1 because that
contract excludes payload observation. The shared literal classifier still
rejects private-tag encodings; old modes retain their literal restrictions.
Receipts, manifests, proofs and source identities retain `language_numeric_v1`.

The extension adds constant work per scalar node, literal and legacy operand
check. It retains the existing fixed-height sparse worklist and dependency
storage; no additional CFG traversal, dependency or per-variable scan.

Numeric regression also exposed initialization moved into a later argument.
The common snapshot planner now enumerates actual inline evaluation occurrences
and checks assignment availability after provisional parent fusion. Unsafe
assignments and affected input dependencies retain their original Root placement
through a bounded queue. The independent checker rebuilds that index. This
preserves full output rather than refusing a whole function or matching an
update/call shape. Existing cross-block snapshot admission remains an explicit
coverage gap; it is not expanded here.

## Evidence boundaries

`language_numeric_test` decodes all three supported dynamic profiles through
normal and phased source trees. It covers copies/block splitting, operand and
result composition, modern/deprecated conversions and updates, accepted NaN
literals, private input refusal, legacy contract/operand tampering and
200/201/402 scaling. `language_numeric_source_test` independently executes
complete generated functions over Number/String/BigInt/Symbol, signed zero,
NaNs, conversion getters, receiver identity and original exceptions.

`window_numeric_source_test` finishes ABC-only record generation before
opening paired TypeScript. It checks the newly recovered getCalDegree method
against the independent source, including changing coercions, Math/Number
calls and exceptions, and detects numeric mutations. WindowUtil's record is
source-complete in this mode; the test isolates a complete method and never
constructs its class. The fixture supplies identity Observed for the unused
WindowInfo class and removes its decorator for Node parsing. This evidence
does not certify the observation protocol, whole-record behavior or HarmonyOS.

Provenance: pinned OpenHarmony-v7.0-Release ets_runtime commit
`f04900cf951c66c2ea18b2bab5b591d5336c34b9` in
`docs/reference/openharmony-v7.0-release.lock.json`, interpreter-inl.cpp NEG
handlers and RuntimeStubs::RuntimeNeg/RuntimeInc/RuntimeDec/ToNumeric.
The existing ECMA-262 numeric-expression rules supply language equivalence.
No new dependency or license is introduced. Final checks and remaining
phase-one gates are recorded in [status.md](status.md).

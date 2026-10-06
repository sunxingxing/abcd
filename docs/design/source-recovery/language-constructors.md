# Explicit language constructor semantics

User-approved on 2026-10-06. `--value-source --language-constructors` selects
native language `super` semantics for retained `ValueSuperSpread` operations.
It composes independently with `--language-arithmetic` or `--language-numeric`.
The three serialized profiles are `language_constructors_v1`,
`language_arithmetic_constructors_v1`, and `language_numeric_constructors_v1`.
Each inherits exactly its selected arithmetic premise and V3 host/resource/text
policies. Default V3 and the two existing numeric contracts retain their scopes.

This is an explicit source-equivalence declaration, not an inference that a
parent is always a direct JSFunction and not measured Ark-binary equivalence.
Pinned `RuntimeSuperCallSpread` reads the current parent, checks `IsJSFunction`,
then expands its VM array. Native `super(...iterable)` instead uses language
constructor and iterator rules. Proxy/bound parents, iterable observations and
abrupt completion at that instruction follow the native expression in this
mode. Current parent and `new.target` remain invocation identities; external
mutation is permitted. No trusted lifetime-brand premise is introduced.
Range super and implicit raw-frame forwarding retain their existing protocols.
Apply construction still requires its existing direct-function/private-array
proof; the new flag does not silently remove that independent obligation.

## Common implementation

`ConstructorSemantics` is a typed independent adapter. All six current profile
combinations retain distinct source identities and round-trip through existing
receipt/manifest boundaries. Selectors require the common value-tree route,
reject duplicate/detail/legacy/conflicting numeric forms before file access,
and never fall back to another writer.

`SuperArguments` contains an owned `ArgumentRange` or `SpreadArguments` child.
The same expression forest, iterative child/structure walk, operand checker,
function ABI, origins and printer handle both. The spread child is the original
array operand, not an invented rest parameter or a single argument. Constructor
identity stays VM-only; it cannot become an emitted source variable. The fresh
checker invokes the existing sparse `BuildDerivedValueStates` once for the same
original FileFunction/work graph, proving identity through copies, cells and
phis. Original class context, initialization/guards/captures and complete body
preparation remain owned by the existing derived projection and family pipeline.
There is no opcode sequence, PC/register layout, sample/name dispatch, producer
list or second body writer.

Existing effect/exception scheduling retains argument producer order through
native argument evaluation. Independent Node probes confirm that both range
and spread super obtain the current parent after argument evaluation; there is
no additional before-argument lookup snapshot/admission condition. Constructor
identity and receiver initialization remain distinct obligations.

The added scans and child processing are O(operations + uses). Constructor
identity reuses the existing bounded sparse solver and local SSA; its dimensions
and provenance are described in implicit-constructors.md. There is no per-variable
graph walk, new dependency or license. Memory/RSS benchmarks remain paused.

## Evidence and remaining boundaries

`language_constructors_test` decodes all three supported dynamic profiles,
three independent constructor/numeric combinations, rest and ordinary array
inputs, copies, block splitting and 200/201/402 scaling. All 198 positive trees must be
complete and pass fresh tree semantics/instruction origins. Old contracts,
missing owner input, foreign constructor and stale contract are refused. Getter
operands and their original effects use the unchanged common evaluation plan.

`language_constructors_paired_test` finishes all ten ABC-only record generations
before opening paired TypeScript. It compares constructor argument identities,
current Proxy/bound/arrow parents, alternate newTarget, returned objects,
ordinary field descriptors and original exceptions across 2,450 paired cases.
LocalStorage is an explicit
minimal test host, not a platform implementation. Independent explicit native
super cases check iterator getters/calls/next failures and replacement values;
120 such cases are separate from paired implicit-constructor equivalence.
All three argument/field mutations are detected.

These checks do not certify inherited field setters, iterator contamination
against a paired implicit constructor, all record methods or HarmonyOS behavior.
The explicit spread representation and receiver snapshots may still need source
style/implicit-constructor/field integration. Artifact source-completeness is not
whole-record acceptance and does not complete the phase-one Goal.

Provenance: pinned OH7 ets_runtime commit
`f04900cf951c66c2ea18b2bab5b591d5336c34b9`,
`runtime_stubs-inl.h::RuntimeSuperCallSpread` / `RuntimeSuperCall`, plus existing
language native-super rules and independent Node evaluation-order probes.
Current verification and fixed-five output evidence are recorded in status.md.


## Whole-record continuation (2026-10-06)

`push_ability_record_source_test` uses the explicit constructor selector for
ABC-only complete PushMessageAbility generation. It independently exercises
322 lifecycle and 1,860 delivery scenarios, including original constructor
argument identity, four registration kinds, cross-instance lexical receivers,
module replacement during argument evaluation, original exceptions and native
Promise settlement order. Three valid-JS mutations are detected. Eight further
scenarios link the actual recovered Ability and VoipCallService ESM records,
control concurrent resource requests, and compare raw/pixel/incoming rejection,
static state, async listener and close behavior against the paired module graph.
The test captures the actual async operation started by void `close`, then
awaits it; it never replaces that operation's body with a reference function.

The original default-mode partial-registration test remains registered with its
original restricted scope. Host doubles implement observable platform boundaries
only; these tests are neither HarmonyOS deployment nor fixed-Ark equivalence.
The CLI's executable flag is unchanged. Current evidence and fixture corrections
are recorded in status.md; stage-one whole-corpus acceptance remains open.

`backup_records_source_test` independently generates the three complete backup
Ability records before opening paired TypeScript. It constructs actual instances
and calls both original async methods, including module initialization and the
push record's Promise await expressions. The 1,224 constructor cases cover
dynamic parents, parameter identities, original exceptions, foreign new.target,
prototype-getter failures and fallback in two new.target realms. The 4,068 method
cases compare live import/callee replacement, JSON serialization, intrinsic async
promises, nested thenables, arbitrary rejection identities and concurrent
microtask ordering. Thirteen valid-JS mutations are detected. Native class,
constructor, async method, import/export and initializer representation is
accepted with conservative mutable bindings and local await temporaries.

The prior default-mode partial backup test remains independent. These controlled
whole-record checks use ordinary constructor iterators; they do not expand
paired implicit-constructor equivalence to contaminated iterators, recover type
or const metadata, or certify HarmonyOS/platform execution. Production selectors
and rules are unchanged; current regression evidence is recorded in status.md.

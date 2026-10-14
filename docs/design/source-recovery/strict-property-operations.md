# Strict delete, indexed data definitions, and super reads (Round 50)

These projections add no source-language premise and do not change a contract
predicate. The only newly liftable instructions are DELOBJPROP,
CALLRUNTIME_DEFINEFIELDBYINDEX, and LDSUPERBYNAME (both cache widths).
The pinned references are the repositories/identities in
`docs/reference/openharmony-v7.0-release.lock.json`:
`ets_runtime/ecmascript/stubs/runtime_stubs-inl.h`, RuntimeDelObjProp,
RuntimeDefineField, RuntimeLdSuperByValue; and
`ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`, their handlers.

## Delete

`PropertyDelete` / `ValuePropertyDelete` / `DeleteExpr` are distinct from a
member read. The base register and accumulator key are separate ordered uses.
ToObject, ToPropertyKey and DeletePropertyOrThrow may throw and have effects
before throwing; only successful completion defines the Boolean accumulator.
Normal and phased import use the same register/effect facts. The source node
prints native strict delete, never GetValue followed by some other operation.
Tree structure and operand correspondence require strict syntax and the exact
base/key uses. Recovery retains acquisition, receiver conversion, key conversion
and access; no property getter is inserted. The module output is strict ESM.
A sloppy tree is refused.

## Indexed DefineField

`IndexedDefinition` distinguishes the existing STOWNBYINDEX interpretation from
CreateDataPropertyOrThrow's all-true descriptor. The index is uint32 and is
converted to its exact decimal property name only at the string boundary.
Accumulator is read and preserved on both normal and exceptional paths.

LiteralPlan independently proves a fresh private ordinary object, unobserved
until its ordered definitions finish, with the existing RHS availability and
catch correspondence. Numeric keys need not be dense or increasing. Arrays,
external receivers and staged assignment forms cannot consume this new kind.
The array construction proof explicitly excludes it. Buffer overlays retain
existing key positions and ordered RHSs. Tree operands check the exact numeric
key and stored value against the original operation. The resulting object
literal uses [[Define]], never [[Set]] or a mutable intrinsic helper.

## Super

`PropertyLookup::kSuper` survives semantic IR, Value IR and MemberExpr. Acc is
the receiver; the lookup base is the current function's home object's current
prototype. This is not an ordinary read on the receiver, even when the receiver
is this. Its effects/exceptional completion remain ordered and possibly throwing.

`HomeObjectBinding::kClass` comes from a resolved ClassTreeContext occurrence;
creation-only functions/arrows receive none. The pinned
`jspandafile/class_info_extractor.cpp:470,606` assigns constructor home to the
prototype and member home to the selected class/prototype object. Prepared-family and standalone
publication compare the binding with that immutable context. Conditional tree
verification additionally checks current receiver SSA identity, this owner and
native class callable syntax. The first increment does not infer lexical super
for nested arrows or reconstruct an arbitrary home object.

CallPlan retains the existing unique member-reference and receiver identity
proof. Super lookup is performed before arguments, so a getter/argument throw
and a callee change during argument evaluation preserve their original order.
Recovery records the lookup kind and resolves the home-object prototype as a
separate possibly-throwing reference step; it never performs ToObject(this) as
the lookup-base operation. Its lexical dependency is the callable owner.
Ordinary .prototype/.length analyses cannot consume this lookup. Nullish syntax
does not turn it into an optional access on this. The printer emits the verified
super key and maps the omitted receiver origin to the `super` span.

## Bounds and drift

All new adapters and tags take constant time/storage per instruction. Existing
SSA identity, effect/catch order, private construction and member-reference
proofs retain their own charged indexes and bounds. No opcode-neighbor,
register number, block-shape or sample identity admission was added. Added
fields are included in the existing sizeof-based storage charges. There is no
new per-operation graph traversal or uncharged index.

Ordinary lookups and indexed own stores retain default tags and prior plans.
No naming or owner allocation was added. Already-published text must compare
byte for byte against explore48/explore48s; refusal changes alone are not a
completion claim. apply, iterator/for-in protocols and sendable classes remain
outside this increment.

## Tests and evidence

- `phased_properties_test`: all three operations retain their original exception
  edges; deleting a handler, before-throw effects or success-only Commit evidence
  is rejected by independent phased import verification.
- `property_delete_test`: operand roles, Boolean success, phased exception
  exits, mutation of identity/result/effects, register renaming and copies.
- `property_delete_tree_test`: strictness, read-vs-delete and swapped-input
  mutations; split blocks, 200/201/400/800 and exact work/byte boundaries.
- `indexed_write_test`: imm32 field adapter, malformed/range cases, descriptor
  kind, accumulator preservation and exceptional effects.
- `indexed_define_test`: fresh/private target, escape, destination/kind/order/
  property-key mutations, non-dense keys, copies/split blocks, scale and exact
  work/byte boundaries.
- `super_read_test`: both encodings, receiver/home/lookup mutations, split
  blocks, scale and exact work/byte boundaries.
- The three CLI tests assert actual target instructions and handler ranges
  before Node source-oracle comparisons. Fixture READMEs give commands and
  hashes. Exception constructors and traces are compared, not message/stack.
  This is not an Ark VM execution claim. `property_delete` also retains the
  currently refused phased array-return shape (EvaluationError=2) instead of loosening it.

Acceptance results are recorded in `round-50-results.md` after execution.

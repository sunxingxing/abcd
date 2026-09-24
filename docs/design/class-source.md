# Class Source Integration

WriteCreatedClass is the source integration boundary for one ordinary class
creation occurrence, not a record exporter or reconstruction of later effects.
The caller supplies an unchanged indexed file, decoded caller site and heritage
proof from that caller. Matching PC is checked, but is not proof provenance.

The adapter resolves the constructor pool target using MethodLookup, validates
the complete class literal buffer, reads callType, builds the canonical frame,
decodes only the target body, and invokes the native constructor writer inside
a named class expression. RuntimeName shares the pinned ASCII method-name
normalization with ordinary created functions. The internal named-class binding
provides stable self identity without consulting mutable module bindings.

The current admitted path requires no extends, an empty class member buffer,
simple formal count matching length, and a supported constructor body without
unresolved captures. Nonempty buffers return kMembers; no property is omitted.
Unknown/null heritage, sendable creation, unsupported names, target failures and
unsupported bodies retain typed rejection. Initial method kind is not required
to be BASE_CONSTRUCTOR: the pinned runtime refines it to CLASS_CONSTRUCTOR.

Runtime evidence is ClassInfoExtractor::ExtractClassInfo and ClassHelper class
construction in ecmascript/jspandafile/class_info_extractor.cpp, and
RuntimeSetClassInheritanceRelationship in ecmascript/stubs/runtime_stubs-inl.h.
Even an empty buffer supplies reserved length/name/prototype properties and
prototype.constructor, matching native named-class construction. Class buffer
properties and subsequent bytecode assignments are distinct phases: later
static assignments must still be emitted by the enclosing function/record.

Literal decoding/validation scratch is released before target decoding. Target
vector capacities are charged against the body/scratch byte budget. The source
wrapper consumes output budget before constructor preflight; no whole-output
buffer is allocated by the engine. Configured semantic/budget failure writes
nothing. Actual allocation failure, file mutation or output failure during the
second pass can leave a prefix and require caller-owned atomic publication.

Member-bearing classes need faithful method kinds/descriptors, runtime naming,
home objects and stable method-self identity; emitting constructible function
expressions as native methods is not an acceptable substitute. Derived classes,
captures, complex signatures and caller module flow remain required future work,
not reasons to publish partial classes as complete records.

## Ordinary Method Emission

WriteClassMethod emits native computed-key methods, including static placement,
using the shared parameter/register/body writer. Its private immutable self
binding must hold the exact method before invocation. A caller may capture that
value after a side-effect-free class definition, but cannot look up a mutable
prototype/static property each time the method executes. This preserves self
identity after replacement/deletion and keeps methods nonconstructible.

The primitive requires an ordinary buffer template, initial NONE and explicit
proven effective ordinary kind. These are different facts: the pinned
LiteralDataExtractor::DefineFunctionTemplate and DefineMethodInLiteral mutate
shared MethodLiteral state. An earlier generator/accessor/object-literal role
can invalidate inference from an initial NONE flag. EffectiveMethodKind means
the template/method kind at materialization, and defaults to unknown/rejected.
Production class-member admission must build a cross-role index including
object literals, or otherwise establish the effective kind; checking only this
class's buffer is insufficient. The primitive does not itself establish that
proof. Its integration is described below.

Body home-object operations, unresolved captures, async/generator semantics
and complex formals remain unsupported in this ordinary-member path. Native
accessor admission is described below. Runtime method name must
equal the supplied property key; descriptor/reserved-property compatibility is
also a class integration obligation. UTF-16 keys are source-escaped, not used
as semantic dispatch strings. Semantic and configured-budget rejection leaves
the output untouched under the shared preflight contract.

## Whole-File Role Admission

MethodRoleIndex now scans all decoded methods in an unchanged indexed ABC,
collects typed literal references, sorts/deduplicates payload offsets, and joins
method-bearing tags by actual target membership. Object and array literals are
included, not just class buffers. Ordinary tags alone yield kOrdinary; mixing
ordinary with getter/setter/generator/async-generator roles yields kConflict.
Other-only targets remain kOther. Even unused tags conservatively contribute.

Ordinary and sendable class constructor targets contribute kOther as well:
JitCompilationEnv can copy CLASS_CONSTRUCTOR into shared MethodLiteral state.
The three deprecated implicit-literal instructions, nested literals and any
unreadable/unsupported payload fail the whole proof. No incomplete scan is
published as an ordinary-only certificate. Historical compatibility of these
runtime semantics is not inferred from container recognition.

Two bounded method-body passes count references and then fill an exact-sized
offset array; literal payloads are read once each after sorting. One role byte
per method, reference offsets and current body/literal scratch share the
configured budget. There is no growing hash table, per-target rescan of methods,
or retained collection of all decoded bodies. Moved-from indexes are queryable
as empty. Build once per immutable file context, before dependent admission;
the final bounded asynchronous service integration remains unfinished.

This index discharges the cross-role premise, not source admission by itself.
The caller must still check initial attributes, member template/name/descriptor,
private self identity, frame/signature and body support.

## Ordinary Members In Complete Class Expressions

WriteCreatedClass accepts an optional whole-file MethodRoleIndex from the same
immutable file/index context. Empty buffers need no role certificate. Nonempty
buffers require ordinary templates, initial NONE/non-sendable attributes,
ordinary-only target roles and exact ASCII runtime-name/UTF-16-key equality.
Targets must be real indexed methods with supported ABI/signature/body.
Duplicate keys within a placement, numeric element names, prototype constructor
and static name/length/prototype remain rejected; no members are dropped.

Pinned jspandafile/class_info_extractor.cpp uses Default(true,false,true) for
ordinary members in both fast and dictionary layouts. Native computed-key
methods therefore preserve these descriptors, strict invocation and lack of
constructibility. Prototype members remain methods, not instance fields.
Instance assignments are still emitted in constructor order without hoisting
or bypassing inherited setters.

A nonempty class is created inside an immediate arrow call. After the complete,
side-effect-free class definition, private const bindings capture each own data
method exactly once, before the class escapes. Bodies initialize their function
register from those bindings, never from a mutable member lookup. Generated
binding names avoid the inner class name. No method can run during capture:
keys are constants, heritage is absent and accessors/static initializers are
not admitted alongside these buffer members. Empty class output retains its
existing direct expression form.

Whole-class dry emission validates constructor, every method, captures and
total output limit before the real stream receives any bytes. IO/file mutation
or actual allocation failure still requires atomic publication by the caller.
ClassBuffer capacity remains admitted throughout, but literal input, duplicate
index and individual decoded bodies have separate nonoverlapping lifetimes.
The retained current UTF-16 key is charged against body scratch while constant
reads retain their separately admitted string allowance. Metadata, role index,
stream buffers and constant-size writer/allocator overhead are caller-owned.

For P properties, M indexed methods and total admitted body/key bytes B, member
admission uses O(P log M) lookups and O(P log P) name comparisons (including
the actual compared character lengths). A fixed number of streaming body/key
passes is O(B) plus existing per-body proof costs; no collection of all bodies
or all output is retained. Duplicate validation sorts borrowed name views and
preserves the original property order for emission.

This integration does not finish the current source gate: unresolved captures,
derived classes, complex/async signatures, global declarations and record
publication remain required. A separately proven global name-read and bounded
branch-body path now admit the paired BreakpointType class site, including its
four ordered constructor assignments and branching `getValue` method. These
are class expressions, not complete modules or inferred source field
declarations; inherited setters remain observable and the assignments must
stay in the constructor body.

## Post-Class Accessors

The record writer can now consume a `DEFINEMETHOD` result exactly once through
`DEFINEGETTERSETTERBYVALUE` when its home object and destination are the same
fresh unescaped class or prototype. It preserves the method pool target,
length, constant key, and false enumerable flag. The pinned v7 runtime marks
the method as getter/setter and sets enumerable=false for class receivers;
native computed get/set syntax reproduces those descriptors and function
names. The class is emitted only after the accessor sequence is proved.

Accessor bodies must not observe their own function ABI slot; dead compiler
copies are permitted through a linear register-window proof. Reusing a target
method, duplicate accessor kind/key, unsupported body, foreign owner,
non-NONE initial kind, sendable target, malformed key, class-buffer key
collision, mixed static-field definitions, or class escape rejects the complete
record source. Disjoint ordinary buffer members retain their captured identity
and can coexist with later accessors; a synthetic ABC and Node oracle cover
that combination and a collision refusal. That stage did not yet publish
the real UI entries.

## Attached Methods And Nested Fields

The pinned v7 `RuntimeStOwnByName` defines a non-enumerable, writable,
configurable own data property on a class constructor or class prototype.
The record writer now folds only an unescaped, same-home-object
`DEFINEMETHOD` followed by `STOWNBYNAME` into a native class method. It checks
the indexed owner, unseen method role, name/key equality, placement and
reserved keys, and rejects collisions with buffer methods or accessors. A
private post-class binding captures each attached method's identity. Derived
class emission may be delayed only across typed pure class-assembly opcodes;
other operations force emission before they execute. This keeps later
accessors and attached methods in source order without moving observable
effects past class construction.

Fresh object field recovery tracks multiple private receivers by register.
An accumulator alias must be overwritten before a receiver is eligible, and
any register observation, overwrite or unknown effect revokes freshness.
Branch bodies reset this proof at basic-block boundaries, so fields in
different arms are not coalesced. Ordered body emission uses the same proven
field batches as straight-line lexical bodies. The retained register table is
allocated once per method; per-block resets are constant time. Unsupported
closures still reject the whole record rather than publishing partial source.

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
proof and is not yet connected to WriteCreatedClass.

Body home-object operations, unresolved captures, async/generator/accessor
semantics and complex formals remain unsupported. Runtime method name must
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
private self identity, frame/signature and body support. Current class creation
generation continues to reject nonempty member buffers until these are wired
together.

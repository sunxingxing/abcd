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

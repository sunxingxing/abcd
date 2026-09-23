# DEFINE instruction coverage (OpenHarmony v7)

Reference: pinned `OpenHarmony-v7.0-Release` ISA table and
`arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` handlers.
All entries below are decoded through the generated ISA table. A typed lift is
not itself permission to emit executable source. Historical profiles retain
format decoding but require a separate execution-semantics proof.

| Family | Typed operands and effect | Executable-source admission |
| --- | --- | --- |
| `DEFINEFUNC` narrow/wide | `FunctionCreation`: pool method, observable length, current-function home; target attributes/capture checked separately | Verified created functions only |
| `DEFINEMETHOD` narrow/wide | `FunctionCreation`: pool method, observable length, accumulator home | Fresh-class constant static initializer or a proved native class accessor subsequently consumed by `DEFINEGETTERSETTERBYVALUE` |
| `DEFINECLASSWITHBUFFER` narrow/wide | `ClassCreation`: constructor, literal, length, parent register | Ordinary unescaped class with proved absent heritage and supported constructor/members |
| `DEFINEFIELDBYNAME`, `DEFINEPROPERTYBYNAME` | `OwnWrite`: string pool name, receiver register, accumulator value; both call `DefineField` | Constant or single-parent literal-node definitions on an unescaped record object, or proved static initializer on fresh class |
| `CALLRUNTIME_DEFINEFIELDBYVALUE` | `DefineFieldByValue`: key/object registers, accumulator value; calls `DefineField`, preserves accumulator | Static initializer on its own class, or a fresh unescaped record object; key must be a proved string or nonnegative integer constant |
| `CALLRUNTIME_DEFINEFIELDBYINDEX` | `DefineFieldByIndex`: unsigned 32-bit key, object register, accumulator value; calls `DefineField`, preserves accumulator | Static initializer on its own class, or a fresh unescaped record object; index rendered as a decimal computed property name |
| `DEFINEGETTERSETTERBYVALUE` | `DefineAccessor`: object/key/getter/setter registers, accumulator enumerable flag; replaces accumulator with runtime result | Native get/set output for a fresh unescaped class/prototype, constant string key, one getter or setter method, false flag and proved v7 target/ABI/body |
| `CALLRUNTIME_DEFINEPRIVATEPROPERTY` | `DefinePrivateProperty`: lexical level/slot, object register, accumulator value; preserves accumulator | Not yet emitted: must resolve private name and brand from lexical environment |
| `CALLRUNTIME_DEFINESENDABLECLASS` | `DefineSendableClass`: constructor/literal/length/parent; creates a shared class | v7 structure site retains every operand and resolved constructor/buffer references with `properties: null`, `source_admission: false`; shared-function identity and runtime restrictions prevent ordinary JS class output |
| `DEPRECATED_DEFINECLASSWITHBUFFER` | `DefineLegacyClass`: constructor, encoded literal, length, lexical environment and parent registers; pinned v7 runtime uses constructor slot + 1 as literal | v7 structure site retains encoded literal and derived buffer reference with `properties: null`, `source_admission: false`; source needs lexical-environment proof |

The six final families use `LiftDefineInstruction`. Its returned variant retains
operands that the pinned handler does not read, for diagnostics and future
profile comparison. The generated decoder validates wire widths; the lift
validates operand kinds, ranges, and register bounds. Unsupported or malformed
instructions never fall through to an assembly/fake-body emitter.
Historical profiles still decode these encodings, but the v7-only class-site
interpretation reports `class_profile` instead of applying unproven legacy or
sendable execution semantics. A synthetic v7 bytecode test checks both site
shapes and frame bounds; the inspected Immersive sample has neither form.

Static initializer admission is deliberately narrow. `RecoverStaticFields`
requires a zero-argument v7 method whose straight-line body only constructs
supported constants, defines data properties on its own `this`, and returns
undefined. The record interpreter separately proves an unescaped class,
`DEFINEMETHOD` home-object provenance, and one `CALLTHIS0` with that class as
receiver. The emitted `static [name] = value` preserves own-property descriptor
flags. Existing class-buffer members, `prototype`, array alias reuse, unknown
keys, side effects, exceptions, and control flow are rejected. The real
`CommonConstants` sample has an executable-source oracle; computed field
forms have a dedicated bytecode recovery test but need a record-level source
oracle.

`WriteClassMethod` emits native getter/setter syntax when the caller proves
method kind and placement and the function ABI slot is unobservable.
Its register-window proof permits dead copies of the function slot in linear
time and memory. A Node oracle checks getter/setter names, lengths,
nonconstructibility, descriptors and calls. The record writer now tracks fresh
class/prototype identity, method creation, string keys and the false
enumerability flag through each `DEFINEGETTERSETTERBYVALUE`. A synthetic ABC
record validates the full bytecode-to-ESM chain and its negative kind path.
Disjoint class-buffer ordinary members can coexist with these later
accessors; same-position key collisions and constant static fields combined
with accessors remain conservatively rejected.
The Immersive corpus has 4/45 executable records after nested record literals
admit `Constants`; the UI entries with
accessors fail earlier at global/branch prelude instructions, so the synthetic
oracle is not evidence of full real-app source coverage.

Fresh object/array identity, buffered constants and dense indexed writes for
record source are specified in [record-literals.md](record-literals.md).
The synthetic record oracle also exercises both runtime computed-field forms
through decoded ABC and Node descriptors. Dynamic keys requiring `ToPropertyKey`
effects are refused rather than converted from printed source.

Next source work: handle the real UI record preludes and static-field/accessor
ordering; recover private lexical names/brands; represent sendable classes without falsely
lowering to ordinary JS; add historical execution profiles and a record-level
computed-key execution oracle. Corpus coverage must report these as semantic
refusals until those proofs and tests exist.

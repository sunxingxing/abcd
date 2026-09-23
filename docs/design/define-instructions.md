# DEFINE instruction coverage (OpenHarmony v7)

Reference: pinned `OpenHarmony-v7.0-Release` ISA table and
`arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` handlers.
All entries below are decoded through the generated ISA table. A typed lift is
not itself permission to emit executable source. Historical profiles retain
format decoding but require a separate execution-semantics proof.

| Family | Typed operands and effect | Executable-source admission |
| --- | --- | --- |
| `DEFINEFUNC` narrow/wide | `FunctionCreation`: pool method, observable length, current-function home; target attributes/capture checked separately | Verified created functions only |
| `DEFINEMETHOD` narrow/wide | `FunctionCreation`: pool method, observable length, accumulator home | Only a fresh-class static initializer proved to perform constant field definitions, invoked once with that class as `this` |
| `DEFINECLASSWITHBUFFER` narrow/wide | `ClassCreation`: constructor, literal, length, parent register | Ordinary unescaped class with proved absent heritage and supported constructor/members |
| `DEFINEFIELDBYNAME`, `DEFINEPROPERTYBYNAME` | `OwnWrite`: string pool name, receiver register, accumulator value; both call `DefineField` | Fresh-object definitions or proved static initializer on fresh class |
| `CALLRUNTIME_DEFINEFIELDBYVALUE` | `DefineFieldByValue`: key/object registers, accumulator value; calls `DefineField`, preserves accumulator | Static initializer only when receiver is its own class and key is a proved string or nonnegative integer constant |
| `CALLRUNTIME_DEFINEFIELDBYINDEX` | `DefineFieldByIndex`: unsigned 32-bit key, object register, accumulator value; calls `DefineField`, preserves accumulator | Static initializer on its own class; index rendered as a decimal computed property name |
| `DEFINEGETTERSETTERBYVALUE` | `DefineAccessor`: object/key/getter/setter registers, accumulator enumerable flag; replaces accumulator with runtime result | Not yet emitted: needs callable/accessor identity and descriptor/flag equivalence |
| `CALLRUNTIME_DEFINEPRIVATEPROPERTY` | `DefinePrivateProperty`: lexical level/slot, object register, accumulator value; preserves accumulator | Not yet emitted: must resolve private name and brand from lexical environment |
| `CALLRUNTIME_DEFINESENDABLECLASS` | `DefineSendableClass`: constructor/literal/length/parent; creates a shared class | Not emitted as ordinary JS class; shared-function identity and runtime restrictions are observable |
| `DEPRECATED_DEFINECLASSWITHBUFFER` | `DefineLegacyClass`: constructor, encoded literal, length, lexical environment and parent registers; runtime uses constructor slot + 1 as literal | Not yet emitted: explicit legacy profile and lexical-environment proof required |

The six final families use `LiftDefineInstruction`. Its returned variant retains
operands that the pinned handler does not read, for diagnostics and future
profile comparison. The generated decoder validates wire widths; the lift
validates operand kinds, ranges, and register bounds. Unsupported or malformed
instructions never fall through to an assembly/fake-body emitter.

Static initializer admission is deliberately narrow. `RecoverStaticFields`
requires a zero-argument v7 method whose straight-line body only constructs
supported constants, defines data properties on its own `this`, and returns
undefined. The record interpreter separately proves an unescaped class,
`DEFINEMETHOD` home-object provenance, and one `CALLTHIS0` with that class as
receiver. The emitted `static [name] = value` preserves own-property descriptor
flags. Existing class-buffer members, `prototype`, array alias reuse, unknown
keys, side effects, exceptions, and control flow are rejected. The real
`CommonConstants` sample has an executable-source oracle; computed field
forms currently have typed-lift tests but need dedicated source oracles.

Next source work: prove getter/setter object and callable identity; recover
private lexical names/brands; represent sendable classes without falsely
lowering to ordinary JS; add historical execution profiles and dedicated
computed-key bytecode fixtures. Corpus coverage must report these as semantic
refusals until those proofs and tests exist.

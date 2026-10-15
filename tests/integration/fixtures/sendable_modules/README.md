# Sendable module reads and shared initialization

Handwritten independent fixtures, compiled by DevEco's dynamic es2abc:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
Compiler SHA256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root (replace NAME by cache or shared):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --extension=ts --opt-level=0 --output=tests/integration/fixtures/sendable_modules/NAME.abc tests/integration/fixtures/sendable_modules/NAME.ts
```

The input dependency is intentionally unresolved by the compiler. The Node test
provides its own deterministic module with logging constructors/getters, async
lock calls and a small store. It is a source oracle, never an engine input.

| File | SHA256 |
|---|---|
| cache.ts | 1b7af4ba0770d318a69b4ce94e1fd552d60712b9e185d8cec8273718bbe6a861 |
| cache.abc | c23f0d787465dbbba91ac5fabfa1a7bb53b277e443979fe338d83c9b8eb3bd3e |
| shared.ts | b1dd9121ac42f77321499aa3debedc6d3d8ad8757d61d5456a3f31d6e9d0b5d1 |
| shared.abc | 07b02f1762e0fe7e1716c762e1e2ad22187758fd8da6e33db0b4670007228e7e |

Both fixtures contain `definesendableclass`, two Sendable external module reads
with named hole guards, two GENERIC(32) instance fields, and three async members.
Each member's ordinary inner arrow captures its own member's cells; these are
not captures crossing the outer Sendable boundary. shared additionally creates
and exports a single instance. Tests decode and assert these facts before
comparing engine output and a real recompile (`--extension=ts`, same O0 options).

The independent metadata/body probe compares shared/CJS/TLA, requests and all
import/export entries, class kind and field masks/split, constructor signature,
async resolve/reject/enter counts, callback relations, module domain/slot/guard,
and ordered strings, constructs, property sets/calls and export stores, plus
first-matching-handler dispatch for effect events and module guards. Native let
recompilation may add redundant initialized-capture HoleGuards; the probe does
not demand raw guard-count identity for these, and existing cell/recovery proofs
remain responsible for their availability. Existing
production tree/recovery verification additionally checks value flow and
exceptions; probe equality alone is not a proof of arbitrary program equivalence.
The target LruCache uses the same procedure with the original record descriptor.

Node executes only mechanically type-erased storage descriptions. The
`"use sendable"` and `"use shared"` directives remain strings. It compares
construction and [[Set]] effects, getter/call order, ordinary async resolution and
microtask traces, exceptions by constructor, rebinding of the actual import,
module initialization and stable export identity. Mutations remove a constructor
[[Set]], change a mask/directive/export, or corrupt IR/binding/guard evidence.
Shared heap, field type enforcement, real locks and cross-thread behavior remain
an explicit Ark runtime-verification gap. No finally-throw oracle is used here.

Generated 200/201/400/800-field fixtures exercise imported type-path indexing,
complete engine export and real recompilation. Unit verification checks exact
byte/work bounds, and corrupts field/assignment/constructor/binding/path evidence.
Raw Sendable-read tests exercise imm8/wide, handlers, uninitialized module inputs
(no fabricated LanguageLoadFact), missing guards and domain changes at the same
scales, with exact exception-module bytes and evaluation work boundaries.

GENERIC spelling is selected from an actual import-backed constructor assignment.
Direct imported constructors (a zero-length member path) use the same proof.
It is not claimed to be the unique original nominal type. Unproved/ambiguous
spellings remain typed kTypeSpelling; no `any`, fake import or initializer is added.

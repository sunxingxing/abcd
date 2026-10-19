# ISA property variants: runtime and emission evidence

Baseline: `5e057b6f`. This increment is **evidence-only so far**: no production
lifter, verifier or printer behavior has changed. Refusal is not support.

References are pinned by `docs/reference/openharmony-v7.0-release.lock.json`:
ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9`,
ets_frontend `c754cf6300873cdd8cdb71c7340a98abfd885389`.
The source and constructed ABC hashes are in
`tests/fixtures/isa/properties/manifest.json`.

## Per-variant measurements

Every listed handler calls a real fast/slow property implementation, checks
abrupt completion, and dispatches onward. **None is NonExecutable.** Each
constructed specimen passes both the engine decoder and SDK ark_disasm.
O0/default columns below are observations of the checked-in source, not an
exhaustive proof that no source can emit an instruction.

| Variant | interpreter-inl.cpp line | Runtime | es2abc O0 / default | Current raw lift |
|---|---:|---|---|---|
| `LDSUPERBYVALUE_IMM8_V8` | [5669](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5669) | implemented | yes / yes | UnsupportedInstruction |
| `STOBJBYINDEX_IMM8_V8_IMM16` | [5811](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5811) | implemented | not observed / not observed | UnsupportedInstruction |
| `LDTHISBYNAME_IMM8_ID16` | [7286](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7286) | implemented | not observed / not observed | UnsupportedInstruction |
| `STTHISBYNAME_IMM8_ID16` | [7404](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7404) | implemented | not observed / not observed | UnsupportedInstruction |
| `LDTHISBYVALUE_IMM8` | [7056](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7056) | implemented | not observed / not observed | UnsupportedInstruction |
| `STTHISBYVALUE_IMM8_V8` | [7162](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7162) | implemented | not observed / not observed | UnsupportedInstruction |
| `LDSUPERBYVALUE_IMM16_V8` | [5685](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5685) | implemented | yes / yes | UnsupportedInstruction |
| `STOBJBYINDEX_IMM16_V8_IMM16` | [5840](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5840) | implemented | not observed / not observed | UnsupportedInstruction |
| `STOWNBYNAMEWITHNAMESET_IMM8_ID16_V8` | [2966](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2966) | implemented | yes / yes | UnsupportedInstruction |
| `LDTHISBYNAME_IMM16_ID16` | [7345](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7345) | implemented | not observed / not observed | UnsupportedInstruction |
| `STTHISBYNAME_IMM16_ID16` | [7476](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7476) | implemented | not observed / not observed | UnsupportedInstruction |
| `LDTHISBYVALUE_IMM16` | [7109](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7109) | implemented | not observed / not observed | UnsupportedInstruction |
| `STTHISBYVALUE_IMM16_V8` | [7224](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7224) | implemented | not observed / not observed | UnsupportedInstruction |
| `STOWNBYVALUEWITHNAMESET_IMM8_V8_V8` | [2929](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2929) | implemented | yes / yes | UnsupportedInstruction |
| `STSUPERBYVALUE_IMM8_V8_V8` | [2773](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2773) | implemented | yes / yes | UnsupportedInstruction |
| `STSUPERBYVALUE_IMM16_V8_V8` | [6763](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6763) | implemented | yes / yes | UnsupportedInstruction |
| `STSUPERBYNAME_IMM8_ID16_V8` | [3183](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L3183) | implemented | yes / yes | UnsupportedInstruction |
| `STSUPERBYNAME_IMM16_ID16_V8` | [6835](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6835) | implemented | yes / yes | UnsupportedInstruction |
| `STOWNBYVALUEWITHNAMESET_IMM16_V8_V8` | [6860](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6860) | implemented | yes / yes | UnsupportedInstruction |
| `STOWNBYNAMEWITHNAMESET_IMM16_ID16_V8` | [6897](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6897) | implemented | yes / yes | UnsupportedInstruction |
| `WIDE_STOBJBYINDEX_PREF_V8_IMM32` | [5869](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5869) | implemented | not observed / not observed | UnsupportedInstruction |

## Exact runtime distinctions

- **LDTHISBYNAME / LDTHISBYVALUE**: read `GetThis(sp)`, not the writable ABI
  receiver register. Named keys come from the method pool; computed keys come
  from acc. IC/fast/slow Get can invoke getters or proxies and throw; acc commits
  only after success. **STTHISBYNAME / STTHISBYVALUE** use the same frame
  receiver, acc as value, and pool/register key respectively. They preserve acc
  and use strict Set, not own-definition semantics. The slow paths are the
  ordinary LdObj/StObj runtime stubs, with frame `this` as their receiver.
- **LDSUPERBYVALUE** uses the explicit register as receiver and acc as key.
  `RuntimeLdSuperByValue` (`runtime_stubs-inl.h:707`) reads the current
  function's home object, computes/validates its super base, converts the key,
  then performs Get with that separate receiver. It commits acc on success.
- **STSUPERBYVALUE / STSUPERBYNAME** use explicit receiver, acc value, and
  register/pool key. `RuntimeStSuperByValue` (`runtime_stubs-inl.h:725`) validates
  the home object's super base, converts the key, then Set(base,key,value,
  receiver,true). Acc is preserved even on the exceptional snapshot. A source
  `super` reference requires the original home object and receiver identity.
  Computed-key conversion must not be reordered across super-base validation.
- **STOBJBYINDEX** calls `RuntimeStObjByIndex` (`runtime_stubs-inl.h:782`),
  forwarding an unsigned index to `JSTaggedValue::SetProperty`'s uint32 overload
  (`js_tagged_value.cpp:1210`). This is strict Set, not STOWNBYINDEX. Acc stays
  unchanged. The uint32 overload sends a **numeric tagged key** to Proxy Set
  (`js_proxy.cpp:624,650`), which asserts a property key and passes the key
  unchanged to the trap. Native bracket assignment supplies a string key.
  This is a pinned-source divergence/debug-precondition issue, not an Ark
  execution result. The existing read-side warning in
  `docs/reference/property-indices.md` applies analogously: generic bracket
  lowering needs a receiver/path proof, including upper-half uint32 handling.
- **STOWNBY*WITHNAMESET** first defines the own data property, then mutates the
  function name. `RuntimeStOwnByValueWithNameSet` (`runtime_stubs-inl.h:1291`)
  converts the key and uses writable/configurable=true and enumerable=false
  for class constructors/prototypes; ordinary objects are enumerable=true.
  `RuntimeStOwnByNameWithNameSet` (`:1340`) does likewise with the pool key.
  The fast paths call `SetFunctionNameNoPrefix` after successful definition;
  the slow paths call `SetFunctionName`. These are ordered heap effects and
  must not be silently represented as a name-neutral OwnWrite. The computed
  slow path checks IsJSFunction, while fast paths assume function input.
  Anonymous-function origin and source inference must be proved before a
  literal can replace both effects.

## Frontend evidence and real emission

`compiler/core/pandagen.cpp`: StoreObjByIndex at 644 (uint16 vs wide),
StOwnByName/Value at 664/671 (nameSetting selector), StSuperByName at 1957,
StSuperByValue at 1970, LdSuperByValue at 1975. Format/cache widening chooses
imm8/imm16. No Ldthisby/Stthisby emitter was found in the pinned dynamic frontend;
ordinary `this` property expressions emit explicit receiver operations in both
actual compiler runs.

`ir/expressions/objectExpression.cpp:61` has the unusual IsLegalNameFormat test
(both dot and backslash); :354-384 chooses nameSetting for computed anonymous
functions/methods and qualifying noncomputed names. The fixture exercises all
four name-setting variants and all six super variants. `StoreObjByIndex` has a
real PandaGen emitter, but the numeric bracket assignment probe reaches the
register-key StoreObjByValue path. No claim of exhaustive non-emittability is
made for the three indexed stores.

## Implementation boundary discovered

The 21 current refusals cannot safely be changed by adding opcode aliases:

1. `NamedRead` currently implies an acc receiver; `NamedWrite` / `ValueWrite`
   require an explicit register. Frame-this accesses need an immutable frame
   receiver operand/proof; using the writable ABI register is unsound.
2. Round-50 home-object proofs cover named super reads. Computed super and
   super writes must carry lookup/receiver identity and key-coercion order
   through operands, reference creation, effects and recovery. These fields
   are absent from the existing computed-read/write operations.
3. Name-setting needs a typed ordered naming effect plus a private literal/
   anonymous-function identity proof; existing OwnWrite loses that effect.
4. Indexed Set must retain its uint32 runtime domain. An unconditional mapping
   to `obj[index] = value` is not premise-free for arbitrary receivers. No new
   premise is assumed; a receiver-domain proof remains a valid faithful route.

No production edits are included in this evidence slice. The requested lifting,
source projection, independent verification and engine-output Node differentials
remain unfinished. Tests assert the current typed gap, not permanent refusal;
update those assertions as each semantic family is implemented. Node / Ark
round-trip / Ark runtime columns remain empty. Constructed decoding and frontend
emission do not justify SupportedVerified or NonExecutable classification.

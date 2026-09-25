# Exception boundary decision

Status: user selected source-language/host-contract equivalence on 2026-09-25.
Production admission remains strict until this contract is wired explicitly.

## Accepted execution premise

The user confirmed: legitimate JS/ArkTS source and compliant host APIs may
throw any JavaScript value, but do not expose VM-internal tagged markers.
This is an execution-environment assumption, not evidence inferred from ABC.
Record it explicitly in admission, emitted artifact metadata and eventual cache
keys. Preserve arbitrary JS thrown values; do not replace them with Error.
Internal ABC operands and explicit throws still require independent validation.
Arbitrary-tagged-runtime equivalence is outside this selected source contract.

## Concrete unresolved boundary

Pasteboard method 6733, `checkPermissionGrant`, reaches exception input
verification after capture-preservation integration. Catch PC96 spills the
pending exception into a register. Its ten incoming PendingException sites are:

| Original PC | Typed operation | Missing evidence |
| --- | --- | --- |
| 22, 38 | ModuleLoad | Module/runtime exceptional payload |
| 24, 40 | HoleGuard | Incoming pending state and runtime error paths |
| 32, 48, 56, 76, 88 | NamedRead | Runtime and possible getter/proxy payloads |
| 64 | ReceiverCall | Callee/host exceptional payload |

These are actual ABC exception edges, not inferred source syntax. The input
artifact is `build/pasteboard-6733-instructions.jsonl`. None of these ten sites
is an explicit ThrowOperation whose payload can simply follow an SSA operand.
The existing per-edge `ExceptionPendingFact` interface can represent proved
domains, but does not supply their missing premises.

## Pinned runtime evidence

In the pinned OpenHarmony-v7.0-Release ets_runtime repository:

- `ecmascript/js_thread.cpp`, `JSThread::SetException`, stores the tagged value
  without a language-category check.
- `ecmascript/js_thread.h`, `HasPendingException`, excludes Hole only.
- `ecmascript/napi/jsnapi_expo.cpp`, `JSNApi::ThrowException`, retains an already
  pending exception, otherwise stores `ToJSTaggedValue(*error)` without checking
  its category. A `Local<JSValueRef>` parameter alone is not a checked payload
  certificate.
- Runtime error creation may preserve an existing pending exception; see the
  prior audit in `exception-source.md`, Edge-Specific Pending Values.

The local reference tree's foundation checkout contains ArkUI, not the
bundle-manager host implementation needed for the actual imported call.
Neither a JS-facing API name nor an Error allocation on one runtime branch
proves all possible payload paths.

## Alternatives Presented

1. Source-language/host-contract equivalence: legitimate JS/ArkTS and compliant
   host APIs may throw any JS value but cannot expose VM internals. This is an
   explicit execution-environment premise, not an ABC-derived proof. Admission,
   emitted artifact metadata and cache keys must preserve the selected contract.
   Explicit ABC throws and internal register reads still require their own
   value verification; the contract cannot sanitize malformed bytecode.
2. Arbitrary-tagged-runtime equivalence: keep unresolved edges Unknown until
   every relevant runtime, callee and host path has been proved. The current
   rejection remains correct under this stronger scope. No runtime wrapper or
   fabricated Error may substitute for an unrepresentable payload.

The first scope is now selected for source decompilation. Its implementation
must still distinguish contract-derived facts from ABC-derived proofs. No
global Language pending policy has yet been enabled, and no successful source
emission or increased coverage is claimed by this decision alone.
Tests and RSS tracking remain paused.

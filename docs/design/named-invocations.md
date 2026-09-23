# Named Invocations

## Semantic Contract

Recover same-block `LDA receiver` or `STA receiver`, followed immediately by
LDOBJBYNAME and fixed CALLTHIS0/1/2/3 using that same receiver. Keep the initial
copy, and fuse the property read and call into native `acc = acc[name](args)`.
The read happens once, before the call, with the original base as receiver.
Register arguments are private language-value bindings with no getter effects.
No intervening instruction, control-flow entry or exception region is crossed.
The accumulator/registers are private VM temporaries: the caller must not wrap
the emitted statements in a catch that exposes their intermediate state. On a
callee throw, the fused assignment retains the base rather than the loaded
callee in acc; this state is dead when the exception exits this handler-free
method. Fusion is not valid for exception-state reconstruction or debug state.

Pinned reference: `ecmascript/interpreter/interpreter-inl.cpp` CALLTHIS handlers
1347-1384 set callThis, take the accumulator callee and the explicit receiver
register. The common call paths push raw receiver and undefined new.target;
fixed argument macros preserve register argument order. Existing named-read
semantics, including rejecting canonical array-index names, remain binding.

Alternatives: arbitrary receiver calls through `.call`/`.apply` or global
Reflect introduce mutable property/intrinsic dependencies and are rejected.
General SSA/effect-aware expression scheduling is a later extension of this
proof, not a reason to speculate across writes or branch entries now. Legacy,
with-name and range variants require separate evidence and are not accepted.

## Deferred Tail Calls

The lexical-body path additionally admits a handler-free, terminal `CALLTHIS1`
whose callee came from a named read, was spilled to a private register, and is
reloaded immediately before the call. A backward register-access scan proves
the spill has no intervening readers or writers; a forward scan proves the
receiver register is not written. The first instruction after the spill must
overwrite the accumulator without reading it. Only operandless `NOP` and
`LDUNDEFINED` may occur between the call and terminal return/throw. Other
shapes stay unsupported.

The writer emits the native receiver call at the original getter position and
evaluates intervening argument-building statements inside an arrow IIFE. This
keeps getter effects before argument effects, retains the original receiver,
and avoids mutable `.call`, `.apply`, and `Reflect.apply` dependencies. The
extra arrow frame/closure is an implementation limitation for stack-sensitive
code and allocation failure timing; the matcher does not claim general
effect-aware scheduling. The proof and emission are O(I + source bytes) with
constant matcher storage. `LinearLexicals` validates the planned call before
output publication.

## Implementation Plan

1. Add `named_call_test` with a real constant-file fixture, a failing LDA /
   LDOBJBYNAME / CALLTHIS0 / RETURN source assertion and Node execution oracle.
2. Add fixed-storage typed ReceiverCall lifting (register/kind/count bounds)
   and NamedInvocation recognition using typed scalar copy and named-read
   lifts. No strings represent callee provenance or receiver equality.
3. In accumulator analysis, recognize only the proven same-block triple;
   preserve initial-copy preconditions and consume the last two instructions
   as one initialized-accumulator result. Unmatched calls retain rejection.
4. In linear/ordered emission, emit the initial copy normally, fuse only the
   recognized pair and reuse existing property string checks. Validate output
   completely before publishing. Do not use Function/Reflect intrinsics.
5. Exercise fixed arities, STA form, receiver mismatch, malformed operands,
   basic-block boundaries, index-name rejection, exact/short output, getter
   and call ordering, Proxy receivers, primitive receivers, overwritten .call,
   class/non-callable failures and getter/callee exception identity.
6. Run Debug and sanitizer tests, independent review and full corpus under
   1 GB; compare per-file coverage and commit measured results.

Recognition is O(1) per instruction, O(I) over the method and allocation-free.
Emission is O(source bytes), with existing graph/source budgets. It adds native
method-call recovery, not general bound-call synthesis, lexical value proof,
record publication or the asynchronous function service.

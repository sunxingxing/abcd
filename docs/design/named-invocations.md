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
proof, not a reason to speculate across writes or branch entries now. Legacy
and with-name variants require separate evidence and are not accepted.
Narrow `CALLTHISRANGE` is admitted through the typed receiver-call lift. The
pinned v7 `CALL_PUSH_ARGS_THISRANGE` reads the receiver at `startReg` and
`count` arguments from the following contiguous registers. The first
immediate is an inline-cache slot, not an argument count. Source emits each
argument expression directly, without spread or mutable iterator lookup.
Wide and deprecated range layouts remain separate.

## Deferred Tail Calls

The lexical-body path additionally admits a handler-free, terminal
`CALLTHIS1..3` or narrow `CALLTHISRANGE`
whose callee came from a named read, was spilled to a private register, and is
reloaded immediately before the call. The compatibility tail probe uses a
backward register-access scan for the spill and a forward receiver scan;
production uses the function-wide timelines described below. The first
instruction after the spill must
overwrite the accumulator without reading it. Only operandless `NOP` and
`LDUNDEFINED` may occur between the call and terminal return/throw. Other
shapes stay unsupported.

The writer emits the native receiver call at the original getter position and
evaluates intervening argument-building statements inside the first argument's
arrow IIFE. Remaining arguments read their proved language-valued registers
after that IIFE. This keeps getter effects before argument effects, retains the
original receiver,
and avoids mutable `.call`, `.apply`, and `Reflect.apply` dependencies. The
extra arrow frame/closure is an implementation limitation for stack-sensitive
code and allocation failure timing; the matcher does not claim general
effect-aware scheduling. The compatibility tail proof uses constant matcher
storage; production's bounded timelines are described below.
`LinearLexicals` validates the planned call before output publication.

## Staged Nonterminal Calls

The function-wide matcher also admits a zero-argument, nonterminal
`CALLTHIS0` when named read, callee spill, callee reload and call are
consecutive. It emits `receiver[name]()` at the named read, before the private
spill/reload, with the call result left in the accumulator. The two private
copies have no language-visible effects. The same register timelines prove
the receiver unchanged and the eliminated callee register dead after the
call. A live callee, receiver/callee alias, branch or exception region still
rejects the method. This form uses no arrow IIFE.

The same getter-before-argument source form also admits multiple nonterminal
receiver calls in one complete straight-line, handler-free method, provided
their property-to-call intervals do not overlap. Each call still requires the
typed named-read/spill/reload sequence and an accumulator-clobbering first
argument operation. Forward register-access timelines prove the receiver
unchanged and the callee slot unread before reload. Reverse timelines prove
the eliminated callee slot unread after the call until overwrite or method
exit; a read/write is a read and is rejected. The original result remains in
the accumulator for subsequent statements. An overlapping candidate rejects
the whole batch; branch and exception-containing methods remain refusals.

Immediate zero-argument and deferred argument-building calls may coexist in
one disjoint sequence. The timelines cover only the 256 low v8-addressable
registers and charge each expanded access against the method work budget.
Candidate and liveness storage
share the method byte budget. The matcher is O(I + A), where I is instructions
and A is expanded low-register accesses, independent of the number of calls.
The lexical planner validates the ordered matches before output publication.

The arrow IIFE preserves ordinary getter, argument and receiver behavior,
but introduces a JS frame and closure. Stack-observing code and allocation
failure timing are not proven equivalent; this remains an explicit admission
limit rather than a general expression scheduler.

## Nonterminal Pure Copy

`MatchCopiedArgumentInvocation` admits one additional fixed, nonterminal
shape: load receiver; named getter; spill callee; load one register; store it
to a distinct argument register; reload callee; `CALLTHIS1`; immediately
overwrite the spilled callee register with the result. The receiver and
argument registers cannot alias the callee, and argument preparation cannot
write the receiver. The copied register must already hold a language value.
Only private register copies move before the getter, so the getter remains
before every observable effect. No exception region survives source emission;
the async completion normalizer separately proves its catch/reject tail.

The call is emitted at the call site as native `receiver[name](argument)`;
the original getter, spill and reload are omitted, while the pure argument
copy remains. This preserves the callee result's later register use because
the next instruction overwrites its old slot. The matcher is O(1) per call,
with no backward liveness walk. Other deferred nonterminal shapes require
the staged proof above or remain refusals. A synthetic Node oracle and the real Immersive
`EntryBackupAbility.onRestore` record oracle check receiver, getter order,
argument order, result and Promise rejection. The staged nonterminal form has
separate synthetic C++ and Node tests, including post-call callee liveness.

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

Direct adjacent-call recognition is O(1) per instruction and allocation-free.
The staged-call matcher uses the bounded timelines above. Emission is
O(source bytes), with graph/source budgets. These proofs add native method-call
recovery, not general bound-call synthesis or the asynchronous function service.

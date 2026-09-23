# Guarded Arrow Function Source

Pinned OpenHarmony v7.0 `RuntimeDefinefunc` instantiates a `Method` with its
recorded function kind and sets the literal's length. `ObjectFactory::NewJSFunction`
uses the no-prototype class for `ARROW_FUNCTION`. `EcmaInterpreter::Execute`
copies call-info `this` and `newTarget` into the declared ABI slots. Native JS
arrows instead capture these lexically. The source writer therefore rejects
observable reads of either ABI slot and implicit this/newTarget opcodes; it
does not silently map a dynamic call input to a lexical value. It admits at
most two pure `MOV` copies of context slots only when a whole-body scan proves
its destination is never read. This two-pass, constant-space proof is O(body
instructions and operand windows), including branches; it deliberately rejects
copies with later reads even if another write might kill them. Explicit writes
to an unread slot are allowed. Unsupported control flow, captures, instructions
and child kinds keep their existing rejection path.

A complete same-file `MethodRoleIndex` must also prove `kUnseen` for the target.
Pinned class materialization can mutate a shared `MethodLiteral` kind, so the
initial access flags alone do not establish arrow semantics. Constructor and
method-bearing literal roles fail closed, as does a missing role index.

For an admitted `DEFINEFUNC`, emit a native arrow inside an arrow IIFE. A fresh
object's constant computed property applies `SetFunctionName` to the anonymous
arrow without consulting a mutable global intrinsic. The IIFE returns the
arrow and binds its function register through a private captured `const` after
initialization. Neither arrow introduces a dynamic `this`, `new.target` or
`arguments` binding. ABI this/newTarget slots are left uninitialized because
the proof established that the body cannot observe them. The function name is
the pinned runtime-parsed ASCII view, including empty names; non-ASCII names
remain rejected rather than guessed.

The call-context scan is O(instructions + operand windows) time and O(1) extra
space. Name conversion is O(name bytes) with retained UTF-16 capacity charged
against the method budget. Output uses existing two-pass preflight and bounded
streaming. Unit and Node tests cover module-top-level syntax, function name,
length, nonconstructibility, fresh creation identity, self slot, empty name,
and rejection of both observable dynamic call-context slots. This does not
complete arrow bodies with branches/object literals, async arrows, or
record-level source.

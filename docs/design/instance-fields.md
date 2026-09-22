# Instance Field Recovery Gate

Finish instance fields, globals, source signatures and function-body integration
before returning to broader corpus completion. This gate is not satisfied by
more metadata alone.

Start with precise property-write semantics. STOWNBYNAME has a UseOwn fast path
and StOwnByName fallback; DEFINEFIELDBYNAME calls DefineField. Keep these distinct
from STOBJBYNAME/STTHISBYNAME assignments, which can call inherited setters.
Own writes preserve accumulator and explicit receiver on normal completion,
but may throw and are not pure operations. Strings require regional resolution.

First add a reviewed typed lift for own stores/field definitions and expose its
sites alongside class creation evidence using the existing one-method decode.
Then prove receiver identity against constructor this, including copies, joins,
super initialization and exceptional paths. Only proven instance definitions
may populate class fields. Conditional writes retain guards/order; getter/setter
assignments are not declaration evidence. No source field initializer may move
across super calls or observable operations.

Tests first for supported encodings, operand roles/widths/frame bounds and
ordinary assignment exclusion. Record output keeps receiver and write-kind
evidence explicit. This first primitive does not complete field recovery.

Entry-receiver proof uses copy propagation over the normal-entry straight-line
prefix, stopping before the earliest incoming branch/handler target and at any
non-fallthrough or unsupported operation. Scalar slot copies preserve identity;
reviewed register definitions kill it. Own writes preserve receiver and acc.
No shared register-effect coverage is widened, so heap-escape analyses retain
their previous rejection boundaries. Time is O(instructions + registers), space
O(registers + own-write sites), capped by a byte budget. This is a prerequisite,
not a replacement for CFG/SSA joins, derived-this initialization or class-owner
proof. `entry_this` does not imply a non-Hole instance or a field declaration.

The optional ABI this register and the frame's thisObj are distinct storage.
Pinned EcmaInterpreter and InterpreterAssembly GetThis both read thisObj.
LDTHIS therefore recovers entry-frame identity even without an ABI this slot,
or after that register copy is overwritten. Unknown super/resume effects still
stop propagation; no post-super identity is inferred from this rule.

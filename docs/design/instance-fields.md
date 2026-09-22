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

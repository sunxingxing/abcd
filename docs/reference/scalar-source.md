# Scalar JS Expression Output

`WriteScalarExpression` renders typed scalar IR as JavaScript expressions. It is
not a bytecode listing, method wrapper or record-output pipeline. The caller must
supply declared, initialized local binding names for accumulator/register slots;
source scoping, SSA binding identity and declaration recovery are not inferred
from those names. Only referenced names are inspected, not the entire register
name array.

Operators are selected by enums at the source serialization boundary. Binary
expressions retain operand order. Parentheses avoid precedence changes and the
special exponentiation restriction on an unparenthesized negative base. Unary
ToBoolean renders `!!`, bitwise complement `~`, and ToNumber unary `+`.
Increment/decrement update a private parameter to preserve source slots and
ToNumeric behavior; see [numeric-updates.md](numeric-updates.md).

Names are checked as conservative strict-safe ASCII identifiers, including a
reserved-word check. Unicode/escaped names and expression fragments are rejected,
not renamed or injected. Literal undefined, canonical NaN and infinity use
expressions independent of shadowable global names. Integers and finite binary64
values use locale-independent `std::to_chars`; negative zero is explicit.
Raw binary64 NaNs return `kNaNPayload`: the emitter does not silently erase their
payload/sign bits or introduce dependence on mutable DataView/typed-array globals.

The same render traversal performs preflight and output. Binding, value and budget
failures therefore leave the stream untouched. I/O failures may leave a prefix.
The emitter uses fixed numeric scratch and no heap allocation of its own; work
is linear in referenced name bytes. A caller still owns output storage, combined
function/output budgets and whole-process memory enforcement.

## Verification Scope

The integration fixture decodes actual instruction encodings, lifts them, emits
JS functions around the resulting expressions, and executes them with Node.
Independent JS operator functions check 20 binary and 12 unary encodings over
24-by-24 input pairs including strings, signed zero, BigInt and Symbol. Object
ToPrimitive traces and throwing conversions test evaluation order and errors.
More than 4,000 finite/infinite numeric bit patterns, including subnormal values,
round-trip through emitted source and DataView comparison. A negative numeric
exponentiation base is checked separately.

Unit tests cover invalid names and late operands, reserved identifiers, invalid
enum values, exact/insufficient budgets, stream failures, non-default locale,
NaN rejection and names of length 200, 201, 4096 and 65536. Generated fixture
modules remain build artifacts and are not presented as recovered application
records.

These tests establish the IR-to-JS expression mapping under the stated binding
contract. They are not execution differential tests against Ark and do not prove
historical runtime equivalence or pinned native-fast-path edge cases. Full
function control flow, exceptions, environments, closures, modules and per-record
source output remain unfinished.

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

Actual paired constructors use STOBJBYNAME assignments, not the own-store and
define-field subset: all 101 class-site constructor targets have zero own-write
sites. BreakpointType (Immersive entity 7586) has four STOBJBYNAME writes, while
WindowInfo (8371) has eight, including module-derived values. The unified
PropertyWrite entry now includes ordinary assignments and retains setter-capable
semantics. Constructor ownership is correlated during paired validation;
source integration still needs the corresponding typed class-instance proof.
These assignments must remain in constructor bodies; they cannot be rewritten
as field declarations/initializers merely because their receiver is this.

Fixed receiver calls reuse LiftReceiverCall and preserve private caller register
identity while killing accumulator identity on normal return. No heap property
fact survives by implication, and this does not authorize source .call rewriting.

Local/external module loads now carry typed ModuleSpace and ModuleSlot. These
are module value slots, not string pool entries or module request IDs. Their
normal-completion register effect replaces acc but preserves caller registers
and frame.thisObj, allowing receiver aliases to survive. Loaded values remain
unknown and may be Hole; initialization guards are not removed. Modern narrow
and wide encodings are lifted separately from deprecated name-based accesses.

## Base Constructor Source

WriteBaseConstructor emits a native constructor method with simple parameters,
canonical ABI register initialization and the existing local lexical body
writer. It shares the ordinary function writer's bounded two-pass preflight;
semantic and budget rejection publish no bytes. Assignment statements stay in
execution order, preserving inherited setters and abrupt completion instead of
being hoisted into field initializers.

This is a source component, not a class/record exporter. The caller must prove
the ordinary creation site, matching target/ABI, actual no-extends runtime
heritage, and a stable private enclosing class name. An initial method-kind
attribute does not establish runtime heritage. Derived constructors, unresolved
outer captures, non-simple signatures and unsupported bodies remain rejected
or outside the contract. Record integration must establish these prerequisites
before invoking the writer; metadata-only receiver evidence is insufficient.

Native syntax supplies new-only calls, constructor return substitution,
new.target and prototype descriptor behavior. Tests execute generated methods
in Node, including alternate new.target and inherited setter/throw cases.
Constructor and ordinary-function emission share register/name allocation and
body budgets; no additional whole-output buffer or unbounded cache is added.

## Class Heritage Proof

ProveClassHeritage tracks only Hole (absent parent), null and unknown across
scalar copies inside straight-line regions. The pinned runtime's
RuntimeSetClassInheritanceRelationship in ecmascript/stubs/runtime_stubs-inl.h
sets CLASS_CONSTRUCTOR for Hole, DERIVED_CONSTRUCTOR for null, and validates
other values as constructors. No-extends is therefore not an inference from
the target's initial method kind, and not the enum BASE_CONSTRUCTOR.

All branch and exception-handler entries discard incoming facts. Unknown
instructions and non-fallthrough transfers also discard them; later explicit
definitions may establish fresh facts. Known register effects kill their
destination, while ordinary class creation reads its parent then kills acc.
Private caller-register values survive class creation even when runtime code
executes getters. This does not preserve heap facts or prove class identity.

Sorted incoming targets cost O(E log E); scanning and epoch-tagged registers
cost O(N + R). Resetting an epoch is constant time, avoiding O(N R) clearing.
Register cells, edge targets and output sites share an explicit byte budget.
Class-site serialization matches evidence by PC, including around failed,
deprecated and sendable sites. Failure leaves heritage unknown and reports a
typed scan error at the serialization boundary.

This proof discharges only the inheritance premise of constructor source
integration. Binding identity, member descriptors, constructor target/ABI,
captures, full signature shape and body admission remain separate obligations.

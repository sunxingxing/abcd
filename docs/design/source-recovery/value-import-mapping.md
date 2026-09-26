# Normal Value Import Mapping

Implementation: `src/analysis/value_operands.cc`, `call_operands.cc`,
`operand_view.cc`, `value_import.cc`, `value_correspondence.cc`.
This is a low-level owned-graph import, not a source certificate. The source
route still requires an identity envelope and correspondence verification.

| Typed input | Operand roles in semantic order | Normal definition |
|---|---|---|
| Scalar value assignment | Input, when not array allocation | assignment destination |
| Unary assignment | Input | assignment destination |
| Binary assignment | Left, Right, including repeated equal slots | assignment destination |
| Computed property read | Base, Key | assignment destination |
| NamedRead | Base from accumulator; name retained as pool index | accumulator |
| NamedWrite | Base from object register, StoredValue from accumulator | absent |
| OwnWrite | Base, StoredValue; own-store/DefineField kind, name and IC slot retained | absent; accumulator preserved |
| FunctionCreation | protocol HomeObject only for accumulator-home creation; kind, method pool index, length, home input and IC slot retained | fresh accumulator result |
| LexicalLoad | original environment-relative depth/slot retained; no scalar input | accumulator cell value, not an alias |
| LexicalStore | StoredValue from the actual accumulator/register source; depth/slot retained | absent; accumulator preserved |
| PushEnvironment | slot count and optional scope pool index retained | internal environment identity in accumulator |
| PopEnvironment | implicit environment state transition | absent; accumulator preserved |
| CurrentEnvironment | implicit environment read | internal environment identity in accumulator |
| BranchOperation | Condition from predicate input; predicate enum retained | absent |
| ReturnOperation | ReturnValue; literal undefined remains a literal | absent |
| StringLoad | no value operand; string retained as pool index | accumulator |
| EmptyObjectCreation | no operands; fresh object allocation | accumulator |
| Scalar EmptyArray | no operands; fresh array allocation, not a constant | accumulator |
| HoleGuard | GuardValue, conditional diagnostic-name protocol dependency | absent |
| JumpOperation / NoOperation | none | absent |
| OrdinaryCall | Callee from accumulator, Argument occurrences | accumulator |
| ReceiverCall | Callee from accumulator, Receiver, Argument occurrences | accumulator |
| ConstructorCall | Constructor, Argument occurrences; implicit newTarget equals constructor | accumulator |

Every slot occurrence is checked against its register-effect ordinal and SSA
slot. Constants do not consume SSA reads. A repeated slot produces repeated
uses. Diagnostic name registers are protocol dependencies, not permission to
evaluate them unconditionally. Heap reads/writes and guards retain the supplied
normal and pre-throw effects; each throwing operation has its own exceptional
exit. Empty object/array allocation has no fake constant operand and retains
allocation effects. Unsupported variants never become NoOperation.

InvokeReceiver distinguishes an explicit raw receiver from implicit undefined;
ValueInvoke/ValueConstruct retain argument counts independently of operand-use
counts. Range descriptions stay compact in OperandView, then each input read
gets its own owned use occurrence. Capacity admission includes the expanded SSA
read count instead of limiting calls to the scalar array width. Validation work
is charged to the remaining import/correspondence budget. Invalid ranges and
read-ordinal overflow refuse rather than truncate.

Calls are observable even with unused results; CanErasePureOp rejects them even
if their effect fields are incorrectly empty. Original normal/pre-throw effects,
PCs and exceptional exits remain attached. Import does not authorize direct
eval, member reference reconstruction, argument reordering or .call/.apply
substitution. The source evaluation layer admits calls only with its separate
checked ownership/reference plan and AST correspondence.

Fresh empty aggregates now lower to EmptyObjectExpr/EmptyArrayExpr, not literal
values. Each expression occurrence allocates once. The evaluation planner treats
allocation as ordered even if a malformed effects field is empty, and pure-op
erasure explicitly excludes both allocation variants. Multiple consumers require
a shared snapshot; a sole consumer may inline only under the existing exact
effect-order and ownership proof. The independent operand checker rejects an
array/object substitution even though either is syntactically valid JS.
The printer uses `[]` and parenthesized `({})`, so an unused object expression
cannot accidentally become an empty statement block. This admission depends on
the existing source contract excluding VM resource-exhaustion differences.

Buffered entries are not folded into these empty nodes. The initial LiteralPlan
domain can instead emit an ObjectExpr with ordered computed string data keys.
Computed operand conversion, array holes/length and broader initialization remain
pending; named assignment must not stand in for own-define semantics.

OwnWrite now imports as ValueOwnWrite, distinct from ValueNamedWrite. Both
STOWNBYNAME widths retain their IC slot, name pool index and own-store kind;
DEFINEFIELDBYNAME/DEFINEPROPERTYBYNAME retain the shared pinned DefineField kind.
The original InputSite and profile still distinguish the two encoded instructions.
Read occurrences preserve receiver then accumulator order, normal completion
does not define a new accumulator value, and normal/pre-throw effects plus the
exceptional exit remain attached to the original PC. Correspondence rejects
replacement by ordinary named assignment or mutations of kind/name/cache slot.

Pinned provenance: existing LiftOwnWrite and the locked v7 interpreter handlers
in `ecmascript/interpreter/interpreter-inl.cpp` route own-store through UseOwn
and StOwnByName, and field/property definition through SlowRuntimeStub::DefineField.
Those distinctions are deliberately not flattened by source evaluation:
ValueOwnWrite is admitted only through the freshly checked LiteralPlan domain
described in `literal-initialization.md`; all other receivers/initializers refuse.
Import success is not source success. Import adds no graph traversal, alias approximation or
quadratic per-object scan; import and correspondence retain their existing bound.

FunctionCreation imports both cache widths of DEFINEFUNC/DEFINEMETHOD. The
target is still a caller-regional pool index, not a resolved child or guessed
source function name. Current-function home input is implicit; accumulator home
input is an explicit protocol occurrence with its original SSA read ordinal.
Environment/module/heap reads, fresh allocation and pre-throw effects remain on
the original operation/site. Neither unused results nor empty forged effect
fields authorize pure erasure. Input correspondence checks every metadata field
and read occurrence. Source evaluation deliberately refuses until creation
binding, capture obligations and complete child-tree verification are connected.
Import does not imply an ordinary/sendable target classification or source success.

Lexical/environment operations now retain their typed metadata and original
effect sites in Value IR. Raw lexical load is not a TDZ check, environment push
does not initialize cells to undefined, and pop is not an accumulator overwrite.
The semantic proof's value domains are copied unchanged, including Environment;
ordinary use/domain verification still rejects leaking internal values. Named
scope metadata alone does not establish local variable names or initialization.
Historical register stores retain their actual scalar source, not an acc alias.
The normal input proof's existing profile/flow restrictions still apply.

All five operations are explicitly non-erasable and ordered even with empty
effect fields. Original environment state is bound to the operation origin:
no pass may hoist a lexical access across environment changes or reinterpret a
relative depth against a different scope. Source evaluation still refuses them
until cell binding, dynamic lifetime, capture and AST proofs are connected.
The importer does not promote cell values to register SSA or merge allocations.

Inputs remain borrowed only during import. Output graph, correspondence and
use links are owned. SSA values map once to work IDs; operation results are
indexed by one scan of SSA definitions rather than per-instruction lookup.
Phi inputs retain MethodEntry or source/successor EdgeId and must reach the
phi's block with a matching slot. No handler/resume/normalized owner is guessed.

Additional graph and verifier scratch capacities are admitted before reserve.
The caller must separately admit unchanged body/semantic/proof storage; this
primitive's budget is not evidence of whole-process RSS compliance. Work is
charged across instructions, blocks, edges, values, operands and phi inputs.
Operand import is O(I+B+E+V+U+P), counting expanded argument occurrences in U,
plus the existing normal-flow verifier's cost. It does not perform CFG
restructuring or per-variable graph traversal. Structural use-chain verification
alone does not replace independent input correspondence and normal-flow checks.

Regression specifications: `value_operands_test`, `value_uses_test`,
`value_import_test`, `call_operands_test`, `value_call_import_test`.
None have been executed under the test pause.

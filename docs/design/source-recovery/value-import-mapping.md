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
| BranchOperation | Condition from predicate input; predicate enum retained | absent |
| ReturnOperation | ReturnValue; literal undefined remains a literal | absent |
| StringLoad | no value operand; string retained as pool index | accumulator |
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
exit. Array allocation has no fake constant operand and retains allocation
effects. Unsupported variants never become NoOperation.

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
substitution. The source evaluation layer currently refuses these new variants
until its call ownership/reference proof and AST lowering are implemented.

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

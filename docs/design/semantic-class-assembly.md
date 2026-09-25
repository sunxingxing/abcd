# Shared class assembly

The common AST now connects typed `ClassCreation`, `FunctionCreation` method
sites, `DefineAccessor` and method own-stores to the existing native class
writer. This replaces neither the verified class/body emitters nor the semantic
IR. The original CFG, SSA, value/effect proof and region construction still run
before folding. No source-map or evaluation-source input is used.

## Admission

`FoldClassAssembly` considers a single normal basic block starting at a fresh
class creation. The admitted interval contains only scalar atoms/copies,
string loads, the class's own prototype read and method/accessor installation.
Calls, publication, lexical writes, other property reads, another allocation
and control flow end the interval. Phased exception SSA is not admitted.
Prototype identity remains conditional on validating the literal pool key as
`prototype` at the file/text boundary. No opcode or printed-source dispatch is
used to recognize semantics.

Typed SSA facts carry the exact dynamic class site and static/prototype
placement. A method must have that same HomeObject, be installed exactly once,
and have no uses outside the interval, including uses through copies or phis.
Getter/setter definitions require an explicit false naming flag, one method
and one undefined operand, a constant string key and the correct method length.
Accessors precede ordinary attached methods so the native writer preserves
installation order. Remaining class/register/scalar values are still emitted.

The class expression retains typed member targets, pool keys, placement, kind
and length. The output adapter resolves keys and methods using the same file
and caller pool, then supplies the existing writer's accessors/attached-method
inputs. Its role, record owner, duplicate-key, runtime-name, target attribute,
signature and method-body checks remain mandatory. An unresolved member body
rejects the entire class; no fake body or standalone function substitute is
published. The same native class owns all emitted methods' HomeObjects.

Allocation timing moves only within an unobserved interval under the existing
no-OOM/debugger observation scope. Descriptor definitions do not become normal
assignments or calls through mutable global intrinsics. Accessor normal results
remain receiver-register copies; eliminated method-producing/copy/store PCs
and accessor-definition PCs map to the generated class span via typed origin
instruction IDs. This is currently a coarse class span, not member-body PC
mapping across separately emitted methods.

## Cost and ownership

SSA last-use and fact arrays plus one reusable removed-instruction vector are
admitted before allocation. Phi inputs mark escaping uses. Candidates advance
to their first barrier on success or failure, avoiding repeated suffix scans.
Work is linear in instructions, reads and phi inputs apart from the SSA
facade's indexed lookup costs; scratch is O(V + I). Retained member vectors
grow geometrically, admit both old and new storage at growth, and share one
remaining AST budget. Array-fold retained bytes are deducted before class
admission. Resolved UTF-16 names and member arrays reduce the class-body budget;
string decoding is bounded by the remaining allowance.

## Pasteboard evidence and next gate

ABC-only inspection of record entry 9915 yields six attached prototype members:
getter/setter pairs from methods 9860/9971 and 10556/10666, followed by ordinary
methods 10639 and 10278. The shared adapter receives four `ClassAccessor`
entries and two `ClassAttachedMethod` entries. No external method IDs or sample
names are hard-coded into the implementation.

The record now enters these bodies rather than stopping unconditionally at
PC295. Complete class publication remains refused at outer PC264: expansion
reaches method 9804, whose PC24 creates method 9832; the current nested emitter
reports `LinearLexicalError::kInstruction` at child PC18, a further DEFINEFUNC
targeting method 9887. This is the next
concrete function-body gate, not evidence of a complete ProgressBar record.

Both production CLIs compile; read-only review found no confirmed defect.
Tests, generated-JS execution and RSS tracking remain paused by user request.
Deferred regression coverage: direct/copy/phi method escapes, wrong prototype
keys or homes, descriptor/name/length differences, duplicate and reordered
members, interruption by calls/reads/publication, multiple class sites, loops,
source-origin spans and scratch/retained-budget boundaries. Semantic equality
and full pasteboard completion remain unproven.

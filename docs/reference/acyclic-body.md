# Acyclic JS Function Bodies

`WriteAcyclicBody` connects normal CFG construction, bounded block ordering,
accumulator initialization analysis and the existing instruction/string writer.
It emits a function body, not a wrapper, record, module or bytecode interpreter.
Register bindings and initialization still come from the caller's ABI recovery.

## Control-Flow Construction

Let reachable blocks have topological order b0, b1, ..., bn. Open nested JS
label blocks in reverse order bn through b1 before emitting b0. Close the
innermost label immediately before each subsequent block's code. Label bi thus
surrounds precisely the source prefix ending before bi. Every CFG edge points
forward in this order, so `break next_i` exits that prefix and executes bi next.
Conditional edges use the already verified JEQZ/JNEZ predicates, not generic
truthiness. Fallthrough/jump edges use explicit breaks; return/throw are native
JS statements. Physical backward bytecode jumps are allowed when the CFG is
acyclic. Shared successors are emitted once, without copying their effects.

All labels inhabit JavaScript's label namespace, separate from value bindings.
The output is the complete inside of a function and must not be embedded under
caller-defined labels with the same names. This construction favors direct
semantic preservation; future region recovery should simplify labels to idiomatic
if/else structures where proved equivalent. It is not an opcode switch or a
program-counter dispatch loop, and label output is not counted as complete record
decompilation without wrapper/module/binding recovery and validation.

## Safety and Resources

Exception regions are rejected before ordering. Resume edges and reachable
cycles are explicit analysis failures. Normally unreachable blocks can be omitted
only under this exception-free, non-suspending contract. Every reachable supported
accumulator read must be preceded by a value write on all incoming paths. The
state analysis can model Hole, but the statement writer still rejects Hole output.
Unknown operations are not replaced with empty bodies.

Two rendering passes preflight all reachable output and then stream it. Semantic,
binding, constant and output-budget errors leave the destination untouched;
I/O failure or a second-pass file change still requires caller-owned atomic
publication. The scalar/string writer is shared with the straight-line API.

The analysis budget bounds graph construction, then the graph plus simultaneously
live ordering/state storage. The caller separately admits the decoded body,
constant-string allowance and output buffering into the whole-process 1G budget.
Only one transient constant string is live. No whole-body source buffer or second
instruction copy is used. Cost is O(I+B+E+constant lookup/string/output work).
There is no depth-dependent indentation or repeated prefix concatenation.

Label nesting equals reachable blocks minus one, so the default explicit limit
is 128. Exceeding it returns `kNestingLimit`, not a syntactically risky success.
Raising the limit requires validating the target JS parser; this API does not
claim unbounded parser depth. Loop, exceptional and large-region structuring
remain required by the full decompilation goal.

## Verification

Tests execute generated diamond and conditional-throw bodies under Node with
booleans, signed zero, NaN, strings, BigInt, Symbol and objects. A property-read
diamond checks that only the selected getter executes once and preserves thrown
identity. A layout with backward bytecode jumps but no CFG cycle returns the
expected value. Unit tests check exact/short output limits, memory/nesting limits,
uninitialized acc, exception/cycle rejection, and failing streams both with and
without stream exceptions. These are synthetic decoded fixtures, not recovered
application records or an Ark differential execution proof.

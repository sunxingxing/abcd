# Pure module completion

The common module-body printer omits only the terminal ReturnStmt whose value
is absent or exactly an UndefinedLiteral. A symbol read, call, assignment,
sequence or other expression is still evaluated as an expression statement.
Since rules 100, ordinary functions and class methods use the same pure direct
tail rule within their own bodies. Other returns remain, including every
async-generator return operand because it implicitly awaits its value. The
existing module-destination check still rejects early/nested entry returns.

The tree, IR and origin DAG retain the return. Printer events give its statement
and optional literal a zero-width range at the actual module-body end. The
family-emission checker permits this for the typed pure completion at the module
end or within the actual native callable closing block; other
expression/statement ranges remain nonempty. An otherwise empty module
body is handled explicitly. The position serializer retains the original node
kind/ID and adds `emission: module_completion`, rather than inventing an AnchorId
or attributing whitespace to an executable expression. Native callable tails
use the separate serialized label `tail_completion` with the same original IDs.

Tests cover empty completion, ordinary-function returns, effectful sequences
ending in undefined, zero-width position serialization and non-module ranges.
Native callable tests additionally compare return/throw values, effects,
finalizers, suspension and asynchronous completion job order.
A fixed-corpus AST comparison removes exactly one terminal pure completion from
the old module body and verifies that the remaining AST is unchanged. These are
presentation and focused behavior checks, not whole-platform acceptance.

# Pure module completion

The common module-body printer omits only the terminal ReturnStmt whose value
is absent or exactly an UndefinedLiteral. A symbol read, call, assignment,
sequence or other expression is still evaluated as an expression statement.
Ordinary functions and nested class methods retain their returns. The existing
module-destination check still rejects early/nested entry returns.

The tree, IR and origin DAG retain the return. Printer events give its statement
and optional literal a zero-width range at the actual module-body end. The
family-emission checker permits this only for that typed module completion;
other expression/statement ranges remain nonempty. An otherwise empty module
body is handled explicitly. The position serializer retains the original node
kind/ID and adds `emission: module_completion`, rather than inventing an AnchorId
or attributing whitespace to an executable expression.

Tests cover empty completion, ordinary-function returns, effectful sequences
ending in undefined, zero-width position serialization and non-module ranges.
A fixed-corpus AST comparison removes exactly one terminal pure completion from
the old module body and verifies that the remaining AST is unchanged. These are
presentation and focused behavior checks, not whole-platform acceptance.

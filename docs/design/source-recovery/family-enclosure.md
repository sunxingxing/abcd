# Function-Family Lexical Enclosure

`VerifyFamilyEnclosure` checks whether a `GlobalNameExpr` would be shadowed by
the generated family itself. A single-function global check cannot see local
bindings in an enclosing function. Conversely, unioning every enclosing
function's locals incorrectly includes unrelated sibling blocks.

## Source of Scope Facts

`VerifyTreeStructure` records each expression's active `ScopeId` during the same
iterative traversal that verifies unique syntax ownership and scope entry/exit.
Defaults use parameter scope, not body scope. Function expressions therefore
carry a checked creation scope without reinterpreting bytecode or generated JS.
These tables are derived from the unchanged tree and include work/CFG revisions.

The enclosure checker joins each member's scope graph to the scope of its real
parent `FunctionExpr`, using the checked family ownership layout. The existing
capture/module alias scopes remain represented. Alias identity and spelling are
proved by the family/capture/module stages; this pass does not invent them.

All declarations in a scope affect lexical resolution throughout that scope,
including its TDZ before declaration evaluation. A function's inferred runtime
name is not a symbol and does not introduce a self binding. A sibling block's
bindings cannot shadow a function created outside that block.

## Algorithm and Bounds

1. Recheck member structure and retain expression-to-scope tables.
2. Flatten scopes and attach child roots at verified creation scopes.
3. Build CSR child adjacency; assign half-open Euler subtree intervals with an
   explicit stack. Reject cycles, disconnected scopes and invalid parent links.
4. Give each declared name its scope interval. Sort by UTF-16 name and interval
   start, then merge overlapping/adjacent intervals of the same name in place.
5. For each global read, binary-search the interval index using its name and
   scope-entry point. A containing interval is a typed shadowing refusal.

For F members, S scopes, B bindings, E expressions and G global reads, the added
work after structural verification is O(F + S + E + B log B + G log B), multiplied
by actual compared name lengths where relevant. Storage is O(F + S + B + E).
Dense source output and name payload dimensions are not hidden. There is no
recursive C++ traversal or per-read walk through function/scope ancestors.
Scratch, table capacity, comparisons and replaced stage charges share the caller's
byte/work budget. This accounting is not whole-process RSS verification.

## Boundary

The check runs after actual file-family AST/capture assembly and before returning
that construction product. It does not rewrite CFG, basic blocks, binding names
or node origins. Failure reports member and optional NameId, not a textual
semantic tag. The external record/module/global enclosure remains unproved here;
nonzero `global_reads` still requires destination-context checking at publication.
Capture initialization/writes, strictness, receiver and async/exception effects
are separate obligations. Passing this check grants no executable receipt.

`VerifyFamilyDestination` runs the same lexical-range check plus the actual
external `GlobalScope` boundary. Its null context rejects global reads; it is
not equivalent to a caller-proven empty enclosure. Every descendant's global
name is checked against borrowed sorted enclosing/import/parameter indexes and
class/self/host-realm bindings. ASCII admission remains the shared global-name
policy, and byte/UTF-16 comparisons avoid signed-char mismatches. Each comparison
charges work, including its name length. Added lookup work is O(G log D) for D
external bindings; external indexes are neither copied nor scanned per member.

This destination check must be repeated when embedding under a different
enclosure. It does not retain borrowed scope pointers or grant a transferable
publication certificate. The diagnostic family builder still deliberately calls
the internal-only check: there is no inferred record scope or silent promotion
of its draft. Wiring the destination check into an immutable publication owner
and proving strict/module execution remain required next steps.

Tests are isolated in `tests/unit/source/family_enclosure_test.cc`, including
same-function/ancestor shadowing, sibling block exclusion, creation inside a
block, TDZ, runtime names without self binding, malformed parents, deep families,
200/201/geometric sizes and budget exhaustion. They remain uncompiled/unrun while
the user's testing pause is in force.
Destination specifications additionally cover missing context, all external
binding categories, reserved names, destination changes, no-global families,
sibling-block exclusion and exact work limits. These are also uncompiled/unrun.

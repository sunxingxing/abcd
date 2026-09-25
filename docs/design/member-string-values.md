# String Values In Named Stores

The unified source AST admits a named store value as a typed variant of
ScalarValue and StringExpression. A string retains its constant-pool identity
until the common printer resolves and escapes it. This is not general
expression sinking: calls, allocation expressions and coercions are not new
alternatives in this store node.

The existing FoldMemberValue scan now admits a StringLoad immediately before
a NamedWrite in the same basic block. The AST pool index must match the IR
load, the producer must write the accumulator with no language effects and
no exceptional completion, and its SSA definition must have exactly one use,
including exception-handler phi uses. The store must consume that definition,
with the original register receiver and pool key. Merged origins still reject
this transformation.

Reading a string primitive after the private receiver and static key has no
application-visible effect. The Set operation, including setter and Proxy
behavior, remains at its original PC. The removed accumulator assignment keeps
its zero-width source anchor. A live accumulator on a normal or exceptional
continuation prevents folding through the existing use count certificate.

Complexity remains O(I) for I instructions and bounded operand counts; the
variant introduces no dynamic storage. AST admission uses sizeof(SourceStatement),
so any layout change is included in the existing capacity budget. Pool lookup,
escaping and output admission use the existing printer paths.

## Evidence And Limits

Both production CLIs built successfully and read-only review found no concrete
defect. ABC-only export to `build/pasteboard-member-strings-01` emits Logger's
format initialization as one named store with its string literal, without the
accumulator transport. Combined output is 9,724 lines / 165,176 bytes. The eight
incomplete application records and the two host-constructor refusals at PC14
are unchanged. This is output evidence, not runtime-equivalence proof; original
variable recovery and nested final-file position mappings remain incomplete.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include Unicode/escaping and empty strings, setter/Proxy throws,
accumulator uses in handler phis and following blocks, mismatched pool IDs,
merged origins, malformed pool entities and output-budget exhaustion.

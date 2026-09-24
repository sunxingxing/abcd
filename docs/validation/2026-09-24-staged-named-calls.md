# Staged Named Calls

The lexical-body writer now admits one nonterminal `CALLTHIS1..3` or narrow
`CALLTHISRANGE` in a complete straight-line method when its named callee is
spilled and reloaded, the receiver remains unchanged, and the eliminated
callee register is dead or overwritten before reuse. Source keeps the getter
before argument effects and preserves the call result for later instructions.
This extends source admission only for the proved shape, not arbitrary
receiver calls or branch/exception regions.

Synthetic C++ tests cover dead, delayed-overwrite, read/write and live-callee
suffixes, receiver mutation and 200/201/402 instruction lengths. The Node
oracle checks getter count, getter-before-argument order, receiver identity,
nonterminal result use, and poisoned `call`/`apply` properties. The complete
CTest run passed 166/166 tests.

An exploratory scan of the paired corpus found 27 non-tail matcher candidates
in Immersive, MultiCommunityApplication and push-kit modules. No new complete
record source was admitted by this change. Several candidates still fail the
existing numeric value-kind proof after `ADD2`; others need surrounding module
or owner context. Candidate recognition is not a source-completion claim.

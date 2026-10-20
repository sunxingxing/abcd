# Multiple abrupt exits from a synchronous for-of

`build.json` records the pinned DevEco compiler SHA-256, every source/ABC hash
and the exact O0/default commands. All six sources are independent evaluation
oracles. Only ABC paths go to the engine.

- `multi`: one activation, break / return payload / body throw / same-loop
  continue. The tail visit makes continue observably different from fallthrough.
  The engine must publish; `for_of_multiple.mjs` checks270 traces including
  return lookup/call failures, missing/null return, primitive results and body
  Throw priority. Source assignment-form binding is allowed because this
  iteration binding is noncaptured.
- `liveout`: retained last value after break/exhaustion. Still refuses the
  edge-qualified copy obligation; no source-level last-value proof is assumed.
- `labelled`: outer continue/break. Still refuses the loop-boundary obligation.
- `nested`: return/throw crosses inner and outer close. Still refuses the
  current-completion bridge; a failing inner close must become the outer input.
- `async`: await in the body. Still refuses raw/rejection correspondence.
- `catch`: a surrounding effectful user catch. Still refuses mixed ordinary
  try / protocol topology, rather than consuming the catch as close code.

The last five are negative admission tests, not differential passes. See
`docs/design/source-recovery/for-of-multiple-exits.md` for the boundaries.

`tests/tools/for_of_multiple_roundtrip.py` reuses the strict SDK assembly helpers
from `grouping_bytecode.py`. It preserves full method/assembly diffs, counts the
real acquisition/return-read/object-check/receiver-call instructions, and reports
recompiled protocol verification separately from compilation and Node evidence.

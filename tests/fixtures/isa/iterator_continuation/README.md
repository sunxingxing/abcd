# Iterator exhaustion continuation

Pinned O0 JS fixtures for a natural for-of loop with conditional early returns
and a separate exhaustion suffix. `manifest.json` contains the exact compiler
commands and source/ABC SHA-256 identities. Neither corpus source nor paired
source is an engine input.

`choose.js` exercises a conditional return-expression subtree and an exhaustion
return. `variants.js` adds effectful exhaustion, multiple return arms, captures,
throwing return expressions, an ordinary enclosing catch, and property reads.
The Node oracle compares native source with emitted modules: empty input,
exhaustion, both selected values, getter/call/coercion ordering, returned object
identity, and missing/null/throwing/primitive-result iterator `return`.

Run `python3 tests/isa/iterator_continuation_test.py`. Certificate mutations,
register renumbering, harmless copies and edge splitting live in
`tests/unit/source/iterator_continuation_test.cc`. The independent scale runner
uses 100/200/201/400/800 inserted instructions with a fixed protocol count.

# Completion ports

Real JavaScript compiled with the pinned SDK es2abc `--module --merge-abc
--extension=js --opt-level=0` command form. `manifest.json` records exact commands,
compiler identity and source/ABC SHA-256s. ABCs are inputs, never mutated by tests.

`ports.js` has eleven positive exports. The Node oracle compares 6,578 executions
of original and emitted modules: nested close ordering, `return` getters/calls,
non-callable methods and primitive results, pending return/throw priority, ordinary
catches, array-pattern closes, iterable selection, returned identity and async
suspension. This is source/Node differential evidence, not an Ark execution claim.

The other three sources are exact typed refusal tests. Outer labelled continue
is not admitted by assuming different compiler/runtime behavior; acyclic loops
need another source placement representation; the user-try/pattern enclosure is
not admitted by treating interval overlap as compiler ownership.

Run `python3 tests/isa/completion_ports_test.py` and
`python3 tests/isa/completion_ports_scale_test.py`. The C++ unit test additionally
mutates certificate edges, payloads, contexts, guards, closed routes, coverage and
budgets, and rebuilds equivalent register/copy/block variations. An unrelated
multi-exit ordinary try remains refused.

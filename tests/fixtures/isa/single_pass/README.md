# Single-pass iterator activations

Real JavaScript compiled at O0 with the pinned SDK es2abc. The manifest records
exact commands, compiler identity, and source/ABC hashes. Tests never rewrite ABCs.

`once.js` and `variants.js` cover synchronous and async single-pass bodies,
ordinary try/catch with an array pattern, terminal return/throw arms, a logical
iterator parent that also has no natural loop, and suspension before/inside/after
the scopes. The Node oracle checks 6,246 original/emitted executions: empty and
repeated outer activations; fresh iterator acquisition; getter/call order and
receiver identity; returned object identity; missing/noncallable/primitive close
results; pending-throw priority; thrown undefined/null; and rejection logs.
`completion_ports/single_iteration.js` is also promoted to differential coverage.
This is source/Node evidence, not a claim of Ark runtime execution.

`for_await.js` retains the exact unsupported-instruction cause chain. The nested
unconditional-return negative still refuses at the older close-entry continuation
owner, before a missing-natural-loop placement witness exists. This increment
cannot activate from that lower failure. Unrelated multi-exit user tries retain
their existing refusal in the completion-ports fixture.

Run `python3 tests/isa/single_pass_test.py` and
`python3 tests/isa/single_pass_scale_test.py`. The C++ test mutates entry, coverage,
repeat reachability, logical parent, protection context, removed exits and pending
payload/priority, and verifies coherent register/copy/block transformations.
Scaling uses 100/200/201/400/800 extra instructions at fixed protocol count; it
makes no unbounded dense-product claim.

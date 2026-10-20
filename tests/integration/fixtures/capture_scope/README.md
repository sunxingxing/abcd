# Exception-aware deferred capture scope

Original fixture sources, compiled independently by the pinned dynamic DevEco
es2abc. `build.json` records the exact compiler hash, commands and ABC/source
hashes at O0 and default. No source file is an engine input.

`async` reproduces a two-slot environment (receiver and a pending await result),
an escaping async callback reading the pending binding, user catch, success,
rejection and a throw after initialization. `nested` adds conditional native
scope ownership. `sync` checks the same private try-scope proof without an async
protocol. Early reads and retained readers after failed initialization must
throw ReferenceError, while later successful calls have isolated bindings.

All three O0 fixtures publish under the source contract and execute against
their source in Node. Under previous flags, sync/nested publish; async retains
an independent EvaluationError::kOwnership refusal in its callback. Default optimized
inputs retain explicitly asserted pre-existing AsyncCompletionCheck::kCatchStore
or CatchRegionError refusals; they do not count as successful recovery. Output
is recompiled in both compiler modes and compared with original source using
the unnormalized `grouping_bytecode.py` assembly reader. Environment placement,
synthetic names, explicit empty returns, and inverted equivalent branch layout
are reported as differences, not bytecode-equality passes.

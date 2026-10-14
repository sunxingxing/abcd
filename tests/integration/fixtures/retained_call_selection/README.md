# Retained fallback in a member-call argument

Independent JavaScript source oracles; the engine receives only the ABC files.
Generated 2026-10-14 by the installed DevEco SDK compiler (ABC 24.0.0.0):

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=retained --source-file=retained.js --output tests/integration/fixtures/retained_call_selection/retained.abc tests/integration/fixtures/retained_call_selection/retained.js
"$compiler" --module --merge-abc --opt-level=0 --record-name=twice --source-file=twice.js --output tests/integration/fixtures/retained_call_selection/twice.abc tests/integration/fixtures/retained_call_selection/twice.js
```

SHA-256:

- retained.abc: `4d116d8ce55568b0f19ef79bf4a8c82daaa75e9b38841c932feb45589229c863`
- twice.abc: `31cab4e54db393594a5a8793708dbc16f7a06f9462daa688a2613ef0650a137b`

The test first checks actual `CALLARG0`, runtime `ISTRUE` / `ISFALSE`,
conditional branch and `CALLTHIS3` bytecode, with the member lookup before the
child call. `direct`, `copied`, `and` and `nested` keep the selected fallback
from a parameter; no effectful RHS producer lives in that arm. The nested
case adds an ordinary call around the selected value.

The engine exports the entire record under both flag sets. Node compares 104
function/mode results, exception constructors and complete effect traces:
callee getter, earlier argument, child call, optional outer argument call,
later argument and invocation; true/false/null/undefined/object results;
throwing getters/calls; replacing the callee during the child call; receiver
identity and receiver-state mutation. A throwing `valueOf` must never run for
ToBoolean. Twelve mutants duplicate/drop the child, invert short-circuit,
unbind the receiver, move a later getter earlier or repeat the member lookup.
The oracle does not compare error messages or stacks. These functions have no
finally, so there is no original-JS/ABC inlined-finalizer discrepancy to mask.

`twice.abc` is an explicit negative test: two sibling retained-result selections
still fail CallReferenceError::kNotInline. This round does not broaden route
composition or the deferred Round-46 joint predicate/keep-edge admission.

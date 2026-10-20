# Guarded writes to an incoming lexical capture

`stores.abc` is a real DevEco dynamic es2abc module. Only ABC reaches the engine;
`stores.js` is an independent Node oracle. It creates the writer and calls it
before `state` is initialized, catches the TDZ exception, then calls the same
closure after initialization. `observe` exercises guarded reads in a shared
logical decision, including an early call before initialization. The captured binding is a lexical cell, not a module store.

Compiler (pinned SDK):

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=stores --source-file=stores.js \
  --output tests/integration/fixtures/guarded_stores/stores.abc \
  tests/integration/fixtures/guarded_stores/stores.js
```

SHA-256:

```
4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca  es2abc
2ec7ba46e2f76c2b94582a4d4a7cb3d90bfad7d1e03c1dba0ffafaeba91cd6d2  stores.js
05ad5a8002efd74b6c898eb769eab81869d90343c6ee78836c02c1da051bfe33  stores.abc
```

The pinned runtime's `interpreter-inl.cpp:4092-4121` throws on Hole, whereas
`:6118-6154` stores without a TDZ test. The source keeps the original guard as
`state;` before native assignment; the proof excludes resets/effects between
that guard and store. The declaration stays after the early invocation.

`guarded_stores_source_test` exports the fixture and compares engine output
against the original on four ordinary-call scenarios: success, throwing RHS,
undefined RHS, and throwing callee lookup. It checks exception constructors,
trace/order, shared live updates, object identity, function name and length.
Three source mutations expose premature initialization, the wrong stored value,
and reordered RHS effects. Removing just the explicit guard is deliberately
recorded as an equivalent control: native PutValue still performs the TDZ test.
The IR unit test, rather than that equivalent source mutation, rejects a missing
or forged guard certificate. It also refuses a reset, an intervening effect,
a changed catch, wrong storage, missing/duplicated entries and exceeded budgets.

Recompilation is a separate oracle. `grouping_bytecode.py` was run on original
fixture source vs emitted source at default and `--opt-level=0`, with identical
compiler identities and symmetric export-alias adaptation. Both compile and
disassemble, but the strict whole-module check reports **differ**, not pass:

- the existing anonymous-function name-preservation lowering materializes a
  private `{["write"]: function(...) {...}}["write"]`; it preserves the returned
  function's `.name` without exposing that object's identity;
- `observe` uses a Boolean temporary/decision instead of the original logical
  expression; its second guarded read remains conditional;
- the emitted writer retains an explicit guard before assignment; recompiling
  adds PutValue's second guard. The proved interval between guards/store is
  effect-free, so it cannot newly fail;
- the retained RHS/catch temporary changes register allocation and copies;
  guard strings reflect the recovered local name. The oracle does not claim
  message/stack equality; constructors and ordering are tested;
- the existing module-level `let` function binding adds an initialized-binding
  guard. Literal offsets, inline-cache slots and scope/string metadata change
  with these source forms. Module exports and catch coverage remain the same.

No bytecode normalization or weakened equality mode was added. Token-only
comparison also correctly fails: this feature changes source construction, not
just grouping. These outcomes must not be called bytecode/token equality passes.
The generated commands, diffs and complete disassemblies are scratch artifacts
under `build/review/guarded-finish/fixture-roundtrip-final/`.
